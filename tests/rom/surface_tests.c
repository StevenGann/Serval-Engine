// Tests for the pixel surface on the GBA (src/gba/surface.c, surface.h):
// display mode 4 and its two pages, the flip and the transform in VBlank,
// its colors, the sprite tiles it leaves, and what it refuses and makes the
// text layer, the map and the splash refuse while it is on. What the screen
// shows for a bitmap page and a transform is the web renderer's tests
// (tests/web_ppu_tests.c).

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/map.h"
#include "serval/screen.h"
#include "serval/sprites.h"
#include "serval/surface.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS_ON 1u
#else
#define WARNINGS_ON 0u
#endif

// Cycle budgets only hold for optimized code.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

#define PAGE_BYTES (SURFACE_W * SURFACE_H)
#define DCNT_BGS (DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3)

static u8* const front = (u8*)MEM_VRAM_FRONT;
static u8* const back = (u8*)MEM_VRAM_BACK;

static void show_frame(void) {
    frame_begin();
    frame_end();
}

// True if every byte of a page is `value`.
static bool page_is(const u8* page, u32 value) {
    const u32* p = (const u32*)page;
    for (u32 k = 0; k < PAGE_BYTES / 4; k++) {
        if (p[k] != value * 0x01010101u)
            return false;
    }
    return true;
}

static bool transform_is(FIXED x0, FIXED y0, FIXED xx, FIXED xy, FIXED yx, FIXED yy) {
    FIXED t[6];
    serval_surface_transform(t);
    return t[0] == x0 && t[1] == y0 && t[2] == xx && t[3] == xy && t[4] == yx && t[5] == yy;
}

// A sprite group of one 8x8 sprite.
static const u32 dot_tiles[8] = {0x11111111, 0x22222222};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
static const SpriteAsset* const table[] = {&dot};
static const u16 palettes[16] = {[1] = 0x7FFF};
static const SpriteGroup group = {.palettes = palettes, .sprite_count = 1, .palette_count = 1};

// A one-metatile map layer and its tileset.
static const u32 tileset_tiles[2 * 8] = {[8] = 0x33333333};
static const u16 tileset_palette[16] = {[1] = 0x1234};
static const Tileset tileset = {
    .tiles = tileset_tiles, .palettes = tileset_palette, .tile_count = 2, .palette_count = 1};
static const Metatile metatiles[1] = {{.se = {1, 1, 1, 1}}};
static const u16 cells[1] = {0};
static const MapLayer layer = {
    .width = 1, .height = 1, .cells = cells, .metatiles = metatiles, .metatile_count = 1, .bg = 2};

// --- On and off -----------------------------------------------------------------

// surface_begin() switches to mode 4 with background 2 alone and both pages
// transparent, at once; surface_end() goes back to mode 0 with no background.
static void begin_and_end_switch_the_display_mode(void) {
    u32 before = debug_warning_count();
    show_frame();
    u16 kept = REG_DISPCNT & (DCNT_OBJ | DCNT_OBJ_1D);
    CHECK(kept == (DCNT_OBJ | DCNT_OBJ_1D));
    CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE0);
    // Whatever the tiled backgrounds left in VRAM is not shown as pixels.
    memset32(front, 0x5A5A5A5A, PAGE_BYTES / 4);
    memset32(back, 0xA5A5A5A5, PAGE_BYTES / 4);
    CHECK(surface_begin());
    CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE4);
    CHECK((REG_DISPCNT & DCNT_BGS) == DCNT_BG2);
    CHECK(!(REG_DISPCNT & DCNT_PAGE));
    CHECK((REG_DISPCNT & (DCNT_OBJ | DCNT_OBJ_1D)) == kept);
    CHECK((REG_BG2CNT & BG_PRIO_MASK) == BG_PRIO(2));
    CHECK(page_is(front, 0) && page_is(back, 0));
    CHECK(transform_is(0, 0, FX_ONE, 0, 0, FX_ONE));
    // The page to draw is the one off screen.
    CHECK(surface_pixels() == back);
    show_frame();
    CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE4);
    CHECK((REG_DISPCNT & DCNT_BGS) == DCNT_BG2);
    CHECK(surface_pixels() == back);

    surface_end();
    CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE0);
    CHECK((REG_DISPCNT & DCNT_BGS) == 0);
    CHECK((REG_DISPCNT & (DCNT_OBJ | DCNT_OBJ_1D)) == kept);
    show_frame();
    CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE0);
    CHECK((REG_DISPCNT & DCNT_BGS) == 0);
    CHECK(debug_warning_count() == before);
}

