// Tests for the GBA text layer (src/gba/text.c): what ends up in the BG0 map.

#include "../test.h"
#include "serval/debug.h"
#include "serval/text.h"

#include <tonc.h>

#define TEXT_MAP se_mem[31]
#define GLYPH(c) ((c) - ' ')

static void print_writes_glyphs_and_enables_layer(void) {
    text_clear();
    text_print(2, 3, "Hi!");
    CHECK((TEXT_MAP[3 * 32 + 2] & SE_ID_MASK) == GLYPH('H'));
    CHECK((TEXT_MAP[3 * 32 + 3] & SE_ID_MASK) == GLYPH('i'));
    CHECK((TEXT_MAP[3 * 32 + 4] & SE_ID_MASK) == GLYPH('!'));
    CHECK((TEXT_MAP[3 * 32 + 2] & SE_PALBANK_MASK) == SE_PALBANK(15));
    CHECK(REG_DISPCNT & DCNT_BG0);
}

static void print_clips_and_replaces_unprintable(void) {
    text_clear();
    text_print(TEXT_COLS - 2, 0, "abcd"); // only "ab" fits
    CHECK((TEXT_MAP[TEXT_COLS - 2] & SE_ID_MASK) == GLYPH('a'));
    CHECK((TEXT_MAP[TEXT_COLS - 1] & SE_ID_MASK) == GLYPH('b'));
    CHECK(TEXT_MAP[TEXT_COLS] == 0); // column 30 (off screen) untouched

    text_print(-1, 1, "xy"); // starts left of the screen
    CHECK((TEXT_MAP[32] & SE_ID_MASK) == GLYPH('y'));

    text_print(0, 2, "\t");
    CHECK((TEXT_MAP[64] & SE_ID_MASK) == GLYPH('?'));

    text_print(0, TEXT_ROWS, "z"); // row off screen: ignored
    text_print(0, -1, "z");
}

static void print_line_blanks_the_rest_of_the_row(void) {
    text_clear();
    text_print(0, 4, "SCORE 10");
    text_print_line(0, 4, "SCORE 9");
    CHECK((TEXT_MAP[4 * 32 + 6] & SE_ID_MASK) == GLYPH('9'));
    CHECK(TEXT_MAP[4 * 32 + 7] == 0); // the old '0' is gone
    CHECK(TEXT_MAP[4 * 32 + TEXT_COLS - 1] == 0);
    text_print(0, 5, "keep");
    text_print_line(0, 4, "x");
    CHECK((TEXT_MAP[5 * 32] & SE_ID_MASK) == GLYPH('k')); // other rows untouched
}

static void hud_is_drawn_above_sprites(void) {
    text_print(0, 0, " ");
    CHECK((REG_BG0CNT & BG_PRIO_MASK) == BG_PRIO(0)); // sprites default to priority 2
}

static void clear_blanks_the_map(void) {
    text_print(0, 0, "x");
    text_clear();
    CHECK(TEXT_MAP[0] == 0);
}

static void font_is_loaded(void) {
    text_print(0, 0, " "); // ensures the font is set up
    const TILE* a = &tile_mem[0][GLYPH('A')];
    u32 bits = 0;
    for (int i = 0; i < 8; i++)
        bits |= a->data[i];
    CHECK(bits != 0);                   // 'A' has pixels
    CHECK((bits & 0xEEEEEEEE) == 0);    // ... all in color 1
    CHECK(tile_mem[0][0].data[0] == 0); // ' ' is blank
    CHECK(pal_bg_bank[15][1] == RGB15(31, 31, 31));
}

#define ENTRY(col, row) TEXT_MAP[(row) * 32 + (col)]

// Every nibble of a glyph tile's pixels, checked against the colors allowed.
static bool tile_uses_only(const TILE* t, u32 a, u32 b, bool need_b) {
    bool saw_a = false, saw_b = false;
    for (int i = 0; i < 8; i++) {
        for (int shift = 0; shift < 32; shift += 4) {
            u32 px = t->data[i] >> shift & 15;
            if (px != 0 && px != a && px != b)
                return false;
            saw_a |= px == a;
            saw_b |= px == b;
        }
    }
    return saw_a && (saw_b || !need_b);
}

