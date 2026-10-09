// Screen-wide effects: brightness (fade to black or white) through the
// hardware's color special effect, and the stubs of the planned ones (raster
// effects; docs/runtime-systems.md#special-effects). Alpha blending, which
// shares the color effect with the brightness, is blend.c.

#include "serval/screen.h"

#include <tonc.h>

#include "../core/warn.h"
#include "screen_internal.h"

// BLDY is write-only, so the level is kept here (for the splash screen, which
// borrows the effect, and for tests).
static s8 brightness;

#ifdef SERVAL_DEBUG
static bool warned_range;
#endif

void serval_screen_apply_brightness(void) {
    // Every layer and the backdrop are first targets, with no second targets,
    // so sprites marked semi-transparent fade too instead of blending; the
    // mode picks the direction, BLDY (0-16) how far. Level 0 gives the effect
    // back to alpha blending: its settings, or off (blending pauses during a
    // fade, screen.h).
    if (brightness == 0)
        REG_BLDCNT = serval_blend_control(); // blending: resumes at level 0
    else
        REG_BLDCNT = (u16)(BLD_ALL | BLD_BACKDROP | (brightness > 0 ? BLD_WHITE : BLD_BLACK));
    REG_BLDY = (u16)(brightness < 0 ? -brightness : brightness);
}

int serval_screen_brightness(void) {
    return brightness;
}

void screen_set_brightness(int level) {
    if (level < SCREEN_BRIGHTNESS_MIN || level > SCREEN_BRIGHTNESS_MAX) {
#ifdef SERVAL_DEBUG
        if (!warned_range) {
            warned_range = true;
            SERVAL_WARN("screen_set_brightness(%d): the level goes from %d (black) to %d (white); "
                        "clamped",
                        level, SCREEN_BRIGHTNESS_MIN, SCREEN_BRIGHTNESS_MAX);
        }
#endif
        level = level < 0 ? SCREEN_BRIGHTNESS_MIN : SCREEN_BRIGHTNESS_MAX;
    }
    brightness = (s8)level;
    serval_screen_apply_brightness();
}

// --- Planned API --------------------------------------------------------------
//
// Declared in screen.h with SERVAL_PLANNED, not implemented in this engine
// version (docs/development.md#planned-api). Each stub changes nothing, so the
// game sees today's behaviour (layers scrolled as a whole, one backdrop
// color), and says so once per function in debug builds: a game calling them
// every frame gets one line, not one per frame. They touch no register, so
// the hardware they will need (DMA 0 and the HBlank interrupt) stays
// untouched until then.

#ifdef SERVAL_DEBUG
static bool warned_raster_scroll, warned_raster_backdrop, warned_raster_clear;

static void warn_planned(bool* warned, const char* message) {
    if (!*warned) {
        *warned = true;
        SERVAL_WARN("%s", message);
    }
}
#define WARN_PLANNED(flag, message) warn_planned(&(flag), (message))
#else
#define WARN_PLANNED(flag, message) ((void)0)
#endif

void raster_scroll(u32 bg, bool vertical, const s16* offsets) {
    (void)bg;
    (void)vertical;
    (void)offsets;
    WARN_PLANNED(warned_raster_scroll, "raster_scroll: raster effects are planned, not "
                                       "implemented in this engine version; the layer scrolls "
                                       "as a whole");
}

void raster_backdrop(const Color* colors) {
    (void)colors;
    WARN_PLANNED(warned_raster_backdrop, "raster_backdrop: raster effects are planned, not "
                                         "implemented in this engine version; the backdrop "
                                         "stays one color");
}

void raster_clear(void) {
    WARN_PLANNED(warned_raster_clear, "raster_clear: raster effects are planned, not implemented "
                                      "in this engine version; there is no effect to end");
}
