# ECS

Game world state lives in a simple bitmask ECS with fixed pools of 128 entities, matching the OAM sprite limit. No archetype storage and no malloc.

- **Entity:** an ID only.
- **Component:** plain data in struct-of-arrays pools indexed by entity.
- **System:** a function looping over entities whose mask includes the required components.

```c
#define MAX_ENT 128
enum { C_POS = 1<<0, C_VEL = 1<<1, C_SPR = 1<<2, C_HIT = 1<<3 };

IWRAM_DATA u32   ent_mask[MAX_ENT];
IWRAM_DATA FIXED pos_x[MAX_ENT], pos_y[MAX_ENT];
IWRAM_DATA FIXED vel_x[MAX_ENT], vel_y[MAX_ENT];

IWRAM_CODE void sys_movement(void) {
    for (int i = 0; i < MAX_ENT; i++)
        if ((ent_mask[i] & (C_POS|C_VEL)) == (C_POS|C_VEL)) {
            pos_x[i] += vel_x[i];
            pos_y[i] += vel_y[i];
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

- `entity_create(mask)`, `entity_destroy(e)`, `entity_alive(e)`, `ecs_reset()` (destroys everything, e.g. on room change).
- Bit 31 of `ent_mask` is reserved as `C_ALIVE`, so free slots never match a system's required mask.
- Generations start at 1 and skip 0 when wrapping, so no handle ever equals `ENTITY_NONE` (0).
- Free slots are kept on a stack: creation and destruction are O(1), with no scan.
- Component bits 0-15 belong to the engine and 16-30 to games (`C_GAME(n)`); bit 31 is `C_ALIVE`.
- Engine components so far: `C_POS` (`pos_x`, `pos_y`), `C_VEL` (`vel_x`, `vel_y`), both 24.8 `FIXED`, and `C_SPR` (`spr_id`, `spr_frame`). `entity_create()` zeroes them.
- Engine systems so far: `sys_movement()` (position += velocity) and `sys_render()` (`sprite_draw()` for `C_POS | C_SPR`). Games call them once per frame, alongside their own systems.
- None of this is optimized yet (Thumb code in ROM, no IWRAM placement); bunnymark measures it ([development.md](development.md#benchmark)).

## Sprite component

The sprite component stores only `(sprite_id, frame)`. The render system resolves it to a VRAM tile index; see [sprites.md](sprites.md#rom-data-format).
