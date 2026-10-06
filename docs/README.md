# Serval Engine documentation

Design documents and reference for the runtime. They are the source of truth for design and should be updated as decisions are made. Each document opens with a **Status** line saying what is implemented and what is planned.

- **Writing a game:** start with [getting-started.md](getting-started.md), then [api-reference.md](api-reference.md).
- **Working on the engine:** start with [development.md](development.md).

| Document | Contents | Status |
| --- | --- | --- |
| [getting-started.md](getting-started.md) | Build the examples (with a reading order), create a game, sprites, entities, sound, text, web builds | Implemented features only |
| [api-reference.md](api-reference.md) | Every public function and type, by header: units, limits, misuse behaviour | Implemented |
| [examples-roadmap.md](examples-roadmap.md) | Implemented and candidate examples, what each exposed or would expose, and the open gaps | Five implemented with findings; the rest candidates |
| [development.md](development.md) | Building, testing, source layout, benchmark, memory use, code style, CI/CD and releases | Current |
| [overview.md](overview.md) | Goals, scope, layers, relationship to Studio Advance | Design; core API, ECS and the VM's runtime implemented |
| [core-api.md](core-api.md) | raylib-style C API by header, debug warnings, hardware the engine uses, the sprite submission model | Implemented; Maxmod and palette management planned |
| [ecs.md](ecs.md) | Entity storage, components, systems, handles, cheap iteration | Implemented; collision events planned |
| [sprites.md](sprites.md) | Sprite groups, VRAM residency, palettes, ROM data, animation | Resident groups, metasprites, animation, per-draw palettes implemented; streaming, palette sharing planned |
| [tilemaps.md](tilemaps.md) | Tilesets, metatile layers, streaming, fixed and self-scrolling layers, map collision | Implemented; tileset groups, LZ77, slopes and ladders, raster effects planned |
| [audio.md](audio.md) | PSG sound effects and music; Maxmod music and SFX API | PSG implemented; Maxmod planned |
| [frame-loop.md](frame-loop.md) | What `frame_begin`/`frame_end` do, the VBlank flush, the update order with scripts | Implemented, order with scripts confirmed; parts of the VBlank flush planned |
| [runtime-systems.md](runtime-systems.md) | Save data, text, effects, math, paths, physics, entity collision, camera | Mostly implemented, per section; dialogue, blending, broad phase planned |
| [vm.md](vm.md) | Object/event model and the bytecode VM: blob format, opcode set, scheduling, test plan and milestones | Format v1, interpreter, scheduler, engine bridge and proof example (`fireflies`) implemented; debug link planned |
| [debug-link.md](debug-link.md) | Runtime side of the editor/emulator debug protocol | Planned |
| [platforms.md](platforms.md) | Portability rules, the web target, future targets (GB/GBC, DS) | GBA and web implemented; GB/GBC, DS planned |
| [releases.md](releases.md) | Versioning, the `serval.json` manifest, release contents | Workflow ready, no release yet |
| [licensing.md](licensing.md) | Engine license, third-party licenses and what games must ship | Current |
| [open-questions.md](open-questions.md) | Decisions made and still open, and API caveats found by the examples | Living list |
