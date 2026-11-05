#!/usr/bin/env bash
# Builds bunbun.exe, bunbun-hook.exe, bunbun-settings.exe and the settings UI into build/ with llvm-mingw.
# First time? Run scripts/setup.sh to fetch the toolchains.
#   ./build.sh            release (-O2, stripped)
#   ./build.sh debug      -O0 -g
# Toolchain: set LLVM_MINGW to an llvm-mingw root, or keep one in .toolchain/.
# C++/WinRT headers: set WINRT_INCLUDE, or generate them into .toolchain/winrt-include
# with cppwinrt.exe -in local -out .toolchain/winrt-include
set -euo pipefail
cd "$(dirname "$0")"

LLVM_MINGW="${LLVM_MINGW:-$(ls -d .toolchain/llvm-mingw-*-ucrt-x86_64 2>/dev/null | tail -1)}"
WINRT_INCLUDE="${WINRT_INCLUDE:-.toolchain/winrt-include}"
WEBVIEW2="${WEBVIEW2:-.toolchain/webview2/build/native}"  # Microsoft.Web.WebView2 NuGet package
[ -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" ] || { echo "llvm-mingw not found (set LLVM_MINGW)"; exit 1; }
[ -f "$WINRT_INCLUDE/winrt/base.h" ] || { echo "C++/WinRT headers not found (set WINRT_INCLUDE)"; exit 1; }

# One version for everything (exes, tray, settings app, installer, release name).
VERSION="$(tr -d ' \r\n' < VERSION)"
IFS=. read -r V_MAJOR V_MINOR V_PATCH <<< "${VERSION%%-*}"
VERSION_DEFS=(-DBUNBUN_VERSION_STR="\"$VERSION\"")
# Homepage for the User-Agent of web requests; CI sets it to the GitHub repo URL.
[ -n "${BUNBUN_HOMEPAGE:-}" ] && VERSION_DEFS+=(-DBUNBUN_HOMEPAGE_STR="\"$BUNBUN_HOMEPAGE\"")

CXX="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++"
RC="$LLVM_MINGW/bin/x86_64-w64-mingw32-windres"
mkdir -p build/obj

COMMON=(-std=c++20 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -municode "${VERSION_DEFS[@]}"
        -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable)
if [ "${1:-}" = debug ]; then OPT=(-O0 -g); LDOPT=(); else OPT=(-O2 -DNDEBUG); LDOPT=(-s); fi

compile() {  # src obj [extra flags...]
    local src=$1 obj=$2; shift 2
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "$(find src -name '*.h' -newer "$obj" -print -quit)" ] ||
       [ src/island/default_settings.inc -nt "$obj" ] || [ VERSION -nt "$obj" ] ||
       [ -n "$(find src -name '*.inc' -newer "$obj" -print -quit)" ]; then
        echo "  CXX $src"
        "$CXX" "${COMMON[@]}" "${OPT[@]}" "$@" -c "$src" -o "$obj"
    fi
}

echo "Building Bunbun"
pids=()
compile src/island/island_core.cpp build/obj/island_core.o -I"$WINRT_INCLUDE" -Wno-sign-compare -Wno-reorder-ctor & pids+=($!)
compile src/island/wh_shim.cpp     build/obj/wh_shim.o & pids+=($!)
compile src/agents/agent_bridge.cpp build/obj/agent_bridge.o & pids+=($!)
compile src/agents/hook_installer.cpp build/obj/hook_installer.o & pids+=($!)
compile src/mascot/mascot.cpp      build/obj/mascot.o & pids+=($!)
compile src/mascot/pack_sheet.cpp  build/obj/pack_sheet.o & pids+=($!)
compile src/assistant/assistant.cpp build/obj/assistant.o & pids+=($!)
compile src/main.cpp               build/obj/main.o & pids+=($!)
compile src/updater.cpp            build/obj/updater.o & pids+=($!)
compile src/hook/bunbun_hook.cpp   build/obj/bunbun_hook.o & pids+=($!)
compile src/settings/settings_host.cpp build/obj/settings_host.o -I"$WEBVIEW2/include" & pids+=($!)
failed=0
for p in "${pids[@]}"; do wait "$p" || failed=1; done
[ "$failed" = 0 ] || { echo "Build failed"; exit 1; }

"$RC" -I res -DBUNBUN_VER_COMMA="$V_MAJOR,$V_MINOR,$V_PATCH,0" -DBUNBUN_VERSION_STR="\\\"$VERSION\\\"" \
    res/bunbun.rc -O coff -o build/obj/bunbun_res.o

LIBS=(-lole32 -loleaut32 -luuid -lshcore -lshlwapi -ld2d1 -ldwrite -ldwmapi -lgdi32 -luser32 -lshell32 -lruntimeobject
      -lwindowscodecs -lsetupapi -lwinhttp -lpdh -liphlpapi -lwlanapi -lwinmm -ladvapi32 -lcomctl32
      -lmfuuid -lbcrypt)
