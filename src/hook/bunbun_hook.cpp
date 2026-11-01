// bunbun-hook.exe — relays coding-agent hook events to the Bunbun island.
//
//   bunbun-hook.exe [--agent <name>] [<EventName>]   (hook JSON on stdin)
//
// One relay for every agent: Claude Code (no --agent), codex, claude-desktop,
// antigravity, cursor, copilot / vscode. Each agent's dialect is translated to
// the island's canonical events (see Dialects), sent as one JSON line over a
// per-user named pipe, and answered in that agent's own format. Only approval
// requests wait for a reply. If the island is not running, or nobody clicks,
// the agent gets its own "no opinion" and behaves as if Bunbun were not there.
#include <windows.h>

#include <cstdio>
#include <string>

#include "../common/json.h"
#include "../common/pipe.h"

using bb::json::Value;

namespace {

constexpr DWORD kConnectTimeoutMs = 300;
constexpr DWORD kDecisionTimeoutMs = 110 * 1000;  // under the 120 s hook timeout
constexpr size_t kMaxString = 2000;
constexpr size_t kMaxEditString = 256 * 1024;

// Set BUNBUN_HOOK_DEBUG=1 to see on stderr why an event did not arrive.
void Debug(const std::string& msg) {
    static const bool on = GetEnvironmentVariableW(L"BUNBUN_HOOK_DEBUG", nullptr, 0) > 0;
    if (!on) return;
    const std::string line = "bunbun-hook: " + msg + "\n";
    DWORD n = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
}

std::string ReadStdin() {
    std::string data;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    char buf[8192];
    DWORD n = 0;
    while (data.size() < 8u * 1024u * 1024u && ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0)
        data.append(buf, n);
    return data;
}

void WriteStdout(const std::string& s) {
    DWORD n = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s.data(), static_cast<DWORD>(s.size()), &n, nullptr);
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Cuts at a UTF-8 boundary and marks the cut.
void Truncate(std::string* s, size_t max) {
    if (s->size() <= max) return;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>((*s)[cut]) & 0xC0) == 0x80) --cut;
    s->resize(cut);
    *s += "\xE2\x80\xA6";  // …
}

void TruncateStrings(Value* v) {
    if (v->isString()) Truncate(&v->str, kMaxString);
    else if (v->isArray()) for (auto& e : v->arr) TruncateStrings(&e);
    else if (v->isObject()) for (auto& m : v->obj) TruncateStrings(&m.second);
}

bool ValidAgentName(const std::string& n) {
    if (n.empty() || n.size() > 32) return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

// Keeps what the island displays, drops what it never needs.
void Slim(Value* payload, const std::string& event) {
    payload->erase("transcript_path");
    payload->erase("tool_response");
    payload->erase("tool_output");

    const std::string tool = payload->getString("tool_name");
    Value* input = payload->find("tool_input");
    const bool editTool = tool == "Edit" || tool == "MultiEdit" || tool == "Write";
    const bool question = tool == "AskUserQuestion";

    // Diff counts need the edit text; the question card needs every option.
    Value keep;
    if (input && (question || (editTool && event == "PostToolUse"))) {
        keep = *input;
        payload->erase("tool_input");
    }
    TruncateStrings(payload);
    if (!keep.isNull()) {
        if (editTool) {
            for (const char* k : {"old_string", "new_string", "content"})
                if (Value* s = keep.find(k); s && s->isString()) Truncate(&s->str, kMaxEditString);
        }
        (*payload)["tool_input"] = std::move(keep);
    }
}

HANDLE ConnectToIsland() {
    const std::wstring name = bb::pipe::Name();
    const ULONGLONG deadline = GetTickCount64() + kConnectTimeoutMs;
    while (true) {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            if (bb::pipe::ServerIsSameUser(h)) return h;
            Debug("refusing: pipe server runs as another user");
            CloseHandle(h);
            return INVALID_HANDLE_VALUE;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY || GetTickCount64() >= deadline) {
            Debug("island not reachable (error " + std::to_string(err) + ")");
            return INVALID_HANDLE_VALUE;
        }
        WaitNamedPipeW(name.c_str(), 15);
    }
}

