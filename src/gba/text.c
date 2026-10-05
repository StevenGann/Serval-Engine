#include "serval/text.h"

#include "internal.h"

#include <tonc.h>

// Layout on background 0: the font's 96 glyphs (ASCII 32-127) are 4bpp tiles
// 0-95 of charblock 0, the map is screenblock 31, and glyph pixels use color 1
// of background palette bank 15. Tile 0 is the blank space glyph, so an empty
// map shows nothing.

#define TEXT_CHARBLOCK 0
#define TEXT_SCREENBLOCK 31
#define TEXT_PALBANK 15
#define FIRST_GLYPH 32
#define GLYPH_COUNT 96

static bool ready;

static void text_init(void) {
    // sys8 (from libtonc): 8x8 1bpp glyphs, unpacked to 4bpp so set bits
    // become color 1.
    static const BUP unpack = {.src_len = GLYPH_COUNT * 8, .src_bpp = 1, .dst_bpp = 4};
    BitUnPack(sys8Glyphs, &tile_mem[TEXT_CHARBLOCK][0], &unpack);

    pal_bg_bank[TEXT_PALBANK][1] = RGB15(31, 31, 31);
    memset32(&se_mem[TEXT_SCREENBLOCK][0], 0, sizeof(SCREENBLOCK) / 4);

    REG_BG0CNT =
        BG_CBB(TEXT_CHARBLOCK) | BG_SBB(TEXT_SCREENBLOCK) | BG_4BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    REG_DISPCNT |= DCNT_BG0;
    ready = true;
}

bool serval_text_active(void) {
    return ready;
}

void serval_text_deactivate(void) {
    if (!ready)
        return;
    memset32(&se_mem[TEXT_SCREENBLOCK][0], 0, sizeof(SCREENBLOCK) / 4);
    REG_DISPCNT &= ~DCNT_BG0;
    ready = false; // the next text call sets the layer up again
}

void serval_text_print_bank(int col, int row, const char* s, u32 palbank) {
    if (!ready)
        text_init();
    if (row < 0 || row >= TEXT_ROWS)
        return;
    for (; *s && col < TEXT_COLS; s++, col++) {
        if (col < 0)
            continue;
        int c = (unsigned char)*s;
        if (c < FIRST_GLYPH || c >= FIRST_GLYPH + GLYPH_COUNT)
            c = '?';
        se_mem[TEXT_SCREENBLOCK][row * 32 + col] =
            (SCR_ENTRY)(SE_PALBANK(palbank) | (c - FIRST_GLYPH));
    }
}

void text_print(int col, int row, const char* s) {
    serval_text_print_bank(col, row, s, TEXT_PALBANK);
}

void text_print_line(int col, int row, const char* s) {
    text_print(col, row, s);
    if (row < 0 || row >= TEXT_ROWS)
        return;
    int end = col;
    while (*s++)
        end++;
    for (int c = end < 0 ? 0 : end; c < TEXT_COLS; c++)
        se_mem[TEXT_SCREENBLOCK][row * 32 + c] = 0;
}

void text_clear(void) {
    if (!ready)
        text_init();
    memset32(&se_mem[TEXT_SCREENBLOCK][0], 0, sizeof(SCREENBLOCK) / 4);
}