// A game may set DISPCNT's "H-Blank interval free" bit (docs/core-api.md):
// the surface keeps it, on, flipping and off.
static void other_display_bits_are_kept(void) {
    REG_DISPCNT |= DCNT_OAM_HBL;
    CHECK(surface_begin());
    CHECK(REG_DISPCNT & DCNT_OAM_HBL);
    surface_flip();
    show_frame();
    CHECK(REG_DISPCNT & DCNT_OAM_HBL);
    surface_end();
    CHECK(REG_DISPCNT & DCNT_OAM_HBL);
    REG_DISPCNT &= ~DCNT_OAM_HBL;
}

// --- Pages ------------------------------------------------------------------------

// surface_flip() swaps the pages at the next frame_end(), not at the call,
// and once however often it is called; without it the same page stays.
static void flip_swaps_the_pages_in_vblank(void) {
    u32 before = debug_warning_count();
    CHECK(surface_begin());
    CHECK(surface_pixels() == back);
    surface_flip();
    CHECK(surface_pixels() == back); // not yet
    CHECK(!(REG_DISPCNT & DCNT_PAGE));
    show_frame();
    CHECK(REG_DISPCNT & DCNT_PAGE); // the second page is on screen
    CHECK(surface_pixels() == front);
    show_frame(); // no flip: kept
    CHECK(REG_DISPCNT & DCNT_PAGE);
    CHECK(surface_pixels() == front);
    surface_flip();
    surface_flip(); // one flip
    show_frame();
    CHECK(!(REG_DISPCNT & DCNT_PAGE));
    CHECK(surface_pixels() == back);
    // A new surface starts on the first page again, whatever the last showed.
    surface_flip();
    show_frame();
    CHECK(REG_DISPCNT & DCNT_PAGE);
    surface_end();
    CHECK(!(REG_DISPCNT & DCNT_PAGE));
    CHECK(surface_begin());
    CHECK(surface_pixels() == back);
    show_frame();
    CHECK(!(REG_DISPCNT & DCNT_PAGE));
    surface_end();
    CHECK(debug_warning_count() == before);
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// surface_clear() fills the page being drawn and leaves the one on screen;
// a game's 16-bit and 32-bit stores land where surface.h says pixels are.
static void clear_and_pixels_reach_the_hidden_page(void) {
    CHECK(surface_begin());
    u32 start = cycles();
    surface_clear(7);
    u32 clear_cycles = cycles() - start;
    debug_log(text_format("  surface_clear: %u cycles", clear_cycles));
    CHECK_TIMING(clear_cycles < 30000); // surface.h says about 20,000
    CHECK(page_is(back, 7));
    CHECK(page_is(front, 0));
    u8* p = surface_pixels();
    *(u16*)(void*)&p[3 * SURFACE_W + 10] = 0x0201; // pixels (10, 3) and (11, 3)
    *(u32*)(void*)&p[159 * SURFACE_W + 236] = 0x06050403;
    CHECK(back[3 * SURFACE_W + 10] == 1 && back[3 * SURFACE_W + 11] == 2);
    CHECK(back[PAGE_BYTES - 4] == 3 && back[PAGE_BYTES - 1] == 6);
    surface_flip();
    show_frame();
    surface_clear(0xFF);
    CHECK(page_is(front, 0xFF));
    CHECK(back[3 * SURFACE_W + 10] == 1); // the page on screen now
    surface_end();
    // The begin's own cost, both pages cleared: logged for docs/surface.md.
    start = cycles();
    CHECK(surface_begin());
    debug_log(text_format("  surface_begin: %u cycles", cycles() - start));
    surface_end();
}

// --- Transform --------------------------------------------------------------------

// The transform reaches background 2's registers at the next frame_end(), and
// out-of-range values are clamped to what the registers hold.
static void transform_applies_in_vblank(void) {
    u32 before = debug_warning_count();
    CHECK(surface_begin());
    surface_set_transform(FX(3), FX(-2), FX_ONE / 2, 0, 0, FX_ONE / 2);
    CHECK(transform_is(0, 0, FX_ONE, 0, 0, FX_ONE)); // not yet
    show_frame();
    CHECK(transform_is(FX(3), FX(-2), FX_ONE / 2, 0, 0, FX_ONE / 2));
    show_frame(); // kept
    CHECK(transform_is(FX(3), FX(-2), FX_ONE / 2, 0, 0, FX_ONE / 2));
    // Screen columns as surface rows: the last call of a frame wins.
    surface_set_transform(0, 0, FX(2), 0, 0, FX(2));
    surface_set_transform(0, 0, 0, FX_ONE, FX_ONE / 2, 0);
    show_frame();
    CHECK(transform_is(0, 0, 0, FX_ONE, FX_ONE / 2, 0));
    // The largest values the registers hold.
    surface_set_transform(FX(524288) - 1, FX(-524288), FX(128) - 1, FX(-128), 0, 0);
    show_frame();
    CHECK(transform_is(FX(524288) - 1, FX(-524288), FX(128) - 1, FX(-128), 0, 0));
    CHECK(debug_warning_count() == before);
    // Past them: clamped, with a warning (once).
    for (u32 call = 0; call < 2; call++) {
        surface_set_transform(FX(524288), FX(-524289), FX(128), FX(-129), FX(1000), FX(-1000));
        show_frame();
        CHECK(transform_is(FX(524288) - 1, FX(-524288), FX(128) - 1, FX(-128), FX(128) - 1,
                           FX(-128)));
        CHECK(debug_warning_count() == before + WARNINGS_ON);
    }
    // A new surface starts with the identity.
    surface_end();
    CHECK(surface_begin());
    CHECK(transform_is(0, 0, FX_ONE, 0, 0, FX_ONE));
    surface_end();
}

// --- Colors -----------------------------------------------------------------------

// surface_set_colors() writes any of the 256 background colors at the next
// frame_end(); color 0 is the backdrop.
static void colors_apply_in_vblank(void) {
    static const Color some[3] = {0x1111, 0x2222, 0x3333};
    static const Color last[2] = {0x4444, 0x5555};
    static const Color backdrop[1] = {0x0123};
    u32 before = debug_warning_count();
    CHECK(surface_begin());
    show_frame();
    Color old = pal_bg_mem[100];
    surface_set_colors(100, some, 3);
    surface_set_colors(254, last, 2); // the text layer's bank is the surface's too
    surface_set_colors(0, backdrop, 1);
    surface_set_colors(50, some, 0); // nothing
    CHECK(pal_bg_mem[100] == old);   // not yet
    show_frame();
    CHECK(pal_bg_mem[100] == 0x1111 && pal_bg_mem[101] == 0x2222 && pal_bg_mem[102] == 0x3333);
    CHECK(pal_bg_mem[254] == 0x4444 && pal_bg_mem[255] == 0x5555);
    CHECK(pal_bg_mem[0] == 0x0123);
    CHECK(debug_warning_count() == before);
    // Refused: past color 255, a NULL table, and while the surface is off.
    for (u32 call = 0; call < 2; call++) {
        surface_set_colors(255, some, 2);
        surface_set_colors(256, some, 1);
        surface_set_colors(0, some, 257);
        surface_set_colors(10, NULL, 1);
        CHECK(debug_warning_count() == before + 2 * WARNINGS_ON); // one per kind, once
    }
    show_frame();
    CHECK(pal_bg_mem[255] == 0x5555 && pal_bg_mem[10] != 0x1111);
    surface_end();
    for (u32 call = 0; call < 2; call++) {
        surface_set_colors(100, last, 2);
        CHECK(debug_warning_count() == before + 3 * WARNINGS_ON);
    }
    show_frame();
    CHECK(pal_bg_mem[100] == 0x1111);
    screen_set_backdrop(0);
}

// --- Sprites ----------------------------------------------------------------------

// Bitmap modes show no sprite tile below 512: groups loaded while the
// surface is on start there, and both switches unload every group.
static void sprite_groups_load_from_tile_512(void) {
    u32 before = debug_warning_count();
    sprite_table_set(table, 1);
    CHECK(sprite_group_load(&group));
    CHECK(serval_sprite_frames(0).kind == SERVAL_SPRITE_ORDINARY);
    CHECK(serval_sprite_frames(0).first_tile == 0);

    CHECK(surface_begin());
    CHECK(serval_sprite_frames(0).kind == SERVAL_SPRITE_ABSENT); // unloaded
    CHECK(sprite_group_load(&group));
    CHECK(serval_sprite_frames(0).first_tile == 512);
    CHECK(((const u32*)MEM_VRAM_OBJ)[512 * 8] == 0x11111111);
    // A mark's release, and a reset, go back to 512, not to 0.
    sprite_groups_reset();
    u32 mark = sprite_groups_mark();
    CHECK(sprite_group_load(&group));
    sprite_groups_release(mark);
    CHECK(serval_sprite_frames(0).kind == SERVAL_SPRITE_ABSENT);
    CHECK(sprite_group_load(&group));
    CHECK(serval_sprite_frames(0).first_tile == 512);
    sprite_groups_reset();
    CHECK(sprite_group_load(&group));
    CHECK(serval_sprite_frames(0).first_tile == 512);
    // And it is drawn from there.
    frame_begin();
    sprite_draw(0, 0, 20, 30, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 512);
    CHECK(debug_warning_count() == before);

    surface_end();
    CHECK(serval_sprite_frames(0).kind == SERVAL_SPRITE_ABSENT);
    CHECK(sprite_group_load(&group));
    CHECK(serval_sprite_frames(0).first_tile == 0);
    sprite_groups_reset();
    show_frame();
}

// --- What has no place beside it -------------------------------------------------

// The text layer goes off with surface_begin(), prints nothing while the
// surface is on (its map and font would be written into the pages), and
// sets itself up again afterwards.
static void text_is_off_while_the_surface_is_on(void) {
    u32 before = debug_warning_count();
    text_print(0, 0, "A");
    CHECK(REG_DISPCNT & DCNT_BG0);
    CHECK(surface_begin());
    CHECK(!(REG_DISPCNT & DCNT_BG0));
    for (u32 call = 0; call < 2; call++) {
        text_print(1, 1, "B");
        text_print_line(1, 2, "C");
        text_print_centered(3, "D");
        text_print_centered_in(2, 10, 4, "E");
        text_clear();
        text_clear_area(0, 0, 4, 4);
        text_set_style(TEXT_HIGHLIGHT);
        text_set_style_color(TEXT_HIGHLIGHT, 0x7FFF, 0);
        text_set_shadow(true);
        text_set_shadow(false);
        text_set_style(TEXT_NORMAL);
        CHECK(debug_warning_count() == before + WARNINGS_ON);
    }
    CHECK(page_is(front, 0) && page_is(back, 0)); // nothing was written
    show_frame();
    CHECK((REG_DISPCNT & DCNT_BGS) == DCNT_BG2);
    // Pixels where the font and the text map are, to see them replaced.
    surface_clear(9);
    surface_flip();
    show_frame();
    surface_clear(9);
    surface_end();
    CHECK(!(REG_DISPCNT & DCNT_BG0));
    text_print(0, 0, "A");
    CHECK(REG_DISPCNT & DCNT_BG0);
    CHECK(se_mem[31][0] == (SE_PALBANK(15) | ('A' - 32)));
    CHECK(se_mem[31][1] == 0); // the map was cleared
    // The font is back: the blank glyph and one with pixels.
    const u32* glyphs = (const u32*)&tile_mem[0][0];
    CHECK(glyphs[0] == 0 && glyphs[7] == 0);
    bool any = false;
    for (u32 r = 0; r < 8; r++)
        any = any || glyphs[('A' - 32) * 8 + r] != 0;
    CHECK(any);
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    text_clear();
}

// A loaded map layer keeps the surface off; while it is on, tilesets and
// layers are refused.
static void maps_and_the_surface_exclude_each_other(void) {
    u32 before = debug_warning_count();
    CHECK(tileset_load(&tileset));
    CHECK(map_load(&layer));
    show_frame();
    for (u32 call = 0; call < 2; call++) {
        CHECK(!surface_begin());
        CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE0);
        CHECK(REG_DISPCNT & DCNT_BG2);
        CHECK(debug_warning_count() == before + WARNINGS_ON);
    }
    // Unloaded in the same frame: the surface comes on, and stays on through
    // the frame_end() in which the map module turns the layer's bit off.
    map_unload(2);
    CHECK(surface_begin());
    CHECK((REG_DISPCNT & DCNT_BGS) == DCNT_BG2);
    show_frame();
    show_frame();
    CHECK((REG_DISPCNT & DCNT_BGS) == DCNT_BG2);
    for (u32 call = 0; call < 2; call++) {
        CHECK(!tileset_load(&tileset));
        tileset_set_tiles(0, tileset_tiles, 1);
        CHECK(!map_load(&layer));
        CHECK(debug_warning_count() == before + 2 * WARNINGS_ON);
    }
    show_frame();
    CHECK(page_is(front, 0) && page_is(back, 0)); // no tile, no map entry written
    CHECK((REG_DISPCNT & DCNT_BGS) == DCNT_BG2);
    surface_end();
    // Afterwards they load again.
    CHECK(tileset_load(&tileset));
    CHECK(map_load(&layer));
    show_frame();
    CHECK(REG_DISPCNT & DCNT_BG2);
    CHECK((REG_DISPCNT & DCNT_MODE_MASK) == DCNT_MODE0);
    map_unload(2);
    show_frame();
    CHECK(debug_warning_count() == before + 2 * WARNINGS_ON);
}

