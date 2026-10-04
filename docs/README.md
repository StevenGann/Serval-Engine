# Serval Engine documentation

Design documents for the runtime. Code does not exist yet; these documents are the source of truth until it does, and should be updated as decisions are made.

| Document | Contents |
| --- | --- |
| [overview.md](overview.md) | Goals, scope, layers, relationship to Studio Advance |
| [core-api.md](core-api.md) | raylib-style C API and the sprite submission model |
| [ecs.md](ecs.md) | Entity storage, components, systems, handles |
| [vm.md](vm.md) | Object/event model and the bytecode VM |
| [frame-loop.md](frame-loop.md) | Per-frame update order and the VBlank flush |
| [sprites.md](sprites.md) | Sprite groups, VRAM residency, palette management, ROM data |
| [tilemaps.md](tilemaps.md) | Backgrounds, metatiles, streaming, collision |
| [audio.md](audio.md) | Maxmod integration, SFX/music API, PSG channels |
| [runtime-systems.md](runtime-systems.md) | Save data, text, effects, math, entity collision, camera |
| [debug-link.md](debug-link.md) | Runtime side of the editor/emulator debug protocol |
| [platforms.md](platforms.md) | Portability rules and future targets (GB/GBC, DS) |
| [releases.md](releases.md) | Versioning, the `serval.json` manifest, release contents |
| [licensing.md](licensing.md) | Engine license, third-party licenses and what games must ship |
| [open-questions.md](open-questions.md) | Undecided items that block or shape implementation |
