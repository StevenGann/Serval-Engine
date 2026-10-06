# Development

**Status:** current: describes the build, tests, benchmark and CI/CD as they are.

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
# -> build/web-release/examples/hello.html, bunnymark.html, pong.html, asteroids.html,
#    breakout.html, platformer.html, shmup.html, blackjack.html
```

Open a page from disk, or upload it to any static host. A page waits for a click or key press before starting the game, because browsers only allow sound after one.

To check a page without a display, `tools/web-shots.py` runs it headless with scripted buttons and saves chosen frames as PNG files (same numbering and button letters as the capture tool, for comparing frames with an emulator):

```sh
tools/web-shots.py build/web-release/examples/pong.html 400 /tmp/pong shot=100 shot=400 key=250:S:2
# -> /tmp/pong-00100.png, /tmp/pong-00400.png
```

`--require-picture` fails if a shot is one flat color, as CI uses it. Pages run this way start with blank save memory and never touch `localStorage`; `save=FILE` loads the game's save memory from `FILE` (a `.sav`, as mGBA writes) if it exists and writes it back at the end, so consecutive runs see each other's saves ([platforms.md](platforms.md#web)).

Frame numbers don't line up exactly with an emulator's. The page counts `frame_end()` calls, while an emulator counts every refresh, including ones the game misses while booting or loading (the platformer misses about 20 while building its level). In the examples so far, web shot N matches mGBA frame N + 1, and web buttons must be scripted about 2 frames earlier. A game that loads for a while needs a bigger shift, found by comparing a frame both builds show. With the same button timing, the same game plays out identically on both: `random_entropy()` depends only on input and frame count. That holds as long as the game itself is deterministic C: the GBA build uses GCC and the web build clang, which may evaluate function arguments in different orders, so `f(random_range(0, 3), random_range(0, 3))` can differ between them. Make random calls one per statement.

## Source layout

| Path | Contents |
| --- | --- |
| `include/serval/` | Public headers: `serval.h` (umbrella: includes everything), `core.h` (init, splash, frames, buttons), `screen.h`, `sprites.h`, `ecs.h`, `physics.h`, `map.h` (tilemaps, camera, map collision), `audio.h`, `text.h`, `math.h`, `fixed.h`, `random.h`, `save.h` (save slots), `debug.h`, `platform.h`; `gba.h` holds GBA-only escape hatches. No third-party includes |
| `src/ecs/` | Platform-neutral systems: entities, `ecs_count`/`ecs_gather` and `sys_movement` (`ecs.c`), bouncing bodies, `sys_physics`, `body_overlap`, `body_hit_side` (`physics.c`, with `physics_internal.h`, state shared with map bodies), map bodies and `sys_map_movement` (`map_movement.c`), `sys_animate` (`animate.c`) |
| `src/core/` | Platform-neutral modules: map layers as data, the camera, runtime cell changes and collision queries (`map.c`, with `map_internal.h`), held-button repeat (`input.c`), random numbers and `random_entropy` (`random.c`), text formatting (`text_format.c`), trigonometry, `angle_of` and `fx_length` (`trig.c`), paths and `sys_path` (`path.c`), the PSG music sequencer (`psg_sequencer.c`), the registered sprite table, so `sys_animate` can read assets (`sprite_table.c`), save slots on a byte-addressed save memory (`save.c`, with `save_internal.h`, the memory each platform supplies), and `warn.h` (the warning macro) |
| | `src/ecs/` and `src/core/` compile on the host (no libtonc, no hardware access). On the GBA they are part of the single `serval` library; host builds compile them alone as `serval_portable`, with `src/host/platform.c` (stderr output, clock-based entropy, save memory in RAM) |
| `src/gba/` | GBA-only code: core API, frame loop, frame timing and `WAITCNT` (`core.c`), sprites, rotation and render systems (`sprites.c`), map layers in VRAM: tileset, animated tiles, streaming and background registers (`map.c`), brightness fades (`screen.c`), text layer (`text.c`), PSG sound effects (`psg.c`) and the music player (`music.c`, hooked in by `psg_music_play()`), save memory and its ROM ID string per save type (`save_sram.c`; `save_flash.c`, Flash with its chip-reading routines in EWRAM; `save_eeprom.c`, EEPROM through DMA3), each compiled once per type into a `serval_save_<type>` object, splash screen (`splash.c`), debug output (`debug.c`), engine-internal declarations (`internal.h`, `screen_internal.h`), startup code (`crt0.s`), linker script (`gba.ld`), and `memcpy` and friends (`libc.c`, linked into every ROM as the `serval_libc` object) |
| `third_party/libtonc/` | Vendored libtonc, see its `VENDORED.md` |
| `tests/` | The harness (`test.h`, `test.c`); shared suites run natively and in the ROM (`ecs_tests.c`, `physics_tests.c`, `map_tests.c`, `anim_tests.c`, `path_tests.c`, `math_tests.c`, `random_tests.c`, `text_format_tests.c`, `input_tests.c`, `psg_sequencer_tests.c`, `save_tests.c`); host-only suites for the web renderer and sound (`web_ppu_tests.c`, `web_apu_tests.c`); the runners (`host/main.c`, `rom/main.c`); hardware suites in `rom/` (core, sprites, map layers, presentation (fades, text styles, hidden sprites, animated tiles), text, audio, splash, save memory, libc, ECS and physics costs, libtonc compatibility: `compat_*.c`); `rom/save_main.c` (the per-save-type test ROMs) and `rom/run-rom-test.cmake` (runs them and checks mGBA's log); `public_headers.c`; and `consumer/` (a minimal game project built against the release archive, plus a game that saves) |
| `examples/` | Example games, one directory each (see [getting-started.md](getting-started.md#1-build-the-examples)): `hello`, `bunnymark` (also the benchmark), `pong`, `asteroids`, `breakout`, `platformer`, `shmup`, `blackjack`; `build-all.sh` builds them all into `roms/` and `html/` |
| `cmake/` | Toolchain files (`arm-gba-toolchain.cmake`, `web-toolchain.cmake`), `serval_add_rom()` (`Serval.cmake`, with its web variant in `ServalWeb.cmake`) and `serval_add_rom_checks()` (`ServalRomChecks.cmake`) |
| `tools/` | ROM header fixer (`gbafix.py`), ROM checker (`check-rom.py`), mGBA test runner build, release packaging, release-archive game checks (`check-consumer.sh`, and `check-consumer-web.sh` for web builds), benchmark (`bench.sh`), headless web page runner (`web-shots.py`) |

## Building a game

`serval_add_rom()` (in `cmake/Serval.cmake`) links sources against the engine with its own startup code and linker script, no C library, then produces the `.gba` file with a fixed header:

```cmake
serval_add_rom(my_game SOURCES main.c TITLE "MY GAME" GAME_CODE "MYGM")
serval_add_rom(my_rpg SOURCES main.c TITLE "MY RPG" GAME_CODE "MYRP" SAVE FLASH128K)
```

`TITLE` is up to 12 ASCII characters (default: the target name in upper case) and `GAME_CODE` 4 (default `0000`). `SAVE` is the cartridge save memory that `save.h` uses: `SRAM` (default), `FLASH64K`, `FLASH128K`, `EEPROM8K` or `EEPROM512`; it sets the slots and their capacity, and the ROM gets that type's backend (the `serval_save_<type>` object) and ID string only, or nothing if the game never saves ([runtime-systems.md](runtime-systems.md#save-types)). An unknown type is a configure error. It works both inside the engine's tree (`examples/`) and from a game's own CMake project that adds the engine (a release archive or a checkout) with `add_subdirectory()`; everything it needs comes from the function's own directory, cache variables or target properties, never from the engine's directory scope. Game sources get `-ffunction-sections -fdata-sections` from the `serval` target, so unused game code is dropped too. `tests/consumer/` is the reference game project, and [getting-started.md](getting-started.md#2-create-your-game) walks through creating one.

In web builds (the `web` presets) it produces `<target>.html` instead, from the same arguments; see [Web builds](#web-builds).

ROMs are padded with `0xFF` to at least 512 KiB. Emulators guess whether a small file is a cartridge or a multiboot image (which runs from RAM and is at most 256 KiB); older mGBA releases (0.8.x) mistake small Serval ROMs for multiboot and show a white screen. Anything over 256 KiB is always treated as a cartridge.

## Tests

Test cases use the small harness in `tests/test.h` (`CHECK(cond)`, `TEST_SUITE(...)`), which needs no C library.

- **Platform-neutral suites** (e.g. `ecs_tests.c`) run both natively and in the test ROM.
- **Hardware suites** (`tests/rom/`) run only in the test ROM.

The test ROM writes results to mGBA's debug log and ends with `swi 3`, passing 1 in `r0` if any check failed and 0 otherwise (not the failure count, since exit codes wrap at 256); `mgba-rom-test -S 3 -R r0` turns that into its exit code. Register new suites in `tests/host/main.c` and/or `tests/rom/main.c`.

Save types get test ROMs of their own: `serval_tests` saves to SRAM, and `serval_tests_flash64k`, `serval_tests_flash128k`, `serval_tests_eeprom8k` and `serval_tests_eeprom512` (built with that `SAVE`, runner `tests/rom/save_main.c`) run the shared save suite and the hardware save suite (`tests/rom/save_tests.c`) on mGBA's Flash or EEPROM. Their ctests (`rom_tests`, `rom_tests_<type>`) run through `tests/rom/run-rom-test.cmake`, which also checks mGBA's log: it must have detected the save type the ROM was built for (e.g. "Detected Flash savegame", and the switch to 128 KiB) and reported no malformed Flash or EEPROM access. `tests/save_tests.c` runs every case on every type's layout, with simulated memories that follow each type's rules (Flash erase and programming, EEPROM blocks) and lose power after every step.

Debug-only behavior (`SERVAL_DEBUG` warnings, [core-api.md](core-api.md#debug-builds-report-misuse)) is tested with `debug_warning_count()`, with `#ifdef SERVAL_DEBUG` branches for what release builds must do instead. CI runs the test ROM in RelWithDebInfo and Debug builds (checks on) and in a Release build (checks off), all with warnings as errors.

