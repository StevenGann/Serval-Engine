// Tests for sprite groups and sprite_draw (src/gba/sprites.c), checked against
// what ends up in VRAM, palette RAM and OAM.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/sprites.h"

#include <tonc.h>

enum { SPR_SMALL, SPR_ANIM, SPR_WIDE, SPR_UNLOADED, SPRITE_COUNT };

static const u32 small_tiles[8] = {0x11111111, 0x22222222, 0x33333333, 0x44444444,
                                   0x55555555, 0x66666666, 0x77777777, 0x88888888};
static const u32 anim_tiles[16] = {[0] = 0xAAAAAAAA, [8] = 0xBBBBBBBB}; // 2 frames
static const u32 wide_tiles[16] = {[0] = 0xCCCCCCCC};

static const SpriteAsset small = {.size = SPRITE_8x8, .tiles = small_tiles};
static const SpriteAsset anim = {.size = SPRITE_8x8,
                                 .frame_count = 2,
                                 .tiles = anim_tiles,
                                 .origin_x = 4,
                                 .origin_y = 2,
                                 .palette_slot = 1};
static const SpriteAsset wide = {.size = SPRITE_16x8, .tiles = wide_tiles}; // 2 tiles

static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_SMALL] = &small, [SPR_ANIM] = &anim, [SPR_WIDE] = &wide, [SPR_UNLOADED] = &small};

static const u16 palettes[32] = {[1] = 0x1111, [17] = 0x2222};

static const u16 first_ids[] = {SPR_SMALL, SPR_ANIM};
static const SpriteGroup first = {
    .sprite_ids = first_ids, .palettes = palettes, .sprite_count = 2, .palette_count = 2};

static const u16 second_ids[] = {SPR_WIDE};
static const SpriteGroup second = {
    .sprite_ids = second_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};

// 17 sprites of 64x64 (64 tiles each): 1088 tiles, more than the 1024 there are.
static const SpriteAsset big = {.size = SPRITE_64x64, .tiles = small_tiles};
#define BIG_COUNT 17
static const SpriteAsset* const big_table[BIG_COUNT] = {
    &big, &big, &big, &big, &big, &big, &big, &big, &big,
    &big, &big, &big, &big, &big, &big, &big, &big,
};
static const SpriteGroup big_group = {
    .palettes = palettes, .sprite_count = BIG_COUNT, .palette_count = 1};

static const TILE* const obj_tiles = (const TILE*)MEM_VRAM_OBJ;

// Loads `first`, draws one sprite and flushes it to OAM.
static void draw_one(u16 id, u8 frame, int x, int y, u16 flags) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    sprite_draw(id, frame, x, y, flags);
    frame_end();
}

static void load_copies_tiles_and_palettes(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));
    CHECK(obj_tiles[0].data[0] == 0x11111111 && obj_tiles[0].data[7] == 0x88888888);
    CHECK(obj_tiles[1].data[0] == 0xAAAAAAAA); // anim frame 0
    CHECK(obj_tiles[2].data[0] == 0xBBBBBBBB); // anim frame 1
    CHECK(pal_obj_bank[0][1] == 0x1111 && pal_obj_bank[1][1] == 0x2222);
}

static void draw_writes_position_shape_and_tile(void) {
    draw_one(SPR_SMALL, 0, 100, 50, 0);
    CHECK((oam_mem[0].attr0 & ATTR0_Y_MASK) == 50);
    CHECK((oam_mem[0].attr0 & ATTR0_HIDE) == 0);
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 100);
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 0);
    CHECK((oam_mem[0].attr2 & ATTR2_PALBANK_MASK) == ATTR2_PALBANK(0));
}

static void draw_applies_frame_origin_and_palette_slot(void) {
    draw_one(SPR_ANIM, 1, 100, 50, 0);
    CHECK((oam_mem[0].attr0 & ATTR0_Y_MASK) == 48); // origin_y 2
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 96); // origin_x 4
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 2); // base 1 + frame 1
    CHECK((oam_mem[0].attr2 & ATTR2_PALBANK_MASK) == ATTR2_PALBANK(1));
}

static void draw_applies_flags(void) {
    draw_one(SPR_SMALL, 0, 10, 10, SPRITE_FLIP_H | SPRITE_FLIP_V);
    CHECK(oam_mem[0].attr1 & ATTR1_HFLIP);
    CHECK(oam_mem[0].attr1 & ATTR1_VFLIP);
}

