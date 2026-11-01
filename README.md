<div align="center">

<img src="docs/images/mascots.png" alt="Bun and friends: a bunny, cat, dog, fox, panda and robot" width="720">

# Bunbun

**A native Dynamic Island for Windows, with a small bunny named Bun who keeps an eye on your coding agents.**

[Get started](#get-started) · [Features](#features) · [Settings](#the-settings-app) · [Docs](#documentation)

</div>

---

Bunbun is **one island** at the top of your screen (or the bottom, or on every display). It shows
what's playing, your calendar, weather, system stats, Wi-Fi and Bluetooth, notifications, timers and
a file shelf. When your coding agent is working, Bun shows what it's doing; when the agent needs
permission, you answer with one click. When two things happen at once, they share the island side by
side.

It's C++20, Win32 and Direct2D: no Electron, about **30 MB of memory** and **1–3 % of one core**
while idle.

<p align="center">
  <img src="docs/images/settings-overview.png" alt="The Bunbun settings app: overview page with a live island preview" width="820">
</p>

## Features

### Coding agents, on the island

- **Live ticker** of what your agent is doing: `Edits · island.cpp`, `Runs · ./build.sh`, `+14 −0`.
- **One-click Allow / Deny** for permission requests, and answers to single questions.
- Works with **Claude Code, Antigravity, Cursor and GitHub Copilot** (VS Code agent mode and CLI).
  Connect each one in a click; Bunbun backs up the agent's config first and only adds its own entries.
- **Fails safe.** If Bunbun is closed or you don't click, the agent asks in its own window exactly as
  it would without Bunbun. Nothing is ever approved for you.

### Bun, your mascot

Six characters, all drawn in code: **Bun** the bunny, **Miso** the cat, **Biscuit** the dog,
**Ember** the fox, **Bao** the panda and **Bolt** the robot. Each has its own colours and reacts to
what's happening:

<p align="center">
  <img src="docs/images/moods.png" alt="Bun's moods: idle, working, thinking, needs you, celebrate, music, do not disturb, error, sleepy" width="900">
  <br>
  <sub>Idle · Working · Thinking · Needs you · Celebrate · Music · Do not disturb · Error · Sleepy</sub>
</p>

Bun sits beside the clock, puts on headphones when music plays, hops when a task finishes, and
settles in with a snack when you turn on mascot do-not-disturb. You can also load your own art as a
[skin pack](docs/mascots.md#custom-skin-packs).

### Everything else on the island

| | |
|---|---|
| **Music** | Album art, controls, waveform, synced lyrics, and the song title on the collapsed pill |
| **Dashboards** | Hover to expand, scroll to switch: calendar, weather, hardware monitor (CPU, RAM, GPU, disk, network speed), connectivity (Wi-Fi, Bluetooth, Do Not Disturb), file shelf |
| **System flyouts** | Volume, brightness, battery, Bluetooth devices, Caps Lock |
| **Notifications & clipboard** | App toasts and a peek at what you copy |
| **Privacy dots** | Orange while the microphone is in use, green for the camera |
| **Ask Bun** | Press **Ctrl+Alt+B** and type `open notepad`, `volume 40`, `next song`, `timer 25 min`, `weather`, `12*7`, `dnd on`… A local assistant: nothing is sent to any AI service |

## The settings app

Every option has a home, and changes apply to the island the moment you make them.

<table>
  <tr>
    <td width="50%"><img src="docs/images/settings-appearance.png" alt="Appearance: shapes and themes"><br><sub><b>Appearance.</b> Pill, macOS-style notch or Windows 11 shape, themes, and a live preview of your real island on your wallpaper.</sub></td>
    <td width="50%"><img src="docs/images/settings-displays.png" alt="Position and displays"><br><sub><b>Position & displays.</b> Any screen, or all of them, with its own top or bottom position per display.</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/settings-animations.png" alt="Animations with spring previews"><br><sub><b>Animations.</b> Four motion styles, each previewed with the island's real spring and an overshoot trace.</sub></td>
    <td width="50%"><img src="docs/images/settings-features.png" alt="Features"><br><sub><b>Features.</b> Turn on what you use; disabled features stop their background work completely.</sub></td>
  </tr>
</table>

## Get started

1. **Build it.** You'll need llvm-mingw, Node.js and Python; see [docs/building.md](docs/building.md).

   ```bash
   ./build.sh
   ```

2. **Run it.** Start `build/bunbun.exe`. Bun appears in the tray and the island at the top of your
   screen.
3. **Connect your agent** in **Settings → Assistant & agents**, or from the command line:

   ```bash
   build/bunbun.exe --connect claude
   ```

   See [docs/agents.md](docs/agents.md) for Antigravity, Cursor and Copilot.

| Hotkey | Does |
|---|---|
| Ctrl+Alt+B | Opens Ask Bun |
| Ctrl+Alt+D | Hides or shows the island |

Hover the island to expand it, scroll to step through activities and dashboards, and right-click it
for quick options. Settings live in `%APPDATA%\Bunbun\settings.ini`.

## Documentation

| Doc | What's in it |
|---|---|
| [Building](docs/building.md) | Toolchain, building, tests, the live dev loop |
| [Coding agents](docs/agents.md) | Connecting agents, how approvals work, troubleshooting |
| [Mascots](docs/mascots.md) | Characters, moods, custom skin packs |
| [Architecture](docs/architecture.md) | How the pieces fit, source layout, security model |
| [Contributing](CONTRIBUTING.md) | Code style, where to add things, project rules |
| [Security](SECURITY.md) | Reporting vulnerabilities |
| [Changelog](CHANGELOG.md) | What changed in each version |

## Privacy

No telemetry and no AI service. The network is used only by the weather, lyrics and dictionary
modules, and by web searches you ask Bun for. Agent hooks talk to the island over a local pipe that
only your user account can open.

## Credits & license

Bunbun builds on open-source work; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Released under the [MIT License](LICENSE).
