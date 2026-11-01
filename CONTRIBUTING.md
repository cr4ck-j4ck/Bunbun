# Contributing to Bunbun

Thanks for helping! Bug reports, fixes, new island modules and new original mascots are all welcome.

## Getting started

1. Set up the toolchain and build: [docs/building.md](docs/building.md).
2. Run the tests before and after your change: `./tests/run.sh`.
3. While the island runs, use `./scripts/dev.sh` to push your build live in about a second.

## Code style

- **C++20**, 4-space indents, `// ──` section banners. Comments explain *why*, not *what*.
- `src/island/island_core.cpp` is derived from the MIT-licensed
  [Dynamic Island for Windows](https://windhawk.net/mods/dynamic-island-for-windows) Windhawk mod.
  Mark every Bunbun change inside it with a `Bunbun:` comment, so it stays separable from upstream
  code. Keep upstream credits and licence text intact.
- Scripted edits to `island_core.cpp`: assert the text you replace occurs exactly once.
- Build with `-DUNICODE` (the build script does). The core lives in an anonymous namespace; its entry
  points are at the end of the file.
- TypeScript in `settings-ui/` must pass `npx tsc --noEmit`.

## Adding things

| To add | Start at |
|---|---|
| A setting | `src/island/settings_schema.yaml`, then regenerate (see building.md) and read it in `LoadSettings()` |
| An island activity | `IslandKind` → `ActivityForKind` → `ChooseActivities` → the `DrawPill` switch → click handling in `OverlayWndProc` |
| A dashboard tab | `ActiveTabCount` and the tab-index helpers, then both tab lists (idle and media) |
| An Ask Bun command | `src/assistant/assistant.cpp` (with a test in `tests/test_core.cpp`), then `RunBun()` in the core |
| An agent | the dialect layer in `src/hook/bunbun_hook.cpp`, an installer in `src/agents/hook_installer.cpp`, a case in `tests/test_dialects.py` |
| A mascot mood or character | `src/mascot/mascot.cpp` (`LookFor`, `BodyMotion`, `DrawStyled`, `ExtentFor`) |

Threading: the window procedure and the render loop share the render thread. `g_state` is guarded by
`g_stateMutex`.

## Rules

- **No new runtime dependencies for the island.** It's C++, Win32, Direct2D and C++/WinRT only. The
  settings UI may use npm packages; record their licences in `THIRD_PARTY_NOTICES.md`.
- **Ask Bun stays local.** No AI service calls.
- **Licences.** Don't copy code from GPL projects. Credit MIT and other permissive sources in
  `THIRD_PARTY_NOTICES.md`.
- **Art.** Only original characters and art you own. No third-party characters (film, TV, comic or
  game) in code or assets; users can load those privately as skin packs.
- **Agents are never auto-approved.** Anything that answers a permission request must come from a
  person's click.

## Pull requests

- One topic per PR, with a short description of what changed and how you tested it.
- Include a screenshot or short clip for visual changes.
- Add an entry under "Unreleased" in [CHANGELOG.md](CHANGELOG.md).

By contributing you agree that your work is released under the [MIT licence](LICENSE).
