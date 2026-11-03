// Installs agent hooks: one matcher-less entry per event, identified by "bunbun-hook" in its command; the command is
// the quoted exe path with forward slashes, because Claude Code runs hooks
// through Git Bash on Windows.
#include "hook_installer.h"

#include <windows.h>
#include <shlobj.h>

#include <cwctype>
#include <string>

#include "../common/json.h"

namespace bb::hooks {
namespace {

using json::Value;

struct HookEvent {
    const char* name;
    int timeout;  // seconds
};

// PermissionRequest waits for a click on the island, so it gets two minutes.
constexpr HookEvent kEvents[] = {
    {"SessionStart", 10},    {"SessionEnd", 10},         {"UserPromptSubmit", 10},
    {"PreToolUse", 10},      {"PostToolUse", 10},        {"PostToolUseFailure", 10},
    {"PermissionRequest", 120}, {"Notification", 10},    {"Stop", 10},
    {"StopFailure", 10},     {"SubagentStart", 10},      {"SubagentStop", 10},
};

constexpr char kMarker[] = "bunbun-hook";

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string HookCommand(const char* event) {
    return HookCommandFor(nullptr, event);
}

bool ReadFileUtf8(const std::wstring& path, std::string* out, bool* exists) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        *exists = false;
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    }
    *exists = true;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > 16 * 1024 * 1024) { CloseHandle(f); return false; }
    out->resize(static_cast<size_t>(size.QuadPart));
    DWORD n = 0;
    const bool ok = out->empty() || (ReadFile(f, out->data(), (DWORD)out->size(), &n, nullptr) && n == out->size());
    CloseHandle(f);
    return ok;
}

// settings.json -> object. Missing or blank file = {}. Anything else invalid is
// an error: we never overwrite a file we could not understand.
bool Load(const std::wstring& path, Value* root, bool* exists, std::wstring* err) {
    std::string text;
    if (!ReadFileUtf8(path, &text, exists)) { *err = L"Could not read " + path; return false; }
    bool blank = true;
    for (char c : text) if (!isspace((unsigned char)c)) { blank = false; break; }
    if (blank) { *root = Value::Object(); return true; }
    std::string perr;
    if (!json::Parse(text, root, &perr) || !root->isObject()) {
        *err = path + L" is not a valid JSON object (" + Wide(perr) + L"). Nothing was changed.";
        return false;
    }
    return true;
}

bool IsOurs(const Value& entry) {
    const Value* hooks = entry.find("hooks");
    if (!hooks || !hooks->isArray()) return false;
    for (const Value& h : hooks->arr)
        if (h.getString("command").find(kMarker) != std::string::npos) return true;
    return false;
}

// Removes our entries from every event. Fails if the structure is not what
// Claude Code documents, rather than guessing.
bool StripOurs(Value* root, std::wstring* err) {
    Value* hooks = root->find("hooks");
    if (!hooks) return true;
    if (!hooks->isObject()) { *err = L"\"hooks\" in settings.json is not an object. Nothing was changed."; return false; }
    for (auto it = hooks->obj.begin(); it != hooks->obj.end();) {
        Value& list = it->second;
        if (!list.isArray()) { *err = L"hooks." + Wide(it->first) + L" is not an array. Nothing was changed."; return false; }
        const size_t before = list.arr.size();
        std::vector<Value> kept;
        for (Value& e : list.arr) if (!IsOurs(e)) kept.push_back(std::move(e));
        list.arr = std::move(kept);
        // Drop an event key only if we emptied it.
        if (list.arr.empty() && before > 0) it = hooks->obj.erase(it);
        else ++it;
    }
    if (hooks->obj.empty()) root->erase("hooks");
    return true;
}

// Our statusLine forwards Claude Code's usage limits to the island. Someone
// else's statusLine is never replaced or removed.
bool IsOurStatusLine(const Value& root) {
    const Value* line = root.find("statusLine");
    return line && line->isObject() && line->getString("command").find(kMarker) != std::string::npos;
}

