#!/usr/bin/env bash
# Builds mGBA's headless ROM test runner (mgba-rom-test), used by CTest to run
# the engine's test ROM. Only the GBA core is built, with no optional deps.
#
# Usage: tools/build-mgba-rom-test.sh <install-dir> [mgba-version]
# Then point CMake at it: export MGBA_ROM_TEST_DIR=<install-dir>
set -euo pipefail

install_dir="${1:?usage: $0 <install-dir> [mgba-version]}"
version="${2:-0.10.5}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git clone --quiet --depth 1 --branch "$version" https://github.com/mgba-emu/mgba "$work/mgba"

# CMAKE_POLICY_VERSION_MINIMUM: mGBA 0.10 declares a CMake minimum that CMake 4 rejects.
cmake -S "$work/mgba" -B "$work/build" -G Ninja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ROM_TEST=ON \
    -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=OFF -DBUILD_PERF=OFF \
    -DBUILD_TEST=OFF -DBUILD_SUITE=OFF -DBUILD_CINEMA=OFF \
    -DBUILD_SHARED=OFF -DBUILD_STATIC=ON \
    -DM_CORE_GB=OFF -DENABLE_SCRIPTING=OFF \
    -DUSE_FFMPEG=OFF -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF \
    -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=OFF -DUSE_JSON_C=OFF -DUSE_EDITLINE=OFF \
    -DUSE_DISCORD_RPC=OFF -DUSE_EPOXY=OFF \
    > "$work/configure.log" || { cat "$work/configure.log"; exit 1; }
cmake --build "$work/build" --target mgba-rom-test

mkdir -p "$install_dir"
cp "$work/build/test/mgba-rom-test" "$install_dir/"
echo "Installed $install_dir/mgba-rom-test (mGBA $version)"
