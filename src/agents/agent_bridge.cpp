// Bunbun — agent bridge: named-pipe server + session state machine.
//
// bunbun-hook.exe connects once per hook event (see common/pipe.h). Events
// update a per-session table; the most relevant session is published to the
// island as an AgentView. PermissionRequest connections stay open until the
// user clicks Allow / Deny on the island (or the request is withdrawn).
// Event handling manages session lifecycles, active agent pills, and user decisions.
//
// Linux: the same state machine, served over a Unix socket (common/unix_socket.h)
// instead of the named pipe; the transport is the only part that differs.
#include "agent_view.h"

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "../common/json.h"
#ifdef _WIN32
#include "../common/pipe.h"
#include "../island/island.h"
#include "../island/wh_shim.h"
#else
#include "../common/unix_socket.h"
#include "../platform/linux/island_api.h"
#include "../platform/linux/platform.h"
#endif

namespace bb {
namespace {

using json::Value;

constexpr double kFinishedFlashSeconds = 6.0;
constexpr double kErrorFlashSeconds = 8.0;
constexpr double kStaleWorkingSeconds = 15 * 60.0;  // no events for this long: treat as idle
constexpr auto kDecisionWait = std::chrono::seconds(108);
constexpr size_t kMaxPending = 5;

double Now() {
    return IslandNow();
}

#ifdef _WIN32
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
#else
std::wstring Wide(const std::string& s) { return plat::Wide(s); }
std::string Utf8(const std::wstring& w) { return plat::Utf8(w); }

// The tick thread's wake-up, a manual-reset event like the Windows one, so the
// shared code below reads the same on both systems.
struct WakeEvent {
    std::mutex m;
    std::condition_variable cv;
    bool set = false;
};
constexpr unsigned INFINITE = ~0u;
constexpr bool TRUE = true, FALSE = false;
WakeEvent* CreateEventW(void*, bool, bool, void*) { return new WakeEvent; }
void SetEvent(WakeEvent* e) {
    { std::lock_guard lock(e->m); e->set = true; }
    e->cv.notify_all();
}
void ResetEvent(WakeEvent* e) { std::lock_guard lock(e->m); e->set = false; }
void WaitForSingleObject(WakeEvent* e, unsigned ms) {
    std::unique_lock lock(e->m);
    if (ms == INFINITE) e->cv.wait(lock, [&] { return e->set; });
    else e->cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return e->set; });
}
void CloseHandle(WakeEvent* e) { delete e; }
#endif

std::wstring Clip(std::wstring s, size_t max) {
    for (auto& c : s) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
    if (s.size() > max) { s.resize(max - 1); s += L'…'; }
    return s;
}

// An approval's diff keeps its lines (the card shows them and scrolls), up to a cap.
std::wstring ClipLines(std::wstring s, size_t maxLines = 200, size_t maxChars = 12000) {
    std::wstring out;
    size_t lines = 1;
    for (const wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n' && ++lines > maxLines) { out += L"\n\x2026"; break; }
        if (out.size() >= maxChars) { out += L'\x2026'; break; }
        if (c == L'\t') out += L"    ";
        else out += c;
    }
    return out;
}

std::wstring BaseName(const std::string& path) {
    std::wstring w = Wide(path);
    while (!w.empty() && (w.back() == L'/' || w.back() == L'\\')) w.pop_back();
    const size_t slash = w.find_last_of(L"/\\");
    return slash == std::wstring::npos ? w : w.substr(slash + 1);
}

std::wstring AgentDisplayName(const std::string& id) {
    static const std::map<std::string, const wchar_t*> kNames = {
        {"", L"Claude Code"},               {"claude", L"Claude Code"},
        {"claude-code", L"Claude Code"},    {"claude-desktop", L"Claude Desktop"},
        {"vscode", L"VS Code"},             {"visual-studio-code", L"VS Code"},
        {"antigravity", L"Antigravity IDE"},{"antigravity-ide", L"Antigravity IDE"},
        {"cli", L"CLI Agent"},              {"terminal", L"Terminal Agent"},
        {"codex", L"Codex"},                {"gemini", L"Gemini CLI"},
        {"cursor", L"Cursor"},              {"copilot", L"GitHub Copilot"},
        {"opencode", L"OpenCode"},          {"amp", L"Amp"},
    };
    auto it = kNames.find(id);
    return it != kNames.end() ? it->second : Wide(id);
}

std::wstring ToolVerb(const std::string& tool) {
    static const std::map<std::string, const wchar_t*> kVerbs = {
        {"Bash", L"Runs"},          {"PowerShell", L"Runs"},     {"Read", L"Reads"},
        {"Edit", L"Edits"},         {"MultiEdit", L"Edits"},     {"Write", L"Writes"},
        {"NotebookEdit", L"Edits"}, {"Glob", L"Searches"},       {"Grep", L"Searches"},
        {"WebFetch", L"Fetches"},   {"WebSearch", L"Searches web"}, {"Task", L"Delegates"},
        {"Agent", L"Delegates"},    {"TodoWrite", L"Plans"},
        // Antigravity tools:
        {"run_command", L"Runs"},                 {"view_file", L"Reads"},
        {"replace_file_content", L"Edits"},       {"multi_replace_file_content", L"Edits"},
        {"write_to_file", L"Writes"},             {"grep_search", L"Searches"},
        {"list_dir", L"Lists"},                   {"read_url_content", L"Fetches"},
        {"search_web", L"Searches web"},          {"ask_question", L"Asks"},
        {"browser_subagent", L"Browses"},         {"schedule", L"Schedules"},
        {"manage_task", L"Manages task"},         {"generate_image", L"Generates"},
    };
    auto it = kVerbs.find(tool);
    return it != kVerbs.end() ? it->second : Wide(tool);
}

