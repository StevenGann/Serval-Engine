#!/usr/bin/env bash
# Builds the release archive serval-engine-<version>.zip: only what building a
# game needs (docs/releases.md). Also copies serval.json next to it, so tools
# can read a release's manifest without downloading the archive.
#
# Usage: tools/package-release.sh <output-dir>
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?usage: $0 <output-dir>}"
version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$root/serval.json")"
name="serval-engine-$version"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name/tools"

cp -r "$root/include" "$root/src" "$root/cmake" "$root/third_party" "$stage/$name/"
cp "$root/CMakeLists.txt" "$root/CMakePresets.json" "$root/serval.json" "$root/LICENSE" "$stage/$name/"
cp "$root/tools/gbafix.py" "$stage/$name/tools/"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -f "$out/$name.zip"
(cd "$stage" && python3 -m zipfile -c "$out/$name.zip" "$name")
cp "$root/serval.json" "$out/serval.json"
echo "$out/$name.zip"
