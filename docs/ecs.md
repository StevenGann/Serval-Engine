# ECS

Game world state lives in a simple bitmask ECS with fixed pools of 128 entities (`MAX_ENT`), matching the OAM sprite limit. No archetype storage and no malloc.

**Status:** implemented (`include/serval/ecs.h`, `include/serval/physics.h`, map bodies in `include/serval/map.h`); function reference in [api-reference.md](api-reference.md#ecsh). Component bits 8-15 and `body_contact` bits 4 and 7 are reserved for later engine versions ([Component bits](#component-bits), [Bodies](#bodies)). Scripts use the ECS through the bytecode VM ([vm.md](vm.md)): entity properties (the body's pools among them, `body_contact` read-only), spawning, and collision events (`vm_event()`, `vm_collide()`). Not in this version, and addable later without breaking games: `ent_add()`/`ent_remove()` helpers (writing `ent_mask` stays legal regardless), more than 15 game components (a second tag word, with its own queries), `entity_destroy()` detaching an entity's VM binding (today the VM notices the stale binding and warns: [vm.md](vm.md)), and for bodies swept body-against-body tests, body-to-body response and a broad phase ([runtime-systems.md](runtime-systems.md#physics)).

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

`Entity` is a 16-bit generational handle: low 8 bits slot index, high 8 bits generation. The generation increments when an entity is destroyed, so code (and scripts, through the VM) holding a stale handle gets a detectable mismatch: `entity_alive()` returns false and `entity_destroy()` does nothing. `ecs_reset()` also bumps the generation of every slot in use, so all outstanding handles go stale. Generations are 8 bits, so after 255 reuses of one slot a very old handle can match again; freed slots are reused oldest-first, which spreads reuse over all free slots and makes that as late as possible.

## Implementation

`include/serval/ecs.h`, `src/ecs/ecs.c` (platform-neutral, unit tested on the host and in the test ROM).

- `entity_create(mask)`, `entity_destroy(e)`, `entity_alive(e)`, `entity_at(index)` (the handle in a slot, e.g. to destroy the entity an `ECS_FOR_EACH` loop is on), `ecs_reset()` (destroys everything, e.g. on room change).
- Bit 31 of `ent_mask` is `C_ALIVE`, so free slots never match a system's required mask ([Component bits](#component-bits)). Whether a slot is in use is tracked separately, so an entity whose `C_ALIVE` was cleared by hand is still freed by `entity_destroy()` and `ecs_reset()`.
- Generations start at 1 and skip 0 when wrapping, so no handle ever equals `ENTITY_NONE` (0).
- Free slots are kept in a FIFO ring (oldest-freed first; ascending order after `ecs_reset()`): creation and destruction are O(1), with no scan.
- `entity_create()` warns once in debug builds (again after each `ecs_reset()`) when the pool is full (it returns `ENTITY_NONE`), when the mask it is given has `C_ALIVE` (added anyway), when it has reserved bits 8-15 (left out), and when it has both `C_MAPBODY` and `C_KINEMATIC` (kept; the second changes nothing for a map body).
- Engine components: `C_POS` (`pos_x`, `pos_y`: top-left, pixels) and `C_VEL` (`vel_x`, `vel_y`: pixels per frame), both 24.8 `FIXED`; `C_SPR` (`spr_id`, `spr_frame`, `spr_flags`, `spr_depth`, `spr_angle`, `spr_scale`); `C_BODY` (`body_w`, `body_h`, `body_bounce`, `body_friction`, `body_max_fall`, `body_gravity`, `body_contact`; `include/serval/physics.h`, [Bodies](#bodies)); `C_MAPBODY` (bit 4, a tag with no fields of its own; `include/serval/map.h`): together with `C_POS | C_VEL | C_BODY`, a body that collides with the map, with `body_contact` (which sides touched it in the last `sys_map_movement()`); `C_ANIM` (bit 5, `spr_anim_time`, `spr_anim_step`): with `C_SPR`, an animated sprite. `entity_create()` zeroes all of them. `C_PATH` (bit 6, `include/serval/path.h`): with `C_VEL`, an entity following a path; added only by `path_start()`, which sets its state (`path_heading`, `path_speed`, `path_step`, `path_time`). `C_KINEMATIC` (bit 7, a tag with no fields of its own; `include/serval/physics.h`): together with `C_POS | C_VEL | C_BODY`, a kinematic body, which moves only by its velocity ([Bodies](#bodies)).
- Game components: pick a bit with `C_GAME(n)` (n = 0-14) and declare your own `MAX_ENT`-sized arrays, indexed by slot like the engine's.
- Writing systems: `ECS_FOR_EACH(i, C_POS | C_VEL) { ... }` loops over matching entities, and `ent_has(i, mask)` tests one; both require `C_ALIVE` and *every* component in the mask (a hand-written `ent_mask[i] & (A | B)` is true for either).
- Engine systems so far, called once per frame by games alongside their own systems:
  - `sys_path()`: for `C_PATH | C_VEL`, steps the entity's path: turns its heading, changes its speed and sets the velocity from them; run it before `sys_movement()` ([runtime-systems.md](runtime-systems.md#paths)).
  - `sys_movement()`: position += velocity, except for map bodies (kinematic bodies included: it is all that moves them).
  - `sys_map_movement()`: map bodies (`C_MAPBODY`): gravity and maximum fall speed, then movement that stops flush against solid and one-way metatiles of the playfield, bouncing (`body_bounce`), resting and sliding (`body_friction`) as `sys_physics()` bodies do ([tilemaps.md](tilemaps.md#collision)).
  - `sys_physics()`: gravity, maximum fall speed (`body_max_fall`), bounces off the world bounds (world coordinates: set them to the level in a scrolling game), friction and resting for `C_POS | C_VEL | C_BODY` entities other than map bodies and kinematic bodies. Floor bounces keep `body_bounce`/256 of the speed (0-254), so bodies come to rest, or with 255 bounce perfectly, back to the height they fell from, for ever. Friction is rounded up, so any non-zero friction stops a sliding body; a speed along a floor under 1/16 pixel per frame stops even without friction. Each body can scale gravity (`body_gravity`, e.g. a ball that flies straight while power-ups fall), and with `physics_set_contacts(true)` it reports the walls each body touched, and the open edges it left through, in `body_contact` ([Bodies](#bodies), [runtime-systems.md](runtime-systems.md#physics)).
  - `sys_animate()`: for `C_SPR | C_ANIM`, counts frames in `spr_anim_time` and steps `spr_frame` through the sprite's animation by its `frame_times` ([sprites.md](sprites.md#animation)), looping, or stopping on the last frame for `SPRITE_ASSET_ANIM_ONCE`. For sprites with a `frame_order` sequence it steps `spr_anim_step` instead and sets `spr_frame` (and the step's flips in `spr_flags`) from it. Run it before rendering.
  - `sys_render()`: `sprite_draw()` for `C_POS | C_SPR` in entity order, at the position minus the camera ([runtime-systems.md](runtime-systems.md#camera)); `sys_render_by_depth()` draws higher `spr_depth` in front (a counting sort whose cost depends on the depths: nothing to sort when they are already in slot order, one pass over a few buckets for a few kinds of entity, about 8,200 cycles for 128 sprites sorted by y).
- `sys_movement`, `sys_physics`, `sys_render` and `sys_render_by_depth` run as ARM code from IWRAM, and so do `ecs_count()` and `ecs_gather()` (each in a section of its own, so only games that call them pay the IWRAM); bunnymark measures the systems ([development.md](development.md#benchmark)). `sys_map_movement` and `sys_animate` stay in ROM: they handle a few entities, or do little per entity. `spr_anim_time` and `spr_anim_step` (read only by `sys_animate`), `spr_scale` (read only for sprites with `SPRITE_SCALED`, out of the render loops' usual path) and the path pools (`path_heading`, `path_speed`, `path_step`, `path_time`: `sys_path` reads them once a frame per pathed entity) are in EWRAM; the other pools are in IWRAM.

## Component bits

`ent_mask` and every mask the API takes (`entity_create()`, `ent_has()`, `ECS_FOR_EACH`, `ecs_count()`, `ecs_gather()`) use one 32-bit layout:

| Bits | Owner | Names |
| --- | --- | --- |
| 0-7 | Engine components | `C_POS` 0, `C_VEL` 1, `C_SPR` 2, `C_BODY` 3, `C_MAPBODY` 4 (`map.h`), `C_ANIM` 5, `C_PATH` 6 (`path.h`), `C_KINEMATIC` 7 (`physics.h`) |
| 8-15 | Reserved for engine components of later versions | none: never set them |
| 16-30 | The game's own components, 15 of them | `C_GAME(0)` to `C_GAME(14)`; a constant `n` outside 0-14 is a compile error ("size of array is negative"); there is no `C_GAME(15)` |
| 31 | Alive | `C_ALIVE`: `entity_create()` adds it; free slots never have it |

**Reserved bits.** `entity_create()` leaves bits 8-15 out of the new entity's mask, with a warning (once, in debug builds): no entity can have one before an engine version gives it a meaning, so giving it one then is a compatible change (a meaning for a value this version refuses: [releases.md](releases.md)). Bit 7 was reserved this way until `C_KINEMATIC` took it, before 1.0.0-rc.1 ([api-freeze.md](api-freeze.md#what-the-freeze-did-100-rc1)). A game that writes them into `ent_mask` by hand isn't stopped, but a later version may make engine systems act on them. A static assertion in `src/ecs/ecs.c` stops the build if an engine component is ever given a bit in the reserved range without shrinking the range.

**Writing `ent_mask`** is part of the API for good: a game may add and remove components, engine or its own, at any time (`ent_mask[i] |= C_SPR;`, `ent_mask[i] &= ~C_SHIELD;`). Later versions may add helpers for it, never take it away. The rules: keep `C_ALIVE` (a slot without it matches no system and no `ent_has()`, though `entity_destroy()` still frees it, with a warning), leave bits 8-15 clear, and add `C_PATH` only through `path_start()` (debug builds warn about, and remove, one added by hand). A component added by hand starts from whatever its pools hold: zero from `entity_create()`, unless the game wrote them since.

The game's 15 bits are all it gets in this version. More would be a second tag word with its own queries and VM property, which can be added later without changing anything here.

## Bodies

An entity with `C_POS | C_VEL | C_BODY` is a body, moved by `sys_physics()` ([runtime-systems.md](runtime-systems.md#physics) has the behaviour); with `C_MAPBODY` as well, a map body, moved by `sys_map_movement()` ([tilemaps.md](tilemaps.md#collision)); with `C_KINEMATIC` as well, a **kinematic body**, which moves only by its velocity: `sys_movement()` moves it and `sys_physics()` leaves it alone (no gravity, bounds, bounces, friction, maximum fall, contacts or exits), for shots, enemies on a course of their own, platforms the game moves by velocity. `body_overlap()`, `body_hit_side()` and `vm_collide()` test every body alike, and work on any entities with `C_POS`, so a body without `C_VEL` is a static collider. A map body with `C_KINEMATIC` is a map body (`entity_create()` warns): `C_KINEMATIC` changes nothing for it. The pools (`include/serval/physics.h`), all zeroed by `entity_create()`:

| Pool | Type | Meaning |
| --- | --- | --- |
| `body_w`, `body_h` | `u8` | Size in pixels (the rectangle `body_overlap()` tests, kept inside the bounds) |
| `body_bounce` | `u8` | How bouncy the body is on a floor (the wall gravity pulls toward). 0-254: the share of the speed a floor bounce keeps, in 256ths (224 keeps 7/8; 0, the default, stops the body). **255: a perfect bounce**, which loses nothing: the body comes back up as high as it fell from, to within the frame steps (a sweep of 430 drops peaked within 2.1 pixels of it, all but one within 1.4), bounce after bounce, so it doesn't come to rest (unless its bounces are tiny, or `body_max_fall` slows it: [runtime-systems.md](runtime-systems.md#physics)); a `u8` can't hold 256. `sys_physics()` reads it only on floors (other walls keep all of the speed); map bodies use it on every side of the map |
| `body_friction` | `u8` | Speed lost per frame while touching a floor, in 256ths, rounded up (0: none). Whatever it is, `sys_physics()` stops a speed along the floor under 1/16 pixel per frame (`FX_ONE / 16`), so a body without friction keeps sliding only at that speed or faster; map bodies without friction keep any speed ([runtime-systems.md](runtime-systems.md#physics)) |
| `body_max_fall` | `u16` | Fall speed limit in 24.8 pixels per frame (0: none) |
| `body_gravity` | `s8` | Gravity scale in 16ths, stored minus 16: write `BODY_GRAVITY(16)` (normal, the zero default), `BODY_GRAVITY(0)` (none) or `BODY_GRAVITY(-16)` (reversed); scales -112 to 143 |
| `body_contact` | `u8` | What the body touched in the last `sys_physics()` (with `physics_set_contacts(true)`; 0 otherwise) or `sys_map_movement()` (always). Read it, don't write it: bits below. Scripts can only read it |

`body_contact`'s bits (test the ones you need, as later versions may set more):

| Bit | Name | Set when |
| --- | --- | --- |
| 0 | `BODY_SIDE_BOTTOM` = `MAP_CONTACT_FLOOR` | The body's bottom touched the bottom bound (`sys_physics()`) or the map (`sys_map_movement()`) |
| 1 | `BODY_SIDE_TOP` = `MAP_CONTACT_CEILING` | Its top touched the top bound or the map |
| 2 | `BODY_SIDE_LEFT` = `MAP_CONTACT_LEFT` | Its left side touched the left bound or the map |
| 3 | `BODY_SIDE_RIGHT` = `MAP_CONTACT_RIGHT` | Its right side touched the right bound or the map |
| 4 | reserved | Never set. It is `BODY_SIDE_INSIDE`'s value, which only `body_hit_side()` returns |
| 5 | `BODY_CONTACT_EXIT` | With the side bit of an open edge (`physics_set_open_edges()`): `sys_physics()` found the body entirely outside past that edge, and it wasn't before this frame's `sys_movement()` (at its position minus its velocity). Set on that one frame; [runtime-systems.md](runtime-systems.md#physics) has the exact tests |
| 6 | `MAP_CONTACT_LADDER` (`map.h`) | Ladders, for map bodies. Planned: never set in this version |
| 7 | reserved | Never set |

`sys_physics()` sets a side on the frame the body bounces off that wall and on every frame it rests against it (on a floor, or at a wall with no speed away from it); `sys_map_movement()` sets one when the body moves into the map on that side ([tilemaps.md](tilemaps.md#collision)). A wrapping axis (`physics_set_wrap()`) has no walls and no exits, so it sets none of these.

## Iterating

`ECS_FOR_EACH` visits all 128 slots, however few match: from game code (Thumb, in ROM) that is about 4,300 cycles per loop. One loop per kind and frame is fine; a loop per entity (each shot tested against every enemy with its own `ECS_FOR_EACH`) is not: `shmup` spent 48% of the CPU that way with four entities on screen. Gather each kind once per frame instead and loop over the lists:

```c
static u8 shots[MAX_ENT], enemies[MAX_ENT];

static void hit_enemies(void) {
    u32 shot_count = ecs_gather(C_SHOT, shots);   // ascending slot order
    u32 enemy_count = ecs_gather(C_ENEMY, enemies);
    for (u32 s = 0; s < shot_count; s++) {
        for (u32 e = 0; e < enemy_count; e++) {
            u32 enemy = enemies[e];
            if (ent_has(enemy, 0) && body_overlap(shots[s], enemy)) {
                entity_destroy(entity_at(enemy)); // still listed: ent_has() says it is gone
                entity_destroy(entity_at(shots[s]));
                break;
            }
        }
    }
}
```

`ecs_gather()` and `ecs_count()` run as ARM code from IWRAM, four masks per iteration: about 1,000 and 870 cycles for the whole pool (`ecs_count` from ROM took about 2,900). A list is a snapshot: entities created after it aren't in it, and destroyed ones stay in it (test `ent_has(i, 0)` when an earlier step may have destroyed them). `ecs_free_count()` is the number of free slots (O(1)): create optional entities, like sparks, only while it leaves a reserve, so a full pool never makes `entity_create()` fail (and warn) for an entity that matters.

## Sprite component

`C_SPR` holds `spr_id` and `spr_frame` (which sprite and animation frame), `spr_flags` (flip and layer, as for `sprite_draw`), `spr_depth` (draw order for `sys_render_by_depth`), `spr_angle` (rotation; 0 draws unrotated) and `spr_scale` (size, used only with `SPRITE_SCALED` in `spr_flags`). `SPRITE_HIDDEN` in `spr_flags` skips drawing (blinking). Adding `C_ANIM` lets `sys_animate()` advance `spr_frame` by the sprite's timing; to start an animation, set `spr_id`, `spr_frame = 0` and `spr_anim_time = 0` (and `spr_anim_step = 0` for a sprite with a `frame_order`). The render systems resolve the sprite to its VRAM tiles and palette; see [sprites.md](sprites.md#rom-data-format).