// The most telling argument of a tool call, for the ticker.
std::wstring ToolArgument(const Value& input) {
    for (const char* k : {"command", "description", "CommandLine", "Prompt", "Instruction"}) {
        std::string v = input.getString(k);
        if (!v.empty()) return Clip(Wide(v), 48);
    }
    for (const char* k : {"file_path", "notebook_path", "path", "TargetFile", "AbsolutePath", "SearchPath", "DirectoryPath"}) {
        std::string v = input.getString(k);
        if (!v.empty()) return BaseName(v);
    }
    for (const char* k : {"pattern", "query", "url", "prompt", "Query", "Url"}) {
        std::string v = input.getString(k);
        if (!v.empty()) return Clip(Wide(v), 40);
    }
    return {};
}

std::wstring StepFor(const std::string& tool, const Value* input) {
    std::wstring step = ToolVerb(tool);
    if (input) {
        std::wstring arg = ToolArgument(*input);
        if (!arg.empty()) step += L" · " + arg;
    }
    return step;
}

// Line diff counts: trims the shared head and tail, counts what is left.
// Not a minimal diff, but exact for the usual single-hunk edit.
void CountLines(const std::string& a, const std::string& b, int* added, int* removed) {
    auto split = [](const std::string& s) {
        std::vector<std::string> out;
        if (s.empty()) return out;
        size_t start = 0;
        while (true) {
            size_t nl = s.find('\n', start);
            out.push_back(s.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return out;
    };
    const auto x = split(a), y = split(b);
    size_t head = 0;
    while (head < x.size() && head < y.size() && x[head] == y[head]) ++head;
    size_t tail = 0;
    while (tail < x.size() - head && tail < y.size() - head &&
           x[x.size() - 1 - tail] == y[y.size() - 1 - tail]) ++tail;
    *removed += static_cast<int>(x.size() - head - tail);
    *added += static_cast<int>(y.size() - head - tail);
}

bool DiffCounts(const std::string& tool, const Value& input, int* added, int* removed) {
    *added = *removed = 0;
    if (tool == "Write" || tool == "write_to_file") {
        std::string text = input.getString("content");
        if (text.empty()) text = input.getString("CodeContent");
        CountLines("", text, added, removed);
        return true;
    }
    if (tool == "Edit" || tool == "replace_file_content") {
        std::string oldS = input.getString("old_string");
        std::string newS = input.getString("new_string");
        if (oldS.empty() && newS.empty()) {
            oldS = input.getString("TargetContent");
            newS = input.getString("ReplacementContent");
        }
        CountLines(oldS, newS, added, removed);
        return true;
    }
    if (tool == "MultiEdit" || tool == "multi_replace_file_content") {
        const Value* edits = input.find("edits");
        if (!edits) edits = input.find("ReplacementChunks");
        if (!edits || !edits->isArray()) return false;
        for (const Value& e : edits->arr) {
            std::string oldS = e.getString("old_string");
            std::string newS = e.getString("new_string");
            if (oldS.empty() && newS.empty()) {
                oldS = e.getString("TargetContent");
                newS = e.getString("ReplacementContent");
            }
            CountLines(oldS, newS, added, removed);
        }
        return true;
    }
    return false;
}

// ── State ───────────────────────────────────────────────────────────────────

struct Session {
    std::wstring agent;
    std::wstring project;
    std::wstring step;
    AgentPhase phase = AgentPhase::Thinking;
    int added = -1, removed = -1;
    double updatedAt = 0;
    double phaseSince = 0;
    // Studio agent page.
    double turnStart = 0, turnSeconds = 0;
    int stage = -1;
    std::wstring prompt, command;
    std::vector<std::wstring> output;
    bool commandFailed = false;
    std::wstring diffOld, diffNew;  // the last edit's snippets
};

// Read / edit / run-or-test, for the page's four-step progress. -1 = doesn't move it.
int StageFor(const std::string& tool) {
    static const std::map<std::string, int> kStages = {
        {"Read", 0}, {"Glob", 0}, {"Grep", 0}, {"WebFetch", 0}, {"WebSearch", 0},
        {"view_file", 0}, {"grep_search", 0}, {"list_dir", 0}, {"read_url_content", 0}, {"search_web", 0},
        {"Edit", 1}, {"MultiEdit", 1}, {"Write", 1}, {"NotebookEdit", 1},
        {"replace_file_content", 1}, {"multi_replace_file_content", 1}, {"write_to_file", 1},
        {"Bash", 2}, {"PowerShell", 2}, {"run_command", 2},
    };
    auto it = kStages.find(tool);
    return it != kStages.end() ? it->second : -1;
}

bool IsShellTool(const std::string& tool) {
    return tool == "Bash" || tool == "PowerShell" || tool == "run_command";
}

struct Pending {
    uint64_t id = 0;
    std::string sessionKey;
    std::wstring agent, project, tool, detail;
    std::wstring diffOld, diffNew;
    bool question = false;
    std::string questionText;              // AskUserQuestion: the question, verbatim
    std::vector<std::string> optionLabels; // and its option labels, verbatim
    std::wstring plan;                     // ExitPlanMode: the plan to approve
    std::vector<AgentView::Question> questions;  // AskUserQuestion: every question
    // Resolution, written under g_mutex.
    bool resolved = false;
    AgentDecision decision = AgentDecision::Terminal;
    int answerIndex = -1;
    std::string rawReply;                  // DecideRaw: the hook's reply line, as is
};

// A finished Claude Code session listening for your next message (bunbun-hook
// --reply, an asyncRewake Stop hook). It waits up to kReplyWait; typing in the
// terminal, a new prompt or the session ending withdraws it.
struct ReplyWaiter {
    uint64_t id = 0;
    std::string sessionKey;
    bool done = false;
    std::wstring text;
};
std::vector<std::shared_ptr<ReplyWaiter>> g_replyWaiters;  // guarded by g_mutex
constexpr auto kReplyWait = std::chrono::minutes(30);

std::mutex g_mutex;
std::condition_variable g_decided;
std::map<std::string, Session> g_sessions;
std::deque<std::shared_ptr<Pending>> g_pending;  // FIFO, front is on screen
std::atomic<uint64_t> g_nextId{1};
uint64_t g_generation = 0;
uint64_t g_lastPublishedKey = 0;

// Finished turns (newest first), today's count and the statusLine usage. Global,
// not per session: the island shows them even when nothing is running.
std::deque<AgentView::HistoryItem> g_history;
int g_jobsToday = 0;
int g_jobsDay = -1;  // day of year g_jobsToday counts
float g_usage5h = -1, g_usage7d = -1, g_contextPct = -1;
long long g_usage5hResets = 0, g_usage7dResets = 0;
double g_usageAt = 0;

int DayOfYear() {
#ifdef _WIN32
    SYSTEMTIME t;
    GetLocalTime(&t);
    return t.wYear * 400 + t.wMonth * 32 + t.wDay;
#else
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    return (t.tm_year + 1900) * 400 + (t.tm_mon + 1) * 32 + t.tm_mday;
#endif
}

void FillGlobalsLocked(AgentView* v) {
    if (g_jobsDay != DayOfYear()) { g_jobsDay = DayOfYear(); g_jobsToday = 0; }
    v->jobsToday = g_jobsToday;
    v->history.assign(g_history.begin(), g_history.end());
    v->usage5h = g_usage5h;
    v->usage7d = g_usage7d;
    v->contextPct = g_contextPct;
    v->usage5hResets = g_usage5hResets;
    v->usage7dResets = g_usage7dResets;
    v->usageAt = g_usageAt;
    // The most recent session still listening for a reply, whatever is on screen.
    for (auto it = g_replyWaiters.rbegin(); it != g_replyWaiters.rend(); ++it) {
        if ((*it)->done) continue;
        v->replyId = (*it)->id;
        if (auto s = g_sessions.find((*it)->sessionKey); s != g_sessions.end())
            v->replyTo = s->second.project.empty() ? s->second.agent : s->second.agent + L" · " + s->second.project;
        break;
    }
}

void FillTurnLocked(AgentView* v, const Session& s) {
    v->turnStart = s.turnStart;
    v->turnSeconds = s.turnSeconds;
    v->stage = s.stage;
    v->prompt = s.prompt;
    v->command = s.command;
    v->output = s.output;
    v->commandFailed = s.commandFailed;
    if (v->diffOld.empty() && v->diffNew.empty()) {
        v->diffOld = s.diffOld;
        v->diffNew = s.diffNew;
    }
}

// Percent from a statusLine object field ("used_percentage"), -1 when absent.
float Percent(const Value* obj, const char* key) {
    if (!obj) return -1;
    const Value* v = obj->find(key);
    if (!v || !v->isNumber()) return -1;
    return static_cast<float>(std::clamp(v->num, 0.0, 100.0));
}

std::atomic<bool> g_running{false};
std::thread g_serverThread;
std::thread g_tickThread;
#ifdef _WIN32
HANDLE g_wake = nullptr;  // pokes the tick thread when timing may have changed
#else
WakeEvent* g_wake = nullptr;
int g_listenFd = -1;
#endif

bool IsBusy(AgentPhase p) { return p == AgentPhase::Thinking || p == AgentPhase::Working; }

// Builds the view under g_mutex. Returns a key that changes whenever the view's
// identity changes (so a new approval or phase can trigger the nudge).
AgentView BuildViewLocked(double now, uint64_t* identity) {
    AgentView v;
    v.generation = ++g_generation;
    FillGlobalsLocked(&v);

    if (!g_pending.empty()) {
        const Pending& p = *g_pending.front();
        v.phase = p.question ? AgentPhase::Question : AgentPhase::Approval;
        v.agent = p.agent;
        v.project = p.project;
        v.tool = p.tool;
        v.detail = p.detail;
        v.diffOld = p.diffOld;
        v.diffNew = p.diffNew;
        v.requestId = p.id;
        v.queued = static_cast<int>(g_pending.size()) - 1;
        for (const auto& o : p.optionLabels) v.options.push_back(Clip(Wide(o), 28));
        v.plan = p.plan;
        v.questions = p.questions;
        auto it = g_sessions.find(p.sessionKey);
        v.phaseSince = it != g_sessions.end() ? it->second.phaseSince : now;
        if (it != g_sessions.end()) FillTurnLocked(&v, it->second);
        // The request's own diff only: a command's card showed the session's last edit.
        v.diffOld = p.diffOld;
        v.diffNew = p.diffNew;
        *identity = p.id << 4 | static_cast<uint64_t>(v.phase);
        return v;
    }

    const Session* best = nullptr;
    int busy = 0;
    for (auto& [key, s] : g_sessions) {
        if (IsBusy(s.phase) && now - s.updatedAt < kStaleWorkingSeconds) {
            ++busy;
            if (!best || !IsBusy(best->phase) || s.updatedAt > best->updatedAt) best = &s;
        } else if (!best || !IsBusy(best->phase)) {
            const double flash = s.phase == AgentPhase::Error ? kErrorFlashSeconds : kFinishedFlashSeconds;
            const bool flashing = (s.phase == AgentPhase::Finished || s.phase == AgentPhase::Error) &&
                                  now - s.phaseSince < flash;
            if (flashing && (!best || s.phaseSince > best->phaseSince)) best = &s;
        }
    }
    if (!best) {
        *identity = 0;
        return v;
    }
    v.phase = best->phase;
    v.agent = best->agent;
    v.project = best->project;
    v.step = best->step;
    v.added = best->added;
    v.removed = best->removed;
    v.sessions = busy;
    v.phaseSince = best->phaseSince;
    FillTurnLocked(&v, *best);
    *identity = (std::hash<std::wstring>{}(best->project) << 4) | static_cast<uint64_t>(v.phase);
    return v;
}

void Publish() {
    AgentView view;
    bool nudge = false;
    {
        std::lock_guard lock(g_mutex);
        uint64_t identity = 0;
        view = BuildViewLocked(Now(), &identity);
        nudge = identity != 0 && identity != g_lastPublishedKey;
        g_lastPublishedKey = identity;
    }
    IslandSetAgentView(view, nudge);
    if (g_wake) SetEvent(g_wake);
}

void WithdrawRepliesLocked(const std::string& sessionKey) {
    bool any = false;
    for (auto& w : g_replyWaiters)
        if (w->sessionKey == sessionKey && !w->done) { w->done = true; w->text.clear(); any = true; }
    if (any) g_decided.notify_all();
}

void ResolveLocked(Pending& p, AgentDecision d, int answer) {
    if (p.resolved) return;
    p.resolved = true;
    p.decision = d;
    p.answerIndex = answer;
}

// Withdraws a session's open requests: the user answered in the terminal, or
// the turn ended. The waiting hooks get no reply, which is always safe.
void WithdrawLocked(const std::string& sessionKey) {
    bool any = false;
    for (auto& p : g_pending)
        if (p->sessionKey == sessionKey) { ResolveLocked(*p, AgentDecision::Terminal, -1); any = true; }
    if (any) {
        g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(),
                                       [](const auto& p) { return p->resolved; }),
                        g_pending.end());
        g_decided.notify_all();
    }
}