std::wstring Timestamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[32];
    swprintf(b, 32, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

// Backup first (never overwriting an older backup), then temp file + rename.
bool Save(const std::wstring& path, const Value& root, bool existed, std::wstring* backup, std::wstring* err) {
    const size_t slash = path.find_last_of(L"\\/");
    SHCreateDirectoryExW(nullptr, path.substr(0, slash).c_str(), nullptr);

    if (existed) {
        const std::wstring base = path + L".bak-bunbun-" + Timestamp();
        *backup = base;
        for (int i = 1; !CopyFileW(path.c_str(), backup->c_str(), TRUE); ++i) {
            if (GetLastError() != ERROR_FILE_EXISTS || i > 50) { *err = L"Could not back up " + path + L". Nothing was changed."; return false; }
            *backup = base + L"-" + std::to_wstring(i);
        }
    }

    const std::string text = json::Serialize(root, 2) + "\n";
    const std::wstring tmp = path + L".bunbun-tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *err = L"Could not write " + tmp; return false; }
    DWORD n = 0;
    const bool ok = WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr) && n == text.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        *err = L"Could not replace " + path + L". Nothing was changed.";
        return false;
    }
    return true;
}

}  // namespace

std::wstring ClaudeSettingsPath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::wstring(buf) + L"\\settings.json";
    PWSTR home = nullptr;
    std::wstring p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home))) p = home;
    CoTaskMemFree(home);
    return p + L"\\.claude\\settings.json";
}

bool ClaudeHooksInstalled() {
    Value root;
    bool exists = false;
    std::wstring err;
    if (!Load(ClaudeSettingsPath(), &root, &exists, &err)) return false;
    const Value* hooks = root.find("hooks");
    if (!hooks || !hooks->isObject()) return false;
    for (const auto& [event, list] : hooks->obj)
        if (list.isArray()) for (const Value& e : list.arr) if (IsOurs(e)) return true;
    return false;
}

bool InstallClaudeHooks(std::wstring* report) {
    const std::wstring path = ClaudeSettingsPath();
    if (GetFileAttributesW(HookExePath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        *report = L"bunbun-hook.exe was not found next to bunbun.exe.";
        return false;
    }
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report) || !StripOurs(&root, report)) return false;

    const bool statusLineFree = !root.find("statusLine") || IsOurStatusLine(root);
    if (statusLineFree) {
        Value line = Value::Object();
        line["type"] = Value::String("command");
        line["command"] = Value::String(HookCommandFor(nullptr, "--statusline"));
        line["padding"] = Value::Number(0);
        root["statusLine"] = std::move(line);
    }

    Value& hooks = root["hooks"];
    for (const HookEvent& ev : kEvents) {
        Value cmd = Value::Object();
        cmd["type"] = Value::String("command");
        cmd["command"] = Value::String(HookCommand(ev.name));
        cmd["timeout"] = Value::Number(ev.timeout);
        Value entry = Value::Object();
        entry["hooks"].push(std::move(cmd));
        hooks[ev.name].push(std::move(entry));
    }
    // Reply from the island: an asyncRewake Stop hook that waits in the background
    // for your next message and hands it to Claude (bunbun-hook --reply).
    {
        Value cmd = Value::Object();
        cmd["type"] = Value::String("command");
        cmd["command"] = Value::String(HookCommandFor(nullptr, "--reply"));
        cmd["asyncRewake"] = Value::Bool(true);
        cmd["timeout"] = Value::Number(30 * 60 + 15);
        Value entry = Value::Object();
        entry["hooks"].push(std::move(cmd));
        hooks["Stop"].push(std::move(entry));
    }

    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun now receives Claude Code events.\n\nUpdated: " + path +
              (backup.empty() ? L"" : L"\nBackup: " + backup) +
              L"\n\nNew Claude Code sessions will show up on the island." +
              (statusLineFree ? L"" : L"\n\nYou already have a statusLine, so it was left alone; "
                                      L"Bunbun's usage ring needs its own statusLine to see your limits.");
    return true;
}

bool UninstallClaudeHooks(std::wstring* report) {
    const std::wstring path = ClaudeSettingsPath();
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;
    if (!exists || (!ClaudeHooksInstalled() && !IsOurStatusLine(root))) { *report = L"No Bunbun hooks were installed."; return true; }
    if (!StripOurs(&root, report)) return false;
    if (IsOurStatusLine(root)) root.erase("statusLine");
    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun hooks removed from " + path + L"\nBackup: " + backup;
    return true;
}

std::wstring AntigravitySettingsPath() {
    PWSTR home = nullptr;
    std::wstring p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home))) p = home;
    CoTaskMemFree(home);
    return p + L"\\.gemini\\config\\hooks.json";
}

bool AntigravityHooksInstalled() {
    Value root;
    bool exists = false;
    std::wstring err;
    if (!Load(AntigravitySettingsPath(), &root, &exists, &err)) return false;
    const Value* bunbun = root.find("bunbun");
    return bunbun && bunbun->isObject();
}

