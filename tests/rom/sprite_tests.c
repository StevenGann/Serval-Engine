// Tests for sprite groups and sprite_draw (src/gba/sprites.c), checked against
// what ends up in VRAM, palette RAM and OAM.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/map.h"
#include "serval/math.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

enum { SPR_SMALL, SPR_ANIM, SPR_WIDE, SPR_UNLOADED, SPR_META, SPRITE_COUNT };

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

// A metasprite of two pieces from `first`, in two frames.
static const SpritePiece meta_pieces[] = {
    {.x = -10, .y = 0, .sprite = SPR_SMALL},                                   // frame 0
    {.x = 10, .y = 4, .sprite = SPR_ANIM, .frame = 1, .flags = SPRITE_FLIP_H}, //
    {.x = 0, .y = 0, .sprite = SPR_SMALL},                                     // frame 1
    {.x = 0, .y = -20, .sprite = SPR_ANIM, .flags = SPRITE_PALETTE(0)},        //
};
static const SpriteAsset meta = {
    .flags = SPRITE_ASSET_METASPRITE, .pieces = meta_pieces, .piece_count = 2, .frame_count = 2};

static const SpriteAsset* const table[SPRITE_COUNT] = {[SPR_SMALL] = &small,
                                                       [SPR_ANIM] = &anim,
                                                       [SPR_WIDE] = &wide,
                                                       [SPR_UNLOADED] = &small,
                                                       [SPR_META] = &meta};

static const u16 palettes[32] = {[1] = 0x1111, [17] = 0x2222};

static const u16 first_ids[] = {SPR_SMALL, SPR_ANIM};
static const SpriteGroup first = {
    .sprite_ids = first_ids, .palettes = palettes, .sprite_count = 2, .palette_count = 2};

static const u16 second_ids[] = {SPR_WIDE};
static const SpriteGroup second = {
    .sprite_ids = second_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};

static const u16 meta_ids[] = {SPR_META};
static const SpriteGroup meta_group = {.sprite_ids = meta_ids, .sprite_count = 1};

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

// (Streamed groups are tested in tests/rom/sprite_stream_tests.c; groups
// needing a planned feature, SPRITE_ASSET_LZ77, in
// tests/planned_sprites_tests.c.)
static void load_rejects_unsupported_groups(void) {
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

// Values this version doesn't know are refused, each with a warning, so a
// later version can give them a meaning (docs/releases.md): reserved bits of
// SpriteAsset.flags and SpriteGroup.flags, slots in a resident group, and
// piece flags other than flips, SPRITE_PALETTE and SPRITE_BLEND.
static void load_refuses_reserved_values(void) {
    static const SpriteAsset reserved_assets[] = {
        {.size = SPRITE_8x8, .tiles = small_tiles, .flags = 1 << 0}, // was STREAMED
        {.size = SPRITE_8x8, .tiles = small_tiles, .flags = 1 << 4},
        {.size = SPRITE_8x8, .tiles = small_tiles, .flags = 1 << 7},
        {.size = SPRITE_8x8, .tiles = small_tiles, .flags = SPRITE_ASSET_ANIM_ONCE | 1 << 5},
    };
    static const SpritePiece bad_pieces[][1] = {
        {{.sprite = SPR_SMALL, .flags = SPRITE_ABOVE_HUD}},
        {{.sprite = SPR_SMALL, .flags = SPRITE_HIDDEN}},
        {{.sprite = SPR_SMALL, .flags = SPRITE_SCALED}},
        {{.sprite = SPR_SMALL, .flags = 1 << 13}}, // reserved: mosaic
        {{.sprite = SPR_SMALL, .flags = 1 << 14}}, // reserved: the object window
        {{.sprite = SPR_SMALL, .flags = SPRITE_SCREEN}},
    };
    enum { ASSETS = 4, PIECES = 6 };
    static SpriteAsset bad_metas[PIECES];
    static const SpriteAsset* reserved_table[SPRITE_COUNT + ASSETS + PIECES];
    for (u32 k = 0; k < SPRITE_COUNT; k++)
        reserved_table[k] = table[k];
    for (u32 k = 0; k < ASSETS; k++)
        reserved_table[SPRITE_COUNT + k] = &reserved_assets[k];
    for (u32 k = 0; k < PIECES; k++) {
        bad_metas[k] = (SpriteAsset){
            .flags = SPRITE_ASSET_METASPRITE, .pieces = bad_pieces[k], .piece_count = 1};
        reserved_table[SPRITE_COUNT + ASSETS + k] = &bad_metas[k];
    }
    sprite_table_set(reserved_table, SPRITE_COUNT + ASSETS + PIECES);
    u32 before = debug_warning_count();
    for (u32 k = 0; k < ASSETS + PIECES; k++) {
        u16 id = (u16)(SPRITE_COUNT + k);
        SpriteGroup g = {
            .sprite_ids = &id, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
        CHECK(!sprite_group_load(&g));
    }
    for (u32 bit = 1; bit < 8; bit++) {
        SpriteGroup g = first;
        g.flags = (u8)(1u << bit);
        CHECK(!sprite_group_load(&g));
    }
    SpriteGroup slotted = first;
    slotted.slots = 1; // only streamed groups have slots
    CHECK(!sprite_group_load(&slotted));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + ASSETS + PIECES + 7 + 1);
#else
    CHECK(debug_warning_count() == before);
#endif
    // What a piece may carry loads, and nothing was consumed before it.
    static const SpritePiece good_piece[] = {
        {.sprite = SPR_SMALL, .flags = SPRITE_FLIP_H | SPRITE_FLIP_V | SPRITE_PALETTE(14)}};
    static const SpriteAsset good_meta = {
        .flags = SPRITE_ASSET_METASPRITE, .pieces = good_piece, .piece_count = 1};
    static const SpriteAsset* const good_table[] = {&small, &good_meta};
    static const SpriteGroup good = {.palettes = palettes, .sprite_count = 2, .palette_count = 1};
    sprite_table_set(good_table, 2);
    CHECK(sprite_group_load(&good));
    frame_begin();
    sprite_draw(1, 0, 10, 10, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 0);
    CHECK((oam_mem[0].attr1 & (ATTR1_HFLIP | ATTR1_VFLIP)) == (ATTR1_HFLIP | ATTR1_VFLIP));
    sprite_table_set(table, SPRITE_COUNT);
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

static const OBJ_AFFINE* const matrices = (const OBJ_AFFINE*)oam_mem;
#define MATRIX_INDEX(attr1) (((attr1) >> 9) & 31)

static void rotation_uses_a_double_size_affine_sprite(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&second); // SPR_WIDE: 16x8
    frame_begin();
    sprite_draw_rotated(SPR_WIDE, 0, 100, 50, ANGLE_DEG(90), 0);
    frame_end();
    CHECK((oam_mem[0].attr0 & ATTR0_AFF_DBL) == ATTR0_AFF_DBL);
    // Double size: 32x16 box, shifted by half the size to stay centered.
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 100 - 8);
    CHECK((oam_mem[0].attr0 & ATTR0_Y_MASK) == 50 - 4);
    // Clockwise quarter turn: the inverse rotation maps screen to texture.
    const OBJ_AFFINE* m = &matrices[MATRIX_INDEX(oam_mem[0].attr1)];
    CHECK(m->pa == 0 && m->pb == 256 && m->pc == -256 && m->pd == 0);
}

