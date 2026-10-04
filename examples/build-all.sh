#!/usr/bin/env bash
# Builds every example and copies its ROM to examples/roms/<name>.gba.
#
# Each example is built on its own, so one that fails to build does not stop
# the others. Exits non-zero if any example failed.
#
# Usage: examples/build-all.sh [preset]     (default: gba-release)
#
# Needs an arm-none-eabi GCC on PATH, or ARM_GNU_TOOLCHAIN set to the
# toolchain's root directory. See docs/development.md.
#
# An example is a directory under examples/ with a main.c; its CMake target
# (in examples/CMakeLists.txt) has the same name as the directory.
set -uo pipefail

examples_dir="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$examples_dir")"
preset="${1:-gba-release}"
out="$examples_dir/roms"
logs="$root/build/$preset/example-logs"

if ! command -v arm-none-eabi-gcc > /dev/null && [[ -z "${ARM_GNU_TOOLCHAIN:-}" ]]; then
    echo "error: arm-none-eabi-gcc not found. Put it on PATH or set ARM_GNU_TOOLCHAIN." >&2
    exit 1
fi

cd "$root"
mkdir -p "build/$preset"
echo "Configuring ($preset)..."
if ! cmake --preset "$preset" > "build/$preset/configure.log" 2>&1; then
    cat "build/$preset/configure.log" >&2
    echo "error: configure failed, so no example can be built." >&2
    exit 1
fi

mkdir -p "$out" "$logs"
rm -f "$out"/*.gba

built=()
failed=()
for dir in "$examples_dir"/*/; do
    name="$(basename "$dir")"
    [[ -f "$dir/main.c" ]] || continue

    printf '%-24s' "$name"
    log="$logs/$name.log"
    if cmake --build --preset "$preset" --target "$name" > "$log" 2>&1 &&
        cp "build/$preset/examples/$name.gba" "$out/$name.gba" 2>> "$log"; then
        echo "ok      examples/roms/$name.gba"
        built+=("$name")
    else
        echo "FAILED  (log: ${log#"$root"/})"
        tail -n 15 "$log" | sed 's/^/    /'
        failed+=("$name")
    fi
done

echo
echo "${#built[@]} built, ${#failed[@]} failed."
[[ ${#failed[@]} -eq 0 ]]
