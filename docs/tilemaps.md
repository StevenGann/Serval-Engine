# Tilemaps

Levels are stored as 16×16 metatiles in ROM and streamed into 32×32 ring-buffered screenblocks as the camera scrolls. 1.0 uses regular tiled backgrounds only; affine (Mode 7) is post-1.0.

**Status:** partly implemented (`include/serval/map.h`; function reference in [api-reference.md](api-reference.md#maph)). Implemented: one tileset per room, up to three map layers on BG1-BG3 streamed around the camera (any map size), parallax, wrapping and fixed layers, per-layer scroll offsets (layers that scroll by themselves), the camera ([runtime-systems.md](runtime-systems.md#camera)), runtime cell changes, animated tiles (`tileset_set_tiles`), and map collision for map bodies (solid and one-way metatiles, plus four tag bits for the game). Planned: tileset groups with a bump allocator, LZ77-compressed tilesets, slopes, ladders and other collision types, 8bpp layers, raster effects.

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
} Tileset;

typedef struct {
    u16 se[4];            // screen entries: top-left, top-right, bottom-left, bottom-right
                          // MAP_SE(tile, palette, MAP_SE_FLIP_H | MAP_SE_FLIP_V)
    u8  collision;        // type (low 4 bits: MAP_EMPTY, MAP_SOLID, MAP_ONEWAY) | MAP_TAG(0..3)
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

Format addition (before the first release): `MAP_LAYER_FIXED` (bit 1 of `flags`); existing layers are unaffected.

- Tileset tile 0 should be blank: a layer that doesn't wrap shows screen entry 0 outside its map.
- A cell naming a metatile the layer doesn't have shows and collides as empty (debug builds warn when the layer loads).
- Every layer is streamed, whatever its size, so the draft's `STREAMED` flag is gone; its priority is its BG number; one tileset serves all layers of a room (tileset groups are planned, below).
- The four tag bits of the collision byte are for the game (bonus block, hazard...); only the type affects `sys_map_movement()`.

## Streaming

**Implemented** (`src/gba/map.c`):

- Each layer's screenblock is a 32×32 ring buffer: layer tile `(tx, ty)` goes to entry `(tx & 31, ty & 31)`, and the background's scroll registers hold the layer's scroll position (the hardware wraps at 256 pixels too).
- The valid part is the *window*: the 31×21 tiles that a 240×160 screen can touch at the current scroll position. When the camera moves, only tiles entering the window are written: whole columns (21 entries) and rows (31 entries). A move of a whole window or more, a newly loaded layer or `map_load()` redraws the window.
- A layer scrolls to `camera * scroll_factor` (rounded down) plus its offset from `map_set_scroll()` (below). `MAP_LAYER_WRAP` layers repeat their cells in both directions (small parallax backgrounds); other layers show entry 0 outside their map.
- Playfield cells changed with `map_set_cell()` are redrawn where they are in the window; changes outside it appear when they scroll in (streaming reads the changes too).
- `map_load()` draws the new layer's window straight to VRAM and turns the background on (a load-time VRAM write, like `tileset_load()`), so the frame after loading a room already shows it rather than only the backdrop; loading during a fade to black hides the write. Set the camera before loading the playfield.
- After that, everything happens in `frame_end()`, in two steps. Before it waits for VBlank (as CPU work of the frame), entries are written to a copy of each screenblock in EWRAM (6 KB). In VBlank, the changed rows (and columns, written 64 bytes apart) are copied to VRAM, and the scroll and control registers are set. So VRAM writes stay in VBlank, and the VBlank part stays short even for a full redraw. Nothing runs (or links) in games that never call `map_load()`.
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

**Implemented:** `tileset_load()` copies one tileset to charblocks 1-2 and its palettes to BG banks 0-14 immediately (like `sprite_group_load()`), so call it while loading a room, before its layers are shown.

**Animated tiles:** `tileset_set_tiles(first, tiles, count)` replaces tiles of the loaded tileset, so every cell using them changes at once (water, lava, a shimmering bonus block): the game keeps each animation frame's tiles in ROM and passes the next frame's when it is due. The copies are queued (`MAP_MAX_TILE_UPDATES`, 8 per frame; a second call for the same `first` replaces the first) and done in VBlank at the next `frame_end()`, before the map rows and columns: 32 bytes per tile, so a few dozen tiles per frame fit comfortably beside a full map redraw. Calls outside the tileset, or past the queue, are ignored with a warning in debug builds; `tileset_load()` drops queued updates.

**Planned:** loaded per room as groups into charblocks with a bump allocator; LZ77 allowed.

## Collision

Read directly from ROM per metatile, separate from graphics. Dynamic changes (breakable blocks, doors) go in a small RAM overlay checked before ROM and applied during streaming.

**Implemented** (`src/core/map.c`, `src/ecs/map_movement.c`, platform-neutral and unit tested on the host):

- `map_collision_at(x, y)` reads the playfield's collision byte at a world pixel. Outside the map, the sides are `MAP_SOLID` (also above and below the map, so bodies can't get around them) and above and below is `MAP_EMPTY` (bodies can jump above the map and fall out of the bottom). Without a playfield everything is empty.
- `map_set_cell()` keeps up to `MAP_MAX_CHANGES` (64) changed playfield cells in a table in IWRAM that `map_cell()`, `map_collision_at()` and streaming check first; a cell set back to its original metatile leaves the table; loading the playfield clears it.
- Map bodies (`C_POS | C_VEL | C_BODY | C_MAPBODY`) move with `sys_map_movement()`, which `sys_movement()` and `sys_physics()` skip. Each frame it adds gravity (`physics_set_gravity()`) and limits the fall speed (`body_max_fall`), then moves the body along x, then y, in steps of at most 7 pixels (less than a metatile, so the leading edge enters at most one new row or column of metatiles per step, and fast bodies can't pass through one). A body covers `[x, x + body_w) × [y, y + body_h)` in 24.8 fixed point; a step that would take its leading edge into a blocking metatile puts the body flush against it on a whole pixel instead, records the side in `body_contact`, and bounces the velocity on that axis: it becomes `-velocity * body_bounce / 256` (0, the default, stops the body), and the rest of that axis's movement this frame is dropped. On a floor (the side gravity pulls toward), a rebound slower than twice one frame's gravity is a rest, and `body_friction` slows a sliding body, as in `sys_physics()`; unlike `sys_physics()`'s bounds, walls and ceilings use `body_bounce` too, so the default stops a character at a wall. Every metatile along the edge is checked, so bodies of any size up to 255×255 work.
- Only metatiles the leading edge newly enters block it. A body overlapping a solid metatile (placed there, or a cell changed under it) can move out, and one-way platforms follow naturally: a body moving down enters a `MAP_ONEWAY` row only from above, while one that jumped into it from below is already inside its row and falls through. Sideways movement ignores one-way metatiles.
- A body standing on a floor touches it every frame while gravity pulls it down (`MAP_CONTACT_FLOOR`); a body stopped by a wall touches it again only when pushed into it again.

**Planned:** slopes, ladders and hazards as collision types; collision events for scripts ([vm.md](vm.md)).

## Raster effects

Per-scanline scroll changes via HBlank DMA (wavy water, split-screen HUDs, multi-speed parallax). Candidate for 1.0 if time allows; the debugger should be able to visualize them.
