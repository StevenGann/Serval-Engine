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
    static const u8 streamed = SPRITE_GROUP_STREAMED; // planned
    static const u8 packed = SPRITE_ASSET_LZ77;       // planned
    (void)streamed;
    (void)packed;
}
