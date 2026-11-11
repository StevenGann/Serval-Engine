#ifndef DEN_YARN_H
#define DEN_YARN_H

// Yarn Bat on screen, the kitten's game (rules in yarn_rules.h): the pet
// bats a ball of yarn against the den's far wall and the thread unravels
// behind it. UP and DOWN move the paw, START pauses (RESUME or QUIT, which
// ends the round and counts it). A round that ends shows its bats, the best
// and the fun earned; the pet is played with and the best kept.

#include "menu.h"

// Where the best score lives (the save's best[0]); call once before the
// game is chosen.
void yarn_attach(u16* best);

// The game, as the menu calls it (menu.h's MenuGameFn): plays one round and
// leaves the screen black. True: the pet changed.
bool yarn_play(Pet* pet, u32 now);

#endif // DEN_YARN_H
