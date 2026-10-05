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

Each example's `main.c` opens with a comment describing what it demonstrates and what to expect when the ROM boots: what appears on screen, what each button does, and whether there is sound. Keep it accurate when changing an example.

To build every example for a demo:

```sh
examples/build-all.sh            # optional preset argument, default gba-release
# -> examples/roms/<name>.gba (git-ignored)
```

Each example builds separately, so one that fails to build doesn't stop the rest; failures are listed with the end of their build log, and the script exits non-zero. CI runs it too.

## Source layout

| Path | Contents |
| --- | --- |
| `include/serval/` | Public headers: `serval.h` (umbrella: includes everything), `core.h` (init, splash, frames, buttons), `screen.h`, `sprites.h`, `ecs.h`, `physics.h`, `audio.h`, `text.h`, `math.h`, `fixed.h`, `random.h`, `debug.h`, `platform.h`; `gba.h` holds GBA-only escape hatches. No third-party includes |
| `src/ecs/`, `src/core/` | Platform-neutral modules: ECS (`ecs.c`), physics (`physics.c`), random numbers, text formatting, trigonometry (`trig.c`), and the internal warning macro (`warn.h`). On the GBA they are part of the single `serval` library; host builds compile them alone as `serval_portable`, with `src/host/platform.c` (stderr output, clock-based entropy) |
| `src/gba/` | GBA-only code: core API, frame timing and `WAITCNT` (`core.c`), sprites, rotation and render systems (`sprites.c`), text layer (`text.c`), PSG sound effects (`psg.c`), splash screen (`splash.c`), debug output (`debug.c`), engine-internal declarations (`internal.h`), startup code (`crt0.s`), linker script (`gba.ld`), and `memcpy` and friends (`libc.c`, linked into every ROM as the `serval_libc` object) |
| `third_party/libtonc/` | Vendored libtonc, see its `VENDORED.md` |
| `tests/` | The harness (`test.h`, `test.c`), shared suites run natively and in the ROM (`ecs_tests.c`, `physics_tests.c`, `math_tests.c`, `random_tests.c`, `text_format_tests.c`), hardware suites (`rom/`: core, sprites, text, audio, splash, libc, libtonc compatibility), the runners, and `public_headers.c` |
| `examples/` | Example ROMs, one directory each, plus `build-all.sh`: `hello` (smallest game), `bunnymark` (ECS, physics, benchmark), `pong` (a complete small game with AI, shaded sprites, effects and sound), `asteroids` (rotation, wrap-around, many short-lived entities, sound) |
| `cmake/` | Toolchain file and `serval_add_rom()` |
| `tools/` | ROM header fixer, mGBA test runner build, release packaging, benchmark (`bench.sh`) |

## Building a game

`serval_add_rom()` (in `cmake/Serval.cmake`) links sources against the engine with its own startup code and linker script, no C library, then produces the `.gba` file with a fixed header:

```cmake
serval_add_rom(my_game SOURCES main.c TITLE "MY GAME" GAME_CODE "MYGM")
```

ROMs are padded with `0xFF` to at least 512 KiB. Emulators guess whether a small file is a cartridge or a multiboot image (which runs from RAM and is at most 256 KiB); older mGBA releases (0.8.x) mistake small Serval ROMs for multiboot and show a white screen. Anything over 256 KiB is always treated as a cartridge.

## Tests

Test cases use the small harness in `tests/test.h` (`CHECK(cond)`, `TEST_SUITE(...)`), which needs no C library.

- **Platform-neutral suites** (e.g. `ecs_tests.c`) run both natively and in the test ROM.
- **Hardware suites** (`tests/rom/`) run only in the test ROM.

The test ROM writes results to mGBA's debug log and ends with `swi 3`, passing the number of failed checks in `r0`; `mgba-rom-test -S 3 -R r0` turns that into its exit code. Register new suites in `tests/host/main.c` and/or `tests/rom/main.c`.

