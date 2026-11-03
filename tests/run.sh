#!/usr/bin/env bash
# Builds and runs the unit tests. The installer test writes only to a fresh
# temporary CLAUDE_CONFIG_DIR, never to your real ~/.claude.
set -euo pipefail
cd "$(dirname "$0")/.."
LLVM_MINGW="${LLVM_MINGW:-$(ls -d .toolchain/llvm-mingw-*-ucrt-x86_64 2>/dev/null | tail -1)}"
[ -f build/bunbun-hook.exe ] || ./build.sh
CXX="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++"
[ -x "$CXX" ] || CXX="${CXX}.exe"
"$CXX" -std=c++20 -DUNICODE -D_UNICODE -municode -O1 -static \
    tests/test_core.cpp src/agents/hook_installer.cpp src/assistant/assistant.cpp -o build/test_core.exe -lole32 -lshell32 -luuid -lwinhttp
if command -v cygpath >/dev/null 2>&1; then
    scratch="$(mktemp -d)"
    trap 'rm -rf "$scratch"' EXIT
    win_scratch="$(cygpath -w "$scratch")"
else
    # In Git Bash without cygpath, or Windows shell, map /tmp to %TEMP% or use Windows temp
    win_temp="${TEMP:-${TMP:-build/tmp}}"
    mkdir -p "$win_temp"
    win_scratch="$win_temp/bb_test_$$"
    mkdir -p "$win_scratch"
    trap 'rm -rf "$win_scratch"' EXIT
fi
./build/test_core.exe "$win_scratch"
"$CXX" -std=c++20 -DUNICODE -D_UNICODE -O1 -static tests/test_studio.cpp -o build/test_studio.exe
./build/test_studio.exe
