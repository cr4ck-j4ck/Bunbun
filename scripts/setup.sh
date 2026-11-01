#!/usr/bin/env bash
# One-time setup: downloads the pinned toolchains into .toolchain/ and installs
# the settings UI's packages. Safe to re-run; anything already present is kept.
#
#   ./scripts/setup.sh
#
# Set TOOLCHAIN to put the toolchains somewhere else (then point build.sh at them
# with LLVM_MINGW, WINRT_INCLUDE and WEBVIEW2).
set -euo pipefail
cd "$(dirname "$0")/.."

# Pinned versions; bump them together and note it in the CHANGELOG.
LLVM_MINGW_VER=20261020
CPPWINRT_VER=3.0.260818.1
WEBVIEW2_VER=1.0.4314.35

TOOLCHAIN="${TOOLCHAIN:-.toolchain}"
mkdir -p "$TOOLCHAIN"

# Extracts a zip (or .nupkg, which is a zip) with PowerShell; Git Bash has no unzip.
extract() {
    powershell -NoProfile -Command \
        "Expand-Archive -Path '$(cygpath -w "$1")' -DestinationPath '$(cygpath -w "$2")' -Force"
}

fetch() {  # url, destination file
    echo "  GET $1"
    curl -fsSL --retry 3 -o "$2" "$1"
}

llvm="$TOOLCHAIN/llvm-mingw-$LLVM_MINGW_VER-ucrt-x86_64"
if [ ! -x "$llvm/bin/x86_64-w64-mingw32-clang++.exe" ] && [ ! -x "$llvm/bin/x86_64-w64-mingw32-clang++" ]; then
    echo "llvm-mingw $LLVM_MINGW_VER"
    fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VER/llvm-mingw-$LLVM_MINGW_VER-ucrt-x86_64.zip" \
          "$TOOLCHAIN/llvm-mingw.zip"
    extract "$TOOLCHAIN/llvm-mingw.zip" "$TOOLCHAIN"
    rm -f "$TOOLCHAIN/llvm-mingw.zip"
fi

if [ ! -f "$TOOLCHAIN/cppwinrt/bin/cppwinrt.exe" ]; then
    echo "C++/WinRT $CPPWINRT_VER"
    fetch "https://www.nuget.org/api/v2/package/Microsoft.Windows.CppWinRT/$CPPWINRT_VER" "$TOOLCHAIN/cppwinrt.zip"
    extract "$TOOLCHAIN/cppwinrt.zip" "$TOOLCHAIN/cppwinrt"
    rm -f "$TOOLCHAIN/cppwinrt.zip"
fi

# The WinRT headers are generated from this PC's own Windows metadata.
if [ ! -f "$TOOLCHAIN/winrt-include/winrt/base.h" ]; then
    echo "Generating C++/WinRT headers"
    "$TOOLCHAIN/cppwinrt/bin/cppwinrt.exe" -in local -out "$TOOLCHAIN/winrt-include"
fi

if [ ! -f "$TOOLCHAIN/webview2/build/native/x64/WebView2Loader.dll" ]; then
    echo "WebView2 SDK $WEBVIEW2_VER"
    fetch "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$WEBVIEW2_VER" "$TOOLCHAIN/webview2.zip"
    extract "$TOOLCHAIN/webview2.zip" "$TOOLCHAIN/webview2"
    rm -f "$TOOLCHAIN/webview2.zip"
fi

if [ ! -d settings-ui/node_modules ]; then
    echo "Settings UI packages"
    (cd settings-ui && npm ci)
fi

echo "Toolchains ready in $TOOLCHAIN. Next: ./build.sh"
