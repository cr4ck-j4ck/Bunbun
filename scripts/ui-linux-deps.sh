#!/bin/sh
# The settings UI's build tools (Vite: Rollup and esbuild) each need a native part
# built for the system they run on. A checkout shared by Windows and WSL has the
# node_modules Windows npm installed, with only the Windows parts, so the Linux build
# failed ("Cannot find module '@rollup/rollup-linux-x64-gnu'"). This adds the Linux
# parts beside them: `npm pack` downloads each package as a tarball and it's unpacked
# in place, so nothing else in node_modules changes (Windows keeps working).
# On an ordinary Linux checkout they're already there and this does nothing.
set -e
cd "$(dirname "$0")/../settings-ui"
[ -d node_modules/rollup ] && [ -d node_modules/esbuild ] || exit 0
case "$(uname -m)" in
    x86_64) arch=x64 ;;
    aarch64 | arm64) arch=arm64 ;;
    *) exit 0 ;;  # another CPU: leave it to npm
esac

add() {  # package, version
    dir="node_modules/$1"
    [ -f "$dir/package.json" ] && return 0
    echo "settings UI: adding $1@$2 for this system"
    tmp=$(mktemp -d)
    (cd "$tmp" && npm pack --silent "$1@$2" >/dev/null)
    mkdir -p "$dir"
    tar -xzf "$tmp"/*.tgz -C "$dir" --strip-components=1
    rm -rf "$tmp"
}

add "@rollup/rollup-linux-$arch-gnu" "$(node -p "require('./node_modules/rollup/package.json').version")"
add "@esbuild/linux-$arch" "$(node -p "require('./node_modules/esbuild/package.json').version")"
