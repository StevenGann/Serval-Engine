# CLAUDE.md

Serval Engine: the open-source GBA game runtime (C on libtonc) that every Studio Advance game links. Public repository.

## Status

1.0.0-rc.1: the API is frozen, and unfinished features are declared as planned API (`docs/api-freeze.md`; taking over the engine: `docs/handoff.md`). Implemented: build system, startup code, frame loop, input, sprites (animation, rotation, scaling, metasprites, depth, layers, per-draw palettes, screen-space), tilemaps with a camera and map collision, ECS with movement/physics/animation/path/render systems, PSG sound effects and music, text, fades, color mixing, math, save data (SRAM, Flash or EEPROM per game; localStorage on the web), splash screen, the bytecode VM (interpreter, scheduler, engine bridge, collision pass (`vm_collide`), `tools/svm.py`, the reference assembler and disassembler for its blob format, and `tools/svlua.py`, the compiler for game logic in a Lua 5.4 subset), a web target; nine examples (hello, bunnymark, pong, asteroids, breakout, platformer, shmup, blackjack, fireflies); tests, benchmark and CI/CD. See README.md for the feature summary. `docs/` is the source of truth for design; start with `docs/README.md` and `docs/development.md`. When a design decision is made, update the relevant doc and tick it off in `docs/open-questions.md`.

## Commands

```sh
tools/setup-dev.sh                # Linux: installs every tool below but Chrome at CI's versions; writes ~/opt/serval-env.sh
. ~/opt/serval-env.sh             # ARM_GNU_TOOLCHAIN, MGBA_ROM_TEST_DIR, EMSDK
cmake --preset gba-ci && cmake --build --preset gba-ci && ctest --preset gba-ci   # GBA build + test ROM in mGBA
cmake --preset host && cmake --build --preset host && ctest --preset host         # host tests, ASan/UBSan
tools/setup-dev.sh --with-lua32   # once: Lua 5.4 with 32-bit integers; SERVAL_LUA32 enables CTest svlua_difftest
tools/svlua_difftest.py -v tests/svlua/diff/loops.lua                              # one Lua-vs-VM differential program
cmake --preset web && cmake --build --preset web                                  # web pages (Emscripten 6.0.11, via EMSDK)
tools/web-shots.py build/web/examples/pong.html 400 /tmp/pong shot=400                 # run a page headless, save frames
git ls-files '*.c' '*.h' ':!:third_party/**' | xargs clang-format -i              # clang-format 18
```

## Code layout and conventions

