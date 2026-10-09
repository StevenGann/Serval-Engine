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

// The values the API froze as planned are part of the frozen data formats
// and draw flags (docs/sprites.md#rom-data-format), including those since
// implemented (SPRITE_GROUP_STREAMED, SPRITE_BLEND): they must not move, and
// must not overlap the bits already in use.
static void planned_values_are_fixed(void) {
    CHECK(SPRITE_GROUP_STREAMED == 1 << 0);
    CHECK(SPRITE_ASSET_LZ77 == 1 << 3);
    CHECK((SPRITE_ASSET_LZ77 & (SPRITE_ASSET_METASPRITE | SPRITE_ASSET_ANIM_ONCE)) == 0);
    CHECK(SPRITE_BLEND == 1 << 12);
    const u32 others = SPRITE_FLIP_H | SPRITE_FLIP_V | SPRITE_ABOVE_FOREGROUND | SPRITE_HIDDEN |
                       SPRITE_ANIM_FLIP_H | SPRITE_ANIM_FLIP_V | SPRITE_SCALED |
                       SPRITE_PALETTE_MASK | SPRITE_SCREEN;
    CHECK((SPRITE_BLEND & others) == 0);
}

#ifdef SERVAL_GBA
// Hardware memory, read directly: the shared suites don't see libtonc.
#define OAM ((const volatile u16*)0x07000000) // 4 halfwords per sprite
#define OAM_BANK(k) (OAM[(k) * 4 + 2] >> 12)

enum { SPR_DOT, SPR_PACKED, SPRITE_COUNT };

static const u32 dot_tiles[8] = {0x11111111, 0x11111111};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
static const SpriteAsset packed = {
    .size = SPRITE_8x8, .tiles = dot_tiles, .flags = SPRITE_ASSET_LZ77};
static const SpriteAsset* const table[SPRITE_COUNT] = {[SPR_DOT] = &dot, [SPR_PACKED] = &packed};
static const u16 palettes[32] = {[1] = 0x1234, [17] = 0x0567};
static const u16 group_ids[] = {SPR_DOT};
static const SpriteGroup group = {
    .sprite_ids = group_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 2};

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

// A loader refuses data that needs a planned feature, on every attempt, and
// loads nothing of it: the next group still lands at tile 0. (Streamed
// groups, no longer planned, are tested in tests/rom/sprite_stream_tests.c.)
static void loads_needing_planned_features_are_refused(void) {
    static const u16 packed_ids[] = {SPR_PACKED};
    static const SpriteGroup with_packed = {
        .sprite_ids = packed_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    sprite_table_set(table, SPRITE_COUNT);
    for (u32 call = 0; call < 2; call++) {
        u32 before = debug_warning_count();
        CHECK(!sprite_group_load(&with_packed));
        check_warnings(before, 1);
    }
    CHECK(sprite_group_load(&group));
    frame_begin();
    sprite_draw(SPR_DOT, 0, 10, 10, 0);
    frame_end();
    CHECK((OAM[2] & 0x3FF) == 0 && OAM_BANK(0) == 0);
}
#endif

#ifdef SERVAL_GBA
#define GBA_CASES                                                                                  \
    , {                                                                                            \
        "loads_needing_planned_features_are_refused", loads_needing_planned_features_are_refused   \
    }
#else
#define GBA_CASES
#endif

TEST_SUITE(planned_sprites_tests, "planned_sprites",
           {"planned_values_are_fixed", planned_values_are_fixed} GBA_CASES);