static void sprites_share_matrices_by_angle_and_flips(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    sprite_draw_rotated(SPR_SMALL, 0, 10, 10, ANGLE_DEG(45), 0);
    sprite_draw_rotated(SPR_SMALL, 0, 30, 10, ANGLE_DEG(45), 0);             // same: shared
    sprite_draw_rotated(SPR_SMALL, 0, 50, 10, ANGLE_DEG(45), SPRITE_FLIP_H); // flipped: own
    sprite_draw_rotated(SPR_SMALL, 0, 70, 10, ANGLE_DEG(30), 0);             // other angle
    frame_end();
    u32 a = MATRIX_INDEX(oam_mem[0].attr1), b = MATRIX_INDEX(oam_mem[1].attr1);
    u32 c = MATRIX_INDEX(oam_mem[2].attr1), d = MATRIX_INDEX(oam_mem[3].attr1);
    CHECK(a == b && a != c && c != d && a != d);
    CHECK(matrices[c].pa == -matrices[a].pa && matrices[c].pb == -matrices[a].pb);
    CHECK(matrices[c].pc == matrices[a].pc && matrices[c].pd == matrices[a].pd);

    frame_begin(); // matrices start over each frame
    sprite_draw_rotated(SPR_SMALL, 0, 10, 10, ANGLE_DEG(30), 0);
    frame_end();
    CHECK(MATRIX_INDEX(oam_mem[0].attr1) == 0);
}

static void past_32_angles_sprites_are_drawn_unrotated(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    u32 warnings = debug_warning_count();
    frame_begin();
    for (u32 k = 0; k < 33; k++)
        sprite_draw_rotated(SPR_SMALL, 0, 10, 10, (u16)(1000 + k * 100), 0);
    frame_end();
    CHECK(oam_mem[31].attr0 & ATTR0_AFF);
    CHECK(!(oam_mem[32].attr0 & ATTR0_AFF) && !(oam_mem[32].attr0 & ATTR0_HIDE));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
}

static void offscreen_rotated_sprites_take_no_matrix(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    for (u32 k = 0; k < 40; k++) // 40 distinct angles, all off screen
        sprite_draw_rotated(SPR_SMALL, 0, -100, 10, (u16)(1000 + k * 100), 0);
    sprite_draw_rotated(SPR_SMALL, 0, 50, 50, ANGLE_DEG(90), 0);
    frame_end();
    CHECK(serval_matrices_used == 1);
    CHECK((oam_mem[0].attr0 & ATTR0_AFF_DBL) == ATTR0_AFF_DBL); // still rotated
    CHECK(oam_mem[1].attr0 & ATTR0_HIDE);
}

static void rotated_sprites_past_full_oam_take_no_matrix(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    for (u32 k = 0; k < 128; k++)
        sprite_draw(SPR_SMALL, 0, 10, 10, 0);
    sprite_draw_rotated(SPR_SMALL, 0, 50, 50, ANGLE_DEG(90), 0);
    CHECK(serval_matrices_used == 0);
    frame_end();
}

static void angle_zero_draws_like_sprite_draw(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    sprite_draw(SPR_ANIM, 1, 100, 50, SPRITE_FLIP_H | SPRITE_ABOVE_HUD);
    sprite_draw_rotated(SPR_ANIM, 1, 100, 50, 0, SPRITE_FLIP_H | SPRITE_ABOVE_HUD);
    CHECK(serval_matrices_used == 0);
    frame_end();
    CHECK(oam_mem[1].attr0 == oam_mem[0].attr0);
    CHECK(oam_mem[1].attr1 == oam_mem[0].attr1);
    CHECK(oam_mem[1].attr2 == oam_mem[0].attr2);
    CHECK(!(oam_mem[1].attr0 & ATTR0_AFF) && (oam_mem[1].attr1 & ATTR1_HFLIP));
}

static void ids_past_the_maximum_are_reported_apart_from_511(void) {
    static const SpriteAsset* const full_table[SPRITE_MAX] = {[SPRITE_MAX - 1] = &small};
    sprite_table_set(full_table, SPRITE_MAX);
    u32 before = debug_warning_count();
    frame_begin();
    sprite_draw(SPRITE_MAX - 1, 0, 10, 10, 0);  // not loaded
    sprite_draw(SPRITE_MAX + 50, 0, 10, 10, 0); // not in the table: a different problem
    sprite_draw(SPRITE_MAX + 60, 0, 10, 10, 0); // shares the out-of-range report
    frame_end();
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 2);
#else
    CHECK(debug_warning_count() == before);
#endif
    sprite_table_set(table, SPRITE_COUNT);
}

static void load_rejects_incomplete_data(void) {
    static const SpriteAsset no_tiles = {.size = SPRITE_8x8};
    static const SpriteAsset too_few_tiles = {
        .size = SPRITE_16x8, .tiles = wide_tiles, .tiles_per_frame = 1}; // 16x8 needs 2
    static const SpriteAsset padded = {
        .size = SPRITE_16x8, .tiles = wide_tiles, .tiles_per_frame = 3, .frame_count = 1};
    static const SpriteAsset* const bad_table[] = {&no_tiles, &too_few_tiles, NULL, &padded};
    static const u16 ids[4][1] = {{0}, {1}, {2}, {3}};
    sprite_table_set(bad_table, 4);
    u32 before = debug_warning_count();
    for (u32 k = 0; k < 3; k++) {
        SpriteGroup g = {
            .sprite_ids = ids[k], .palettes = palettes, .sprite_count = 1, .palette_count = 1};
        CHECK(!sprite_group_load(&g));
    }
    SpriteGroup no_palettes = {.sprite_ids = ids[3], .sprite_count = 1, .palette_count = 1};
    CHECK(!sprite_group_load(&no_palettes));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 4);
