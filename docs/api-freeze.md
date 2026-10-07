# The 1.0 API freeze

**Status:** frozen in 1.0.0-rc.1 (decided 2026-10-07). This document records what the freeze covers, how each unfinished feature was classified, and why, so that later work can extend the API without breaking games. The mechanics of planned API are in [releases.md](releases.md#planned-api) (the policy) and [development.md](development.md#planned-api) (how to add, test and implement a planned name); the hardware the engine claims is in [core-api.md](core-api.md#hardware-the-engine-uses).

## What 1.0 promises

1.0.0 freezes the public C API (`include/serval/`), the ROM data formats, the VM blob format, the `serval.json` manifest, the CMake functions, and the command lines and listing syntax of `tools/svm.py` and `tools/svlua.py`. From 1.0.0 on, changing what any existing name, field, flag bit, value or zero default means is a major version ([releases.md](releases.md#versioning)). Adding is a minor version.

1.0.0-rc.1 is the release candidate: Studio Advance integrates against it, and what integration finds is fixed before 1.0.0. Breaking changes are still possible between rc.1 and 1.0.0, but each must be justified by that integration and recorded in the release notes.

The API is **complete** in 1.0, not finished: features the 1.x line will implement are declared now as *planned* API ([releases.md](releases.md#planned-api)). They compile with a warning at every use, do nothing harmful at run time, and are implemented in minor versions without changing their signatures.

## Principles

1. **A planned (declared) feature is a promise made before any implementation has tested it.** Its signature freezes with 1.0, and changing it later costs a major version. So a feature is declared only when its shape is clear *and* Studio Advance needs to target it now (asset import, event blocks, collision painting). Anything that can be *added* later without changing an existing name, value or default is left out of the API until it is implemented: it can arrive in any 1.x minor.
2. **What cannot be added later without breaking is done now**, along with a few implementations so small (under about a day, portable, host-testable) that a stub would be silly.
3. **Data formats grow by fields whose zero keeps today's behaviour.** Studio Advance writes ROM data with designated initializers ([sprites.md](sprites.md#rom-data-format)), so a new field is source-compatible. A new meaning for a value is compatible only if today's engine refuses that value. Therefore **every loader refuses values it does not understand**: otherwise data with a stray bit, accepted today, would change meaning the day that bit is assigned.
4. **Hardware and bit claims are part of the API.** Claiming a timer, a DMA channel, an interrupt or a flag bit in 1.3 would break a 1.0 game that used it, so everything the planned features need is reserved now ([core-api.md](core-api.md#hardware-the-engine-uses)).

## What the freeze did (1.0.0-rc.1)

These could not be added later without breaking games, or were small enough to finish:

| Item | Where |
| --- | --- |
| The planned-API mechanism: `SERVAL_PLANNED`, `SERVAL_NO_PLANNED_WARNINGS`, `tools/check-planned.py` and the `planned_api` test, the stub test suites | `platform.h`, [releases.md](releases.md#planned-api), [development.md](development.md#planned-api) |
| Loaders refuse unknown values: `SpriteAsset.flags`, `SpriteGroup.flags` and `.slots`, `SpritePiece.flags`, `MapLayer.flags`, `Tileset.flags`; `map_load` warns about collision types it does not implement yet (they load, colliding as described); `entity_create` leaves out the reserved engine component bits 7-15, with a warning | [sprites.md](sprites.md#rom-data-format), [tilemaps.md](tilemaps.md#collision-types), [ecs.md](ecs.md#component-bits) |
| `Tileset.flags`, a new byte in existing padding (LZ77 and 8bpp need a field) | [tilemaps.md](tilemaps.md#tilesets) |
| `SPRITE_ASSET_STREAMED` removed: residency is a group's (`SpriteGroup.flags`); bit 0 of `SpriteAsset.flags` is reserved | [sprites.md](sprites.md#residency-modes) |
| Hardware, flag bits, collision types and VRAM reserved for planned features | [core-api.md](core-api.md#hardware-the-engine-uses) and each area's doc |
| `sprite_groups_mark()` / `sprite_groups_release()`: global and per-room sprite groups | [sprites.md](sprites.md#marks) |
| `color_mix()`, and the `LAYER_*` constants that `screen_set_blend()` will take | [runtime-systems.md](runtime-systems.md#color-mixing) |
| `map_tags_in()`: hazards are game tag bits, not a collision type | [tilemaps.md](tilemaps.md#tags) |
| `vm_collide()` / `vm_collide_clear()`: scripted games collide with no C glue | [vm.md](vm.md#collisions) |
| `body_bounce` 255 means a perfect bounce in both `sys_physics()` and `sys_map_movement()`: a body comes back up as high as it fell from (it kept 255/256 of its speed before) | [ecs.md](ecs.md#bodies), [runtime-systems.md](runtime-systems.md#physics) |
| VM and Lua names equal the C names: `VM_SYS_PSG_MUSIC_*`, `VM_SYS_SCREEN_SET_BRIGHTNESS`, `VmBindings.psg_songs`, Lua's `psg_music_play`, `screen_set_brightness`, `text_print_number` and the rest; numbers and golden bytes unchanged | [vm.md](vm.md#engine-calls), [lua.md](lua.md) |

## Planned in 1.x (declared now)

Each of these 37 names is in the headers with `SERVAL_PLANNED`; its header comment says what it will do and what it does today. `tests/planned/` uses every one, and the `planned_api` test checks that each warns.

| Feature | Names | Doc |
| --- | --- | --- |
| Sound bank | `audio_bank_set` | [audio.md](audio.md#sound-bank) |
| Tracker music | `music_play`, `music_stop`, `music_playing`, `music_pause`, `music_resume`, `music_paused`, `music_set_volume`, `music_set_speed` | [audio.md](audio.md#tracker-music) |
| Sampled sound effects | `sfx_play`, `sfx_play_ex`, `sfx_stop`, `sfx_playing`, `sfx_stop_all`, `sfx_set_volume` (the handle type `Sfx` and `SFX_NONE` are ordinary declarations) | [audio.md](audio.md#sampled-sound-effects) |
| PSG wave channel | `PSG_WAVE`, `psg_waves_set` | [audio.md](audio.md#wave-channel) |
| Streamed sprite groups | `SPRITE_GROUP_STREAMED` (with the field `SpriteGroup.slots`) | [sprites.md](sprites.md#residency-modes) |
| LZ77 sprites and tilesets | `SPRITE_ASSET_LZ77`, `TILESET_LZ77` | [sprites.md](sprites.md#lz77-compression), [tilemaps.md](tilemaps.md#tilesets) |
| Runtime sprite tiles | `sprite_set_tiles`, `SPRITE_MAX_TILE_UPDATES` | [sprites.md](sprites.md#runtime-tiles) |
| Palette writes | `sprite_set_colors`, `tileset_set_colors` | [sprites.md](sprites.md#palettes), [tilemaps.md](tilemaps.md#palette-writes) |
| Ladders | `MAP_LADDER`, `MAP_CONTACT_LADDER` | [tilemaps.md](tilemaps.md#collision-types) |
| Floor slopes | `MAP_SLOPE_R`, `MAP_SLOPE_L`, `MAP_SLOPE_R_LOW`, `MAP_SLOPE_R_HIGH`, `MAP_SLOPE_L_HIGH`, `MAP_SLOPE_L_LOW` | [tilemaps.md](tilemaps.md#collision-types) |
| Alpha blending | `screen_set_blend`, `SPRITE_BLEND` | [runtime-systems.md](runtime-systems.md#alpha-blending), [sprites.md](sprites.md#alpha-blending) |
| Raster effects | `raster_scroll`, `raster_backdrop`, `raster_clear` | [runtime-systems.md](runtime-systems.md#raster-effects) |

**Suggested order**, by value to Studio Advance: palette writes; runtime sprite tiles; blending; ladders and slopes; LZ77; Maxmod on the GBA (with the mixer configuration below); streamed groups; raster effects; the wave channel; the web's sampled-audio player. To implement one, follow [development.md](development.md#planned-api): drop the marker, implement, move its line out of `tests/planned/`, and turn its stub test into a feature test.

## Later, additively (no API now)

Each of these can be added in a 1.x minor without changing anything that exists: a new function or header, a flag bit that loaders refuse today, or a field whose zero means today's behaviour.

| Feature | Why adding it later can't break games |
| --- | --- |
| Mixer configuration (rate, channels) and a CMake `serval_add_soundbank()` | New CMake keywords and functions; the planned audio API works with defaults |
| Sampled audio on the web | Implementation behind the planned API; until then the web plays nothing and warns once |
| Jingles, module position and sync events, changing a playing effect; streamed PCM, interactive music | New functions |
| Palette sharing (reference-counted banks) | Internal: palette writes are already copy-on-write |
| Palette cycle and fade helpers | New functions over `sprite_set_colors`, `tileset_set_colors` and `color_mix` |
| VRAM defragmentation, loading in forced blank | Internal |
| A plain affine mode for small angles; more than 32 affine matrices | Automatic, a reserved draw-flag bit, or an asset field with a zero default |
| 8bpp tilesets | `Tileset.flags` bit 1, reserved and refused |
| Tileset groups (several tilesets per room) | New `Tileset` fields whose zero means today's layout |
| More than `MAP_MAX_CHANGES` | A writable-layer flag in a refused `MapLayer.flags` bit; the constant can also grow |
| Ceiling slopes and other collision types | Types 10-15, reserved |
| Windows, the object window, mosaic | New `screen_*` functions; draw-flag bits 13 and 14 reserved |
| Affine (Mode 7) backgrounds | New API |
| Swept body-against-body tests, body-to-body response, moving platforms, a platformer controller | New functions, flags or systems |
| A collision broad phase | Internal to `vm_collide`, or a new public grid |
| Tweens, easing, springs; camera following | New header or functions |
| Path extras (per-step events, absolute headings, aiming from a script) | A `PathStep` flag in its padding (zero = today), and a new SYS call |
| Variable-width and large fonts, dialogue boxes, menus, localization | New headers; charblock 0 and BG0 are already the text layer's |
| Input recording, remapping, touch; `button_released()` | New functions or header |
| `ent_add()` / `ent_remove()` helpers | Inline wrappers; writing `ent_mask` directly stays legal forever |
| More than 15 game component bits | A second tag word with its own queries and VM property |
| `entity_destroy()` detaching VM bindings | Turns today's "stale binding" warning into the right behaviour |
| VM SYS calls and properties for new features; more VM events | The SYS and property pages are append-only; VM header flag bit 1 is reserved for an extended handler table |
| The debug link | No game-facing API; its protocol is versioned by its own handshake |
| GB/GBC and DS targets, link cable | New targets and headers, same API |

## Decisions

- **D1. Version 1.0.0, through 1.0.0-rc.1.** Under semver, 0.x promises nothing, which contradicts the compatibility promise in releases.md and the editor's pin-per-project model. The freeze is what 1.0 means; planned features land in 1.1, 1.2 and so on.
- **D2. Raster effects are declared as planned API** (the owner's decision, overriding a recommendation to leave them out of 1.0 until implemented). The shape is deliberately small: one table-driven effect at a time (a value per scanline, by HBlank DMA), which suits both the GBA and a web renderer with per-line state. DMA 0 and the HBlank interrupt are reserved for it. More effects, or several at once, can be added later as new functions.
- **D3. Maxmod comes from BlocksDS.** It is maintained, ISC-licensed, needs no devkitARM, and is the ecosystem the DS target already plans on ([platforms.md](platforms.md)). The audio API does not depend on the choice: the sound bank is opaque and its IDs are the bank's indices. The fork fixes only the bank format (produced by BlocksDS's `mmutil`; `serval.json` gains an `mmutil` version when Maxmod lands) and whether the web can run the same player ([audio.md](audio.md)).
- **D4. The small implementations were done** (`sprite_groups_mark`/`release`, `color_mix`, `map_tags_in`, `vm_collide`) rather than declared as planned.
- **D5. `body_bounce` 255 means a perfect bounce.** It closes the "at most 255/256" gap with no new type and no extra IWRAM; widening the pool to `u16` would cost 128 bytes of IWRAM and touch the hot loops for one value.
- **D6. VM and Lua names equal the C names.** `music_*` is tracker music in C; had the VM kept `VM_SYS_MUSIC_PLAY` and Lua `music_play` for the PSG player, the names would disagree forever.
- **D7. Sampled audio on the web starts as a silent stub.** The GBA implementation ships without waiting for a web player; the web keeps the same API, plays nothing and warns once until it has one.
- Also decided: streaming is per group only; hazards are tags; writing `ent_mask` stays legal forever; palette writes are copy-on-write against future sharing; blending pauses while the screen brightness is not 0 (the hardware has one color effect); volume ranges differ between the PSG (0-15, the hardware's steps) and the mixer (0-255).

## Caveats that remain

- `vm_reload` halts behaviours, so an entity whose life is one long Create stands still after a reload. To be settled with the debug link ([vm.md](vm.md)); it changes behaviour only under the debugger.
- `entity_destroy` on an entity with a VM binding leaves a stale binding, which the VM catches and warns about (see the "later" table).
- The compiler's wording of the planned warning says "deprecated"; the message text says "planned, not implemented".
- `SERVAL_NO_PLANNED_WARNINGS` must be defined before the first Serval include.
