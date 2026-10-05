#ifndef SERVAL_SCREEN_H
#define SERVAL_SCREEN_H

// Screen size and colors.

#include "serval/platform.h"

// A color in the target's native format (BGR555 on the GBA).
typedef u16 Color;

// Builds a Color from 8-bit components (0-255), dropping precision the target
// cannot show. Usable in static initializers, e.g. palettes.
#define COLOR_RGB(r, g, b) ((Color)(((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10)))

// Screen size in pixels, as constants for array sizes, static initializers and
// case labels. (libtonc's SCREEN_WIDTH is the same value under another name.)
#define SCREEN_W 240
#define SCREEN_H 160

static inline int screen_width(void) {
    return SCREEN_W;
}

static inline int screen_height(void) {
    return SCREEN_H;
}

// Sets the color shown wherever nothing else is drawn.
void screen_set_backdrop(Color color);

// Brightness of the whole screen (backgrounds, sprites and backdrop): from
// SCREEN_BRIGHTNESS_MIN (-16, black) through 0 (normal, the default) to
// SCREEN_BRIGHTNESS_MAX (16, white); values outside are clamped (warning in
// debug builds). Stays until changed. To fade, step it once per frame, e.g.
// from 0 to -16 over 16 frames. Takes effect at once, like
// screen_set_backdrop(): set it before the frame's game logic (right after
// frame_begin()) so the change lines up with the next frame.
//
// It uses the hardware's color special effect (GBA: BLDCNT and BLDY), which
// does one effect at a time: while the brightness is not 0, the engine has
// no other blending (there is no alpha blending yet). The splash screen
// (serval_splash) borrows the effect and puts the game's brightness back.
#define SCREEN_BRIGHTNESS_MIN (-16)
#define SCREEN_BRIGHTNESS_MAX 16
void screen_set_brightness(int level);

#endif // SERVAL_SCREEN_H
