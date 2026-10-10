#!/usr/bin/env bash
# Sets up a Linux machine to build, test and run Serval Engine: the same
# tools and versions CI uses. Safe to run again: whatever is already
# installed is checked and kept, so a second run only reports.
#
#   tools/setup-dev.sh [--prefix DIR] [--no-system] [--no-web] [--no-rom-tests]
#                      [--with-lua32] [--add-to-shell]
#
# Installs:
#   - system packages (apt, with sudo, only those missing): CMake, Ninja,
#     Python 3 (3.11 or later), a host C compiler, git, curl, xz and
#     clang-format 18
#   - the ARM GNU Toolchain (arm-none-eabi-gcc) into DIR, checksum-verified
#   - mgba-rom-test, built from mGBA's source into DIR/mgba-rom-test
#     (tools/build-mgba-rom-test.sh); runs the test ROM
#   - Emscripten (emsdk) into DIR/emsdk, for web builds
#   - BlocksDS's mmutil, built from source into DIR/mmutil-<version>
#     (tools/build-mmutil.sh, the version serval.json's toolchain.mmutil
#     names); builds sound banks (serval_add_soundbank)
#   - with --with-lua32: Lua 5.4 built with 32-bit integers (LUA_32BITS),
#     checksum-verified, into DIR/lua-5.4.8-32 (tools/build-lua32.sh), for
#     the Lua compiler's differential test (CTest svlua_difftest)
# and writes DIR/serval-env.sh, which sets ARM_GNU_TOOLCHAIN,
# MGBA_ROM_TEST_DIR, EMSDK and SERVAL_MMUTIL for CMake and
# examples/build-all.sh, and SERVAL_LUA32 when that Lua is installed. Source it
# from your shell startup (--add-to-shell does that for ~/.profile and
# ~/.bashrc). DIR defaults to ~/opt.
#
# Options:
#   --prefix DIR     where the toolchains go (default ~/opt)
#   --no-system      don't install system packages; only check for them
#   --no-web         skip Emscripten
#   --no-rom-tests   skip mgba-rom-test
#   --with-lua32     also build Lua 5.4 with 32-bit integers (optional)
#   --add-to-shell   source DIR/serval-env.sh from ~/.profile and ~/.bashrc
#
# Chromium (for tools/web-shots.py) is optional and not installed here.
set -euo pipefail

# Versions: keep in sync with CI (.github/actions/setup-gba/action.yml,
# .github/workflows/ci.yml and pages.yml) and serval.json's toolchain.gcc.
ARM_RELEASE=15.3.rel1
ARM_SHA256_x86_64=563bebb2b97d53382b956d6ee1fe61e2cae26699901417234a37df505ef9b5fa
ARM_SHA256_aarch64=06979e0c8171de58e5dc2a2b2019330a290f30930f27728af98a83e1a7369b3a
ARM_URL=https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain
MGBA_VERSION=0.10.5
EMSDK_VERSION=6.0.11
CLANG_FORMAT_MAJOR=18
CMAKE_MIN=3.25
PYTHON_MIN=3.11 # tools/svlua.py's (cmake/Serval.cmake checks it to compile Lua scripts)
LUA_VERSION=5.4.8 # tools/build-lua32.sh's; ci.yml's host job caches the same
MMUTIL_VERSION=1.24.0-blocks # serval.json's toolchain.mmutil; tools/build-mmutil.sh's

prefix="$HOME/opt"
system=1 web=1 rom_tests=1 lua32=0 add_to_shell=0
while (($#)); do
    case "$1" in
    --prefix) prefix="${2:?--prefix needs a directory}"; shift ;;
    --no-system) system=0 ;;
    --no-web) web=0 ;;
    --no-rom-tests) rom_tests=0 ;;
    --with-lua32) lua32=1 ;;
    --add-to-shell) add_to_shell=1 ;;
    -h | --help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd)"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