struct ReplyWait {
    HANDLE pipe;
    std::string line;
    bool ok = false;
};

DWORD WINAPI ReadReply(void* p) {
    auto* w = static_cast<ReplyWait*>(p);
    w->ok = bb::pipe::ReadLine(w->pipe, &w->line);
    return 0;
}

std::string PermissionReply(const std::string& behavior, const Value* updatedInput) {
    Value decision = Value::Object();
    decision["behavior"] = Value::String(behavior);
    if (behavior == "deny") decision["message"] = Value::String("Denied from Bunbun");
    if (updatedInput) decision["updatedInput"] = *updatedInput;
    Value out = Value::Object();
    Value& hso = out["hookSpecificOutput"];
    hso["hookEventName"] = Value::String("PermissionRequest");
    hso["decision"] = std::move(decision);
    return bb::json::Serialize(out) + "\n";
}

// Only accept answers that fit the questions that were asked.
bool AnswersFit(const Value& input, const Value& answers) {
    const Value* qs = input.find("questions");
    if (!qs || !qs->isArray() || !answers.isObject() || answers.obj.size() != qs->arr.size()) return false;
    for (const Value& q : qs->arr) {
        const Value* a = answers.find(q.getString("question"));
        const Value* opts = q.find("options");
        if (!a || !opts || !opts->isArray()) return false;
        auto isLabel = [&](const std::string& s) {
            for (const Value& o : opts->arr) if (o.getString("label") == s) return true;
            return false;
        };
        if (a->isString()) { if (!isLabel(a->str)) return false; }
        else if (a->isArray() && !a->arr.empty()) { for (const Value& e : a->arr) if (!e.isString() || !isLabel(e.str)) return false; }
        else return false;
    }
    return true;
}

// ── Dialects ────────────────────────────────────────────────────────────────
// Every agent speaks its own hook dialect: event names, payload fields and,
// above all, what it expects on stdout. The hook translates each one into the
// island's canonical Claude Code-style event, so a session looks the same on
// the island whichever tool runs it, and answers in that tool's own format.
//
// The one rule: nothing that allows anything is printed unless a human clicked
// Allow on the island. With no decision every agent gets its own "no opinion"
// (silence, "ask", or an empty object), exactly as if Bunbun were not there.
enum class Dialect { Claude, Antigravity, Cursor, Copilot };

Dialect DialectFor(const std::string& agent) {
    if (agent == "antigravity" || agent == "antigravity-ide") return Dialect::Antigravity;
    if (agent == "cursor") return Dialect::Cursor;
    if (agent == "copilot" || agent == "vscode" || agent == "visual-studio-code") return Dialect::Copilot;
    return Dialect::Claude;  // Claude Code, Claude Desktop, Codex, generic CLI agents
}

// Claude Code agents whose PermissionRequest takes the hookSpecificOutput reply.
bool ClaudeTakesDecisions(const std::string& agent) {
    return agent.empty() || agent == "codex" || agent == "claude-desktop";
}

const Value* FirstOf(const Value& v, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        if (const Value* f = v.find(k); f && !f->isNull()) return f;
    return nullptr;
}

std::string FirstString(const Value& v, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        if (const Value* f = v.find(k); f && f->isString() && !f->str.empty()) return f->str;
    return {};
}

// Shell-running tools across agents (Claude names, Antigravity, Cursor, Copilot).
bool IsShellTool(const std::string& t) {
    return t == "Bash" || t == "PowerShell" || t == "run_command" || t == "run_in_terminal" || t == "runInTerminal" ||
           t == "shell" || t == "bash" || t == "powershell" || t == "terminal" || t == "execute_command";
}

