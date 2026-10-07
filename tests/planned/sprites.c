// Every planned name in sprites.h (SERVAL_PLANNED), used once, each on its own line
// ending "// planned". tools/check-planned.py (CTest planned_api; part of the
// build in web builds) compiles this file and fails unless each marked line
// warns as planned, no other line warns, and every planned name in the public
// headers is used in one of these files. When a name is implemented and its
// SERVAL_PLANNED goes, its line goes too. This file is in no build target: it
// warns on purpose. See docs/development.md#planned-api.

#include "serval/sprites.h"

void planned_sprites(void);
void planned_sprites(void) {
    static const u8 streamed = SPRITE_GROUP_STREAMED;  // planned
    static const u8 packed = SPRITE_ASSET_LZ77;        // planned
    static const u16 blended = SPRITE_BLEND;           // planned
    static const u8 updates = SPRITE_MAX_TILE_UPDATES; // planned
    static const u32 blank[8];
    static const Color white = COLOR_RGB(255, 255, 255);
    sprite_set_tiles(0, 0, blank);      // planned
    sprite_set_colors(0, 1, &white, 1); // planned
    (void)streamed;
    (void)packed;
    (void)blended;
    (void)updates;
}
