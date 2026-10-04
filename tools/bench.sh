#!/usr/bin/env bash
# Runs the bunnymark benchmark headless in mGBA and prints its result: the
# average and peak CPU cycles per frame with 128 bunnies. Lower is better.
#
# Usage: tools/bench.sh [preset]     (default: gba-release)
#
# Needs the ARM toolchain (see docs/development.md) and mgba-rom-test, found
# in $MGBA_ROM_TEST_DIR or on PATH (build it with tools/build-mgba-rom-test.sh).
# In GitHub Actions, the result is also added to the job summary.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
preset="${1:-gba-release}"
cd "$root"

runner="${MGBA_ROM_TEST_DIR:+$MGBA_ROM_TEST_DIR/}mgba-rom-test"
if ! command -v "$runner" > /dev/null; then
    echo "error: mgba-rom-test not found. Set MGBA_ROM_TEST_DIR or put it on PATH." >&2
    exit 1
fi

mkdir -p "build/$preset"
log="build/$preset/bench-build.log"
if ! { cmake --preset "$preset" && cmake --build --preset "$preset" --target bunnymark_bench; } > "$log" 2>&1; then
    cat "$log" >&2
    echo "error: building bunnymark_bench failed." >&2
    exit 1
fi

output="$(timeout 300 "$runner" -S 3 -R r0 -l 15 "build/$preset/examples/bunnymark_bench.gba" 2>&1)" || {
    echo "$output" >&2
    echo "error: the benchmark ROM failed or timed out." >&2
    exit 1
}
result="$(grep -o 'bunnymark:.*' <<< "$output")" || {
    echo "$output" >&2
    echo "error: the benchmark ROM did not report a result." >&2
    exit 1
}

echo "$result ($preset)"
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    printf '### Benchmark\n\n`%s` (%s, commit %s)\n' "$result" "$preset" "${GITHUB_SHA:0:7}" >> "$GITHUB_STEP_SUMMARY"
fi
