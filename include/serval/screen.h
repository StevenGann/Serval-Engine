#ifndef SERVAL_SCREEN_H
#define SERVAL_SCREEN_H

// Screen size and colors.

#include "serval/platform.h"

// A color in the target's native format (BGR555 on the GBA).
typedef u16 Color;

// Builds a Color from 8-bit components (0-255), dropping precision the target
// cannot show. Usable in static initializers, e.g. palettes.
#define COLOR_RGB(r, g, b) ((Color)(((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10)))

static inline int screen_width(void) {
    return 240;
}

static inline int screen_height(void) {
    return 160;
}

// Sets the color shown wherever nothing else is drawn.
void screen_set_backdrop(Color color);

#endif // SERVAL_SCREEN_H
