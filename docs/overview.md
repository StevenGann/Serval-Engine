# Overview

## Purpose

Serval Engine is the GBA-side runtime for Studio Advance, a cross-platform editor for building Game Boy Advance games without writing code. Every ROM the editor exports links this engine. Advanced users can write C against the same API directly.

The engine is open source; the editor is a separate, closed-source product. See [Relationship to Studio Advance](#relationship-to-studio-advance).

## Design inspirations

- **raylib:** a flat, simple C API with no hidden objects.
- **ECS:** data-oriented entity storage in fixed pools.
- **GameMaker:** objects with events, rooms, and grouped assets.

## Layers

```mermaid
flowchart TD
    L3[Objects + events<br/>bytecode VM]
    L2[ECS world]
    L1[raylib-style core API]
    L0[libtonc]
    L3 --> L2 --> L1 --> L0
```

| Layer | Borrowed from | Role | Doc |
| --- | --- | --- | --- |
| Core API | raylib | Flat C calls over libtonc: input, drawing, sound | [core-api.md](core-api.md) |
| World | ECS | Fixed-pool entity storage and per-frame bulk processing | [ecs.md](ecs.md) |
| Game logic | GameMaker | Objects with events, executed by the bytecode VM | [vm.md](vm.md) |

## Guiding principle: precompute everything

The tooling does the heavy lifting (sprite packing, tile deduplication, palette assignment, script compilation) so the runtime only executes lean, precomputed data read directly from memory-mapped ROM. When a choice exists between runtime work and build-time work, prefer build time.

## 1.0 scope

In scope: regular tiled backgrounds, sprites (resident and streamed), the ECS, the bytecode VM, Maxmod audio plus PSG SFX, save data, text, special effects, camera, and the debug link hooks.

**Not in 1.0:** Mode 7 / affine backgrounds, GB/GBC and DS targets, streamed PCM audio, link cable multiplayer. Raster effects are a 1.0 candidate if time allows.

## Relationship to Studio Advance

- Studio Advance (closed source, sibling repo `../Studio-Advance`) depends on this engine. This engine must never depend on Studio Advance.
- Studio Advance's build pipeline emits generated C (constant asset tables and bytecode) that conforms to the data formats defined here, then compiles it together with the engine.
- The editor's packing, deduplication and palette algorithms are not part of this repository. The engine only defines the formats they produce.
- The debug link protocol is a shared contract: this repo defines the runtime side ([debug-link.md](debug-link.md)).