// Card headline for a permission request ("Wants to run a command").
std::wstring ApprovalTitle(const std::string& tool) {
    static const std::map<std::string, const wchar_t*> kTitles = {
        {"Bash", L"Wants to run a command"},   {"PowerShell", L"Wants to run a command"},
        {"Edit", L"Wants to edit a file"},     {"MultiEdit", L"Wants to edit a file"},
        {"Write", L"Wants to write a file"},   {"NotebookEdit", L"Wants to edit a notebook"},
        {"Read", L"Wants to read a file"},     {"WebFetch", L"Wants to fetch a page"},
        {"WebSearch", L"Wants to search the web"}, {"Task", L"Wants to start a subagent"},
        {"Agent", L"Wants to start a subagent"},
        // Antigravity:
        {"run_command", L"Wants to run a command"},
        {"replace_file_content", L"Wants to edit a file"},
        {"multi_replace_file_content", L"Wants to edit a file"},
        {"write_to_file", L"Wants to write a file"},
        {"ask_question", L"Question from Antigravity"},
        {"browser_subagent", L"Wants to browse web"},
    };
    auto it = kTitles.find(tool);
    if (it != kTitles.end()) return it->second;
    return tool.empty() ? L"Needs your OK" : L"Wants to use " + Wide(tool);
}

