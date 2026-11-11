#ifndef DEN_LITTER_H
#define DEN_LITTER_H

// The litter a pet leaves: 2-4 kittens whose coats come from the mother and
// the visiting serval, and their names. Portable C; random numbers come from
// the engine's (random_seed() makes it repeatable).

#include "pet.h"

#define LITTER_MAX 4
#define LITTER_NAME_COUNT 32

typedef struct Litter {
    u8 count;
    Coat coats[LITTER_MAX];
    u8 names[LITTER_MAX]; // indices into litter_names
} Litter;

// 32 original names, at most 8 letters, A-Z.
extern const char* const litter_names[LITTER_NAME_COUNT];

// A random coat for the visiting serval.
Coat litter_visitor_coat(void);

// 2-4 kittens. Each trait comes from either parent; one time in eight it is
// replaced by a random value in its range. Names are distinct within the
// litter and never `avoid_name` (an index; 255 or more avoids none).
void litter_make(Litter* out, Coat mother, Coat father, u8 avoid_name);

#endif // DEN_LITTER_H
