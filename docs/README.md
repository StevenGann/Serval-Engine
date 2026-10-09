# Serval Engine documentation

Design documents and reference for the runtime. They are the source of truth for design and should be updated as decisions are made. Each document opens with a **Status** line saying what is implemented, what is declared as *planned* API (in the headers since 1.0.0-rc.1, implemented in a 1.x version; [releases.md](releases.md#planned-api)) and what comes later.

- **Taking over the engine:** start with [handoff.md](handoff.md), then [api-freeze.md](api-freeze.md).
- **Writing a game:** start with [getting-started.md](getting-started.md), then [api-reference.md](api-reference.md).
- **Working on the engine:** start with [development.md](development.md).

| Document | Contents | Status |
| --- | --- | --- |
| [handoff.md](handoff.md) | Start here if you are taking over the engine: where things are, their state, what is promised, what to do next | Written for 1.0.0-rc.1 |
| [api-freeze.md](api-freeze.md) | What 1.0 promises, what is planned and why: the freeze, every planned name, what can come later without breaking games, the decisions | Frozen in 1.0.0-rc.1 |
| [getting-started.md](getting-started.md) | Build the examples (with a reading order), create a game, scripting, sprites, entities, sound, text, web builds | Implemented features only |
| [api-reference.md](api-reference.md) | Every public function and type, by header: units, limits, misuse behaviour | Implemented; planned names marked |
| [examples-roadmap.md](examples-roadmap.md) | Implemented and candidate examples, what each exposed or would expose, and the open gaps | Ten examples, six write-ups of what they exposed; the rest candidates; gaps the freeze closed or declared marked |
| [development.md](development.md) | Building, testing, source layout, benchmark, memory use, code style, planned API, the documentation site, CI/CD and releases | Current |
| [overview.md](overview.md) | Goals, layers, what 1.0 means, relationship to Studio Advance | Design; core API, ECS and the VM implemented; 1.0's planned API declared |
| [core-api.md](core-api.md) | raylib-style C API by header, debug warnings, hardware the engine uses and reserves, reserved bits and values in the data formats, the sprite submission model | Implemented, palette writes and raster effects included; tracker music and sampled sound planned (declared) |
| [ecs.md](ecs.md) | Entity storage, components, systems, handles, cheap iteration, bodies | Implemented, collision events to scripts included (`vm_collide`); engine component bits 8-15 reserved; helpers and more components later |
| [sprites.md](sprites.md) | Sprite groups, VRAM residency, palettes, ROM data, animation | Resident and streamed groups, metasprites, animation, per-draw palettes, marks, runtime tiles, blending and palette writes implemented; LZ77 planned (declared); palette sharing later |
| [tilemaps.md](tilemaps.md) | Tilesets, metatile layers, streaming, fixed and self-scrolling layers, map collision and tags | Implemented, palette writes included; LZ77 tilesets, ladders and slopes planned (declared); tileset groups and 8bpp later |
| [audio.md](audio.md) | PSG sound effects and music; the wave channel, the sound bank, tracker music and sampled sound through Maxmod (BlocksDS) | PSG implemented; wave channel, sound bank, tracker music and sampled sound planned (declared) |
| [frame-loop.md](frame-loop.md) | What `frame_begin`/`frame_end` do, the VBlank flush in order, the update order with scripts | Implemented, order with scripts confirmed; the planned parts of the VBlank flush listed in place |
| [runtime-systems.md](runtime-systems.md) | Save data, text, effects, math, paths, physics, entity collision, camera | Mostly implemented, per section (blending and raster effects included); dialogue, a broad phase and camera following later |
| [vm.md](vm.md) | Object/event model and the bytecode VM: blob format, opcode set, scheduling, collisions, test plan and milestones | Format v1, interpreter, scheduler, engine bridge, collision pass (`vm_collide`), the revision for compiled Lua, proof example (`fireflies`, written in Lua) and the reference assembler and disassembler (`tools/svm.py`) implemented; milestone 8 (debug link, script benchmark) open |
| [lua.md](lua.md) | Game logic in a statically checked subset of Lua 5.4, compiled to the VM: the rule, types, what compiles to what, the tool, testing | Implemented (`tools/svlua.py`; `fireflies` is written in it) |
| [debug-link.md](debug-link.md) | Runtime side of the editor/emulator debug protocol | Planned, no API |
| [platforms.md](platforms.md) | Portability rules, the web target and what it will do for each planned feature, future targets (GB/GBC, DS) | GBA and web implemented; GB/GBC, DS post-1.0 |
| [releases.md](releases.md) | Versioning, planned API, the `serval.json` manifest, release contents | Workflow ready, no release published yet |
| [licensing.md](licensing.md) | Engine license, third-party licenses and what games must ship | Current; Maxmod (BlocksDS) listed, not linked yet |
| [open-questions.md](open-questions.md) | Decisions made (the 1.0 freeze's included) and still open, and API caveats found by the examples | Living list |
