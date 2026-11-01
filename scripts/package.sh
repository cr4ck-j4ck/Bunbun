#!/usr/bin/env bash
# Packages a release: dist/Bunbun-<version>-win-x64.zip plus SHA256SUMS.txt.
#
#   ./scripts/package.sh            build, then package
#   ./scripts/package.sh --no-build package what's already in build/ (e.g. while the island runs)
#
# The zip is portable: unzip anywhere and run bunbun.exe. Everything is linked
# statically, so the only runtime need is WebView2 (built into Windows 11, and
# into Windows 10 via Edge) for the settings window.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(tr -d ' \r\n' < VERSION)"
NAME="Bunbun-$VERSION-win-x64"
STAGE="dist/$NAME"

if [ "${1:-}" != "--no-build" ]; then
    ./build.sh
fi

# What the app needs at runtime, next to bunbun.exe.
required=(build/bunbun.exe build/bunbun-hook.exe build/bunbun-settings.exe build/WebView2Loader.dll build/settings-ui/index.html)
for f in "${required[@]}"; do
    [ -f "$f" ] || { echo "missing $f; run ./build.sh first" >&2; exit 1; }
done

rm -rf "$STAGE" "dist/$NAME.zip"
mkdir -p "$STAGE"
cp build/bunbun.exe build/bunbun-hook.exe build/bunbun-settings.exe build/WebView2Loader.dll "$STAGE/"
cp -r build/settings-ui "$STAGE/settings-ui"
cp README.md LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md "$STAGE/"
cat > "$STAGE/START HERE.txt" <<EOF
Bunbun $VERSION

1. Keep this folder somewhere permanent (for example %LOCALAPPDATA%\\Programs\\Bunbun).
   Connected coding agents call bunbun-hook.exe from here, so moving the folder
   later means connecting them again.
2. Run bunbun.exe. Bun appears in the tray and the island at the top of the screen.
3. Right-click Bun in the tray for Settings and Ask Bun (Ctrl+Alt+B).

Windows may show a SmartScreen warning because the app isn't code-signed yet:
choose "More info", then "Run anyway".

Docs: https://github.com/iaryasharma/Bunbun
EOF

# Zip with PowerShell (always present on Windows; Git Bash has no zip).
win_stage="$(cygpath -w "$PWD/$STAGE")"
win_zip="$(cygpath -w "$PWD/dist/$NAME.zip")"
powershell -NoProfile -Command "Compress-Archive -Path '$win_stage' -DestinationPath '$win_zip' -CompressionLevel Optimal -Force"

(cd dist && sha256sum "$NAME.zip" > SHA256SUMS.txt)
echo "Packaged dist/$NAME.zip ($(du -h "dist/$NAME.zip" | cut -f1))"
cat dist/SHA256SUMS.txt
