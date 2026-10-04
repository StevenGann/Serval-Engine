# Tilemaps

Levels are stored as 16×16 metatiles in ROM and streamed into 32×32 ring-buffered screenblocks as the camera scrolls. 1.0 uses regular tiled backgrounds only; affine (Mode 7) is post-1.0.

**Hardware budget:** 64 KB BG VRAM as four 16 KB charblocks overlapping 32 two-kilobyte screenblocks. Regular BG map entries are 16-bit (tile index, H/V flip, palette bank).

## Default layer roles

| BG | Default role |
| --- | --- |
| BG0 | HUD / text box |
| BG1 | Foreground (in front of sprites) |
| BG2 | Main playfield |
| BG3 | Parallax background |

The editor exposes these as room layers, with hardware limits visible.

## Metatiles

Four screen entries plus a collision byte. They cut level data about 4× and give collision a natural granularity. Tile deduplication at build time also matches H/V-flipped tiles.

## Streaming

- When the camera crosses an 8-pixel boundary, write the newly exposed column or row at index `(tile_x & 31)`.
- Queue writes during the frame; flush them in VBlank ([frame-loop.md](frame-loop.md)).
- Use 32×32 screenblocks for streamed layers; 64-wide maps complicate wrap math.
- Parallax layers use a per-layer scroll factor. Small repeating backgrounds just wrap.

## Tilesets

Loaded per room as groups into charblocks with a bump allocator; LZ77 allowed. Animated tiles (water, lava) swap tile graphics in VRAM during VBlank.

## Collision

Read directly from ROM per metatile (solid, one-way, slope, ladder, hazard), separate from graphics. Dynamic changes (breakable blocks, doors) go in a small RAM overlay checked before ROM and applied during streaming.

## ROM data format

```c
typedef struct {
    u16 se[4];        // 2x2 screen entries: tile, flip, palette
    u8  collision;    // SOLID, ONEWAY, SLOPE_L, LADDER...
} Metatile;

typedef struct {
    u16 width, height;          // in metatiles
    const u16 *cells;           // metatile index per cell
    const Metatile *metatiles;  // definitions
    u8  tileset_group;
    u8  bg_layer, priority;
    FIXED scroll_factor;        // parallax
    u8  flags;                  // STREAMED, WRAP, ...
} MapLayer;
```

## Raster effects

Per-scanline scroll changes via HBlank DMA (wavy water, split-screen HUDs, multi-speed parallax). Candidate for 1.0 if time allows; the debugger should be able to visualize them.
