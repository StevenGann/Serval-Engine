// Tests for palette writes on the GBA (src/gba/palette.c): sprite_set_colors()
// and tileset_set_colors() reach palette RAM at the next frame_end(), not
// before, and give way to what loads write at once (docs/sprites.md#palettes,
// docs/tilemaps.md#palette-writes).

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/map.h"
#include "serval/screen.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS_ON 1u
#else
#define WARNINGS_ON 0u
#endif

// The warning count after `before` once the first of two rounds of misuse
// (call 0) has reported `kinds` problems: the second round reports none.
static u32 warned(u32 before, u32 call, u32 kinds) {
    return before + (call == 0 ? kinds * WARNINGS_ON : 0);
}

enum { SPR_TWO, SPR_ONE, SPR_META, SPR_UNLOADED, SPRITE_COUNT };

static const u32 tiles[8] = {0x11111111};
static const SpriteAsset two = {.size = SPRITE_8x8, .tiles = tiles, .palette_slot = 1};
static const SpriteAsset one = {.size = SPRITE_8x8, .tiles = tiles};
static const SpritePiece pieces[] = {{.sprite = SPR_TWO}};
static const SpriteAsset meta = {
    .flags = SPRITE_ASSET_METASPRITE, .pieces = pieces, .piece_count = 1};
static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_TWO] = &two, [SPR_ONE] = &one, [SPR_META] = &meta, [SPR_UNLOADED] = &one};

// Two groups: SPR_TWO's two palettes (banks 0 and 1), then SPR_ONE's one
// (bank 2), and the metasprite's group, which has none.
static const u16 two_palettes[32] = {
    [1] = 0x0101, [2] = 0x0102, [15] = 0x010F, [17] = 0x0201, [18] = 0x0202};
static const u16 one_palette[16] = {[1] = 0x0301, [2] = 0x0302};
static const u16 two_ids[] = {SPR_TWO};
static const u16 one_ids[] = {SPR_ONE};
static const u16 meta_ids[] = {SPR_META};
static const SpriteGroup two_group = {
    .sprite_ids = two_ids, .palettes = two_palettes, .sprite_count = 1, .palette_count = 2};
static const SpriteGroup one_group = {
    .sprite_ids = one_ids, .palettes = one_palette, .sprite_count = 1, .palette_count = 1};
static const SpriteGroup meta_group = {.sprite_ids = meta_ids, .sprite_count = 1};

static void load_groups(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&two_group));
    CHECK(sprite_group_load(&one_group));
    CHECK(sprite_group_load(&meta_group));
    // Earlier tests may have left writes in sprite banks 3 and up; none here.
    frame_begin();
    frame_end();
}

static void frame(void) {
    frame_begin();
    frame_end();
}

// The colors reach palette RAM at frame_end(), copied at the call; one call
// can run across palettes, and touches only the sprite's group's banks.
static void sprite_colors_reach_palette_ram_at_frame_end(void) {
    load_groups();
    frame_begin();
    Color colors[3] = {0x7C00, 0x03E0, 0x001F};
    sprite_set_colors(SPR_TWO, 1, colors, 2);                    // palette 0, colors 1 and 2
    colors[0] = 0x1234;                                          // copied at the call: not seen
    CHECK(pal_obj_mem[1] == 0x0101 && pal_obj_mem[2] == 0x0102); // not before VBlank
    frame_end();
    CHECK(pal_obj_mem[1] == 0x7C00 && pal_obj_mem[2] == 0x03E0);
    CHECK(pal_obj_mem[15] == 0x010F && pal_obj_mem[17] == 0x0201); // the rest stays
    CHECK(pal_obj_mem[33] == 0x0301);                              // the other group's

    // From color 15 of palette 0 to color 1 of palette 1: color 0 of palette
    // 1 is kept (not shown: it is transparent).
    frame_begin();
    sprite_set_colors(SPR_TWO, 15, colors + 1, 2);
    sprite_set_colors(SPR_TWO, 17, colors, 1);
    frame_end();
    CHECK(pal_obj_mem[15] == 0x03E0 && pal_obj_mem[16] == 0x001F && pal_obj_mem[17] == 0x1234);
    CHECK(pal_obj_mem[18] == 0x0202);

    // The second group's palette 0 is bank 2.
    frame_begin();
    sprite_set_colors(SPR_ONE, 2, colors, 1);
    frame_end();
    CHECK(pal_obj_mem[34] == 0x1234 && pal_obj_mem[33] == 0x0301);
    CHECK(pal_obj_mem[2] == 0x03E0); // SPR_TWO's group keeps its own
}