- `src/ecs/`, `src/core/`: portable code that must compile on the host (no libtonc calls, no hardware access); unit tested natively (`serval_portable`, with `src/host/platform.c`) and in the test ROM. GBA-only code goes in `src/gba/`. The web target compiles `src/gba/` unchanged to WebAssembly and runs it on virtual GBA hardware (`src/web/`: registers, VRAM, palette and OAM at their GBA addresses in wasm memory; renderer, PSG emulation, BIOS/libtonc-assembly stand-ins). Keep `src/gba/` compiling for it: no inline asm or ARM-only attributes outside `SERVAL_GBA` / `internal.h`'s `SERVAL_ARM`/`SERVAL_IWRAM_TEXT`. GBA is the baseline; the web fakes what it must (docs/platforms.md#web). On the GBA everything is one `serval` library, so portable and GBA code can call each other without link-order problems.
- Asset conversion (PNG to tiles, packing, palettes) is out of scope for the engine: Studio Advance does it. The engine defines the data formats.
- **Planned API** (declared, not implemented; `docs/releases.md#planned-api`): `SERVAL_PLANNED("feature, docs/x.md#anchor")` on functions and enumerators only, never fields or typedefs; each planned name used once in `tests/planned/<header>.c` on a line ending `// planned` (CTest `planned_api`, `tools/check-planned.py`); its stub returns 0/`false`/none, changes nothing and warns once, and a loader refuses data that needs a planned flag; run-time stub tests in `tests/planned_<area>_tests.c`. To implement one: drop the marker, keep the signature, remove its `tests/planned/` line, turn its stub tests into feature tests, update the docs that call it planned (`docs/development.md#planned-api`).
- **Loaders refuse values they don't understand** (unknown flag bits, nonzero reserved fields, planned flags): return false, load nothing, warn once. Data formats grow only by fields whose zero keeps today's behaviour, or by giving a meaning to a value today's engine refuses (`docs/releases.md#versioning`); collision types are the one exception (`map_load` warns and loads, `docs/tilemaps.md#collision-types`). Hardware and bits the engine claims are API (`docs/core-api.md`).
- Public headers in `include/serval/`. Portable API matches the docs' names; GBA-only API goes in `gba.h` with a `gba_` prefix.
- **Games must never need a dependency directly.** Public headers include no third-party headers, and no public name may collide with libtonc's (check with `grep` in `third_party/libtonc/include`). Examples use only Serval API; when an example needs something, add engine API for it. Enforced by `serval_api_only_check` (host build; globs `examples/*/*.c`), `serval_header_check` and `serval_gba_header_check` (each public header compiled on its own) and `tests/rom/compat_*.c`.
- New tests: shared suites in `tests/*.c` (register in both `tests/host/main.c` and `tests/rom/main.c`, and list the file in `serval_test_harness` in `tests/CMakeLists.txt`), hardware suites in `tests/rom/` (registered in `tests/rom/main.c`, listed in `serval_tests`' sources). Verify a new test can fail.
- Examples can split their game into other files (bunnymark: `bunnymark.c` holds the game, `bench.c` the benchmark harness), keeping `main.c` a clean, readable entry point. An example's logic can be a Lua script (fireflies: `fireflies.lua`), built by `serval_add_script()` after its `serval_add_rom()` in `examples/CMakeLists.txt`; add the same `serval_add_script()` for `serval_api_only_check` in `tests/CMakeLists.txt`, or the C that includes the generated `<name>_script.h` won't compile there. The site shows `.lua` sources with the C.
- Every example's `main.c` opens with a comment covering **what it demonstrates** (the engine features it exercises) and **what to expect when booting the ROM** (what appears on screen, what each button does, sound or its absence), so anyone demoing it knows what correct looks like. Keep it in sync when the example changes.
- Scripts are built at build time by `serval_add_script()` (`cmake/Serval.cmake`): a `.lua` script in the Lua subset (`docs/lua.md`; `examples/fireflies/fireflies.lua`, `tests/consumer/consumer.lua`) is compiled by `tools/svlua.py` to a listing, and listings (`.svm`, syntax in `docs/vm.md#tools`; `tests/svm/fireflies.svm` is the hand-written one) are assembled by `tools/svm.py`. The assembler reads every number from `include/serval/vm.h` at run time, so a new opcode needs a row in its operand table (it refuses to run otherwise) and a case in `tools/svm_test.py` (CTest `svm_tool`, host preset). A new property (`VM_P_*`, appended) needs its row in `vm.c`'s `prop_component`, `get_prop` and `set_prop` (and `READ_ONLY_PROPS` if scripts can't write it; a static assertion counts them), its Lua name, starting with one of `svlua.py`'s `RESERVED_PREFIXES` (normally the C pool's name; `docs/lua.md#reserved-names`), and type in `svlua.py`'s `PROPERTIES` (and `READ_ONLY`), and its place in what the difftest compares: `svlua_runner`'s entity line, `stub.lua`'s `PROPS` and `STORE`, and `PROPS` in `svlua_test.py` and `svlua_difftest.py`. A compiler change needs tests in `tools/svlua_test.py` (CTest `svlua_tool`; it runs compiled programs on the VM with `svlua_runner`, `tests/svlua/runner.c`) and, for anything a script can observe, a program in `tests/svlua/diff/` that `tools/svlua_difftest.py` runs under real Lua (`tests/svlua/stub.lua`) and on the VM (CTest `svlua_difftest`, run when `SERVAL_LUA32` names a Lua 5.4 built with `LUA_32BITS`).
- New example: `examples/<name>/main.c`, registered in `examples/CMakeLists.txt` as `serval_add_rom(<name> ...)` (target name = directory name, which `examples/build-all.sh` relies on) plus `serval_add_rom_checks(<name>)`, and an entry in `examples/gallery.toml` for the website's gallery (the site's build fails without one; `docs/development.md#documentation-site`).
- ROMs: `serval_add_rom()` in `cmake/Serval.cmake`; it must keep working when called from a game's own project (`tests/consumer/`, checked against the release zip by `tools/check-consumer.sh`). `<rom>_rom_checks` ctests (`tools/check-rom.py`) fail if a ROM is unpadded, has a bad header, links `libc.a` or contains BLX (ARMv4T has no BLX). Its `TITLE` (1-12 printable ASCII characters) and `GAME_CODE` (exactly 4) rules are Studio Advance's, checked at configure time (CTest `rom_fields_configure`) and by `tools/gbafix.py` (`gbafix_tool`); the values must reach the header and the web page unchanged for every printable character (the `serval_header_*` ROMs, `tools/check-consumer-web.sh`), so they travel as hex (`--title-hex`) and are escaped into the page (`cmake/ServalWeb.cmake`), never as raw text.
- API misuse must never fail silently: report it with `SERVAL_WARN` (`src/core/warn.h`, debug builds only; once per problem, not per frame) and fail safely. Keep warning text actionable for game developers.
- Never modify files in `third_party/libtonc/`; see its `VENDORED.md`.
- Performance work is measured with `tools/bench.sh` (bunnymark, 128 bunnies, CPU cycles per frame; deterministic). Run it before and after, put both numbers in the commit message, and add a row to the table in `docs/development.md#benchmark`.
- Release: bump `version` in `serval.json`, tag `vX.Y.Z` → draft release; publish it on GitHub. See `docs/development.md`.

