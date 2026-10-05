#!/usr/bin/env bash
# Builds a game project for the web against a release archive the way games
# use it (add_subdirectory + serval_add_rom from the game's own directory,
# with cmake/web-toolchain.cmake), using examples/pong as the game, then runs
# the page headless and checks it draws (tools/web-shots.py).
#
# Usage: tools/check-consumer-web.sh <serval-engine-X.Y.Z.zip>
#
# Needs Emscripten (emsdk_env.sh sourced) and Chrome or Chromium.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
zip="$(cd "$(dirname "${1:?usage: $0 <release.zip>}")" && pwd)/$(basename "$1")"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir "$work/game"
python3 -m zipfile -e "$zip" "$work/game"
mv "$work"/game/serval-engine-* "$work/game/serval-engine"
cp "$root/examples/pong/main.c" "$work/game/"
cat >"$work/game/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.25)
project(web_game LANGUAGES C)
add_subdirectory(serval-engine)
serval_add_rom(web_game SOURCES main.c TITLE "WEB GAME")
CMAKE

cmake -S "$work/game" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$work/game/serval-engine/cmake/web-toolchain.cmake"
cmake --build "$work/build"
"$root/tools/web-shots.py" --require-picture "$work/build/web_game.html" 300 "$work/shot" shot=300
echo "check-consumer-web: $(wc -c <"$work/build/web_game.html") byte page boots and draws"
