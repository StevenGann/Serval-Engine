#ifndef SERVAL_CORE_INPUT_INTERNAL_H
#define SERVAL_CORE_INPUT_INTERNAL_H

// Engine-internal: the held-button counters behind button_repeat() and the
// secret sequence behind button_secret_set(), fed by the frame loop
// (src/gba/core.c). Not part of the public API.

#include "serval/platform.h"

// Forgets held buttons (and any silenced by button_repeat_reset), sets the
// default delay and interval, and clears the secret sequence's hook and
// progress. Called by serval_init(). Not the public button_repeat_reset()
// (core.h), which only silences the buttons held now and keeps the delay and
// interval.
void serval_repeat_reset(void);

// Records the buttons held at the start of a frame (BUTTON_* bits), and calls
// the secret sequence's hook on the frame it is completed. Called by
// frame_begin() after polling input, as its last step.
void serval_repeat_frame(u32 buttons);

#endif // SERVAL_CORE_INPUT_INTERNAL_H
