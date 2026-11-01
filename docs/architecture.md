# How Bunbun fits together

Bunbun is three programs and a web UI:

```
coding agent ──hook──▶ bunbun-hook.exe ──\\.\pipe\bunbun-<SID>──▶ bunbun.exe (island + tray)
                          ▲   one JSON line per event; approvals wait     │
                          └──────── allow / deny / answers ◀──────────────┘

bunbun-settings.exe (WebView2) ──JSON bridge──▶ settings.ini ──file watcher──▶ bunbun.exe
```

## Source layout

| Path | What |
|---|---|
| `src/island/island_core.cpp` | The island: Direct2D renderer, springs, and the system modules (media, calendar, weather, hardware, connectivity, notifications, clipboard, file shelf, timers) |
| `src/island/modules/*.inc` | Pieces split out of the core (weather, lyrics, Ask Bun panel), included inside the core's namespace |
| `src/island/settings_schema.yaml` | Every setting with its default, options and description |
| `src/island/wh_shim.*` | Settings and state on top of INI files |
| `src/agents/agent_bridge.cpp` | Pipe server and agent session state machine |
| `src/agents/hook_installer.*` | Connect / disconnect for each supported agent |
| `src/hook/bunbun_hook.cpp` | The relay agents run; translates each agent's hook format |
| `src/mascot/` | The mascots, their moods, and custom skin packs, drawn with Direct2D |
| `src/assistant/` | Ask Bun's command parser (pure, unit-tested) |
| `src/settings/settings_host.cpp` | The settings window: WebView2 host and JSON bridge |
| `settings-ui/` | The settings UI: React, Vite, Framer Motion |
| `src/main.cpp` | Tray icon, single instance, settings watcher, command-line flags |
| `tests/` | Unit tests, agent simulator, mascot contact-sheet renderer |

## The island

- A layered top-level window updated with `UpdateLayeredWindowIndirect`, drawn with Direct2D and
  DirectWrite into a DIB.
- Width, height and position move on springs. The render loop sleeps when nothing animates, and
  expensive counters (GPU, network, connectivity) are only sampled while their tab is visible.
- One island, many activities: media, agents, timers, alerts and the idle dashboard. When two
  happen at once they share the island side by side; the mouse wheel steps through live activities
  and then the dashboard tabs.

## Coding agents

Each agent runs `bunbun-hook.exe --agent <name> <event>` and passes the event JSON on stdin. The
hook normalises the agent's own format (Claude Code, Antigravity, Cursor, GitHub Copilot) to one
shape, sends it to the island over a named pipe, and, for approval requests, waits for the answer
and replies in the agent's format.

Rules the hook follows:

- Nothing is ever approved unless a person clicked **Allow** on the island.
- If the island isn't running, crashes or nobody answers, the hook defers. The agent asks in its own
  UI exactly as it would without Bunbun.

See [agents.md](agents.md) for connecting each agent.

## Settings

The settings app writes `%APPDATA%\Bunbun\settings.ini`; the island watches the file and reloads.
Values equal to their default are removed from the file rather than written, so changed defaults in
new versions still apply. Runtime state lives in `%LOCALAPPDATA%\Bunbun\state.ini`.

## Security

- The pipe's DACL admits only the current user, and remote clients are rejected. The hook refuses
  to talk to a pipe server running as anyone else.
- The island only answers the exact request it shows; requests are withdrawn when the agent's turn
  ends or you answer in the terminal.
- No telemetry and no AI service. Network use is limited to the weather, lyrics and dictionary
  modules, and to web searches you ask Bun for.
