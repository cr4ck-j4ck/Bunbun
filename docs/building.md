# Building Bunbun

Bunbun builds on Windows 10/11 (x64) with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) from
Git Bash. No Visual Studio needed.

## Requirements

| What | Where it goes | Notes |
|---|---|---|
| llvm-mingw (UCRT, x86_64) | `.toolchain/llvm-mingw-*-ucrt-x86_64/` | Or set `LLVM_MINGW` |
| C++/WinRT headers | `.toolchain/winrt-include/` | Or set `WINRT_INCLUDE` |
| WebView2 SDK | `.toolchain/webview2/` (NuGet `Microsoft.Web.WebView2`, extracted) | Or set `WEBVIEW2` |
| Node.js 18+ | on `PATH` | For the settings UI |
| Python 3 | on `PATH` | Code generators and tests |

Generate the C++/WinRT headers from this PC's Windows metadata with Microsoft's `cppwinrt.exe`
(NuGet `Microsoft.Windows.CppWinRT`):

```bash
cppwinrt.exe -in local -out .toolchain/winrt-include
```

Install the settings UI's packages once:

```bash
cd settings-ui && npm ci
```

`.toolchain/` and `node_modules/` are gitignored.

## Build

```bash
./build.sh
```

Outputs, all in `build/`:

- `bunbun.exe`: the island and tray app
- `bunbun-hook.exe`: the relay that coding agents run on each hook event
- `bunbun-settings.exe` and `WebView2Loader.dll`: the settings window
- `settings-ui/`: the settings UI, built with Vite

`./build.sh debug` builds without optimisation and with symbols; debug with
`.toolchain/llvm-mingw-*/bin/lldb.exe`. The version comes from the `VERSION` file.

## Test

```bash
./tests/run.sh
```

Runs the JSON, hook-installer and Ask Bun parser tests. With the island running, these exercise the
real hook and island:

```bash
python tests/test_dialects.py
```

```bash
python tests/simulate_agent.py demo
```

## Working while the island runs

A running `bunbun.exe` can't be overwritten, so plain `./build.sh` fails while the island is up.
Use the dev script instead:

```bash
./scripts/dev.sh
```

It moves the running binaries aside, builds, and swaps in the new island with about a second of
downtime. If the build fails, the old binaries go back and the island keeps running. The hook is
replaced in place with a rename, so connected agents never see it missing.

| Mode | What it does |
|---|---|
| `./scripts/dev.sh` | Full rebuild and swap |
| `./scripts/dev.sh debug` | Same, debug build |
| `./scripts/dev.sh ui` | Rebuild only the settings UI and reopen its window |
| `./scripts/dev.sh stop` | Quit everything |

`bunbun.exe --quit` and `bunbun.exe --replace` also work by hand.

## Settings schema

Every setting is declared once in `src/island/settings_schema.yaml`. After changing it, regenerate
the island's defaults and the settings UI's schema:

```bash
sh scripts/gen_defaults.sh
```

```bash
python scripts/gen_schema.py
```

## Settings UI in a browser

```bash
cd settings-ui && npm run dev
```

A mock bridge answers instead of the real host. To load the dev server inside the real settings
window, start `build/bunbun-settings.exe` with `BUNBUN_UI_DEV=http://localhost:5173`. Add
`BUNBUN_DEVTOOLS=1` for DevTools.

## Diagnostics

| Variable | Effect |
|---|---|
| `BUNBUN_LOG=1` | Writes `%LOCALAPPDATA%\Bunbun\bunbun.log` |
| `BUNBUN_HOOK_DEBUG=1` | The hook explains on stderr why an event didn't arrive |
