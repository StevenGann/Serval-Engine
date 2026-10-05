# Development

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 3.25 | Uses presets (`CMakePresets.json`) |
| Ninja | any recent | Generator for all presets |
| ARM GNU Toolchain | 15.3.Rel1 (what CI uses) | Any `arm-none-eabi-gcc` should work, including devkitARM; CMake warns if it is older than `toolchain.gcc` in `serval.json`. Found on `PATH` or via `ARM_GNU_TOOLCHAIN=<toolchain root>` |
| Python 3 | any recent | Runs `tools/gbafix.py` after each ROM link, and `tools/check-rom.py` in the tests |
| Host C compiler | GCC or Clang | Only for host unit tests |
| Emscripten | 6.0.11 (what CI uses) | Only for [web builds](#web-builds). Install with [emsdk](https://emscripten.org/docs/getting_started/downloads.html) and `source emsdk_env.sh` (the toolchain file finds it through `EMSDK`, or `emcc` on `PATH`) |
| Chrome or Chromium | any recent | Only for `tools/web-shots.py` (found on `PATH`, or set `SERVAL_CHROME`) |
| `mgba-rom-test` | mGBA 0.10.5 | Runs the test ROM. Optional locally (without it the ROM tests are built but not run), unless `SERVAL_REQUIRE_ROM_TESTS` is ON (as in `gba-ci`), which makes configuration fail without it. Build it with `tools/build-mgba-rom-test.sh <dir>` and set `MGBA_ROM_TEST_DIR=<dir>` |

Download the ARM GNU Toolchain from <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads> (`arm-none-eabi`, for your host).

## Build and test

```sh
# GBA: engine library, example ROMs and the test ROM
export ARM_GNU_TOOLCHAIN=~/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi
export MGBA_ROM_TEST_DIR=~/opt/mgba-rom-test
cmake --preset gba-debug
cmake --build --preset gba-debug
ctest --preset gba-debug          # runs the test ROM in mGBA, and checks every ROM (check-rom.py)

# Host: platform-neutral modules, with AddressSanitizer and UBSan
cmake --preset host
cmake --build --preset host
ctest --preset host
```

| Preset | Target | Notes |
| --- | --- | --- |
| `gba-debug` / `gba-release` | GBA | |
| `gba-ci` | GBA | RelWithDebInfo, warnings as errors, ROM tests required (`SERVAL_REQUIRE_ROM_TESTS`); used by CI |
| `web` | Web | RelWithDebInfo (debug warnings go to the browser console), warnings as errors; used by CI |
| `web-release` | Web | |
| `host` | Native | Debug, AddressSanitizer and UBSan (any finding fails the test), warnings as errors |

ROMs are written next to their ELF files, e.g. `build/gba-debug/examples/hello.gba`, with a linker map (`.map`). Open them in any GBA emulator.

Each example's `main.c` opens with a comment describing what it demonstrates and what to expect when the ROM boots: what appears on screen, what each button does, and whether there is sound. Keep it accurate when changing an example.

To build every example for a demo:

```sh
examples/build-all.sh            # optional presets: [gba-preset [web-preset]], default gba-release web-release
# -> examples/roms/<name>.gba and examples/html/<name>.html (both git-ignored)
```

It builds for the GBA and, with Emscripten set up ([Web builds](#web-builds)), for the web; a platform whose toolchain is missing is skipped with a note, and the script fails if both are. Each example builds separately, so one that fails to build doesn't stop the rest; failures are listed with the end of their build log, and the script exits non-zero. CI runs it too (GBA only: the web job builds the pages itself).

## Web builds

The `web` and `web-release` presets build every example as one self-contained HTML page ([platforms.md](platforms.md#web) explains how it works):

```sh
source ~/opt/emsdk/emsdk_env.sh
cmake --preset web-release
cmake --build --preset web-release
# -> build/web-release/examples/hello.html, bunnymark.html, pong.html, asteroids.html
```

Open a page from disk, or upload it to any static host. A page waits for a click or key press before starting the game, because browsers only allow sound after one.

To check a page without a display, `tools/web-shots.py` runs it headless with scripted buttons and saves chosen frames as PNG files (same numbering and button letters as the capture tool, for comparing frames with an emulator):

```sh
tools/web-shots.py build/web-release/examples/pong.html 400 /tmp/pong shot=100 shot=400 key=250:S:2
# -> /tmp/pong-00100.png, /tmp/pong-00400.png
```

`--require-picture` fails if a shot is one flat color, as CI uses it.

## Source layout

| Path | Contents |
| --- | --- |
| `include/serval/` | Public headers: `serval.h` (umbrella: includes everything), `core.h` (init, splash, frames, buttons), `screen.h`, `sprites.h`, `ecs.h`, `physics.h`, `map.h` (tilemaps, camera, map collision), `audio.h`, `text.h`, `math.h`, `fixed.h`, `random.h`, `debug.h`, `platform.h`; `gba.h` holds GBA-only escape hatches. No third-party includes |
| `src/ecs/`, `src/core/` | Platform-neutral modules: ECS (`ecs.c`), physics (`physics.c`), map bodies (`map_movement.c`), map layers as data, the camera, runtime cell changes and collision queries (`map.c`), random numbers, text formatting, trigonometry (`trig.c`), and internal headers (`warn.h`, the warning macro; `map_internal.h` and `physics_internal.h`, state shared between modules). On the GBA they are part of the single `serval` library; host builds compile them alone as `serval_portable`, with `src/host/platform.c` (stderr output, clock-based entropy) |
| `src/gba/` | GBA-only code: core API, frame timing and `WAITCNT` (`core.c`), sprites, rotation and render systems (`sprites.c`), map layers in VRAM: tileset, streaming and background registers (`map.c`), text layer (`text.c`), PSG sound effects (`psg.c`), splash screen (`splash.c`), debug output (`debug.c`), engine-internal declarations (`internal.h`), startup code (`crt0.s`), linker script (`gba.ld`), and `memcpy` and friends (`libc.c`, linked into every ROM as the `serval_libc` object) |
| `src/web/` | The web backend: virtual GBA hardware for the GBA backend compiled to WebAssembly. Renderer (`ppu.c`), PSG sound (`apu.c`), BIOS and libtonc-assembly stand-ins, the VBlank wait that yields to the browser, debug output (`platform.c`), their interface (`web.h`), and the page template (`shell.html`) |
| `third_party/libtonc/` | Vendored libtonc, see its `VENDORED.md` |
| `tests/` | The harness (`test.h`, `test.c`), shared suites run natively and in the ROM (`ecs_tests.c`, `physics_tests.c`, `map_tests.c`, `math_tests.c`, `random_tests.c`, `text_format_tests.c`), hardware suites (`rom/`: core, sprites, map layers, text, audio, splash, libc, libtonc compatibility), the runners, `public_headers.c`, and `consumer/` (a minimal game project built against the release archive) |
| `examples/` | Example ROMs, one directory each, plus `build-all.sh`: `hello` (smallest game), `bunnymark` (ECS, physics, benchmark), `pong` (a complete small game with AI, shaded sprites, effects and sound), `asteroids` (rotation, wrap-around, many short-lived entities, sound) |
| `cmake/` | Toolchain files (`arm-gba-toolchain.cmake`, `web-toolchain.cmake`), `serval_add_rom()` (`Serval.cmake`, with its web variant in `ServalWeb.cmake`) and `serval_add_rom_checks()` (`ServalRomChecks.cmake`) |
| `tools/` | ROM header fixer (`gbafix.py`), ROM checker (`check-rom.py`), mGBA test runner build, release packaging, release-archive game checks (`check-consumer.sh`, and `check-consumer-web.sh` for web builds), benchmark (`bench.sh`), headless web page runner (`web-shots.py`) |

## Building a game

`serval_add_rom()` (in `cmake/Serval.cmake`) links sources against the engine with its own startup code and linker script, no C library, then produces the `.gba` file with a fixed header:

```cmake
serval_add_rom(my_game SOURCES main.c TITLE "MY GAME" GAME_CODE "MYGM")
```

`TITLE` is up to 12 ASCII characters (default: the target name in upper case) and `GAME_CODE` 4 (default `0000`). It works both inside the engine's tree (`examples/`) and from a game's own CMake project that adds the engine (a release archive or a checkout) with `add_subdirectory()`; everything it needs comes from the function's own directory, cache variables or target properties, never from the engine's directory scope. Game sources get `-ffunction-sections -fdata-sections` from the `serval` target, so unused game code is dropped too. `tests/consumer/` is the reference game project, and [getting-started.md](getting-started.md#2-create-your-game) walks through creating one.

In web builds (the `web` presets) it produces `<target>.html` instead, from the same arguments; see [Web builds](#web-builds).

ROMs are padded with `0xFF` to at least 512 KiB. Emulators guess whether a small file is a cartridge or a multiboot image (which runs from RAM and is at most 256 KiB); older mGBA releases (0.8.x) mistake small Serval ROMs for multiboot and show a white screen. Anything over 256 KiB is always treated as a cartridge.

## Tests

Test cases use the small harness in `tests/test.h` (`CHECK(cond)`, `TEST_SUITE(...)`), which needs no C library.

- **Platform-neutral suites** (e.g. `ecs_tests.c`) run both natively and in the test ROM.
- **Hardware suites** (`tests/rom/`) run only in the test ROM.

The test ROM writes results to mGBA's debug log and ends with `swi 3`, passing 1 in `r0` if any check failed and 0 otherwise (not the failure count, since exit codes wrap at 256); `mgba-rom-test -S 3 -R r0` turns that into its exit code. Register new suites in `tests/host/main.c` and/or `tests/rom/main.c`.

Debug-only behavior (`SERVAL_DEBUG` warnings, [core-api.md](core-api.md#debug-builds-report-misuse)) is tested with `debug_warning_count()`, with `#ifdef SERVAL_DEBUG` branches for what release builds must do instead. CI runs the test ROM in RelWithDebInfo and Debug builds (checks on) and in a Release build (checks off), all with warnings as errors.

Every ROM (test ROM and examples) also gets a `<target>_rom_checks` test (`serval_add_rom_checks()`, running `tools/check-rom.py`): the ROM is padded past 256 KiB and has a valid header (title, game code, checksum), newlib's `libc.a` is not in the link map, and the code contains no BLX instruction (the ARM7TDMI has none). `tests/consumer/` builds a minimal game against the release archive, the way games use it, runs it in mGBA and checks that unused game code was dropped; run it with `tools/check-consumer.sh <serval-engine-X.Y.Z.zip>`.

Compile-only checks keep third-party libraries behind the API ([core-api.md](core-api.md#dependencies-stay-behind-the-api)):

- **Host build:** `tests/public_headers.c`, every example's sources (`examples/*/*.c`, found automatically) and `tests/consumer/main.c` compile without libtonc on the include path (`serval_api_only_check`).
- **Each public header on its own:** every `include/serval/*.h` compiles alone, with only `include/` on the include path, on the host and for the GBA (`serval_header_check`, `serval_gba_header_check`).
- **Test ROM:** `tests/rom/compat_*.c` include `<tonc.h>` and Serval's headers in both orders.

## Benchmark

`examples/bunnymark` doubles as the engine's CPU benchmark: its game (`bunnymark.c`) is also built with `bench.c` as `bunnymark_bench`, which starts with 128 bunnies (the entity limit) from a fixed random seed, runs 600 frames headless in mGBA and reports the CPU cycles spent per frame. Lower is better; the frame budget is 280,896 cycles.

```sh
tools/bench.sh            # optional preset argument, default gba-release
# bunnymark: 128 bunnies, 600 frames: avg 74976 cycles (26.6%), peak 80782 (gba-release)
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
| 2026-10-04 | 724c283 | 74,976 | 26.6% | Correctness fixes (before: avg 72,875, peak 76,551; after: peak 80,782). Mainly `ent_has` requiring `C_ALIVE` (~+1,430), `text_format` (~+490) and the physics fixes (~+230) |
| 2026-10-05 | 724c283 | 74,684 | 26.5% | Tilemaps and camera (before: avg 74,976, peak 80,782; after: peak 80,081). The render systems subtract the camera (~+740: the loops are out of registers, so it costs two IWRAM loads per sprite), paid for by checking sprite IDs against the constant `SPRITE_MAX` instead of reloading the table size (~−725); `sys_physics` reads gravity once per call (~−310) |
| 2026-10-05 | 724c283 | 75,052 | 26.7% | `body_max_fall` in `sys_physics` (before: avg 74,887, peak 80,326, same tree without it; after: peak 80,545). A separate pass that skips 16 zero limits per word-group read (~+165); inside the main loop it cost ~+2,900, as that loop is out of registers |
| 2026-10-05 | 724c283 | 75,052 | 26.7% | `SPRITE_HIDDEN` (before: avg 74,938, peak 80,441, same tree without the check; after: peak 80,545). The render systems test it together with `spr_angle` (one ORR per entity), sending hidden sprites down the out-of-line rotated path, which drops them (~+115); a separate test before the draw cost ~+645 |

## Memory use

IWRAM (32 KB, the fast RAM) holds the engine's hot code, the ECS pools, the shadow OAM and the stack, and is shared with the game. Unused engine code is dropped at link time (`--gc-sections`), so use depends on the features a game calls. Measured with `arm-none-eabi-size -A` on the release `.elf` files (IWRAM code and data plus `.bss`):

| Example | IWRAM used |
| --- | --- |
| `hello` | 8,336 bytes |
| `pong` | 16,356 bytes |
| `bunnymark` | 16,496 bytes |
| `asteroids` | 16,676 bytes |

Games that load map layers add about 2.2 KB: the streaming loops (1.4 KB of ARM code, [tilemaps.md](tilemaps.md#streaming)), the runtime cell change table (384 bytes, read by every collision query) and the per-background state. The screenblock copies (6 KB) and the redraw list are in EWRAM.

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
- GBA: `gba-ci` (RelWithDebInfo, debug checks on) built with warnings as errors and tested (test ROM in mGBA, ROM checks); a check that the release archive builds on its own; `tools/check-consumer.sh` (a game built against the archive and run in mGBA); Release (debug checks compiled out) and Debug (`-O0`) builds with warnings as errors, each tested; every example built; the bunnymark benchmark (result in the job summary); and the ROMs uploaded as artifacts.
- Web: the `web` preset (warnings as errors); every example run headless in Chrome for 300 frames with `tools/web-shots.py --require-picture`, after checking that its page loads no other file; `tools/check-consumer-web.sh` (a game project built for the web against the release archive, run and checked the same way); the pages uploaded as artifacts.

The ARM toolchain and mGBA versions are set in `.github/actions/setup-gba/action.yml`; `mgba-rom-test` is built once and cached. The Emscripten version is set in the web job.

**Release** (`.github/workflows/release.yml`):

1. Set `version` in `serval.json` and merge to `main`.
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`. The tag must equal `v` + the `serval.json` version exactly; a version with a suffix such as `0.2.0-rc.1` creates a prerelease.
3. The workflow runs the whole CI (`ci.yml`) on the tagged commit, then builds Release with warnings as errors, runs the test ROM, packages `serval-engine-X.Y.Z.zip` (with `serval-engine-X.Y.Z.zip.sha256`, and `serval.json` as a separate asset), checks that the archive builds on its own and that a game builds against it (`tools/check-consumer.sh`), and attaches all three to a **draft** release. Only files tracked by git are packaged.
4. Review the draft and publish it. Only published releases are visible to Studio Advance.

See [releases.md](releases.md) for what the archive contains and how versions are numbered.
