#ifndef DEN_STATUS_H
#define DEN_STATUS_H

// The status page: the pet's needs as bars, its age, when it grows up (or
// leaves its litter), how long since it was cared for, its mood, the warning
// that neglect earns, its generation. A full-screen page of text and bar
// sprites; the caller hides the den first (art_hide()).

#include "pet.h"

// Draws the page: text when it changes (the minute, the pet), bars every
// frame. Call each frame the page is up, after the caller's own text_clear().
void status_draw(const Pet* p, u32 now);

// Forces the text to be drawn again by the next status_draw().
void status_reset(void);

// "5H 20M", "2D 5H", "7M": a length of time in minutes, in a static buffer.
const char* status_duration(u32 minutes);

#endif // DEN_STATUS_H