say() { printf '\n== %s\n' "$*"; }
ok() { printf '   %-15s %s\n' "$1" "$2"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Downloads go into a fresh empty directory, are checked, then removed.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

[[ "$(uname -s)" == Linux ]] ||
    die "this script supports Linux only; on $(uname -s), install the tools in docs/development.md#requirements by hand"
arch="$(uname -m)"
case "$arch" in
x86_64 | aarch64) ;;
*) die "no ARM GNU Toolchain build for $arch; see docs/development.md#requirements" ;;
esac

# --- System packages ---------------------------------------------------------

say "System packages"
# command -> Debian/Ubuntu package
declare -A packages=(
    [cmake]=cmake [ninja]=ninja-build [python3]=python3 [cc]=gcc [git]=git
    [curl]=curl [xz]=xz-utils [clang-format-$CLANG_FORMAT_MAJOR]=clang-format-$CLANG_FORMAT_MAJOR
)
missing=()
for cmd in "${!packages[@]}"; do
    command -v "$cmd" > /dev/null || missing+=("${packages[$cmd]}")
done
if ((${#missing[@]})); then
    if ((system)) && command -v apt-get > /dev/null; then
        echo "   installing: ${missing[*]} (sudo)"
        sudo apt-get update -qq
        sudo apt-get install -y "${missing[@]}"
    else
        die "missing: ${missing[*]} (Debian/Ubuntu package names; install them, then run this again)"
    fi
fi
cmake_version="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')"
[[ "$(printf '%s\n%s\n' "$CMAKE_MIN" "$cmake_version" | sort -V | head -1)" == "$CMAKE_MIN" ]] ||
    die "CMake $cmake_version is older than $CMAKE_MIN; install a newer one (e.g. pipx install cmake, or Kitware's apt repository)"
python_version="$(python3 --version | cut -d' ' -f2)"
[[ "$(printf '%s\n%s\n' "$PYTHON_MIN" "$python_version" | sort -V | head -1)" == "$PYTHON_MIN" ]] ||
    die "Python $python_version is older than $PYTHON_MIN, which compiling Lua scripts needs (tools/svlua.py); install a newer python3 (e.g. Ubuntu 24.04's, or the deadsnakes PPA's python3.11) and put it first on PATH"
ok cmake "$cmake_version"
ok ninja "$(ninja --version)"
ok python3 "$python_version"
ok clang-format "$(clang-format-$CLANG_FORMAT_MAJOR --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"

# The repository's commands call plain `clang-format`; if that isn't version
# 18, serval-env.sh puts a clang-format -> clang-format-18 link first on PATH.
bin_dir=""
if [[ "$(clang-format --version 2> /dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f1)" != "$CLANG_FORMAT_MAJOR" ]]; then
    bin_dir="$prefix/serval-bin"
    mkdir -p "$bin_dir"
    ln -sf "$(command -v clang-format-$CLANG_FORMAT_MAJOR)" "$bin_dir/clang-format"
    ok "" "clang-format -> clang-format-$CLANG_FORMAT_MAJOR in $bin_dir"
fi

# --- ARM GNU Toolchain -------------------------------------------------------

say "ARM GNU Toolchain $ARM_RELEASE"
arm_name="arm-gnu-toolchain-$ARM_RELEASE-$arch-arm-none-eabi"
arm_dir="$prefix/$arm_name"
arm_gcc="$arm_dir/bin/arm-none-eabi-gcc"
if [[ -x "$arm_gcc" ]] && "$arm_gcc" --version | head -1 | grep -qi "$ARM_RELEASE"; then
    ok installed "$arm_dir"
else
    sha_var="ARM_SHA256_$arch"
    mkdir "$work/arm"
    echo "   downloading $arm_name.tar.xz"
    progress=-sS # a progress bar only on a terminal, not in logs
    [[ -t 2 ]] && progress=--progress-bar
    curl -fL "$progress" -o "$work/arm/$arm_name.tar.xz" "$ARM_URL/$ARM_RELEASE/$arm_name.tar.xz"
    echo "${!sha_var}  $work/arm/$arm_name.tar.xz" | sha256sum -c --quiet ||
        die "checksum mismatch for $arm_name.tar.xz; not installed"
    rm -rf "$arm_dir"
    tar -xJf "$work/arm/$arm_name.tar.xz" -C "$prefix"
    [[ -x "$arm_gcc" ]] || die "the archive didn't contain $arm_name/bin/arm-none-eabi-gcc"
    ok installed "$arm_dir"
fi
ok version "$("$arm_gcc" --version | head -1)"

# --- mgba-rom-test -----------------------------------------------------------

mgba_dir="$prefix/mgba-rom-test"
if ((rom_tests)); then
    say "mgba-rom-test (mGBA $MGBA_VERSION)"
    if [[ -x "$mgba_dir/mgba-rom-test" && "$(cat "$mgba_dir/.version" 2> /dev/null)" == "$MGBA_VERSION" ]]; then
        ok installed "$mgba_dir"
    elif [[ -x "$mgba_dir/mgba-rom-test" && ! -e "$mgba_dir/.version" ]]; then
        # Built before this script recorded versions: keep it.
        ok installed "$mgba_dir (version not recorded; delete it to rebuild $MGBA_VERSION)"
    else
        echo "   building from source (a minute or two)"
        "$repo/tools/build-mgba-rom-test.sh" "$mgba_dir" "$MGBA_VERSION" > "$work/mgba.log" 2>&1 ||
            { tail -30 "$work/mgba.log"; die "building mgba-rom-test failed"; }
        echo "$MGBA_VERSION" > "$mgba_dir/.version"
        ok installed "$mgba_dir"
    fi
fi

# --- Emscripten --------------------------------------------------------------

emsdk_dir="$prefix/emsdk"
if ((web)); then
    say "Emscripten $EMSDK_VERSION"
    emcc="$emsdk_dir/upstream/emscripten/emcc"
    if [[ -x "$emcc" ]] && "$emcc" --version 2> /dev/null | head -1 | grep -qF " $EMSDK_VERSION "; then
        ok installed "$emsdk_dir"
    else
        [[ -d "$emsdk_dir/.git" ]] ||
            git clone --quiet https://github.com/emscripten-core/emsdk.git "$emsdk_dir"
        echo "   installing (several hundred MB)"
        # An older emsdk checkout may not know the version yet: update and retry.
        "$emsdk_dir/emsdk" install "$EMSDK_VERSION" > "$work/emsdk.log" 2>&1 ||
            { git -C "$emsdk_dir" pull --quiet --ff-only &&
                "$emsdk_dir/emsdk" install "$EMSDK_VERSION" > "$work/emsdk.log" 2>&1; } ||
            { tail -30 "$work/emsdk.log"; die "emsdk install $EMSDK_VERSION failed"; }
        "$emsdk_dir/emsdk" activate "$EMSDK_VERSION" > "$work/emsdk.log" 2>&1 ||
            { tail -30 "$work/emsdk.log"; die "emsdk activate $EMSDK_VERSION failed"; }
        ok installed "$emsdk_dir"
    fi
fi

# --- mmutil ------------------------------------------------------------------

say "mmutil $MMUTIL_VERSION (BlocksDS)"
mmutil_dir="$prefix/mmutil-$MMUTIL_VERSION"
mmutil_bin="$mmutil_dir/mmutil"
if [[ -x "$mmutil_bin" && "$("$mmutil_bin" -V 2> /dev/null)" == "mmutil v$MMUTIL_VERSION" ]]; then
    ok installed "$mmutil_dir"
else
    "$repo/tools/build-mmutil.sh" "$mmutil_dir" > /dev/null || die "building mmutil $MMUTIL_VERSION failed"
    ok installed "$mmutil_dir"
fi

# --- Lua 5.4 with 32-bit integers (optional) ----------------------------------

lua_dir="$prefix/lua-$LUA_VERSION-32"
lua_bin="$lua_dir/bin/lua"
lua32_ok() {
    [[ -x "$lua_bin" && "$(cat "$lua_dir/.version" 2> /dev/null)" == "$LUA_VERSION" &&
        "$("$lua_bin" -e "io.write(_VERSION, ' ', math.maxinteger)" 2> /dev/null)" == "Lua 5.4 2147483647" ]]
}
if ((lua32)); then
    say "Lua $LUA_VERSION with 32-bit integers (LUA_32BITS)"
    if lua32_ok; then
        ok installed "$lua_dir"
    else
        "$repo/tools/build-lua32.sh" "$lua_dir" > /dev/null || die "building Lua $LUA_VERSION failed"
        ok installed "$lua_dir"
    fi
fi

# --- Chromium (optional) -----------------------------------------------------

say "Chromium (optional, for tools/web-shots.py)"
chrome="${SERVAL_CHROME:-}"
for c in chromium chromium-browser google-chrome google-chrome-stable; do
    [[ -n "$chrome" ]] && break
    chrome="$(command -v "$c" || true)"
done
if [[ -n "$chrome" ]]; then
    ok found "$chrome"
else
    ok missing "install Chromium or Chrome (Ubuntu: sudo snap install chromium) to take web screenshots"
fi

# --- Environment -------------------------------------------------------------

env_file="$prefix/serval-env.sh"
{
    echo "# Written by Serval Engine's tools/setup-dev.sh: where the toolchains are."
    echo "# CMake, CTest and examples/build-all.sh read these. Safe to source more than once."
    echo "export ARM_GNU_TOOLCHAIN=\"$arm_dir\""
    if ((rom_tests)); then
        echo "export MGBA_ROM_TEST_DIR=\"$mgba_dir\""
    fi
    # EMSDK alone is enough (cmake/web-toolchain.cmake): no emsdk_env.sh, so
    # emsdk's own node, python and clang stay off PATH.
    if ((web)); then
        echo "export EMSDK=\"$emsdk_dir\""
    fi
    echo "export SERVAL_MMUTIL=\"$mmutil_bin\""
    if [[ -n "$bin_dir" ]]; then
        echo "case \":\$PATH:\" in *\":$bin_dir:\"*) ;; *) export PATH=\"$bin_dir:\$PATH\" ;; esac"
    fi
    # Kept once installed, also when a later run doesn't ask for it.
    if lua32_ok; then
        echo "export SERVAL_LUA32=\"$lua_bin\""
    fi
} > "$env_file"
say "Environment"
ok written "$env_file"

source_line="[ -f \"$env_file\" ] && . \"$env_file\""
if ((add_to_shell)); then
    # ~/.profile: the whole login session (IDEs included). ~/.bashrc: every
    # bash shell; first line, so it comes before any "not interactive" return.
    if ! grep -qF "$source_line" "$HOME/.profile" 2> /dev/null; then
        printf '\n# Serval Engine toolchains (tools/setup-dev.sh)\n%s\n' "$source_line" >> "$HOME/.profile"
        ok added "to ~/.profile"
    fi
    if ! grep -qF "$source_line" "$HOME/.bashrc" 2> /dev/null; then
        { printf '# Serval Engine toolchains (tools/setup-dev.sh)\n%s\n\n' "$source_line"
          cat "$HOME/.bashrc" 2> /dev/null || true; } > "$work/bashrc"
        cat "$work/bashrc" > "$HOME/.bashrc"
        ok added "to the top of ~/.bashrc"
    fi
    ok "" "open a new terminal (or: source $env_file); IDEs see it after your next login"
elif [[ "${ARM_GNU_TOOLCHAIN:-}" != "$arm_dir" ]] &&
    ! grep -qsF "$env_file" "$HOME/.profile" "$HOME/.bashrc"; then
    ok "" "not in your shell startup yet: run again with --add-to-shell, or add"
    ok "" "  $source_line"
fi

say "Done. Next:"
cat << EOF
   source $env_file
   cmake --preset gba-debug && cmake --build --preset gba-debug && ctest --preset gba-debug
   cmake --preset host && cmake --build --preset host && ctest --preset host
   cmake --preset web && cmake --build --preset web
   examples/build-all.sh
EOF