#else
    CHECK(debug_warning_count() == before);
#endif
    SpriteGroup ok = {
        .sprite_ids = ids[3], .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    CHECK(sprite_group_load(&ok)); // more tiles than needed is fine
    sprite_table_set(table, SPRITE_COUNT);
}

static void sys_render_rotates_entities_with_an_angle(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    ecs_reset();
    u32 still = entity_index(entity_create(C_POS | C_SPR));
    u32 turned = entity_index(entity_create(C_POS | C_SPR));
    pos_x[still] = pos_x[turned] = FX(40);
    spr_angle[turned] = ANGLE_DEG(180);
    frame_begin();
    sys_render();
    frame_end();
    CHECK(!(oam_mem[0].attr0 & ATTR0_AFF));
    CHECK((oam_mem[1].attr0 & ATTR0_AFF_DBL) == ATTR0_AFF_DBL);
    CHECK(matrices[MATRIX_INDEX(oam_mem[1].attr1)].pa == -256);
    ecs_reset();
}

// --- Screen-space sprites and palette selection --------------------------------

// Draws the entities with sys_render or sys_render_by_depth.
static void render(bool by_depth) {
    frame_begin();
    if (by_depth)
        sys_render_by_depth();
    else
        sys_render();
    frame_end();
}

#define OAM_X(k) (oam_mem[k].attr1 & ATTR1_X_MASK)
#define OAM_Y(k) (oam_mem[k].attr0 & ATTR0_Y_MASK)
#define OAM_BANK(k) ((oam_mem[k].attr2 & ATTR2_PALBANK_MASK) >> ATTR2_PALBANK_SHIFT)

static void screen_space_entities_ignore_the_camera(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    ecs_reset();
    u32 world = entity_index(entity_create(C_POS | C_SPR));
    u32 screen = entity_index(entity_create(C_POS | C_SPR));
    u32 turned = entity_index(entity_create(C_POS | C_SPR));
    pos_x[world] = pos_x[screen] = pos_x[turned] = FX(300);
    pos_y[world] = pos_y[screen] = pos_y[turned] = FX(90);
    spr_flags[screen] = SPRITE_SCREEN | SPRITE_FLIP_H;
    spr_flags[turned] = SPRITE_SCREEN;
    spr_angle[turned] = ANGLE_DEG(90);
    camera_set(250, 40); // no playfield loaded: not clamped
    for (int by_depth = 0; by_depth < 2; by_depth++) {
        pos_x[screen] = pos_x[turned] = FX(100);
        render(by_depth);
        // In slot order (equal depths): world, screen, turned.
        CHECK(OAM_X(0) == 50 && OAM_Y(0) == 50);
        CHECK(OAM_X(1) == 100 && OAM_Y(1) == 90);
        CHECK(oam_mem[1].attr1 & ATTR1_HFLIP);
        CHECK((oam_mem[2].attr0 & ATTR0_AFF_DBL) == ATTR0_AFF_DBL);
        CHECK(OAM_X(2) == 100 - 4 && OAM_Y(2) == 90 - 4); // 8x8: 16x16 box
        // Off the screen at x 300, wherever the camera is.
        pos_x[screen] = pos_x[turned] = FX(300);
        render(by_depth);
        CHECK(OAM_X(0) == 50 && (oam_mem[1].attr0 & ATTR0_HIDE));
    }
    camera_set(0, 0);
    ecs_reset();
}

static void sprite_palette_selects_a_palette_of_the_group(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));  // banks 0-1: SPR_SMALL (slot 0), SPR_ANIM (slot 1)
    CHECK(sprite_group_load(&second)); // bank 2: SPR_WIDE
    u32 before = debug_warning_count();
    frame_begin();
    sprite_draw(SPR_SMALL, 0, 10, 10, SPRITE_PALETTE(1));
    sprite_draw(SPR_ANIM, 1, 10, 10, SPRITE_PALETTE(0) | SPRITE_ABOVE_HUD | SPRITE_FLIP_V);
    sprite_draw(SPR_ANIM, 1, 10, 10, 0);
    sprite_draw(SPR_WIDE, 0, 10, 10, SPRITE_PALETTE(0));
    sprite_draw(SPR_WIDE, 0, 10, 10, SPRITE_PALETTE(1)); // the group has one palette
    sprite_draw(SPR_WIDE, 0, 10, 10, SPRITE_PALETTE(14));
    sprite_draw_rotated(SPR_SMALL, 0, 50, 50, ANGLE_DEG(90), SPRITE_PALETTE(1));
    sprite_draw_rotated(SPR_ANIM, 0, 50, 50, 0, SPRITE_PALETTE(0));
    frame_end();
    CHECK(OAM_BANK(0) == 1);
    CHECK(OAM_BANK(1) == 0);
    // The tile (and frame) stay the sprite's own.
    CHECK((oam_mem[1].attr2 & ATTR2_ID_MASK) == (oam_mem[2].attr2 & ATTR2_ID_MASK));
    CHECK((oam_mem[1].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(0));
    CHECK((oam_mem[1].attr2 & ATTR2_ID_MASK) == 2 && (oam_mem[1].attr1 & ATTR1_VFLIP));
    CHECK(OAM_BANK(2) == 1);
    CHECK(OAM_BANK(3) == 2);
    CHECK(OAM_BANK(4) == 2 && OAM_BANK(5) == 2); // beyond the group: its own
    CHECK((oam_mem[6].attr0 & ATTR0_AFF_DBL) == ATTR0_AFF_DBL && OAM_BANK(6) == 1);
    CHECK(!(oam_mem[7].attr0 & ATTR0_AFF) && OAM_BANK(7) == 0);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 1); // reported once
#else
    CHECK(debug_warning_count() == before);
#endif
}

