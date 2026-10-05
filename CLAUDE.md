# CLAUDE.md

Serval Engine: the open-source GBA game runtime (C on libtonc) that every Studio Advance game links. Public repository.

## Status

Pre-alpha: build system, startup code, frame loop, input, shadow OAM and ECS core, with tests and CI/CD. `docs/` is the source of truth for design; start with `docs/README.md` and `docs/development.md`. When a design decision is made, update the relevant doc and tick it off in `docs/open-questions.md`.

## Commands

```sh
export ARM_GNU_TOOLCHAIN=~/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi MGBA_ROM_TEST_DIR=~/opt/mgba-rom-test
cmake --preset gba-ci && cmake --build --preset gba-ci && ctest --preset gba-ci   # GBA build + test ROM in mGBA
cmake --preset host && cmake --build --preset host && ctest --preset host         # host tests, ASan/UBSan
git ls-files '*.c' '*.h' ':!:third_party/**' | xargs clang-format -i              # clang-format 18
```

## Code layout and conventions

- `src/ecs/` etc. → `serval_portable`: must compile on the host (no libtonc calls, no hardware access); unit tested natively and in the test ROM. GBA-only code goes in `src/gba/` → `serval`.
- Public headers in `include/serval/`. Portable API matches the docs' names; GBA-only API goes in `gba.h` with a `gba_` prefix.
- **Games must never need a dependency directly.** Public headers include no third-party headers, and no public name may collide with libtonc's (check with `grep` in `third_party/libtonc/include`). Examples use only Serval API; when an example needs something, add engine API for it. Enforced by `serval_api_only_check` (host build) and `tests/rom/compat_*.c`.
- New tests: shared suites in `tests/*.c` (register in both `tests/host/main.c` and `tests/rom/main.c`), hardware suites in `tests/rom/`. Verify a new test can fail.
- Every example's `main.c` opens with a comment covering **what it demonstrates** (the engine features it exercises) and **what to expect when booting the ROM** (what appears on screen, what each button does, sound or its absence), so anyone demoing it knows what correct looks like. Keep it in sync when the example changes.
- New example: `examples/<name>/main.c`, registered in `examples/CMakeLists.txt` as `serval_add_rom(<name> ...)` (target name = directory name, which `examples/build-all.sh` relies on) and added to `serval_api_only_check` in `tests/CMakeLists.txt`.
- ROMs: `serval_add_rom()` in `cmake/Serval.cmake`. After changing link flags, check the `.map` for `libc.a` (must not appear) and `objdump -d | grep blx` (must be empty: ARMv4T has no BLX).
- API misuse must never fail silently: report it with `SERVAL_WARN` (`src/core/warn.h`, debug builds only; once per problem, not per frame) and fail safely. Keep warning text actionable for game developers.
- Never modify files in `third_party/libtonc/`; see its `VENDORED.md`.
- Performance work is measured with `tools/bench.sh` (bunnymark, 128 bunnies, CPU cycles per frame; deterministic). Run it before and after, put both numbers in the commit message, and add a row to the table in `docs/development.md#benchmark`.
- Release: bump `version` in `serval.json`, tag `vX.Y.Z` → draft release; publish it on GitHub. See `docs/development.md`.

## Repository relationships

- `../Studio-Advance` is the closed-source Qt editor that depends on this engine. Dependency direction is one-way: **this repo must never depend on, reference code from, or describe proprietary internals of Studio Advance** (e.g. its packing/dedupe/palette algorithms, legal strategy, or business plans). It is fine to mention Studio Advance as the commercial editor.
- Versioned independently of the editor; each game project pins an engine release, downloaded from GitHub Releases. Breaking changes to the API, data formats, bytecode or debug link require a major version bump. See `docs/releases.md`.
- The engine defines the data formats (`SpriteAsset`, `SpriteGroup`, `MapLayer`, bytecode) that the editor's build pipeline emits. Changes to these formats are cross-repo changes and breaking for existing projects.

## Hard constraints

- Target: ARM7TDMI at 16.78 MHz, no FPU, no hardware divider, no data cache. Use fixed-point math and lookup tables.
- No malloc and no garbage collection at runtime. Fixed pools only (128 entities).
- Hot data and per-frame system loops go in IWRAM (`IWRAM_DATA`, `IWRAM_CODE`, ARM mode).
- Prefer build-time precomputation over runtime work.
- VRAM, palette RAM and OAM writes happen in VBlank or forced blank.
- VM opcodes must stay platform-neutral (no hardware addresses, no hard 32-bit dependency).
- Every ROM-linked dependency must keep games free of license obligations beyond light attribution (MIT-compatible; the engine itself is MIT). Never include libtonc's `tonc_libgba.h` (LGPL), never use devkitARM's crt0/linker script (MPL / unlicensed), keep newlib out of the link. See `docs/licensing.md`; keep `third_party/licenses/` in sync with dependency updates.
- Never add Nintendo-owned material (BIOS, header logo bitmap).

## Style

Flat, raylib-style C API: plain functions, no hidden objects, `snake_case`, libtonc types (`u8`, `u16`, `FIXED`, …).
