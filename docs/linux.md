# Bunbun on Linux

Bunbun runs on Linux as a `.deb` (Debian, Ubuntu and their derivatives) and an AppImage (most other
distributions). The Linux version is newer and smaller than the Windows one: it has the island,
the clock, music, the mascot, coding agents with Allow / Deny, and the settings app. The rest of
this page says what works where, what isn't there yet, and how the port is built.

## Install

**Debian, Ubuntu, Linux Mint, Pop!_OS** (Ubuntu 22.04 or Debian 12 and newer):

```bash
sudo apt install ./Bunbun-<version>-amd64.deb
```

Then start **Bunbun** from your app menu, or run `bunbun`. Remove it with `sudo apt remove bunbun`.

**Any other distribution** (Fedora, Arch, openSUSE…) with glibc 2.35 or newer:

```bash
chmod +x Bunbun-<version>-x86_64.AppImage
```

```bash
./Bunbun-<version>-x86_64.AppImage
```

`./Bunbun-<version>-x86_64.AppImage settings` opens the settings window directly. The AppImage needs
GTK 3, which every GNOME and KDE desktop has. If your system has no FUSE, add
`--appimage-extract-and-run`.

**Start at login:** Settings → General → Start when you log in. It writes
`~/.config/autostart/bunbun.desktop`, which GNOME, KDE, XFCE, Cinnamon and MATE all read.

**Connect your coding agent:** right-click the island → **Connect Claude Code**, or Settings →
Assistant & agents for Claude Code, Codex, Antigravity, Cursor and Copilot. It works as on Windows; see
[Coding agents](agents.md).

## What works where

| Desktop | Session | How the island is shown | Status |
|---|---|---|---|
| KDE Plasma 5.27 / 6 | Wayland | layer-shell surface | Supported |
| Sway, Hyprland, river, Wayfire, labwc | Wayland | layer-shell surface | Supported (tested on Sway) |
| GNOME | Wayland | through XWayland (see below) | Supported, with caveats |
| GNOME, KDE, XFCE, Cinnamon, MATE | X11 | always-on-top dock window | Supported |
| i3 and other X11 window managers | X11 | dock window; override-redirect with `BUNBUN_X11_OVERRIDE=1` | Works; no shadow without a compositor |

**GNOME on Wayland** has no layer-shell protocol, so a Wayland window can't place itself at the top
of the screen or stay above others. Bunbun notices this and restarts itself through XWayland, where
it can. This needs XWayland, which GNOME ships by default. On a fractional scale (125 %, 150 %) the
island may look a little soft, because GNOME scales XWayland windows. A GNOME Shell extension that
offers layer-shell would let Bunbun draw natively; there isn't a maintained one yet.

**Without a compositor** (some X11 window managers, or XFCE with compositing off) transparency isn't
possible, so the window is cut to the pill's shape instead: the island still looks like a pill, only
without its soft shadow.

## What isn't on Linux yet

| Feature | Why | Plan |
|---|---|---|
| Studio layout (header, icon tab bar, Home cards) | The Windows Studio is drawn with Direct2D and DirectWrite inside the Windows island | Next: Linux draws the classic pill for now, whatever **Layout** says. The page order, Home cards and tab bar come first, then the Music and Agent pages |
| Island pages (order, Home cards, page pictures) | Needs the Studio layout | Arrives with Studio. The screen works in Settings, with example drawings instead of real pictures |
| Plan approval and several-question cards | Part of the Studio drawing | With Studio. Until then a plan shows Allow / Deny, and several questions are answered in the terminal |
| Replying to a finished agent, typed answers | Need keyboard input on the island | After the cards (layer-shell keyboard focus on demand) |
| Claude usage ring and history | Part of the Studio Agent page | With the Agent page. The numbers already reach the Linux island |
| Tray icon | Linux has no single tray; it needs StatusNotifierItem over D-Bus | Right-click the island for the same menu. SNI tray later |
| Ask Bun (Ctrl+Alt+B) | Its commands open Windows apps and Settings pages | Port the commands to Linux equivalents (`xdg-open`, desktop files, `pactl`) |
| Weather, lyrics and the lyric pill, calendar, downloads, to-dos | Built into the Windows island; not ported yet | One module at a time; lyrics after Studio |
| Volume, brightness, battery, Wi-Fi, Bluetooth flyouts | Need Linux system APIs | PipeWire/PulseAudio, `brightnessctl`/logind, UPower, NetworkManager, BlueZ |
| Notifications and clipboard peek | Need the desktop's notification service | `org.freedesktop.Notifications` monitoring |
| Music waveform from the real audio | Needs audio capture | PipeWire monitor stream. The bars animate while music plays for now |
| Several displays, other positions | The island sits top-centre on the primary display | Per-monitor layer surfaces / windows |
| Clipboard, note, focus, apps and mirror pages, colour picker, tuck in full screen | Windows-only for now | Later |
| Live island preview in Settings, character-sheet import, Local AI | Windows-only for now | Later |
| Update check | Packages are updated by your package manager or a new AppImage | Not planned |

