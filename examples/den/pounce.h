#ifndef DEN_POUNCE_H
#define DEN_POUNCE_H

// High Pounce on screen, the adult's game (rules in pounce_rules.h): the
// serval hunts by ear in tall grass for 60 seconds. Patches of grass shake
// and rustle (the louder, the nearer; the engine's sound is mono, so an
// arrow over the serval's head points to the last rustle it heard); some hide
// a mouse, some a bird. LEFT and RIGHT creep, B held crouches (slower and
// quiet: a bird isn't startled), A leaps 48 pixels ahead in a high arc: land
// on a rustle to catch the mouse; a bird that flies up can be swatted with
// another A. START pauses (RESUME or QUIT, which ends the round and counts
// it). A round that ends shows its score, the best and the fun earned; the
// pet is played with and the best kept.

#include "menu.h"

// Where the best score lives (the save's best[2]); call once before the game
// is chosen.
void pounce_attach(u16* best);

// The game, as the menu calls it (menu.h's MenuGameFn): plays one round and
// leaves the screen black. True: the pet changed.
bool pounce_play(Pet* pet, u32 now);

#endif // DEN_POUNCE_H
