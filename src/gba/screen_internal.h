#ifndef SERVAL_GBA_SCREEN_INTERNAL_H
#define SERVAL_GBA_SCREEN_INTERNAL_H

// Engine-internal: screen presentation state the splash screen borrows and
// puts back, and tests inspect. Not part of the public API.

#include <tonc_types.h>

// Brightness (screen.c): the level last set with screen_set_brightness() (0
// if never; BLDY is write-only, so it is kept), and writing it to the blend
// registers again.
int serval_screen_brightness(void);
void serval_screen_apply_brightness(void);

// Text layer (text.c): whether glyphs are drawn with a shadow
// (text_set_shadow).
bool serval_text_shadow(void);

#endif // SERVAL_GBA_SCREEN_INTERNAL_H