std::wstring ApprovalDetail(const std::string& tool, const Value* input) {
    if (!input) return Wide(tool);
    std::string cmd = input->getString("command");
    if (cmd.empty()) cmd = input->getString("CommandLine");
    if (!cmd.empty()) return Clip(Wide(cmd), 220);
    for (const char* k : {"file_path", "notebook_path", "path", "url", "TargetFile", "AbsolutePath", "Url", "Instruction"}) {
        std::string v = input->getString(k);
        if (!v.empty()) return Clip(Wide(v), 220);
    }
    return Clip(Wide(json::Serialize(*input)), 220);
}

// ── Event handling ──────────────────────────────────────────────────────────

// The Bunbun.Show* setting that covers an agent id ("" is Claude Code).
const wchar_t* ShowSettingFor(const std::string& id) {
    if (id.empty() || id == "claude" || id == "claude-code" || id == "claude-desktop") return L"Bunbun.ShowClaude";
    if (id.rfind("antigravity", 0) == 0 || id == "gemini") return L"Bunbun.ShowAntigravity";
    if (id == "cursor") return L"Bunbun.ShowCursor";
    if (id == "vscode" || id == "visual-studio-code" || id == "copilot") return L"Bunbun.ShowCopilot";
    if (id == "codex") return L"Bunbun.ShowCodex";
    return L"Bunbun.ShowOtherAgents";
}

