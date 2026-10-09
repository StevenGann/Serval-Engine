// Every planned name in screen.h (SERVAL_PLANNED), used once, each on its own line
// ending "// planned". tools/check-planned.py (CTest planned_api; part of the
// build in web builds) compiles this file and fails unless each marked line
// warns as planned, no other line warns, and every planned name in the public
// headers is used in one of these files. When a name is implemented and its
// SERVAL_PLANNED goes, its line goes too. This file is in no build target: it
// warns on purpose. See docs/development.md#planned-api.

#include "serval/screen.h"

static const s16 offsets[SCREEN_H];
static const Color colors[SCREEN_H];

void planned_screen(void);
void planned_screen(void) {
    raster_scroll(2, false, offsets); // planned
    raster_backdrop(colors);          // planned
    raster_clear();                   // planned
}
