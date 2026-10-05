# ECS

Game world state lives in a simple bitmask ECS with fixed pools of 128 entities (`MAX_ENT`), matching the OAM sprite limit. No archetype storage and no malloc.

**Status:** implemented (`include/serval/ecs.h`, `include/serval/physics.h`, map bodies in `include/serval/map.h`); function reference in [api-reference.md](api-reference.md#ecsh). Collision events and script access are planned with the VM ([vm.md](vm.md)).

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
- Flat data at known addresses that the planned debugger can inspect and edit live ([debug-link.md](debug-link.md)).

## Entity handles

`Entity` is a 16-bit generational handle: low 8 bits slot index, high 8 bits generation. The generation increments when an entity is destroyed, so code (and later scripts) holding a stale handle gets a detectable mismatch: `entity_alive()` returns false and `entity_destroy()` does nothing. `ecs_reset()` also bumps the generation of every slot in use, so all outstanding handles go stale. Generations are 8 bits, so after 255 reuses of one slot a very old handle can match again; freed slots are reused oldest-first, which spreads reuse over all free slots and makes that as late as possible.

## Implementation

`include/serval/ecs.h`, `src/ecs/ecs.c` (platform-neutral, unit tested on the host and in the test ROM).

- `entity_create(mask)`, `entity_destroy(e)`, `entity_alive(e)`, `entity_at(index)` (the handle in a slot, e.g. to destroy the entity an `ECS_FOR_EACH` loop is on), `ecs_reset()` (destroys everything, e.g. on room change).
- Bit 31 of `ent_mask` is reserved as `C_ALIVE`, so free slots never match a system's required mask. Games may set and clear their own component bits in `ent_mask` but must keep `C_ALIVE`: a slot without it matches no system and no `ent_has()`. `entity_create()` adds it (and warns if the mask passed in already has it). Whether a slot is in use is tracked separately, so an entity whose `C_ALIVE` was cleared by hand is still freed by `entity_destroy()` and `ecs_reset()`.
- Generations start at 1 and skip 0 when wrapping, so no handle ever equals `ENTITY_NONE` (0).
- Free slots are kept in a FIFO ring (oldest-freed first; ascending order after `ecs_reset()`): creation and destruction are O(1), with no scan.
- Component bits 0-15 belong to the engine and 16-30 to games (`C_GAME(n)`, n = 0-14; a constant outside that range is a compile error); bit 31 is `C_ALIVE`.
- Engine components: `C_POS` (`pos_x`, `pos_y`: top-left, pixels) and `C_VEL` (`vel_x`, `vel_y`: pixels per frame), both 24.8 `FIXED`; `C_SPR` (`spr_id`, `spr_frame`, `spr_flags`, `spr_depth`, `spr_angle`); `C_BODY` (`body_w`, `body_h`, `body_bounce`, `body_friction`; `include/serval/physics.h`); `C_MAPBODY` (bit 4, a tag with no fields of its own; `include/serval/map.h`): together with `C_POS | C_VEL | C_BODY`, a body that collides with the map, with `body_contact` (which sides touched it in the last `sys_map_movement()`); `C_ANIM` (bit 5, `spr_anim_time`): with `C_SPR`, an animated sprite. `entity_create()` zeroes all of them.
- Game components: pick a bit with `C_GAME(n)` (n = 0-14) and declare your own `MAX_ENT`-sized arrays, indexed by slot like the engine's.
- Writing systems: `ECS_FOR_EACH(i, C_POS | C_VEL) { ... }` loops over matching entities, and `ent_has(i, mask)` tests one; both require `C_ALIVE` and *every* component in the mask (a hand-written `ent_mask[i] & (A | B)` is true for either).
- Engine systems so far, called once per frame by games alongside their own systems:
  - `sys_movement()`: position += velocity, except for map bodies.
  - `sys_map_movement()`: map bodies (`C_MAPBODY`): gravity and maximum fall speed, then movement that stops flush against solid and one-way metatiles of the playfield, bouncing (`body_bounce`), resting and sliding (`body_friction`) as `sys_physics()` bodies do ([tilemaps.md](tilemaps.md#collision)).
  - `sys_physics()`: gravity, maximum fall speed (`body_max_fall`), bounces off the world bounds (world coordinates: set them to the level in a scrolling game), friction and resting for `C_BODY` entities other than map bodies. Bounces mirror the overshoot and gravity is applied after the bounce, so no energy is created; floor bounces lose speed (`body_bounce` is at most 255/256), so bodies come to rest. Friction is rounded up, so any non-zero friction stops a sliding body.
  - `sys_animate()`: for `C_SPR | C_ANIM`, counts frames in `spr_anim_time` and steps `spr_frame` through the sprite's animation by its `frame_times` ([sprites.md](sprites.md#animation)), looping, or stopping on the last frame for `SPRITE_ASSET_ANIM_ONCE`. Run it before rendering.
  - `sys_render()`: `sprite_draw()` for `C_POS | C_SPR` in entity order, at the position minus the camera ([runtime-systems.md](runtime-systems.md#camera)); `sys_render_by_depth()` draws higher `spr_depth` in front (a radix sort, about 10,000 cycles for 128 sprites, so opt-in).
- `sys_movement`, `sys_physics`, `sys_render` and `sys_render_by_depth` run as ARM code from IWRAM; bunnymark measures them ([development.md](development.md#benchmark)). `sys_map_movement` and `sys_animate` stay in ROM: they handle a few entities, or do little per entity. `spr_anim_time` is in EWRAM for the same reason (the other pools are in IWRAM).

## Sprite component

`C_SPR` holds `spr_id` and `spr_frame` (which sprite and animation frame), `spr_flags` (flip and layer, as for `sprite_draw`), `spr_depth` (draw order for `sys_render_by_depth`) and `spr_angle` (rotation; 0 draws unrotated). `SPRITE_HIDDEN` in `spr_flags` skips drawing (blinking). Adding `C_ANIM` lets `sys_animate()` advance `spr_frame` by the sprite's timing; to start an animation, set `spr_id`, `spr_frame = 0` and `spr_anim_time = 0`. The render systems resolve the sprite to its VRAM tiles and palette; see [sprites.md](sprites.md#rom-data-format).