// Returns the reply line for blocking requests ("" = no reply).
std::string HandleEvent(const Value& msg) {
    // An agent the user switched off is ignored completely: no session, no card,
    // and a permission request gets no reply, so the agent asks in its own window.
    if (Wh_GetIntSetting(ShowSettingFor(msg.getString("bunbun_agent"))) == 0) {
        // Forget a session that was open before the switch, so it stops showing.
        bool had = false;
        {
            std::lock_guard lock(g_mutex);
            const std::string key = msg.getString("bunbun_agent") + ":" + msg.getString("session_id");
            WithdrawLocked(key);
            had = g_sessions.erase(key) > 0;
        }
        if (had) Publish();
        return {};
    }

    const std::string event = msg.getString("hook_event_name");
    const std::string agentId = msg.getString("bunbun_agent");
    const std::string sessionKey = agentId + ":" + msg.getString("session_id");
    const std::string tool = msg.getString("tool_name");
    const Value* input = msg.find("tool_input");
    const double now = Now();

    // Claude Code's statusLine (bunbun-hook --statusline): usage only, no session.
    if (event == "StatusLine") {
        {
            std::lock_guard lock(g_mutex);
            const Value* limits = msg.find("rate_limits");
            const Value* five = limits ? limits->find("five_hour") : nullptr;
            const Value* week = limits ? limits->find("seven_day") : nullptr;
            if (const float p = Percent(five, "used_percentage"); p >= 0) g_usage5h = p;
            if (const float p = Percent(week, "used_percentage"); p >= 0) g_usage7d = p;
            if (five) {
                const Value* r = five->find("resets_at");
                if (r && r->isNumber()) g_usage5hResets = static_cast<long long>(r->num);
            }
            if (week) {  // Bunbun: the weekly limit's reset too, for the Agent page
                const Value* r = week->find("resets_at");
                if (r && r->isNumber()) g_usage7dResets = static_cast<long long>(r->num);
            }
            if (const float p = Percent(msg.find("context_window"), "used_percentage"); p >= 0) g_contextPct = p;
            g_usageAt = now;
        }
        Publish();
        return {};
    }

    // bunbun-hook --reply: hold until the user answers from the island (or moves on).
    if (event == "ReplyWait") {
        auto waiter = std::make_shared<ReplyWaiter>();
        {
            std::lock_guard lock(g_mutex);
            WithdrawRepliesLocked(sessionKey);  // one listener per session
            waiter->id = g_nextId++;
            waiter->sessionKey = sessionKey;
            g_replyWaiters.push_back(waiter);
        }
        Publish();
        std::unique_lock lock(g_mutex);
        g_decided.wait_for(lock, kReplyWait, [&] { return waiter->done || !g_running; });
        g_replyWaiters.erase(std::remove(g_replyWaiters.begin(), g_replyWaiters.end(), waiter), g_replyWaiters.end());
        const std::wstring text = waiter->text;
        lock.unlock();
        Publish();
        if (text.empty()) return {};
        Value reply = Value::Object();
        reply["reply"] = Value::String(Utf8(text));
        return json::Serialize(reply);
    }

    std::shared_ptr<Pending> pending;
    {
        std::lock_guard lock(g_mutex);
        Session& s = g_sessions[sessionKey];
        if (event == "UserPromptSubmit" || event == "SessionStart" || event == "SessionEnd" || event == "PreToolUse")
            WithdrawRepliesLocked(sessionKey);  // answered in the terminal, or working again
        if (s.agent.empty()) {
            s.agent = AgentDisplayName(agentId);
            s.phaseSince = now;
        }
        const std::wstring project = BaseName(msg.getString("cwd"));
        if (!project.empty()) s.project = project;
        s.updatedAt = now;
        auto setPhase = [&](AgentPhase p) {
            if (s.phase != p) { s.phase = p; s.phaseSince = now; }
        };

        if (event == "SessionStart") {
            setPhase(AgentPhase::Thinking);
            s.step = L"Session started";
            s.phaseSince = now;
        } else if (event == "UserPromptSubmit") {
            WithdrawLocked(sessionKey);
            setPhase(AgentPhase::Thinking);
            s.step = Clip(Wide(msg.getString("prompt")), 60);
            s.added = s.removed = -1;
            s.turnStart = now;
            s.stage = -1;
            s.prompt = Clip(Wide(msg.getString("prompt")), 120);
            s.command.clear();
            s.output.clear();
            s.commandFailed = false;
            s.diffOld.clear();
            s.diffNew.clear();
        } else if (event == "PreToolUse" || event == "beforeShellExecution") {
            setPhase(AgentPhase::Working);
            s.step = event == "beforeShellExecution" ? L"Runs command" : StepFor(tool, input);
            if (s.turnStart <= 0) s.turnStart = now;
            s.stage = std::max(s.stage, event == "beforeShellExecution" ? 2 : StageFor(tool));
            if (IsShellTool(tool) || event == "beforeShellExecution") {
                std::string cmd = input ? input->getString("command") : std::string();
                if (cmd.empty() && input) cmd = input->getString("CommandLine");
                if (cmd.empty()) cmd = msg.getString("command");
                s.command = Clip(Wide(cmd), 160);
                s.output.clear();
                s.commandFailed = false;
            }
        } else if (event == "PostToolUse" || event == "PostToolUseFailure" || event == "afterFileEdit") {
            WithdrawLocked(sessionKey);
            setPhase(AgentPhase::Working);
            int a = 0, r = 0;
            if (event == "PostToolUseFailure") s.step = StepFor(tool, input) + L" — failed";
            else if (event == "afterFileEdit") s.step = L"Edits file";
            else if (input && DiffCounts(tool, *input, &a, &r)) { s.added = a; s.removed = r; }
            s.stage = std::max(s.stage, StageFor(tool));
            if (input && StageFor(tool) == 1) {
                std::string oldS = input->getString("old_string"), newS = input->getString("new_string");
                if (oldS.empty() && newS.empty()) newS = input->getString("content");
                s.diffOld = Clip(Wide(oldS), 110);
                s.diffNew = Clip(Wide(newS), 110);
            }
            if (IsShellTool(tool)) {
                // The hook keeps only the tail of the output (bunbun_output).
                s.output.clear();
                if (const Value* out = msg.find("bunbun_output"); out && out->isArray())
                    for (const Value& line : out->arr) if (line.isString()) s.output.push_back(Clip(Wide(line.str), 90));
                s.commandFailed = event == "PostToolUseFailure";
            }
        } else if (event == "SubagentStart") {
            s.step = L"Starts a subagent";
        } else if (event == "Notification") {
            std::wstring m = Wide(msg.getString("message"));
            std::wstring lower = m;
            for (auto& c : lower) c = towlower(c);
            if (lower.find(L"rate limit") != std::wstring::npos) {
                setPhase(AgentPhase::Error);
                s.step = Clip(m, 60);
            }
        } else if (event == "Stop") {
            WithdrawLocked(sessionKey);
            const bool wasBusy = IsBusy(s.phase);
            setPhase(AgentPhase::Finished);
            std::string last = msg.getString("last_assistant_message");
            s.step = last.empty() ? L"All done" : Clip(Wide(last), 80);
            s.stage = 3;
            if (wasBusy) {
                s.turnSeconds = s.turnStart > 0 ? now - s.turnStart : 0;
                if (g_jobsDay != DayOfYear()) { g_jobsDay = DayOfYear(); g_jobsToday = 0; }
                ++g_jobsToday;
                AgentView::HistoryItem h;
                h.agent = s.agent;
                h.project = s.project;
                h.prompt = s.prompt.empty() ? s.step : s.prompt;
                h.finishedAt = static_cast<long long>(time(nullptr));
                h.seconds = static_cast<int>(s.turnSeconds);
                g_history.push_front(std::move(h));
                while (g_history.size() > 8) g_history.pop_back();
            }
        } else if (event == "StopFailure") {
            WithdrawLocked(sessionKey);
            setPhase(AgentPhase::Error);
            s.step = L"Stopped with an error";
            s.turnSeconds = s.turnStart > 0 ? now - s.turnStart : 0;
            AgentView::HistoryItem h;
            h.agent = s.agent;
            h.project = s.project;
            h.prompt = s.prompt;
            h.finishedAt = static_cast<long long>(time(nullptr));
            h.seconds = static_cast<int>(s.turnSeconds);
            h.failed = true;
            g_history.push_front(std::move(h));
            while (g_history.size() > 8) g_history.pop_back();
        } else if (event == "SessionEnd") {
            WithdrawLocked(sessionKey);
            g_sessions.erase(sessionKey);
        } else if (event == "PermissionRequest" && msg.getBool("bunbun_shell_gate") &&
                   Wh_GetIntSetting(L"Bunbun.ShellApprovals") == 0) {
            // A shell command gated by the hook (Cursor, Antigravity) while "Ask before
            // shell commands" is off: show the step, and close the pipe without a
            // decision so the agent's own rules apply straight away.
            setPhase(AgentPhase::Working);
            s.step = StepFor(tool, input);
        } else if (event == "PermissionRequest") {
            if (g_pending.size() < kMaxPending) {
                pending = std::make_shared<Pending>();
                pending->id = g_nextId++;
                pending->sessionKey = sessionKey;
                pending->agent = s.agent;
                pending->project = s.project;
                pending->tool = ApprovalTitle(tool);
                pending->detail = ApprovalDetail(tool, input);
                if (input) {
                    std::string oldS = input->getString("old_string");
                    std::string newS = input->getString("new_string");
                    if (oldS.empty() && newS.empty()) {
                        oldS = input->getString("TargetContent");
                        newS = input->getString("ReplacementContent");
                    }
                    // A new file: its whole content is the addition.
                    if (oldS.empty() && newS.empty()) newS = input->getString("content");
                    if (oldS.empty() && newS.empty()) newS = input->getString("CodeContent");
                    if (!oldS.empty() || !newS.empty()) {
                        pending->diffOld = ClipLines(Wide(oldS));
                        pending->diffNew = ClipLines(Wide(newS));
                    }
                }
                if (tool == "ExitPlanMode" && input) {
                    pending->tool = L"Has a plan for you";
                    pending->plan = Wide(input->getString("plan")).substr(0, 20000);
                    pending->detail = L"Approve it, or tell Claude what to change.";
                }
                if ((tool == "AskUserQuestion" || tool == "ask_question") && input) {
                    pending->question = true;
                    // Every question, for the island's step-through card (several
                    // questions, multi-select and free text).
                    if (const Value* all = input->find("questions"); all && all->isArray()) {
                        for (const Value& q : all->arr) {
                            AgentView::Question qq;
                            qq.text = Wide(q.getString("question"));
                            qq.header = Wide(q.getString("header"));
                            qq.multi = q.getBool("multiSelect") || q.getBool("is_multi_select");
                            if (const Value* opts = q.find("options"); opts && opts->isArray())
                                for (const Value& o : opts->arr)
                                    qq.options.push_back(Wide(o.isString() ? o.str : o.getString("label")));
                            pending->questions.push_back(std::move(qq));
                        }
                    }
                    const Value* qs = input->find("questions");
                    // The island answers one single-select question with 2-4
                    // options; anything richer is answered in the terminal.
                    if (qs && qs->isArray() && qs->arr.size() == 1) {
                        const Value& q = qs->arr[0];
                        bool multi = q.getBool("multiSelect") || q.getBool("is_multi_select");
                        if (!multi) {
                            pending->questionText = q.getString("question");
                            pending->detail = Clip(Wide(pending->questionText), 220);
                            if (const Value* opts = q.find("options"); opts && opts->isArray() &&
                                opts->arr.size() >= 2 && opts->arr.size() <= 4) {
                                for (const Value& o : opts->arr) {
                                    std::string lbl = o.isString() ? o.str : o.getString("label");
                                    pending->optionLabels.push_back(lbl);
                                }
                            }
                        }
                    }
                }
                g_pending.push_back(pending);
            }
        }
    }
    Publish();

    if (!pending) return {};

    std::unique_lock lock(g_mutex);
    g_decided.wait_for(lock, kDecisionWait, [&] { return pending->resolved || !g_running; });
    ResolveLocked(*pending, AgentDecision::Terminal, -1);  // no-op unless timed out
    g_pending.erase(std::remove(g_pending.begin(), g_pending.end(), pending), g_pending.end());
    const AgentDecision d = pending->decision;
    const int answer = pending->answerIndex;
    const std::string raw = pending->rawReply;
    lock.unlock();
    Publish();

    if (!raw.empty()) return raw;  // DecideRaw: plan feedback, or answers to several questions

    if (pending->question) {
        if (d != AgentDecision::Allow || answer < 0 || answer >= (int)pending->optionLabels.size()) return {};
        Value reply = Value::Object();
        reply["answers"][pending->questionText] = Value::String(pending->optionLabels[answer]);
        return json::Serialize(reply);
    }
    if (d == AgentDecision::Allow) return "allow";
    if (d == AgentDecision::Deny) return "deny";
    return {};
}