bool InstallAntigravityHooks(std::wstring* report) {
    const std::wstring path = AntigravitySettingsPath();
    if (GetFileAttributesW(HookExePath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        *report = L"bunbun-hook.exe was not found next to bunbun.exe.";
        return false;
    }
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;

    // Create our "bunbun" named hook entry
    Value bunbun = Value::Object();
    bunbun["enabled"] = Value::Bool(true);

    auto makeHandler = [](const std::string& cmdStr, int timeout) {
        Value h = Value::Object();
        h["type"] = Value::String("command");
        h["command"] = Value::String(cmdStr);
        h["timeout"] = Value::Number(timeout);
        return h;
    };

    // Per Antigravity's hooks docs: tool events take matcher groups, lifecycle
    // events take handlers directly. PreToolUse is only hooked for run_command,
    // the one tool Bunbun can gate (Ask before shell commands); every other tool
    // runs untouched and still shows up through PostToolUse.
    auto group = [&](const char* matcher, const char* event, int timeout) {
        Value g = Value::Object();
        g["matcher"] = Value::String(matcher);
        g["hooks"].push(makeHandler(HookCommandBare("antigravity", event), timeout));
        return g;
    };
    bunbun["SessionStart"].push(makeHandler(HookCommandBare("antigravity", "SessionStart"), 10));
    bunbun["PreToolUse"].push(group("run_command", "PreToolUse", 120));
    bunbun["PostToolUse"].push(group("*", "PostToolUse", 10));
    bunbun["Stop"].push(makeHandler(HookCommandBare("antigravity", "Stop"), 10));

    root["bunbun"] = std::move(bunbun);

    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun now receives Antigravity IDE and CLI events.\n\nUpdated: " + path +
              (backup.empty() ? L"" : L"\nBackup: " + backup) +
              L"\n\nActive Antigravity sessions and tools will now show up on the island.";
    return true;
}

bool UninstallAntigravityHooks(std::wstring* report) {
    const std::wstring path = AntigravitySettingsPath();
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;
    if (!exists || !AntigravityHooksInstalled()) { *report = L"No Bunbun hooks were installed in Antigravity."; return true; }
    root.erase("bunbun");
    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun hooks removed from " + path + L"\nBackup: " + backup;
    return true;
}

std::wstring CursorSettingsPath() {
    PWSTR home = nullptr;
    std::wstring p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home))) p = home;
    CoTaskMemFree(home);
    return p + L"\\.cursor\\hooks.json";
}

bool CursorHooksInstalled() {
    Value root;
    bool exists = false;
    std::wstring err;
    if (!Load(CursorSettingsPath(), &root, &exists, &err)) return false;
    const Value* hooks = root.find("hooks");
    if (!hooks || !hooks->isObject()) return false;
    for (const auto& [ev, list] : hooks->obj) {
        if (list.isArray()) {
            for (const Value& item : list.arr) {
                if (item.getString("command").find("bunbun-hook") != std::string::npos) return true;
            }
        }
    }
    return false;
}

bool InstallCursorHooks(std::wstring* report) {
    const std::wstring path = CursorSettingsPath();
    if (GetFileAttributesW(HookExePath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        *report = L"bunbun-hook.exe was not found next to bunbun.exe.";
        return false;
    }
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;

    root["version"] = Value::Number(1);
    Value& hooks = root["hooks"];
    if (!hooks.isObject()) hooks = Value::Object();

    auto makeEntry = [](const std::string& cmdStr, int timeout) {
        Value h = Value::Object();
        h["command"] = Value::String(cmdStr);
        h["timeout"] = Value::Number(timeout);
        return h;
    };

    // Strip previous bunbun entries
    for (auto& [ev, list] : hooks.obj) {
        if (list.isArray()) {
            std::vector<Value> kept;
            for (Value& item : list.arr) {
                if (item.getString("command").find("bunbun-hook") == std::string::npos) kept.push_back(std::move(item));
            }
            list.arr = std::move(kept);
        }
    }

    // Cursor hook events (camelCase, per Cursor's hooks docs). preToolUse is left
    // out on purpose: it is a permission hook, and a reply that isn't valid JSON
    // blocks the tool. beforeShellExecution gets a reply from the hook itself.
    const std::pair<const char*, int> kCursorEvents[] = {
        {"sessionStart", 10},      {"sessionEnd", 10},         {"beforeSubmitPrompt", 10},
        {"beforeShellExecution", 120}, {"afterShellExecution", 10}, {"afterFileEdit", 10},
        {"postToolUse", 10},       {"postToolUseFailure", 10}, {"stop", 10},
    };
    for (const auto& [ev, timeout] : kCursorEvents) hooks[ev].push(makeEntry(HookCommandFor("cursor", ev), timeout));

    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun now receives Cursor agent & Composer events.\n\nUpdated: " + path +
              (backup.empty() ? L"" : L"\nBackup: " + backup) +
              L"\n\nActive Cursor Composer and agent sessions will now show up on the island.";
    return true;
}

bool UninstallCursorHooks(std::wstring* report) {
    const std::wstring path = CursorSettingsPath();
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;
    if (!exists || !CursorHooksInstalled()) { *report = L"No Bunbun hooks were installed in Cursor."; return true; }

    Value* hooks = root.find("hooks");
    if (hooks && hooks->isObject()) {
        for (auto it = hooks->obj.begin(); it != hooks->obj.end();) {
            Value& list = it->second;
            if (list.isArray()) {
                std::vector<Value> kept;
                for (Value& item : list.arr) {
                    if (item.getString("command").find("bunbun-hook") == std::string::npos) kept.push_back(std::move(item));
                }
                list.arr = std::move(kept);
            }
            if (list.arr.empty()) it = hooks->obj.erase(it);
            else ++it;
        }
    }

    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun hooks removed from " + path + L"\nBackup: " + backup;
    return true;
}

std::wstring HookExePath() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring p = self;
    p.resize(p.find_last_of(L"\\/") + 1);
    return p + L"bunbun-hook.exe";
}

