# Sprites and asset management

Assets stay in memory-mapped ROM; the engine only manages what is resident in VRAM and palette RAM. Sprites are organized into GameMaker-style groups, packed at build time by the tooling.

**Status:** implemented: resident, uncompressed groups of 4bpp sprites and metasprites, drawn regular, rotated or scaled, with any palette of their group, in world or screen coordinates; loading in layers with marks ([VRAM allocation](#vram-allocation)); loaders that refuse values they don't know ([ROM data format](#rom-data-format)); [runtime tiles](#runtime-tiles) (`sprite_set_tiles()`). Reference: [api-reference.md](api-reference.md#spritesh). **Planned**, declared in `sprites.h` with `SERVAL_PLANNED` ([releases.md](releases.md#planned-api)) and implemented in a 1.x version: streamed groups, LZ77-compressed sprites, palette writes through a shadow palette, and alpha-blended sprites. After 1.0, with no API of their own: palette sharing between groups, VRAM defragmentation and loading in forced blank. See [Implemented so far](#implemented-so-far).

**Hardware budget:** OBJ VRAM is 32 KB (1024 4bpp tiles; 16 KB in bitmap modes, which the engine doesn't use) with 16 OBJ palette banks of 16 colors.

## Residency modes

Set per group, in `SpriteGroup.flags`; a sprite has no residency of its own (bit 0 of `SpriteAsset.flags`, `SPRITE_ASSET_STREAMED` before 1.0, is reserved).

| Mode | Flag | Status | How it works | Best for | Compression |
| --- | --- | --- | --- | --- | --- |
| Resident | `SPRITE_GROUP_RESIDENT` (0, the default) | Implemented | `sprite_group_load()` copies all frames to VRAM | Small enemies, pickups, HUD | LZ77 (*planned*, [below](#lz77-compression)) |
| Streamed | `SPRITE_GROUP_STREAMED` | *Planned*: `sprite_group_load()` refuses it (*warns*) | Frames stay in ROM; the group's `.slots` hold the frames on screen, copied in VBlank | Large, heavily animated characters | Uncompressed only |

**Streamed groups (planned design).** A streamed group is a frame cache, so drawing needs no instance API and stays immediate-mode:

- Each distinct frame drawn in a frame, a (sprite, frame number) pair, takes one of the group's `slots` (`SpriteGroup.slots`; 0 means 1). A frame already in a slot costs nothing more; a new one is copied from ROM to its slot in VBlank by `frame_end()`, in the same VBlank as the OAM, so it shows on time. Two entities on the same frame share a slot.
- Each slot is the size of the group's largest frame. The slots are taken from the top of OBJ VRAM when the group loads (below) and freed with the group (marks, `sprite_groups_reset()`).
- Drawing is unchanged: `sprite_draw*()`, `sys_render`, `sys_render_by_depth`, metasprite pieces and `sys_animate` work as with resident sprites. Frames past the slots in one frame are not drawn: counted in `sprite_stats().dropped`, *warns*.
- Cost: each new frame is a copy in VBlank, about 64 cycles per tile, so a few new frames per frame fit. Big, many-framed art (a boss, a character with dozens of frames) then costs VRAM for the frames on screen, not all of them.
- The resident fast path doesn't pay for it: a streamed sprite's draw record will have no frames of its own, as a metasprite's has, so the drawing path's existing "frame out of range" test sends it out of line.
- Not combined with `SPRITE_ASSET_LZ77` (refused: streaming copies straight from ROM) or `sprite_set_tiles()` (ignored).

Until it is implemented, `sprite_group_load()` refuses a group with `SPRITE_GROUP_STREAMED` (returns false, *warns*), and refuses a resident group whose `slots` is not 0.

## VRAM allocation

Tiles are bump-allocated from tile 0 in load order, and each group's palettes take the next free OBJ palette banks. Nothing is freed one group at a time: memory goes back by [marks](#marks) or all at once (`sprite_groups_reset()`, `sprite_table_set()`).

```
OBJ VRAM tile 0 ─────────────────────────────────────────── tile 1023
[ groups every room uses | mark | room groups →     ← streamed slots (planned) ]
```

### Marks

*Implemented.* `sprite_groups_mark()` returns a mark, an opaque `u32` naming the point after the groups loaded so far; `sprite_groups_release(mark)` unloads every group loaded since, rolling sprite VRAM and palette banks back to that point, and keeps the groups loaded before it. That makes the global/room watermark:

```c
sprite_table_set(sprite_table, SPRITE_COUNT);
sprite_group_load(&player_and_hud);         // every room uses these
u32 rooms = sprite_groups_mark();

void enter_room(const SpriteGroup* room) {  // while the screen is faded out
    sprite_groups_release(rooms);           // the last room's groups go
    sprite_group_load(room);
}
```

- **Taking a mark** before anything more has loaded returns the same mark again. A failed load changes nothing, so it doesn't count; a group of metasprites only, which takes no VRAM, does.
- **Marks nest** (a level's groups above the global ones, a room's above the level's), up to 16. Past 16, `sprite_groups_mark()` returns the newest mark (*warns*), so releasing to it also unloads the groups loaded since that one was taken.
- **Releasing** clears the released groups' sprites (drawing them does nothing, *warns*, until a group with them loads again), forgets the marks taken after `mark`, and keeps `mark` itself, so a game releases to the same mark on every room change. It takes effect at once, like loading.
- **Stale marks are ignored** (*warns*): one forgotten by releasing to an earlier mark, one from before the last `sprite_groups_reset()` or `sprite_table_set()`, or a value `sprite_groups_mark()` never returned.
- **A sprite loaded twice** (its ID in a group before the mark and one after) is drawn from the newest copy, so releasing the later group unloads it although the earlier copy is still in VRAM. Keep each sprite in one group.

How it works: the groups loaded between two marks are always released together, so only the marks are recorded, as a stack of segments (segment 0 holds the groups loaded since the last reset, segment k those loaded since mark k was taken), each with where the allocator stood when it began. Each loaded sprite's draw record names its segment; releasing to mark k clears the records of segments k and up (one pass over the sprite table: releases are rare) and rolls the allocator back to segment k's start. Mark values are serial numbers, never reused, which is how stale ones are recognized. The state (17 segments of 8 bytes) is in EWRAM.

**Loads write VRAM at once,** not in VBlank: `sprite_group_load()` copies tiles and palettes immediately (as `tileset_load()` does), so load while changing rooms, with the screen faded out (`screen_set_brightness()`): a sprite drawn in the same frame from tiles being replaced may tear for that frame. Loading in forced blank (`REG_DISPCNT` bit 7, for unrestricted VRAM access) and defragmenting VRAM are post-1.0 ideas with no API; neither would change what a game calls.

## LZ77 compression

*Planned:* `SPRITE_ASSET_LZ77` (bit 3 of `SpriteAsset.flags`). The sprite's `.tiles` then hold its `frame_count` × `tiles_per_frame` tiles LZ77-compressed, as the GBA BIOS's `LZ77UnCompVram` reads them: type 0x10, a header word with the unpacked size in bytes, which must equal `frame_count` × `tiles_per_frame` × 32, and a stream that function can unpack (compressors call this their VRAM-safe mode). `sprite_group_load()` unpacks them into VRAM, so the sprite takes less ROM and the same VRAM. On the web, a C decoder will stand in for the BIOS call, as `src/web/platform.c` does for the other BIOS functions. Ordinary sprites in resident groups only: a metasprite with the flag, or a streamed group holding such a sprite, will be refused.

Until it is implemented, `sprite_group_load()` refuses a group holding a sprite with the flag (returns false, *warns*).

## Palettes

**Implemented: a bank per group palette.** Each loaded group takes the next `palette_count` OBJ palette banks, copied to palette RAM at once by `sprite_group_load()` and freed by marks or a reset; a sprite draws with its group's first bank plus its `palette_slot`. Two groups with identical palettes take two banks.

**Implemented: choosing a palette per draw.** A sprite draws with its asset's `palette_slot`, or with `SPRITE_PALETTE(n)` in its draw flags (`spr_flags` for entities) with palette `n` (0-14) of its group: a white hit flash, a damaged or enraged color, the eight colors of one brick, without a copy of the sprite per color. The art and its color variants share tiles; only the group's palette list grows. Details under [API](#api).

**Planned: palette writes,** `sprite_set_colors(sprite_id, index, colors, count)`: changes `count` colors of the palettes of the group `sprite_id` was loaded with, from color `index` = palette × 16 + color on (palettes numbered as `SPRITE_PALETTE(n)` numbers them, so one call can run across several). The colors are copied at once into a shadow of the palette banks, which `frame_end()` copies to palette RAM in VBlank. For hit flashes and palette cycles, and fades: colors mixed from the ROM palette toward white, black or a tint with `color_mix()` (`screen.h`), written each frame. The rules, fixed now so that sharing (below) can't change them:

- **Copy-on-write:** a write changes that group's sprites only. Where a later version shares a bank between groups with identical palettes, writing to it first gives the group its own copy of the bank; if no free bank is left for the copy, the write goes to the shared bank, every group sharing it changes, and debug builds warn.
- Color 0 of each palette is transparent: a write to it is kept, not shown.
- Written colors stay until written again or the group is unloaded. To put the ROM colors back, write them again from the group's `.palettes`.
- Ignored (*warns*): a sprite that isn't loaded, a metasprite (its pieces' groups hold the colors), a NULL `colors`, colors past the group's palettes (`index + count > palette_count × 16`).

Until it is implemented, `sprite_set_colors()` does nothing (the colors stay as loaded) and warns once in debug builds.

**After 1.0: sharing.** The tooling can give each group logical banks, and a runtime palette manager can map them to physical banks with reference counting, so that identical palettes share one bank. It needs no new API, and the copy-on-write rule above already fixes what a game sees.

## Runtime tiles

*Implemented:* `sprite_set_tiles(sprite_id, frame, tiles)`, the sprites' counterpart of `tileset_set_tiles()` ([tilemaps.md](tilemaps.md)). It replaces the pixels of one frame of a loaded sprite with `tiles_per_frame` tiles from `tiles` (8 words each, as in `SpriteAsset.tiles`): a card face composed at run time into a RAM buffer and drawn as one hardware sprite instead of several pieces (Blackjack's card faces are metasprites of four pieces, so four hardware sprites each), a score or a counter drawn into one sprite, a name tag. Every draw of that frame changes, from the frame after the copy:

- The copy happens in VBlank at the next `frame_end()`, step 3 of its flush ([frame-loop.md](frame-loop.md#vblank-flush)), so `tiles` must stay valid until then: a buffer composed for one frame can't be reused for another before `frame_end()`. After it, the buffer is free again.
- Up to `SPRITE_MAX_TILE_UPDATES` (8) calls per frame; a later call for the same sprite and frame replaces the earlier one and takes no other place in the queue.
- Ignored (*warns*, once per kind of problem until `sprite_groups_reset()`): a sprite that isn't loaded (or isn't in the sprite table), a metasprite (it has no tiles of its own: set its pieces' sprites' tiles), a sprite of a streamed group (its frames are copied from ROM as they are drawn), a frame the sprite doesn't have, a NULL `tiles`, a full queue. `sprite_groups_release()` and `sprite_groups_reset()` drop the copies queued for the sprites they unload, even if those sprites load again before `frame_end()`.
- VRAM for frames composed later comes from loading sprites whose `.tiles` are a blank frame in RAM, which they can all share: each sprite ID gets its own copy in VRAM. The tiles stay as written until the sprite is written again or unloaded; loading the group again copies its `.tiles` afresh.

**Cost.** The call checks the sprite and queues a pointer; the copy is a `memcpy32` to OBJ VRAM in VBlank, about 75 cycles a tile from EWRAM and 58 from ROM (a 32x64 frame, 32 tiles: about 2,400 and 1,850 cycles of VBlank's 83,776), so a few frames a frame fit beside the map's rows and columns. With nothing queued, `frame_end()`'s call is a test of a counter in EWRAM, about 40 cycles of VBlank, and nothing in IWRAM (`sprite_tiles.c`). Composing the pixels is the game's: the [effects example](../examples/effects/sprite_tiles_fx.c) draws a card's face on its ASCII-art canvas when the card is dealt, and builds a frame counter every frame from digit tiles composed once, a few word copies.

## Alpha blending

*Planned:* `SPRITE_BLEND` (bit 12 of the draw flags: `sprite_draw*()` flags, `spr_flags` and `SpritePiece.flags`) draws a sprite semi-transparent, for shadows, ghosts and glass. Where the sprite is over one of the `bottom` layers of `screen_set_blend()` (`screen.h`, [runtime-systems.md](runtime-systems.md#special-effects)), it is mixed with them with that call's weights, whether or not the call's `top` layers include the sprites; elsewhere, and while blending is off (the default), it is drawn opaque. A metasprite's piece blends when the draw or the piece has the flag.

The render systems already route it: entities with `SPRITE_BLEND` take the out-of-line drawing path, as rotated, scaled, hidden and recolored ones do, so the flag will cost those sprites about 60 cycles each and the others nothing. The test for all of them stayed two ARM instructions: the flags that take that path (`SPRITE_HIDDEN`, `SPRITE_SCALED`, `SPRITE_PALETTE`, `SPRITE_BLEND`) are bits 4 and 7-12, which no single ARM immediate covers, so the render loops clear the plain bits (`BIC #0x6F`) and OR the rest, shifted above the angle's 16 bits, with the angle (`ORRS` with a shifted operand, which also drops bits 13-15); an empty `asm` keeps GCC from folding the mask into the shift, which would cost a constant load per sprite (+400 cycles in bunnymark). Bunnymark measured the same before and after (avg 73,426 cycles, peak 78,643; `sprites.c`, `draw_entity`).

Until it is implemented, a sprite with `SPRITE_BLEND` is drawn opaque and debug builds warn once (until `sprite_groups_reset()`).

**Bits 12-14 of the draw flags are the engine's:** 12 is `SPRITE_BLEND`, 13 and 14 are reserved for later draw flags (mosaic and the object window). Drawing doesn't check them (it is the hot path), so a game must not store anything there: a stray bit is ignored today and gets a meaning later. `SpritePiece.flags` are checked at load.

## ROM data format

Emitted as constant C tables by the build tooling. Write them with designated initializers, in tools and by hand: fields left out are zero, and every zero is a usable default (a sprite needs only `.size` and `.tiles`, a group only its palettes and counts), so data written before a field existed keeps its meaning when the field is added. Offsets are the GBA's (4-byte pointers); `src/core/sprite_table.c` asserts the sizes at compile time.

**Loaders refuse values they don't know.** `sprite_group_load()` refuses a group (returns false, loads nothing of it, *warns*) when anything in it has a reserved bit or value, below, or needs a planned feature. Accepting them would let data with a stray bit work today and change meaning the day the bit is assigned ([releases.md](releases.md#versioning)).

```c
typedef struct {
    s16 x, y;                 // the piece's center, relative to the metasprite's pivot
    u16 sprite;               // an ordinary sprite's ID
    u16 flags;                // SPRITE_FLIP_H/V, SPRITE_PALETTE(n), SPRITE_BLEND (planned)
    u8  frame;
} SpritePiece;                // 10 bytes

typedef struct {
    union {
        const u32 *tiles;     // ROM tile data, frame after frame
        const SpritePiece *pieces; // metasprites: piece_count pieces per frame
    };
    u8  size;                 // SPRITE_8x8 ... SPRITE_32x64 (required, except metasprites)
    union {
        u8 tiles_per_frame;   // 0: computed from size; if set, at least what size needs
        u8 piece_count;       // metasprites: pieces per frame
    };
    u8  frame_count;          // 0 means 1
    u8  order_length;         // frame_order entries (steps); 0: no frame_order
    u8  palette_slot;         // which of its group's palettes it uses
    s8  origin_x, origin_y;   // drawn position = (x, y) - origin
    u8  flags;                // SPRITE_ASSET_METASPRITE, _ANIM_ONCE; _LZ77 (planned)
    const u8  *frame_times;   // frames each animation frame (or step) shows, or NULL
    const u8  *frame_order;   // steps: frame index | SPRITE_FRAME_FLIP_H/V, or NULL
} SpriteAsset;                // 20 bytes

typedef struct {
    const u16 *sprite_ids;    // sprite table indices of the group's sprites; NULL: 0..sprite_count-1
    const u16 *palettes;      // palette_count banks of 16 colors, color 0 transparent
    u8  sprite_count, palette_count;
    u8  flags;                // 0 (SPRITE_GROUP_RESIDENT); SPRITE_GROUP_STREAMED (planned)
    u8  slots;                // streamed groups (planned): frames on screen at once; else 0
} SpriteGroup;                // 12 bytes
```

**`SpritePiece`** (10 bytes):

| Offset | Field | Meaning |
| --- | --- | --- |
| 0, 2 | `s16 x, y` | The piece's center relative to the metasprite's pivot, unflipped and unrotated |
| 4 | `u16 sprite` | Sprite ID of an ordinary sprite (not a metasprite) in the table; refused otherwise |
| 6 | `u16 flags` | Draw flags for this piece: `SPRITE_FLIP_H` (bit 0), `SPRITE_FLIP_V` (bit 1), `SPRITE_PALETTE(n)` (bits 8-11), `SPRITE_BLEND` (bit 12, *planned*: drawn opaque, *warns*). **Every other bit is refused**: the layer (bits 2-3), `SPRITE_HIDDEN`, `SPRITE_SCALED`, `SPRITE_SCREEN` and the animation flips belong to the whole draw, and bits 13-14 are reserved |
| 8 | `u8 frame` | A frame of that sprite; refused if it doesn't have it |
| 9 | (padding) | Reserved: leave it zero |

**`SpriteAsset`** (20 bytes):

| Offset | Field | Meaning |
| --- | --- | --- |
| 0 | `tiles` / `pieces` | Pixel data: 4bpp 8x8 tiles, 8 words each (low nibble = leftmost pixel), frame after frame, each frame's tiles row by row (1D mapping); required (refused if NULL or invalid). Metasprites: `piece_count` `SpritePiece`s per frame, frame after frame |
| 4 | `u8 size` | `SPRITE_8x8` (1) to `SPRITE_32x64` (12), [below](#api); required except for metasprites (refused if 0 or past 12); 13-255 reserved |
| 5 | `u8 tiles_per_frame` / `piece_count` | 0: computed from `size` (the usual case); if set, at least what `size` needs (refused otherwise; more leaves padding between frames). Metasprites: pieces per frame, at least 1 |
| 6 | `u8 frame_count` | Animation frames; 0 means 1 |
| 7 | `u8 order_length` | `frame_order` entries (steps); at least 1 if `frame_order` is set; 0: no sequence (a `frame_order` is then ignored, *warns*). Non-zero without a `frame_order`: nothing plays (*warns*) |
| 8 | `u8 palette_slot` | Which of its group's palettes it uses: below the group's `palette_count` (refused otherwise). Not used by metasprites |
| 9, 10 | `s8 origin_x, origin_y` | Drawn position = (x, y) − origin (metasprites: where the pivot is drawn) |
| 11 | `u8 flags` | Below |
| 12 | `frame_times` | One byte per frame (per step with `frame_order`): display frames it shows, 1-255; 0 holds it. NULL: one display frame each |
| 16 | `frame_order` | `order_length` steps: a frame index (bits 0-5) \| `SPRITE_FRAME_FLIP_H` (bit 6) \| `SPRITE_FRAME_FLIP_V` (bit 7). NULL: frames 0 to `frame_count` − 1 |

`SpriteAsset.flags`:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | (reserved) | Refused. It was `SPRITE_ASSET_STREAMED` before 1.0: residency is a group's (`SPRITE_GROUP_STREAMED`) |
| 1 | `SPRITE_ASSET_METASPRITE` | Made of other sprites: `.pieces` ([API](#api)) |
| 2 | `SPRITE_ASSET_ANIM_ONCE` | `sys_animate` stops on the last frame or step instead of looping |
| 3 | `SPRITE_ASSET_LZ77` | *Planned*: `.tiles` LZ77-compressed ([above](#lz77-compression)). Refused until implemented |
| 4-7 | (reserved) | Refused |

**`SpriteGroup`** (12 bytes):

| Offset | Field | Meaning |
| --- | --- | --- |
| 0 | `sprite_ids` | `sprite_count` sprite IDs (refused if one isn't in the table); NULL: IDs 0 to `sprite_count` − 1 |
| 4 | `palettes` | `palette_count` banks of 16 colors (`Color`, BGR555; color 0 transparent); required when `palette_count` is not 0 |
| 8 | `u8 sprite_count` | Sprites in the group |
| 9 | `u8 palette_count` | Palettes in the group; refused when more than the free banks (16 in all) |
| 10 | `u8 flags` | Bit 0: `SPRITE_GROUP_STREAMED` (*planned*, refused until implemented). Bits 1-7 reserved, refused. 0 (`SPRITE_GROUP_RESIDENT`): resident |
| 11 | `u8 slots` | Streamed groups (*planned*): frames on screen at once, each slot the size of the group's largest frame; 0 means 1. Must be 0 in a resident group (refused otherwise) |

**Draw flags** (`u16`: the `flags` of `sprite_draw*()`, the entities' `spr_flags` in [ecs.md](ecs.md), and, the subset above, `SpritePiece.flags`). Every bit has an owner:

| Bits | Name | Meaning |
| --- | --- | --- |
| 0, 1 | `SPRITE_FLIP_H`, `SPRITE_FLIP_V` | Flips |
| 2-3 | `SPRITE_ABOVE_FOREGROUND` (3), `SPRITE_ABOVE_HUD` (2), `SPRITE_BEHIND_PLAYFIELD` (1) | Layer; 0 is the default, between the foreground and the playfield |
| 4 | `SPRITE_HIDDEN` | Not drawn |
| 5, 6 | `SPRITE_ANIM_FLIP_H`, `_V` | Set by `sys_animate`; drawing ignores them |
| 7 | `SPRITE_SCALED` | Entities: scaled by `spr_scale`; `sprite_draw*()` ignores it |
| 8-11 | `SPRITE_PALETTE(n)` | Palette `n` (0-14) of the group, stored as `n` + 1; 0: the sprite's own |
| 12 | `SPRITE_BLEND` | *Planned*: semi-transparent ([above](#alpha-blending)); drawn opaque until implemented (*warns*) |
| 13, 14 | (reserved) | The engine's: mosaic and the object window, later. Not checked per draw: games must not use them |
| 15 | `SPRITE_SCREEN` | Entities: screen coordinates; `sprite_draw*()` ignores it |

Format changes before 1.0: `order_length` and `frame_order` were added for animation sequences ([Animation](#animation)), and `tiles_per_frame` moved before `frame_count`, so `frame_count` and `order_length` are adjacent and `sys_animate` reads both with one load (the struct grew from 16 to 20 bytes). Metasprites added `SpritePiece` and the `pieces`/`piece_count` names, sharing storage with `tiles`/`tiles_per_frame`. The API freeze made bit 0 of `SpriteAsset.flags` (`SPRITE_ASSET_STREAMED`) reserved, added `SpriteGroup.slots` in what was padding (size unchanged), and made the loader refuse every reserved bit and value above.

**Sprite IDs** are indices into a project-wide sprite table (`const SpriteAsset *const table[]`) emitted by the build. IDs rather than pointers keep the sprite component compact and keep addresses out of bytecode ([vm.md](vm.md)); groups list their sprites by ID for the same reason. The table and the sprites must stay valid while registered (normally const data in ROM); a group need not stay valid after it loads.

The sprite component stores `(sprite_id, frame)` plus per-entity draw flags (`spr_flags`: flip, layer, palette, hidden, screen-space). The render systems resolve it to a VRAM tile index through the group's load offset (and, for streamed groups once implemented, through the frame cache's slot for that sprite and frame).

## API

`include/serval/sprites.h`:

```c
void sprite_table_set(const SpriteAsset *const *table, u16 count);   // unloads everything
bool sprite_group_load(const SpriteGroup *group);   // false if VRAM/palettes run out, data is
                                                    // incomplete or holds an unknown value
void sprite_groups_reset(void);                     // unload all, forget every mark
u32  sprite_groups_mark(void);                      // a point in the load order
void sprite_groups_release(u32 mark);               // unload the groups loaded since it
void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
                                                    // SPRITE_FLIP_H/V, layer flags, SPRITE_HIDDEN,
                                                    // SPRITE_PALETTE(n), SPRITE_BLEND (planned)
void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags);
void sprite_draw_ex(u16 sprite_id, u8 frame, int x, int y, u16 angle,
                    FIXED scale_x, FIXED scale_y, u16 flags);   // rotated and scaled
SpriteStats sprite_stats(void);                     // last frame: drawn, matrices, dropped
void sprite_stats_scanlines(bool on);               // also the per-scanline budget
void sprite_set_tiles(u16 sprite_id, u8 frame, const u32 *tiles);       // SPRITE_MAX_TILE_UPDATES

// Planned (SERVAL_PLANNED): warn at compile time, do nothing yet
void sprite_set_colors(u16 sprite_id, u32 index, const Color *colors, u32 count);
```

Planned constants: `SPRITE_GROUP_STREAMED`, `SPRITE_ASSET_LZ77`, `SPRITE_BLEND` (enumerators, so they warn at every use too).

Sizes: `SPRITE_8x8`, `SPRITE_16x16`, `SPRITE_32x32`, `SPRITE_64x64` (square), `SPRITE_16x8`, `SPRITE_32x8`, `SPRITE_32x16`, `SPRITE_64x32` (wide), `SPRITE_8x16`, `SPRITE_8x32`, `SPRITE_16x32`, `SPRITE_32x64` (tall). `SPRITE_MAX` (512) sprite IDs per table. Tile data is 4 bits per pixel, 8 words per 8x8 tile (low nibble = leftmost pixel); for sprites larger than 8x8, tiles are row by row (1D mapping).

**Rotation:** `sprite_draw_rotated(id, frame, x, y, angle, flags)`, or a non-zero `spr_angle` with `sys_render`, rotates a sprite around its center with the hardware's affine mode (double size, so corners aren't clipped). The 32 rotation matrices are allocated per frame and shared by sprites with the same angle and flips; past 32, sprites are drawn unrotated and debug builds warn. Off-screen sprites take no matrix, and angle 0 draws like `sprite_draw` (no matrix). Unrotated sprites don't pay for rotation support beyond one check per entity (bunnymark: +1,530 cycles for 128 sprites).

**Scaling:** `sprite_draw_ex(id, frame, x, y, angle, scale_x, scale_y, flags)` also scales around the center, along the art's own axes: `FX_ONE` is normal size, a negative scale mirrors (a card flip is `scale_x` going from `FX_ONE` through 0 to `-FX_ONE`), 0 draws nothing. The matrix is the inverse of the transform (rotate back, divide by the scales: one division per scaled axis of a new matrix, at most 64 a frame), and matrices are shared by draws with the same angle, flips and scales, so animate scales in steps (a shrinking effect in sixteenths of its size, as the shooter's cannon debris does) rather than giving every sprite its own. Rotated or enlarged sprites use the double-size box: art grown beyond it is cut off (scale 2 unrotated, about 1.4 at 45°). Sprites that are only shrunk or mirrored (angle 0, scales within ±`FX_ONE`) are drawn in plain affine mode, in their own box, which costs half the per-scanline time of the double-size box. For entities, `SPRITE_SCALED` in `spr_flags` draws them scaled by `spr_scale` (one scale for both axes, 256ths). It is a flag rather than a non-zero `spr_scale` because reading `spr_scale` for every entity cost bunnymark \~1,000 cycles (the render loop is out of registers); the flag joins the existing test of `spr_angle` and the flags, so unscaled sprites pay nothing measurable (\~+75). Debug builds warn about a `spr_scale` set without the flag.

**Limits made visible:** `sprite_stats()` returns the last frame's counts: hardware sprites `drawn` (of 128) and `matrices` used (of 32), draws `dropped` because OAM was full, and rotated or scaled draws shown `untransformed` because the matrices ran out. Counted in release builds too, only on the rare paths, so it costs nothing in the usual case; debug builds also warn once per problem. `sprite_stats_scanlines(true)` adds the per-scanline budget: `frame_end()` walks the frame's OAM in order, adding each sprite's cost to the lines it covers (1,210 cycles a line, 954 with DISPCNT's "H-Blank interval free"; an ordinary sprite costs its width, an affine one 10 + 2 × its box's width; sprites entirely off screen cost nothing, as in mGBA), and reports `cut_short` (sprites missing from a line that ran out) and `busiest_line` (the cycles the busiest line asked for). The web renderer draws by the same rules, so what it leaves out is what this counts. It walks every line of every sprite, a few thousand cycles for a busy screen, so it is off by default; Shmup turns it on with its debug readout, which shows the lost draws of all three kinds.

**Metasprites:** a `SpriteAsset` with `SPRITE_ASSET_METASPRITE` is made of pieces, `SpritePiece {s16 x, y; u16 sprite; u16 flags; u8 frame}`: frames of ordinary sprites placed by their centers relative to the metasprite's pivot, the point drawn at (x, y) − origin. `.pieces` and `.piece_count` (pieces per frame) share their storage with `.tiles` and `.tiles_per_frame` (anonymous unions), so the struct and its layout are unchanged. A metasprite is drawn, flipped, rotated, scaled, animated (`sys_animate` steps its frames, each its own list of pieces) and depth-sorted as one sprite or entity. Rotating or scaling it turns and scales each piece's offset about the pivot and draws the piece with the same transform, so the pivot can be anywhere: Shmup's cannons turn about the dome while their barrels reach 39 pixels out, beyond what a 32x32 sprite turning about its own center could (16). Whole flips mirror the offsets and toggle each piece's flips; the draw's palette replaces the pieces' own (`SPRITE_PALETTE` in `spr_flags` recolors every piece: Breakout's glowing paddle); a piece blends when the draw or the piece has `SPRITE_BLEND` (planned). A piece's flags may hold only flips, `SPRITE_PALETTE(n)` and `SPRITE_BLEND`; anything else is refused at load ([ROM data format](#rom-data-format)). Pieces are drawn in order, the first in front, each taking a hardware sprite; pieces with the same flips share a rotation matrix. A metasprite takes no VRAM; the sprites its pieces use must be loaded to be drawn.

Ordinary sprites pay nothing measurable for it in `sys_render_by_depth` and bunnymark: a metasprite's draw data has no frames, so the drawing path's existing "frame out of range" test rejects it, and the rejection path, out of line in ROM, draws its pieces before it would report a bad frame. Plain `sys_render` pays about 5 cycles per sprite (88 sprites: 16,578 → 17,018) for having that call in its loop: it needs registers kept across a call. The piece math (offsets turned and scaled in 64 bits, with `fx_sin`/`fx_cos`) runs from ROM, once per piece.

**Layering:** by default sprites draw between the foreground (BG1) and the playfield (BG2), so the HUD (BG0) stays on top. `SPRITE_ABOVE_FOREGROUND`, `SPRITE_ABOVE_HUD` and `SPRITE_BEHIND_PLAYFIELD` move a sprite to another layer (see [tilemaps.md](tilemaps.md#default-layer-roles)).

`sprite_draw` takes screen coordinates; `sys_render` and `sys_render_by_depth` draw entities at their world position minus the camera ([runtime-systems.md](runtime-systems.md#camera)), except those with `SPRITE_SCREEN` in `spr_flags`, drawn at their position on the screen. `sprite_draw` subtracts the sprite's origin, skips sprites that are fully off screen (so they don't use one of the 128 hardware sprites), and does nothing for sprites that are not loaded. `sprite_group_load()` checks the whole group first (NULL pointers, missing tiles or palettes, sizes, palette slots, metasprite pieces, flags and every reserved value, planned features, VRAM and banks) and loads nothing if any of it is wrong.

## Animation

`frame_times` holds one byte per animation frame: how many display frames (1/60 s, 1-255) it shows. 0 holds that frame (the animation stops there); `NULL` shows each frame for one display frame. The build tooling emits it from the art's frame durations. `SPRITE_ASSET_ANIM_ONCE` plays the animation once and stays on the last frame (death, a door opening); without it, animations loop.

**Sequences:** `frame_order` (with `order_length` entries, the steps) makes the animation play frames in any order, repeating or reversing them, so a ping-pong cycle or a frame shown twice needs its tiles only once. Each entry is a frame index (0-63) optionally `| SPRITE_FRAME_FLIP_H` and/or `| SPRITE_FRAME_FLIP_V`, which draws that frame mirrored: a spinning coin or gem stores half its turn, tumbling debris one frame. `frame_times` then has one entry per step, and `SPRITE_ASSET_ANIM_ONCE` stops on the last step. `order_length` is what turns a sequence on: 0 plays frames 0 to `frame_count` − 1 as before. Example: `{0, 1, 2, 1 | SPRITE_FRAME_FLIP_H}` with `order_length = 4` and `frame_count = 3`.

`sys_animate()` ([ecs.md](ecs.md)) plays them for entities with `C_SPR | C_ANIM`: `spr_anim_time` counts the display frames the current `spr_frame` has shown, and when it reaches the frame's time, `spr_frame` advances and the count restarts. Switching animation (say from run to jump) is a different sprite ID: set `spr_id`, `spr_frame = 0` and `spr_anim_time = 0`. A frame the sprite doesn't have (a forgotten `spr_frame = 0`) restarts the animation, with a warning in debug builds. For a sprite with a sequence, `spr_anim_step` is where the animation is (the step; with `SPRITE_ASSET_ANIM_ONCE`, `spr_anim_step == order_length − 1` means it is over), and `sys_animate` sets `spr_frame` to the step's frame every call, so `spr_frame` is always the frame drawn and the render systems don't need to know about sequences. Start one with `spr_anim_step = 0` and `spr_anim_time = 0` (any step can be a starting point, e.g. to put pieces out of phase); a step past the end restarts it (*warns*), and an entry naming a frame the sprite doesn't have shows frame 0 (*warns*). Flips: the step's flips are XORed with the game's own in `spr_flags`, so a sprite the game faces left still spins; `sys_animate` records the flips it applied in `SPRITE_ANIM_FLIP_H`/`_V` (bits 5-6, which drawing ignores) and swaps them for the next step's. A game that assigns `spr_flags` whole, toggles a flip with `^=`, or doesn't touch flips gets the right result; setting or clearing a flip bit with `|=`/`&= ~` while the step has that flip applied inverts it until the next assignment. Sequences cost about twice as much per entity as plain animations (out-of-line path); sprites without one pay about 3 cycles each for the check (128 animated entities: 26,112 → 26,548 cycles per `sys_animate` call; with sequences about 49,000).

Animation is independent of VRAM residency: all frames of a resident sprite are in VRAM already, so stepping frames costs nothing but the frame index. (A streamed sprite's new frame, once streaming is implemented, is copied when first drawn.)

**Screen-space entities:** `SPRITE_SCREEN` (bit 15 of `spr_flags`) makes the render systems ignore the camera for that entity: its position is a screen position. In a vertical shooter the ship, enemies and bullets stay on the screen while the camera scrolls the map (and the turrets on it, which stay world-space); a HUD icon can be an entity too. Collision between entities follows it: `body_overlap()` and `body_hit_side()` (`physics.h`), and so `vm_collide()`, add the camera to a screen-space entity's position when they compare it with a world one, so the ship's shots hit the turrets. `sprite_draw` ignores the flag (it always takes screen coordinates). It costs nothing measurable: the camera is loaded only for world-space entities, so one test replaces two loads (bunnymark: +5 cycles for 128 sprites).

**Palettes:** `SPRITE_PALETTE(n)` (bits 8-11 of the flags hold `n + 1`, so 0 keeps the asset's own palette) draws with palette `n` of the sprite's group: bank = the group's first bank + `n`, replacing the asset's `palette_slot`. It works with `sprite_draw`, `sprite_draw_rotated`, `sprite_draw_ex` and both render systems, and keeps the frame, flips and layer. A palette the group doesn't have draws with the sprite's own (*warns* once). To switch it on an entity, `spr_flags[i] = (spr_flags[i] & ~SPRITE_PALETTE_MASK) | SPRITE_PALETTE(n)`. The render systems send entities with a palette down the out-of-line path that also takes rotated, scaled, hidden and blended sprites (one test for all of them, so other sprites don't pay); such a sprite costs about 60 cycles more to draw than one without (about 180 for a plain sprite in `sys_render`), e.g. 5,000 cycles for 84 bricks, while sprites without one pay nothing measurable.

**Blinking:** `SPRITE_HIDDEN` in the draw flags (or `spr_flags`) draws nothing and uses no hardware sprite. The render systems test it together with `spr_angle`, sending hidden sprites down the out-of-line rotated path, so the check costs one instruction per entity (bunnymark: about +115 cycles for 128 sprites).

## Implemented so far

Resident, uncompressed groups with 4bpp sprites and metasprites, regular, rotated or scaled, animated by `sys_animate()` (in frame order or by a `frame_order` sequence with per-step flips), hideable, drawn with any palette of their group, and entities in world or screen coordinates; tiles and palette banks are bump-allocated in load order and released in layers by marks (`sprite_groups_mark()`, `sprite_groups_release()`) or all at once; `sprite_group_load()` refuses every reserved bit and value of the data formats; a loaded sprite's frames rewritten at run time (`sprite_set_tiles()`, copied in VBlank).

**Planned** (declared, implemented in 1.x; each use warns at compile time, and at run time each does nothing harmful and warns once): streamed groups (`SPRITE_GROUP_STREAMED`, `SpriteGroup.slots`; refused by the loader), LZ77-compressed sprites (`SPRITE_ASSET_LZ77`; refused), palette writes through a shadow palette (`sprite_set_colors()`; does nothing), alpha-blended sprites (`SPRITE_BLEND`; drawn opaque).

**After 1.0, no API:** palette sharing with reference counting, VRAM defragmentation, loading in forced blank, and the draw flags reserved in bits 13-14 (mosaic, the object window).