#ifdef _WIN32
std::string Base64(const std::string& in) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const auto b = [&](size_t k) { return k < in.size() ? static_cast<unsigned char>(in[k]) : 0u; };
        const unsigned v = (b(i) << 16) | (b(i + 1) << 8) | b(i + 2);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? kB64[v & 63] : '=';
    }
    return out;
}

Value RectJson(const RECT& r) {
    Value o = Value::Object();
    o["x"] = Value::Number(r.left);
    o["y"] = Value::Number(r.top);
    o["w"] = Value::Number(r.right - r.left);
    o["h"] = Value::Number(r.bottom - r.top);
    return o;
}

// Commands from Bunbun's own settings app or tools (same user only, like every pipe
// client).
std::string HandleCommand(const std::string& cmd, const Value& msg) {
    Value out = Value::Object();
    if (cmd == "snapshot") {
        std::string png;
        RECT screen{}, monitor{}, work{};
        out["shown"] = Value::Bool(IslandSnapshotPng(&png, &screen, &monitor, &work));
        // Bunbun: "display": n previews that display's island (a mirror), when it has one.
        if (!png.empty() && msg.getNumber("display", 0) > 0)
            out["display"] = Value::Bool(IslandDisplayPlace(static_cast<int>(msg.getNumber("display", 0)), &screen, &monitor, &work));
        if (!png.empty()) {
            out["png"] = Value::String(Base64(png));
            out["island"] = RectJson(screen);
            out["monitor"] = RectJson(monitor);
            out["work"] = RectJson(work);
        }
    } else if (cmd == "capture_pages") {
        out["count"] = Value::Number(IslandCapturePages());
    } else if (cmd == "test_open") {  // Bunbun: test hooks (island.h)
        out["count"] = Value::Number(IslandTestOpen(static_cast<int>(msg.getNumber("tab", -1))));
    } else if (cmd == "test_hits") {
        return "{\"hits\":" + IslandTestHits() + "}";
    } else if (cmd == "test_click") {
        out["ok"] = Value::Bool(IslandTestClick(static_cast<int>(msg.getNumber("x", 0)), static_cast<int>(msg.getNumber("y", 0))));
    } else if (cmd == "test_state") {
        out["ok"] = Value::Bool(IslandTestState(msg.getString("kind"), Wide(msg.getString("a")), Wide(msg.getString("b")),
                                                static_cast<int>(msg.getNumber("n", 0)), static_cast<int>(msg.getNumber("seconds", 8)),
                                                Wide(msg.getString("lyrics"))));
    } else if (cmd == "ask") {
        const std::string q = msg.getString("query");
        if (!q.empty()) {
            IslandSubmitAsk(Wide(q));
            out["ok"] = Value::Bool(true);
        }
    }
    return json::Serialize(out);
}
#else
// The settings app asks for page pictures (Island pages); its live preview is
// Windows-only for now.
std::string HandleCommand(const std::string& command) {
    if (command == "capture_pages") {
        Value out = Value::Object();
        out["count"] = Value::Number(std::max(0, IslandCapturePages()));
        return json::Serialize(out);
    }
    if (command == "pick_colour") {  // bunbun --pick-colour, for a keyboard shortcut
        IslandPickColour();
        return "{\"ok\":true}";
    }
    if (command == "toggle") {  // bunbun --toggle: the hide hotkey on Wayland
        IslandToggleHidden();
        return "{\"ok\":true}";
    }
    if (command == "tuck") {  // bunbun --tuck: the tuck hotkey on Wayland
        IslandToggleTucked();
        return "{\"ok\":true}";
    }
    return "{\"shown\":false}";
}
#endif

