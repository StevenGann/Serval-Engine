# Other runtime systems

These systems are scoped; several have a first implementation (marked **Implemented so far**), the rest are planned and not designed in detail yet.

## Save data

**Status:** implemented for SRAM (`include/serval/save.h`); Flash and EEPROM cartridges planned. Web builds keep saves in `localStorage` ([platforms.md](platforms.md#web)). Not yet verified on real flash carts.

The GBA has three save types, detected by emulators and flash carts from ID strings in the ROM (`SRAM_V`, `FLASH1M_V`, `EEPROM_V`...). Serval uses 32 KiB battery-backed **SRAM**, the simplest and the one every flash cart supports. Games get numbered slots with checksums and a version number, so saves survive game updates.

### API

`save_write(slot, &data, sizeof data, version)`, `save_read(slot, &data, sizeof data, version)`, `save_slot_version()`, `save_slot_size()`, `save_erase()`: 8 slots (`SAVE_SLOTS`) of up to 2000 bytes (`SAVE_SLOT_MAX`). `save_read()` returns `SAVE_OK`, `SAVE_EMPTY`, `SAVE_CORRUPT` or `SAVE_OTHER_VERSION`, and touches the game's struct only for `SAVE_OK`. The version is the game's: raise it when the saved struct changes, and read old saves with the old struct (`save_slot_version()` says which) to convert them. See [api-reference.md](api-reference.md#saveh).

### Format

The portable code (`src/core/save.c`, host-tested) works on a 32 KiB byte memory supplied by the platform: SRAM on the GBA (`src/gba/save_sram.c`), a buffer kept in `localStorage` on the web (`src/web/save.c`). The layout is the same everywhere, so a web page's save memory is byte for byte an mGBA `.sav` file and vice versa.

Each slot is two 2048-byte **copies**, A and B (slot *s*: copies 2*s* and 2*s*+1, at `copy * 2048`). A copy is a 16-byte header, then the data. Header, little-endian:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `SVS1`: this copy holds a save |
| 4 | 4 | Sequence number: the newer copy has the higher one (compared modulo 2^32, so wrapping is harmless) |
| 8 | 2 | The game's save version |
| 10 | 2 | Data size in bytes, 1 to 2000 |
| 12 | 4 | CRC-32 (IEEE, as in zip and PNG) of bytes 4-11 and the data |

A copy is **empty** without the magic (never-written SRAM reads `0xFF`, garbage rarely matches 4 bytes, erasing zeroes the magic), **valid** if the size is in range and the CRC matches, and **damaged** otherwise. A slot's save is its newest valid copy. `save_read()` returns:

- `SAVE_OK` or `SAVE_OTHER_VERSION` if a copy is valid (the newest valid one decides, even if a newer copy is damaged: the slot falls back to the previous save);
- `SAVE_CORRUPT` only if no copy is valid and one is damaged: a header claims a save whose checksum fails (bit rot, a dying battery, another program writing SRAM);
- `SAVE_EMPTY` otherwise: blank or never-written SRAM is empty, not corrupt.

### Guarantees

- **Power loss during `save_write()`** leaves the slot's previous save (or an empty slot) readable, never a corrupt or mixed one. The write goes to the copy that does *not* hold the newest valid save: it clears that copy's magic, writes the data, then the rest of the header, and the magic last. Until the last byte the copy reads as empty; with it, the copy is complete and, with the next sequence number, the newest. Every write is then read back and compared; if anything differs (no save RAM, failing SRAM), the copy's magic is cleared again, `save_write()` returns false and the previous save stays.
- **Power loss during `save_erase()`** leaves the slot either as it was or empty: the older copy's magic is cleared first, then the newer one's.
- **Damage** to a save is detected by the CRC-32 (every 1- to 3-bit error, and burst errors up to 32 bits) and, if the other copy holds an intact older save, reading falls back to it.
- These are tested on the host with a memory that loses power after every possible number of bytes, flips bits, and ignores writes (`tests/save_tests.c`), and on mGBA's SRAM (`tests/rom/save_tests.c`).

### Costs

SRAM is on an 8-bit bus with 8 wait states (`WAITCNT`, set by `serval_init()`), so it is read and written a byte at a time. Measured in mGBA (`tests/rom/save_tests.c` logs them), release build:

| Data size | `save_write` | `save_write`, slot empty | `save_read` | `save_erase` |
| --- | --- | --- | --- | --- |
| 2000 bytes (full slot) | 362,000 cycles (21.6 ms, 1.3 frames) | 232,000 (13.8 ms) | 188,000 (11.2 ms) | 3,000 (0.2 ms) |
| 100 bytes | 22,500 (1.3 ms) | 15,400 (0.9 ms) | 11,600 (0.7 ms) | 3,000 (0.2 ms) |

Per byte, a write is about 180 cycles (checking the slot's current save, the new CRC, writing, reading back) and a read about 94 (checking the CRC, then copying). Call them at natural pauses (game over, a menu), not every frame. The code runs from ROM (Thumb) and costs no IWRAM beyond a 4-byte pointer; games that never call `save_*` link none of it (and get no ID string).

### Planned

- **Flash** (64 and 128 KiB, `FLASH_V`/`FLASH1M_V`) and **EEPROM** (512 B and 8 KiB, `EEPROM_V`), for reproduction cartridges that have no SRAM. Flash is written through command sequences and erased in 4 KiB sectors; EEPROM is reached through DMA, 8 bytes at a time. The API stays the same; how slots map onto smaller chips (512-byte EEPROM can't hold 8 full slots) is open.
- Verify each save type on real flash carts (EverDrive, EZ-Flash) and reproduction cartridges.

## Text and dialogue

**Status:** HUD text implemented; dialogue planned.

Variable-width font renderer drawing glyphs into BG tiles; text boxes with typewriter effect and choices; localization support. Japanese glyph sets need early planning.

**Implemented so far:** a minimal fixed-width HUD/debug text layer (`include/serval/text.h`): libtonc's 8x8 `sys8` font on BG0, a 30x20 character grid, one color (white), `text_print_line()` for lines redrawn with changing content, and `text_format()` for numbers without a C library. The full system above will build on or replace it.

## Special effects

**Status:** partly implemented (brightness).

Alpha blending, brightness fades, windows (spotlights, masked HUD regions) and mosaic, exposed as API calls and script ops for transitions.

**Implemented:** `screen_set_brightness(level)` (−16 black … 0 … 16 white) fades the whole screen through the hardware's brightness effect (`BLDCNT`/`BLDY`, every layer and the backdrop as first targets); games fade by stepping it once per frame. The hardware does one color effect at a time, so alpha blending, when it comes, will share it with brightness. `serval_splash()` borrows the effect for its own fade and restores the game's level. Not exposed yet: alpha blending, windows, mosaic; examples build other effects from palettes (Pong's paddle flash, score glow via `screen_set_backdrop`).

## Math

**Status:** implemented.

Standardize on fixed-point types and lookup tables for trig early. There is no FPU or hardware divider.

**Implemented so far:** sine and cosine from a 1024-step table, with u16 angles (`include/serval/math.h`); 24.8 fixed point (`include/serval/fixed.h`) and deterministic random numbers (`include/serval/random.h`; `random_range` scales by multiplication, not division).

## Physics

**Status:** bouncing bodies and map bodies implemented; slopes and a fuller platformer controller (coyote time, moving platforms) planned.

**Implemented so far:** bouncing bodies (`include/serval/physics.h`): gravity in any direction, bounces inside a world rectangle (any edge can be left open) with per-entity bounciness and friction, and resting; or wrapping around the edges instead (`physics_set_wrap`). They don't collide with each other or with tilemaps. Map bodies (`C_MAPBODY`, `include/serval/map.h`) are the platformer side: `sys_map_movement()` applies the same gravity and stops them flush against solid and one-way metatiles of the playfield, reporting which sides touched (`body_contact`); see [tilemaps.md](tilemaps.md#collision).

## Entity collision

**Status:** pairwise test implemented; broad phase and collision events planned.

**Implemented so far:** `body_overlap(a, b)` (`include/serval/physics.h`), a rectangle test between two bodies, which the game calls for the pairs it cares about (Pong: ball against each paddle; Asteroids: every shot against every rock, and the ship against every rock), and `body_hit_side(a, b)`, which side of `a` met `b` (a stomp is `BODY_SIDE_BOTTOM`), judged from their positions before the frame's movement and their relative motion, so it holds for fast bodies and static colliders; bodies that already overlapped before the frame get `BODY_SIDE_INSIDE` rather than a guessed side. Hitboxes can be smaller than sprites, with the sprite's origin centering the art. No broad phase or collision events yet.

Planned: avoid all-pairs checks (about 8,000 pairs at 128 entities). Use a coarse spatial grid or collision groups as the broad phase. The collision system emits collision events to the VM ([vm.md](vm.md)).

## Camera

**Status:** implemented (`include/serval/map.h`): a position the game sets; following a target is game code for now.

Follows a target entity, clamps to room bounds, and drives BG streaming ([tilemaps.md](tilemaps.md#streaming)).

**Implemented so far:**

- `camera_set(x, y)` sets the world position shown at the screen's top-left, in pixels; `camera_x()` and `camera_y()` read it. While a playfield (BG2) map is loaded, it is clamped so the view stays inside it (0 on an axis where the map is smaller than the screen); loading the playfield clamps the current position too. Without one, any position is kept.
- `sys_render()` and `sys_render_by_depth()` draw entities at their world position minus the camera, so entity positions are world coordinates; `sprite_draw()` takes screen coordinates (HUD sprites). The camera starts at (0, 0), where world and screen coordinates are the same, so games that don't scroll never notice it.
- At the next `frame_end()`, every map layer scrolls to `camera * scroll_factor` and newly visible rows and columns are streamed in ([tilemaps.md](tilemaps.md#streaming)). Set the camera before the render systems run, so sprites and backgrounds move together.
- Following a target is a few lines of game code, e.g. `camera_set(fx_to_int(pos_x[player]) - SCREEN_W / 2, fx_to_int(pos_y[player]) - SCREEN_H / 2)` (the clamp keeps it inside the room). Planned: dead zones and smoothing.