// Written colors stay over frames without writes, and a later write to the
// same bank keeps them.
static void sprite_colors_stay_and_add_up(void) {
    load_groups();
    static const Color red = 0x001F, blue = 0x7C00;
    frame_begin();
    sprite_set_colors(SPR_TWO, 1, &red, 1);
    frame_end();
    frame();
    frame();
    CHECK(pal_obj_mem[1] == 0x001F);
    frame_begin();
    sprite_set_colors(SPR_TWO, 2, &blue, 1);
    frame_end();
    CHECK(pal_obj_mem[1] == 0x001F && pal_obj_mem[2] == 0x7C00 && pal_obj_mem[15] == 0x010F);
    // A load between frames is what the next write starts from.
    sprite_groups_reset();
    CHECK(sprite_group_load(&two_group));
    frame_begin();
    sprite_set_colors(SPR_TWO, 2, &red, 1);
    frame_end();
    CHECK(pal_obj_mem[1] == 0x0101 && pal_obj_mem[2] == 0x001F);
}

// A group loaded in the same frame, into banks written earlier in it, keeps
// its colors; writes after the load land.
static void loads_win_over_earlier_writes(void) {
    load_groups();
    static const Color red = 0x001F, blue = 0x7C00;
    frame_begin();
    sprite_set_colors(SPR_TWO, 1, &red, 1);  // bank 0
    sprite_set_colors(SPR_ONE, 1, &blue, 1); // bank 2
    sprite_groups_reset();
    CHECK(sprite_group_load(&one_group)); // into bank 0
    sprite_set_colors(SPR_ONE, 2, &blue, 1);
    frame_end();
    CHECK(pal_obj_mem[1] == 0x0301 && pal_obj_mem[2] == 0x7C00);
}

// Ignored, with one warning per kind in debug builds: a sprite not loaded or
// not in the table, a metasprite, NULL colors, colors past the group's.
static void sprite_set_colors_misuse(void) {
    load_groups();
    static const Color red = 0x001F;
    u16 before_colors[48];
    for (u32 k = 0; k < 48; k++)
        before_colors[k] = pal_obj_mem[k];
    for (u32 call = 0; call < 2; call++) {
        u32 before = debug_warning_count();
        frame_begin();
        sprite_set_colors(SPR_UNLOADED, 1, &red, 1);
        sprite_set_colors(999, 1, &red, 1); // the same problem: no second warning
        CHECK(debug_warning_count() == warned(before, call, 1));
        sprite_set_colors(SPR_META, 1, &red, 1);
        CHECK(debug_warning_count() == warned(before, call, 2));
        sprite_set_colors(SPR_TWO, 1, NULL, 1);
        CHECK(debug_warning_count() == warned(before, call, 3));
        sprite_set_colors(SPR_TWO, 31, (const Color[]){1, 2}, 2); // 33 colors of 32
        sprite_set_colors(SPR_ONE, 0, two_palettes, 17);
        sprite_set_colors(SPR_TWO, 0xFFFFFFFFu, &red, 2); // wraps past 2^32
        CHECK(debug_warning_count() == warned(before, call, 4));
        sprite_set_colors(SPR_UNLOADED, 1, NULL, 0); // a count of 0: nothing, silently
        sprite_set_colors(SPR_TWO, 1, &red, 0);
        CHECK(debug_warning_count() == warned(before, call, 4));
        frame_end();
    }
    bool same = true;
    for (u32 k = 0; k < 48; k++)
        same &= pal_obj_mem[k] == before_colors[k];
    CHECK(same);
    // The last color of the last palette is in range.
    frame_begin();
    sprite_set_colors(SPR_TWO, 31, &red, 1);
    frame_end();
    CHECK(pal_obj_mem[31] == 0x001F);
}

