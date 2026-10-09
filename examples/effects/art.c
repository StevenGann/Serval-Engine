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
