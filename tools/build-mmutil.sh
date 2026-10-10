#!/usr/bin/env bash
# Builds BlocksDS's mmutil, the tool that builds Maxmod sound banks
# (docs/audio.md#sound-bank), into DIR/mmutil, with its licence (ISC) beside
# it as DIR/COPYING. The version must match the Maxmod in third_party/maxmod
# (its VENDORED.md) and serval.json's toolchain.mmutil: a bank is read by the
# Maxmod that the matching mmutil was written for.
#
# The source is cloned at the pinned tag and checked against the pinned
# commit. mmutil embeds its demo ROMs with C23's #embed (GCC 15, Clang 19);
# for an older compiler the #embed lines are expanded to byte lists first, so
# any C99 compiler builds it.
#
# Usage: tools/build-mmutil.sh DIR [CC]
#
# Used by tools/setup-dev.sh (DIR: ~/opt/mmutil-<version>) and by CI (cached).
# Keep the version in serval.json, VENDORED.md and those two in sync.
set -euo pipefail

MMUTIL_VERSION=1.24.0-blocks
MMUTIL_COMMIT=f8abd4f40bd42023c2e4bfcc127841fd7ceecf1e

out="${1:?usage: $0 DIR [CC]}"
cc="${2:-${CC:-cc}}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

src="$work/mmutil"
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "v$MMUTIL_VERSION" \
    https://github.com/blocksds/mmutil.git "$src"
commit="$(git -C "$src" rev-parse HEAD)"
if [[ "$commit" != "$MMUTIL_COMMIT" ]]; then
    echo "error: mmutil's tag v$MMUTIL_VERSION is commit $commit, not the pinned $MMUTIL_COMMIT; not built" >&2
    exit 1
fi

# Does the compiler know #embed?
mkdir "$work/probe"
printf 'x' > "$work/probe/byte.bin"
printf 'const unsigned char b[] = {\n#embed "byte.bin"\n};\n' > "$work/probe/probe.c"
if ! "$cc" -c -o "$work/probe/probe.o" -I"$work/probe" "$work/probe/probe.c" 2> /dev/null; then
    # Expand each '#embed "file"' (the file in data/) into a byte list.
    while IFS= read -r file; do
        python3 -I - "$file" "$src/data" << 'EOF'
import pathlib, re, sys

source, data = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])

def expand(match):
    blob = (data / match.group(1)).read_bytes()
    return ",\n".join(",".join(str(b) for b in blob[i:i + 32]) for i in range(0, len(blob), 32))

text = source.read_text(encoding="utf-8")
source.write_text(re.sub(r'^#embed "([^"]+)"$', expand, text, flags=re.M), encoding="utf-8")
EOF
    done < <(grep -l '^#embed ' "$src"/source/*.c)
fi

make -C "$src" HOSTCC="$cc" VERSION_STRING="v$MMUTIL_VERSION" > "$work/build.log" 2>&1 || {
    tail -30 "$work/build.log" >&2
    echo "error: building mmutil $MMUTIL_VERSION failed" >&2
    exit 1
}
says="$("$src/mmutil" -V)"
if [[ "$says" != "mmutil v$MMUTIL_VERSION" ]]; then
    echo "error: the built mmutil says '$says', not 'mmutil v$MMUTIL_VERSION'" >&2
    exit 1
fi

mkdir -p "$out"
cp "$src/mmutil" "$out/mmutil"
cp "$src/COPYING" "$out/COPYING"
echo "$MMUTIL_VERSION" > "$out/.version"
echo "$out/mmutil"
