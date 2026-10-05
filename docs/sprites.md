# Sprites and asset management

Assets stay in memory-mapped ROM; the engine only manages what is resident in VRAM and palette RAM. Sprites are organized into GameMaker-style groups, packed at build time by the tooling.

**Hardware budget:** OBJ VRAM is 32 KB (1024 4bpp tiles; 16 KB in bitmap modes) with 16 OBJ palette banks of 16 colors.

## Residency modes

Set per group.

| Mode | How it works | Best for | Compression |
| --- | --- | --- | --- |
| Resident | All frames copied to VRAM at room start | Small enemies, pickups, HUD | LZ77 allowed (BIOS `LZ77UnCompVram`) |
| Streamed | One-frame VRAM slot per instance; new frame DMA'd in VBlank | Large, heavily animated characters | Must be uncompressed |

## VRAM allocation

```
OBJ VRAM tile 0 ─────────────────────────────── tile 1023
[ global groups | room groups →      ← streamed slots ]
```

- Global groups (player, HUD, common FX) load once at boot.
- Room groups use a bump allocator reset to the global watermark on each room change.
- Streamed slots allocate downward from the top, with per-OBJ-size free lists.
- Room loads run during a fade with forced blank (`REG_DISPCNT` bit 7) for unrestricted VRAM access.

## Palettes

The tooling assigns each group logical banks. A runtime palette manager maps them to physical banks with reference counting, so identical palettes share one bank. A shadow palette is copied in VBlank, enabling fades, flashes and swaps.

## ROM data format

Emitted as constant C tables by the build tooling.

```c
typedef struct {
    const u32 *tiles;         // ROM tile data, frame after frame
    u8  size;                 // SPRITE_8x8 ... SPRITE_32x64 (required)
    u8  frame_count;          // 0 means 1
    u8  tiles_per_frame;      // 0: computed from size
    u8  palette_slot;         // logical bank within group
    s8  origin_x, origin_y;
    u8  flags;                // STREAMED, METASPRITE, ...
    const u8  *frame_times;   // animation timing
} SpriteAsset;

typedef struct {
    const u16 *sprite_ids;    // sprite table indices of the group's sprites; NULL: 0..sprite_count-1
    const u16 *palettes;
    u8  sprite_count, palette_count;
    u8  flags;                // 0 = resident (default); STREAMED, LZ77...
} SpriteGroup;
```

Fields left out of a C initializer take usable defaults, so a hand-written sprite needs only `.size` and `.tiles`, and a group only its palettes and counts. The engine computes how many tiles a group needs; `size` names the hardware dimensions directly (e.g. `SPRITE_16x16`), replacing the hardware's separate shape and size fields.

**Sprite IDs** are indices into a project-wide sprite table (`const SpriteAsset *const table[]`) emitted by the build. IDs rather than pointers keep the sprite component compact and keep addresses out of bytecode ([vm.md](vm.md)); groups list their sprites by ID for the same reason.

The sprite component stores `(sprite_id, frame)` plus per-entity draw flags (`spr_flags`: flip and layer). The render system resolves it to a VRAM tile index through the group's load offset or the instance's streamed slot.

## API

`include/serval/sprites.h`:

```c
void sprite_table_set(const SpriteAsset *const *table, u16 count);
bool sprite_group_load(const SpriteGroup *group);   // false if VRAM/palettes run out
void sprite_groups_reset(void);                     // unload all
void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
                                                    // SPRITE_FLIP_H/V, layer flags
void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags);
```

Sizes: `SPRITE_8x8`, `SPRITE_16x16`, `SPRITE_32x32`, `SPRITE_64x64` (square), `SPRITE_16x8`, `SPRITE_32x8`, `SPRITE_32x16`, `SPRITE_64x32` (wide), `SPRITE_8x16`, `SPRITE_8x32`, `SPRITE_16x32`, `SPRITE_32x64` (tall). `SPRITE_MAX` (512) sprite IDs per table. Tile data is 4 bits per pixel, 8 words per 8x8 tile (low nibble = leftmost pixel); for sprites larger than 8x8, tiles are row by row (1D mapping).

**Rotation:** `sprite_draw_rotated(id, frame, x, y, angle, flags)`, or a non-zero `spr_angle` with `sys_render`, rotates a sprite around its center with the hardware's affine mode (double size, so corners aren't clipped). The 32 rotation matrices are allocated per frame and shared by sprites with the same angle and flips; past 32, sprites are drawn unrotated and debug builds warn. Unrotated sprites don't pay for rotation support beyond one check per entity (bunnymark: +1,530 cycles for 128 sprites).

**Layering:** by default sprites draw between the foreground (BG1) and the playfield (BG2), so the HUD (BG0) stays on top. `SPRITE_ABOVE_FOREGROUND`, `SPRITE_ABOVE_HUD` and `SPRITE_BEHIND_PLAYFIELD` move a sprite to another layer (see [tilemaps.md](tilemaps.md#default-layer-roles)).

`sprite_draw` subtracts the sprite's origin, skips sprites that are fully off screen (so they don't use one of the 128 hardware sprites), and does nothing for sprites that are not loaded.

**Implemented so far:** resident, uncompressed groups with 4bpp sprites, regular or rotated; tiles and palette banks are bump-allocated in load order. **Not yet:** streamed sprites, LZ77 groups, metasprites, palette sharing with reference counting, the shadow palette, the global/room watermark, and loading during forced blank.