static void entities_draw_with_their_sprite_palette(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    sprite_group_load(&second);
    ecs_reset();
    static const struct {
        u16 id, flags, angle;
        u32 bank; // expected; 99: not drawn
    } cases[] = {
        {SPR_SMALL, 0, 0, 0},
        {SPR_SMALL, SPRITE_PALETTE(1), 0, 1},
        {SPR_ANIM, SPRITE_PALETTE(0) | SPRITE_SCREEN, 0, 0},
        {SPR_ANIM, SPRITE_PALETTE(1) | SPRITE_HIDDEN, 0, 99},
        {SPR_SMALL, SPRITE_PALETTE(1), ANGLE_DEG(45), 1},
        {SPR_WIDE, SPRITE_PALETTE(0) | SPRITE_ABOVE_FOREGROUND, 0, 2},
        {SPR_WIDE, SPRITE_PALETTE(3), 0, 2},
        {SPR_ANIM, 0, 0, 1},
    };
    const u32 n = sizeof(cases) / sizeof(cases[0]);
    for (u32 k = 0; k < n; k++) {
        u32 i = entity_index(entity_create(C_POS | C_SPR));
        pos_x[i] = FX(20 + 10 * (int)k);
        pos_y[i] = FX(30);
        spr_id[i] = cases[k].id;
        spr_frame[i] = cases[k].id == SPR_ANIM;
        spr_flags[i] = cases[k].flags;
        spr_angle[i] = cases[k].angle;
        spr_depth[i] = (s16)(100 - k); // by depth: the same order
    }
    for (int by_depth = 0; by_depth < 2; by_depth++) {
        render(by_depth);
        u32 slot = 0;
        bool ok = true;
        for (u32 k = 0; k < n; k++) {
            if (cases[k].bank == 99)
                continue;
            ok &= OAM_BANK(slot) == cases[k].bank;
            ok &= OAM_X(slot) ==
                  (u32)(20 + 10 * k - (cases[k].id == SPR_ANIM ? 4 : 0) - (cases[k].angle ? 4 : 0));
            slot++;
        }
        CHECK(ok);
        CHECK(oam_mem[slot].attr0 & ATTR0_HIDE); // nothing more drawn
        CHECK((oam_mem[4].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(1));
        CHECK((oam_mem[1].attr2 & ATTR2_ID_MASK) == 0 && (oam_mem[2].attr2 & ATTR2_ID_MASK) == 2);
    }
    ecs_reset();
}

// --- Scaling and sprite statistics ---------------------------------------------

#define AFFINE_MODE(k) (oam_mem[k].attr0 & ATTR0_AFF_DBL)

static void scaling_enlarges_in_a_double_size_box(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&second); // SPR_WIDE: 16x8
    frame_begin();
    sprite_draw_ex(SPR_WIDE, 0, 100, 50, 0, FX(2), FX(3) / 2, 0);
    frame_end();
    CHECK(AFFINE_MODE(0) == ATTR0_AFF_DBL);
    CHECK(OAM_X(0) == 100 - 8 && OAM_Y(0) == 50 - 4); // centered, as when rotated
    // The inverse: a pixel on screen is half (two thirds of) a texture pixel.
    const OBJ_AFFINE* m = &matrices[MATRIX_INDEX(oam_mem[0].attr1)];
    CHECK(m->pa == 128 && m->pb == 0 && m->pc == 0 && m->pd == 170);
}

static void shrinking_and_mirroring_use_the_sprites_own_box(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    sprite_draw_ex(SPR_ANIM, 1, 100, 50, 0, FX_ONE / 2, FX_ONE, SPRITE_FLIP_V);
    sprite_draw_ex(SPR_ANIM, 1, 100, 50, 0, -FX_ONE, FX_ONE, 0); // a mirror, as in a card flip
    sprite_draw_ex(SPR_ANIM, 1, 100, 50, ANGLE_DEG(10), FX_ONE / 2, FX_ONE / 2, 0); // rotated
    frame_end();
    CHECK(AFFINE_MODE(0) == ATTR0_AFF && AFFINE_MODE(1) == ATTR0_AFF);
    CHECK(AFFINE_MODE(2) == ATTR0_AFF_DBL);
    CHECK(OAM_X(0) == 100 - 4 && OAM_Y(0) == 50 - 2); // minus the origin, like sprite_draw
    CHECK(OAM_X(1) == 100 - 4 && OAM_Y(1) == 50 - 2);
    CHECK(OAM_X(2) == 100 - 4 - 4 && OAM_Y(2) == 50 - 2 - 4);
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 2); // frame 1 of SPR_ANIM
    const OBJ_AFFINE* a = &matrices[MATRIX_INDEX(oam_mem[0].attr1)];
    const OBJ_AFFINE* b = &matrices[MATRIX_INDEX(oam_mem[1].attr1)];
    CHECK(a->pa == 512 && a->pd == -256); // half as wide, flipped vertically
    CHECK(b->pa == -256 && b->pd == 256);
}

static void scales_share_matrices_and_zero_draws_nothing(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    sprite_draw_ex(SPR_SMALL, 0, 10, 10, ANGLE_DEG(45), FX_ONE / 2, FX_ONE / 2, 0);
    sprite_draw_ex(SPR_SMALL, 0, 30, 10, ANGLE_DEG(45), FX_ONE / 2, FX_ONE / 2, 0); // shared
    sprite_draw_ex(SPR_SMALL, 0, 50, 10, ANGLE_DEG(45), FX_ONE / 2, FX_ONE, 0);     // own
    sprite_draw_ex(SPR_SMALL, 0, 70, 10, ANGLE_DEG(45), FX_ONE, FX_ONE, 0);         // rotated only
    sprite_draw_rotated(SPR_SMALL, 0, 90, 10, ANGLE_DEG(45), 0);            // the same matrix
    sprite_draw_ex(SPR_SMALL, 0, 50, 50, 0, 0, FX_ONE, 0);                  // no width: nothing
    sprite_draw_ex(SPR_SMALL, 0, 60, 60, 0, FX_ONE, FX_ONE, SPRITE_FLIP_H); // plain
    CHECK(serval_oam_used == 6 && serval_matrices_used == 3);
    frame_end();
    CHECK(MATRIX_INDEX(oam_mem[0].attr1) == MATRIX_INDEX(oam_mem[1].attr1));
    CHECK(MATRIX_INDEX(oam_mem[3].attr1) == MATRIX_INDEX(oam_mem[4].attr1));
    CHECK(!(oam_mem[5].attr0 & ATTR0_AFF) && (oam_mem[5].attr1 & ATTR1_HFLIP));
}

