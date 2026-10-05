// Tests for presentation features on the GBA: screen brightness (screen.c),
// text colors, shadow and centering (text.c), hidden sprites (sprites.c),
// animated tiles and drawing a map layer at load time (map.c).

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/screen.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"
#include "../../src/gba/screen_internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

static void show_frame(void) {
    frame_begin();
    frame_end();
}

// --- Brightness ---------------------------------------------------------------

static void brightness_sets_the_blend_registers(void) {
    screen_set_brightness(-8);
    CHECK(REG_BLDCNT == (BLD_ALL | BLD_BACKDROP | BLD_BLACK));
    CHECK(serval_screen_brightness() == -8);
    screen_set_brightness(16);
    CHECK(REG_BLDCNT == (BLD_ALL | BLD_BACKDROP | BLD_WHITE));
    CHECK(serval_screen_brightness() == 16);
    u32 before = debug_warning_count();
    screen_set_brightness(-40); // clamped to black
    screen_set_brightness(99);  // clamped to white, reported once
    CHECK(debug_warning_count() == before + WARNINGS(1));
    CHECK(serval_screen_brightness() == 16);
    screen_set_brightness(0);
    CHECK(REG_BLDCNT == 0);
    CHECK(serval_screen_brightness() == 0);
}

// --- Text ---------------------------------------------------------------------

#define TEXT_MAP se_mem[31]
#define GLYPH(c) ((c) - ' ')

// The 4bpp color of pixel (x, y) of the font tile of character c.
static u32 glyph_pixel(char c, u32 x, u32 y) {
    return (tile_mem[0][GLYPH(c)].data[y] >> (4 * x)) & 15;
}

// True if every pixel of glyph c is 0, 1 or (with a shadow) 2, and the color
// 2 pixels are exactly those right of and below a color 1 pixel that aren't
// color 1 themselves.
static bool glyph_ok(char c, bool shadow) {
    for (u32 y = 0; y < 8; y++) {
        for (u32 x = 0; x < 8; x++) {
            u32 p = glyph_pixel(c, x, y);
            bool lit = p == 1;
            bool casts = x > 0 && y > 0 && glyph_pixel(c, x - 1, y - 1) == 1;
            u32 expected = lit ? 1 : shadow && casts ? 2 : 0;
            if (p != expected)
                return false;
        }
    }
    return true;
}

static bool glyph_has_color(char c, u32 color) {
    for (u32 y = 0; y < 8; y++) {
        for (u32 x = 0; x < 8; x++) {
            if (glyph_pixel(c, x, y) == color)
                return true;
        }
    }
    return false;
}

static void text_shadow_and_colors(void) {
    text_print(0, 0, " "); // sets the layer up
    CHECK(glyph_ok('A', false) && !glyph_has_color('A', 2));
    CHECK(pal_bg_bank[15][1] == RGB15(31, 31, 31));

    text_set_shadow(true);
    CHECK(serval_text_shadow());
    CHECK(glyph_ok('A', true) && glyph_ok('g', true) && glyph_ok('_', true));
    CHECK(glyph_has_color('A', 2));
    CHECK(tile_mem[0][0].data[0] == 0); // ' ' stays blank

    text_set_color(RGB15(31, 31, 0), RGB15(4, 0, 8));
    CHECK(pal_bg_bank[15][1] == RGB15(31, 31, 0) && pal_bg_bank[15][2] == RGB15(4, 0, 8));

    text_set_shadow(false);
    CHECK(glyph_ok('A', false) && !glyph_has_color('A', 2));
    text_set_color(RGB15(31, 31, 31), RGB15(0, 0, 0));
    CHECK(pal_bg_bank[15][1] == RGB15(31, 31, 31) && pal_bg_bank[15][2] == 0);
}

