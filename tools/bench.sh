#!/usr/bin/env bash
# Runs the benchmarks headless in mGBA and prints their results. Lower is
# better.
#   - bunnymark (bunnymark_bench): the average and peak CPU cycles per frame
#     with 128 bunnies.
#   - script benchmark (tests/bench): scriptmark (scriptmark_bench), a busy
#     scripted scene, and fireflies (fireflies_bench), a whole game in a
#     script: the VM's cycles a frame and its ops, a fit of the one against
#     the other (the cycles every frame pays + the cycles per op), the whole
#     frame, and a checksum of the run (it changes only when the scene runs
#     differently); scriptmark also gives the VM's fixed cost, with nothing
#     to run. docs/development.md#script-benchmark.
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
benches=(bunnymark_bench scriptmark_bench fireflies_bench)
if ! { cmake --preset "$preset" && cmake --build --preset "$preset" --target "${benches[@]}"; } > "$log" 2>&1; then
    cat "$log" >&2
    echo "error: building the benchmarks failed." >&2
    exit 1
fi

# GNU timeout is not on stock macOS (Homebrew coreutils calls it gtimeout).
limit=()
if command -v timeout > /dev/null; then
    limit=(timeout 300)
elif command -v gtimeout > /dev/null; then
    limit=(gtimeout 300)
fi
# run <ROM, under build/<preset>/> <name>: runs the ROM and keeps the lines it
# logs that start with "<name>:".
results=()
run() {
    local output result
    output="$(${limit[@]+"${limit[@]}"} "$runner" -S 3 -R r0 -l 15 "build/$preset/$1.gba" 2>&1)" || {
        echo "$output" >&2
        echo "error: the benchmark ROM $1 failed or timed out." >&2
        exit 1
    }
    result="$(grep -o "$2:.*" <<< "$output")" || {
        echo "$output" >&2
        echo "error: the benchmark ROM $1 did not report a result." >&2
        exit 1
    }
    results+=("$result")
}
run examples/bunnymark_bench bunnymark
results[0]="${results[0]} ($preset)"
run tests/bench/scriptmark_bench scriptmark
run tests/bench/fireflies_bench fireflies

printf '%s\n' "${results[@]}"
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    sha="${GITHUB_SHA:-unknown}"
    {
        printf '### Benchmark\n\n%s, commit %s\n\n```text\n' "$preset" "${sha:0:7}"
        printf '%s\n' "${results[@]}"
        printf '```\n'
    } >> "$GITHUB_STEP_SUMMARY"
fi
