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
| `src/island/island_core.cpp` | A thin spine (about 2.4k lines): the `Renderer` class shell, the thread lifecycle and the public `Island*` entry points. Everything else is `#include`d from `modules/` |
| `src/island/modules/*.inc` | Pieces split out of the core, included inside the core's namespace: weather, lyrics, Ask Bun panel, connectivity (data + `connectivity_draw.inc`), privacy dots, to-dos (+ `todos_draw.inc`), Focus mode, Ask Bun actions (undo, brightness), calendar link, downloads, the meeting / download pills (`bunbun_pills_draw.inc`) and shared types (`bunbun_types.inc`). The core is also split by topic into 43 files: types and constants (`layout_constants`, `enums_themes`, `settings_struct`, `snapshot_types`, `state_types`, `shared_state`), platform helpers (`dock_colors_monitors`, `overlay_zorder`, `backdrop_dwm`, `display_mirrors`, `icons_images`, `settings_load`), data sources (`media_source`, `audio_battery_system`, `volume_brightness`, `notifications_bluetooth`, `clipboard_capture`, `audio_spectrum`), behaviour (`click_handlers`, `media_transport_focus`, `run_bun`, `context_menu`, `input_hooks`, `file_tray_store`, `file_tray_dnd`, `rest_island_activities`, `overlay_wndproc`, `render_thread`) and the `renderer_*` files that hold the `Renderer` class's methods. Each is included at its original position, so declaration order is unchanged. New modules go here, not into `island_core.cpp`; `*_draw.inc` files are included inside the `Renderer` class |
| `src/island/settings_schema.yaml` | Every setting with its default, options and description |
| `src/island/wh_shim.*` | Settings and state on top of INI files |
| `src/agents/agent_bridge.cpp` | Agent session state machine, served over a named pipe (Windows) or a Unix socket (Linux) |
| `src/agents/hook_installer.*` | Connect / disconnect for each supported agent |
| `src/hook/bunbun_hook.cpp` | The relay agents run; translates each agent's hook format |
| `src/mascot/` | The mascots, their moods, and custom skin packs, drawn with Direct2D |
| `src/assistant/` | Ask Bun's command parser (pure, unit-tested) |
| `src/brain/` | `bunbun-brain.exe`, the optional local AI helper (llama.cpp). The island starts it on demand and talks JSON lines over its stdin/stdout (`modules/brain_module.inc`); see [Local AI](local-ai.md) |
| `src/settings/settings_host.cpp` | The settings window: WebView2 host and JSON bridge |
| `settings-ui/` | The settings UI: React, Vite, Framer Motion |
| `src/main.cpp` | Tray icon, single instance, settings watcher, command-line flags |
| `src/platform/linux/` | The Linux island (GTK, Cairo/Pango, layer-shell), MPRIS, the WebKitGTK settings host, and the Win32 / Direct2D stand-ins that let shared code build on Linux; see [Bunbun on Linux](linux.md) |
| `CMakeLists.txt`, `packaging/linux/` | The Linux build, desktop entry and metadata |
| `tests/` | Unit tests, agent simulator, mascot contact-sheet renderer |

## The island

- A layered top-level window updated with `UpdateLayeredWindowIndirect`, drawn with Direct2D and
  DirectWrite into a DIB.
- Width, height and position move on springs. The render loop sleeps when nothing animates, and
  expensive counters (GPU, network, connectivity) are only sampled while their tab is visible.
- One island, many activities: media, agents, timers, alerts and the idle dashboard. When two
  happen at once they share the island side by side; the mouse wheel steps through live activities
  and then the dashboard tabs.

On Linux the island is a GTK window drawn with Cairo and Pango: a layer-shell surface on Wayland, a
dock window on X11. The mascot is the same code on both systems. See [Bunbun on Linux](linux.md).

## Coding agents

Each agent runs `bunbun-hook.exe --agent <name> <event>` and passes the event JSON on stdin. The
hook normalises the agent's own format (Claude Code, Antigravity, Cursor, GitHub Copilot) to one
shape, sends it to the island over a named pipe (a Unix socket on Linux), and, for approval
requests, waits for the answer and replies in the agent's format.

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
- No telemetry and no Bunbun AI service (Gemini, with the user's own key, is opt-in). Network use is limited to the weather, lyrics and dictionary
  modules, to the calendar link the user adds (HTTPS, every 5 minutes), to web searches you ask Bun for, and to the update check (one request a day to
  `api.github.com/repos/<repo>/releases/latest`, off with `Bunbun.UpdateCheck`). The update check
  only reads; nothing is downloaded or run, and only a `https://github.com/` page is ever opened
  from its reply.
