// The planned API of sprites.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.

// Calls planned API on purpose: without this, every call would warn.
#define SERVAL_NO_PLANNED_WARNINGS

#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/math.h"
#include "serval/sprites.h"
#include "test.h"

// The planned values are part of the frozen data formats and draw flags
// (docs/sprites.md#rom-data-format): they must not move, and must not
// overlap the bits already in use.
static void planned_values_are_fixed(void) {
    CHECK(SPRITE_GROUP_STREAMED == 1 << 0);
    CHECK(SPRITE_ASSET_LZ77 == 1 << 3);
    CHECK((SPRITE_ASSET_LZ77 & (SPRITE_ASSET_METASPRITE | SPRITE_ASSET_ANIM_ONCE)) == 0);
    CHECK(SPRITE_BLEND == 1 << 12);
    const u32 others = SPRITE_FLIP_H | SPRITE_FLIP_V | SPRITE_ABOVE_FOREGROUND | SPRITE_HIDDEN |
                       SPRITE_ANIM_FLIP_H | SPRITE_ANIM_FLIP_V | SPRITE_SCALED |
                       SPRITE_PALETTE_MASK | SPRITE_SCREEN;
    CHECK((SPRITE_BLEND & others) == 0);
    CHECK(SPRITE_MAX_TILE_UPDATES == 8);
}

#ifdef SERVAL_GBA
// Hardware memory, read directly: the shared suites don't see libtonc.
#define OAM ((const volatile u16*)0x07000000) // 4 halfwords per sprite
#define OBJ_VRAM ((const volatile u32*)0x06010000)
#define OBJ_PALETTE ((const volatile u16*)0x05000200)
#define OAM_ATTR0(k) OAM[(k) * 4]
#define OAM_HIDDEN(k) ((OAM_ATTR0(k) & 0x0300) == 0x0200) // not affine, disabled
#define OAM_MODE(k) (OAM_ATTR0(k) & 0x0C00)               // 0 normal, 0x400 semi-transparent
#define OAM_BANK(k) (OAM[(k) * 4 + 2] >> 12)

enum { SPR_DOT, SPR_SHADOW, SPR_META, SPR_PACKED, SPRITE_COUNT };

static const u32 dot_tiles[8] = {0x11111111, 0x11111111};
static const u32 other_tiles[8] = {0x22222222, 0x22222222};
static const SpritePiece shadow_pieces[] = {
    {.sprite = SPR_DOT},
    {.x = 2, .y = 2, .sprite = SPR_DOT, .flags = SPRITE_BLEND}, // a piece may blend
};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
static const SpriteAsset shadow = {
    .flags = SPRITE_ASSET_METASPRITE, .pieces = shadow_pieces, .piece_count = 2};
static const SpriteAsset packed = {
    .size = SPRITE_8x8, .tiles = dot_tiles, .flags = SPRITE_ASSET_LZ77};
static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_DOT] = &dot, [SPR_SHADOW] = &shadow, [SPR_META] = &shadow, [SPR_PACKED] = &packed};
static const u16 palettes[32] = {[1] = 0x1234, [17] = 0x0567};
static const u16 group_ids[] = {SPR_DOT, SPR_SHADOW};
static const SpriteGroup group = {
    .sprite_ids = group_ids, .palettes = palettes, .sprite_count = 2, .palette_count = 2};

static void load(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&group));
}

// Checks that `count` warnings were reported since `before` (none in release
// builds, which report nothing).
static void check_warnings(u32 before, u32 count) {
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + count);
#else
    (void)count;
    CHECK(debug_warning_count() == before);
#endif
}

static void sprite_set_tiles_changes_nothing(void) {
    load();
    for (u32 call = 0; call < 2; call++) {
        u32 before = debug_warning_count();
        frame_begin();
        sprite_set_tiles(SPR_DOT, 0, other_tiles);
        frame_end();
        CHECK(OBJ_VRAM[0] == 0x11111111); // tile 0 is SPR_DOT's, as loaded
        check_warnings(before, call == 0);
    }
}

static void sprite_set_colors_changes_nothing(void) {
    load();
    static const Color red = 0x001F;
    for (u32 call = 0; call < 2; call++) {
        u32 before = debug_warning_count();
        frame_begin();
        sprite_set_colors(SPR_DOT, 1, &red, 1);
        sprite_set_colors(SPR_DOT, 17, &red, 1); // the second call of the frame: no warning
        frame_end();
        CHECK(OBJ_PALETTE[1] == 0x1234 && OBJ_PALETTE[17] == 0x0567);
        check_warnings(before, call == 0);
    }
}