// PowerShell needs the call operator to run a quoted path.
std::string HookCommandPowerShell(const char* agentName, const char* eventName) {
    std::string exe = Utf8(HookExePath());
    std::string quoted;
    for (char c : exe) { quoted += c; if (c == '\'') quoted += '\''; }
    return "& '" + quoted + "' --agent " + agentName + " " + eventName;
}

// ── GitHub Copilot: VS Code agent mode and Copilot CLI ──────────────────────
// Both read every *.json in ~/.copilot/hooks (or $COPILOT_HOME/hooks). Bunbun
// owns its own file there, so installing is writing it and removing is
// deleting it; nothing of the user's is merged or touched.
std::wstring CopilotHooksPath() {
    wchar_t buf[MAX_PATH];
    std::wstring dir;
    DWORD n = GetEnvironmentVariableW(L"COPILOT_HOME", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        dir = buf;
    } else {
        PWSTR home = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home))) dir = home;
        CoTaskMemFree(home);
        dir += L"\\.copilot";
    }
    return dir + L"\\hooks\\bunbun.json";
}

bool CopilotHooksInstalled() {
    return GetFileAttributesW(CopilotHooksPath().c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool InstallCopilotHooks(std::wstring* report) {
    if (GetFileAttributesW(HookExePath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        *report = L"bunbun-hook.exe was not found next to bunbun.exe.";
        return false;
    }
    // camelCase events; permissionRequest fires only when Copilot would ask, like
    // Claude Code's PermissionRequest, and waits for a click on the island.
    const std::pair<const char*, int> kEvents[] = {
        {"sessionStart", 10}, {"userPromptSubmitted", 10}, {"preToolUse", 10}, {"permissionRequest", 120},
        {"postToolUse", 10},  {"postToolUseFailure", 10},  {"agentStop", 10},  {"sessionEnd", 5},
        {"notification", 10},
    };
    Value root = Value::Object();
    root["version"] = Value::Number(1);
    Value& hooks = root["hooks"];
    for (const auto& [ev, timeout] : kEvents) {
        Value h = Value::Object();
        h["type"] = Value::String("command");
        h["bash"] = Value::String(HookCommandFor("copilot", ev));
        h["powershell"] = Value::String(HookCommandPowerShell("copilot", ev));
        h["timeoutSec"] = Value::Number(timeout);
        hooks[ev].push(std::move(h));
    }
    const std::wstring path = CopilotHooksPath();
    std::wstring backup;
    if (!Save(path, root, CopilotHooksInstalled(), &backup, report)) return false;
    *report = L"Bunbun now receives GitHub Copilot events (VS Code agent mode and Copilot CLI).\n\nWrote: " + path +
              L"\n\nStart a new chat or Copilot session to pick the hooks up.";
    return true;
}

bool UninstallCopilotHooks(std::wstring* report) {
    const std::wstring path = CopilotHooksPath();
    if (!CopilotHooksInstalled()) { *report = L"No Bunbun hooks were installed for GitHub Copilot."; return true; }
    if (!DeleteFileW(path.c_str())) { *report = L"Could not delete " + path; return false; }
    *report = L"Bunbun hooks removed: deleted " + path;
    return true;
}

// Antigravity runs hook commands through cmd.exe, but escapes embedded quotes
// as \" first. cmd doesn't understand that escaping, so a quoted path becomes a
// program literally named \"D:/...exe\" and every hook fails with "is not
// recognized". So: no quotes at all. A path with spaces uses its 8.3 short form.
std::string HookCommandBare(const char* agentName, const char* eventName) {
    std::wstring path = HookExePath();
    if (path.find(L' ') != std::wstring::npos) {
        wchar_t shortPath[MAX_PATH];
        const DWORD n = GetShortPathNameW(path.c_str(), shortPath, MAX_PATH);
        if (n > 0 && n < MAX_PATH) path = shortPath;
    }
    std::string exe = Utf8(path);
    for (auto& c : exe) if (c == '\\') c = '/';
    std::string cmd = exe + " --agent " + agentName;
    if (eventName && eventName[0]) {
        cmd += " ";
        cmd += eventName;
    }
    return cmd;
}

std::string HookCommandFor(const char* agentName, const char* eventName) {
    std::string exe = Utf8(HookExePath());
    for (auto& c : exe) if (c == '\\') c = '/';
    std::string cmd = "\"" + exe + "\"";
    if (agentName && agentName[0]) {
        cmd += " --agent ";
        cmd += agentName;
    }
    if (eventName && eventName[0]) {
        cmd += " ";
        cmd += eventName;
    }
    return cmd;
}

// ── Codex ────────────────────────────────────────────────────────────────────
// Codex reads command hooks from %CODEX_HOME%\hooks.json (default
// ~/.codex/hooks.json) in Claude Code's shape. Permission requests are answered in
// Codex itself, so these hooks only report progress.
std::wstring CodexHooksPath() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"CODEX_HOME", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::wstring(buf) + L"\\hooks.json";
    PWSTR home = nullptr;
    std::wstring p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home))) p = home;
    CoTaskMemFree(home);
    return p + L"\\.codex\\hooks.json";
}

