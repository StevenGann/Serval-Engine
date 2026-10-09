// Map layers on backgrounds 1-3: the tileset in VRAM, and each layer's
// screenblock kept up to date around the camera. See docs/tilemaps.md.
//
// VRAM layout: tileset tiles 0-1023 in charblocks 1-2 (CBB 1 for every map
// layer); BG1, BG2 and BG3 maps in screenblocks 28, 29 and 30, which lie in
// charblock 3 with the text layer's screenblock 31 (charblock 3 holds no
// tiles). The text layer keeps charblock 0, sprites charblocks 4-5.
//
// Streaming: each layer has a 32x32-entry screenblock used as a ring buffer:
// tile (tx, ty) of the layer goes to entry (tx & 31, ty & 31), and the
// background's scroll registers hold the layer's scroll position (camera *
// scroll_factor + map_set_scroll's offset; the hardware wraps at 256 pixels
// too). The entries for the 31x21 tiles that the
// screen can show at that scroll position (the "window") are valid. When the
// camera moves, only the tiles that enter the window are written; a jump of a
// whole window or more, or a new layer, rewrites the window.
//
// Entries are first written to a copy of each screenblock in EWRAM during
// frame_end(), before it waits for VBlank (as CPU work of the frame); then,
// in VBlank, the rows of 32 entries that changed are copied to VRAM. That
// keeps the VBlank part a few block copies even for a full redraw of all three
// layers.
//
// Exceptions, at load time: map_load() draws a new layer's window straight to
// VRAM (so the next frame shows it), as tileset_load() copies tiles at once.
// tileset_set_tiles() queues tile copies for the VBlank part of frame_end().

#include "serval/map.h"

#include <stddef.h>
#include <tonc.h>

#include "../core/map_internal.h"
#include "../core/warn.h"
#include "internal.h"

#define MAP_CHARBLOCK 1
#define FIRST_SCREENBLOCK 27 // + background number: 28, 29, 30
#define WINDOW_W 31          // tiles a 240-pixel line can touch
#define WINDOW_H 21          // tiles a 160-pixel column can touch
#define ALL_ROWS 0xFFFFFFFFu
// The fill loops' wrap point on a layer that doesn't wrap: a metatile
// coordinate counting up never reaches it (it would from -1 if this were
// 0xFFFFFFFF, and layers scrolled left of or above their map start there).
#define NO_WRAP 0x80000000u

// The data formats Studio Advance emits (docs/tilemaps.md#rom-data-format):
// their layout on 32-bit targets (the GBA and wasm32) must not change. New
// fields go into padding, as Tileset.flags did.
_Static_assert(sizeof(Tileset) == 16 && offsetof(Tileset, flags) == 13, "Tileset layout changed");
_Static_assert(sizeof(Metatile) == 10 && offsetof(Metatile, collision) == 8,
               "Metatile layout changed");
_Static_assert(sizeof(MapLayer) == 20 && offsetof(MapLayer, flags) == 15 &&
                   offsetof(MapLayer, scroll_factor) == 16,
               "MapLayer layout changed");

typedef struct {
    const MapLayer* layer; // what the window shows, or NULL
    int tx, ty;            // the window's top-left tile
    u32 dirty;             // screenblock rows (bit n = row n) to copy to VRAM
    u32 dirty_cols;        // screenblock columns to copy to VRAM
    u16 hofs, vofs;
} Background;

// Copies of the screenblocks of backgrounds 1-3 (index bg - 1).
static EWRAM_BSS u16 mirror[3][32 * 32];
static Background backgrounds[4];
static u32 control_pending; // backgrounds whose BGxCNT and DISPCNT bit need setting
static u32 shown;           // backgrounds enabled in DISPCNT (bit n = background n)

// frame_end() updates the layers and copies queued tiles from now on.
static void attach_hooks(void) {
    serval_map_prepare_hook = serval_map_prepare;
    serval_map_commit_hook = serval_map_commit;
}

// Tile replacements queued by tileset_set_tiles(), copied to VRAM in VBlank.
typedef struct {
    const u32* tiles;
    u16 first, count;
} TileUpdate;
static EWRAM_BSS TileUpdate tile_updates[MAP_MAX_TILE_UPDATES];
static u32 tile_update_count;
static u16 tileset_tiles; // tile_count of the loaded tileset, 0 if none