static void entities_scale_with_sprite_scaled(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    ecs_reset();
    u32 scaled = entity_index(entity_create(C_POS | C_SPR));
    u32 forgot = entity_index(entity_create(C_POS | C_SPR));
    u32 gone = entity_index(entity_create(C_POS | C_SPR));
    pos_x[scaled] = pos_x[forgot] = pos_x[gone] = FX(40);
    spr_flags[scaled] = SPRITE_SCALED;
    spr_scale[scaled] = FX(2);
    spr_scale[forgot] = FX(2);       // no SPRITE_SCALED: normal size, reported in debug builds
    spr_flags[gone] = SPRITE_SCALED; // spr_scale 0: no size, not drawn
    u32 warnings = debug_warning_count();
    for (int by_depth = 0; by_depth < 2; by_depth++) {
        render(by_depth);
        // Equal depths: by depth draws them in the same order.
        CHECK(AFFINE_MODE(0) == ATTR0_AFF_DBL && !(oam_mem[1].attr0 & ATTR0_AFF));
        CHECK(oam_mem[2].attr0 & ATTR0_HIDE);
        CHECK(matrices[MATRIX_INDEX(oam_mem[0].attr1)].pa == 128);
    }
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    ecs_reset();
}

static void sprite_stats_count_the_last_frame(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    frame_begin();
    for (u32 k = 0; k < 33; k++)
        sprite_draw_rotated(SPR_SMALL, 0, 10, 10, (u16)(1000 + k * 100), 0);
    for (u32 k = 0; k < 100; k++)
        sprite_draw(SPR_SMALL, 0, 10, 10, 0);
    SpriteStats during = sprite_stats(); // still the frame before
    frame_end();
    SpriteStats s = sprite_stats();
    CHECK(s.drawn == 128 && s.matrices == 32 && s.dropped == 5 && s.untransformed == 1);
    CHECK(during.drawn != 128 || during.dropped != 5);

    frame_begin(); // counts start over each frame
    sprite_draw(SPR_SMALL, 0, 10, 10, 0);
    frame_end();
    s = sprite_stats();
    CHECK(s.drawn == 1 && s.matrices == 0 && s.dropped == 0 && s.untransformed == 0);
}

// sys_render_by_depth puts higher depths in front (earlier in OAM), equal
// depths in slot order, whichever way it sorts: depths already in order (no
// sort), within 256 of each other (one pass) or far apart (two passes).
static void depth_order_holds_for_every_kind_of_sort(void) {
    static const s16 cases[3][6] = {
        {9, 9, 5, 5, 2, -1},          // already in order
        {3, 7, 3, 250, 0, 7},         // a small range
        {-2000, 300, 7, 7, 1000, -5}, // far apart
    };
    static const u8 expected[3][6] = {
        {0, 1, 2, 3, 4, 5},
        {3, 1, 5, 0, 2, 4},
        {4, 1, 2, 3, 5, 0},
    };
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    for (u32 c = 0; c < 3; c++) {
        ecs_reset();
        for (u32 k = 0; k < 6; k++) {
            u32 i = entity_index(entity_create(C_POS | C_SPR));
            pos_x[i] = FX((int)(10 + k * 20)); // tells them apart in OAM
            spr_depth[i] = cases[c][k];
        }
        render(true);
        for (u32 k = 0; k < 6; k++)
            CHECK(OAM_X(k) == 10 + expected[c][k] * 20u);
    }
    ecs_reset();
}

// --- Metasprites ------------------------------------------------------------------

#define OAM_HFLIP(k) ((oam_mem[k].attr1 & ATTR1_HFLIP) != 0)
#define OAM_TILE(k) (oam_mem[k].attr2 & ATTR2_ID_MASK)

static void load_with_metasprite(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));      // SMALL (tile 0), ANIM (tiles 1-2)
    CHECK(sprite_group_load(&meta_group)); // no tiles, no palettes
}

static void metasprites_draw_their_pieces_around_the_pivot(void) {
    load_with_metasprite();
    frame_begin();
    sprite_draw(SPR_META, 0, 100, 50, 0);
    sprite_draw(SPR_META, 0, 100, 50, SPRITE_FLIP_H); // mirrored as a whole
    sprite_draw(SPR_META, 1, 100, 50, SPRITE_PALETTE(1));
    CHECK(serval_oam_used == 6 && serval_matrices_used == 0);
    frame_end();
    // Pieces centered on their offsets (8x8: top-left = center - 4), first
    // piece first (in front), with its own flips, frame and palette.
    CHECK(OAM_X(0) == 86 && OAM_Y(0) == 46 && !OAM_HFLIP(0) && OAM_TILE(0) == 0);
    CHECK(OAM_X(1) == 106 && OAM_Y(1) == 50 && OAM_HFLIP(1) && OAM_TILE(1) == 2);
    CHECK(OAM_BANK(1) == 1); // ANIM's own palette slot
    CHECK(OAM_X(2) == 106 && OAM_Y(2) == 46 && OAM_HFLIP(2));
    CHECK(OAM_X(3) == 86 && OAM_Y(3) == 50 && !OAM_HFLIP(3)); // its flip undone
    // Frame 1; the whole's palette wins over the piece's.
    CHECK(OAM_X(4) == 96 && OAM_Y(4) == 46 && OAM_BANK(4) == 1);
    CHECK(OAM_X(5) == 96 && OAM_Y(5) == 26 && OAM_TILE(5) == 1 && OAM_BANK(5) == 1);
}