namespace {
constexpr const char* kCodexEvents[] = {"SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "Stop", "SessionEnd"};
}

bool CodexHooksInstalled() {
    Value root;
    bool exists = false;
    std::wstring err;
    if (!Load(CodexHooksPath(), &root, &exists, &err)) return false;
    const Value* hooks = root.find("hooks");
    if (!hooks || !hooks->isObject()) return false;
    for (const auto& [event, list] : hooks->obj)
        if (list.isArray()) for (const Value& e : list.arr) if (IsOurs(e)) return true;
    return false;
}

bool InstallCodexHooks(std::wstring* report) {
    const std::wstring path = CodexHooksPath();
    if (GetFileAttributesW(HookExePath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        *report = L"bunbun-hook.exe was not found next to bunbun.exe.";
        return false;
    }
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report) || !StripOurs(&root, report)) return false;
    Value& hooks = root["hooks"];
    for (const char* ev : kCodexEvents) {
        Value cmd = Value::Object();
        cmd["type"] = Value::String("command");
        cmd["command"] = Value::String(HookCommandFor("codex", ev));
        cmd["timeout"] = Value::Number(5);
        Value entry = Value::Object();
        entry["hooks"].push(std::move(cmd));
        hooks[ev].push(std::move(entry));
    }
    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun now receives Codex events.\n\nUpdated: " + path +
              (backup.empty() ? L"" : L"\nBackup: " + backup) +
              L"\n\nNew Codex sessions will show up on the island. Codex asks for permission in its own window.";
    return true;
}

bool UninstallCodexHooks(std::wstring* report) {
    const std::wstring path = CodexHooksPath();
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;
    if (!exists || !CodexHooksInstalled()) { *report = L"No Bunbun hooks were installed for Codex."; return true; }
    if (!StripOurs(&root, report)) return false;
    std::wstring backup;
    if (!Save(path, root, exists, &backup, report)) return false;
    *report = L"Bunbun hooks removed from " + path + L"\nBackup: " + backup;
    return true;
}

// ── Claude Code's model ─────────────────────────────────────────────────────
std::string ClaudeModel() {
    Value root;
    bool exists = false;
    std::wstring err;
    if (!Load(ClaudeSettingsPath(), &root, &exists, &err)) return {};
    return root.getString("model");
}

bool SetClaudeModel(const std::string& model, std::wstring* report) {
    const std::wstring path = ClaudeSettingsPath();
    Value root;
    bool exists = false;
    if (!Load(path, &root, &exists, report)) return false;
    if (model.empty()) root.erase("model");
    else root["model"] = Value::String(model);
    std::wstring backup;
    return Save(path, root, exists, &backup, report);
}

}  // namespace bb::hooks
