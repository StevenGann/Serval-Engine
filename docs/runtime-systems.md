# Other runtime systems

These systems are scoped but not yet designed in detail.

## Save data

Support SRAM, Flash and EEPROM, detected by emulators and flash carts from ROM ID strings (`SRAM_V`, `FLASH1M_V`, etc.). Slot-based API with checksums and a version number so saves survive game updates. Verify each save type on real flash carts.

## Text and dialogue

Variable-width font renderer drawing glyphs into BG tiles; text boxes with typewriter effect and choices; localization support. Japanese glyph sets need early planning.

**Implemented so far:** a minimal fixed-width HUD/debug text layer (`include/serval/text.h`): libtonc's 8x8 `sys8` font on BG0, a 30x20 character grid, and `text_format()` for numbers without a C library. The full system above will build on or replace it.

## Special effects

Alpha blending, brightness fades, windows (spotlights, masked HUD regions) and mosaic, exposed as API calls and script ops for transitions.

## Math

Standardize on fixed-point types and lookup tables for trig early. There is no FPU or hardware divider.

**Implemented so far:** sine and cosine from a 1024-step table, with u16 angles (`include/serval/math.h`); 24.8 fixed point (`include/serval/fixed.h`) and deterministic random numbers (`include/serval/random.h`; `random_range` scales by multiplication, not division).

## Physics

**Implemented so far:** bouncing bodies (`include/serval/physics.h`): gravity in any direction, bounces inside a world rectangle (any edge can be left open) with per-entity bounciness and friction, and resting; or wrapping around the edges instead (`physics_set_wrap`). Bodies don't collide with each other or with tilemaps; a platformer character controller is separate future work.

## Entity collision

**Implemented so far:** `body_overlap(a, b)` (`include/serval/physics.h`), a rectangle test between two bodies, which the game calls for the pairs it cares about (Pong: ball against each paddle). No broad phase or collision events yet.

Avoid all-pairs checks (about 8,000 pairs at 128 entities). Use a coarse spatial grid or collision groups as the broad phase. The collision system emits collision events to the VM ([vm.md](vm.md)).

## Camera

Follows a target entity, clamps to room bounds, and drives BG streaming ([tilemaps.md](tilemaps.md#streaming)).