static void styles_print_their_own_glyphs_and_colors(void) {
    text_clear();
    text_print(0, 0, "A");
    text_set_style(TEXT_HIGHLIGHT);
    text_print(1, 0, "A");
    text_set_style(3);
    text_print(2, 0, "A");
    text_set_style(TEXT_NORMAL);
    text_print(3, 0, "A");
    CHECK(ENTRY(0, 0) == (SE_PALBANK(15) | GLYPH('A')));
    CHECK(ENTRY(1, 0) == (SE_PALBANK(15) | (96 + GLYPH('A'))));
    CHECK(ENTRY(2, 0) == (SE_PALBANK(15) | (3 * 96 + GLYPH('A'))));
    CHECK(ENTRY(3, 0) == ENTRY(0, 0));
    // Same shape, colors 3 (style 1) and 7 (style 3) instead of 1.
    CHECK(tile_uses_only(&tile_mem[0][GLYPH('A')], 1, 1, false));
    CHECK(tile_uses_only(&tile_mem[0][96 + GLYPH('A')], 3, 3, false));
    CHECK(tile_uses_only(&tile_mem[0][3 * 96 + GLYPH('A')], 7, 7, false));
    CHECK(tile_mem[0][96].data[3] == 0); // style 1's space is blank
    // Defaults: white, yellow, light red, grey text; black shadows.
    CHECK(pal_bg_bank[15][1] == RGB15(31, 31, 31) && pal_bg_bank[15][2] == 0);
    CHECK(pal_bg_bank[15][3] == COLOR_RGB(255, 224, 0) && pal_bg_bank[15][4] == 0);
    CHECK(pal_bg_bank[15][5] == COLOR_RGB(255, 96, 96));
    CHECK(pal_bg_bank[15][7] == COLOR_RGB(160, 160, 160) && pal_bg_bank[15][8] == 0);
}

static void style_colors_and_shadow(void) {
    text_print(0, 0, " ");
    text_set_style_color(TEXT_HIGHLIGHT, COLOR_RGB(0, 255, 0), COLOR_RGB(0, 0, 255));
    CHECK(pal_bg_bank[15][3] == RGB15(0, 31, 0) && pal_bg_bank[15][4] == RGB15(0, 0, 31));
    CHECK(pal_bg_bank[15][1] == RGB15(31, 31, 31)); // other styles keep theirs
    text_set_color(COLOR_RGB(255, 0, 0), COLOR_RGB(0, 0, 0));
    CHECK(pal_bg_bank[15][1] == RGB15(31, 0, 0) && pal_bg_bank[15][3] == RGB15(0, 31, 0));

    text_set_style(TEXT_HIGHLIGHT);
    text_set_shadow(true); // rewrites every loaded style with its shadow
    CHECK(tile_uses_only(&tile_mem[0][GLYPH('A')], 1, 2, true));
    CHECK(tile_uses_only(&tile_mem[0][96 + GLYPH('A')], 3, 4, true));
    text_set_shadow(false);
    CHECK(tile_uses_only(&tile_mem[0][96 + GLYPH('A')], 3, 3, false));

    u32 warnings = debug_warning_count();
    text_set_style(TEXT_STYLES); // ignored
    text_set_style_color(-1, 0, 0);
    text_print(0, 1, "A");
    CHECK(ENTRY(0, 1) == (SE_PALBANK(15) | (96 + GLYPH('A')))); // still the highlight
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    text_set_style(TEXT_NORMAL);
    text_set_color(COLOR_RGB(255, 255, 255), COLOR_RGB(0, 0, 0));
    text_set_style_color(TEXT_HIGHLIGHT, COLOR_RGB(255, 224, 0), COLOR_RGB(0, 0, 0));
}