// Maps an agent's tool names and argument keys onto Claude Code's, which the
// island knows how to label ("Runs · npm test", "Edits · main.cpp").
void CanonicalTool(Value* payload) {
    std::string& name = (*payload)["tool_name"].str;
    (*payload)["tool_name"].type = Value::Type::String;
    static const std::pair<const char*, const char*> kNames[] = {
        {"run_command", "Bash"},        {"run_in_terminal", "Bash"},   {"runInTerminal", "Bash"},
        {"shell", "Bash"},              {"bash", "Bash"},              {"execute_command", "Bash"},
        {"powershell", "PowerShell"},   {"view_file", "Read"},         {"read_file", "Read"},
        {"readFile", "Read"},           {"view", "Read"},              {"write_to_file", "Write"},
        {"create_file", "Write"},       {"createFile", "Write"},       {"replace_file_content", "Edit"},
        {"multi_replace_file_content", "MultiEdit"}, {"edit_file", "Edit"}, {"editFile", "Edit"},
        {"replace_string_in_file", "Edit"}, {"insert_edit_into_file", "Edit"}, {"str_replace", "Edit"},
        {"grep_search", "Grep"},        {"codebase_search", "Grep"},   {"find_by_name", "Glob"},
        {"list_dir", "Glob"},           {"read_url_content", "WebFetch"}, {"fetch_webpage", "WebFetch"},
        {"search_web", "WebSearch"},
    };
    for (const auto& [from, to] : kNames)
        if (name == from) { name = to; break; }

    Value* input = payload->find("tool_input");
    if (!input || !input->isObject()) return;
    auto alias = [&](const char* canonical, std::initializer_list<const char*> keys) {
        if (input->find(canonical)) return;
        const std::string v = FirstString(*input, keys);
        if (!v.empty()) (*input)[canonical] = Value::String(v);
    };
    alias("command", {"CommandLine", "commandLine", "cmd"});
    alias("file_path", {"AbsolutePath", "TargetFile", "filePath", "path", "file"});
    alias("pattern", {"Query", "query", "SearchPath"});
    alias("url", {"Url"});
}

// Field names differ (session_id / sessionId / conversation_id / conversationId,
// tool_input / toolArgs / toolCall.args ...); this fills the canonical ones.
void CanonicalFields(Value* p) {
    if (p->getString("session_id").empty()) {
        const std::string id = FirstString(*p, {"sessionId", "conversation_id", "conversationId", "generation_id"});
        if (!id.empty()) (*p)["session_id"] = Value::String(id);
    }
    if (p->getString("cwd").empty()) {
        if (const Value* roots = FirstOf(*p, {"workspace_roots", "workspacePaths"});
            roots && roots->isArray() && !roots->arr.empty() && roots->arr[0].isString())
            (*p)["cwd"] = Value::String(roots->arr[0].str);
    }
    if (p->getString("prompt").empty()) {
        const std::string pr = FirstString(*p, {"initialPrompt", "initial_prompt"});
        if (!pr.empty()) (*p)["prompt"] = Value::String(pr);
    }
    if (p->getString("tool_name").empty()) {
        std::string tn = FirstString(*p, {"toolName"});
        if (tn.empty())
            if (const Value* tc = p->find("toolCall"); tc && tc->isObject()) tn = tc->getString("name");
        if (!tn.empty()) (*p)["tool_name"] = Value::String(tn);
    }
    if (!p->find("tool_input")) {
        const Value* args = p->find("toolArgs");
        if (!args)
            if (const Value* tc = p->find("toolCall"); tc && tc->isObject()) args = tc->find("args");
        if (args) {
            Value parsed;
            // Copilot can send toolArgs as a JSON string.
            if (args->isString() && bb::json::Parse(args->str, &parsed) && parsed.isObject()) (*p)["tool_input"] = parsed;
            else if (args->isObject()) (*p)["tool_input"] = *args;
        }
    }
    if (p->find("tool_name")) CanonicalTool(p);
}

