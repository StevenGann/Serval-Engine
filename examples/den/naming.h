#ifndef DEN_NAMING_H
#define DEN_NAMING_H

// Naming a kitten: a letter grid (A-Z, DEL, END), up to 8 letters, a random
// suggestion from litter_names to start with. The screen is a full-screen
// page of text over the backdrop; the caller has the den off the screen and
// fades.

#include <serval/core.h>

// Runs the page until the player accepts a name (END, or START; at least one
// letter), and writes it to `out` (8 letters and a NUL). `title` heads the
// page; `suggestion` is the name filled in to start with (NULL: a random one
// from litter_names). Clears the text on the way out. Call with the screen
// visible; buttons held at entry are ignored until released.
void naming_run(char out[9], const char* title, const char* suggestion);

#endif // DEN_NAMING_H
