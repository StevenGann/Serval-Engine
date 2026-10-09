// Every planned name in map.h (SERVAL_PLANNED), used once, each on its own line
// ending "// planned". tools/check-planned.py (CTest planned_api; part of the
// build in web builds) compiles this file and fails unless each marked line
// warns as planned, no other line warns, and every planned name in the public
// headers is used in one of these files. When a name is implemented and its
// SERVAL_PLANNED goes, its line goes too. This file is in no build target: it
// warns on purpose. See docs/development.md#planned-api.

#include "serval/map.h"

void planned_map(void);
void planned_map(void) {
    static const Tileset lz77 = {.flags = TILESET_LZ77}; // planned
    (void)lz77;
    static const u8 ladder = MAP_LADDER;          // planned
    static const u8 slopes[] = {MAP_SLOPE_R,      // planned
                                MAP_SLOPE_L,      // planned
                                MAP_SLOPE_R_LOW,  // planned
                                MAP_SLOPE_R_HIGH, // planned
                                MAP_SLOPE_L_HIGH, // planned
                                MAP_SLOPE_L_LOW}; // planned
    static const u8 contact = MAP_CONTACT_LADDER; // planned
    (void)ladder;
    (void)slopes;
    (void)contact;
}