#ifdef SERVAL_DEBUG
enum {
    W_POINTER,
    W_LZ77,
    W_FLAGS,
    W_TILES,
    W_COUNT,
    W_PALETTES,
    W_SET_RANGE,
    W_SET_DATA,
    W_SET_FULL,
};
static u32 warned;
#define WARN_ONCE(kind, ...)                                                                       \
    do {                                                                                           \
        if (!(warned & (1u << (kind)))) {                                                          \
            warned |= 1u << (kind);                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
        }                                                                                          \
    } while (0)
#else
#define WARN_ONCE(kind, ...) ((void)0)
#endif

bool tileset_load(const Tileset* tileset) {
    if (!serval_plausible_pointer(tileset)) {
        WARN_ONCE(W_POINTER, "tileset_load: the tileset pointer is NULL or not valid");
        return false;
    }
    // Refused rather than ignored: copying LZ77 data as tiles would show
    // garbage, and a reserved bit may get a meaning later, which would change
    // what such a tileset does (docs/releases.md).
    if (tileset->flags & TILESET_LZ77) {
        WARN_ONCE(W_LZ77, "tileset_load: TILESET_LZ77 (LZ77-compressed tilesets) is planned, not "
                          "implemented in this engine version; nothing is loaded");
        return false;
    }
    if (tileset->flags) {
        WARN_ONCE(W_FLAGS,
                  "tileset_load: unknown .flags 0x%x (bit 1 is reserved for 8bpp tilesets, bits "
                  "2-7 for later use); nothing is loaded",
                  tileset->flags);
        return false;
    }
    if (tileset->tile_count == 0 || !serval_plausible_pointer(tileset->tiles)) {
        WARN_ONCE(W_TILES, "tileset_load: the tileset needs .tiles and .tile_count");
        return false;
    }
    if (tileset->tile_count > MAP_MAX_TILES || tileset->palette_count > MAP_MAX_PALETTES) {
        WARN_ONCE(W_COUNT,
                  "tileset_load: %u tiles and %u palettes, but at most %u tiles and %u palettes "
                  "fit (palette bank 15 is the text layer's)",
                  tileset->tile_count, tileset->palette_count, MAP_MAX_TILES, MAP_MAX_PALETTES);
        return false;
    }
    if (tileset->palette_count && !serval_plausible_pointer(tileset->palettes)) {
        WARN_ONCE(W_PALETTES,
                  "tileset_load: the tileset has palette_count %u but no .palettes (NULL or "
                  "not a valid pointer)",
                  tileset->palette_count);
        return false;
    }
    memcpy32(&tile_mem[MAP_CHARBLOCK][0], tileset->tiles, tileset->tile_count * 8u);
    tileset_tiles = tileset->tile_count;
    tile_update_count = 0;
    // Color 0 is transparent, and bank 0's is the backdrop: leave them be.
    for (u32 bank = 0; bank < tileset->palette_count; bank++)
        memcpy16(&pal_bg_bank[bank][1], &tileset->palettes[bank * 16 + 1], 15);
    // palettes: the tileset's colors win over tileset_set_colors() writes made
    // before it in the frame (palette.c); color 0 keeps them
    if (serval_palette_hooks) {
        for (u32 bank = 0; bank < tileset->palette_count; bank++)
            serval_palette_hooks->overwritten(false, bank * 16 + 1, 15);
    }
    return true;
}

void tileset_set_tiles(u16 first, const u32* tiles, u16 count) {
    if (count == 0)
        return;
    if ((u32)first + count > tileset_tiles) {
        WARN_ONCE(W_SET_RANGE,
                  "tileset_set_tiles(%u, ..., %u): the loaded tileset has %u tiles; load one "
                  "with tileset_load() and stay inside it",
                  first, count, tileset_tiles);
        return;
    }
    if (!serval_plausible_pointer(tiles)) {
        WARN_ONCE(W_SET_DATA, "tileset_set_tiles: the tiles pointer is NULL or not valid");
        return;
    }
    u32 k = 0;
    while (k < tile_update_count && tile_updates[k].first != first)
        k++;
    if (k == MAP_MAX_TILE_UPDATES) {
        WARN_ONCE(W_SET_FULL,
                  "tileset_set_tiles: more than %u updates in one frame; the extra ones are "
                  "ignored",
                  MAP_MAX_TILE_UPDATES);
        return;
    }
    if (k == tile_update_count)
        tile_update_count++;
    tile_updates[k] = (TileUpdate){tiles, first, count};
    attach_hooks();
}

// v modulo m (m > 0), from 0 to m - 1 also for negative v.
static int floor_mod(int v, u32 m) {
    int r = v % (int)m;
    return r < 0 ? r + (int)m : r;
}

