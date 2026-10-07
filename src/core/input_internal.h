#ifndef SERVAL_CORE_INPUT_INTERNAL_H
#define SERVAL_CORE_INPUT_INTERNAL_H

// Engine-internal: the held-button counters behind button_repeat(), fed by
// the frame loop (src/gba/core.c). Not part of the public API.

#include "serval/platform.h"

// Forgets held buttons (and any silenced by button_repeat_reset) and sets the
// default delay and interval. Called by serval_init(). Not the public
// button_repeat_reset() (core.h), which only silences the buttons held now
// and keeps the delay and interval.
void serval_repeat_reset(void);

// Records the buttons held at the start of a frame (BUTTON_* bits). Called by
// frame_begin() after polling input.
void serval_repeat_frame(u32 buttons);

#endif // SERVAL_CORE_INPUT_INTERNAL_H
