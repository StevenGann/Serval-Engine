#ifndef SERVAL_CORE_RANDOM_INTERNAL_H
#define SERVAL_CORE_RANDOM_INTERNAL_H

// Engine-internal: the input history behind random_entropy(), fed by the
// frame loop (src/gba/core.c). Not part of the public API.

#include "serval/platform.h"

// Forgets the history: random_entropy() then depends only on what follows.
// Called by serval_init().
void serval_entropy_reset(void);

// Records the button state polled at the start of frame `frame` (frames since
// serval_init). Called by frame_begin() after polling input.
void serval_entropy_frame(u32 frame, u32 buttons);

#endif // SERVAL_CORE_RANDOM_INTERNAL_H