#ifdef _WIN32
void ServeConnection(HANDLE pipe) {
    std::string line;
    if (bb::pipe::ReadLine(pipe, &line)) {
        Value msg;
        if (json::Parse(line, &msg) && msg.isObject()) {
            const std::string command = msg.getString("bunbun_command");
            const std::string reply = command.empty() ? HandleEvent(msg) : HandleCommand(command, msg);
            if (!reply.empty()) bb::pipe::WriteAll(pipe, reply + "\n");
            FlushFileBuffers(pipe);
        }
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

void ServerLoop() {
    const std::wstring name = bb::pipe::Name();
    // Owner-only DACL: the current user and SYSTEM. No one else can connect.
    const std::wstring sddl = L"D:P(A;;GA;;;" + bb::pipe::CurrentUserSid() + L")(A;;GA;;;SY)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
    SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};

    bool first = true;
    while (g_running) {
        HANDLE h = CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_DUPLEX | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, sd ? &sa : nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            Wh_Log(L"agents: CreateNamedPipe failed (%lu)", GetLastError());
            if (first) break;  // another Bunbun (or a squatter) owns the name
            Sleep(250);
            continue;
        }
        first = false;
        const bool connected = ConnectNamedPipe(h, nullptr) ? true : GetLastError() == ERROR_PIPE_CONNECTED;
        if (!g_running) { CloseHandle(h); break; }
        if (!connected) { CloseHandle(h); continue; }
        std::thread(ServeConnection, h).detach();
    }
    if (sd) LocalFree(sd);
}
#else
void ServeConnection(int fd) {
    std::string line;
    if (usock::PeerIsSameUser(fd) && usock::ReadLine(fd, &line)) {
        Value msg;
        if (json::Parse(line, &msg) && msg.isObject()) {
            const std::string command = msg.getString("bunbun_command");
            const std::string reply = command.empty() ? HandleEvent(msg) : HandleCommand(command);
            if (!reply.empty()) usock::WriteAll(fd, reply + "\n");
        }
    }
    close(fd);
}

