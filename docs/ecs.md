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

## Sprite component

The sprite component stores only `(sprite_id, frame)`. The render system resolves it to a VRAM tile index; see [sprites.md](sprites.md#rom-data-format).
