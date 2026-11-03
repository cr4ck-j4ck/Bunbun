#!/usr/bin/env bash
# Regenerates res/bunbun.ico from the mascot code (see scripts/make_icon.cpp).
set -euo pipefail
cd "$(dirname "$0")/.."
LLVM_MINGW="${LLVM_MINGW:-$(ls -d .toolchain/llvm-mingw-*-ucrt-x86_64 2>/dev/null | tail -1)}"
mkdir -p build
"$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" -std=c++20 -DUNICODE -D_UNICODE -municode -O1 -static \
    scripts/make_icon.cpp src/mascot/mascot.cpp -o build/make_icon.exe -lole32 -luuid -ld2d1 -lwindowscodecs -lshell32
./build/make_icon.exe res/bunbun.ico
