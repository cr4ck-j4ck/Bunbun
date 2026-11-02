#!/usr/bin/env bash
# One-time setup: downloads the pinned toolchains into .toolchain/ and installs
# the settings UI's packages. Safe to re-run; anything already present is kept.
#
#   ./scripts/setup.sh                 everything needed for ./build.sh
#   ./scripts/setup.sh --local-ai      also what bunbun-brain.exe is built from
#   ./scripts/setup.sh --installer     also Inno Setup, for the setup .exe (scripts/package.sh)
#
# Set TOOLCHAIN to put the toolchains somewhere else (then point build.sh at them
# with LLVM_MINGW, WINRT_INCLUDE and WEBVIEW2).
set -euo pipefail
cd "$(dirname "$0")/.."

# Pinned versions; bump them together and note it in the CHANGELOG.
LLVM_MINGW_VER=20261020
CPPWINRT_VER=3.0.260818.1
WEBVIEW2_VER=1.0.4314.35
# Optional local AI (bunbun-brain.exe). Only fetched with --local-ai; see docs/local-ai.md.
LLAMA_CPP_TAG=b11721
CMAKE_VER=4.4.4
NINJA_VER=1.13.2
# Optional installer compiler (Inno Setup). Only fetched with --installer.
INNO_VER=6.4.3

TOOLCHAIN="${TOOLCHAIN:-.toolchain}"
mkdir -p "$TOOLCHAIN"
LOCAL_AI=0
INSTALLER=0
for arg in "$@"; do
    case "$arg" in
        --local-ai) LOCAL_AI=1 ;;
        --installer) INSTALLER=1 ;;
    esac
done

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

# ── Optional: what bunbun-brain.exe (the local AI helper) is built from ───────
# llama.cpp (MIT) runs the model; CMake and Ninja build it. Portable copies,
# nothing is installed system-wide. The model itself is never downloaded here:
# the user picks one (docs/local-ai.md).
if [ "$LOCAL_AI" = 1 ]; then
    if [ ! -x "$TOOLCHAIN/cmake/bin/cmake.exe" ]; then
        echo "CMake $CMAKE_VER"
        fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-windows-x86_64.zip" "$TOOLCHAIN/cmake.zip"
        extract "$TOOLCHAIN/cmake.zip" "$TOOLCHAIN"
        mv "$TOOLCHAIN/cmake-$CMAKE_VER-windows-x86_64" "$TOOLCHAIN/cmake"
        rm -f "$TOOLCHAIN/cmake.zip"
    fi
    if [ ! -x "$TOOLCHAIN/ninja/ninja.exe" ]; then
        echo "Ninja $NINJA_VER"
        fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VER/ninja-win.zip" "$TOOLCHAIN/ninja.zip"
        extract "$TOOLCHAIN/ninja.zip" "$TOOLCHAIN/ninja"
        rm -f "$TOOLCHAIN/ninja.zip"
    fi
    if [ ! -f "$TOOLCHAIN/llama.cpp/include/llama.h" ]; then
        echo "llama.cpp $LLAMA_CPP_TAG"
        git clone --quiet --depth 1 --branch "$LLAMA_CPP_TAG" https://github.com/ggml-org/llama.cpp "$TOOLCHAIN/llama.cpp"
    fi
fi

# ── Optional: Inno Setup, which turns a release into a setup .exe ───────────
# Installed portably into the toolchain folder: nothing registered system-wide.
if [ "$INSTALLER" = 1 ] && [ ! -f "$TOOLCHAIN/innosetup/ISCC.exe" ]; then
    echo "Inno Setup $INNO_VER"
    fetch "https://github.com/jrsoftware/issrc/releases/download/is-${INNO_VER//./_}/innosetup-$INNO_VER.exe" "$TOOLCHAIN/innosetup-$INNO_VER-setup.exe"
    # MSYS2_ARG_CONV_EXCL: Git Bash would otherwise turn "/VERYSILENT" into a path.
    MSYS2_ARG_CONV_EXCL='*' "$TOOLCHAIN/innosetup-$INNO_VER-setup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /PORTABLE=1 /NOICONS \
        /DIR="$(cygpath -w "$PWD/$TOOLCHAIN/innosetup")"
    rm -f "$TOOLCHAIN/innosetup-$INNO_VER-setup.exe"
    [ -f "$TOOLCHAIN/innosetup/ISCC.exe" ] || { echo "Inno Setup didn't install" >&2; exit 1; }
fi

echo "Toolchains ready in $TOOLCHAIN. Next: ./build.sh"
