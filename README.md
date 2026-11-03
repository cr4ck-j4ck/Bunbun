<div align="center">

<img src="docs/images/mascots.png" alt="Bun and friends: a bunny, cat, dog, fox, panda and robot" width="720">

# Bunbun

**A native Dynamic Island for Windows and Linux, with a small bunny named Bun who keeps an eye on your coding agents.**

[Get started](#get-started) · [Studio](#the-studio-island) · [Features](#features) · [Settings](#the-settings-app) · [Docs](#documentation)

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
  <img src="docs/images/studio-home.png" alt="The Studio island: date and time, the Claude Code, Music and Focus cards, and the tab bar" width="640">
</p>

## The Studio island

Open the island and it becomes a small dashboard: the date and time on top, your **Home cards** (by
default your coding agent, what's playing and a focus timer), and a tab bar along the bottom with
every page and a volume slider. Scroll or click a tab to move between pages. Titles, tabs and
highlights follow the colour of the album you're playing; each coding agent keeps its own colour.

<table>
  <tr>
    <td width="50%"><img src="docs/images/studio-music.png" alt="Studio Music page"><br><sub><b>Music.</b> Cover, controls, a seek bar and live bars that move with the song.</sub></td>
    <td width="50%"><img src="docs/images/studio-lyrics.png" alt="Studio Lyrics page"><br><sub><b>Lyrics.</b> Synced lyrics, with the line being sung lighting up.</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/studio-agent.png" alt="Studio Agent page"><br><sub><b>Agent.</b> What your coding agent is doing, tasks done today and your usage limits.</sub></td>
    <td width="50%"><img src="docs/images/studio-calendar.png" alt="Studio Calendar page"><br><sub><b>Calendar.</b> The month, and your next meeting and to-dos beside it.</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/studio-focus.png" alt="Studio Focus page"><br><sub><b>Focus.</b> Pick hours and minutes, or a preset, and start.</sub></td>
    <td width="50%"><img src="docs/images/studio-apps.png" alt="Studio All apps page"><br><sub><b>All apps.</b> Every page, the colour picker and Settings in one grid.</sub></td>
  </tr>
</table>

Choose which pages the island has, put them in any order and pick the three Home cards in
**Settings → Island pages**. Prefer the original look? Switch to **Classic** on the same screen.

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
| **Music** | Album art, controls, live bars, synced lyrics, and the song title or the current lyric line on the small island (it sizes itself to the song) |
| **Dashboards** | Hover to expand, scroll to switch: calendar, weather, hardware monitor (CPU, RAM, GPU, disk, network speed), connectivity (tap Wi-Fi or Bluetooth to toggle, hold it to pick a network or device; battery on laptops), file shelf |
| **System flyouts** | Volume, brightness, battery, Bluetooth devices, Caps Lock |
| **To-dos & reminders** | Ask Bun `add buy milk`, `remind me at 3pm to call Sam` or `remind me every weekday at 9 to stand up`; a To-do tab, a reminder pill with Snooze and Done (quiet in full screen, presentations and meetings), and a To-dos page in Settings to edit and reschedule. Stored on your PC only |
| **Focus mode** | `focus 50`: a timer, the mascot on do-not-disturb, notifications held back, and a summary of what you ticked off |
| **Meetings** | Paste your calendar's private iCal link (Google or Outlook) and the island counts down to your next meeting with a **Join** button |
| **Downloads** | Browser downloads show as they arrive (name, size, speed), then "Downloaded" |
| **Studio layout** | The default ([see above](#the-studio-island)): a header, Home cards, an icon tab bar with a volume slider, and Ask Bun as a chat. Classic, the original island, is one click away |
| **Clipboard & note** | Your last 8 copies (click to copy again) and one quick note with checkboxes |
| **Colour picker** | Pick any pixel on screen with a magnifier and copy it as HEX, RGB or HSL |
| **Focus** | A focus page with an hours/minutes picker, presets, +5 min and an optional nudge when a distracting app comes up |
| **Home your way** | Pick what each Home card shows (agent, music, focus, weather, calendar, battery or system), add up to four links under them, and open any page from All apps |
| **Mirror** | An optional camera preview before a call; the camera runs only while it's on |
| **Tuck away** | Ctrl+Alt+H folds the island to a thin status line; point at it to bring it back. It can also tuck itself while a video or game is full screen |
| **Notifications & clipboard** | App toasts and a peek at what you copy |
| **Local AI (optional)** | Turn on a small language model that runs offline on your PC. Bun then answers free-form questions and works like an agent: "get me ready for a meeting" turns on DND, lowers the volume and opens Teams; "is it warmer in Delhi or Mumbai?" looks both up and compares. No account, no keys; you download the model yourself ([guide](docs/local-ai.md)) |
| **Privacy dots** | Orange while the microphone is in use, green for the camera, blue while the screen is recorded, and a short note naming the app ("Discord is using your microphone") |
| **Ask Bun** | Press **Ctrl+Alt+B** and type `open notepad`, `volume 40`, `next song`, `timer 25 min`, `weather`, `12*7`, `dnd on`, `wifi off`, `brightness 60`, `add buy milk`, `remind me in 20 min to stretch`, `undo`… Up arrow recalls earlier commands; Ctrl+Z undoes the last one A local assistant: nothing is sent to any AI service |

## The settings app

Every option has a home, and changes reach the island the moment you make them. If a change ever
can't be saved, a banner says so and offers to try again.

<p align="center">
  <img src="docs/images/settings-pages.png" alt="Island pages: layout, and the Home cards with a real picture of your island" width="820">
</p>

**Island pages** shows real pictures of your own island. Pick Studio or Classic, choose what each
Home card shows, then switch pages on or off and drag them into order. The pictures keep themselves
up to date: the island saves one whenever you look at a page.

<table>
  <tr>
    <td width="50%"><img src="docs/images/settings-pages-list.png" alt="Island pages: every page with its picture, a switch and a drag handle"><br><sub><b>Pages.</b> Every page with its picture, a switch, and a handle to drag it into place.</sub></td>
    <td width="50%"><img src="docs/images/settings-features.png" alt="Features: a grid of feature switches"><br><sub><b>Features.</b> Grouped switches; click one for its options. What you turn off stops completely.</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/settings-assistant.png" alt="Assistant and agents"><br><sub><b>Assistant & agents.</b> Ask Bun, optional local AI, and connecting Claude Code, Antigravity, Cursor, Copilot and Codex.</sub></td>
    <td width="50%"><img src="docs/images/settings-mascot.png" alt="Mascot studio"><br><sub><b>Mascot studio.</b> Characters, moods, eyes or mascot on the idle island, and your own skin packs.</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/settings-appearance.png" alt="Appearance: shapes and themes"><br><sub><b>Appearance.</b> Pill, notch or Windows 11 shape, themes, and a live preview of your island on your wallpaper.</sub></td>
    <td width="50%"><img src="docs/images/settings-displays.png" alt="Position and displays"><br><sub><b>Position & displays.</b> Any screen, or all of them, top or bottom per display.</sub></td>
  </tr>
</table>

## Download

Grab **`Bunbun-<version>-setup.exe`** from the [latest release](../../releases/latest) and run it;
that one file is all you need. It installs for your account only (no admin prompt) into `%LOCALAPPDATA%\Programs\Bunbun`, adds
Bunbun to the Start menu, and can:

- start Bunbun when you sign in;
- install the WebView2 Runtime if your Windows doesn't have it (the settings window needs it;
  Windows 11 always does);
- set up [Local AI](docs/local-ai.md) for Ask Bun: tick it and the installer downloads a small
  language model (about 1.1 GB, checked against its published hash) and turns it on. Skip it and
  add a model any time later.

Uninstall from **Settings → Apps**. It quits Bunbun and disconnects the coding agents connected to
it first; your settings, skin packs and models stay in your AppData folders.

It runs on Windows 10 and 11 (x64). The app isn't code-signed yet, so SmartScreen may warn the
first time: choose **More info → Run anyway**. Check the download against `SHA256SUMS.txt` on the
release page if you like.

### Linux

Each release also has **`Bunbun-<version>-amd64.deb`** for Debian, Ubuntu and their derivatives
(`sudo apt install ./Bunbun-<version>-amd64.deb`) and **`Bunbun-<version>-x86_64.AppImage`** for
most other distributions. It runs on GNOME and KDE, on Wayland and X11. The Linux version has the
island, clock, music, the mascot, coding agents with Allow / Deny, and the settings app; the other
modules are still being ported. See [Bunbun on Linux](docs/linux.md) for what works where.

## Get started

1. **Run it.** Install it (above), or build it yourself (see
   [docs/building.md](docs/building.md)):

   ```bash
   ./scripts/setup.sh
   ```

   ```bash
   ./build.sh
   ```

2. **Find Bun.** Bun appears in the tray and the island at the top of your screen.
3. **Connect your agent** in **Settings → Assistant & agents**, or from the command line:

   ```bash
   build/bunbun.exe --connect claude
   ```

   See [docs/agents.md](docs/agents.md) for Antigravity, Cursor and Copilot.

| Hotkey | Does |
|---|---|
| Ctrl+Alt+B | Opens Ask Bun |
| Ctrl+Alt+D | Hides or shows the island |
| Ctrl+Alt+H | Tucks the island into a thin line; point at it to bring it back |

Hover the island to expand it, scroll to step through activities and dashboards, and right-click it
for quick options. Settings live in `%APPDATA%\Bunbun\settings.ini`.

## Documentation

| Doc | What's in it |
|---|---|
| [Building](docs/building.md) | Toolchain, building, tests, the live dev loop, the Linux build |
| [Linux](docs/linux.md) | Installing on Linux, what works on which desktop, how the port is built |
| [Coding agents](docs/agents.md) | Connecting agents, how approvals work, troubleshooting |
| [Mascots](docs/mascots.md) | Characters, moods, custom skin packs |
| [Local AI](docs/local-ai.md) | Optional offline AI for Ask Bun: what to download, setup, how it works |
| [Architecture](docs/architecture.md) | How the pieces fit, source layout, security model |
| [Contributing](CONTRIBUTING.md) | Code style, where to add things, project rules |
| [Security](SECURITY.md) | Reporting vulnerabilities |
| [Changelog](CHANGELOG.md) | What changed in each version |

## Privacy

No telemetry and no AI service. The optional local AI runs entirely on your PC. The network is used only by the weather, lyrics and dictionary
modules (and weather for a city you ask about), by the calendar link you add (fetched every 5 minutes), by web searches you ask Bun for, and by a once-a-day update check that asks GitHub for the
latest release (turn it off in **Settings → General → Updates**). Weather comes from Open-Meteo; with
no city set, the location comes from Windows' own location service (never from your IP address),
and its coordinates are sent to Open-Meteo and to BigDataCloud for the place name. Agent hooks talk to the island
over a local pipe that only your user account can open.

## Updates

Bunbun checks GitHub once a day and shows a notification when a new version is out; click it, or
**Update available** in the tray menu, to open the release page. You can also check any time from
the tray (**Check for updates…**) or **Settings → General → Updates**. Nothing is downloaded or
installed for you: run the new setup, which replaces the old version in place. Your settings and
connected agents carry over.

## Credits & license

Bunbun builds on open-source work; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Released under the [MIT License](LICENSE).
