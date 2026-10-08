#!/usr/bin/env bash
# Builds a game project for the web against a release archive the way games
# use it (add_subdirectory + serval_add_rom from the game's own directory,
# with cmake/web-toolchain.cmake), using examples/pong as the game, then runs
# the page headless and checks it draws (tools/web-shots.py). The same game is
# also built with a title and game code made of characters that mean
# something to CMake, HTML, JavaScript, JSON or Emscripten's processing of the
# page; each page must boot and draw, show its title and keep its saves under
# serval-save:<title as shown>:<game code>.
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
# Markup in titles and game codes (in CMake strings, \" is ", \\ is \ and \$ is $).
serval_add_rom(web_escapes SOURCES main.c TITLE " <&lt;\"'\\`> " GAME_CODE "\${}#")
serval_add_rom(web_markup SOURCES main.c TITLE "</title>&lt@" GAME_CODE "{{{\"")
serval_add_rom(web_references SOURCES main.c TITLE "&#60&#x3c&lt" GAME_CODE "&#62")
CMAKE

cmake -S "$work/game" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$work/game/serval-engine/cmake/web-toolchain.cmake"
cmake --build "$work/build"
"$root/tools/web-shots.py" --require-picture "$work/build/web_game.html" 300 "$work/shot" shot=300 \
    'expect-title=WEB GAME' 'expect-save-key=serval-save:WEB GAME:0000'
echo "check-consumer-web: $(wc -c <"$work/build/web_game.html") byte page boots and draws"
# The title as the browser shows it: without the spaces at either end.
"$root/tools/web-shots.py" --require-picture "$work/build/web_escapes.html" 300 "$work/escapes" \
    shot=300 'expect-title=<&lt;"'\''\`>' 'expect-save-key=serval-save:<&lt;"'\''\`>:${}#'
"$root/tools/web-shots.py" --require-picture "$work/build/web_markup.html" 300 "$work/markup" \
    shot=300 'expect-title=</title>&lt@' 'expect-save-key=serval-save:</title>&lt@:{{{"'
"$root/tools/web-shots.py" --require-picture "$work/build/web_references.html" 300 "$work/references" \
    shot=300 'expect-title=&#60&#x3c&lt' 'expect-save-key=serval-save:&#60&#x3c&lt:&#62'
echo "check-consumer-web: pages with markup in their title and game code boot, draw, show" \
     "their title and name their saves right"