Every ROM (test ROM and examples) also gets a `<target>_rom_checks` test (`serval_add_rom_checks()`, running `tools/check-rom.py`): the ROM is padded past 256 KiB and has a valid header (title, game code, checksum), newlib's `libc.a` is not in the link map, the code contains no BLX instruction (the ARM7TDMI has none), and a ROM that links the save code contains exactly one save type ID string, its type's (`SRAM_V113`, `FLASH512_V131`, `FLASH1M_V103` or `EEPROM_V124`), which emulators and flash carts look for, while a ROM that doesn't save contains none. `tests/consumer/` builds a minimal game against the release archive, the way games use it, runs it in mGBA and checks that unused game code was dropped, plus a game that saves with `SAVE EEPROM8K`; run it with `tools/check-consumer.sh <serval-engine-X.Y.Z.zip>`.

Compile-only checks keep third-party libraries behind the API ([core-api.md](core-api.md#dependencies-stay-behind-the-api)):

- **Host build:** `tests/public_headers.c`, every example's sources (`examples/*/*.c`, found automatically) and `tests/consumer/*.c` compile without libtonc on the include path (`serval_api_only_check`).
- **Each public header on its own:** every `include/serval/*.h` compiles alone, with only `include/` on the include path, on the host and for the GBA (`serval_header_check`, `serval_gba_header_check`).
- **Test ROM:** `tests/rom/compat_*.c` include `<tonc.h>` and Serval's headers in both orders.

## Benchmark

`examples/bunnymark` doubles as the engine's CPU benchmark: its game (`bunnymark.c`) is also built with `bench.c` as `bunnymark_bench`, which starts with 128 bunnies (the entity limit) from a fixed random seed, runs 600 frames headless in mGBA and reports the CPU cycles spent per frame. Lower is better; the frame budget is 280,896 cycles.

```sh
tools/bench.sh            # optional preset argument, default gba-release
# bunnymark: 128 bunnies, 600 frames: avg 74969 cycles (26.6%), peak 80489 (gba-release)
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
| 2026-10-04 | `724c283` | 74,976 | 26.6% | Correctness fixes (before: avg 72,875, peak 76,551; after: peak 80,782). Mainly `ent_has` requiring `C_ALIVE` (~+1,430), `text_format` (~+490) and the physics fixes (~+230) |
| 2026-10-05 | `724c283` | 74,684 | 26.5% | Tilemaps and camera (before: avg 74,976, peak 80,782; after: peak 80,081). The render systems subtract the camera (~+740: the loops are out of registers, so it costs two IWRAM loads per sprite), paid for by checking sprite IDs against the constant `SPRITE_MAX` instead of reloading the table size (~−725); `sys_physics` reads gravity once per call (~−310) |
| 2026-10-05 | `724c283` | 75,052 | 26.7% | `body_max_fall` in `sys_physics` (before: avg 74,887, peak 80,326, same tree without it; after: peak 80,545). A separate pass that skips 16 zero limits per word-group read (~+165); inside the main loop it cost ~+2,900, as that loop is out of registers |
| 2026-10-05 | `724c283` | 75,052 | 26.7% | `SPRITE_HIDDEN` (before: avg 74,938, peak 80,441, same tree without the check; after: peak 80,545). The render systems test it together with `spr_angle` (one ORR per entity), sending hidden sprites down the out-of-line rotated path, which drops them (~+115); a separate test before the draw cost ~+645 |
| 2026-10-05 | `6ba7de7` | 75,057 | 26.7% | `SPRITE_SCREEN` and `SPRITE_PALETTE(n)` (before: avg 75,052, peak 80,545; after: peak 80,520). Screen-space entities skip the camera with a test and conditional moves; the camera is loaded only for the others (two loads became one load-multiple, so ~+5 net). Sprites with a palette take the out-of-line rotated path, folded into the existing angle/hidden test (one ARM immediate covers both bits), so others don't pay; an inline palette test cost ~+500, and subtracting the camera conditionally ~+420. IWRAM +224 bytes (the palette path) |
| 2026-10-05 | `6ba7de7` | 75,165 | 26.7% | Per-body gravity (`body_gravity`), contacts (`physics_set_contacts`), `ecs_gather`/`ecs_count` in IWRAM (before: avg 75,052, peak 80,545, HEAD without them; after: peak 80,573). bunnymark keeps `sys_physics`' fast loop; the cost is the check for gravity scales (~+110, sixteen per word read, only while there is gravity). Recording contacts in the fast loop cost ~+1,000-2,000 (out of registers) and scaling gravity per body ~+3,500, so both go to the general loop (out of line, IWRAM), scaled bodies further out of line in ROM. IWRAM +288 bytes (the two pools) |
| 2026-10-05 | `6ba7de7` | 74,969 | 26.6% | Net result of the commit, with all its engine changes (before: avg 75,052, peak 80,545; after: peak 80,489); the two rows above were measured on intermediate trees |
| 2026-10-05 | `01963b8` | 75,045 | 26.7% | Sprite scaling (`sprite_draw_ex`, `SPRITE_SCALED` with `spr_scale`) and `sprite_stats()` (before: avg 74,969, peak 80,489; after: peak 80,425). Reading `spr_scale` for every entity in the render loops cost ~+1,000 (the loop is out of registers), so entities opt in with the `SPRITE_SCALED` flag, which joins the existing one-immediate test of `spr_angle` and the flags (~+75). Dropped-draw counters only on the rare paths |
| 2026-10-05 | `01963b8` | 75,162 | 26.7% | `body_max_fall` in fixed point, a `u16` (before: avg 75,045, peak 80,425; after: peak 80,641). Its zero-skipping pass reads twice the bytes (~+115) |
| 2026-10-05 | `01963b8` | 75,693 | 26.9% | Net result of the commit (before: avg 74,969, peak 80,489; after: peak 81,301). The last +531 is code layout, not work: building a new rotation matrix moved out of IWRAM into ROM (saving IWRAM; bunnymark never calls it), which shifts the Thumb code after it in ROM; the same tree with it in IWRAM measures 75,162. The render loops call the transformed path with four arguments, so its nine don't spill in the usual path |
| 2026-10-06 | `NEXT` | 73,551 | 26.1% | `sys_render_by_depth` sorts by what the depths need (before: avg 75,693, peak 81,301; after: peak 78,741): no sort when depths are already in slot order, one counting pass over only the buckets the depths span (bunnymark's y range: about 160 instead of 256 cleared and summed), two passes only for ranges of 256 or more. The render cost test (88 sprites, two depths) went from 7,532 to about 4,100 cycles over `sys_render`; one depth from 9,132 to about 400 |
| 2026-10-06 | `NEXT` | 73,421 | 26.1% | Net result of the commit, with metasprites and the opt-in scanline count (before: avg 75,693, peak 81,301; after: peak 78,653). Metasprites ride on the drawing path's existing frame-out-of-range rejection, so `sys_render_by_depth` doesn't pay for them; plain `sys_render` (not in bunnymark) pays about 5 cycles per sprite for the call in its loop (88 sprites: 16,578 to 17,018). The scanline count is a flag test in `frame_end()` while off |

## Memory use

IWRAM (32 KB, the fast RAM) holds the engine's hot code, the ECS pools, the shadow OAM and the stack, and is shared with the game. Unused engine code is dropped at link time (`--gc-sections`), so use depends on the features a game calls. Measured with `arm-none-eabi-size -A` on the release `.elf` files (`.iwram`, code and initialized data, plus `.bss`; the games' own data included, the stack not), at `NEXT`:

| Example | IWRAM used |
| --- | --- |
| `hello` | 9,536 bytes |
| `shmup` | 16,268 bytes |
| `blackjack` | 17,816 bytes |
| `pong` | 18,272 bytes |
| `bunnymark` | 18,408 bytes |
| `asteroids` | 19,056 bytes |
| `platformer` | 21,380 bytes |
| `breakout` | 21,852 bytes |

Depth sorting by what the depths need and the metasprite hook in the drawing paths added about 350 bytes (`hello`: 9,180 at `01963b8`); drawing a metasprite's pieces and counting scanlines run from ROM, and the scanline table is in EWRAM. Sprite scaling and `sprite_stats()` add about 500 bytes to every game that draws sprites (the transformed draw path, which handles rotation, scaling, hidden sprites and palettes, and the matrix keys; building a new matrix runs from ROM and `spr_scale` lives in EWRAM), and a fixed-point `body_max_fall` 128 bytes to games with bodies.

Bouncing bodies' `body_gravity` and `body_contact` pools add 256 bytes to games that use `sys_physics` or `sys_map_movement`, and `ecs_count` and `ecs_gather` about 120 bytes each to games that call them (each has an IWRAM section of its own).

Games that load map layers add about 2.2 KB: the streaming loops (1.4 KB of ARM code, [tilemaps.md](tilemaps.md#streaming)), the runtime cell change table (384 bytes, read by every collision query) and the per-background state. The screenblock copies (6 KB) and the redraw list are in EWRAM.

The linker script reserves 2 KB below the stack and fails the build if IWRAM overflows. Large engine buffers live in EWRAM (256 KB) instead.

## Checking what a game shows and plays

Tests check state (OAM, VRAM, registers), not what the screen looks like or what the speakers play. During development, a small capture tool built on mGBA's core library ran ROMs headlessly with scripted buttons, saved chosen frames as images and recorded the audio, to check each example against its header comment; it loads and writes the ROM's `.sav` as mGBA does, so saves carry over between runs. It is not part of the repository yet; adding it (and turning it into screenshot tests in CI) is an open question.

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
