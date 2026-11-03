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

For Claude Code, connecting also sets a `statusLine` (`bunbun-hook.exe --statusline`). It prints
the model and your 5-hour, weekly and context use under Claude Code's prompt, and sends the same
numbers to the island's Agent page and usage ring. If you already have a statusLine, Bunbun leaves
it alone and the usage stays empty. Hide the numbers with **Show Claude Code usage limits** in
Settings → Assistant & agents.

For Antigravity, Bunbun reads the model quota Antigravity itself caches on your PC (in its
`state.vscdb`), read-only and once a minute. It shows the model with the least quota left and its
reset time. Turn it off with **Show Antigravity quota**. Cursor keeps no usage numbers on your PC,
so Bunbun can't show its limits.

Each agent has an **On island** switch beside its Connect button. Switched off, Bunbun ignores it
completely: no pill, no cards (it asks in its own window), and no usage. With every agent off, the
Agent page and usage ring disappear and the Home tile shows the weather. **Main agent** picks which
agent the Home tile and the usage ring follow; Auto follows the one you used last.

## Answering from the island

- **Plans** (Claude Code plan mode): the island shows the plan with **Approve** and **Change it…**.
  Change it opens a box where you type what should change; Claude gets it and keeps planning.
- **Questions**: one question with two to four options gets quick buttons. Several questions,
  multi-select or your own answer (**Other…**) step through a card and are sent together.
- **Reply**: connecting Claude Code also adds a background Stop hook (`bunbun-hook.exe --reply`,
  `asyncRewake`). After a task finishes it waits up to 30 minutes; **Reply** on the island sends your
  next message to Claude. Typing in the terminal first cancels it.
- **Model**: the Agent page's model chip switches Claude Code between Default, Opus, Sonnet and Haiku
  (the `model` field in `~/.claude/settings.json`, used by new sessions).
- **Codex**: Connect writes `~/.codex/hooks.json` (or `%CODEX_HOME%\hooks.json`). The island shows
  progress; Codex asks for permission in its own window.

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
