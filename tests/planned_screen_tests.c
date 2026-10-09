// The planned API of screen.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.

// Calls planned API on purpose: without this, every call would warn.
#define SERVAL_NO_PLANNED_WARNINGS

#include "serval/screen.h"
#include "test.h"

#ifdef SERVAL_GBA
// screen.h's planned functions are all stubs in src/gba/screen.c.

#include "serval/core.h"
#include "serval/debug.h"

#include <tonc.h>

#include "../src/gba/screen_internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

static void show_frame(void) {
    frame_begin();
    frame_end();
}

// The raster stubs claim none of the hardware raster effects will use (DMA 0,
// the HBlank interrupt) and leave the backdrop alone; each warns once.
static void raster_effects_change_nothing(void) {
    static const s16 offsets[SCREEN_H] = {[0] = 4, [80] = -4};
    static const Color colors[SCREEN_H] = {
        [0] = COLOR_RGB(255, 0, 0), [159] = COLOR_RGB(0, 0, 255)};
    Color old_backdrop = pal_bg_mem[0], backdrop = COLOR_RGB(16, 32, 80);
    screen_set_backdrop(backdrop);
    u16 ie = REG_IE;
    u32 before = debug_warning_count();
    raster_scroll(2, false, offsets);
    CHECK(debug_warning_count() == before + WARNINGS(1));
    raster_scroll(3, true, offsets);
    CHECK(debug_warning_count() == before + WARNINGS(1));
    raster_backdrop(colors);
    CHECK(debug_warning_count() == before + WARNINGS(2));
    raster_backdrop(colors);
    CHECK(debug_warning_count() == before + WARNINGS(2));
    raster_clear();
    CHECK(debug_warning_count() == before + WARNINGS(3));
    raster_clear();
    CHECK(debug_warning_count() == before + WARNINGS(3));
    show_frame();
    CHECK(!(REG_DMA0CNT_H & (DMA_ENABLE >> 16)));
    CHECK(REG_IE == ie && !(REG_IE & IRQ_HBLANK));
    CHECK(!(REG_DISPSTAT & DSTAT_HBL_IRQ));
    CHECK(pal_bg_mem[0] == backdrop);
    screen_set_backdrop(old_backdrop);
}

TEST_SUITE(planned_screen_tests, "planned_screen",
           {"raster_effects_change_nothing", raster_effects_change_nothing});
#else
// The host build links no stubs of screen.h's planned API (src/gba/ only).
TEST_SUITE(planned_screen_tests, "planned_screen");
#endif