echo "  LD  build/bunbun.exe"
"$CXX" "${LDOPT[@]}" -municode -mwindows -static -o build/bunbun.exe \
    build/obj/main.o build/obj/updater.o build/obj/island_core.o build/obj/wh_shim.o build/obj/agent_bridge.o \
    build/obj/hook_installer.o build/obj/mascot.o build/obj/pack_sheet.o build/obj/assistant.o build/obj/bunbun_res.o "${LIBS[@]}"
echo "  LD  build/bunbun-hook.exe"
# Agents run the hook before every tool call, so it must never be missing, not
# even while this links. Link to a temp file, then swap it in with two renames
# (Windows allows renaming a running exe). Old copies go in the next dev.sh push.
"$CXX" "${LDOPT[@]}" -municode -mconsole -static -o build/bunbun-hook.exe.new build/obj/bunbun_hook.o -ladvapi32
[ -f build/bunbun-hook.exe ] && mv -f build/bunbun-hook.exe "build/bunbun-hook.exe.old-$(date +%s%N)"
mv -f build/bunbun-hook.exe.new build/bunbun-hook.exe
echo "  LD  build/bunbun-settings.exe"
"$CXX" "${LDOPT[@]}" -municode -mwindows -static -o build/bunbun-settings.exe     build/obj/settings_host.o build/obj/wh_shim.o build/obj/mascot.o build/obj/pack_sheet.o build/obj/hook_installer.o build/obj/bunbun_res.o     -lole32 -loleaut32 -luuid -lshlwapi -lshell32 -ld2d1 -lwindowscodecs -ldwmapi -luser32 -ladvapi32 -lgdi32 -lwinhttp -lshcore
cp -f "$WEBVIEW2/x64/WebView2Loader.dll" build/

# Optional: bunbun-brain.exe, the local AI helper (docs/local-ai.md). Built only
# when llama.cpp has been fetched (scripts/setup.sh --local-ai), whose libraries
# are built once into .toolchain/llama-build. A normal build needs none of this.
LLAMA="${LLAMA:-.toolchain/llama.cpp}"
LLAMA_BUILD="${LLAMA_BUILD:-.toolchain/llama-build}"
CMAKE="${CMAKE:-.toolchain/cmake/bin/cmake.exe}"
if [ -f "$LLAMA/include/llama.h" ] && [ -x "$CMAKE" ]; then
    if [ ! -f "$LLAMA_BUILD/src/libllama.a" ]; then
        echo "  LLAMA $LLAMA_BUILD (first time, a minute or two)"
        # Portable CPU code (AVX2: any PC from about 2013), not tuned to this one.
        "$CMAKE" -S "$LLAMA" -B "$LLAMA_BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$(cygpath -am .toolchain/ninja/ninja.exe)" \
            -DCMAKE_C_COMPILER="$(cygpath -am "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang.exe")" \
            -DCMAKE_CXX_COMPILER="$(cygpath -am "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++.exe")" \
            -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
            -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_CURL=OFF -DGGML_NATIVE=OFF -DGGML_AVX2=ON \
            -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_OPENMP=OFF -DGGML_BACKEND_DL=OFF >/dev/null
        "$CMAKE" --build "$LLAMA_BUILD" --target llama ggml >/dev/null
    fi
    if [ ! -f build/bunbun-brain.exe ] || [ src/brain/bunbun_brain.cpp -nt build/bunbun-brain.exe ] || [ src/common/json.h -nt build/bunbun-brain.exe ]; then
        echo "  LD  build/bunbun-brain.exe"
        # Like the hook: link aside, then swap in (the island may be running the old one).
        "$CXX" -std=c++20 "${OPT[@]}" "${LDOPT[@]}" -I"$LLAMA/include" -I"$LLAMA/ggml/include" src/brain/bunbun_brain.cpp \
            "$LLAMA_BUILD/src/libllama.a" "$LLAMA_BUILD/ggml/src/ggml.a" "$LLAMA_BUILD/ggml/src/ggml-cpu.a" \
            "$LLAMA_BUILD/ggml/src/ggml-base.a" -static -o build/bunbun-brain.exe.new
        [ -f build/bunbun-brain.exe ] && mv -f build/bunbun-brain.exe "build/bunbun-brain.exe.old-$(date +%s%N)"
        mv -f build/bunbun-brain.exe.new build/bunbun-brain.exe
    fi
fi

# Settings UI (React + Vite): built when its sources are newer than the output.
if [ -d settings-ui ] && command -v npm >/dev/null; then
    if [ ! -f build/settings-ui/index.html ] || [ -n "$(find settings-ui/src settings-ui/index.html -newer build/settings-ui/index.html -print -quit)" ]; then
        echo "  UI  build/settings-ui"
        (cd settings-ui && { [ -d node_modules ] || npm ci --silent; } && npm run build --silent)
    fi
fi
ls -la build/*.exe
