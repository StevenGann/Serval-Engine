# CLAUDE.md

Serval Engine: the open-source GBA game runtime (C on libtonc) that every Studio Advance game links. Public repository.

## Status

Design phase. There is no code yet; `docs/` is the source of truth. Start with `docs/README.md`. When a design decision is made, update the relevant doc and tick it off in `docs/open-questions.md`.

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