// Screenblock rows (or columns) holding layer tile rows (or columns) t to
// t + count - 1, as a bit mask.
static u32 ring_mask(int t, int count) {
    if (count >= 32)
        return ALL_ROWS;
    u32 bits = (1u << count) - 1;
    u32 shift = (u32)t & 31;
    return (bits << shift) | (shift ? bits >> (32 - shift) : 0);
}

// Writes the four entries of metatile `cell` placed at layer metatile (mx,
// my), where they fall inside tiles [tx, tx + cols) x [ty, ty + rows).
// On a wrapping layer, also every copy of it at (mx + i * width, my + j *
// height). Returns the screenblock rows written.
static u32 put_metatile(const MapLayer* layer, u16* sb, u32 mx, u32 my, u32 cell, int tx, int ty,
                        int cols, int rows) {
    const u16* se = layer->metatiles[cell].se;
    bool wrap = layer->flags & MAP_LAYER_WRAP;
    // The first copy whose right column is at or after tx, and whose bottom
    // row is at or after ty.
    int x0 = wrap ? (tx >> 1) + floor_mod((int)mx - (tx >> 1), layer->width) : (int)mx;
    int y0 = wrap ? (ty >> 1) + floor_mod((int)my - (ty >> 1), layer->height) : (int)my;
    u32 dirty = 0;
    for (int y = y0; 2 * y <= ty + rows - 1; y += layer->height) {
        for (int x = x0; 2 * x <= tx + cols - 1; x += layer->width) {
            for (u32 k = 0; k < 4; k++) {
                int px = 2 * x + (int)(k & 1), py = 2 * y + (int)(k >> 1);
                if (px >= tx && px < tx + cols && py >= ty && py < ty + rows) {
                    sb[((u32)py & 31) * 32 + ((u32)px & 31)] = se[k];
                    dirty |= 1u << ((u32)py & 31);
                }
            }
            if (!wrap)
                break;
        }
        if (!wrap)
            break;
    }
    return dirty;
}

static const u16 blank[4]; // the entries of tiles outside a layer

// Writes the entries of layer tiles [tx, tx + cols) x [ty, ty + rows) to its
// screenblock copy, given the metatile (mx, my) holding tile (tx, ty) (wrapped
// into the layer on a wrapping one). Tiles outside a layer that doesn't wrap
// get entry 0 (tileset tile 0, which should be blank). Works a metatile at a
// time: one lookup for up to four entries.
//
// ARM code in IWRAM, like fill_column: as Thumb code in ROM, streaming took
// about 70 cycles per entry, and a full redraw of three layers 140,000 cycles.
static SERVAL_IWRAM_TEXT __attribute__((noinline)) void
fill_entries(const MapLayer* layer, u16* sb, int tx, int ty, int cols, int rows, int mx, int my) {
    const Metatile* metatiles = layer->metatiles;
    const u32 width = layer->width, height = layer->height, count = layer->metatile_count;
    const bool wrap = layer->flags & MAP_LAYER_WRAP;
    const u32 wrap_x = wrap ? width : NO_WRAP, wrap_y = wrap ? height : NO_WRAP;
    const int end = ty + rows;
    u32 row = (u32)ty & 31;
    for (int y = ty; y < end;) {
        // Tile row y, and y + 1 too if it is in the same metatile row.
        const bool upper = !(y & 1), both = upper && y + 1 < end;
        u16* out = sb + row * 32;
        u16* out_lower = sb + ((row + 1) & 31) * 32;
        const u16* cells = (u32)my < height ? layer->cells + (u32)my * width : NULL;
        const u32 corner = upper ? 0 : 2;
        u32 col = (u32)tx & 31;
        int x = mx;
        bool right_only = (u32)tx & 1; // tx is the right half of its metatile
        for (u32 left = (u32)cols; left;) {
            const u16* se = blank;
            if (cells && (u32)x < width) {
                u32 cell = cells[x];
                if (cell < count)
                    se = metatiles[cell].se;
            }
            if (!right_only) {
                out[col] = se[corner];
                if (both)
                    out_lower[col] = se[2];
                col = (col + 1) & 31;
                if (--left == 0)
                    break;
            }
            right_only = false;
            out[col] = se[corner + 1];
            if (both)
                out_lower[col] = se[3];
            col = (col + 1) & 31;
            left--;
            if ((u32)++x == wrap_x)
                x = 0;
        }
        u32 done = both ? 2 : 1;
        y += (int)done;
        row = (row + done) & 31;
        if ((u32)++my == wrap_y)
            my = 0;
    }
}