Settings for features that aren't on Linux yet still show in the settings app; changing them has no
effect on Linux.

## Files

| What | Where |
|---|---|
| Settings | `~/.config/Bunbun/settings.ini` (same keys as on Windows; plain UTF-8) |
| Skin packs | `~/.config/Bunbun/mascots/<name>/` |
| Log (with `BUNBUN_LOG=1`) | `~/.local/state/Bunbun/bunbun.log` |
| Agent socket, instance lock | `$XDG_RUNTIME_DIR/bunbun/` (only your user can open it) |
| Hook used by agents (AppImage) | `~/.local/share/Bunbun/bin/bunbun-hook` |
| Programs (`.deb`) | `/usr/lib/bunbun/`, with `/usr/bin/bunbun` |

`bunbun --quit` closes a running island; `bunbun --replace` restarts it; `bunbun --settings` opens
the settings window.

## How the port is built

### What is shared

These parts are the same code on both systems:

- **The mascot** (`src/mascot/`): every character and mood, and skin packs. It's written against
  Direct2D; on Linux it compiles unchanged against a small Direct2D implementation on Cairo (see
  [Compatibility layer](#compatibility-layer)), so a new mood or character arrives on both systems at once.
- **The agent bridge** (`src/agents/agent_bridge.cpp`): sessions, phases, the approval queue and
  question answers. Only its transport differs: a named pipe on Windows, a Unix socket on Linux.
- **The hook** (`src/hook/`): every agent dialect and reply format; again only the transport differs.
- **The hook installers** (`src/agents/hook_installer.cpp`): Claude Code (with its statusLine and
  reply hook), Codex, Antigravity, Cursor and Copilot configs are merged the same way.
- **Settings**: the schema (`settings_schema.yaml`), the generated defaults and `settings.ini`'s rules
  (keys written commented out, so defaults stay live).
- **The settings UI** (`settings-ui/`): the same React build. It talks to `window.chrome.webview`;
  the Linux host provides that object on WebKitGTK, so the UI has no Linux branch.
- **JSON** (`src/common/json.h`).

`src/assistant/` (the Ask Bun parser) and `src/common/todos.h` are portable too, but their callers
are Windows-only for now.

### What each system does its own way

| Need | Windows | Linux |
|---|---|---|
| The overlay window | Layered Win32 window, `UpdateLayeredWindow` | GTK 3 window: a wlr-layer-shell surface (gtk-layer-shell) on Wayland, an always-on-top dock or override-redirect window on X11; input region = the pill |
| Drawing | Direct2D + DirectWrite | Cairo + Pango (the mascot through the Direct2D stand-ins) |
| Settings window | WebView2 | WebKitGTK 4.1, UI served from a private `bunbun-ui://` scheme |
| Now playing | Media session (GSMTC) | MPRIS on the session D-Bus |
| Agent channel | Named pipe `\\.\pipe\bunbun-<SID>`, owner-only DACL | Unix socket `$XDG_RUNTIME_DIR/bunbun/agent.sock`, folder mode 0700, both ends check `SO_PEERCRED` |
| Single instance | Named mutex | `flock` on `$XDG_RUNTIME_DIR/bunbun/island.lock` |
| Settings file watching | `ReadDirectoryChangesW` | `GFileMonitor` |
| Autostart | `HKCU\…\Run` | `~/.config/autostart/bunbun.desktop` |
| Tray | Shell notify icon | Not yet (StatusNotifierItem planned); right-click menu on the island |
| Volume / audio level | Core Audio | Planned: PipeWire or PulseAudio |
| Wi-Fi / Bluetooth | WLAN API, WinRT radios | Planned: NetworkManager, BlueZ (D-Bus) |
| Battery | `GetSystemPowerStatus` | Planned: UPower (D-Bus) |
| Notifications | WinRT notification listener | Planned: `org.freedesktop.Notifications` |
| Brightness | WMI | Planned: logind `SetBrightness` or `brightnessctl`; not every display allows it |
| Location for weather | Windows location API | Planned: GeoClue, or the city from settings |
| Screen text lookup | Windows OCR | Dropped on Linux |

### Code layout

```
src/
  island/, settings/, main.cpp     Windows island, settings host and tray (unchanged)
  mascot/, agents/, hook/,         shared; Windows-only lines inside are behind #ifdef _WIN32
  common/, assistant/
  platform/linux/
    island_linux.cpp               the island: window, layout, drawing, input
    mpris.cpp                      now playing over D-Bus
    settings.cpp                   settings.ini and the generated defaults
    settings_host_linux.cpp        bunbun-settings on WebKitGTK
    platform.cpp                   XDG folders, UTF-8, starting programs
    image_codec.cpp                skin-pack images through GdkPixbuf
    mascot_png.cpp                 mascot frames as PNG (previews, the app icon)
    compat/                        Win32 and Direct2D stand-ins for shared code
packaging/linux/                   desktop entry, AppStream metadata
CMakeLists.txt                     the Linux build
scripts/package-linux.sh           .deb and AppImage
```

The Windows code stays where it is and builds exactly as before with `./build.sh`; nothing in
`src/platform/linux/` is part of it. As more of the Windows island is shared, those pieces move out
of `island_core.cpp` into portable modules, with a `src/platform/win/` counterpart for the parts that
stay Windows-specific.

### Compatibility layer

`src/platform/linux/compat/` holds a `windows.h`, `d2d1.h` and friends that only the Linux build sees,
and only for shared files that still include them:

- **Direct2D on Cairo** (`d2d1.h`, `d2d_cairo.cpp`): render targets, brushes (solid, radial gradient,
  bitmap), path geometries with Béziers and arcs, layers with geometric and opacity masks, axis-aligned
  clips, bitmaps. Exactly what the mascot uses; anything else is a compile error rather than a silent
  difference. `bbcompat::CreateCairoTarget()` and `bbcompat::SetCairo()` connect it to a `cairo_t`.
- **Win32 basics** (`windows.h`, `shlobj.h`, `win32_compat.cpp`): files, environment, known folders
  mapped to XDG folders, INI reading, UTF-8 conversion. Paths with backslashes are converted to
  slashes.

New Linux code doesn't use these: it talks to POSIX, GLib and GTK directly. They exist so the
mascot and the hook installers stay one copy.

### Packaging

`scripts/package-linux.sh` builds with CMake (`-DCMAKE_INSTALL_PREFIX=/usr`), installs into a staging
tree, and makes both packages from it:

- the **`.deb`** takes its dependencies from the binaries (`dpkg-shlibdeps`), so on newer releases
  the 64-bit-time package names (`libgtk-3-0t64`) satisfy them;
- the **AppImage** carries the programs, the settings UI and gtk-layer-shell, and uses the system's
  GTK 3 and WebKitGTK. Without WebKitGTK the island opens `settings.ini` in a text editor instead of
  the settings window. Because the AppImage's folder changes on every run, agent hooks point at a
  copy of `bunbun-hook` kept in `~/.local/share/Bunbun/bin`; the hook needs only the C library.

Both are built on Ubuntu 22.04 in CI, so they run on glibc 2.35 and newer: Ubuntu 22.04+, Debian 12+,
Fedora 36+, current Arch and openSUSE Tumbleweed. Building is covered in [Building](building.md#linux).

### Testing

The island has been run end to end on X11 (Openbox, with and without a compositor) and on Wayland
(Sway): clock, music with cover art, the hover card and its controls, agent pills, and an approval
answered with a real click. `tests/test_dialects.py` and `tests/simulate_agent.py` run against the
Linux hook (they find `build-linux/bunbun-hook`, or set `BUNBUN_HOOK`). Real GNOME and KDE sessions
haven't been tested yet.