static void centered_in_keeps_to_its_columns(void) {
    text_clear();
    text_print(0, 2, "##############################"); // 30 columns
    text_print_centered_in(0, 22, 2, "GO");             // a 22-column field
    CHECK(ENTRY(0, 2) == 0 && ENTRY(9, 2) == 0);
    CHECK((ENTRY(10, 2) & SE_ID_MASK) == GLYPH('G'));
    CHECK((ENTRY(11, 2) & SE_ID_MASK) == GLYPH('O'));
    CHECK(ENTRY(12, 2) == 0 && ENTRY(21, 2) == 0);
    CHECK((ENTRY(22, 2) & SE_ID_MASK) == GLYPH('#')); // the panel is untouched

    text_print_centered_in(22, 8, 2, "ODD"); // (8 - 3) / 2: one left of center
    CHECK((ENTRY(24, 2) & SE_ID_MASK) == GLYPH('O'));
    CHECK(ENTRY(23, 2) == 0 && ENTRY(27, 2) == 0 && ENTRY(29, 2) == 0);
    CHECK(ENTRY(21, 2) == 0); // the field's blank cell, unchanged

    text_print(0, 3, "##############################");
    text_print_centered_in(4, 4, 3, "ABCDEFGH"); // too wide: loses both ends
    CHECK((ENTRY(3, 3) & SE_ID_MASK) == GLYPH('#'));
    CHECK((ENTRY(4, 3) & SE_ID_MASK) == GLYPH('C'));
    CHECK((ENTRY(7, 3) & SE_ID_MASK) == GLYPH('F'));
    CHECK((ENTRY(8, 3) & SE_ID_MASK) == GLYPH('#'));

    text_print_centered_in(26, 8, 4, "XY"); // partly off screen: clipped
    CHECK((ENTRY(29, 4) & SE_ID_MASK) == GLYPH('X'));
    CHECK(TEXT_MAP[4 * 32 + 30] == 0);

    text_print_centered(5, "MID"); // the whole row, as before
    CHECK((ENTRY(13, 5) & SE_ID_MASK) == GLYPH('M'));
}

static void clear_area_clears_only_the_rectangle(void) {
    text_clear();
    for (int row = 0; row < TEXT_ROWS; row++)
        text_print(0, row, "##############################");
    text_clear_area(22, 16, 8, 3);
    CHECK(ENTRY(22, 16) == 0 && ENTRY(29, 18) == 0);
    CHECK((ENTRY(21, 16) & SE_ID_MASK) == GLYPH('#'));
    CHECK((ENTRY(22, 15) & SE_ID_MASK) == GLYPH('#'));
    CHECK((ENTRY(22, 19) & SE_ID_MASK) == GLYPH('#'));
    text_clear_area(-2, -1, 4, 2); // clipped at the top left
    CHECK(ENTRY(0, 0) == 0 && ENTRY(1, 0) == 0);
    CHECK((ENTRY(2, 0) & SE_ID_MASK) == GLYPH('#'));
    CHECK((ENTRY(0, 1) & SE_ID_MASK) == GLYPH('#'));
    text_clear_area(28, 19, 10, 10); // clipped at the bottom right
    CHECK(ENTRY(29, 19) == 0 && TEXT_MAP[19 * 32 + 30] == 0);
    CHECK(TEXT_MAP[20 * 32] == 0); // row 20 (off screen) never written

    u32 warnings = debug_warning_count();
    text_clear_area(0, 0, -1, 5);
    text_print_centered_in(0, -3, 6, "x");
    CHECK((ENTRY(5, 6) & SE_ID_MASK) == GLYPH('#'));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    text_clear();
}

TEST_SUITE(gba_text_tests, "gba_text",
           {"print_writes_glyphs_and_enables_layer", print_writes_glyphs_and_enables_layer},
           {"print_clips_and_replaces_unprintable", print_clips_and_replaces_unprintable},
           {"print_line_blanks_the_rest_of_the_row", print_line_blanks_the_rest_of_the_row},
           {"hud_is_drawn_above_sprites", hud_is_drawn_above_sprites},
           {"clear_blanks_the_map", clear_blanks_the_map}, {"font_is_loaded", font_is_loaded},
           {"styles_print_their_own_glyphs_and_colors", styles_print_their_own_glyphs_and_colors},
           {"style_colors_and_shadow", style_colors_and_shadow},
           {"centered_in_keeps_to_its_columns", centered_in_keeps_to_its_columns},
           {"clear_area_clears_only_the_rectangle", clear_area_clears_only_the_rectangle});
