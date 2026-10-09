# Development

**Status:** current: describes the build, tests, benchmark and CI/CD as they are.

## Requirements

**On Linux, `tools/setup-dev.sh` sets all of this up but Chrome, which it only looks for** (x86_64 or aarch64): the system packages it needs through `apt` (with `sudo`, only those missing), the ARM GNU Toolchain (checksum-verified), `mgba-rom-test` built from source, and Emscripten through emsdk, at the versions CI uses, into `~/opt` (`--prefix` to change). It writes `~/opt/serval-env.sh`, which sets `ARM_GNU_TOOLCHAIN`, `MGBA_ROM_TEST_DIR` and `EMSDK`; `--add-to-shell` sources that from `~/.profile` and the top of `~/.bashrc` (before its "not interactive" return, so scripts and tools see it too). `examples/build-all.sh` reads that file itself for whatever the shell lacks, so it works even from a terminal or IDE started before setup. Running it again only checks what is there. `--no-web`, `--no-rom-tests` and `--no-system` skip parts; `--with-lua32` also builds Lua 5.4 with 32-bit integers into `~/opt/lua-5.4.8-32` and exports `SERVAL_LUA32` for the [Lua differential test](#tests); `--help` lists them. Its version pins must match CI's (`.github/actions/setup-gba/action.yml`, `.github/workflows/ci.yml` and `pages.yml`). On other systems, install the tools below by hand.

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 3.25 | Uses presets (`CMakePresets.json`) |
| Ninja | any recent | Generator for all presets |
| ARM GNU Toolchain | 15.3.Rel1 (what CI uses) | Any `arm-none-eabi-gcc` should work, including devkitARM; CMake warns if it is older than `toolchain.gcc` in `serval.json`. Found on `PATH` or via `ARM_GNU_TOOLCHAIN=<toolchain root>` |
| Python 3 | 3.11 or later | Runs `tools/gbafix.py` after each ROM link, `tools/svlua.py` and `tools/svm.py` for scripts (`serval_add_script()`), and `tools/check-rom.py`, `tools/gbafix_test.py`, `tools/svm_test.py`, `tools/svlua_test.py`, `tools/svlua_difftest.py`, `tools/logo-png.py` and `tools/check-planned.py` in the tests (the last also in web builds) |
| Host C compiler | GCC or Clang | Only for host unit tests |
| Emscripten | 6.0.11 (what CI uses) | Only for [web builds](#web-builds). Install with [emsdk](https://emscripten.org/docs/getting_started/downloads.html); the toolchain file finds it through `EMSDK` alone (set it, or `source emsdk_env.sh`, which also puts emsdk's own tools on `PATH`), or `emcc` on `PATH` |
| Chrome or Chromium | any recent | Only for `tools/web-shots.py` (found on `PATH`, or set `SERVAL_CHROME`) |
| Lua 5.4, `LUA_32BITS` | 5.4.8 | Optional: only for the Lua compiler's differential test (CTest `svlua_difftest`, skipped without it). `tools/build-lua32.sh <dir>` downloads it from lua.org, checks the published SHA-256, builds it with 32-bit integers and floats; set `SERVAL_LUA32=<dir>/bin/lua` (`setup-dev.sh --with-lua32` does both) |
| `mgba-rom-test` | mGBA 0.10.5 | Runs the test ROM. Optional locally (without it the ROM tests are built but not run), unless `SERVAL_REQUIRE_ROM_TESTS` is ON (as in `gba-ci`), which makes configuration fail without it. Build it with `tools/build-mgba-rom-test.sh <dir>` and set `MGBA_ROM_TEST_DIR=<dir>` |

Download the ARM GNU Toolchain from <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads> (`arm-none-eabi`, for your host).

## Build and test

```sh
# GBA: engine library, example ROMs and the test ROM
. ~/opt/serval-env.sh             # from tools/setup-dev.sh; or export ARM_GNU_TOOLCHAIN and MGBA_ROM_TEST_DIR
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
. ~/opt/serval-env.sh             # sets EMSDK; or source ~/opt/emsdk/emsdk_env.sh
cmake --preset web-release
cmake --build --preset web-release
# -> build/web-release/examples/hello.html, bunnymark.html, pong.html, asteroids.html,
#    breakout.html, platformer.html, shmup.html, blackjack.html, fireflies.html,
#    effects.html
```

Open a page from disk, or upload it to any static host. A page waits for a click or key press before starting the game, because browsers only allow sound after one.

To check a page without a display, `tools/web-shots.py` runs it headless with scripted buttons and saves chosen frames as PNG files (same numbering and button letters as the capture tool, for comparing frames with an emulator):

```sh
tools/web-shots.py build/web-release/examples/pong.html 400 /tmp/pong shot=100 shot=400 key=250:S:2
# -> /tmp/pong-00100.png, /tmp/pong-00400.png
```

`--require-picture` fails if a shot is one flat color, as CI uses it. Pages run this way start with blank save memory and never touch `localStorage`; `save=FILE` loads the game's save memory from `FILE` (a `.sav`, as mGBA writes) if it exists and writes it back at the end, so consecutive runs see each other's saves ([platforms.md](platforms.md#web)). `expect-title=TEXT` and `expect-save-key=TEXT` fail unless the page's title, as the browser shows it, and the `localStorage` key of its saves are `TEXT` (the page's test mode reports both).

Frame numbers don't line up exactly with an emulator's. The page counts `frame_end()` calls, while an emulator counts every refresh, including ones the game misses while booting or loading (the platformer misses about 20 while building its level). In the examples so far, web shot N matches mGBA frame N + 1, and web buttons must be scripted about 2 frames earlier. A game that loads for a while needs a bigger shift, found by comparing a frame both builds show. With the same button timing, the same game plays out identically on both: `random_entropy()` depends only on input and frame count. That holds as long as the game itself is deterministic C: the GBA build uses GCC and the web build clang, which may evaluate function arguments in different orders, so `f(random_range(0, 3), random_range(0, 3))` can differ between them. Make random calls one per statement.

## Source layout

| Path | Contents |
| --- | --- |
| `include/serval/` | Public headers: `serval.h` (umbrella: includes everything), `core.h` (init, splash, frames, buttons), `screen.h` (backdrop, brightness, color mixing, alpha blending; raster effects planned), `sprites.h`, `ecs.h`, `physics.h`, `map.h` (tilemaps, camera, map collision), `path.h`, `audio.h`, `text.h`, `math.h`, `fixed.h`, `random.h`, `save.h` (save slots), `vm.h` (the bytecode VM), `debug.h`, `platform.h` (types, placement macros, `SERVAL_PLANNED`); `gba.h` holds GBA-only escape hatches. No third-party includes |
| `src/ecs/` | Platform-neutral systems: entities, `ecs_count`/`ecs_gather` and `sys_movement` (`ecs.c`), bouncing bodies, `sys_physics`, `body_overlap`, `body_hit_side` (`physics.c`, with `physics_internal.h`, state shared with map bodies), map bodies and `sys_map_movement` (`map_movement.c`), `sys_animate` (`animate.c`) |
| `src/core/` | Platform-neutral modules: color math (`color.c`), map layers as data, the camera, runtime cell changes and collision queries (`map.c`, with `map_internal.h`), held-button repeat (`input.c`), random numbers and `random_entropy` (`random.c`), text formatting, warnings' included (`text_format.c`), trigonometry, `angle_of` and `fx_length` (`trig.c`), paths and `sys_path` (`path.c`), the PSG music sequencer (`psg_sequencer.c`), the registered sprite table, so `sys_animate` can read assets (`sprite_table.c`), save slots on a byte-addressed save memory (`save.c`, with `save_internal.h`, the memory each platform supplies), the splash screen's timing and buttons (`splash_logic.c`), the bytecode VM: loader, interpreter, scheduler and collision pass (`vm.c`, with `vm_internal.h`, the engine calls each platform supplies), and `warn.h` (the warning macro and the longest warning, 247 characters) |
| | `src/ecs/` and `src/core/` compile on the host (no libtonc, no hardware access). On the GBA they are part of the single `serval` library; host builds compile them alone as `serval_portable`, with `src/host/platform.c` (stderr output, clock-based entropy, save memory in RAM) |
| `src/gba/` | GBA-only code: core API, frame loop, frame timing and `WAITCNT` (`core.c`), sprites, rotation and render systems (`sprites.c`), runtime sprite tiles (`sprite_tiles.c`), streamed groups' slots (`sprite_stream.c`), map layers in VRAM: tileset, animated tiles, streaming and background registers (`map.c`), palette writes: the shadow palette and its VBlank copy (`palette.c`), brightness fades and the planned raster stubs (`screen.c`), alpha blending (`blend.c`), text layer (`text.c`), PSG sound effects (`psg.c`) and the music player (`music.c`, hooked in by `psg_music_play()`), the planned tracker music and sampled sound effects' stubs (`sampled_audio.c`), the VM's engine calls for sound, text, buttons, brightness and blending (`vm_platform.c`, also in web builds), save memory and its ROM ID string per save type (`save_sram.c`; `save_flash.c`, Flash with its chip-reading routines in EWRAM; `save_eeprom.c`, EEPROM through DMA3), each compiled once per type into a `serval_save_<type>` object, splash screen (`splash.c`, its logo art in `splash_art.c`), debug output (`debug.c`), engine-internal declarations (`internal.h`, `screen_internal.h`), startup code (`crt0.s`), linker script (`gba.ld`), and `memcpy` and friends (`libc.c`, linked into every ROM as the `serval_libc` object) |
| `src/web/` | The web target ([platforms.md](platforms.md#web)): the renderer (`ppu.c`) and PSG sound (`apu.c`), plain C on the GBA's memory regions (`web.h`; host-tested); stand-ins for the BIOS calls, libtonc's assembly copies, the debug output and the cycle counter (`platform.c`); save memory kept in `localStorage` (`save.c`, one object per save type); the page template (`shell.html`: canvas, input, sound, test mode) |
| `third_party/libtonc/` | Vendored libtonc, see its `VENDORED.md` |
| `tests/` | The harness (`test.h`, `test.c`); shared suites run natively and in the ROM (`ecs_tests.c`, `physics_tests.c`, `map_tests.c`, `anim_tests.c`, `path_tests.c`, `math_tests.c`, `random_tests.c`, `text_format_tests.c`, `input_tests.c`, `psg_sequencer_tests.c`, `save_tests.c`, `color_tests.c`, `vm_tests.c`, `splash_logic_tests.c`, and the [planned API](#planned-api)'s run-time suites `planned_audio_tests.c`, `planned_sprites_tests.c`, `planned_map_tests.c` and `planned_screen_tests.c`); `planned/` (every planned name used once, for `tools/check-planned.py`); host-only suites for the web renderer and sound (`web_ppu_tests.c`, `web_apu_tests.c`); the runners (`host/main.c`, `rom/main.c`); hardware suites in `rom/` (core, sprites, sprite streaming, runtime sprite tiles, map layers, presentation (fades, text styles, hidden sprites, animated tiles), alpha blending, palette writes (`palette_tests.c`), text, audio, splash, save memory, libc, ECS and physics costs, the VM's engine calls, its collision pass's cost and a full room's attaches, libtonc compatibility: `compat_*.c`); `rom/save_main.c` (the per-save-type test ROMs) and `rom/run-rom-test.cmake` (runs them and checks mGBA's log); `rom/header_main.c` (the ROMs that test `serval_add_rom()`'s `TITLE` and `GAME_CODE`); `rom_fields_test.cmake` (its configure-time checks); `public_headers.c`; `svlua/` (the Lua compiler's golden listings, `runner.c` (`svlua_runner`: a blob run on the VM, its state printed), `stub.lua` (the engine's API in Lua) and `diff/` (the differential test's programs)); `svm/` (the hand-written fireflies listing, the assembler's fixture); and `consumer/` (a minimal game project built against the release archive, with a Lua script, plus a game that saves) |
| `examples/` | Example games, one directory each (see [getting-started.md](getting-started.md#1-build-the-examples)): `hello`, `bunnymark` (also the benchmark), `pong`, `asteroids`, `breakout`, `platformer`, `shmup`, `blackjack`, `fireflies`, `effects`; `build-all.sh` builds them all into `roms/` and `html/`; `gallery.toml` describes them for the [documentation site](#documentation-site) |
| `cmake/` | Toolchain files (`arm-gba-toolchain.cmake`, `web-toolchain.cmake`), `serval_add_rom()` and `serval_add_script()` (`Serval.cmake`, with the web variant of the former in `ServalWeb.cmake`) and `serval_add_rom_checks()` (`ServalRomChecks.cmake`) |
| `tools/` | Development setup (`setup-dev.sh`, [above](#requirements)), ROM header fixer (`gbafix.py`, tested by `gbafix_test.py`), the Lua-subset compiler (`svlua.py`, tested by `svlua_test.py` and `svlua_difftest.py`; [lua.md](lua.md)), the script assembler and disassembler (`svm.py`, tested by `svm_test.py`; [vm.md](vm.md#tools)), a 32-bit Lua's build (`build-lua32.sh`), ROM checker (`check-rom.py`), the [planned API](#planned-api)'s check (`check-planned.py`), mGBA test runner build, release packaging, release-archive game checks (`check-consumer.sh`, and `check-consumer-web.sh` for web builds), benchmark (`bench.sh`), headless web page runner (`web-shots.py`), the logo PNGs and GitHub's social preview card (`logo-png.py`: compiles the splash's own drawing code for the host and writes `docs/images/`; CTest `logo_png` fails when `splash_art.c` changes until they are regenerated), the documentation site's generator (`site/`, [below](#documentation-site)) |

## Building a game

`serval_add_rom()` (in `cmake/Serval.cmake`) links sources against the engine with its own startup code and linker script, no C library, then produces the `.gba` file with a fixed header:

```cmake
serval_add_rom(my_game SOURCES main.c TITLE "MY GAME" GAME_CODE "MYGM")
serval_add_rom(my_rpg SOURCES main.c TITLE "MY RPG" GAME_CODE "MYRP" SAVE FLASH128K)
```

`TITLE` is 1 to 12 printable ASCII characters, space (`0x20`) to `~` (`0x7E`); left out, it is the target name in upper case, cut to 12 characters. `GAME_CODE` is exactly 4 of them (default `0000`). Every printable ASCII character works, spaces at either end included, and reaches the ROM header and the web page unchanged: `serval_add_rom()` hands both to `tools/gbafix.py` as hexadecimal character codes, so no generator expression or shell sees them, and the web page escapes them. In `CMakeLists.txt` they are CMake strings, so `"`, `\` and `$` are written `\"`, `\\` and `\$`. Anything else stops the configuration with an error naming the target, the field and what is wrong (the same rules as Studio Advance's): an empty `TITLE`, a longer one, a `GAME_CODE` of any other length (it is never padded), a control or non-ASCII character in either, e.g. `serval_add_rom(my_game): GAME_CODE must have exactly 4 characters; "MYG" has 3.` `SAVE` is the cartridge save memory that `save.h` uses: `SRAM` (default), `FLASH64K`, `FLASH128K`, `EEPROM8K` or `EEPROM512`; it sets the slots and their capacity, and the ROM gets that type's backend (the `serval_save_<type>` object) and ID string only, or nothing if the game never saves ([runtime-systems.md](runtime-systems.md#save-types)). An unknown or empty type is a configure error. It works both inside the engine's tree (`examples/`) and from a game's own CMake project that adds the engine (a release archive or a checkout) with `add_subdirectory()`; everything it needs comes from the function's own directory, cache variables or target properties, never from the engine's directory scope. Game sources get `-ffunction-sections -fdata-sections` from the `serval` target, so unused game code is dropped too. `tests/consumer/` is the reference game project, and [getting-started.md](getting-started.md#2-create-your-game) walks through creating one.

In web builds (the `web` presets) it produces `<target>.html` instead, from the same arguments; see [Web builds](#web-builds).

`serval_add_script()` (same file) builds a script for the VM at build time, from the [Lua subset](lua.md) or from a hand-written listing ([vm.md](vm.md#tools)):

```cmake
serval_add_script(my_game game.lua HEADERS game.h serval/ecs.h serval/core.h)
serval_add_script(my_game scripts.svm HEADERS game.h serval/ecs.h)   # a listing
```

A `.lua` script is compiled by `tools/svlua.py` to `game.svm` in the target's binary directory; that listing, or a `.svm` one, is assembled by `tools/svm.py`. Either runs whenever the script, a header, the tools or `vm.h` changes, writing `game_script.c` (the blob as `const unsigned char game_script[]` and `game_script_size`) and `game_script.h` (`OBJ_*`, `STR_*`, `G_*` and `ARR_*` defines for the script's objects, strings, globals and arrays, their counts, and the two `extern`s) into the target's binary directory, adds both to the target and the directory to its include path; the game includes the header and calls `vm_load(game_script, game_script_size)`. `SYMBOL` renames the array (default `<basename>_script`), `PREFIX` the defines; `HEADERS` are C headers whose integer constants the script may use (a Lua script's names in ALL_CAPS), relative to the game's directory or to the engine's `include/`. Call it after `serval_add_rom()`, from the same directory. It works from a game's own project too, since the release archive ships `tools/svlua.py` and `tools/svm.py`; `tests/consumer/` (`consumer.lua`) and `examples/fireflies` use it.

ROMs are padded with `0xFF` to at least 512 KiB. Emulators guess whether a small file is a cartridge or a multiboot image (which runs from RAM and is at most 256 KiB); older mGBA releases (0.8.x) mistake small Serval ROMs for multiboot and show a white screen. Anything over 256 KiB is always treated as a cartridge.

## Tests

Test cases use the small harness in `tests/test.h` (`CHECK(cond)`, `TEST_SUITE(...)`), which needs no C library.

- **Platform-neutral suites** (e.g. `ecs_tests.c`) run both natively and in the test ROM.
- **Hardware suites** (`tests/rom/`) run only in the test ROM.

The test ROM writes results to mGBA's debug log and ends with `swi 3`, passing 1 in `r0` if any check failed and 0 otherwise (not the failure count, since exit codes wrap at 256); `mgba-rom-test -S 3 -R r0` turns that into its exit code. Register new suites in `tests/host/main.c` and/or `tests/rom/main.c`, and list the file in `tests/CMakeLists.txt` (`serval_test_harness` for shared suites, `serval_tests` for hardware ones).

Save types get test ROMs of their own: `serval_tests` saves to SRAM, and `serval_tests_flash64k`, `serval_tests_flash128k`, `serval_tests_eeprom8k` and `serval_tests_eeprom512` (built with that `SAVE`, runner `tests/rom/save_main.c`) run the shared save suite and the hardware save suite (`tests/rom/save_tests.c`) on mGBA's Flash or EEPROM. Their ctests (`rom_tests`, `rom_tests_<type>`) run through `tests/rom/run-rom-test.cmake`, which also checks mGBA's log: it must have detected the save type the ROM was built for (e.g. "Detected Flash savegame", and the switch to 128 KiB) and reported no malformed Flash or EEPROM access. `tests/save_tests.c` runs every case on every type's layout, with simulated memories that follow each type's rules (Flash erase and programming, EEPROM blocks) and lose power after every step.

Debug-only behavior (`SERVAL_DEBUG` warnings, [core-api.md](core-api.md#debug-builds-report-misuse)) is tested with `debug_warning_count()` and, for a warning's text, `serval_warn_text()` (`src/core/warn.h`), with `#ifdef SERVAL_DEBUG` branches for what release builds must do instead. CI runs the test ROM in RelWithDebInfo and Debug builds (checks on) and in a Release build (checks off), all with warnings as errors.

Every ROM (test ROM and examples) also gets a `<target>_rom_checks` test (`serval_add_rom_checks()`, running `tools/check-rom.py`): the ROM is padded past 256 KiB and has a valid header (title, game code, checksum), newlib's `libc.a` is not in the link map, the code contains no BLX instruction (the ARM7TDMI has none), and a ROM that links the save code contains exactly one save type ID string, its type's (`SRAM_V113`, `FLASH512_V131`, `FLASH1M_V103` or `EEPROM_V124`), which emulators and flash carts look for, while a ROM that doesn't save contains none. `tests/consumer/` builds a minimal game against the release archive, the way games use it, runs it in mGBA and checks that unused game code was dropped, with a Lua script built by `serval_add_script()` (a global starting at an initial value, to which its Create handler adds; the ROM checks the sum), plus a game that saves with `SAVE EEPROM8K`; run it with `tools/check-consumer.sh <serval-engine-X.Y.Z.zip>`.

`serval_add_rom()`'s `TITLE` and `GAME_CODE` are tested three ways. CTest `rom_fields_configure` (host preset, `tests/rom_fields_test.cmake`) configures scratch projects that call `serval_add_rom()` with bad values and checks each error message, and with good ones, every printable ASCII character among them, which must pass. The GBA presets build ROMs (`serval_header_*`, `tests/rom/header_main.c`) whose titles and game codes hold every printable ASCII character, spaces at either end, and text such as `$<1:X>`, `${A}`, `;` or a leading `-`; their `<rom>_fields` tests compare the header with the character codes of the text given, and one ROM checks the default title (the target name in upper case, cut to 12). `tools/gbafix_test.py` (CTest `gbafix_tool`, host preset) tests the header fixer: the header it writes, every printable character as text and as hexadecimal codes, and every value it refuses, with the ROM left as it was. The web side is checked by `tools/check-consumer-web.sh` (CI's web job), which also builds pages whose titles and game codes are full of markup (`</title>&lt@`, `&#60&#x3c&lt`, `{{{"`, `${}#`, and quotes, a backslash, a backtick and spaces at either end) and checks that each boots, draws, shows its title and keeps its saves under the right key.

`tools/svm_test.py` (CTest `svm_tool`, host preset) tests the script assembler and disassembler: the golden bytes of [vm.md](vm.md#worked-example-golden-bytes) field by field and their round trip, `PUSH` widths at every boundary, exact `rel16` and `CALL` bytes, every error with its line number, header scraping, a round trip of a blob with every opcode, the array table, RAM pool positions and ROM data (laid out by default and placed with `.data`, and back), the globals' initial values, the hand-written fireflies listing (`tests/svm/fireflies.svm`) and its round trip, and the operand table against `vm.h`.

The Lua-subset compiler ([lua.md](lua.md)) is tested twice over:

- `tools/svlua_test.py` (CTest `svlua_tool`): the lexer and the parser, one test per rejected construct, types and inference, golden listings (`tests/svlua/*.lua` and `.svm`, assembled by `svm.py`; `SVLUA_UPDATE_GOLDEN=1` rewrites them), and compiled programs run on the engine's VM by **`svlua_runner`** (`tests/svlua/runner.c`, built by the host preset, so under the sanitizers): it loads a blob, sets globals, starts threads and attaches instances, runs frames as a game's loop would without rendering (`vm_step`, optionally `sys_movement` and `sys_physics` with contacts, the collision pairs asked for, `vm_events`) with scripted buttons, and prints the engine calls as they happen and every global, RAM array cell and attached instance's properties and fields after the chosen frames (`svlua_runner BLOB.bin --frames N --start OBJ --attach OBJ:X:Y --buttons F:MASK:LEN --print all`; its header comment lists every option). `fireflies.lua` plays a round on it.
- `tools/svlua_difftest.py` (CTest **`svlua_difftest`**): lua.md's rule, that a program means what it means in Lua 5.4 with 32-bit integers, checked frame by frame. Each program in `tests/svlua/diff/` runs under real Lua with `tests/svlua/stub.lua` standing in for the engine (instances, behaviours as coroutines in a pool of contexts, the event queue, the frame's order, `random_range` bit for bit) and compiled on the VM by `svlua_runner`; after every printed frame both must agree on every global, array cell, instance property and field and on the frame's engine calls (fixed values within the program's tolerance), and the VM run must not warn (so a program stays under the ops budget, which the stub doesn't model). A program's `-- diff:` comment lines say how to run it (frames, threads, instances, buttons, collisions, seed, tolerance; the script's docstring lists them). It needs `SERVAL_LUA32`, a Lua 5.4 built with `LUA_32BITS` (`tools/setup-dev.sh --with-lua32`, or `tools/build-lua32.sh`), and is skipped without it. Run one program with `tools/svlua_difftest.py -v tests/svlua/diff/NAME.lua` (`-v` shows both sides' output when they differ).

Compile-only checks keep third-party libraries behind the API ([core-api.md](core-api.md#dependencies-stay-behind-the-api)):

- **Host build:** `tests/public_headers.c`, every example's sources (`examples/*/*.c`, found automatically) and `tests/consumer/*.c` compile without libtonc on the include path (`serval_api_only_check`).
- **Each public header on its own:** every `include/serval/*.h` compiles alone, with only `include/` on the include path, on the host and for the GBA (`serval_header_check`, `serval_gba_header_check`).
- **Test ROM:** `tests/rom/compat_*.c` include `<tonc.h>` and Serval's headers in both orders.

Planned API has a compile-time check of its own, CTest `planned_api` ([below](#planned-api)).

## Benchmark

`examples/bunnymark` doubles as the engine's CPU benchmark: its game (`bunnymark.c`) is also built with `bench.c` as `bunnymark_bench`, which starts with 128 bunnies (the entity limit) from a fixed random seed, runs 600 frames headless in mGBA and reports the CPU cycles spent per frame. Lower is better; the frame budget is 280,896 cycles.

```sh
tools/bench.sh            # optional preset argument, default gba-release
# bunnymark: 128 bunnies, 600 frames: avg 73360 cycles (26.1%), peak 77491 (gba-release)
```

The result is deterministic for a given build, so any change in the number comes from the code. `tools/bench.sh gba-debug` runs the unoptimized build (-O0): avg 266,094 cycles (94.7%), peak 308,729, at `393c36f`, so a debug build of a game as busy as bunnymark drops frames at its peaks. When bunnymark itself changes, the workload changes: record a new baseline row and say so. CI runs it in every run of its GBA job and shows the result in the job summary. For a performance change, run it before and after and put both numbers in the commit message.

| Date | Commit | avg cycles | % of frame | Change |
| --- | --- | --- | --- | --- |
| 2026-10-04 | `e5fbaf0` | 149,564 | 53.2% | Baseline: unoptimized Thumb code in ROM |
| 2026-10-04 | `657244b` | 164,650 | 58.6% | **Workload change**, not an engine change: bunnymark gained gravity, friction and a third HUD line. New baseline; peak 171,675 |
| 2026-10-04 | `d685165` | 83,072 | 29.5% | `sprite_draw`/`sys_render` as ARM code in IWRAM, per-sprite draw data resolved at load, no per-sprite call |
| 2026-10-04 | `06cbcca` | 82,929 | 29.5% | Debug checks (compiled out of release builds) |
| 2026-10-04 | `55d56f0` | 86,510 | 30.7% | Partly a workload change: bunnymark's HUD now uses `text_print_line`, which blanks the rest of each row |
| 2026-10-04 | `5969c88` | 53,217 | 18.9% | `text_format` without division; `WAITCNT` set to 3/1 + prefetch (all ROM code, including the game's, \~40% faster); `sys_movement` in IWRAM |
| 2026-10-04 | `2075d3b` | 57,173 | 20.3% | Per-entity `spr_flags` in `sys_render` (\~29 cycles per sprite); bunnymark uses `ECS_FOR_EACH` |
| 2026-10-04 | `8cac6a0` | 71,266 | 25.3% | **Workload change**: bunnymark now uses the engine's `sys_physics` (faster than its own) plus depth-sorted drawing (\~10,000) and a facing/depth system (\~5,400) |
| 2026-10-04 | `1116a04` | 71,345 | 25.3% | Open-edge checks in `sys_physics` (Pong) |
| 2026-10-04 | `a10fd04` | 72,875 | 25.9% | One rotation check per sprite in the render systems; physics loop specialized for wrapping (Asteroids) |
| 2026-10-04 | `724c283` | 74,976 | 26.6% | Correctness fixes (before: avg 72,875, peak 76,551; after: peak 80,782). Mainly `ent_has` requiring `C_ALIVE` (\~+1,430), `text_format` (\~+490) and the physics fixes (\~+230) |
| 2026-10-05 | `724c283` | 74,684 | 26.5% | Tilemaps and camera (before: avg 74,976, peak 80,782; after: peak 80,081). The render systems subtract the camera (\~+740: the loops are out of registers, so it costs two IWRAM loads per sprite), paid for by checking sprite IDs against the constant `SPRITE_MAX` instead of reloading the table size (\~−725); `sys_physics` reads gravity once per call (\~−310) |
| 2026-10-05 | `724c283` | 75,052 | 26.7% | `body_max_fall` in `sys_physics` (before: avg 74,887, peak 80,326, same tree without it; after: peak 80,545). A separate pass that skips 16 zero limits per word-group read (\~+165); inside the main loop it cost \~+2,900, as that loop is out of registers |
| 2026-10-05 | `724c283` | 75,052 | 26.7% | `SPRITE_HIDDEN` (before: avg 74,938, peak 80,441, same tree without the check; after: peak 80,545). The render systems test it together with `spr_angle` (one ORR per entity), sending hidden sprites down the out-of-line rotated path, which drops them (\~+115); a separate test before the draw cost \~+645 |
| 2026-10-05 | `6ba7de7` | 75,057 | 26.7% | `SPRITE_SCREEN` and `SPRITE_PALETTE(n)` (before: avg 75,052, peak 80,545; after: peak 80,520). Screen-space entities skip the camera with a test and conditional moves; the camera is loaded only for the others (two loads became one load-multiple, so \~+5 net). Sprites with a palette take the out-of-line rotated path, folded into the existing angle/hidden test (one ARM immediate covers both bits), so others don't pay; an inline palette test cost \~+500, and subtracting the camera conditionally \~+420. IWRAM +224 bytes (the palette path) |
| 2026-10-05 | `6ba7de7` | 75,165 | 26.7% | Per-body gravity (`body_gravity`), contacts (`physics_set_contacts`), `ecs_gather`/`ecs_count` in IWRAM (before: avg 75,052, peak 80,545, HEAD without them; after: peak 80,573). bunnymark keeps `sys_physics`' fast loop; the cost is the check for gravity scales (\~+110, sixteen per word read, only while there is gravity). Recording contacts in the fast loop cost \~+1,000-2,000 (out of registers) and scaling gravity per body \~+3,500, so both go to the general loop (out of line, IWRAM), scaled bodies further out of line in ROM. IWRAM +288 bytes (the two pools) |
| 2026-10-05 | `6ba7de7` | 74,969 | 26.6% | Net result of the commit, with all its engine changes (before: avg 75,052, peak 80,545; after: peak 80,489); the two rows above were measured on intermediate trees |
| 2026-10-05 | `2a3f5a1` | 75,045 | 26.7% | Sprite scaling (`sprite_draw_ex`, `SPRITE_SCALED` with `spr_scale`) and `sprite_stats()` (before: avg 74,969, peak 80,489; after: peak 80,425). Reading `spr_scale` for every entity in the render loops cost \~+1,000 (the loop is out of registers), so entities opt in with the `SPRITE_SCALED` flag, which joins the existing one-immediate test of `spr_angle` and the flags (\~+75). Dropped-draw counters only on the rare paths |
| 2026-10-05 | `2a3f5a1` | 75,162 | 26.7% | `body_max_fall` in fixed point, a `u16` (before: avg 75,045, peak 80,425; after: peak 80,641). Its zero-skipping pass reads twice the bytes (\~+115) |
| 2026-10-05 | `2a3f5a1` | 75,693 | 26.9% | Net result of the commit (before: avg 74,969, peak 80,489; after: peak 81,301). The last +531 is code layout, not work: building a new rotation matrix moved out of IWRAM into ROM (saving IWRAM; bunnymark never calls it), which shifts the Thumb code after it in ROM; the same tree with it in IWRAM measures 75,162. The render loops call the transformed path with four arguments, so its nine don't spill in the usual path |
| 2026-10-06 | `01fd31b` | 73,551 | 26.1% | `sys_render_by_depth` sorts by what the depths need (before: avg 75,693, peak 81,301; after: peak 78,741): no sort when depths are already in slot order, one counting pass over only the buckets the depths span (bunnymark's y range: about 160 instead of 256 cleared and summed), two passes only for ranges of 256 or more. The render cost test (88 sprites, two depths) went from 7,532 to about 4,100 cycles over `sys_render`; one depth from 9,132 to about 400 |
| 2026-10-06 | `01fd31b` | 73,421 | 26.1% | Net result of the commit, with metasprites and the opt-in scanline count (before: avg 75,693, peak 81,301; after: peak 78,653). Metasprites ride on the drawing path's existing frame-out-of-range rejection, so `sys_render_by_depth` doesn't pay for them; plain `sys_render` (not in bunnymark) pays about 5 cycles per sprite for the call in its loop (88 sprites: 16,578 to 17,018). The scanline count is a flag test in `frame_end()` while off |
| 2026-10-07 | `fae0016` | 73,426 | 26.1% | `button_repeat_reset()` (before: avg 73,421, peak 78,653; after: peak 78,643): one more mask in held-button repeat, which `frame_begin()` runs (\~+5). Measured afterwards, at the freeze, by benchmarking the commits in between; the commit itself recorded no numbers |
| 2026-10-07 | `9911e93` | 73,426 | 26.1% | API freeze, sprites: `SPRITE_BLEND` joins the render loops' out-of-line test (before: avg 73,426, peak 78,643; after: the same). The flags that take the out-of-line path are now bits 4 and 7-12, past one ARM immediate, so the test is `BIC #0x6F` then `ORRS` with the angle and the flags shifted left; an empty `asm` keeps GCC from folding the mask into a constant load (\~+400). IWRAM +24 bytes for games that draw sprites |
| 2026-10-07 | `cf9d54b` | 73,412 | 26.1% | API freeze, ECS: `body_bounce` 255 is a perfect bounce (before: avg 73,426, peak 78,643; after: peak 77,647). A body resting on its floor is recognized before the bounce's multiply, so it pays neither that nor the 255 test; the perfect bounce's computation is inlined (as a call to ROM it cost \~2,600). The map bodies' perfect bounce (`8801ead`) measured the same |
| 2026-10-07 | `4d9f0e1` | 73,360 | 26.1% | Warnings get a 248-byte buffer of their own (before: avg 73,412, peak 77,647; after: peak 77,491). Release builds have no warnings; the change is `text_format`, which now shares its formatter with them: its padding and copying write through a pointer instead of indexing the buffer (\~−50, bunnymark's three HUD lines a frame) |
| 2026-10-07 | `393c36f` | 73,360 | 26.1% | Debug builds' physics (before: avg 73,360, peak 77,491; after: the same; optimized code is byte for byte the same). Unoptimized GBA builds bounce bodies through `hit_wall_call()` in the fast loop too and call the perfect-bounce solver in ROM, which brings `sys_physics()`' IWRAM code from 13,644 bytes to 6,128 (6,288 in release builds), so debug builds link again ([Debug builds](#debug-builds)). Debug bunnymark: avg 266,094, peak 308,729 |
| 2026-10-07 | `9ff0b75` | 73,380 | 26.1% | The text functions refuse a NULL string (`9403c72`; before: avg 73,360, peak 77,491; with the first version of the check: avg 73,587, peak 77,719; after: peak 77,527). Release builds test only for NULL; debug builds also test that the string is a plausible pointer, three address ranges that cost \~200 a frame for bunnymark's three HUD lines (`text_format` and `text_print_line` each) |
| 2026-10-07 | `effcad9` | 73,347 | 26.1% | The frame's input step returns before any work when no button is held, and does its rarer work out of line (before: avg 73,380, peak 77,527; after: peak 77,443). A frame with buttons held but none newly pressed pays one test more than before. IWRAM +8 bytes of `.bss` |
| 2026-10-08 | `dfe1a5b` | 73,488 | 26.1% | Kinematic bodies (`C_KINEMATIC`), which `sys_physics` skips (before: avg 73,347, peak 77,443; after: peak 77,623). Its loops test six component bits now, one more than fits an ARM immediate, so a BIC moves bit 7 onto `C_ALIVE` first: one instruction per slot (\~+130). The six-bit mask as written cost \~+410 (four BICs), a separate test of the bit \~+1,700 (the fast loop, out of registers, compiled differently around the branch). IWRAM +16 bytes |

## Memory use

IWRAM (32 KB, the fast RAM) holds the engine's hot code, the ECS pools, the shadow OAM and the stack, and is shared with the game. Unused engine code is dropped at link time (`--gc-sections`), so use depends on the features a game calls. Measured with `arm-none-eabi-size -A` on the `.elf` files (`.iwram`, code and initialized data, plus `.bss`; the games' own data included, the stack not): release builds at `8801ead`, the API freeze's last code change (the same at `393c36f`), and debug builds (the `gba-debug` preset: CMake's Debug configuration, -O0) at `393c36f`. A ROM may use up to 30,464 bytes ([below](#debug-builds)):

| Example | Release | Debug |
| --- | --- | --- |
| `hello` | 9,564 bytes | 14,576 bytes |
| `shmup` | 16,300 bytes | 22,280 bytes |
| `blackjack` | 17,840 bytes | 23,208 bytes |
| `pong` | 18,220 bytes | 23,200 bytes |
| `bunnymark` | 18,356 bytes | 23,324 bytes |
| `asteroids` | 18,992 bytes | 24,480 bytes |
| `fireflies` | 20,232 bytes | 25,828 bytes |
| `platformer` | 21,292 bytes | 26,568 bytes |
| `breakout` | 21,800 bytes | 27,292 bytes |

At `01fd31b` they were within 300 bytes of these (`hello` 9,536, `breakout` 21,852). `platformer`'s row was measured again when it grew to four stages (it was 21,328 and 26,592 bytes): its spawn list moved to EWRAM, beside the level's cells and the map layers it now builds there, and the stages' code and data add almost nothing to IWRAM. `fireflies` measured 21,060 bytes when it was added; its blob is now ROM data built at build time, and the boot-time assembler's label and string tables (536 bytes of IWRAM) are gone. The VM keeps its state in EWRAM, with 61 bytes of IWRAM ([vm.md](vm.md#implementation-notes)).

Depth sorting by what the depths need and the metasprite hook in the drawing paths added about 350 bytes (`hello`: 9,180 at `2a3f5a1`); drawing a metasprite's pieces and counting scanlines run from ROM, and the scanline table is in EWRAM. Sprite scaling and `sprite_stats()` add about 500 bytes to every game that draws sprites (the transformed draw path, which handles rotation, scaling, hidden sprites and palettes, and the matrix keys; building a new matrix runs from ROM and `spr_scale` lives in EWRAM), and a fixed-point `body_max_fall` 128 bytes to games with bodies.

Bouncing bodies' `body_gravity` and `body_contact` pools add 256 bytes to games that use `sys_physics` or `sys_map_movement`, and `ecs_count` and `ecs_gather` about 120 bytes each to games that call them (each has an IWRAM section of its own). Skipping kinematic bodies (`C_KINEMATIC`) adds 8-16 bytes to games that use `sys_physics` (an instruction in each of its loops; 116-120 bytes in Debug builds), measured against `c882915`.

Games that load map layers add about 2.2 KB: the streaming loops (1.4 KB of ARM code, [tilemaps.md](tilemaps.md#streaming)), the runtime cell change table (384 bytes, read by every collision query) and the per-background state. The screenblock copies (6 KB) and the redraw list are in EWRAM.

The linker script reserves 2 KB below the stack and fails the build if IWRAM overflows: `.iwram` and `.bss` together may use up to 30,464 bytes (they must end 2 KB below `0x03007F00`, where the stack starts; the 256 bytes above it hold the interrupt stacks and the BIOS's variables). Large engine buffers live in EWRAM (256 KB) instead.

### Debug builds

Debug builds (-O0) use more IWRAM than release builds of the same game, 4,968 to 5,980 bytes more for the examples (the table above), nearly all of it the engine's IWRAM code, which is larger unoptimized: drawing sprites takes 8,452 bytes instead of 3,548 (every game that draws sprites), the ECS queries 60-656 bytes more, the map streaming loops 272 and the linker's veneers for calls between IWRAM and ROM 72-108; the debug checks' warn-once flags add up to 92 bytes of `.bss`. `sys_physics()`' code takes 160 bytes less (6,128 instead of 6,288), since unoptimized it bounces bodies through calls rather than inlined copies ([runtime-systems.md](runtime-systems.md#physics)). Before `393c36f` it took 13,644 bytes (since `cf9d54b` inlined the perfect bounce), and at `3aebd4e` none of the six examples with bodies, nor the test ROM, linked in Debug.

So a game's own budget is up to 6,000 bytes smaller in a Debug build: one that uses more than about 24,400 bytes of IWRAM in release (30,464 less 6,000) may not link in Debug. RelWithDebInfo (-O2 with the debug checks on, the `gba-ci` preset) uses about as much as release (within 1.2 KB: games with bodies 750-1,120 bytes less, the others 150-500 more), so such a game can debug with it. The test ROM (`serval_tests`), which links most of the engine, uses 27,468 bytes in Debug and 21,640 in release; its largest fixtures are in EWRAM to leave that room.

## Checking what a game shows and plays

Tests check state (OAM, VRAM, registers), not what the screen looks like or what the speakers play. During development, a small capture tool built on mGBA's core library ran ROMs headlessly with scripted buttons, saved chosen frames as images and recorded the audio, to check each example against its header comment; it loads and writes the ROM's `.sav` as mGBA does, so saves carry over between runs. It is not part of the repository yet; adding it (and turning it into screenshot tests in CI) is an open question.

## Code style

- C17 with GNU extensions. `.clang-format` (clang-format 18) is enforced by CI on everything outside `third_party/`.
- `snake_case`; public API names follow the docs (`frame_begin`, `entity_create`); GBA-only API is prefixed `gba_`; macros are `SERVAL_*` or the documented short names (`MAX_ENT`, `C_*`).
- Engine code builds warning-free with `-Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes`.

## Planned API

Planned API is declared but not implemented yet; [releases.md](releases.md#planned-api) has the policy as games see it. Each planned function or constant is marked with `SERVAL_PLANNED("what, doc")` (`platform.h`), naming the feature and the doc that describes it, both of which the warning quotes:

```c
SERVAL_PLANNED("tracker music, docs/audio.md#tracker-music")
void music_play(u16 music_id, bool loop);

enum {
    PSG_WAVE SERVAL_PLANNED("the PSG wave channel, docs/audio.md#wave-channel") = 3,
};
```

**Where the marker may go:**

- **Only on functions and enumerators**: before a function's declaration, or after an enumerator's name. A planned constant is an enumerator, never a `#define`: enumerators are `int` constants, usable in initializers, `case` labels and constant expressions, so games see no difference.
- **Never on struct fields.** GCC ignores the attribute in designated initializers (`.slots = 4` doesn't warn; with Clang it does), so a field that only matters for a planned feature is a plain field. Its flag is the planned enumerator, and data that uses a planned feature always names that flag, which warns.
- **Never on typedefs.** A planned type used in any declaration that isn't itself planned warns inside the header, in every game that includes it, breaking every `-Werror` build. Planned types stay plain; only the functions using them are planned.

The `planned_api` test fails on a `SERVAL_PLANNED` anywhere else (a field, a typedef, a variable).

**Engine code names planned API freely.** `serval`, `serval_portable` and the `serval_save_*` objects define `SERVAL_NO_PLANNED_WARNINGS` privately (`CMakeLists.txt`), for the stubs and for loaders that refuse planned flags; being private, it never reaches games. A stub does nothing harmful and says so once: it returns 0, `false` or its type's "none", changes nothing, and reports with `SERVAL_WARN` once per problem, naming the function, the feature and what happens instead (`"music_play: tracker music is planned, not implemented in this engine version; nothing plays"`). A loader refuses data that needs a planned feature, naming the planned flag in its warning.

**Tests that use planned API on purpose** define `SERVAL_NO_PLANNED_WARNINGS` at their top, before any include: the run-time suites below, and any suite checking that a loader refuses a planned flag. Examples, `tests/public_headers.c` and `serval_api_only_check` keep the warnings, so with CI's `-Werror` no example can use planned API.

**Two tests keep every planned name honest:**

- **At compile time, CTest `planned_api`.** `tests/planned/<header>.c` (one file per header: `audio.c`, `sprites.c`, `map.c`, `screen.c`, `vm.c`) uses every planned name of its header once, each on a line ending `// planned`:

  ```c
  void planned_audio(void) {
      music_play(0, true);                // planned
      static const u8 channel = PSG_WAVE; // planned
      (void)channel;
  }
  ```

  `tools/check-planned.py` compiles each file at `-O0` and `-O2` with the engine's warning flags, and fails unless every marked line warns with the Serval message, no other line and no header does, and every `SERVAL_PLANNED` name in `include/serval/` warns in one of the files; each file must also compile silently with `-DSERVAL_NO_PLANNED_WARNINGS -Werror`. So a planned name declared without a use fails, a use without the marker fails, and so does a marked use of a name that is no longer planned. It runs with each preset's compiler: in the `host` and `gba-*` test presets (with `-DSERVAL_GBA` and the toolchain's flags), and in web builds, which run no tests, as part of the build with `emcc` (target `serval_planned_api_check`). By hand: `tools/check-planned.py` (with `cc`), or `tools/check-planned.py -- arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb -DSERVAL_GBA`.
- **At run time, `tests/planned_<area>_tests.c`** (`audio`, `sprites`, `map`, `screen`; shared suites): each planned function is called twice, its safe result checked (`SFX_NONE`, `false`, state unchanged; loaders refuse), and in debug builds `debug_warning_count()` rises by exactly one on the first call and not on the second. This also shows the stubs link. Stubs in `src/gba/` link only into the test ROM, so their cases go inside `#ifdef SERVAL_GBA`.

**Adding a function in 1.x** (a minor version): `svlua.py` reserves the name of every function in the headers, planned or not, read from them ([lua.md](lua.md#c-functions)), so a new function's name is reserved in scripts from the version that declares it, with no list of names to update. That is a minor change, as adding any function to a C library is: a game's own top-level name of that name, in a script or in C, collides with it. Give the function its module's prefix (`psg_`, `sprite_`, `map_`, `raster_`, ...), which makes such a collision with a script's own names unlikely. A builtin for it, then or later, takes nothing more from scripts.

**Implementing a planned item** (a minor version): drop its `SERVAL_PLANNED` and keep the signature, implement it, remove its line from `tests/planned/`, turn its stub cases into feature tests, and drop "planned" from its docs. A function scripts can call also gets its SYS call, appended, and its builtin in `svlua.py`'s `ENGINE`, by its C name: a name scripts can't declare, planned or not, which `svlua.py` reads from the headers, so it needs no other change ([lua.md](lua.md#c-functions)). [api-freeze.md](api-freeze.md#planned-in-1x-declared-now) lists every planned name by feature, with a suggested order; the hardware and bits each feature may use are already reserved for it ([core-api.md](core-api.md#hardware-the-engine-uses)), and its design doc says what the stub does today and what the implementation must do. Docs that name planned features by status, to update with it: [api-reference.md](api-reference.md), [frame-loop.md](frame-loop.md#vblank-flush) (for anything in the VBlank flush), [platforms.md](platforms.md#planned-features-on-the-web), [overview.md](overview.md#what-10-means), [open-questions.md](open-questions.md), [examples-roadmap.md](examples-roadmap.md#open-gaps), [docs/README.md](README.md) and the README's feature list.

## Documentation site

The docs and the examples are also a website, published on GitHub Pages: <https://stevengann.com/Serval-Engine/>. `tools/site/build.py` makes it: every `docs/*.md` as a page (links between docs become links between pages, links to other files in the repository become GitHub links, `mermaid` blocks become diagrams), a gallery in which every example runs in the browser next to its description (from its `main.c` header comment) and its source, and a front page. Search over the docs is Pagefind's. To build it and look at it (Python 3.11 or later):

```sh
python3 -m venv build/site-venv && build/site-venv/bin/pip install -r tools/site/requirements.txt  # once
cmake --preset web-release && cmake --build --preset web-release   # the examples' pages
build/site-venv/bin/python tools/site/build.py                     # -> build/site/
build/site-venv/bin/python -m pagefind --site build/site            # the search index
python3 -m http.server -d build/site                               # http://localhost:8000/
```

The generator, its page template, styles and scripts are in `tools/site/`, with its pinned requirements (Markdown, syntax highlighting, Pagefind). It takes each example's thumbnail with `tools/web-shots.py`, so it needs Chrome or Chromium, and it fails rather than publish a broken site: an example directory without an entry in `examples/gallery.toml`, a doc missing from the sidebar (`NAV` in `build.py`), a link to a file that doesn't exist, or any local link or `#anchor` in its output that doesn't resolve (it lists them). Every link in the site is relative, so it works at any path. Diagrams use Mermaid from jsDelivr, at the version pinned in `build.py`.

`examples/gallery.toml` describes each example for the gallery: its title (the ROM's `TITLE`), a one-line summary, tags from the vocabulary at the top of the file, a level (one to four stars: how much the example asks of a reader) and how to take its thumbnail (a frame and scripted buttons, as for `web-shots.py`: real gameplay, not the splash or a title screen). **A new example needs an entry**, or the site's build fails.

## CI/CD

**CI** (`.github/workflows/ci.yml`), on pushes to `main`, pull requests and manual runs:

- `clang-format` check.
- Host tests with sanitizers, the Lua differential test included: the job builds Lua 5.4.8 with 32-bit integers (`tools/build-lua32.sh`, which checks lua.org's SHA-256; cached by the script's hash) and runs CTest with `SERVAL_LUA32`.
- GBA: `gba-ci` (RelWithDebInfo, debug checks on) built with warnings as errors and tested (test ROM in mGBA, ROM checks); a check that the release archive builds on its own; `tools/check-consumer.sh` (a game built against the archive and run in mGBA); Release (debug checks compiled out) and Debug (`-O0`) builds with warnings as errors, each tested; every example built; the bunnymark benchmark (result in the job summary); and the ROMs uploaded as artifacts.
- Web: the `web` preset (warnings as errors; its build also runs the [planned-API check](#planned-api) with `emcc`); every example run headless in Chrome for 300 frames with `tools/web-shots.py --require-picture`, after checking that its page loads no other file; `tools/check-consumer-web.sh` (a game project built for the web against the release archive, run and checked the same way, also with markup in its titles and game codes); the pages uploaded as artifacts.

The ARM toolchain and mGBA versions are set in `.github/actions/setup-gba/action.yml`; `mgba-rom-test` is built once and cached. The Emscripten version is set in the web job (and in `pages.yml`).

**Pages** (`.github/workflows/pages.yml`), on pushes to `main` and manual runs: builds the examples with the `web-release` preset, then the [documentation site](#documentation-site) and its search index, and deploys it to GitHub Pages (the repository's Pages source must be set to GitHub Actions).

**Release** (`.github/workflows/release.yml`):

1. Set `version` in `serval.json` and merge to `main`.
2. Tag and push: `git tag v0.2.0 && git push origin v0.2.0`. The tag must equal `v` + the `serval.json` version exactly; a version with a suffix such as `0.2.0-rc.1` creates a prerelease.
3. The workflow runs the whole CI (`ci.yml`) on the tagged commit, then builds Release with warnings as errors, runs the test ROM, packages `serval-engine-X.Y.Z.zip` (with `serval-engine-X.Y.Z.zip.sha256`, and `serval.json` as a separate asset), checks that the archive builds on its own and that a game builds against it (`tools/check-consumer.sh`), and attaches all three to a **draft** release. Only files tracked by git are packaged.
4. Review the draft and publish it. Only published releases are visible to Studio Advance.

See [releases.md](releases.md) for what the archive contains and how versions are numbered.
