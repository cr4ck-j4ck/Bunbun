// Bunbun — adds / removes bunbun-hook entries in Claude Code's settings.json.
#pragma once

#include <string>

namespace bb::hooks {

// %CLAUDE_CONFIG_DIR%\settings.json, else %USERPROFILE%\.claude\settings.json.
std::wstring ClaudeSettingsPath();

// True when settings.json already routes events to a bunbun-hook.
bool ClaudeHooksInstalled();

// Merges Bunbun's hook entries (idempotent: old Bunbun entries are replaced,
// everything else is left untouched). Backs the file up first and refuses to
// write if the existing file is not a valid JSON object. `report` receives a
// human-readable result either way.
bool InstallClaudeHooks(std::wstring* report);
bool UninstallClaudeHooks(std::wstring* report);

// %USERPROFILE%\.gemini\config\hooks.json
std::wstring AntigravitySettingsPath();
bool AntigravityHooksInstalled();
bool InstallAntigravityHooks(std::wstring* report);
bool UninstallAntigravityHooks(std::wstring* report);

// %USERPROFILE%\.cursor\hooks.json
std::wstring CursorSettingsPath();
bool CursorHooksInstalled();
bool InstallCursorHooks(std::wstring* report);
bool UninstallCursorHooks(std::wstring* report);

// %USERPROFILE%\.copilot\hooks\bunbun.json (VS Code agent mode and Copilot CLI)
std::wstring CopilotHooksPath();
bool CopilotHooksInstalled();
bool InstallCopilotHooks(std::wstring* report);
bool UninstallCopilotHooks(std::wstring* report);

// %CODEX_HOME%\hooks.json, else %USERPROFILE%\.codex\hooks.json (progress only).
std::wstring CodexHooksPath();
bool CodexHooksInstalled();
bool InstallCodexHooks(std::wstring* report);
bool UninstallCodexHooks(std::wstring* report);

// Claude Code's "model" in settings.json ("" = its default; "opus", "sonnet",
// "haiku" or a full model id).
std::string ClaudeModel();
bool SetClaudeModel(const std::string& model, std::wstring* report);

// Returns the full path to bunbun-hook.exe.
std::wstring HookExePath();

// Formats a command string for any agent runner:
//   "\"C:/path/bunbun-hook.exe\" --agent <agentName> <eventName>"
std::string HookCommandFor(const char* agentName, const char* eventName);
// Antigravity form: no quotes (it passes commands to cmd with \" escaping), short path if spaced.
std::string HookCommandBare(const char* agentName, const char* eventName);

}  // namespace bb::hooks
