#include "serval/text.h"

#include "internal.h"
#include "screen_internal.h"

#include <tonc.h>

// Layout on background 0: the font's 96 glyphs (ASCII 32-127) are 4bpp tiles
// 0-95 of charblock 0, the map is screenblock 31, and glyph pixels use color 1
// of background palette bank 15, their shadow pixels color 2. Tile 0 is the
// blank space glyph, so an empty map shows nothing.

#define TEXT_CHARBLOCK 0
#define TEXT_SCREENBLOCK 31
#define TEXT_PALBANK 15
#define FIRST_GLYPH 32
#define GLYPH_COUNT 96

static bool ready;
static bool shadow;
static Color text_color = COLOR_RGB(255, 255, 255), shadow_color = COLOR_RGB(0, 0, 0);

// A 4-bit pixel mask (bit 0 = leftmost pixel) spread to 4bpp pixels of color
// 1 (low nibble = leftmost pixel).
static const u16 spread[16] = {
    0x0000, 0x0001, 0x0010, 0x0011, 0x0100, 0x0101, 0x0110, 0x0111,
    0x1000, 0x1001, 0x1010, 0x1011, 0x1100, 0x1101, 0x1110, 0x1111,
};

static u32 pixels(u32 mask) {
    return spread[mask & 15] | (u32)spread[mask >> 4] << 16;
}

// Writes the 96 glyph tiles from sys8 (libtonc's 8x8 font, 1 bit per pixel:
// 8 bytes per glyph, bit 0 = leftmost pixel), with the shadow if it is on:
// color 2 wherever the pixel one up and one left is set and this one isn't.
static void load_font(void) {
    const u8* src = (const u8*)sys8Glyphs;
    u32* dst = (u32*)&tile_mem[TEXT_CHARBLOCK][0];
    for (u32 g = 0; g < GLYPH_COUNT; g++) {
        u32 above = 0;
        for (u32 r = 0; r < 8; r++) {
            u32 bits = *src++;
            u32 shade = shadow ? (above << 1) & ~bits & 0xFF : 0;
            *dst++ = pixels(bits) | pixels(shade) << 1;
            above = bits;
        }
    }
}

static void text_init(void) {
    load_font();
    pal_bg_bank[TEXT_PALBANK][1] = text_color;
    pal_bg_bank[TEXT_PALBANK][2] = shadow_color;
    memset32(&se_mem[TEXT_SCREENBLOCK][0], 0, sizeof(SCREENBLOCK) / 4);

    REG_BG0CNT =
        BG_CBB(TEXT_CHARBLOCK) | BG_SBB(TEXT_SCREENBLOCK) | BG_4BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    REG_DISPCNT |= DCNT_BG0;
    ready = true;
}

// Colors and the shadow apply at once if the layer is set up, else when the
// first text call sets it up.
void text_set_color(Color text, Color shadow_col) {
    text_color = text;
    shadow_color = shadow_col;
    if (ready) {
        pal_bg_bank[TEXT_PALBANK][1] = text;
        pal_bg_bank[TEXT_PALBANK][2] = shadow_col;
    }
}

void text_set_shadow(bool on) {
    if (on == shadow)
        return;
    shadow = on;
    if (ready)
        load_font();
}

bool serval_text_shadow(void) {
    return shadow;
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

void text_print_centered(int row, const char* s) {
    int length = 0;
    while (s[length])
        length++;
    text_print_line(0, row, "");
    text_print((TEXT_COLS - length) / 2, row, s);
}
