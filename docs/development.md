# Development

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 3.25 | Uses presets (`CMakePresets.json`) |
| Ninja | any recent | Generator for all presets |
| ARM GNU Toolchain | 15.3.Rel1 (what CI uses) | Any `arm-none-eabi-gcc` should work, including devkitARM. Found on `PATH` or via `ARM_GNU_TOOLCHAIN=<toolchain root>` |
| Python 3 | any recent | Runs `tools/gbafix.py` after each ROM link |
| Host C compiler | GCC or Clang | Only for host unit tests |
| `mgba-rom-test` | mGBA 0.10.5 | Optional locally, runs the test ROM. Build it with `tools/build-mgba-rom-test.sh <dir>` and set `MGBA_ROM_TEST_DIR=<dir>` |

Download the ARM GNU Toolchain from <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads> (`arm-none-eabi`, for your host).

## Build and test

```sh
# GBA: engine library, example ROMs and the test ROM
export ARM_GNU_TOOLCHAIN=~/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi
export MGBA_ROM_TEST_DIR=~/opt/mgba-rom-test
cmake --preset gba-debug
cmake --build --preset gba-debug
ctest --preset gba-debug          # runs build/gba-debug/tests/serval_tests.gba in mGBA

# Host: platform-neutral modules, with AddressSanitizer and UBSan
cmake --preset host
cmake --build --preset host
ctest --preset host
```

| Preset | Target | Notes |
| --- | --- | --- |
| `gba-debug` / `gba-release` | GBA | |
| `gba-ci` | GBA | RelWithDebInfo, warnings as errors; used by CI |
| `host` | Native | Debug, sanitizers, warnings as errors |

ROMs are written next to their ELF files, e.g. `build/gba-debug/examples/hello.gba`, with a linker map (`.map`). Open them in any GBA emulator.

To build every example for a demo:

```sh
examples/build-all.sh            # optional preset argument, default gba-release
# -> examples/roms/<name>.gba (git-ignored)
```

Each example builds separately, so one that fails to build doesn't stop the rest; failures are listed with the end of their build log, and the script exits non-zero. CI runs it too.

## Source layout

| Path | Contents |
| --- | --- |
| `include/serval/` | Public headers: `serval.h` (umbrella), `core.h`, `screen.h`, `sprites.h`, `ecs.h`, `platform.h`; `gba.h` holds GBA-only escape hatches. No third-party includes |
| `src/ecs/` | Platform-neutral modules (`serval_portable`), built for the GBA and for the host |
| `src/gba/` | GBA-only code (`serval`): core API (`core.c`), sprites (`sprites.c`), startup code (`crt0.s`), linker script (`gba.ld`), `memcpy` and friends (`libc.c`) |
| `third_party/libtonc/` | Vendored libtonc, see its `VENDORED.md` |
| `tests/` | Shared test cases (`ecs_tests.c`), the harness, and the host and ROM runners |
| `examples/` | Example ROMs, one directory each, plus `build-all.sh` |
| `cmake/` | Toolchain file and `serval_add_rom()` |
| `tools/` | ROM header fixer, mGBA test runner build, release packaging |

## Building a game

`serval_add_rom()` (in `cmake/Serval.cmake`) links sources against the engine with its own startup code and linker script, no C library, then produces the `.gba` file with a fixed header:

```cmake
serval_add_rom(my_game SOURCES main.c TITLE "MY GAME" GAME_CODE "MYGM")
```

## Tests

Test cases use the small harness in `tests/test.h` (`CHECK(cond)`, `TEST_SUITE(...)`), which needs no C library.

- **Platform-neutral suites** (e.g. `ecs_tests.c`) run both natively and in the test ROM.
- **Hardware suites** (`tests/rom/`) run only in the test ROM.

The test ROM writes results to mGBA's debug log and ends with `swi 3`, passing the number of failed checks in `r0`; `mgba-rom-test -S 3 -R r0` turns that into its exit code. Register new suites in `tests/host/main.c` and/or `tests/rom/main.c`.

Two compile-only checks keep third-party libraries behind the API ([core-api.md](core-api.md#dependencies-stay-behind-the-api)):

- **Host build:** `tests/public_headers.c` and every example's sources compile without libtonc on the include path. Add new examples to `serval_api_only_check` in `tests/CMakeLists.txt`.
- **Test ROM:** `tests/rom/compat_*.c` include `<tonc.h>` and Serval's headers in both orders.

## Code style

- C17 with GNU extensions. `.clang-format` (clang-format 18) is enforced by CI on everything outside `third_party/`.
- `snake_case`; public API names follow the docs (`frame_begin`, `entity_create`); GBA-only API is prefixed `gba_`; macros are `SERVAL_*` or the documented short names (`MAX_ENT`, `C_*`).
- Engine code builds warning-free with `-Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes`.

## CI/CD

**CI** (`.github/workflows/ci.yml`), on pushes to `main`, pull requests and manual runs:

- `clang-format` check.
- Host tests with sanitizers.
- GBA build with warnings as errors, the test ROM run in mGBA, a check that the release archive builds on its own, and the ROMs uploaded as artifacts.

The ARM toolchain and mGBA versions are set in `.github/actions/setup-gba/action.yml`; `mgba-rom-test` is built once and cached.

**Release** (`.github/workflows/release.yml`):

1. Set `version` in `serval.json` and merge to `main`.
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`. The tag must equal `v` + the `serval.json` version exactly; a version with a suffix such as `0.2.0-rc.1` creates a prerelease.
3. The workflow builds, runs the test ROM, packages `serval-engine-X.Y.Z.zip` (plus `serval.json` as a separate asset), checks the archive builds on its own, and attaches both to a **draft** release.
4. Review the draft and publish it. Only published releases are visible to Studio Advance.

See [releases.md](releases.md) for what the archive contains and how versions are numbered.
