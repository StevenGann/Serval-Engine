#!/usr/bin/env bash
# Builds every example for the GBA and for the web, and copies the results to
# examples/roms/<name>.gba and examples/html/<name>.html.
#
# Each example is built on its own, so one that fails to build does not stop
# the others. A platform whose toolchain is missing is skipped (with a note);
# the script fails if both are missing or any example failed.
#
# Usage: examples/build-all.sh [gba-preset [web-preset]]
#        (defaults: gba-release, web-release)
#
# GBA builds need an arm-none-eabi GCC on PATH, or ARM_GNU_TOOLCHAIN set to the
# toolchain's root directory; web builds need Emscripten (emsdk_env.sh
# sourced). See docs/development.md.
#
# An example is a directory under examples/ with a main.c; its CMake target
# (in examples/CMakeLists.txt) has the same name as the directory.
set -uo pipefail

examples_dir="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$examples_dir")"
gba_preset="${1:-gba-release}"
web_preset="${2:-web-release}"

built=()
failed=()
platforms=0

# build_platform <preset> <extension> <output directory>
build_platform() {
    local preset="$1" ext="$2" out="$examples_dir/$3"
    local logs="$root/build/$preset/example-logs"

    mkdir -p "build/$preset"
    echo "Configuring ($preset)..."
    if ! cmake --preset "$preset" > "build/$preset/configure.log" 2>&1; then
        cat "build/$preset/configure.log" >&2
        echo "error: $preset configure failed, so no example can be built for it." >&2
        failed+=("$preset")
        return
    fi

    mkdir -p "$out" "$logs"
    rm -f "$out"/*."$ext"
    for dir in "$examples_dir"/*/; do
        local name
        name="$(basename "$dir")"
        [[ -f "$dir/main.c" ]] || continue

        printf '%-24s' "$name.$ext"
        local log="$logs/$name.log"
        if cmake --build --preset "$preset" --target "$name" > "$log" 2>&1 &&
            cp "build/$preset/examples/$name.$ext" "$out/$name.$ext" 2>> "$log"; then
            echo "ok      examples/$3/$name.$ext"
            built+=("$name.$ext")
        else
            echo "FAILED  (log: ${log#"$root"/})"
            tail -n 15 "$log" | sed 's/^/    /'
            failed+=("$name.$ext")
        fi
    done
    echo
}

cd "$root"

if command -v arm-none-eabi-gcc > /dev/null || [[ -n "${ARM_GNU_TOOLCHAIN:-}" ]]; then
    platforms=$((platforms + 1))
    build_platform "$gba_preset" gba roms
else
    echo "GBA: skipped (arm-none-eabi-gcc not found; put it on PATH or set ARM_GNU_TOOLCHAIN)."
    echo
fi

if command -v emcc > /dev/null || [[ -n "${EMSDK:-}" ]]; then
    platforms=$((platforms + 1))
    build_platform "$web_preset" html html
else
    echo "Web: skipped (Emscripten not found; source emsdk_env.sh)."
    echo
fi

if [[ $platforms -eq 0 ]]; then
    echo "error: neither the ARM toolchain nor Emscripten was found." >&2
    exit 1
fi
echo "${#built[@]} built, ${#failed[@]} failed."
[[ ${#failed[@]} -eq 0 ]]
