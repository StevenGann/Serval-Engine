# ECS

Game world state lives in a simple bitmask ECS with fixed pools of 128 entities, matching the OAM sprite limit. No archetype storage and no malloc.

- **Entity:** an ID only.
- **Component:** plain data in struct-of-arrays pools indexed by entity.
- **System:** a function looping over entities whose mask includes the required components.

```c
// Engine pools (include/serval/ecs.h): one array per component field.
extern u32   ent_mask[MAX_ENT];             // component bits per slot
extern FIXED pos_x[MAX_ENT], pos_y[MAX_ENT];
extern FIXED vel_x[MAX_ENT], vel_y[MAX_ENT];

// A game system (from examples/bunnymark): ECS_FOR_EACH visits every live
// entity with all the given components; i is the slot index.
#define C_BUNNY C_GAME(0)

static void bunny_animate(void) {
    ECS_FOR_EACH(i, C_BUNNY) {
        spr_flags[i] = vel_x[i] < 0 ? SPRITE_FLIP_H : 0;
        spr_depth[i] = (s16)fx_to_int(pos_y[i]);
    }
}
```

## Why ECS on GBA

The ARM7TDMI has no data cache, so the usual cache-locality argument does not apply. The benefits are:

- Fixed, predictable memory use.
- Hot arrays in zero-wait 32-bit IWRAM.
- Tight ARM-mode system loops.
- Flat data at known addresses that the debugger can inspect and edit live ([debug-link.md](debug-link.md)).

## Entity handles

16-bit generational handles: 8-bit index + 8-bit generation. The generation increments when a slot is reused, so scripts holding a stale reference get a detectable mismatch.

## Implementation

`include/serval/ecs.h`, `src/ecs/ecs.c` (platform-neutral, unit tested on the host and in the test ROM).

- `entity_create(mask)`, `entity_destroy(e)`, `entity_alive(e)`, `entity_at(index)` (the handle in a slot, e.g. to destroy the entity an `ECS_FOR_EACH` loop is on), `ecs_reset()` (destroys everything, e.g. on room change).
- Bit 31 of `ent_mask` is reserved as `C_ALIVE`, so free slots never match a system's required mask.
- Generations start at 1 and skip 0 when wrapping, so no handle ever equals `ENTITY_NONE` (0).
- Free slots are kept on a stack: creation and destruction are O(1), with no scan.
- Component bits 0-15 belong to the engine and 16-30 to games (`C_GAME(n)`); bit 31 is `C_ALIVE`.
- Engine components so far: `C_POS` (`pos_x`, `pos_y`), `C_VEL` (`vel_x`, `vel_y`), both 24.8 `FIXED`, and `C_SPR` (`spr_id`, `spr_frame`, `spr_flags`). `entity_create()` zeroes them.
- Writing systems: `ECS_FOR_EACH(i, C_POS | C_VEL) { ... }` loops over matching entities, and `ent_has(i, mask)` tests one; both require *every* component in the mask (a hand-written `ent_mask[i] & (A | B)` is true for either).
- Engine components also include `C_BODY` (`body_w`, `body_h`, `body_bounce`, `body_friction`; `include/serval/physics.h`) and, under `C_SPR`, `spr_depth` and `spr_angle` (rotation).
- Engine systems so far, called once per frame by games alongside their own systems:
  - `sys_movement()`: position += velocity.
  - `sys_physics()`: gravity, bounces off the world bounds, friction and resting for `C_BODY` entities. Bounces mirror the overshoot and gravity is applied after the bounce, so no energy is created; floor bounces lose speed, so bodies come to rest.
  - `sys_render()`: `sprite_draw()` for `C_POS | C_SPR` in entity order; `sys_render_by_depth()` draws higher `spr_depth` in front (a radix sort, about 10,000 cycles for 128 sprites, so opt-in).
- `sys_movement`, `sys_physics`, `sys_render` and `sys_render_by_depth` run as ARM code from IWRAM; bunnymark measures them ([development.md](development.md#benchmark)).

## Sprite component

`C_SPR` holds `spr_id` and `spr_frame` (which sprite and animation frame), `spr_flags` (flip and layer, as for `sprite_draw`), `spr_depth` (draw order for `sys_render_by_depth`) and `spr_angle` (rotation; 0 draws unrotated). The render systems resolve the sprite to its VRAM tiles and palette; see [sprites.md](sprites.md#rom-data-format).