// Event name in the agent's dialect -> the island's canonical event.
std::string CanonicalEvent(Dialect d, const std::string& e, const Value& p) {
    static const std::pair<const char*, const char*> kCommon[] = {
        {"sessionStart", "SessionStart"},       {"sessionEnd", "SessionEnd"},
        {"beforeSubmitPrompt", "UserPromptSubmit"}, {"userPromptSubmitted", "UserPromptSubmit"},
        {"PreInvocation", "UserPromptSubmit"},  {"preToolUse", "PreToolUse"},
        {"postToolUse", "PostToolUse"},         {"postToolUseFailure", "PostToolUseFailure"},
        {"afterShellExecution", "PostToolUse"}, {"afterFileEdit", "PostToolUse"},
        {"subagentStart", "SubagentStart"},     {"subagentStop", "SubagentStop"},
        {"agentStop", "Stop"},                  {"stop", "Stop"},
        {"permissionRequest", "PermissionRequest"}, {"notification", "Notification"},
        {"errorOccurred", "StopFailure"},       {"beforeShellExecution", "PreToolUse"},
    };
    std::string out = e;
    for (const auto& [from, to] : kCommon)
        if (e == from) { out = to; break; }
    // Failures are reported inside the payload by some agents.
    if (out == "Stop") {
        const std::string status = FirstString(p, {"status"});
        if (status == "error" || !FirstString(p, {"error"}).empty()) out = "StopFailure";
    }
    if (out == "PostToolUse" && d == Dialect::Antigravity && !FirstString(p, {"error"}).empty()) out = "PostToolUseFailure";
    return out;
}

// Turns the agent-specific shapes that carry no tool_name into tool events.
void SynthesizeTool(Dialect d, const std::string& rawEvent, Value* p) {
    if (d != Dialect::Cursor) return;
    if (rawEvent == "beforeShellExecution" || rawEvent == "afterShellExecution") {
        (*p)["tool_name"] = Value::String("Bash");
        Value in = Value::Object();
        in["command"] = Value::String(p->getString("command"));
        (*p)["tool_input"] = std::move(in);
    } else if (rawEvent == "afterFileEdit") {
        (*p)["tool_name"] = Value::String("MultiEdit");
        Value in = Value::Object();
        in["file_path"] = Value::String(p->getString("file_path"));
        if (const Value* edits = p->find("edits")) in["edits"] = *edits;
        (*p)["tool_input"] = std::move(in);
    }
}