// The splash is drawn on the text layer: skipped while the surface is on.
static void splash_is_skipped_while_the_surface_is_on(void) {
    u32 before = debug_warning_count();
    CHECK(surface_begin());
    u32 frames = frame_count();
    serval_splash();
    CHECK(frame_count() == frames);
    CHECK(page_is(front, 0) && page_is(back, 0));
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    surface_end();
}

// --- Misuse -----------------------------------------------------------------------

// Every call but surface_begin() is ignored while the surface is off, and a
// second surface_begin() changes nothing; each warns once.
static void calls_while_off_are_ignored(void) {
    u32 before = debug_warning_count();
    show_frame();
    u16 display = REG_DISPCNT;
    for (u32 call = 0; call < 2; call++) {
        surface_end();
        CHECK(surface_pixels() == NULL);
        surface_flip();
        surface_clear(5);
        surface_set_transform(0, 0, FX(2), 0, 0, FX(2));
        CHECK(debug_warning_count() == before + 5 * WARNINGS_ON); // one each, once
        show_frame();
        CHECK(REG_DISPCNT == display);
    }
    CHECK(surface_begin());
    surface_clear(3);
    for (u32 call = 0; call < 2; call++) {
        CHECK(surface_begin()); // on already: nothing is cleared or reset
        CHECK(debug_warning_count() == before + 6 * WARNINGS_ON);
        CHECK(page_is(back, 3));
    }
    surface_end();
    show_frame();
    CHECK(REG_DISPCNT == display);
}

TEST_SUITE(gba_surface_tests, "gba_surface",
           {"begin_and_end_switch_the_display_mode", begin_and_end_switch_the_display_mode},
           {"other_display_bits_are_kept", other_display_bits_are_kept},
           {"flip_swaps_the_pages_in_vblank", flip_swaps_the_pages_in_vblank},
           {"clear_and_pixels_reach_the_hidden_page", clear_and_pixels_reach_the_hidden_page},
           {"transform_applies_in_vblank", transform_applies_in_vblank},
           {"colors_apply_in_vblank", colors_apply_in_vblank},
           {"sprite_groups_load_from_tile_512", sprite_groups_load_from_tile_512},
           {"text_is_off_while_the_surface_is_on", text_is_off_while_the_surface_is_on},
           {"maps_and_the_surface_exclude_each_other", maps_and_the_surface_exclude_each_other},
           {"splash_is_skipped_while_the_surface_is_on", splash_is_skipped_while_the_surface_is_on},
           {"calls_while_off_are_ignored", calls_while_off_are_ignored});