// Binds the socket. A socket file nobody answers on is left over from a crash
// and is replaced; one that answers belongs to a running island.
bool Listen() {
    const std::string path = usock::Path();
    sockaddr_un addr;
    if (!usock::FillAddress(path, &addr)) return false;
    if (const int probe = usock::Connect(); probe >= 0) {
        close(probe);
        Wh_Log(L"agents: another island owns %ls", Wide(path).c_str());
        return false;
    }
    unlink(path.c_str());
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    const mode_t old = umask(0077);  // owner-only from the moment it exists
    const bool ok = bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && listen(fd, 16) == 0;
    umask(old);
    if (!ok) {
        Wh_Log(L"agents: can't listen on %ls", Wide(path).c_str());
        close(fd);
        return false;
    }
    g_listenFd = fd;
    return true;
}

void ServerLoop() {
    while (g_running) {
        const int fd = accept4(g_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            break;  // Stop() shut the socket down
        }
        std::thread(ServeConnection, fd).detach();
    }
}
#endif

// Expires the finished/error flash and stale sessions. Sleeps indefinitely
// while there is nothing time-dependent on screen.
void TickLoop() {
    while (g_running) {
        bool timed = false;
        {
            std::lock_guard lock(g_mutex);
            const double now = Now();
            for (auto it = g_sessions.begin(); it != g_sessions.end();) {
                const Session& s = it->second;
                if (now - s.updatedAt > 4 * 3600.0) { it = g_sessions.erase(it); continue; }
                if (IsBusy(s.phase) || now - s.phaseSince < kErrorFlashSeconds + 1) timed = true;
                ++it;
            }
        }
        WaitForSingleObject(g_wake, timed ? 1000 : INFINITE);
        if (g_running) {
            // Re-publishing only matters when the computed view changes; the
            // island ignores identical generations cheaply.
            Publish();
            ResetEvent(g_wake);
        }
    }
}

}  // namespace

namespace agents {

bool Start() {
    if (g_running.exchange(true)) return true;
#ifndef _WIN32
    if (!Listen()) {
        g_running = false;
        return false;
    }
#endif
    g_wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_serverThread = std::thread(ServerLoop);
    g_tickThread = std::thread(TickLoop);
    return true;
}

void Stop() {
    if (!g_running.exchange(false)) return;
    {
        std::lock_guard lock(g_mutex);
        for (auto& p : g_pending) ResolveLocked(*p, AgentDecision::Terminal, -1);
    }
    g_decided.notify_all();
    SetEvent(g_wake);
#ifdef _WIN32
    // Unblock ConnectNamedPipe with a throwaway connection.
    HANDLE h = CreateFileW(bb::pipe::Name().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
#else
    shutdown(g_listenFd, SHUT_RDWR);  // wakes accept()
#endif
    if (g_serverThread.joinable()) g_serverThread.join();
    if (g_tickThread.joinable()) g_tickThread.join();
    CloseHandle(g_wake);
    g_wake = nullptr;
#ifndef _WIN32
    close(g_listenFd);
    g_listenFd = -1;
    unlink(usock::Path().c_str());
#endif
}

void Decide(uint64_t requestId, AgentDecision decision, int answerIndex) {
    {
        std::lock_guard lock(g_mutex);
        for (auto& p : g_pending)
            if (p->id == requestId) ResolveLocked(*p, decision, answerIndex);
    }
    g_decided.notify_all();
}

void DecideRaw(uint64_t requestId, const std::string& replyLine) {
    {
        std::lock_guard lock(g_mutex);
        for (auto& p : g_pending) {
            if (p->id != requestId || p->resolved) continue;
            p->rawReply = replyLine;
            ResolveLocked(*p, AgentDecision::Allow, -1);
        }
    }
    g_decided.notify_all();
}

void SendReply(uint64_t replyId, const std::wstring& text) {
    {
        std::lock_guard lock(g_mutex);
        for (auto& w : g_replyWaiters)
            if (w->id == replyId && !w->done) { w->done = true; w->text = text; }
    }
    g_decided.notify_all();
}

}  // namespace agents
}  // namespace bb
