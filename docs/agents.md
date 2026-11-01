# Connecting coding agents

Bunbun shows what your coding agent is doing on the island, and lets you answer its permission
requests with one click. Connect an agent from **Settings → Assistant & agents**, or from the tray
menu, or on the command line:

```bash
build/bunbun.exe --connect claude
```

Agents: `claude`, `antigravity`, `cursor`, `copilot` (VS Code agent mode and Copilot CLI; `vscode`
is an alias). `--disconnect <agent>` removes only Bunbun's entries, and `--quiet` skips the
confirmation dialog.

Connecting always shows the file it will change, makes a timestamped backup next to it, and merges
Bunbun's entries next to your own.

| Agent | File Bunbun edits | What you get |
|---|---|---|
| Claude Code | `~/.claude/settings.json` | Live ticker, Allow / Deny for permission requests, answers to single questions |
| Antigravity | `~/.gemini/config/hooks.json` (a `bunbun` group) | Live ticker; Allow / Deny for shell commands when **Ask before shell commands** is on |
| Cursor | `~/.cursor/hooks.json` | Live ticker; Allow / Deny for shell commands when **Ask before shell commands** is on |
| GitHub Copilot | `~/.copilot/hooks/bunbun.json` (its own file) | Live ticker, Allow / Deny for permission requests |

Hooks point at `build/bunbun-hook.exe`. Rebuilding in place keeps them valid; if you move the
build folder, connect again.

## How approvals work

1. The agent asks to run something and calls the hook.
2. The island shows a card with the command or diff, and **Allow** / **Deny**.
3. Your click goes back to the agent in its own format.

If you don't answer, close Bunbun, or it isn't running, the hook steps aside and the agent asks you
in its own window. Bunbun never approves anything by itself.

## Troubleshooting

- **Nothing shows up.** Set `BUNBUN_HOOK_DEBUG=1` for the agent's environment; the hook explains on
  stderr why an event didn't reach the island.
- **Antigravity says the hook "is not recognized".** Antigravity escapes quotes before handing the
  command to `cmd`, so hook commands must be unquoted. Connect again with a current build; Bunbun
  writes the path without quotes (and uses the short 8.3 path if it contains spaces).
- **Try it without an agent:**

```bash
python tests/simulate_agent.py demo
```
