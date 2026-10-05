// Tests for the GBA text layer (src/gba/text.c): what ends up in the BG0 map.

#include "../test.h"
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

TEST_SUITE(gba_text_tests, "gba_text",
           {"print_writes_glyphs_and_enables_layer", print_writes_glyphs_and_enables_layer},
           {"print_clips_and_replaces_unprintable", print_clips_and_replaces_unprintable},
           {"print_line_blanks_the_rest_of_the_row", print_line_blanks_the_rest_of_the_row},
           {"hud_is_drawn_above_sprites", hud_is_drawn_above_sprites},
           {"clear_blanks_the_map", clear_blanks_the_map}, {"font_is_loaded", font_is_loaded});
