#ifndef DEN_CLIMB_H
#define DEN_CLIMB_H

// Acacia Climb on screen, the adolescent's game (rules in climb_rules.h): the
// serval leaps from bough to bough up an acacia, the trunk and the sky
// scrolling past. LEFT and RIGHT steer, A jumps (hold it for height), START
// pauses (RESUME or QUIT, which ends the round and counts it). A round that
// ends shows its height, the best and the fun earned; the pet is played with
// and the best kept.

#include "menu.h"

// Where the best score lives (the save's best[1]); call once before the game
// is chosen.
void climb_attach(u16* best);

// The game, as the menu calls it (menu.h's MenuGameFn): plays one round and
// leaves the screen black. True: the pet changed.
bool climb_play(Pet* pet, u32 now);

#endif // DEN_CLIMB_H
