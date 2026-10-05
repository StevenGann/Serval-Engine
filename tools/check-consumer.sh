#!/usr/bin/env bash
# Builds tests/consumer, a minimal game project, against a release archive the
# way games use it (add_subdirectory + serval_add_rom from the game's own
# directory), then runs it in mGBA and checks the ROM (padded, valid header,
# no libc.a, no BLX, unused game code dropped).
#
# Usage: tools/check-consumer.sh <serval-engine-X.Y.Z.zip> [build-type]
#
# Needs the ARM toolchain and mgba-rom-test (see docs/development.md).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
zip="$(cd "$(dirname "${1:?usage: $0 <release.zip> [build-type]}")" && pwd)/$(basename "$1")"
build_type="${2:-Release}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 -m zipfile -e "$zip" "$work/engine"
engine=("$work"/engine/serval-engine-*)
if [[ ${#engine[@]} -ne 1 || ! -f "${engine[0]}/serval.json" ]]; then
    echo "error: $zip does not contain one serval-engine-*/ directory" >&2
    exit 1
fi

cmake -S "$root/tests/consumer" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_TOOLCHAIN_FILE="${engine[0]}/cmake/arm-gba-toolchain.cmake" \
    -DSERVAL_ENGINE_DIR="${engine[0]}"
cmake --build "$work/build"
ctest --test-dir "$work/build" --output-on-failure --no-tests=error
