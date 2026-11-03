#!/usr/bin/env bash
# Pushes your local changes to the island that is running right now.
#
#   ./scripts/dev.sh          C++ or anything: rebuild, then swap the running island for the new build
#   ./scripts/dev.sh debug    same, with a debug build
#   ./scripts/dev.sh ui       settings UI only: rebuild settings-ui/, reopen the settings window
#   ./scripts/dev.sh stop     quit the island and the settings window
#
# The running island keeps working while you build: its exe is renamed aside
# (Windows allows renaming a running program), the new one is linked in its
# place, then the old island quits cleanly (tray icon removed, agent pipe
# closed) and the new one starts. Downtime is about a second.
#
# Settings and appearance changes never need this: the island reloads
# settings.ini by itself the moment it is saved.
set -euo pipefail
cd "$(dirname "$0")/.."

mode="${1:-release}"

running() { tasklist //FI "IMAGENAME eq $1" 2>/dev/null | grep -qi "$1"; }

start_detached() {  # start a GUI exe without tying it to this shell
    cmd //c start "" "$(cygpath -w "$1")" ${2:-} >/dev/null 2>&1
}

stop_settings() {
    running bunbun-settings.exe || return 0
    taskkill //IM bunbun-settings.exe >/dev/null 2>&1 || true   # polite: WM_CLOSE
    for _ in 1 2 3 4 5 6 7 8 9 10; do running bunbun-settings.exe || return 0; sleep 0.3; done
    taskkill //IM bunbun-settings.exe //F >/dev/null 2>&1 || true
}

stop_island() {  # $1 = an exe that understands --quit
    running bunbun.exe || return 0
    if [ -x "$1" ] && "$1" --quit; then return 0; fi
    # Builds older than --quit ignore the request; fall back to ending the process.
    echo "  island did not quit by itself; ending it"
    taskkill //IM bunbun.exe //F >/dev/null 2>&1 || true
    sleep 0.5
}

if [ "$mode" = stop ]; then
    stop_settings
    stop_island build/bunbun.exe
    echo "Bunbun stopped."
    exit 0
fi

if [ "$mode" = ui ]; then
    was_open=0; running bunbun-settings.exe && was_open=1
    (cd settings-ui && { [ -d node_modules ] || npm ci --silent; } && npm run build --silent)
    stop_settings
    start_detached build/bunbun-settings.exe
    echo "Settings UI rebuilt and reopened."
    exit 0
fi

# ── Full build, then swap ────────────────────────────────────────────────────
island_was_running=0; running bunbun.exe && island_was_running=1
settings_was_open=0; running bunbun-settings.exe && settings_was_open=1

# Old binaries from earlier pushes go once nothing runs them any more.
cleanup_old() { rm -f build/*.old-* 2>/dev/null || true; }
cleanup_old

# Move the island and settings binaries aside so the linker can write fresh ones.
# The hook is NOT moved: agents run it before every tool call, and a missing hook
# blocks them (Antigravity fails every call). build.sh swaps the hook in place.
# Each push uses a unique suffix, since an .old file may still be in use.
stamp="$(date +%s)"
aside=()
for exe in bunbun.exe bunbun-settings.exe; do
    if [ -f "build/$exe" ]; then
        mv "build/$exe" "build/$exe.old-$stamp" && aside+=("$exe")
    fi
done
restore() {  # build failed: drop half-built outputs, put every old binary back
    for exe in "${aside[@]}"; do
        rm -f "build/$exe" 2>/dev/null || true
        mv "build/$exe.old-$stamp" "build/$exe"
    done
}
trap 'echo "Build failed; the running island was left as it was."; restore' ERR

if [ "$mode" = debug ]; then ./build.sh debug; else ./build.sh; fi
trap - ERR

# Swap: quit the old island (the new exe sends the request), start the new one.
if [ "$settings_was_open" = 1 ]; then stop_settings; fi
stop_island build/bunbun.exe
start_detached build/bunbun.exe
[ "$settings_was_open" = 1 ] && start_detached build/bunbun-settings.exe

# The old binaries can go once nothing runs them (a busy hook's copy waits for the next push).
sleep 1
cleanup_old

if [ "$island_was_running" = 1 ]; then echo "Live: the island now runs the new build."
else echo "Started the island."; fi
