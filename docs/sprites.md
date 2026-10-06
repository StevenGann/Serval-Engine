# Sprites and asset management

Assets stay in memory-mapped ROM; the engine only manages what is resident in VRAM and palette RAM. Sprites are organized into GameMaker-style groups, packed at build time by the tooling.

**Status:** resident, uncompressed groups of 4bpp sprites and metasprites, drawn regular, rotated or scaled, with any palette of their group, in world or screen coordinates, are implemented (reference: [api-reference.md](api-reference.md#spritesh)). Streamed sprites, LZ77 groups, palette sharing, the shadow palette and the global/room watermark are planned; see [Implemented so far](#implemented-so-far).

**Hardware budget:** OBJ VRAM is 32 KB (1024 4bpp tiles; 16 KB in bitmap modes) with 16 OBJ palette banks of 16 colors.

## Residency modes

Set per group. Only Resident is implemented, without LZ77.

| Mode | How it works | Best for | Compression |
| --- | --- | --- | --- |
| Resident | All frames copied to VRAM at room start | Small enemies, pickups, HUD | LZ77 allowed (BIOS `LZ77UnCompVram`) |
| Streamed | One-frame VRAM slot per instance; new frame DMA'd in VBlank | Large, heavily animated characters | Must be uncompressed |

## VRAM allocation

**Planned design.** Today tiles are bump-allocated from tile 0 in load order and only `sprite_groups_reset()` frees them.

```
OBJ VRAM tile 0 ─────────────────────────────── tile 1023
[ global groups | room groups →      ← streamed slots ]
```

- Global groups (player, HUD, common FX) load once at boot.
- Room groups use a bump allocator reset to the global watermark on each room change.
- Streamed slots allocate downward from the top, with per-OBJ-size free lists.
- Room loads run during a fade with forced blank (`REG_DISPCNT` bit 7) for unrestricted VRAM access.

## Palettes

**Planned design.** Today each loaded group takes the next `palette_count` OBJ palette banks, written immediately; there is no sharing or shadow palette.

**Implemented: choosing a palette per draw.** A sprite draws with its asset's `palette_slot`, or with `SPRITE_PALETTE(n)` in its draw flags (`spr_flags` for entities) with palette `n` (0-14) of its group: a white hit flash, a damaged or enraged color, the eight colors of one brick, without a copy of the sprite per color. The art and its color variants share tiles; only the group's palette list grows. See [API](#api).

The tooling assigns each group logical banks. A runtime palette manager maps them to physical banks with reference counting, so identical palettes share one bank. A shadow palette is copied in VBlank, enabling fades, flashes and swaps.

## ROM data format

Emitted as constant C tables by the build tooling.

```c
typedef struct {
    s16 x, y;                 // the piece's center, relative to the metasprite's pivot
    u16 sprite;               // an ordinary sprite's ID
    u16 flags;                // SPRITE_FLIP_H/V, SPRITE_PALETTE(n)
    u8  frame;
} SpritePiece;

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
    u8  palette_slot;         // logical bank within group
    s8  origin_x, origin_y;   // drawn position = (x, y) - origin
    u8  flags;                // SPRITE_ASSET_ANIM_ONCE, METASPRITE; STREAMED (planned)
    const u8  *frame_times;   // frames each animation frame (or step) shows, or NULL
    const u8  *frame_order;   // steps: frame index | SPRITE_FRAME_FLIP_H/V, or NULL
} SpriteAsset;
```

Format change (before the first release): `order_length` and `frame_order` were added for animation sequences ([Animation](#animation)), and `tiles_per_frame` now comes before `frame_count`, so `frame_count` and `order_length` are adjacent and `sys_animate` reads both with one load. The struct grew from 16 to 20 bytes. Emit and write it with designated initializers (field order then doesn't matter); zero/`NULL` for both new fields keeps the old behaviour. Metasprites added `SpritePiece` and the `pieces`/`piece_count` names, sharing storage with `tiles`/`tiles_per_frame`: the layout and size are unchanged, and existing data stays valid.

```c
typedef struct {
    const u16 *sprite_ids;    // sprite table indices of the group's sprites; NULL: 0..sprite_count-1
    const u16 *palettes;      // palette_count banks of 16 colors, color 0 transparent
    u8  sprite_count, palette_count;
    u8  flags;                // SPRITE_GROUP_RESIDENT (0, default); SPRITE_GROUP_STREAMED (planned)
} SpriteGroup;
```

Fields left out of a C initializer take usable defaults, so a hand-written sprite needs only `.size` and `.tiles`, and a group only its palettes and counts. The engine computes how many tiles a group needs; `size` names the hardware dimensions directly (e.g. `SPRITE_16x16`), replacing the hardware's separate shape and size fields.

**Sprite IDs** are indices into a project-wide sprite table (`const SpriteAsset *const table[]`) emitted by the build. IDs rather than pointers keep the sprite component compact and keep addresses out of bytecode ([vm.md](vm.md)); groups list their sprites by ID for the same reason.

The sprite component stores `(sprite_id, frame)` plus per-entity draw flags (`spr_flags`: flip, layer, palette, hidden, screen-space). The render systems resolve it to a VRAM tile index through the group's load offset (and, once streaming exists, the instance's streamed slot).

## API

`include/serval/sprites.h`:

```c
void sprite_table_set(const SpriteAsset *const *table, u16 count);
bool sprite_group_load(const SpriteGroup *group);   // false if VRAM/palettes run out or data is incomplete
void sprite_groups_reset(void);                     // unload all
void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
                                                    // SPRITE_FLIP_H/V, layer flags,
                                                    // SPRITE_HIDDEN, SPRITE_PALETTE(n)
void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags);
void sprite_draw_ex(u16 sprite_id, u8 frame, int x, int y, u16 angle,
                    FIXED scale_x, FIXED scale_y, u16 flags);   // rotated and scaled
SpriteStats sprite_stats(void);                     // last frame: drawn, matrices, dropped
```

Sizes: `SPRITE_8x8`, `SPRITE_16x16`, `SPRITE_32x32`, `SPRITE_64x64` (square), `SPRITE_16x8`, `SPRITE_32x8`, `SPRITE_32x16`, `SPRITE_64x32` (wide), `SPRITE_8x16`, `SPRITE_8x32`, `SPRITE_16x32`, `SPRITE_32x64` (tall). `SPRITE_MAX` (512) sprite IDs per table. Tile data is 4 bits per pixel, 8 words per 8x8 tile (low nibble = leftmost pixel); for sprites larger than 8x8, tiles are row by row (1D mapping).

**Rotation:** `sprite_draw_rotated(id, frame, x, y, angle, flags)`, or a non-zero `spr_angle` with `sys_render`, rotates a sprite around its center with the hardware's affine mode (double size, so corners aren't clipped). The 32 rotation matrices are allocated per frame and shared by sprites with the same angle and flips; past 32, sprites are drawn unrotated and debug builds warn. Off-screen sprites take no matrix, and angle 0 draws like `sprite_draw` (no matrix). Unrotated sprites don't pay for rotation support beyond one check per entity (bunnymark: +1,530 cycles for 128 sprites).

**Scaling:** `sprite_draw_ex(id, frame, x, y, angle, scale_x, scale_y, flags)` also scales around the center, along the art's own axes: `FX_ONE` is normal size, a negative scale mirrors (a card flip is `scale_x` going from `FX_ONE` through 0 to `-FX_ONE`), 0 draws nothing. The matrix is the inverse of the transform (rotate back, divide by the scales: one division per new matrix, at most 32 a frame), and matrices are shared by draws with the same angle, flips and scales, so animate scales in steps (a shrinking effect in sixteenths of its size, as the shooter's cannon debris does) rather than giving every sprite its own. Rotated or enlarged sprites use the double-size box: art grown beyond it is cut off (scale 2 unrotated, about 1.4 at 45°). Sprites that are only shrunk or mirrored (angle 0, scales within ±`FX_ONE`) are drawn in plain affine mode, in their own box, which costs half the per-scanline time of the double-size box. For entities, `SPRITE_SCALED` in `spr_flags` draws them scaled by `spr_scale` (one scale for both axes, 256ths). It is a flag rather than a non-zero `spr_scale` because reading `spr_scale` for every entity cost bunnymark ~1,000 cycles (the render loop is out of registers); the flag joins the existing one-instruction test of `spr_angle` and the flags, so unscaled sprites pay nothing measurable (~+75). Debug builds warn about a `spr_scale` set without the flag.

**Limits made visible:** `sprite_stats()` returns the last frame's counts: hardware sprites `drawn` (of 128) and `matrices` used (of 32), draws `dropped` because OAM was full, and rotated or scaled draws shown `untransformed` because the matrices ran out. Counted in release builds too, only on the rare paths, so it costs nothing in the usual case; debug builds also warn once per problem. `sprite_stats_scanlines(true)` adds the per-scanline budget: `frame_end()` walks the frame's OAM in order, adding each sprite's cost to the lines it covers (1,210 cycles a line, 954 with DISPCNT's "H-Blank interval free"; an ordinary sprite costs its width, an affine one 10 + 2 × its box's width; sprites entirely off screen cost nothing, as in mGBA), and reports `cut_short` (sprites missing from a line that ran out) and `busiest_line` (the cycles the busiest line asked for). The web renderer draws by the same rules, so what it leaves out is what this counts. It walks every line of every sprite, a few thousand cycles for a busy screen, so it is off by default; Shmup turns it on with its debug readout, which shows the lost draws of all three kinds.

**Metasprites:** a `SpriteAsset` with `SPRITE_ASSET_METASPRITE` is made of pieces, `SpritePiece {s16 x, y; u16 sprite; u16 flags; u8 frame}`: frames of ordinary sprites placed by their centers relative to the metasprite's pivot, the point drawn at (x, y) − origin. `.pieces` and `.piece_count` (pieces per frame) share their storage with `.tiles` and `.tiles_per_frame` (anonymous unions), so the struct and its layout are unchanged. A metasprite is drawn, flipped, rotated, scaled, animated (`sys_animate` steps its frames, each its own list of pieces) and depth-sorted as one sprite or entity. Rotating or scaling it turns and scales each piece's offset about the pivot and draws the piece with the same transform, so the pivot can be anywhere: Shmup's cannons turn about the dome while their barrels reach 39 pixels out, beyond what a 32x32 sprite turning about its own center could (16). Whole flips mirror the offsets and toggle each piece's flips; the draw's palette replaces the pieces' own (`SPRITE_PALETTE` in `spr_flags` recolors every piece: Breakout's glowing paddle). Pieces are drawn in order, the first in front, each taking a hardware sprite; pieces with the same flips share a rotation matrix. A metasprite takes no VRAM; the sprites its pieces use must be loaded to be drawn.

Ordinary sprites pay nothing measurable for it in `sys_render_by_depth` and bunnymark: a metasprite's draw data has no frames, so the drawing path's existing "frame out of range" test rejects it, and the rejection path, out of line in ROM, draws its pieces before it would report a bad frame. Plain `sys_render` pays about 5 cycles per sprite (88 sprites: 16,578 → 17,018) for having that call in its loop: it needs registers kept across a call. The piece math (offsets turned and scaled in 64 bits, with `fx_sin`/`fx_cos`) runs from ROM, once per piece.

**Layering:** by default sprites draw between the foreground (BG1) and the playfield (BG2), so the HUD (BG0) stays on top. `SPRITE_ABOVE_FOREGROUND`, `SPRITE_ABOVE_HUD` and `SPRITE_BEHIND_PLAYFIELD` move a sprite to another layer (see [tilemaps.md](tilemaps.md#default-layer-roles)).

`sprite_draw` takes screen coordinates; `sys_render` and `sys_render_by_depth` draw entities at their world position minus the camera ([runtime-systems.md](runtime-systems.md#camera)), except those with `SPRITE_SCREEN` in `spr_flags`, drawn at their position on the screen. `sprite_draw` subtracts the sprite's origin, skips sprites that are fully off screen (so they don't use one of the 128 hardware sprites), and does nothing for sprites that are not loaded. `sprite_group_load()` checks the whole group first (NULL pointers, missing tiles or palettes, sizes, palette slots, VRAM and banks) and loads nothing if any of it is wrong.

## Animation

`frame_times` holds one byte per animation frame: how many display frames (1/60 s, 1-255) it shows. 0 holds that frame (the animation stops there); `NULL` shows each frame for one display frame. The build tooling emits it from the art's frame durations. `SPRITE_ASSET_ANIM_ONCE` plays the animation once and stays on the last frame (death, a door opening); without it, animations loop.

**Sequences:** `frame_order` (with `order_length` entries, the steps) makes the animation play frames in any order, repeating or reversing them, so a ping-pong cycle or a frame shown twice needs its tiles only once. Each entry is a frame index (0-63) optionally `| SPRITE_FRAME_FLIP_H` and/or `| SPRITE_FRAME_FLIP_V`, which draws that frame mirrored: a spinning coin or gem stores half its turn, tumbling debris one frame. `frame_times` then has one entry per step, and `SPRITE_ASSET_ANIM_ONCE` stops on the last step. `order_length` is what turns a sequence on: 0 plays frames 0 to `frame_count` − 1 as before. Example: `{0, 1, 2, 1 | SPRITE_FRAME_FLIP_H}` with `order_length = 4` and `frame_count = 3`.

`sys_animate()` ([ecs.md](ecs.md)) plays them for entities with `C_SPR | C_ANIM`: `spr_anim_time` counts the display frames the current `spr_frame` has shown, and when it reaches the frame's time, `spr_frame` advances and the count restarts. Switching animation (say from run to jump) is a different sprite ID: set `spr_id`, `spr_frame = 0` and `spr_anim_time = 0`. A frame the sprite doesn't have (a forgotten `spr_frame = 0`) restarts the animation, with a warning in debug builds. For a sprite with a sequence, `spr_anim_step` is where the animation is (the step; with `SPRITE_ASSET_ANIM_ONCE`, `spr_anim_step == order_length − 1` means it is over), and `sys_animate` sets `spr_frame` to the step's frame every call, so `spr_frame` is always the frame drawn and the render systems don't need to know about sequences. Start one with `spr_anim_step = 0` and `spr_anim_time = 0` (any step can be a starting point, e.g. to put pieces out of phase); a step past the end restarts it (*warns*), as does an entry naming a frame the sprite doesn't have (shows frame 0, *warns*). Flips: the step's flips are XORed with the game's own in `spr_flags`, so a sprite the game faces left still spins; `sys_animate` records the flips it applied in `SPRITE_ANIM_FLIP_H`/`_V` (bits 5-6, which drawing ignores) and swaps them for the next step's. A game that assigns `spr_flags` whole, toggles a flip with `^=`, or doesn't touch flips gets the right result; setting or clearing a flip bit with `|=`/`&= ~` while the step has that flip applied inverts it until the next assignment. Sequences cost about twice as much per entity as plain animations (out-of-line path); sprites without one pay about 3 cycles each for the check (128 animated entities: 26,112 → 26,548 cycles per `sys_animate` call; with sequences about 49,000).

Animation is independent of VRAM residency: all frames of a resident sprite are in VRAM already, so stepping frames costs nothing but the frame index.

**Screen-space entities:** `SPRITE_SCREEN` (bit 15 of `spr_flags`) makes the render systems ignore the camera for that entity: its position is a screen position. In a vertical shooter the ship, enemies and bullets stay on the screen while the camera scrolls the map (and the turrets on it, which stay world-space); a HUD icon can be an entity too. Collision between entities doesn't care, as long as the entities that collide use the same kind of position. `sprite_draw` ignores the flag (it always takes screen coordinates). It costs nothing measurable: the camera is loaded only for world-space entities, so one test replaces two loads (bunnymark: +5 cycles for 128 sprites).

**Palettes:** `SPRITE_PALETTE(n)` (bits 8-11 of the flags hold `n + 1`, so 0 keeps the asset's own palette) draws with palette `n` of the sprite's group: bank = the group's first bank + `n`, replacing the asset's `palette_slot`. It works with `sprite_draw`, `sprite_draw_rotated` and both render systems, and keeps the frame, flips and layer. A palette the group doesn't have draws with the sprite's own (*warns* once). To switch it on an entity, `spr_flags[i] = (spr_flags[i] & ~SPRITE_PALETTE_MASK) | SPRITE_PALETTE(n)`. The render systems send entities with a palette down the out-of-line path that also takes rotated and hidden sprites (one test for the three, so other sprites don't pay); such a sprite costs about 60 cycles more to draw than one without (about 180 for a plain sprite in `sys_render`), e.g. 5,000 cycles for 84 bricks, while sprites without one pay nothing measurable.

**Blinking:** `SPRITE_HIDDEN` in the draw flags (or `spr_flags`) draws nothing and uses no hardware sprite. The render systems test it together with `spr_angle`, sending hidden sprites down the out-of-line rotated path, so the check costs one instruction per entity (bunnymark: +128 cycles for 128 sprites).

## Implemented so far

Resident, uncompressed groups with 4bpp sprites and metasprites, regular, rotated or scaled, animated by `sys_animate()` (in frame order or by a `frame_order` sequence with per-step flips), hideable, drawn with any palette of their group, and entities in world or screen coordinates; tiles and palette banks are bump-allocated in load order. **Not yet:** streamed sprites, LZ77 groups, palette sharing with reference counting, the shadow palette, the global/room watermark, and loading during forced blank.