static void metasprites_rotate_and_scale_about_the_pivot(void) {
    load_with_metasprite();
    frame_begin();
    // A quarter turn clockwise: (-10, 0) goes to (0, -10), (10, 4) to (-4, 10).
    sprite_draw_rotated(SPR_META, 0, 100, 50, ANGLE_DEG(90), 0);
    // Twice the size: (-10, 0) goes to (-20, 0).
    sprite_draw_ex(SPR_META, 0, 100, 50, 0, FX(2), FX(2), 0);
    frame_end();
    // Double-size boxes (16x16) around the turned centers.
    CHECK(AFFINE_MODE(0) == ATTR0_AFF_DBL && OAM_X(0) == 100 - 8 && OAM_Y(0) == 40 - 8);
    CHECK(AFFINE_MODE(1) == ATTR0_AFF_DBL && OAM_X(1) == 96 - 8 && OAM_Y(1) == 60 - 8);
    // The pieces turn with the whole: the flipped one has its own matrix.
    const OBJ_AFFINE* a = &matrices[MATRIX_INDEX(oam_mem[0].attr1)];
    const OBJ_AFFINE* b = &matrices[MATRIX_INDEX(oam_mem[1].attr1)];
    CHECK(a->pa == 0 && a->pb == 256 && b->pa == 0 && b->pb == -256);
    CHECK(OAM_X(2) == 80 - 8 && OAM_Y(2) == 50 - 8);
    CHECK(OAM_X(3) == 120 - 8 && OAM_Y(3) == 58 - 8);
    CHECK(matrices[MATRIX_INDEX(oam_mem[2].attr1)].pa == 128);
}

static void metasprite_entities_are_drawn_and_sorted_as_one(void) {
    load_with_metasprite();
    ecs_reset();
    u32 m = entity_index(entity_create(C_POS | C_SPR));
    u32 dot = entity_index(entity_create(C_POS | C_SPR));
    pos_x[m] = FX(100);
    pos_y[m] = FX(50);
    spr_id[m] = SPR_META;
    pos_x[dot] = FX(20);
    spr_depth[dot] = 1; // in front of both pieces
    render(true);
    CHECK(OAM_X(0) == 20 && OAM_X(1) == 86 && OAM_X(2) == 106);
    spr_angle[m] = ANGLE_DEG(90);
    spr_flags[m] = SPRITE_SCALED;
    spr_scale[m] = FX(2);
    render(false);                                    // slot order: the metasprite first
    CHECK(OAM_X(0) == 100 - 8 && OAM_Y(0) == 30 - 8); // (-10, 0) scaled and turned: (0, -20)
    CHECK(OAM_X(2) == 20);
    ecs_reset();
}

static void metasprites_need_valid_pieces(void) {
    static const SpritePiece of_meta[] = {{.sprite = SPR_META}};
    static const SpritePiece bad_frame[] = {{.sprite = SPR_SMALL, .frame = 1}};
    static const SpriteAsset bad[] = {
        {.flags = SPRITE_ASSET_METASPRITE, .pieces = of_meta, .piece_count = 1},
        {.flags = SPRITE_ASSET_METASPRITE, .pieces = bad_frame, .piece_count = 1},
        {.flags = SPRITE_ASSET_METASPRITE, .pieces = meta_pieces}, // no piece_count
        {.flags = SPRITE_ASSET_METASPRITE, .piece_count = 1},      // no pieces
        // Valid: the pivot drawn at (x, y) - origin, 5 to the right, 3 up.
        {.flags = SPRITE_ASSET_METASPRITE,
         .pieces = meta_pieces,
         .piece_count = 2,
         .origin_x = -5,
         .origin_y = 3},
    };
    static const SpriteAsset* const bad_table[] = {&small,  &anim,   &wide,   &small,  &meta,
                                                   &bad[0], &bad[1], &bad[2], &bad[3], &bad[4]};
    static const u16 ids[5][1] = {{5}, {6}, {7}, {8}, {9}};
    sprite_table_set(bad_table, 10);
    u32 before = debug_warning_count();
    for (u32 k = 0; k < 4; k++) {
        SpriteGroup g = {.sprite_ids = ids[k], .sprite_count = 1};
        CHECK(!sprite_group_load(&g));
    }
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 4);
#else
    CHECK(debug_warning_count() == before);
#endif
    SpriteGroup shifted = {.sprite_ids = ids[4], .sprite_count = 1};
    CHECK(sprite_group_load(&first) && sprite_group_load(&shifted));
    frame_begin();
    sprite_draw(9, 0, 100, 50, 0);
    frame_end();
    CHECK(OAM_X(0) == 86 + 5 && OAM_Y(0) == 46 - 3);
    CHECK(OAM_X(1) == 106 + 5 && OAM_Y(1) == 50 - 3);
    sprite_groups_reset();
    // Pieces whose sprites aren't loaded aren't drawn (and are reported).
    CHECK(sprite_group_load(&meta_group));
    frame_begin();
    sprite_draw(SPR_META, 0, 100, 50, 0);
    CHECK(serval_oam_used == 0);
    sprite_draw(SPR_META, 2, 100, 50, 0); // no frame 2
    CHECK(serval_oam_used == 0);
    frame_end();
    sprite_table_set(table, SPRITE_COUNT);
}

// --- Marks: loading in layers ------------------------------------------------------

// Whether sprite `id` frame 0 is drawn now (a fresh frame, drawn alone).
static bool drawn(u16 id) {
    frame_begin();
    sprite_draw(id, 0, 50, 50, 0);
    frame_end();
    return !(oam_mem[0].attr0 & ATTR0_HIDE);
}

static void release_unloads_the_groups_loaded_since_the_mark(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first)); // tiles 0-2, banks 0-1
    u32 mark = sprite_groups_mark();
    CHECK(sprite_group_load(&second)); // tiles 3-4, bank 2
    CHECK(sprite_group_load(&meta_group));
    CHECK(drawn(SPR_WIDE) && drawn(SPR_META));
    u32 before = debug_warning_count();
    sprite_groups_release(mark);
    CHECK(debug_warning_count() == before);
    CHECK(!drawn(SPR_WIDE) && !drawn(SPR_META));
    CHECK(drawn(SPR_SMALL) && drawn(SPR_ANIM)); // loaded before the mark
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 1 && OAM_BANK(0) == 1);
    // The mark stays, and the next loads reuse the released VRAM and banks.
    CHECK(sprite_groups_mark() == mark);
    CHECK(sprite_group_load(&second));
    frame_begin();
    sprite_draw(SPR_WIDE, 0, 0, 0, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 3 && OAM_BANK(0) == 2);
    sprite_groups_release(mark); // again: every room change
    CHECK(!drawn(SPR_WIDE) && drawn(SPR_SMALL));
}

