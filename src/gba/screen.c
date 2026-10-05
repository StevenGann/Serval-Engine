// Screen-wide effects: brightness (fade to black or white) through the
// hardware's color special effect.

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
    // Every layer and the backdrop are first targets; the mode picks the
    // direction, BLDY (0-16) how far.
    if (brightness == 0)
        REG_BLDCNT = BLD_OFF;
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