// A loader refuses data that needs a planned feature, on every attempt, and
// loads nothing of it: the next group still lands at tile 0.
static void loads_needing_planned_features_are_refused(void) {
    static const u16 packed_ids[] = {SPR_PACKED};
    static const SpriteGroup with_packed = {
        .sprite_ids = packed_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    static const SpriteGroup streamed = {.sprite_ids = group_ids,
                                         .palettes = palettes,
                                         .sprite_count = 2,
                                         .palette_count = 2,
                                         .flags = SPRITE_GROUP_STREAMED,
                                         .slots = 4};
    sprite_table_set(table, SPRITE_COUNT);
    for (u32 call = 0; call < 2; call++) {
        u32 before = debug_warning_count();
        CHECK(!sprite_group_load(&with_packed));
        CHECK(!sprite_group_load(&streamed));
        check_warnings(before, 2);
    }
    CHECK(sprite_group_load(&group));
    frame_begin();
    sprite_draw(SPR_DOT, 0, 10, 10, 0);
    frame_end();
    CHECK((OAM[2] & 0x3FF) == 0 && OAM_BANK(0) == 0);
}

// The draws a frame makes, for blended_sprites_draw_opaque_and_warn_once.
static void draw_plain(void) {
    sprite_draw(SPR_DOT, 0, 20, 20, SPRITE_BLEND);
}
static void draw_rotated(void) {
    sprite_draw_rotated(SPR_DOT, 0, 20, 20, ANGLE_DEG(90), SPRITE_BLEND);
}
static void draw_scaled(void) {
    sprite_draw_ex(SPR_DOT, 0, 20, 20, 0, FX(2), FX(2), SPRITE_BLEND);
}
static void draw_whole_metasprite(void) {
    sprite_draw(SPR_META, 0, 20, 20, SPRITE_BLEND); // both pieces
}
static void draw_blended_piece(void) {
    sprite_draw(SPR_SHADOW, 0, 20, 20, 0); // only the second piece
}
static void render(void) {
    sys_render();
}
static void render_by_depth(void) {
    sys_render_by_depth();
}

// SPRITE_BLEND is planned: every way of drawing draws the sprite, opaque,
// and warns once. With the render systems, the warning also shows that the
// flag took the out-of-line path (the plain one doesn't look at it).
static void blended_sprites_draw_opaque_and_warn_once(void) {
    static void (*const draws[])(void) = {
        draw_plain,         draw_rotated, draw_scaled,    draw_whole_metasprite,
        draw_blended_piece, render,       render_by_depth};
    static const u32 drawn[] = {1, 1, 1, 2, 2, 1, 1};
    for (u32 k = 0; k < sizeof(draws) / sizeof(draws[0]); k++) {
        load(); // also clears the "warned once" state
        static const u16 meta_ids[] = {SPR_META};
        static const SpriteGroup meta = {.sprite_ids = meta_ids, .sprite_count = 1};
        CHECK(sprite_group_load(&meta));
        ecs_reset();
        u32 i = entity_index(entity_create(C_POS | C_SPR));
        pos_x[i] = pos_y[i] = FX(30);
        spr_id[i] = SPR_DOT;
        // SPRITE_BLEND alone: with SPRITE_PALETTE the entity would take the
        // out-of-line path anyway.
        spr_flags[i] = SPRITE_BLEND;
        for (u32 frame = 0; frame < 2; frame++) {
            u32 before = debug_warning_count();
            frame_begin();
            draws[k]();
            frame_end();
            bool ok = OAM_HIDDEN(drawn[k]);
            for (u32 s = 0; s < drawn[k]; s++)
                ok &= !OAM_HIDDEN(s) && OAM_MODE(s) == 0;
            CHECK(ok);
            check_warnings(before, frame == 0);
        }
        CHECK(OAM_BANK(0) == 0); // its own palette
        ecs_reset();
    }
}
#endif

#ifdef SERVAL_GBA
#define GBA_CASES                                                                                  \
    , {"sprite_set_tiles_changes_nothing", sprite_set_tiles_changes_nothing},                      \
        {"sprite_set_colors_changes_nothing", sprite_set_colors_changes_nothing},                  \
        {"loads_needing_planned_features_are_refused",                                             \
         loads_needing_planned_features_are_refused},                                              \
    {                                                                                              \
        "blended_sprites_draw_opaque_and_warn_once", blended_sprites_draw_opaque_and_warn_once     \
    }
#else
#define GBA_CASES
#endif

TEST_SUITE(planned_sprites_tests, "planned_sprites",
           {"planned_values_are_fixed", planned_values_are_fixed} GBA_CASES);