Debug-only behavior (`SERVAL_DEBUG` warnings, [core-api.md](core-api.md#debug-builds-report-misuse)) is tested with `debug_warning_count()`, with `#ifdef SERVAL_DEBUG` branches for what release builds must do instead. CI runs the test ROM in a RelWithDebInfo build (checks on) and builds Release with warnings as errors (checks off).

Two compile-only checks keep third-party libraries behind the API ([core-api.md](core-api.md#dependencies-stay-behind-the-api)):

- **Host build:** `tests/public_headers.c` and every example's sources compile without libtonc on the include path. Add new examples to `serval_api_only_check` in `tests/CMakeLists.txt`.
- **Test ROM:** `tests/rom/compat_*.c` include `<tonc.h>` and Serval's headers in both orders.

## Benchmark

`examples/bunnymark` doubles as the engine's CPU benchmark: its game (`bunnymark.c`) is also built with `bench.c` as `bunnymark_bench`, which starts with 128 bunnies (the entity limit) from a fixed random seed, runs 600 frames headless in mGBA and reports the CPU cycles spent per frame. Lower is better; the frame budget is 280,896 cycles.

```sh
tools/bench.sh            # optional preset argument, default gba-release
# bunnymark: 128 bunnies, 600 frames: avg 72875 cycles (25.9%), peak 76551 (gba-release)
```

The result is deterministic for a given build, so any change in the number comes from the code. When bunnymark itself changes, the workload changes: record a new baseline row and say so. CI runs it on every push and shows the result in the job summary. For a performance change, run it before and after and put both numbers in the commit message.

| Date | Commit | avg cycles | % of frame | Change |
| --- | --- | --- | --- | --- |
| 2026-10-04 | `e5fbaf0` | 149,564 | 53.2% | Baseline: unoptimized Thumb code in ROM |
| 2026-10-04 | `657244b` | 164,650 | 58.6% | **Workload change**, not an engine change: bunnymark gained gravity, friction and a third HUD line. New baseline; peak 171,675 |
| 2026-10-04 | `d685165` | 83,072 | 29.5% | `sprite_draw`/`sys_render` as ARM code in IWRAM, per-sprite draw data resolved at load, no per-sprite call |
| 2026-10-04 | `06cbcca` | 82,929 | 29.5% | Debug checks (compiled out of release builds) |
| 2026-10-04 | `55d56f0` | 86,510 | 30.7% | Partly a workload change: bunnymark's HUD now uses `text_print_line`, which blanks the rest of each row |
| 2026-10-04 | `5969c88` | 53,217 | 18.9% | `text_format` without division; `WAITCNT` set to 3/1 + prefetch (all ROM code, including the game's, ~40% faster); `sys_movement` in IWRAM |
| 2026-10-04 | `2075d3b` | 57,173 | 20.3% | Per-entity `spr_flags` in `sys_render` (~29 cycles per sprite); bunnymark uses `ECS_FOR_EACH` |
| 2026-10-04 | `8cac6a0` | 71,266 | 25.3% | **Workload change**: bunnymark now uses the engine's `sys_physics` (faster than its own) plus depth-sorted drawing (~10,000) and a facing/depth system (~5,400) |
| 2026-10-04 | `1116a04` | 71,345 | 25.3% | Open-edge checks in `sys_physics` (Pong) |
| 2026-10-04 | `a10fd04` | 72,875 | 25.9% | One rotation check per sprite in the render systems; physics loop specialized for wrapping (Asteroids) |

## Memory use

IWRAM (32 KB, the fast RAM) holds the engine's hot code, the ECS pools, the shadow OAM and the stack, and is shared with the game. Unused engine code is dropped at link time (`--gc-sections`), so use depends on the features a game calls. Measured with `arm-none-eabi-size -A` on the release `.elf` files (IWRAM code and data plus `.bss`):

| Example | IWRAM used |
| --- | --- |
| `hello` | 8,044 bytes |
| `pong` | 14,972 bytes |
| `bunnymark` | 15,120 bytes |
| `asteroids` | 15,300 bytes |

The linker script reserves 2 KB below the stack and fails the build if IWRAM overflows. Large engine buffers live in EWRAM (256 KB) instead.

## Checking what a game shows and plays

Tests check state (OAM, VRAM, registers), not what the screen looks like or what the speakers play. During development, a small capture tool built on mGBA's core library ran ROMs headlessly with scripted buttons, saved chosen frames as images and recorded the audio, to check each example against its header comment. It is not part of the repository yet; adding it (and turning it into screenshot tests in CI) is an open question.

## Code style

- C17 with GNU extensions. `.clang-format` (clang-format 18) is enforced by CI on everything outside `third_party/`.
- `snake_case`; public API names follow the docs (`frame_begin`, `entity_create`); GBA-only API is prefixed `gba_`; macros are `SERVAL_*` or the documented short names (`MAX_ENT`, `C_*`).
- Engine code builds warning-free with `-Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes`.

## CI/CD

**CI** (`.github/workflows/ci.yml`), on pushes to `main`, pull requests and manual runs:

- `clang-format` check.
- Host tests with sanitizers.
- GBA build (RelWithDebInfo, debug checks on) with warnings as errors and the test ROM run in mGBA; a check that the release archive builds on its own; a Release build with warnings as errors (debug checks compiled out); every example built; the bunnymark benchmark (result in the job summary); and the ROMs uploaded as artifacts.

The ARM toolchain and mGBA versions are set in `.github/actions/setup-gba/action.yml`; `mgba-rom-test` is built once and cached.

**Release** (`.github/workflows/release.yml`):

1. Set `version` in `serval.json` and merge to `main`.
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`. The tag must equal `v` + the `serval.json` version exactly; a version with a suffix such as `0.2.0-rc.1` creates a prerelease.
3. The workflow builds, runs the test ROM, packages `serval-engine-X.Y.Z.zip` (plus `serval.json` as a separate asset), checks the archive builds on its own, and attaches both to a **draft** release.
4. Review the draft and publish it. Only published releases are visible to Studio Advance.

See [releases.md](releases.md) for what the archive contains and how versions are numbered.