// Like fill_entries for a single column of tiles: for the columns that enter
// the window as the camera moves sideways.
static SERVAL_IWRAM_TEXT __attribute__((noinline)) void
fill_column(const MapLayer* layer, u16* sb, int tx, int ty, int rows, int mx, int my) {
    const Metatile* metatiles = layer->metatiles;
    const u32 width = layer->width, height = layer->height, count = layer->metatile_count;
    const u32 wrap_y = (layer->flags & MAP_LAYER_WRAP) ? height : NO_WRAP;
    const u32 right = (u32)tx & 1;
    const bool inside = (u32)mx < width;
    u16* out = sb + ((u32)tx & 31);
    u32 row = (u32)ty & 31;
    bool lower = (u32)ty & 1;
    for (int r = 0; r < rows; r++) {
        const u16* se = blank;
        if (inside && (u32)my < height) {
            u32 cell = layer->cells[(u32)my * width + (u32)mx];
            if (cell < count)
                se = metatiles[cell].se;
        }
        out[row * 32] = se[(lower ? 2 : 0) + right];
        row = (row + 1) & 31;
        if (lower && (u32)++my == wrap_y)
            my = 0;
        lower = !lower;
    }
}

// Writes tiles [tx, tx + cols) x [ty, ty + rows) to a layer's screenblock
// copy, including the playfield's runtime changes there.
static void fill(const MapLayer* layer, u16* sb, int tx, int ty, int cols, int rows) {
    const bool wrap = layer->flags & MAP_LAYER_WRAP;
    int mx = wrap ? floor_mod(tx >> 1, layer->width) : tx >> 1;
    int my = wrap ? floor_mod(ty >> 1, layer->height) : ty >> 1;
    if (cols > 2) {
        fill_entries(layer, sb, tx, ty, cols, rows, mx, my);
    } else {
        for (int c = 0; c < cols; c++) {
            fill_column(layer, sb, tx + c, ty, rows, mx, my);
            if ((tx + c) & 1 && ++mx == (int)layer->width && wrap)
                mx = 0;
        }
    }
    if (layer == serval_map_layers[2]) {
        for (u32 k = 0; k < serval_map_change_count; k++) {
            const MapChange* change = &serval_map_changes[k];
            put_metatile(layer, sb, change->mx, change->my, change->cell, tx, ty, cols, rows);
        }
    }
}

static void prepare_background(u32 bg, bool reload) {
    Background* b = &backgrounds[bg];
    const MapLayer* layer = serval_map_layers[bg];
    if (!layer) {
        if (b->layer || reload) {
            b->layer = NULL;
            control_pending |= 1u << bg;
        }
        return;
    }
    u16* sb = mirror[bg - 1];
    int sx = serval_map_layer_x(layer), sy = serval_map_layer_y(layer);
    int tx = sx >> 3, ty = sy >> 3; // arithmetic shifts: rounded down
    int dx = tx - b->tx, dy = ty - b->ty;
    if (reload || b->layer != layer || dx >= WINDOW_W || dx <= -WINDOW_W || dy >= WINDOW_H ||
        dy <= -WINDOW_H) {
        if (reload || b->layer != layer)
            control_pending |= 1u << bg;
        b->layer = layer;
        // Entries outside the window are never seen, so they aren't cleared.
        fill(layer, sb, tx, ty, WINDOW_W, WINDOW_H);
        b->dirty = ALL_ROWS;
    } else {
        // Columns entering the window, then rows (over the new columns).
        if (dx > 0) {
            fill(layer, sb, b->tx + WINDOW_W, ty, dx, WINDOW_H);
            b->dirty_cols |= ring_mask(b->tx + WINDOW_W, dx);
        } else if (dx < 0) {
            fill(layer, sb, tx, ty, -dx, WINDOW_H);
            b->dirty_cols |= ring_mask(tx, -dx);
        }
        if (dy > 0) {
            fill(layer, sb, tx, b->ty + WINDOW_H, WINDOW_W, dy);
            b->dirty |= ring_mask(b->ty + WINDOW_H, dy);
        } else if (dy < 0) {
            fill(layer, sb, tx, ty, WINDOW_W, -dy);
            b->dirty |= ring_mask(ty, -dy);
        }
    }
    b->tx = tx;
    b->ty = ty;
    b->hofs = (u16)(sx & 0x1FF);
    b->vofs = (u16)(sy & 0x1FF);
}