static const u32 bg_tiles[8] = {0};
static const u16 bg_palettes[32] = {
    [1] = 0x0401, [2] = 0x0402, [15] = 0x040F, [17] = 0x0501, [18] = 0x0502};
static const Tileset tileset = {
    .tiles = bg_tiles, .tile_count = 1, .palettes = bg_palettes, .palette_count = 2};

// Background colors reach palette RAM at frame_end(); color 0 of palette 0
// is the backdrop.
static void tileset_colors_reach_palette_ram_at_frame_end(void) {
    CHECK(tileset_load(&tileset));
    Color old_backdrop = pal_bg_mem[0];
    frame();
    frame_begin();
    Color colors[3] = {0x7C00, 0x03E0, 0x001F};
    tileset_set_colors(15, colors, 3); // palette 0's color 15, palette 1's 0 and 1
    tileset_set_colors(0, colors + 2, 1);
    colors[0] = 0;
    CHECK(pal_bg_mem[15] == 0x040F && pal_bg_mem[17] == 0x0501 && pal_bg_mem[0] == old_backdrop);
    frame_end();
    CHECK(pal_bg_mem[15] == 0x7C00 && pal_bg_mem[16] == 0x03E0 && pal_bg_mem[17] == 0x001F);
    CHECK(pal_bg_mem[0] == 0x001F); // the backdrop
    CHECK(pal_bg_mem[1] == 0x0401 && pal_bg_mem[18] == 0x0502);
    // Palettes the tileset doesn't have can be written too, up to color 239.
    frame_begin();
    Color old_239 = pal_bg_mem[239];
    tileset_set_colors(239, colors + 1, 1);
    frame_end();
    CHECK(pal_bg_mem[239] == 0x03E0);
    pal_bg_mem[239] = old_239;
    screen_set_backdrop(old_backdrop);
}

// tileset_load() puts colors 1-15 of its palettes back, over writes made
// before it in the frame; color 0 keeps them (the backdrop too), and writes
// after it land.
static void tileset_load_puts_colors_back(void) {
    CHECK(tileset_load(&tileset));
    Color old_backdrop = pal_bg_mem[0];
    frame();
    static const Color red = 0x001F, blue = 0x7C00;
    frame_begin();
    tileset_set_colors(0, &red, 1); // the backdrop
    tileset_set_colors(1, &red, 1);
    tileset_set_colors(16, &red, 1); // palette 1's color 0
    tileset_set_colors(17, &red, 1);
    tileset_set_colors(33, &red, 1); // palette 2: not the tileset's
    CHECK(tileset_load(&tileset));
    tileset_set_colors(18, &blue, 1);
    frame_end();
    CHECK(pal_bg_mem[1] == 0x0401 && pal_bg_mem[17] == 0x0501); // put back
    CHECK(pal_bg_mem[0] == 0x001F && pal_bg_mem[16] == 0x001F); // kept
    CHECK(pal_bg_mem[33] == 0x001F && pal_bg_mem[18] == 0x7C00);
    // Between frames too: loading after the write's frame puts colors back.
    CHECK(tileset_load(&tileset));
    CHECK(pal_bg_mem[18] == 0x0502 && pal_bg_mem[16] == 0x001F);
    screen_set_backdrop(old_backdrop);
}

// screen_set_backdrop() and tileset_set_colors() set the same color: the
// later call wins.
static void the_later_backdrop_wins(void) {
    Color old_backdrop = pal_bg_mem[0];
    static const Color red = 0x001F, blue = 0x7C00;
    frame_begin();
    tileset_set_colors(0, &red, 1);
    screen_set_backdrop(blue);
    frame_end();
    CHECK(pal_bg_mem[0] == 0x7C00);
    frame_begin();
    screen_set_backdrop(red);
    tileset_set_colors(0, &blue, 1);
    CHECK(pal_bg_mem[0] == 0x001F); // at once, then the write at VBlank
    frame_end();
    CHECK(pal_bg_mem[0] == 0x7C00);
    screen_set_backdrop(old_backdrop);
}

