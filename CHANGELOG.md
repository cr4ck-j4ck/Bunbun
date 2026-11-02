# Changelog

All notable changes to Bunbun. The format follows [Keep a Changelog](https://keepachangelog.com/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0]

### Fixed
- Asking Bun for the weather in another city answered with your own city's weather.
- The island no longer redraws at the monitor's full refresh rate while nothing moves. A spring
  that never quite settled kept agent pills at ~128 frames a second; idle CPU under an agent pill
  drops from ~30% of a core to ~2%, GPU from ~3% to ~0.3%.
- Scrolling no longer hits a dead stop past the last tile, and the music player is always reachable
  in the scroll ring, even while paused.
- With the game overlay on, the mouse wheel opens the dashboard so it can be scrolled.
- Mascots on agent pills are as big as the one beside the clock, and skin-pack images are trimmed
  to their visible area so every mood fills the same space.
- Skin packs no longer look blurry or jagged at island size: their art is scaled down with
  high-quality filtering. Packs made from a character sheet show up in the Mascot studio's list.
- Skin-pack frames are no longer tilted or bobbed on top of their own drawn motion, which made
  them wobble.
- A skin pack's art that reaches past the head (question marks, sparks, a raised hand) is no
  longer cut off by the island's edge or covered by text: the agent pill, approval card, Ask Bun
  and the slot beside the clock leave room for it.

### Added
- **An installer.** `Bunbun-<version>-setup.exe` installs for your account (no admin prompt), adds
  Start menu shortcuts and an uninstaller, can start Bunbun at sign-in, installs the WebView2
  Runtime if it's missing, and can download a Local AI model and turn Local AI on. Uninstalling
  disconnects the coding agents connected to that copy first. Releases now offer just this one
  file (with the local AI helper) instead of a zip.
- `bunbun.exe --enable-local-ai` turns Local AI on in `settings.ini`; `--disconnect <agent> --if-ours`
  only disconnects an agent whose hooks call that copy of Bunbun.
- **Your own character from a character sheet.** Mascot studio → Custom skins → Pick a character
  sheet: Bunbun cuts out every frame, you say which row is which mood, and the frames play as
  animations. The resting idle frame's eyes follow your cursor, and a row of look directions
  (left, up, centre, down, right) makes the character look toward it. Timing per mood in
  `pack.ini`.
- **Animated skin packs**: `idle_1.png`, `idle_2.png`… play in turn, cross-fading between frames
  and between moods.
- **Live eyes for skin packs (face rig).** A pack can include its face without eyes and brows
  (`pack.ini` `[Rig]`); Bunbun draws them live and sharp: smooth gaze in every direction toward
  the cursor, idle glances, lids that follow the gaze, natural and double blinks, brows that lift,
  and closed "^ ^" eyes when happy.
- **Local AI for Ask Bun (optional, offline).** A small language model on your own PC answers
  what Bun's commands don't cover, and acts like an agent: several steps for one request ("get me
  ready for a meeting"), and lookups it reads before answering ("is it warmer in Delhi or
  Mumbai?"). Runs in a separate helper (`bunbun-brain.exe`, llama.cpp) that starts on demand and
  frees its memory when idle. No account or keys; you download a model yourself. See the Local AI
  guide. Settings → Assistant & agents → Local AI shows its status.
- **Weather anywhere**: `weather in Mumbai`, `mumbai weather`, `will it rain in Pune tomorrow`.
- **Three new moods for every mascot**: Listening while you type to Bun, Talking while the local AI
  answers, Confused (dizzy: spiral eyes and a ring of stars) when Bun didn't understand. Skin packs can add `listening.png`, `talking.png`
  and `confused.png`.
- **The mascot lives in the Ask Bun box**, in place of the magnifier, and reacts to what's happening.
- **Venom Spidey**: Spidey's colour swatches give him a dark Venom suit with grey webbing.
- **Repeating reminders**: `remind me every day at 9 to stand up`, `every weekday`, `every Monday
  at 10`, `every Mon and Wed`, `every morning`. Done moves one on to its next time; Snooze rings
  again in 10 minutes without changing the schedule.
- **To-dos page** in Settings: add, edit, tick off, reschedule, set repeats, delete, clear
  finished. Changes reach the island at once.
- **Quiet reminders**: during a full-screen app, a presentation, a calendar meeting or Focus mode,
  a reminder shows as a small silent pill (tap it for Snooze and Done).
- **Focus mode**: Ask Bun `focus` (25 min) or `focus 50`. Runs the timer, puts the mascot on
  do-not-disturb, keeps notification pop-ups off the island (they stay in Windows' notification
  centre) and ends with a summary: minutes, to-dos ticked off, notifications that waited.
- **Ask Bun extras**: `undo` (or Ctrl+Z in the empty box) reverts the last command (volume, mute,
  dnd, timer, focus, to-dos, Wi-Fi, Bluetooth, brightness); Up / Down step through earlier
  commands; `wifi on/off`, `bluetooth on/off`, `brightness 60`, `brighter`, `dim the screen`; and
  apps open by fuzzy name from a remembered Start-menu list (`vsc` → Visual Studio Code).
- **To-dos and reminders.** Ask Bun `add buy milk`, `remind me at 3pm to call Sam`,
  `remind me tomorrow at 9 to send the report`, `my list`, `done buy milk`, `clear done`. A To-do
  tab lists them (click to tick off); a reminder pill rings when one is due with **Snooze** (10 min)
  and **Done**, and plays Windows' Calendar Reminder sound (off with "Reminder sound"). Kept on
  your PC in `todos.tsv` next to `settings.ini`.
- **Privacy dots that catch every app.** The microphone dot follows live recording sessions and
  the camera dot follows Windows' live camera monitor, so Discord, OBS and games show up (Windows'
  own usage records had stopped updating for desktop apps). Screen recording and streaming come from
  OBS's log (recording, live, replay buffer) and the "… is sharing your screen" bar of Chromium
  browsers. One dot per device in use, a short pill names the app when it starts, and pills make
  room for the dots instead of being covered by them. Apps like Discord keep the microphone open
  while you're muted, so the mic dot stays on, as Windows' own icon does.
- **Connectivity cards you can use.** Click Wi-Fi or Bluetooth to turn it on or off; hold it to
  see nearby Wi-Fi networks or paired Bluetooth devices and connect or disconnect right there
  (saved networks and Bluetooth audio devices; others open Windows' own picker). Right-click any
  card for its Windows settings. Do Not Disturb still opens Settings: Windows has no public switch.
- **Battery details and moods** on laptops: time until full while charging, a note when the
  battery is held at a limit, battery health on hover, a charging update every 15 minutes, and the
  mascot holds a plug while charging, gets drowsy below 15% and cheers when it's full.
- **Next meeting** from your calendar's private iCal link (Google Calendar, Outlook.com): a
  countdown pill before it starts with a **Join** button for Meet, Zoom, Teams and Webex, and an
  "Up next" panel on the calendar tab (in place of the date block while a meeting is coming up).
  With the Battery card, the Connectivity tab switches to a 2 × 2 grid so every card stays readable. Repeating meetings and time zones are handled.
- **Download progress**: browser downloads in your Downloads folder show as a live pill with name,
  size and speed; click the finished one to show the file.
- **Screen-recording dot**: blue while an app captures the screen through Windows' capture API.
- **Battery on the Connectivity tab** (laptops and tablets only): charging, fully charged on power,
  plugged in but not charging (battery held at a limit), or the percentage with time left and
  battery saver while unplugged. Click it for Windows' Power & battery settings.
- **Ask Bun gets out of the way.** A command that ran (open an app, dnd, volume, media, timer,
  lock, snip, web search) flashes its confirmation for a moment, then the box closes, focus goes
  back to your app and the island returns home. Answers to questions still stay up to read.
  The box empties after each query, ready for the next one. Bun also understands looser phrasing: `turn on dnd`, `disable do not disturb`,
  `can you open up notepad?`, `launch the spotify app`.
- **Mascots at work.** While an agent works, every mascot types at a little laptop with its eyes
  reading across the screen; Bolt runs his terminal on his visor instead. Bolt's do-not-disturb
  snack is now a charging battery.
- **Update check.** Once a day (and on demand from the tray or Settings → General → Updates) Bunbun
  asks GitHub for the latest release and shows a notification when a newer one is out. Read-only:
  it opens the release page and never downloads anything. Off with "Check automatically".
- **Battery saver** (Animations): fewer wakeups, lower ambient frame rates and no 1 ms timer for
  ambient content. Auto turns it on while running on battery or Windows battery saver.
- The hardware monitor says **Upload** and **Download** instead of Net up / Net down.
- `BUNBUN_LOG=1` also logs frames per second, why they were drawn and how long each took.

## [0.1.0]

First public version.

### Island
- One island for media, agents, timers, alerts and an idle dashboard; pill, notch or Windows 11
  shapes; top or bottom; one display or all, with a position per display.
- Media with album art, controls, synced lyrics, and the song title on the collapsed pill.
- Dashboard tabs: calendar, weather, hardware monitor (CPU, RAM, GPU, disk, network speed),
  connectivity (Wi-Fi or Ethernet, Bluetooth, Do Not Disturb), file shelf.
- Privacy dots when the microphone or camera is in use.

### Coding agents
- Claude Code, Antigravity, Cursor and GitHub Copilot: live session ticker and one-click Allow / Deny,
  with one-click connect and disconnect (backups made).

### Mascots
- Six original characters (Bun, Miso, Biscuit, Ember, Bao, Bolt) with moods for idle, working,
  thinking, needs you, celebrate, error, sleepy, music and do not disturb; per-character colours;
  custom skin packs.

### Ask Bun
- A local Windows assistant on Ctrl+Alt+B: open apps and settings, media and volume, timers,
  searches, quick answers, mascot do-not-disturb.

### Releases
- A portable zip (`Bunbun-<version>-win-x64.zip`) with SHA256 checksums; `scripts/setup.sh`
  fetches the pinned toolchains; CI builds every push and tags publish releases.

### Settings app
- WebView2 settings window with a live preview of the real island on your wallpaper, themes,
  animations with real spring previews, and search across every setting.