// Redraws playfield cells changed by map_set_cell() since the last frame,
// where they are in the window.
static void redraw_changes(void) {
    Background* b = &backgrounds[2];
    const MapLayer* layer = b->layer;
    if (layer && b->dirty != ALL_ROWS) {
        u16* sb = mirror[1];
        if (serval_map_redraw_all) {
            fill(layer, sb, b->tx, b->ty, WINDOW_W, WINDOW_H);
            b->dirty = ALL_ROWS;
        } else {
            for (u32 k = 0; k < serval_map_redraw_count; k++) {
                const MapChange* c = &serval_map_redraw[k];
                u32 cell = serval_map_cell_in(layer, c->mx, c->my);
                if (cell < layer->metatile_count)
                    b->dirty |= put_metatile(layer, sb, c->mx, c->my, cell, b->tx, b->ty, WINDOW_W,
                                             WINDOW_H);
            }
        }
    }
    serval_map_redraw_count = 0;
    serval_map_redraw_all = false;
}

// Before VBlank: brings the screenblock copies up to date with the layers
// and the camera.
void serval_map_prepare(void) {
    u32 reload = serval_map_reload;
    serval_map_reload = 0;
    for (u32 bg = 1; bg <= 3; bg++)
        prepare_background(bg, reload & (1u << bg));
    redraw_changes();
}

// Copies what changed on background bg to VRAM and sets its registers.
static void commit_background(u32 bg) {
    Background* b = &backgrounds[bg];
    u32 sbb = FIRST_SCREENBLOCK + bg;
    if (control_pending & (1u << bg)) {
        if (b->layer) {
            REG_BGCNT[bg] =
                (u16)(BG_CBB(MAP_CHARBLOCK) | BG_SBB(sbb) | BG_4BPP | BG_REG_32x32 | BG_PRIO(bg));
            shown |= 1u << bg;
        } else {
            shown &= ~(1u << bg);
        }
        REG_DISPCNT =
            (u16)((REG_DISPCNT & ~(DCNT_BG0 << bg)) | (shown & (1u << bg) ? DCNT_BG0 << bg : 0));
        control_pending &= ~(1u << bg);
    }
    if (!b->layer)
        return;
    REG_BG_OFS[bg].x = b->hofs;
    REG_BG_OFS[bg].y = b->vofs;
    u32 dirty = b->dirty;
    const u16* from = mirror[bg - 1];
    if (dirty == ALL_ROWS) {
        memcpy32(&se_mem[sbb][0], from, sizeof(mirror[0]) / 4);
    } else {
        for (u32 row = 0; dirty; row++, dirty >>= 1) {
            if (dirty & 1)
                memcpy32(&se_mem[sbb][row * 32], from + row * 32, 32 * 2 / 4);
        }
        // Columns entering the window: 32 entries 64 bytes apart.
        u32 cols = b->dirty_cols;
        for (u32 col = 0; cols; col++, cols >>= 1) {
            if (cols & 1) {
                u16* to = &se_mem[sbb][col];
                for (u32 row = 0; row < 32; row++)
                    to[row * 32] = from[row * 32 + col];
            }
        }
    }
    b->dirty = 0;
    b->dirty_cols = 0;
}

// In VBlank: copies queued tiles and what changed on the layers to VRAM, and
// sets the background registers.
void serval_map_commit(void) {
    for (u32 k = 0; k < tile_update_count; k++) {
        const TileUpdate* u = &tile_updates[k];
        memcpy32(&tile_mem[MAP_CHARBLOCK][u->first], u->tiles, u->count * 8u);
    }
    tile_update_count = 0;
    for (u32 bg = 1; bg <= 3; bg++)
        commit_background(bg);
}

// Called by map_load(): starts the frame_end() updates and draws the layers
// loaded or unloaded since the last frame at once, so the next frame shows
// them (no blank frame after loading a room).
void serval_map_attach(void) {
    attach_hooks();
    u32 reload = serval_map_reload;
    serval_map_reload = 0;
    for (u32 bg = 1; bg <= 3; bg++) {
        if (reload & (1u << bg)) {
            prepare_background(bg, true);
            commit_background(bg);
        }
    }
}

u32 serval_map_scroll(u32 bg) {
    return bg >= 1 && bg <= 3 ? (u32)backgrounds[bg].vofs << 16 | backgrounds[bg].hofs : 0;
}