// What the agent reads on stdout. `decision` is the island's reply line:
// "allow", "deny", {"answers":...}, or empty when nobody decided.
std::string Reply(Dialect d, const std::string& agent, const std::string& rawEvent, const std::string& event,
                  bool gated, const std::string& decision, const Value& originalInput) {
    switch (d) {
        case Dialect::Antigravity:
            // "{}" on PreToolUse reads as a denial in Antigravity; "ask" hands the
            // choice to its own prompt (and the user's Always Allow).
            if (rawEvent == "PreToolUse") {
                if (gated && decision == "allow") return R"({"decision":"auto_approve"})";
                if (gated && decision == "deny") return R"({"decision":"deny","reason":"Denied from Bunbun"})";
                return R"({"decision":"ask"})";
            }
            return "{}";
        case Dialect::Cursor:
            if (rawEvent == "beforeShellExecution" && gated) {
                if (decision == "allow") return R"({"permission":"allow"})";
                if (decision == "deny")
                    return R"({"permission":"deny","user_message":"Denied from Bunbun","agent_message":"The user denied this command in Bunbun."})";
            }
            return {};  // no opinion: Cursor's own rules apply
        case Dialect::Copilot:
            // Copilot fails closed, so a permission request always gets valid JSON.
            if (event == "PermissionRequest") {
                if (decision == "allow") return R"({"permissionDecision":"allow"})";
                if (decision == "deny") return R"({"permissionDecision":"deny","permissionDecisionReason":"Denied from Bunbun"})";
                return R"({"permissionDecision":"ask"})";
            }
            return "{}";
        case Dialect::Claude:
            if (event != "PermissionRequest" || !ClaudeTakesDecisions(agent) || decision.empty()) return {};
            if (decision == "allow") return PermissionReply("allow", nullptr);
            if (decision == "deny") return PermissionReply("deny", nullptr);
            {
                Value reply;
                const Value* answers = nullptr;
                if (bb::json::Parse(decision, &reply) && (answers = reply.find("answers")) && originalInput.isObject() &&
                    AnswersFit(originalInput, *answers)) {
                    Value updated = originalInput;
                    updated["answers"] = *answers;
                    return PermissionReply("allow", &updated);
                }
            }
            return {};
    }
    return {};
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::string agent;
    std::string rawEvent;
    for (int i = 1; i < argc; ++i) {
        std::string a = Utf8(argv[i]);
        if (a == "--agent" && i + 1 < argc) agent = Utf8(argv[++i]);
        else if (!a.empty() && a[0] != '-') rawEvent = a;
    }
    if (!ValidAgentName(agent)) agent.clear();
    const Dialect dialect = DialectFor(agent);

    Value payload;
    std::string perr;
    if (!bb::json::Parse(ReadStdin(), &payload, &perr) || !payload.isObject()) {
        Debug("stdin is not a JSON object: " + perr);
        return 0;
    }
    if (rawEvent.empty()) rawEvent = payload.getString("hook_event_name");
    if (rawEvent.empty()) return 0;

    SynthesizeTool(dialect, rawEvent, &payload);
    CanonicalFields(&payload);
    std::string event = CanonicalEvent(dialect, rawEvent, payload);
    const std::string tool = payload.getString("tool_name");

    // Approval cards. Claude Code and Copilot say when they would ask
    // (PermissionRequest). Cursor and Antigravity don't, so a shell command can
    // be gated instead; the island decides whether that gate is on
    // (Bunbun.ShellApprovals) and answers at once when it is off.
    bool gated = false;
    if ((dialect == Dialect::Antigravity && rawEvent == "PreToolUse" && IsShellTool(tool)) ||
        (dialect == Dialect::Cursor && rawEvent == "beforeShellExecution")) {
        event = "PermissionRequest";
        gated = true;
        payload["bunbun_shell_gate"] = Value::Bool(true);
    }
    const bool blocking = event == "PermissionRequest" &&
                          (gated || dialect == Dialect::Copilot ||
                           (dialect == Dialect::Claude && ClaudeTakesDecisions(agent)));

    payload["hook_event_name"] = Value::String(event);
    if (!agent.empty()) payload["bunbun_agent"] = Value::String(agent);
    if (payload.getString("cwd").empty()) {
        wchar_t cwd[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, cwd)) payload["cwd"] = Value::String(Utf8(cwd));
    }

    Value originalInput;
    if (const Value* in = payload.find("tool_input")) originalInput = *in;
    Slim(&payload, event);

    auto finish = [&](const std::string& decision) {
        const std::string out = Reply(dialect, agent, rawEvent, event, gated, decision, originalInput);
        if (!out.empty()) WriteStdout(out.back() == '\n' ? out : out + "\n");
        return 0;
    };

    HANDLE pipe = ConnectToIsland();
    if (pipe == INVALID_HANDLE_VALUE) return finish({});  // island not running
    const bool sent = bb::pipe::WriteAll(pipe, bb::json::Serialize(payload) + "\n");
    Debug((sent ? "sent " : "send failed: ") + event + " (" + rawEvent + ")");
    if (!sent || !blocking) {
        CloseHandle(pipe);
        return finish({});
    }

    ReplyWait wait{pipe};
    HANDLE reader = CreateThread(nullptr, 0, ReadReply, &wait, 0, nullptr);
    const bool done = reader && WaitForSingleObject(reader, kDecisionTimeoutMs) == WAIT_OBJECT_0;
    if (!done || !wait.ok) return finish({});  // nobody decided: the agent's own prompt takes over
    CloseHandle(reader);
    CloseHandle(pipe);
    return finish(wait.line);
}
