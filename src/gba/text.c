#include "serval/text.h"

#include "../core/warn.h"
#include "internal.h"
#include "screen_internal.h"

#include <tonc.h>

// Layout on background 0: the map is screenblock 31 and the font's 96 glyphs
// (ASCII 32-127) are 4bpp tiles of charblock 0, one set of 96 per text style:
// style n uses tiles 96n to 96n + 95 and colors 2n + 1 (glyph) and 2n + 2
// (shadow) of background palette bank 15, so every style shares the one bank
// map layers leave to text. A style's tiles are written the first time it is
// used. Tile 0 (style 0's space) is blank, so an empty map shows nothing.

#define TEXT_CHARBLOCK 0
#define TEXT_SCREENBLOCK 31
#define TEXT_PALBANK 15
#define FIRST_GLYPH 32
#define GLYPH_COUNT 96

_Static_assert(TEXT_STYLES* GLYPH_COUNT <= 512, "the styles' glyphs must fit in charblock 0");
_Static_assert(TEXT_STYLES * 2 <= 15, "the styles' colors must fit in one palette bank");

static bool ready;
static bool shadow;
static u8 style;  // text_set_style's
static u8 loaded; // bit n: style n's glyph tiles are in VRAM
static Color colors[TEXT_STYLES][2] = {
    {COLOR_RGB(255, 255, 255), COLOR_RGB(0, 0, 0)},
    {COLOR_RGB(255, 224, 0), COLOR_RGB(0, 0, 0)},
    {COLOR_RGB(255, 96, 96), COLOR_RGB(0, 0, 0)},
    {COLOR_RGB(160, 160, 160), COLOR_RGB(0, 0, 0)},
};

#ifdef SERVAL_DEBUG
static bool warned_style, warned_area, warned_string;
#endif

// A 4-bit pixel mask (bit 0 = leftmost pixel) spread to 4bpp pixels of color
// 1 (low nibble = leftmost pixel).
static const u16 spread[16] = {
    0x0000, 0x0001, 0x0010, 0x0011, 0x0100, 0x0101, 0x0110, 0x0111,
    0x1000, 0x1001, 0x1010, 0x1011, 0x1100, 0x1101, 0x1110, 0x1111,
};

static u32 pixels(u32 mask) {
    return spread[mask & 15] | (u32)spread[mask >> 4] << 16;
}

// Writes a style's 96 glyph tiles from sys8 (libtonc's 8x8 font, 1 bit per
// pixel: 8 bytes per glyph, bit 0 = leftmost pixel), with the shadow if it is
// on: the shadow color wherever the pixel one up and one left is set and this
// one isn't. Colors 1 and 2 become 2n + 1 and 2n + 2 for style n by adding 2n
// to every pixel that isn't 0.
static void load_style(u32 n) {
    const u8* src = (const u8*)sys8Glyphs;
    u32* dst = (u32*)&tile_mem[TEXT_CHARBLOCK][n * GLYPH_COUNT];
    u32 offset = 2 * n;
    for (u32 g = 0; g < GLYPH_COUNT; g++) {
        u32 above = 0;
        for (u32 r = 0; r < 8; r++) {
            u32 bits = *src++;
            u32 shade = shadow ? (above << 1) & ~bits & 0xFF : 0;
            u32 row = pixels(bits) | pixels(shade) << 1;
            *dst++ = row + ((row | row >> 1) & 0x11111111u) * offset;
            above = bits;
        }
    }
    loaded = (u8)(loaded | 1u << n);
}

static void set_palette(u32 n) {
    pal_bg_bank[TEXT_PALBANK][2 * n + 1] = colors[n][0];
    pal_bg_bank[TEXT_PALBANK][2 * n + 2] = colors[n][1];
}

static void text_init(void) {
    loaded = 0;
    load_style(0); // tile 0, the blank cell, is style 0's space
    if (style != 0)
        load_style(style);
    for (u32 n = 0; n < TEXT_STYLES; n++)
        set_palette(n);
    memset32(&se_mem[TEXT_SCREENBLOCK][0], 0, sizeof(SCREENBLOCK) / 4);

    REG_BG0CNT =
        BG_CBB(TEXT_CHARBLOCK) | BG_SBB(TEXT_SCREENBLOCK) | BG_4BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    REG_DISPCNT |= DCNT_BG0;
    ready = true;
}

static bool valid_style(const char* function, int n) {
    if (n >= 0 && n < TEXT_STYLES)
        return true;
#ifdef SERVAL_DEBUG
    if (!warned_style) {
        warned_style = true;
        SERVAL_WARN("%s: style %d does not exist (0 to TEXT_STYLES - 1 = %d); ignored", function, n,
                    TEXT_STYLES - 1);
    }
#else
    (void)function;
#endif
    return false;
}

void text_set_style(int n) {
    if (!valid_style("text_set_style", n))
        return;
    style = (u8)n;
    if (ready && !(loaded & 1u << n))
        load_style((u32)n);
}