## Repository relationships

- `../Studio-Advance` is the closed-source Qt editor that depends on this engine. Dependency direction is one-way: **this repo must never depend on, reference code from, or describe proprietary internals of Studio Advance** (e.g. its packing/dedupe/palette algorithms, legal strategy, or business plans). It is fine to mention Studio Advance as the commercial editor.
- Versioned independently of the editor; each game project pins an engine release, downloaded from GitHub Releases. Breaking changes to the API, data formats, bytecode or debug link require a major version bump. See `docs/releases.md`.
- The engine defines the data formats (`SpriteAsset`, `SpriteGroup`, `SpritePiece`, `Tileset`, `Metatile`, `MapLayer`, `PsgSound`, `PsgSong`, `Path`, bytecode) that the editor's build pipeline emits. Changes to these formats are cross-repo changes and breaking for existing projects.

## Hard constraints

- Target: ARM7TDMI at 16.78 MHz, no FPU, no hardware divider, no data cache. Use fixed-point math and lookup tables.
- No malloc and no garbage collection at runtime. Fixed pools only (128 entities).
- Hot data and per-frame system loops go in IWRAM as ARM code (`SERVAL_IWRAM_CODE`, `SERVAL_IWRAM_DATA` in `platform.h`); large buffers in EWRAM (`SERVAL_EWRAM_BSS`). IWRAM is 32 KB shared with games (and the stack): a ROM uses about 9 KB with hello's features and 16-22 KB for the bigger examples (physics, rotation, depth sorting, maps; table in `docs/development.md#memory-use`; unused engine code is dropped at link time; check `arm-none-eabi-size -A` on the release `.elf`: `.iwram` + `.bss`), so add IWRAM code only where bunnymark shows it pays.
- Keep rarely-used paths out of hot loops (e.g. the rotated-sprite path is out of line): unrotated sprites must not pay for rotation. Check the benchmark.
- Prefer build-time precomputation over runtime work.
- VRAM, palette RAM and OAM writes happen in VBlank or forced blank.
- VM opcodes must stay platform-neutral (no hardware addresses, no hard 32-bit dependency).
- Every ROM-linked dependency must keep games free of license obligations beyond light attribution (MIT-compatible; the engine itself is MIT). Never include libtonc's `tonc_libgba.h` (LGPL), never use devkitARM's crt0/linker script (MPL / no license, only a public-domain dedication), keep newlib out of the link. See `docs/licensing.md`; keep `third_party/licenses/` in sync with dependency updates.
- Never add Nintendo-owned material (BIOS, header logo bitmap).

## Style

Flat, raylib-style C API: plain functions, no hidden objects, `snake_case`, `u8`/`u16`/`u32`/`FIXED` types (identical to libtonc's). Hand-written assets use designated initializers and rely on defaults (`.size` and `.tiles` are enough for a sprite).

## Hardware quirks learned the hard way

- Square-channel frequency bits and `BLDY` are write-only: never read them back (the PSG module keeps the last written rate; tests use `serval_psg_rate`).
- mGBA 0.8.x treats ROMs ≤ 256 KiB whose startup code loads an EWRAM address as multiboot: `gbafix.py` pads ROMs to 512 KiB.
- `WAITCNT` must be set (3/1 + prefetch, done in `serval_init`), or all ROM code runs ~40% slower.
- GCC turns loops into `memcpy`/`strlen` calls; `src/gba/libc.c` provides them and is linked as an object so archive order can't break it.

## Verifying visuals and sound

Headless checks can't see the screen. A capture tool built on mGBA's core (runs a ROM with scripted buttons, saves frames as images, records audio to WAV) was used during development; it is not in the repository yet. If available (`~/opt/mgba-capture/capture ROM FRAMES OUT_PREFIX [shot=N]... [key=FRAME:KEYS:LENGTH]... [nosave]`, see its README), check new examples' screens and sound with it, and describe in the example's header comment what correct looks like. It loads and writes `ROM.sav` next to the ROM, as mGBA does (copy the ROM to a scratch directory to keep runs apart, or pass `nosave` for blank save memory); `CAPTURE_LOG=1` prints mGBA's log (`serval:` warnings, save type detection). `tools/web-shots.py` does the same for web pages, with the same frame numbering and button letters.
