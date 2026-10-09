# Handoff: the engine at 1.0.0-rc.1

**Status:** written for 1.0.0-rc.1 (October 2026), when development of Serval Engine passes to a new team. It is the starting point for that team: where things are, what state they are in, what is promised, and what to do next. Everything decision-relevant is in this repository; nothing depends on conversations or files outside it.

## What this is

Serval Engine is the MIT-licensed C runtime for Game Boy Advance games, built on libtonc, with a web target that runs the same code compiled to WebAssembly. Its main consumer is Studio Advance, a separate, closed-source editor that emits the engine's data formats and bytecode and pins an engine release per game project ([overview.md](overview.md#relationship-to-studio-advance), [releases.md](releases.md)). The engine must never depend on, or describe the internals of, the editor.

## Read first

1. [README.md](../README.md): features and a quick start.
2. [docs/README.md](README.md): the map of the design docs. Each opens with a **Status** line; the docs are the source of truth for design.
3. [development.md](development.md): requirements, building, tests, benchmark, memory use, style, planned API, the site, CI/CD and releases. `tools/setup-dev.sh` installs every tool at CI's versions on Linux.
4. [api-freeze.md](api-freeze.md): what 1.0 promises, what is declared but not implemented, and why.
5. [`CLAUDE.md`](../CLAUDE.md): the conventions and hard constraints in one page (written for AI assistants, and accurate for people).
6. [open-questions.md](open-questions.md): decisions made and still open; [api-freeze.md](api-freeze.md#caveats-that-remain) lists the API caveats that remain.

## State at rc.1

- **Implemented:** see the README's feature list and each doc's status line, and what integrating Studio Advance's object system added before the tag: body properties and kinematic bodies for scripts, `vm_object_of`, a larger event queue, field names reserved for later properties ([api-freeze.md](api-freeze.md#what-the-freeze-did-100-rc1)). Nine examples ([examples-roadmap.md](examples-roadmap.md)), each with a header comment saying what correct looks like; the website's gallery runs them in the browser.
- **Declared, not implemented (planned API):** the table in [api-freeze.md](api-freeze.md#planned-in-1x-declared-now). Each planned name warns at every use and does nothing harmful; `tests/planned/` lists them and the `planned_api` test checks that each warns.
- **Open:** [open-questions.md](open-questions.md)'s unchecked items, notably the VM's milestone 8 (the debug link and `BRK`, a script benchmark, the IWRAM decision; [vm.md](vm.md#milestones)), the debug link protocol ([debug-link.md](debug-link.md)), real-cartridge save testing, and the capture tool for screenshot tests.

## From rc.1 to 1.0.0

1. Studio Advance integrates against 1.0.0-rc.1: it emits the ROM data formats, assembles or compiles scripts, and runs the examples' kinds of games.
2. Fix what integration finds. Breaking changes are still allowed before 1.0.0, but each needs a reason from that integration and a line in the release notes; prefer additive fixes.
3. Tag 1.0.0 ([releases.md](releases.md), [development.md](development.md#cicd)). From then on, the compatibility promise in [releases.md](releases.md#versioning) holds: breaking changes need a major version.

After 1.0.0, implement the planned features one per minor version, in the order [api-freeze.md](api-freeze.md#planned-in-1x-declared-now) suggests, following [development.md](development.md#planned-api).

## What the editor relies on

These are the shared contracts. Changing any of them is a cross-repository change and, after 1.0.0, a major version unless it is additive under the data-format rule ([releases.md](releases.md#versioning)):

- the ROM data formats: `SpriteAsset`, `SpriteGroup`, `SpritePiece`, `Tileset`, `Metatile`, `MapLayer`, `PsgSound`, `PsgSong`, `Path` and their flags ([sprites.md](sprites.md#rom-data-format), [tilemaps.md](tilemaps.md), [audio.md](audio.md), [runtime-systems.md](runtime-systems.md#paths)); the editor writes them with designated initializers;
- the VM blob format, opcodes, SYS calls and properties ([vm.md](vm.md)), which the editor also emits;
- the Lua subset and `tools/svlua.py`'s command line ([lua.md](lua.md)); `tools/svm.py`'s command line and listing syntax ([vm.md](vm.md#tools));
- `serval.json` ([releases.md](releases.md#manifest)) and the release zip's layout ([releases.md](releases.md#release-contents));
- the CMake functions `serval_add_rom()` and `serval_add_script()` (`cmake/Serval.cmake`), checked from outside the tree by `tests/consumer/` and `tools/check-consumer.sh`;
- the debug link, once defined ([debug-link.md](debug-link.md)).

## Things that are easy to get wrong

- **Every loader refuses values it does not understand**, and warns once. A loader that ignored an unknown bit would let a later version change what existing data means ([api-freeze.md](api-freeze.md#principles)).
- **Hardware claims are API.** The engine's timers, DMA channels, interrupts and VRAM are listed in [core-api.md](core-api.md#hardware-the-engine-uses); claiming another one is a breaking change.
- **A new VM property's Lua name starts with a reserved prefix** (`body_`, `spr_`, `path_`, ...; [lua.md](lua.md#reserved-names)), normally its C pool's name. Scripts can't have fields by those names; any other name would take over some script's own field.
- **Performance is measured, not guessed:** `tools/bench.sh` before and after, both numbers in the commit message, a row in [development.md](development.md#benchmark). IWRAM is 32 KB shared with games ([development.md](development.md#memory-use)).
- **The web build compiles `src/gba/` unchanged** onto virtual GBA hardware ([platforms.md](platforms.md#web)); keep GBA code free of inline assembly outside the guarded places.
- **Planned API:** `SERVAL_PLANNED` goes on functions and enumerators only; tests that name planned API on purpose define `SERVAL_NO_PLANNED_WARNINGS` before their first include.
- **Golden bytes:** the VM's worked example in [vm.md](vm.md) and `tools/svm_test.py` pin the blob format; the Lua difftest (`svlua_difftest`) runs each program under real Lua 5.4 with 32-bit integers and on the VM.
- **Licenses:** nothing linked into a ROM may impose more than light attribution on games ([licensing.md](licensing.md)); never ship Nintendo-owned material.
