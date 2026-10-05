// Tests for sprite groups and sprite_draw (src/gba/sprites.c), checked against
// what ends up in VRAM, palette RAM and OAM.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/math.h"
#include "serval/sprites.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

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
    {"entities_draw_with_their_sprite_palette", entities_draw_with_their_sprite_palette});