static void layers_map_to_hardware_priority(void) {
    // Default: priority 2, behind the HUD (BG0) and foreground (BG1).
    draw_one(SPR_SMALL, 0, 10, 10, 0);
    CHECK((oam_mem[0].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(2));
    draw_one(SPR_SMALL, 0, 10, 10, SPRITE_ABOVE_FOREGROUND);
    CHECK((oam_mem[0].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(1));
    draw_one(SPR_SMALL, 0, 10, 10, SPRITE_ABOVE_HUD | SPRITE_FLIP_H);
    CHECK((oam_mem[0].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(0));
    CHECK(oam_mem[0].attr1 & ATTR1_HFLIP);
    draw_one(SPR_SMALL, 0, 10, 10, SPRITE_BEHIND_PLAYFIELD);
    CHECK((oam_mem[0].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(3));
}

static void partly_offscreen_wraps_coordinates(void) {
    draw_one(SPR_SMALL, 0, -4, -3, 0);
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == (512 - 4));
    CHECK((oam_mem[0].attr0 & ATTR0_Y_MASK) == (256 - 3));
    CHECK((oam_mem[0].attr0 & ~ATTR0_Y_MASK) == 0); // shape/mode bits intact
    CHECK((oam_mem[0].attr1 & ~ATTR1_X_MASK) == 0);
}

static void fully_offscreen_is_not_drawn(void) {
    draw_one(SPR_SMALL, 0, -8, 50, 0);
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
    draw_one(SPR_SMALL, 0, 240, 50, 0);
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
}

static void unloaded_sprite_or_bad_frame_is_not_drawn(void) {
    draw_one(SPR_UNLOADED, 0, 10, 10, 0);
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
    draw_one(SPR_SMALL, 1, 10, 10, 0); // only frame 0 exists
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
    draw_one(SPRITE_COUNT, 0, 10, 10, 0); // ID out of range
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
}

static void groups_are_allocated_one_after_another(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));
    CHECK(sprite_group_load(&second));
    frame_begin();
    sprite_draw(SPR_WIDE, 0, 0, 0, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 3); // after first's 3 tiles
    CHECK((oam_mem[0].attr2 & ATTR2_PALBANK_MASK) == ATTR2_PALBANK(2));
    CHECK((oam_mem[0].attr0 & ATTR0_SHAPE_MASK) == ATTR0_WIDE);
    CHECK(obj_tiles[3].data[0] == 0xCCCCCCCC);
}

static void load_fails_when_out_of_vram_or_palettes(void) {
    sprite_table_set(big_table, BIG_COUNT);
    CHECK(!sprite_group_load(&big_group));

    sprite_table_set(table, SPRITE_COUNT);
    SpriteGroup many_palettes = first;
    many_palettes.palette_count = 17;
    CHECK(!sprite_group_load(&many_palettes));

    CHECK(sprite_group_load(&first)); // nothing was consumed by the failures
    frame_begin();
    sprite_draw(SPR_SMALL, 0, 0, 0, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 0);
}

static void load_rejects_unsupported_groups(void) {
    sprite_table_set(table, SPRITE_COUNT);
    SpriteGroup streamed = first;
    streamed.flags = SPRITE_GROUP_STREAMED;
    CHECK(!sprite_group_load(&streamed));

    static const SpriteAsset no_size = {.tiles = small_tiles};
    static const SpriteAsset* const no_size_table[] = {&no_size};
    static const SpriteGroup no_size_group = {
        .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    sprite_table_set(no_size_table, 1);
    CHECK(!sprite_group_load(&no_size_group));
    sprite_table_set(table, SPRITE_COUNT);

    static const u16 bad_ids[] = {SPRITE_COUNT};
    SpriteGroup bad_id = first;
    bad_id.sprite_count = 1;
    bad_id.sprite_ids = bad_ids;
    CHECK(!sprite_group_load(&bad_id));
}

static void reset_unloads_everything(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    sprite_groups_reset();
    frame_begin();
    sprite_draw(SPR_SMALL, 0, 10, 10, 0);
    frame_end();
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
}

static void misuse_is_reported_once_in_debug_builds(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    u32 before = debug_warning_count();
    frame_begin();
    sprite_draw(SPR_UNLOADED, 0, 10, 10, 0);
    sprite_draw(SPR_UNLOADED, 0, 10, 10, 0); // same problem: not reported again
    frame_end();
    u32 after_draws = debug_warning_count();

    sprite_table_set(big_table, BIG_COUNT);
    CHECK(!sprite_group_load(&big_group));
    sprite_table_set(table, SPRITE_COUNT);
#ifdef SERVAL_DEBUG
    CHECK(after_draws == before + 1);
    CHECK(debug_warning_count() == after_draws + 1);
#else
    CHECK(debug_warning_count() == 0); // release builds report nothing
    (void)before;
    (void)after_draws;
#endif
}

static void defaults_and_computed_tile_counts(void) {
    // `wide` gives no frame_count or tiles_per_frame: one frame of 16x8 = 2
    // tiles. A group without sprite_ids holds IDs 0 to sprite_count - 1.
    static const SpriteAsset* const wide_first[] = {&wide, &small};
    static const SpriteGroup all = {.palettes = palettes, .sprite_count = 2, .palette_count = 1};
    sprite_table_set(wide_first, 2);
    CHECK(sprite_group_load(&all));
    frame_begin();
    sprite_draw(1, 0, 0, 0, 0); // `small` comes after wide's 2 tiles
    sprite_draw(0, 1, 0, 0, 0); // frame 1 doesn't exist
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 2);
    CHECK(oam_mem[1].attr0 & ATTR0_HIDE);
}

TEST_SUITE(sprite_tests, "sprites",
           {"load_copies_tiles_and_palettes", load_copies_tiles_and_palettes},
           {"draw_writes_position_shape_and_tile", draw_writes_position_shape_and_tile},
           {"draw_applies_frame_origin_and_palette_slot",
            draw_applies_frame_origin_and_palette_slot},
           {"draw_applies_flags", draw_applies_flags},
           {"layers_map_to_hardware_priority", layers_map_to_hardware_priority},
           {"partly_offscreen_wraps_coordinates", partly_offscreen_wraps_coordinates},
           {"fully_offscreen_is_not_drawn", fully_offscreen_is_not_drawn},
           {"unloaded_sprite_or_bad_frame_is_not_drawn", unloaded_sprite_or_bad_frame_is_not_drawn},
           {"groups_are_allocated_one_after_another", groups_are_allocated_one_after_another},
           {"load_fails_when_out_of_vram_or_palettes", load_fails_when_out_of_vram_or_palettes},
           {"load_rejects_unsupported_groups", load_rejects_unsupported_groups},
           {"reset_unloads_everything", reset_unloads_everything},
           {"misuse_is_reported_once_in_debug_builds", misuse_is_reported_once_in_debug_builds},
           {"defaults_and_computed_tile_counts", defaults_and_computed_tile_counts});