// Ignored, with one warning per kind in debug builds: colors past 239 (the
// text layer's palette 15) and NULL colors. A count of 0 does nothing.
static void tileset_set_colors_misuse(void) {
    static const Color red = 0x001F;
    u16 before_colors[256];
    for (u32 k = 0; k < 256; k++)
        before_colors[k] = pal_bg_mem[k];
    for (u32 call = 0; call < 2; call++) {
        u32 before = debug_warning_count();
        frame_begin();
        tileset_set_colors(239, (const Color[]){1, 2}, 2);
        tileset_set_colors(240, &red, 1);
        tileset_set_colors(0, bg_palettes, 241);
        tileset_set_colors(0xFFFFFFFFu, &red, 2); // wraps past 2^32
        CHECK(debug_warning_count() == warned(before, call, 1));
        tileset_set_colors(1, NULL, 1);
        CHECK(debug_warning_count() == warned(before, call, 2));
        tileset_set_colors(300, NULL, 0);
        CHECK(debug_warning_count() == warned(before, call, 2));
        frame_end();
    }
    bool same = true;
    for (u32 k = 0; k < 256; k++)
        same &= pal_bg_mem[k] == before_colors[k];
    CHECK(same);
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// The costs, logged: a whole palette (the 15 background banks a tileset can
// have and the 16 sprite banks) written in 16-color calls, then copied to
// palette RAM in VBlank, and the copy of one bank. The colors written are
// the ones there, so nothing changes on screen.
static const u16 full_palettes[256] = {[1] = 0x1111, [255] = 0x7FFF};
static const SpriteGroup full_group = {
    .sprite_ids = one_ids, .palettes = full_palettes, .sprite_count = 1, .palette_count = 16};

static void costs(void) {
    CHECK(tileset_load(&tileset));
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&full_group)); // all 16 sprite banks
    static SERVAL_EWRAM_BSS Color bg[240]; // IWRAM is scarce
    for (u32 k = 0; k < 240; k++)
        bg[k] = pal_bg_mem[k];
    frame();
    u32 t0 = cycles();
    for (u32 bank = 0; bank < 15; bank++)
        tileset_set_colors(bank * 16, bg + bank * 16, 16);
    for (u32 bank = 0; bank < 16; bank++)
        sprite_set_colors(SPR_ONE, bank * 16, full_palettes + bank * 16, 16);
    u32 t1 = cycles();
    VBlankIntrWait();
    u32 t2 = cycles();
    serval_palette_hooks->flush();
    u32 t3 = cycles();
    tileset_set_colors(16, bg + 16, 16);
    VBlankIntrWait();
    u32 t4 = cycles();
    serval_palette_hooks->flush();
    u32 t5 = cycles();
    debug_log(text_format("palette writes: 31 calls of 16 colors in %u cycles; the 31 banks "
                          "copied in VBlank in %u, one bank in %u",
                          t1 - t0, t3 - t2, t5 - t4));
#ifdef __OPTIMIZE__ // Debug builds (-O0) check correctness, not timing
    CHECK(t3 - t2 < 31 * 180 && t5 - t4 < 400);
#endif
    bool same = true;
    for (u32 k = 0; k < 240; k++)
        same &= pal_bg_mem[k] == bg[k];
    for (u32 k = 0; k < 256; k++)
        same &= pal_obj_mem[k] == full_palettes[k];
    CHECK(same);
    sprite_groups_reset();
}

TEST_SUITE(gba_palette_tests, "gba_palettes",
           {"sprite_colors_reach_palette_ram_at_frame_end",
            sprite_colors_reach_palette_ram_at_frame_end},
           {"sprite_colors_stay_and_add_up", sprite_colors_stay_and_add_up},
           {"loads_win_over_earlier_writes", loads_win_over_earlier_writes},
           {"sprite_set_colors_misuse", sprite_set_colors_misuse},
           {"tileset_colors_reach_palette_ram_at_frame_end",
            tileset_colors_reach_palette_ram_at_frame_end},
           {"tileset_load_puts_colors_back", tileset_load_puts_colors_back},
           {"the_later_backdrop_wins", the_later_backdrop_wins},
           {"tileset_set_colors_misuse", tileset_set_colors_misuse}, {"costs", costs});
