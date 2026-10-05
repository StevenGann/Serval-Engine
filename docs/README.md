# Serval Engine documentation

Design documents and reference for the runtime. They are the source of truth for design and should be updated as decisions are made. Each document opens with a **Status** line saying what is implemented and what is planned.

- **Writing a game:** start with [getting-started.md](getting-started.md), then [api-reference.md](api-reference.md).
- **Working on the engine:** start with [development.md](development.md).

| Document | Contents | Status |
| --- | --- | --- |
| [getting-started.md](getting-started.md) | Build the examples, create a game, sprites, entities, sound, text | Implemented features only |
| [api-reference.md](api-reference.md) | Every public function and type, by header: units, limits, misuse behaviour | Implemented |
| [examples-roadmap.md](examples-roadmap.md) | Candidate examples: what each would show and what engine gaps it would expose | Plan |
| [development.md](development.md) | Building, testing, source layout, benchmark, memory use, code style, CI/CD and releases | Implemented |
| [overview.md](overview.md) | Goals, scope, layers, relationship to Studio Advance | Design |
| [core-api.md](core-api.md) | raylib-style C API, debug warnings, hardware the engine uses, the sprite submission model | Implemented |
| [ecs.md](ecs.md) | Entity storage, components, systems, handles | Implemented |
| [sprites.md](sprites.md) | Sprite groups, VRAM residency, palette management, ROM data | Resident groups implemented; streaming, palette sharing planned |
| [audio.md](audio.md) | PSG sound effects; Maxmod music and SFX API | PSG implemented; Maxmod planned |
| [frame-loop.md](frame-loop.md) | Per-frame update order and the VBlank flush | Partly implemented |
| [runtime-systems.md](runtime-systems.md) | Save data, text, effects, math, physics, entity collision, camera | Mixed, per section |
| [tilemaps.md](tilemaps.md) | Backgrounds, metatiles, streaming, collision | Partly implemented |
| [vm.md](vm.md) | Object/event model and the bytecode VM | Planned |
| [debug-link.md](debug-link.md) | Runtime side of the editor/emulator debug protocol | Planned |
| [platforms.md](platforms.md) | Portability rules and future targets (GB/GBC, DS) | GBA only |
| [releases.md](releases.md) | Versioning, the `serval.json` manifest, release contents | Workflow ready, no release yet |
| [licensing.md](licensing.md) | Engine license, third-party licenses and what games must ship | Current |
| [open-questions.md](open-questions.md) | Undecided items that block or shape implementation | |
