#ifndef SERVAL_CORE_INPUT_INTERNAL_H
#define SERVAL_CORE_INPUT_INTERNAL_H

// Engine-internal: the held-button counters behind button_repeat(), fed by
// the frame loop (src/gba/core.c). Not part of the public API.

#include "serval/platform.h"

// Forgets held buttons and sets the default delay and interval. Called by
// serval_init().
void serval_repeat_reset(void);

// Records the buttons held at the start of a frame (BUTTON_* bits). Called by
// frame_begin() after polling input.
void serval_repeat_frame(u32 buttons);

#endif // SERVAL_CORE_INPUT_INTERNAL_H