static void marks_nest(void) {
    sprite_table_set(table, SPRITE_COUNT);
    u32 outer = sprite_groups_mark(); // nothing loaded yet
    CHECK(sprite_group_load(&first));
    u32 middle = sprite_groups_mark();
    CHECK(middle != outer);
    CHECK(sprite_groups_mark() == middle); // nothing loaded since: the same mark
    CHECK(sprite_group_load(&second));
    u32 inner = sprite_groups_mark();
    CHECK(sprite_group_load(&meta_group)); // no VRAM, but loaded after `inner`
    CHECK(inner != middle);
    sprite_groups_release(inner);
    CHECK(!drawn(SPR_META) && drawn(SPR_WIDE) && drawn(SPR_SMALL));
    sprite_groups_release(middle);
    CHECK(!drawn(SPR_WIDE) && drawn(SPR_SMALL));
    sprite_groups_release(outer);
    CHECK(!drawn(SPR_SMALL));
    CHECK(sprite_group_load(&second)); // from tile 0 and bank 0 again
    frame_begin();
    sprite_draw(SPR_WIDE, 0, 0, 0, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 0 && OAM_BANK(0) == 0);
}

// A load that fails consumes nothing, a group with no VRAM of its own (only
// metasprites) still counts as loaded, and a sprite loaded twice is drawn
// from the newest copy, so releasing that one unloads it.
static void marks_follow_loads_not_vram(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));
    u32 mark = sprite_groups_mark();
    SpriteGroup too_many = first;
    too_many.palette_count = 17;
    CHECK(!sprite_group_load(&too_many));
    CHECK(sprite_groups_mark() == mark);
    CHECK(sprite_group_load(&meta_group));
    u32 after_meta = sprite_groups_mark();
    CHECK(after_meta != mark);
    sprite_groups_release(after_meta);
    CHECK(drawn(SPR_META));           // loaded before after_meta
    CHECK(sprite_group_load(&first)); // SPR_SMALL and SPR_ANIM again, from tile 3
    CHECK(drawn(SPR_SMALL) && (oam_mem[0].attr2 & ATTR2_ID_MASK) == 3);
    // Releasing the second copy unloads them, though the first copy is still
    // in VRAM (sprites.h: keep each sprite in one group).
    sprite_groups_release(after_meta);
    CHECK(!drawn(SPR_SMALL) && !drawn(SPR_ANIM));
}

// Marks that are no longer current are ignored, with a warning each time:
// forgotten by releasing to an earlier mark, from before a reset or
// sprite_table_set(), or never returned.
static void stale_marks_are_ignored(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));
    u32 outer = sprite_groups_mark();
    CHECK(sprite_group_load(&second));
    u32 inner = sprite_groups_mark();
    CHECK(sprite_group_load(&meta_group));
    sprite_groups_release(outer); // forgets `inner`
    CHECK(sprite_group_load(&second));
    u32 before = debug_warning_count();
    sprite_groups_release(inner);
    CHECK(drawn(SPR_WIDE));
    u32 unknown = sprite_groups_mark() + 1000;
    sprite_groups_release(unknown);
    sprite_groups_release(0);
    sprite_groups_reset();
    CHECK(sprite_group_load(&first));
    sprite_groups_release(outer); // from before the reset
    CHECK(drawn(SPR_SMALL));
    u32 now = sprite_groups_mark();
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));
    sprite_groups_release(now); // from before sprite_table_set()
    CHECK(drawn(SPR_SMALL));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 5);
#else
    CHECK(debug_warning_count() == before);
#endif
}

// Up to 16 nested marks; past that, sprite_groups_mark() returns the newest
// (warning), which releases the groups loaded since that one too.
static void sixteen_marks_nest(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&first));
    u32 marks[16];
    for (u32 k = 0; k < 16; k++) {
        marks[k] = sprite_groups_mark();
        CHECK(sprite_group_load(&meta_group)); // no VRAM: as many as needed
    }
    bool distinct = true;
    for (u32 k = 1; k < 16; k++)
        distinct &= marks[k] != marks[k - 1];
    CHECK(distinct);
    u32 before = debug_warning_count();
    CHECK(sprite_groups_mark() == marks[15]);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 1);
#else
    CHECK(debug_warning_count() == before);
#endif
    sprite_groups_release(marks[15]);
    CHECK(!drawn(SPR_META));
    sprite_groups_release(marks[0]);
    CHECK(drawn(SPR_SMALL) && !drawn(SPR_META));
    CHECK(sprite_groups_mark() == marks[0]);
}