// The splash screen borrows the blend registers and the text layer, draws
// its text without a shadow, and puts the game's settings back.
static void splash_keeps_brightness_and_shadow(void) {
    screen_set_brightness(-5);
    text_set_shadow(true);
    text_print(0, 0, " ");
    serval_splash();
    CHECK(REG_BLDCNT == (BLD_ALL | BLD_BACKDROP | BLD_BLACK));
    CHECK(serval_screen_brightness() == -5);
    CHECK(serval_text_shadow() && glyph_ok('A', true));
    text_set_shadow(false);
    screen_set_brightness(0);
}

static void print_centered_centers_and_blanks_the_row(void) {
    text_clear();
    text_print(0, 5, "old text on the whole row here");
    text_print_centered(5, "HI");
    CHECK((TEXT_MAP[5 * 32 + 14] & SE_ID_MASK) == GLYPH('H')); // (30 - 2) / 2
    CHECK((TEXT_MAP[5 * 32 + 15] & SE_ID_MASK) == GLYPH('I'));
    CHECK(TEXT_MAP[5 * 32 + 0] == 0 && TEXT_MAP[5 * 32 + 16] == 0 && TEXT_MAP[5 * 32 + 29] == 0);
    text_print_centered(6, "ODD"); // (30 - 3) / 2 = 13
    CHECK((TEXT_MAP[6 * 32 + 13] & SE_ID_MASK) == GLYPH('O'));
    text_clear();
}

// --- Hidden sprites -------------------------------------------------------------

static const u32 sprite_tiles[8];
static const SpriteAsset sprite = {.size = SPRITE_8x8, .tiles = sprite_tiles};
static const SpriteAsset* const sprite_table[1] = {&sprite};
static const u16 sprite_palette[16];
static const SpriteGroup sprite_group = {
    .palettes = sprite_palette, .sprite_count = 1, .palette_count = 1};

static void hidden_sprites_are_not_drawn(void) {
    sprite_table_set(sprite_table, 1);
    CHECK(sprite_group_load(&sprite_group));
    frame_begin();
    sprite_draw(0, 0, 10, 10, SPRITE_HIDDEN);
    sprite_draw_rotated(0, 0, 10, 10, 1000, SPRITE_HIDDEN | SPRITE_FLIP_H);
    CHECK(serval_oam_used == 0 && serval_matrices_used == 0);
    sprite_draw(0, 0, 10, 10, 0);
    CHECK(serval_oam_used == 1);

    ecs_reset();
    Entity e = entity_create(C_POS | C_SPR);
    spr_flags[entity_index(e)] = SPRITE_HIDDEN;
    sys_render();
    sys_render_by_depth();
    CHECK(serval_oam_used == 1);
    spr_flags[entity_index(e)] = 0;
    sys_render();
    CHECK(serval_oam_used == 2);
    frame_end();
    ecs_reset();
    sprite_table_set(NULL, 0);
}

// --- Animated tiles -------------------------------------------------------------

#define TILESET_TILES 12
static const u32 base_tiles[TILESET_TILES * 8]; // all zero
static u32 new_tiles[3 * 8];

static const u32* tileset_vram(u32 tile) {
    return (const u32*)&tile_mem[1][tile];
}

static void set_tiles_copies_in_vblank(void) {
    const Tileset tileset = {.tiles = base_tiles, .tile_count = TILESET_TILES};
    CHECK(tileset_load(&tileset));
    for (u32 k = 0; k < 3 * 8; k++)
        new_tiles[k] = 0x11111111u * (k / 8 + 1);

    tileset_set_tiles(4, new_tiles, 2);
    CHECK(tileset_vram(4)[0] == 0); // not before frame_end
    show_frame();
    CHECK(tileset_vram(4)[0] == 0x11111111u && tileset_vram(5)[7] == 0x22222222u);
    CHECK(tileset_vram(3)[7] == 0 && tileset_vram(6)[0] == 0);

    // A second call for the same tiles in a frame replaces the first.
    tileset_set_tiles(8, new_tiles, 1);
    tileset_set_tiles(8, new_tiles + 16, 1);
    show_frame();
    CHECK(tileset_vram(8)[0] == 0x33333333u);
    show_frame(); // the queue is empty again
    CHECK(tileset_vram(8)[0] == 0x33333333u);
    CHECK(tileset_load(&tileset)); // back to all zero
}

