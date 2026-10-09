# Tilemaps

Levels are stored as 16×16 metatiles in ROM and streamed into 32×32 ring-buffered screenblocks as the camera scrolls. 1.0 uses regular tiled backgrounds only; affine (Mode 7) is post-1.0.

**Status:** implemented, with planned API declared (`include/serval/map.h`; function reference in [api-reference.md](api-reference.md#maph)).

- **Implemented:** one tileset per room, up to three map layers on BG1-BG3 streamed around the camera (any map size), parallax, wrapping and fixed layers, per-layer scroll offsets (layers that scroll by themselves), the camera ([runtime-systems.md](runtime-systems.md#camera)), runtime cell changes, animated tiles (`tileset_set_tiles()`), map collision for map bodies (solid and one-way metatiles, bounces up to a perfect one), four tag bits per metatile for the game and `map_tags_in()` to find them, and loaders that refuse flags they don't know.
- **Planned**, declared now and implemented in a later minor version (each use compiles with a warning; [releases.md](releases.md#planned-api)): LZ77-compressed tilesets (`TILESET_LZ77`), background palette writes (`tileset_set_colors()`), ladders (`MAP_LADDER`, `MAP_CONTACT_LADDER`) and floor slopes (`MAP_SLOPE_R`, `MAP_SLOPE_L`, `MAP_SLOPE_R_LOW`, `MAP_SLOPE_R_HIGH`, `MAP_SLOPE_L_HIGH`, `MAP_SLOPE_L_LOW`). Raster effects are planned API in `screen.h` ([below](#raster-effects)).
- **Not in the API yet**, and addable later without breaking existing games or data: 8bpp tilesets (a reserved `Tileset.flags` bit), tileset groups, more changed cells than `MAP_MAX_CHANGES`, ceiling slopes and other collision types (the reserved types 10-15).

**Hardware budget:** 64 KB BG VRAM as four 16 KB charblocks overlapping 32 two-kilobyte screenblocks. Regular BG map entries are 16-bit (tile index, H/V flip, palette bank).

## Default layer roles

| BG | Default role |
| --- | --- |
| BG0 | HUD / text box |
| BG1 | Foreground (in front of sprites) |
| BG2 | Main playfield |
| BG3 | Parallax background |

The editor exposes these as room layers, with hardware limits visible.

Each background's hardware priority equals its number (BG0 = 0, ..., BG3 = 3), and sprites default to priority 2, which gives this order from front to back: HUD, foreground, sprites, playfield, parallax background. Sprites can opt into other positions with layer flags ([sprites.md](sprites.md#api)).

World coordinates are pixels from the top-left of the playfield (BG2) map; collision, the camera and entity positions all use them.

## VRAM layout

**Implemented:**

| Region | Holds |
| --- | --- |
| Charblock 0 | Text layer font (BG0, [text.h](api-reference.md#texth)) |
| Charblocks 1-2 | Tileset tiles 0-1023 (`MAP_MAX_TILES`), shared by every map layer (BGxCNT character base 1) |
| Charblock 3 | No tiles: screenblocks 28, 29, 30 (BG1, BG2, BG3 maps, 32×32 entries each) and 31 (text layer map) |
| Charblocks 4-5 | Sprite tiles ([sprites.md](sprites.md)) |
| BG palette banks 0-14 | Tileset palettes (`MAP_MAX_PALETTES`); color 0 of bank 0 is the backdrop, and `tileset_load()` skips color 0 of every bank |
| BG palette bank 15 | Text layer |
| Charblock 0 tiles 384-511, screenblocks 24-27 | Unused today; reserved for the text layer and the engine ([core-api.md](core-api.md#hardware-the-engine-uses)) |

Map layers are 4bpp, 32×32-entry (256×256 pixel) regular backgrounds; `map_load()` sets their BGxCNT (character base 1, their screenblock, priority = BG number) and DISPCNT enable bit at once, and `map_unload()` clears the bit at the next `frame_end()`.

## Metatiles

Four screen entries plus a collision byte. They cut level data about 4× and give collision a natural granularity. Tile deduplication at build time also matches H/V-flipped tiles.

## ROM data format

**Implemented** (`include/serval/map.h`; the editor's build pipeline emits these, hand-written data uses designated initializers):

```c
typedef struct {
    const u32 *tiles;     // 4bpp 8x8 tiles, 8 words each
    u16 tile_count;       // at most MAP_MAX_TILES (1024)
    const u16 *palettes;  // palette_count banks of 16 colors (color 0 transparent)
    u8  palette_count;    // at most MAP_MAX_PALETTES (15), loaded into BG banks 0, 1, ...
    u8  flags;            // TILESET_*; 0: .tiles as above
} Tileset;

typedef struct {
    u16 se[4];            // screen entries: top-left, top-right, bottom-left, bottom-right
                          // MAP_SE(tile, palette, MAP_SE_FLIP_H | MAP_SE_FLIP_V)
    u8  collision;        // collision type (low 4 bits) | MAP_TAG(0..3)
} Metatile;

typedef struct {
    u16 width, height;          // in metatiles
    const u16 *cells;           // width x height metatile indices, row by row
    const Metatile *metatiles;  // the definitions cells index
    u16 metatile_count;
    u8  bg;                     // 1-3; priority = bg
    u8  flags;                  // MAP_LAYER_WRAP, MAP_LAYER_FIXED
    FIXED scroll_factor;        // parallax: layer scroll = camera * factor (0 means FX_ONE)
} MapLayer;
```

Offsets and sizes are the GBA's (and wasm32's: the web build has the same layout), checked at compile time in `src/gba/map.c`. Padding is not read; designated initializers leave it 0.

**`Tileset`**, 16 bytes:

| Offset | Field | Type | Values |
| --- | --- | --- | --- |
| 0 | `tiles` | `const u32*` | `tile_count` tiles of 8 words (32 bytes), 4 bits per pixel, row by row; with `TILESET_LZ77`, LZ77 data instead ([Tilesets](#tilesets)) |
| 4 | `tile_count` | `u16` | 1 to `MAP_MAX_TILES` (1024) |
| 6 | (padding) | 2 bytes | |
| 8 | `palettes` | `const u16*` | `palette_count` × 16 colors; color 0 of each is never loaded. May be NULL if `palette_count` is 0 |
| 12 | `palette_count` | `u8` | 0 to `MAP_MAX_PALETTES` (15) |
| 13 | `flags` | `u8` | Bit 0 `TILESET_LZ77` (planned: refused until implemented); bit 1 reserved for 8bpp tilesets; bits 2-7 reserved. In this version `tileset_load()` refuses a tileset with any bit set (returns false, warns) |
| 14 | (padding) | 2 bytes | |

**`Metatile`**, 10 bytes:

| Offset | Field | Type | Values |
| --- | --- | --- | --- |
| 0 | `se` | `u16[4]` | Screen entries, top-left, top-right, bottom-left, bottom-right: tile index (bits 0-9, into the tileset), H and V flip (bits 10 and 11), palette bank 0-14 (bits 12-15); `MAP_SE(tile, palette, flips)` |
| 8 | `collision` | `u8` | Collision type in bits 0-3 ([Collision types](#collision-types): 0-2 implemented, 3-9 planned, 10-15 reserved); tags `MAP_TAG(0)` to `MAP_TAG(3)` in bits 4-7, the game's ([Tags](#tags)). Only the playfield's are read |
| 9 | (padding) | 1 byte | |

**`MapLayer`**, 20 bytes:

| Offset | Field | Type | Values |
| --- | --- | --- | --- |
| 0 | `width` | `u16` | In metatiles, at least 1 |
| 2 | `height` | `u16` | In metatiles, at least 1 |
| 4 | `cells` | `const u16*` | `width` × `height` metatile indices, row by row. An index of `metatile_count` or more shows and collides as empty (debug builds warn at load) |
| 8 | `metatiles` | `const Metatile*` | The definitions |
| 12 | `metatile_count` | `u16` | At least 1 |
| 14 | `bg` | `u8` | Background 1-3; its priority is its number |
| 15 | `flags` | `u8` | Bit 0 `MAP_LAYER_WRAP`, bit 1 `MAP_LAYER_FIXED`; bits 2-7 reserved: `map_load()` refuses a layer with any of them (returns false, warns) |
| 16 | `scroll_factor` | `FIXED` | Layer scroll = camera × factor (24.8 fixed point); 0 means `FX_ONE`; not used with `MAP_LAYER_FIXED` |

Format additions before the first release: `MAP_LAYER_FIXED` (bit 1 of `MapLayer.flags`) and `Tileset.flags` (in what was padding); data written before them leaves them 0 and is unaffected.

- Tileset tile 0 should be blank: a layer that doesn't wrap shows screen entry 0 outside its map.
- Every layer is streamed, whatever its size, so the draft's `STREAMED` flag is gone; its priority is its BG number; one tileset serves all layers of a room.
- Loaders refuse flag bits they don't know rather than ignoring them: a later version may give a reserved bit a meaning, and data that carried it by mistake would then change behaviour ([releases.md](releases.md#versioning), "What breaks a data format"). Collision types are the exception: [they load](#collision-types), with a warning.

## Streaming

**Implemented** (`src/gba/map.c`):

- Each layer's screenblock is a 32×32 ring buffer: layer tile `(tx, ty)` goes to entry `(tx & 31, ty & 31)`, and the background's scroll registers hold the layer's scroll position (the hardware wraps at 256 pixels too).
- The valid part is the *window*: the 31×21 tiles that a 240×160 screen can touch at the current scroll position. When the camera moves, only tiles entering the window are written: whole columns (21 entries) and rows (31 entries). A move of a whole window or more, a newly loaded layer or `map_load()` redraws the window.
- A layer scrolls to `camera * scroll_factor` (rounded down) plus its offset from `map_set_scroll()` (below). `MAP_LAYER_WRAP` layers repeat their cells in both directions (small parallax backgrounds); other layers show entry 0 outside their map.
- Playfield cells changed with `map_set_cell()` are redrawn where they are in the window; changes outside it appear when they scroll in (streaming reads the changes too).
- `map_load()` draws the new layer's window straight to VRAM and turns the background on (a load-time VRAM write, like `tileset_load()`), so the frame after loading a room already shows it rather than only the backdrop; loading during a fade to black hides the write. Set the camera before loading the playfield.
- After that, everything happens in `frame_end()`, in two steps. Before it waits for VBlank (as CPU work of the frame), entries are written to a copy of each screenblock in EWRAM (6 KB). In VBlank, the changed rows (and columns, written 64 bytes apart) are copied to VRAM, and the scroll and control registers are set. So VRAM writes stay in VBlank, and the VBlank part stays short even for a full redraw. Nothing runs (or links) in games that never call `map_load()` or `tileset_set_tiles()`.
- The entry loops run as ARM code from IWRAM (1.4 KB, only in games that use maps): as Thumb code in ROM they took about 70 cycles per entry, and a full redraw of three layers about 140,000 cycles.

Costs, measured by the test ROM (`tests/rom/map_tests.c`, Release build; VBlank is 83,776 cycles):

| Work | Before VBlank | In VBlank |
| --- | --- | --- |
| Full redraw of three layers (map load, camera jump) | 46,000 cycles (16% of a frame, for one frame) | 22,600 cycles |
| One new row and one new column on each of three layers | 10,800 cycles | 3,960 cycles |
| Autoscroll with `map_set_scroll()`, two layers (one vertical, one sideways), 1 pixel per frame | 1,790 on average, 4,170 at most | 990 on average, 2,370 at most |
| The same at 16 pixels per frame (two rows and two columns a frame) | 5,340 | 2,790 on average, 3,200 at most |

A camera moving less than 8 pixels per frame writes at most one row and one column per layer, usually less.

## Fixed and self-scrolling layers

**Implemented** (`map_set_scroll()`, `MAP_LAYER_FIXED`):

- `map_set_scroll(bg, x, y)` gives each background an offset in pixels, added to where the camera puts the layer: the screen's top-left shows layer pixel `camera * scroll_factor + offset`. The offset goes through the same streaming as camera moves, so it can change every frame at no more cost than scrolling.
- **Layers that scroll by themselves:** a starfield, clouds or a conveyor belt add their speed to the offset every frame. A vertical shooter keeps its camera on the stage (or stops it at the top for the boss) while the starfield drifts on, and never jumps the camera back: with the offset counting on, every frame writes only the rows (or columns) entering the window, at any speed up to a metatile per frame and beyond (a whole window, 31 columns or 21 rows, is the limit before a full redraw). Let the offset count on rather than wrapping it back by the layer's size: a wrapping layer repeats anyway, and the jump back would redraw the window. A 32-bit offset at a pixel per frame lasts over a year.
- **Fixed layers:** `MAP_LAYER_FIXED` ignores the camera: the layer shows its top-left, moved only by its offset. A HUD panel beside the playfield, a frame, a title card. A fixed layer whose offset doesn't change costs nothing per frame (no row or column is written). For a 64-pixel panel on the right of the screen, either draw it at x 176 in a 15-metatile-wide layer, or make the layer 4 metatiles wide and `map_set_scroll(bg, -176, 0)`.
- The offset belongs to the background: `map_load()` keeps it (set it before loading a layer, like the camera, and the load draws the layer there), `map_unload()` resets it to 0. On the playfield (BG2) an offset moves the graphics away from collision and entities, which stay in world coordinates (a map-only shake, say); it doesn't change the camera or its clamp.
- Sprites have the matching flag: entities with `SPRITE_SCREEN` ignore the camera ([sprites.md](sprites.md#api)).

## Tilesets

**Implemented:** `tileset_load()` copies one tileset to charblocks 1-2 and its palettes to BG banks 0-14 immediately (like `sprite_group_load()`), so call it while loading a room, before its layers are shown. It checks the whole tileset first (tiles, counts, palettes, flags) and loads nothing if any of it is wrong.

**Flags** (`Tileset.flags`): `tileset_load()` refuses a tileset with a flag it doesn't know or doesn't implement yet, returning false (debug builds warn) and loading nothing.

- Bit 0, `TILESET_LZ77`, **planned**: `.tiles` holds the tiles compressed with LZ77 in the GBA BIOS's format, a header word with `0x10` in its low byte and the unpacked size in bytes (which must be `tile_count × 32`) in bits 8-31, then the compressed data. The data must be "VRAM-safe": no match copies from the byte just before it (a distance of 1), so that the BIOS's VRAM decoder, which writes 16 bits at a time, can unpack it straight into VRAM. Until implemented, `tileset_load()` refuses it.
- Bit 1: reserved for 8bpp tilesets (256-color tiles, a later addition). Refused.
- Bits 2-7: reserved. Refused.

**Animated tiles:** `tileset_set_tiles(first, tiles, count)` replaces tiles of the loaded tileset, so every cell using them changes at once (water, lava, a shimmering bonus block): the game keeps each animation frame's tiles in ROM and passes the next frame's when it is due. The copies are queued (`MAP_MAX_TILE_UPDATES`, 8 per frame; a second call for the same `first` replaces the first) and done in VBlank at the next `frame_end()`, before the map rows and columns: 32 bytes per tile, so a few dozen tiles per frame fit comfortably beside a full map redraw. Calls outside the tileset, or past the queue, are ignored with a warning in debug builds; `tileset_load()` drops queued updates.

### Palette writes

**Planned:** `tileset_set_colors(index, colors, count)` changes `count` background colors from color `index` on, where `index` = palette × 16 + color and palettes are numbered as in `MAP_SE()` (0-14): water whose colors cycle, a room faded toward dusk with `color_mix()` (`screen.h`), a flash. The semantics, fixed now:

- The colors are copied at the call (`colors` may be a temporary) and reach the screen in VBlank at the next `frame_end()`.
- They go to the engine's own copy of the palettes (the shadow palette), never to the tileset's data: a palette that is const data in ROM, or that another tileset uses too, keeps its colors there. This is the same copy-on-write rule as `sprite_set_colors()` for sprite palettes ([sprites.md](sprites.md#palettes)).
- `tileset_load()` puts the tileset's colors back (colors 1-15 of its `palette_count` palettes), also over writes made before it in the same frame.
- Color 0 of palette 0 is the backdrop, which `screen_set_backdrop()` also sets: writing it changes the backdrop, and `tileset_load()` leaves it alone. Color 0 of the other palettes is transparent: writes to it are kept, not shown.
- Ignored, with a warning in debug builds, if the colors reach past color 239 (palette 15 is the text layer's) or `colors` is NULL.

Until implemented, it changes nothing and warns once (debug builds).

**Later (no API yet): tileset groups**, several tilesets loaded side by side at tile and palette offsets assigned at build time, for rooms that share part of their graphics. That would add `Tileset` fields whose 0 keeps today's layout (loading at tile 0 replaces everything, as now), so it can come in a minor version.

## Collision

Read directly from ROM per metatile, separate from graphics: only the playfield's (BG2) collision bytes are read. Dynamic changes (breakable blocks, doors) go in a small RAM overlay checked before ROM and applied during streaming.

**Implemented** (`src/core/map.c`, `src/ecs/map_movement.c`, platform-neutral and unit tested on the host): collision types 0-2, with the planned and reserved ones colliding as the table below says; tags and `map_tags_in()`; map bodies and their contacts; runtime changes.

### Collision types

The low four bits of `Metatile.collision`, read with `MAP_TYPE()`:

| Type | Name | Status | What `sys_map_movement()` does with it |
| --- | --- | --- | --- |
| 0 | `MAP_EMPTY` | Implemented | Nothing |
| 1 | `MAP_SOLID` | Implemented | Blocks bodies from every side |
| 2 | `MAP_ONEWAY` | Implemented | Blocks only bodies moving down into it from above |
| 3 | `MAP_LADDER` | Planned | Empty until implemented |
| 4-9 | `MAP_SLOPE_R`, `MAP_SLOPE_L`, `MAP_SLOPE_R_LOW`, `MAP_SLOPE_R_HIGH`, `MAP_SLOPE_L_HIGH`, `MAP_SLOPE_L_LOW` | Planned | Solid (the whole metatile) until implemented |
| 10-15 | | Reserved | Empty |

`map_load()` loads a playfield whose metatiles use the planned or reserved types rather than refusing it: a game or the editor can paint ladders and slopes now, and they start working, with no change to the data, in the version that implements them. Debug builds warn once per kind (ladders, slopes, reserved types) when such a playfield loads; the check runs at load time over the layer's metatile definitions, so it costs nothing per frame. Data must not use the reserved types: a later version will give them a meaning (ceiling slopes, for one), and such data would change behaviour then. `map_collision_at()` returns the collision byte as the metatile has it, whatever its type.

**Ladders** (`MAP_LADDER`, planned): climbable, not solid. A map body overlapping a ladder metatile has `MAP_CONTACT_LADDER` in `body_contact`, so the game can let it climb: it sets the body's velocity itself, usually with `body_gravity` at `BODY_GRAVITY(0)` while it climbs. The top of a ladder (a ladder metatile with none above it) is a floor to bodies with gravity falling onto it, like `MAP_ONEWAY`; a body without gravity is climbing, and passes through it both ways. Until implemented, a ladder is empty and `MAP_CONTACT_LADDER` is never set.

**Floor slopes** (`MAP_SLOPE_*`, planned): solid below a straight line across the metatile, empty above it. The name says which way the floor rises (`_R` to the right, `_L` to the left). The line's height above the metatile's bottom edge, in pixels:

| Type | Name | At the left edge | At the right edge | Shape |
| --- | --- | --- | --- | --- |
| 4 | `MAP_SLOPE_R` | 0 | 16 | 45°; the top-right corner is high |
| 5 | `MAP_SLOPE_L` | 16 | 0 | 45°; the top-left corner is high |
| 6 | `MAP_SLOPE_R_LOW` | 0 | 8 | Left half of a 1:2 slope (about 27°) rising to the right |
| 7 | `MAP_SLOPE_R_HIGH` | 8 | 16 | Its right half, placed right of `_R_LOW` |
| 8 | `MAP_SLOPE_L_HIGH` | 16 | 8 | Left half of a 1:2 slope rising to the left |
| 9 | `MAP_SLOPE_L_LOW` | 8 | 0 | Its right half, placed right of `_L_HIGH` |

Each rises 16 or 8 pixels across its 16-pixel width, and slopes placed side by side as above meet at the same height. A map body will walk up and down them, standing on the line, with `MAP_CONTACT_FLOOR` as on a flat floor; the exact motion is specified when they are implemented. Until then each collides as `MAP_SOLID`.

**Why these values.** The ladder and the six floor slopes take types 3-9 and leave 10-15 for ceiling slopes and whatever comes later: twelve slope shapes (floors and ceilings) would fill the type space. Hazards are not a type: a hazard is a property (spikes may be solid, lava not), which is what tags are for ([below](#tags)).

### Tags

The high four bits of `Metatile.collision` are the game's: `MAP_TAG(0)` to `MAP_TAG(3)`, e.g. bonus blocks, bricks and gems in `platformer`, or hazards, water and goals. `sys_map_movement()` ignores them.

`map_tags_in(x, y, w, h)` (implemented) returns the tags of every playfield metatile that the rectangle of `w` × `h` world pixels with its top-left at (`x`, `y`) overlaps, ORed together, including runtime changes (`map_set_cell()`); the type bits of the result are 0. Metatiles outside the playfield, and cells naming a metatile the layer doesn't have, have no tags. It returns 0 without a playfield, or for a rectangle with no area (`w` or `h` 0 or less), and any `int` arguments are safe. A body resting on a metatile doesn't overlap it, so what a map body stands on is one pixel below its feet:

```c
#define TAG_SPIKES MAP_TAG(0)
if (map_tags_in(fx_to_int(pos_x[i]), fx_to_int(pos_y[i]) + body_h[i], body_w[i], 1) & TAG_SPIKES)
    hurt(i);
```

It costs one cell lookup per metatile overlapped, each looking through the runtime changes, so keep the rectangle to a few metatiles. `map_collision_at()` reads one pixel's whole collision byte, tags included.

### Map bodies

- `map_collision_at(x, y)` reads the playfield's collision byte at a world pixel. Outside the map, the sides are `MAP_SOLID` (also above and below the map, so bodies can't get around them) and above and below is `MAP_EMPTY` (bodies can jump above the map and fall out of the bottom). Without a playfield everything is empty.
- Map bodies (`C_POS | C_VEL | C_BODY | C_MAPBODY`) move with `sys_map_movement()`, which `sys_movement()` and `sys_physics()` skip. Each frame it adds gravity (`physics_set_gravity()`, scaled by the body's `body_gravity`) and limits the fall speed (`body_max_fall`), then moves the body along x, then y, in steps of at most 7 pixels (less than a metatile, so the leading edge enters at most one new row or column of metatiles per step, and fast bodies can't pass through one). A body covers `[x, x + body_w) × [y, y + body_h)` in 24.8 fixed point; a step that would take its leading edge into a blocking metatile puts the body flush against it on a whole pixel instead, records the side in `body_contact`, and bounces the velocity on that axis: it becomes `-velocity * body_bounce / 256` (0, the default, stops the body; 255 below), and the rest of that axis's movement this frame is dropped.
- **Bounces:** `body_bounce` 255 is a perfect bounce (a `u8` can't hold 256, so its largest value means it). Off a wall or a ceiling it keeps the whole speed. Off a floor (the side gravity pulls toward) the body rebounds at the speed that brings it back up to the height it fell from, to within a pixel, every time, so it bounces for ever. Keeping the speed would not: the body stops flush against the floor `past` short of where the frame's movement would have taken it, and would rebound lower than it fell from by up to a frame's fall (13 pixels at 1 pixel per frame per frame), on every bounce alike. This integration (velocity += gravity, then move) keeps `v² + g·v − 2·g·x` the same in free flight (v the velocity it moves at, x the position along gravity, g its acceleration), so the rebound speed is `u + g` with `u² + g·u = v² + g·v − 2·g·past`, v the speed it hit at: the same bisection `sys_physics()` uses for its perfect bounces (`serval_perfect_rebound()`, `src/ecs/physics_internal.h`; [runtime-systems.md](runtime-systems.md#physics)). Measured on 545 drops (2 to 110 pixels, gravity 1/16 to 1 pixel per frame per frame, 20,000 frames each), every peak is between 0.1 pixel above and 0.9 pixel below the drop height, the same every bounce; keeping the speed, peaks were up to 13 pixels low. On a floor, a rebound slower than twice one frame's gravity is a rest, whatever the bounce (a perfect one rebounds that slowly only when the body hit the floor slower than that), so a body standing on a floor stays put; `body_friction` slows a sliding body, as in `sys_physics()`. Unlike `sys_physics()`'s bounds, walls and ceilings use `body_bounce` too, so the default stops a character at a wall. Every metatile along the edge is checked, so bodies of any size up to 255×255 work.
- Only metatiles the leading edge newly enters block it. A body overlapping a solid metatile (placed there, or a cell changed under it) can move out, and one-way platforms follow naturally: a body moving down enters a `MAP_ONEWAY` row only from above, while one that jumped into it from below is already inside its row and falls through. Sideways movement ignores one-way metatiles.
- A body standing on a floor touches it every frame while gravity pulls it down (`MAP_CONTACT_FLOOR`); a body stopped by a wall touches it again only when pushed into it again.

**Contacts** (`body_contact`, one byte per entity, shared with `sys_physics()`, which reports bouncing bodies' contacts there with [`physics_set_contacts()`](runtime-systems.md#physics)):

| Bit | Name | Set by |
| --- | --- | --- |
| 0 | `MAP_CONTACT_FLOOR` | `sys_map_movement()`: the body's bottom edge touched the map |
| 1 | `MAP_CONTACT_CEILING` | Its top edge |
| 2 | `MAP_CONTACT_LEFT` | Its left edge |
| 3 | `MAP_CONTACT_RIGHT` | Its right edge |
| 4 | | Reserved for the engine |
| 5 | `BODY_CONTACT_EXIT` (`physics.h`) | `sys_physics()` only: a body left through an open edge |
| 6 | `MAP_CONTACT_LADDER` | Planned: `sys_map_movement()`, while the body overlaps a ladder; never set until ladders are implemented |
| 7 | | Reserved for the engine |

Bits 0-3 are the same bits as `physics.h`'s `BODY_SIDE_BOTTOM`, `_TOP`, `_LEFT` and `_RIGHT`. Scripts read them as the read-only property `body_contact` (`VM_P_BODY_CONTACT`; [lua.md](lua.md#what-compiles-to-what)): `self.body_contact & MAP_CONTACT_FLOOR ~= 0` while a map body stands on the floor.

### Runtime changes

`map_set_cell(mx, my, metatile)` changes a playfield cell: a broken block, a used bonus block, an opened door. Up to `MAP_MAX_CHANGES` (64) changed cells are kept in a table in IWRAM that `map_cell()`, `map_collision_at()`, `map_tags_in()`, `sys_map_movement()` and streaming check first; a cell set back to its original metatile leaves the table, a full table is reported in debug builds and the change ignored, and loading the playfield clears it. Changed cells are redrawn at the next `frame_end()` where they are in the streamed window, and the others when they scroll in. Every collision query looks through the table, which is why it is small; a later version can raise the constant, or add a flag (in a reserved `MapLayer.flags` bit) for layers whose cells are in RAM and written directly.

## Raster effects

Per-scanline changes to the background registers, timed by the HBlank interrupt or HBlank DMA: wavy water, split-screen HUDs, multi-speed parallax from one layer. **Decided:** raster effects are declared now as planned API: `raster_scroll()` (a per-line scroll offset for one map layer), `raster_backdrop()` and `raster_clear()` in `screen.h`, described in [runtime-systems.md](runtime-systems.md#raster-effects); they are implemented in a later minor version.

Design notes for the implementation, from the map side:

- A per-line scroll offset on a streamed layer can show tiles outside the streamed window: streaming keeps valid only the 31×21 tiles the screen covers at the layer's own scroll position, and the screenblock's other entries hold stale tiles. So `raster_scroll()` limits one frame's offsets to what VRAM holds around the window (within 9 pixels of each other horizontally and 89 vertically, any offsets on a wrapping axis whose map is 16, 8, 4, 2 or 1 metatiles long; its comment in `screen.h` gives the rule). Streaming a wider window would lift the limit; that is additive.
- The web target draws each frame from the state at VBlank ([platforms.md](platforms.md#web)), so mid-frame register writes are not seen there; it needs per-line state in its renderer to show them.
- The debugger should be able to show them ([debug-link.md](debug-link.md)).
