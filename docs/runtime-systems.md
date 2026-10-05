# Other runtime systems

These systems are scoped; several have a first implementation (marked **Implemented so far**), the rest are planned and not designed in detail yet.

## Save data

**Status:** planned. There is no save API yet (`serval_init()` sets the save-RAM wait state only).

Support SRAM, Flash and EEPROM, detected by emulators and flash carts from ROM ID strings (`SRAM_V`, `FLASH1M_V`, etc.). Slot-based API with checksums and a version number so saves survive game updates. Verify each save type on real flash carts.

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
