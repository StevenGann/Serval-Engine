#!/usr/bin/env bash
# Builds the release archive serval-engine-<version>.zip: only what building a
# game needs (docs/releases.md). Also copies serval.json next to it, so tools
# can read a release's manifest without downloading the archive, and writes
# serval-engine-<version>.zip.sha256.
#
# Only files tracked by git are packaged (from the working tree), so build
# output and other untracked files never end up in a release.
#
# Usage: tools/package-release.sh <output-dir>
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?usage: $0 <output-dir>}"
version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$root/serval.json")"
name="serval-engine-$version"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

files=(include src cmake third_party CMakeLists.txt CMakePresets.json serval.json LICENSE
       tools/gbafix.py tools/svm.py)
copy() {
    mkdir -p "$stage/$name/$(dirname "$1")"
    cp -R "$root/$1" "$stage/$name/$1"
}
if git -C "$root" rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    while IFS= read -r -d '' f; do
        copy "$f"
    done < <(git -C "$root" ls-files -z -- "${files[@]}")
else
    echo "warning: $root is not a git checkout; packaging every file" >&2
    for f in "${files[@]}"; do
        copy "$f"
    done
fi
for f in "${files[@]}"; do
    if [[ ! -e "$stage/$name/$f" ]]; then
        echo "error: $f is missing from the archive (not tracked by git?)" >&2
        exit 1
    fi
done

mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -f "$out/$name.zip"
(cd "$stage" && python3 -m zipfile -c "$out/$name.zip" "$name")
cp "$root/serval.json" "$out/serval.json"
python3 -c 'import hashlib, os, sys
path = sys.argv[1]
with open(path, "rb") as f:
    digest = hashlib.sha256(f.read()).hexdigest()
print(f"{digest}  {os.path.basename(path)}")' "$out/$name.zip" > "$out/$name.zip.sha256"
echo "$out/$name.zip"
