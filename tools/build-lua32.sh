#!/usr/bin/env bash
# Builds Lua 5.4 with 32-bit integers and floats (LUA_32BITS) into
# DIR/bin/lua: the reference tools/svlua_difftest.py compares compiled Lua
# with (docs/lua.md "Testing"; CTest svlua_difftest runs it when SERVAL_LUA32
# names this binary). The source comes from lua.org, is checked against the
# SHA-256 lua.org publishes for it, and is built in a temporary directory
# with luaconf.h's LUA_32BITS set to 1.
#
# Usage: tools/build-lua32.sh DIR
#
# Used by tools/setup-dev.sh --with-lua32 (DIR: ~/opt/lua-5.4.8-32) and by
# CI's host job (cached). Keep the version in those two in sync.
set -euo pipefail

LUA_VERSION=5.4.8
# From https://www.lua.org/ftp/ ("checksum (sha256)").
LUA_SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae

out="${1:?usage: $0 DIR}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

archive="$work/download/lua-$LUA_VERSION.tar.gz"
mkdir "$work/download" "$work/src"
curl -fsSL -o "$archive" "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
echo "$LUA_SHA256  $archive" | sha256sum -c --quiet || {
    echo "error: lua-$LUA_VERSION.tar.gz doesn't match its published checksum; not built" >&2
    exit 1
}
tar -xzf "$archive" -C "$work/src"
src="$work/src/lua-$LUA_VERSION/src"

tab=$'\t'
sed -i "s/^#define LUA_32BITS${tab}0\$/#define LUA_32BITS${tab}1/" "$src/luaconf.h"
grep -q "^#define LUA_32BITS${tab}1\$" "$src/luaconf.h" || {
    echo "error: no '#define LUA_32BITS 0' line to change in luaconf.h" >&2
    exit 1
}
make -C "$src" posix > "$work/build.log" 2>&1 || {
    tail -30 "$work/build.log" >&2
    echo "error: building Lua $LUA_VERSION failed" >&2
    exit 1
}
says="$("$src/lua" -e "io.write(_VERSION, ' ', math.maxinteger, ' ', string.packsize('n'))")"
if [[ "$says" != "Lua 5.4 2147483647 4" ]]; then
    echo "error: the built lua says '$says', not Lua 5.4 with 32-bit integers and floats" >&2
    exit 1
fi

mkdir -p "$out/bin"
cp "$src/lua" "$out/bin/lua"
echo "$LUA_VERSION" > "$out/.version"
echo "$out/bin/lua"
