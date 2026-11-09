// Drawing art at boot from ASCII pictures (effects.h): a canvas, then cut
// into 4bpp tiles.

#include "effects.h"

static u8 canvas[CANVAS_MAX * CANVAS_MAX] SERVAL_EWRAM_BSS;
static int canvas_w, canvas_h;

void canvas_begin(int w, int h) {
    canvas_w = w;
    canvas_h = h;
    for (int i = 0; i < w * h; i++)
        canvas[i] = 0;
}

void canvas_plot(int x, int y, u32 c) {
    if (x >= 0 && x < canvas_w && y >= 0 && y < canvas_h)
        canvas[y * canvas_w + x] = (u8)(c & 15);
}

// The color of picture character c in `keys` (0 if it isn't there).
static u32 key(const char* keys, char c) {
    for (u32 k = 1; keys[k]; k++)
        if (keys[k] == c)
            return k;
    return 0;
}

void canvas_draw(const char* const* rows, int count, int x, int y, const char* keys) {
    for (int py = 0; py < count; py++) {
        for (int px = 0; rows[py][px]; px++) {
            u32 c = key(keys, rows[py][px]);
            if (c)
                canvas_plot(x + px, y + py, c);
        }
    }
}

u32* canvas_pack(u32* out) {
    for (int ty = 0; ty < canvas_h / 8; ty++) {
        for (int tx = 0; tx < canvas_w / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)canvas[(ty * 8 + y) * canvas_w + tx * 8 + x] << (4 * x);
                *out++ = word;
            }
        }
    }
    return out;
}

// The index of `tile` in the painting's tiles, added if it isn't there yet.
static u16 paint_tile(Painting* out, const u32* tile) {
    for (u16 t = 0; t < out->tile_count; t++) {
        const u32* have = &out->tiles[t * 8];
        int w = 0;
        while (w < 8 && have[w] == tile[w])
            w++;
        if (w == 8)
            return t;
    }
    if (out->tile_count == PAINT_MAX_TILES)
        return 0; // full: a blank tile (raise PAINT_MAX_TILES)
    u32* dst = &out->tiles[out->tile_count * 8];
    for (int w = 0; w < 8; w++)
        dst[w] = tile[w];
    return out->tile_count++;
}

void paint(Painting* out, u32 (*pixel)(int x, int y)) {
    for (int w = 0; w < 8; w++)
        out->tiles[w] = 0;
    out->tile_count = 1;
    for (int ty = 0; ty < PAINT_H * 2; ty++) {
        for (int tx = 0; tx < PAINT_W * 2; tx++) {
            u32 tile[8], bank = 0;
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++) {
                    u32 p = pixel(tx * 8 + x, ty * 8 + y);
                    if (p & 15)
                        bank = p >> 4;
                    word |= (p & 15) << (4 * x);
                }
                tile[y] = word;
            }
            int cell = (ty / 2) * PAINT_W + tx / 2;
            out->metatiles[cell].se[(ty & 1) * 2 + (tx & 1)] =
                MAP_SE(paint_tile(out, tile), bank, 0);
            out->cells[cell] = (u16)cell;
        }
    }
}
