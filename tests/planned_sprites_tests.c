// The planned API of sprites.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// sprites.h has no planned names in this version: streamed groups
// (tests/rom/sprite_stream_tests.c), runtime tiles, blending, palette writes
// and LZ77 sprites (tests/rom/lz77_tests.c) are implemented. A planned
// function added later gets its cases here.

#include "serval/sprites.h"
#include "test.h"

// The values the API froze as planned are part of the frozen data formats
// and draw flags (docs/sprites.md#rom-data-format), including those since
// implemented (SPRITE_GROUP_STREAMED, SPRITE_BLEND, SPRITE_ASSET_LZ77): they
// must not move, and must not overlap the bits already in use.
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

TEST_SUITE(planned_sprites_tests, "planned_sprites",
           {"planned_values_are_fixed", planned_values_are_fixed});