// Colors and the shadow apply at once if the layer is set up, else when the
// first text call sets it up.
void text_set_style_color(int n, Color text, Color shadow_col) {
    if (!valid_style("text_set_style_color", n))
        return;
    colors[n][0] = text;
    colors[n][1] = shadow_col;
    if (ready)
        set_palette((u32)n);
}

void text_set_color(Color text, Color shadow_col) {
    text_set_style_color(TEXT_NORMAL, text, shadow_col);
}

void text_set_shadow(bool on) {
    if (on == shadow)
        return;
    shadow = on;
    if (!ready)
        return;
    for (u32 n = 0; n < TEXT_STYLES; n++) {
        if (loaded & 1u << n)
            load_style(n);
    }
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

// Writes s from column col of a row, keeping to columns lo to hi - 1. `base`
// plus a character is its map entry: palette bank and the style's tile for
// the glyph. Inlined, so text_print's copy (print) has the limits as
// constants.
static inline __attribute__((always_inline)) void print_within(int col, int row, const char* s,
                                                               int lo, int hi, u32 base) {
    if (!ready)
        text_init();
    if (row < 0 || row >= TEXT_ROWS)
        return;
    SCR_ENTRY* map = &se_mem[TEXT_SCREENBLOCK][row * 32];
    for (; *s && col < hi; s++, col++) {
        if (col < lo)
            continue;
        u32 c = (unsigned char)*s;
        // Expected not taken: on the GBA a taken branch refills the
        // pipeline from ROM, a few cycles per character.
        if (__builtin_expect(c - FIRST_GLYPH >= GLYPH_COUNT, 0))
            c = '?';
        map[col] = (SCR_ENTRY)(base + c);
    }
}

static u32 entry_base(u32 palbank, u32 n) {
    return SE_PALBANK(palbank) + n * GLYPH_COUNT - FIRST_GLYPH;
}

static __attribute__((noinline)) void print(int col, int row, const char* s, u32 base) {
    print_within(col, row, s, 0, TEXT_COLS, base);
}

// A string to print: NULL, or a number passed for one, is refused (nothing
// changes, not even the layer's setup), so no garbage is printed from
// whatever it points at.
static bool valid_string(const char* function, const char* s) {
    if (serval_plausible_pointer(s))
        return true;
#ifdef SERVAL_DEBUG
    if (!warned_string) {
        warned_string = true;
        SERVAL_WARN("%s: the string is NULL or not a valid pointer (0x%x); nothing printed",
                    function, (u32)(uintptr_t)s);
    }
#else
    (void)function;
#endif
    return false;
}

// For the splash screen: style 0's glyphs through another palette bank.
void serval_text_print_bank(int col, int row, const char* s, u32 palbank) {
    print(col, row, s, entry_base(palbank, 0));
}

void text_print(int col, int row, const char* s) {
    if (valid_string("text_print", s))
        print(col, row, s, entry_base(TEXT_PALBANK, style));
}

// Blanks columns lo to hi - 1 of a row, clipped to the screen.
static inline void blank(int row, int lo, int hi) {
    if (row < 0 || row >= TEXT_ROWS)
        return;
    for (int c = lo < 0 ? 0 : lo; c < hi && c < TEXT_COLS; c++)
        se_mem[TEXT_SCREENBLOCK][row * 32 + c] = 0;
}

void text_print_line(int col, int row, const char* s) {
    if (!valid_string("text_print_line", s))
        return;
    print(col, row, s, entry_base(TEXT_PALBANK, style));
    int end = col;
    while (*s++)
        end++;
    blank(row, end, TEXT_COLS);
}

void text_clear(void) {
    if (!ready)
        text_init();
    memset32(&se_mem[TEXT_SCREENBLOCK][0], 0, sizeof(SCREENBLOCK) / 4);
}

static bool valid_area(const char* function, int width, int height) {
    if (width >= 0 && height >= 0)
        return true;
#ifdef SERVAL_DEBUG
    if (!warned_area) {
        warned_area = true;
        SERVAL_WARN("%s: negative size (width %d, height %d); nothing changed", function, width,
                    height);
    }
#else
    (void)function;
#endif
    return false;
}

void text_clear_area(int col, int row, int width, int height) {
    if (!valid_area("text_clear_area", width, height))
        return;
    if (!ready)
        text_init();
    for (int r = row; r < row + height; r++)
        blank(r, col, col + width);
}

static void print_centered_in(const char* function, int col, int width, int row, const char* s) {
    if (!valid_area(function, width, 0) || !valid_string(function, s))
        return;
    int length = 0;
    while (s[length])
        length++;
    if (!ready)
        text_init();
    blank(row, col, col + width);
    print_within(col + (width - length) / 2, row, s, col < 0 ? 0 : col,
                 col + width < TEXT_COLS ? col + width : TEXT_COLS,
                 entry_base(TEXT_PALBANK, style));
}

void text_print_centered_in(int col, int width, int row, const char* s) {
    print_centered_in("text_print_centered_in", col, width, row, s);
}

void text_print_centered(int row, const char* s) {
    print_centered_in("text_print_centered", 0, TEXT_COLS, row, s);
}