// sprite_stats_scanlines(true): the per-scanline budget, by GBATEK's rules
// (1,210 cycles; 10 + 2 x the box's width for an affine sprite).
static void scanline_budget_is_counted_when_asked(void) {
    static const u16 big_ids[] = {0};
    static const SpriteGroup one_big = {
        .sprite_ids = big_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    sprite_table_set(big_table, BIG_COUNT);
    CHECK(sprite_group_load(&one_big)); // 64x64
    for (int on = 0; on < 2; on++) {
        sprite_stats_scanlines(on);
        frame_begin();
        // Four rotated (double-size 128 box: 266 each) fit: 1,064 cycles.
        // The fifth (1,330) and an unrotated one after it (+64) don't.
        for (int k = 0; k < 5; k++)
            sprite_draw_rotated(0, 0, 40 * k, 60, ANGLE_DEG(30), 0);
        sprite_draw(0, 0, 100, 60, 0);
        // Off screen: no cycles.
        gba_oam_submit((u16)(ATTR0_SQUARE | 170), (u16)(ATTR1_SIZE_64 | 250), 0);
        frame_end();
        SpriteStats st = sprite_stats();
        CHECK(st.drawn == 7);
        CHECK(st.cut_short == (on ? 2 : 0));
        CHECK(st.busiest_line == (on ? 1394 : 0));
    }
    // Spread over different lines, nothing is lost.
    frame_begin();
    for (int k = 0; k < 5; k++)
        sprite_draw_rotated(0, 0, 40 * k, k ? 150 : 10, ANGLE_DEG(30), 0);
    frame_end();
    CHECK(sprite_stats().cut_short == 0 && sprite_stats().busiest_line == 4 * 266);
    sprite_stats_scanlines(false);
    sprite_table_set(table, SPRITE_COUNT);
}

// --- Costs of the render systems ------------------------------------------------

static u32 cycles_now(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Cycles of one sys_render or sys_render_by_depth call.
static u32 render_cycles(bool by_depth) {
    frame_begin();
    u32 t0 = cycles_now();
    if (by_depth)
        sys_render_by_depth();
    else
        sys_render();
    u32 t = cycles_now() - t0;
    frame_end();
    return t;
}

// `count` 8x8 sprites on screen; depth(k) gives entity k's depth.
static void make_sprites(u32 count, s16 (*depth)(u32 k)) {
    ecs_reset();
    for (u32 k = 0; k < count; k++) {
        u32 i = entity_index(entity_create(C_POS | C_SPR));
        pos_x[i] = FX((int)(k * 37 % 232));
        pos_y[i] = FX((int)(k * 53 % 152));
        spr_depth[i] = depth(k);
    }
}

static s16 two_depths(u32 k) { // Breakout: 84 bricks, then balls in front
    return k < 84 ? 0 : 1;
}
static s16 depth_is_y(u32 k) { // bunnymark: lower on screen in front
    return (s16)(k * 53 % 152);
}
static s16 one_depth(u32 k) {
    (void)k;
    return 0;
}

static void render_costs_are_logged(void) {
    sprite_table_set(table, SPRITE_COUNT);
    sprite_group_load(&first);
    make_sprites(88, two_depths);
    u32 plain = render_cycles(false), two = render_cycles(true);
    // The balls (slots 84-87) come first in OAM, so in front of the bricks.
    CHECK(oam_mem[0].attr0 != oam_mem[4].attr0 || oam_mem[0].attr1 != oam_mem[4].attr1);
    make_sprites(128, depth_is_y);
    u32 plain128 = render_cycles(false), by_y = render_cycles(true);
    make_sprites(128, one_depth);
    u32 same = render_cycles(true);
    debug_log(text_format("render: 88 sprites: sys_render %u cycles, by depth (two depths) %u",
                          plain, two));
    debug_log(text_format("render: 128 sprites: sys_render %u, by depth (y) %u, (one depth) %u",
                          plain128, by_y, same));
    ecs_reset();
}

TEST_SUITE(
    sprite_tests, "sprites", {"load_copies_tiles_and_palettes", load_copies_tiles_and_palettes},
    {"draw_writes_position_shape_and_tile", draw_writes_position_shape_and_tile},
    {"draw_applies_frame_origin_and_palette_slot", draw_applies_frame_origin_and_palette_slot},
    {"draw_applies_flags", draw_applies_flags},
    {"layers_map_to_hardware_priority", layers_map_to_hardware_priority},
    {"partly_offscreen_wraps_coordinates", partly_offscreen_wraps_coordinates},
    {"fully_offscreen_is_not_drawn", fully_offscreen_is_not_drawn},
    {"unloaded_sprite_or_bad_frame_is_not_drawn", unloaded_sprite_or_bad_frame_is_not_drawn},
    {"groups_are_allocated_one_after_another", groups_are_allocated_one_after_another},
    {"load_fails_when_out_of_vram_or_palettes", load_fails_when_out_of_vram_or_palettes},
    {"load_rejects_unsupported_groups", load_rejects_unsupported_groups},
    {"load_refuses_reserved_values", load_refuses_reserved_values},
    {"reset_unloads_everything", reset_unloads_everything},
    {"misuse_is_reported_once_in_debug_builds", misuse_is_reported_once_in_debug_builds},
    {"defaults_and_computed_tile_counts", defaults_and_computed_tile_counts},
    {"rotation_uses_a_double_size_affine_sprite", rotation_uses_a_double_size_affine_sprite},
    {"sprites_share_matrices_by_angle_and_flips", sprites_share_matrices_by_angle_and_flips},
    {"past_32_angles_sprites_are_drawn_unrotated", past_32_angles_sprites_are_drawn_unrotated},
    {"offscreen_rotated_sprites_take_no_matrix", offscreen_rotated_sprites_take_no_matrix},
    {"rotated_sprites_past_full_oam_take_no_matrix", rotated_sprites_past_full_oam_take_no_matrix},
    {"angle_zero_draws_like_sprite_draw", angle_zero_draws_like_sprite_draw},
    {"ids_past_the_maximum_are_reported_apart_from_511",
     ids_past_the_maximum_are_reported_apart_from_511},
    {"load_rejects_incomplete_data", load_rejects_incomplete_data},
    {"sys_render_rotates_entities_with_an_angle", sys_render_rotates_entities_with_an_angle},
    {"screen_space_entities_ignore_the_camera", screen_space_entities_ignore_the_camera},
    {"sprite_palette_selects_a_palette_of_the_group",
     sprite_palette_selects_a_palette_of_the_group},
    {"entities_draw_with_their_sprite_palette", entities_draw_with_their_sprite_palette},
    {"scaling_enlarges_in_a_double_size_box", scaling_enlarges_in_a_double_size_box},
    {"shrinking_and_mirroring_use_the_sprites_own_box",
     shrinking_and_mirroring_use_the_sprites_own_box},
    {"scales_share_matrices_and_zero_draws_nothing", scales_share_matrices_and_zero_draws_nothing},
    {"entities_scale_with_sprite_scaled", entities_scale_with_sprite_scaled},
    {"sprite_stats_count_the_last_frame", sprite_stats_count_the_last_frame},
    {"depth_order_holds_for_every_kind_of_sort", depth_order_holds_for_every_kind_of_sort},
    {"metasprites_draw_their_pieces_around_the_pivot",
     metasprites_draw_their_pieces_around_the_pivot},
    {"metasprites_rotate_and_scale_about_the_pivot", metasprites_rotate_and_scale_about_the_pivot},
    {"metasprite_entities_are_drawn_and_sorted_as_one",
     metasprite_entities_are_drawn_and_sorted_as_one},
    {"metasprites_need_valid_pieces", metasprites_need_valid_pieces},
    {"release_unloads_the_groups_loaded_since_the_mark",
     release_unloads_the_groups_loaded_since_the_mark},
    {"marks_nest", marks_nest}, {"marks_follow_loads_not_vram", marks_follow_loads_not_vram},
    {"stale_marks_are_ignored", stale_marks_are_ignored},
    {"sixteen_marks_nest", sixteen_marks_nest},
    {"scanline_budget_is_counted_when_asked", scanline_budget_is_counted_when_asked},
    {"render_costs_are_logged", render_costs_are_logged});