static void set_tiles_rejects_bad_calls(void) {
    const Tileset tileset = {.tiles = base_tiles, .tile_count = TILESET_TILES};
    CHECK(tileset_load(&tileset));
    u32 before = debug_warning_count();
    tileset_set_tiles(TILESET_TILES - 1, new_tiles, 2); // past the tileset
    tileset_set_tiles(0, NULL, 1);
    for (u16 k = 0; k <= MAP_MAX_TILE_UPDATES; k++) // one too many
        tileset_set_tiles(k, new_tiles, 1);
    CHECK(debug_warning_count() == before + WARNINGS(3));
    show_frame();
    CHECK(tileset_vram(TILESET_TILES - 1)[0] == 0);
    CHECK(tileset_vram(MAP_MAX_TILE_UPDATES - 1)[0] == 0x11111111u);
    CHECK(tileset_vram(MAP_MAX_TILE_UPDATES)[0] == 0); // dropped

    // tileset_load drops what is still queued.
    tileset_set_tiles(0, new_tiles + 8, 1);
    CHECK(tileset_load(&tileset));
    show_frame();
    CHECK(tileset_vram(0)[0] == 0);
}

// --- Drawing a layer at load time -----------------------------------------------

static const Metatile metatiles[2] = {
    {.se = {0, 0, 0, 0}},
    {.se = {MAP_SE(1, 0, 0), MAP_SE(2, 0, 0), MAP_SE(3, 0, 0), MAP_SE(4, 1, MAP_SE_FLIP_H)}},
};
static const u16 cells[4 * 3] = {1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1};

static void map_load_draws_at_once(void) {
    const MapLayer layer = {.width = 4,
                            .height = 3,
                            .cells = cells,
                            .metatiles = metatiles,
                            .metatile_count = 2,
                            .bg = 1};
    camera_set(0, 0);
    CHECK(map_load(&layer));
    // Shown, with the window drawn, before any frame_end().
    CHECK(REG_DISPCNT & DCNT_BG1);
    CHECK(REG_BGCNT[1] == (BG_CBB(1) | BG_SBB(28) | BG_PRIO(1)));
    const u16* sb = se_mem[28];
    CHECK(sb[0] == MAP_SE(1, 0, 0) && sb[1] == MAP_SE(2, 0, 0));
    CHECK(sb[32] == MAP_SE(3, 0, 0) && sb[33] == MAP_SE(4, 1, MAP_SE_FLIP_H));
    CHECK(sb[2] == 0 && sb[2 * 32 + 2] == MAP_SE(1, 0, 0));
    CHECK(sb[6 * 32 + 8] == 0); // outside the layer
    show_frame();               // and it stays right
    CHECK(sb[2 * 32 + 2] == MAP_SE(1, 0, 0) && (REG_DISPCNT & DCNT_BG1));
    map_unload(1);
    show_frame();
    CHECK(!(REG_DISPCNT & DCNT_BG1));
}

TEST_SUITE(gba_present_tests, "gba present",
           {"brightness_sets_the_blend_registers", brightness_sets_the_blend_registers},
           {"text_shadow_and_colors", text_shadow_and_colors},
           {"splash_keeps_brightness_and_shadow", splash_keeps_brightness_and_shadow},
           {"print_centered_centers_and_blanks_the_row", print_centered_centers_and_blanks_the_row},
           {"hidden_sprites_are_not_drawn", hidden_sprites_are_not_drawn},
           {"set_tiles_copies_in_vblank", set_tiles_copies_in_vblank},
           {"set_tiles_rejects_bad_calls", set_tiles_rejects_bad_calls},
           {"map_load_draws_at_once", map_load_draws_at_once});
