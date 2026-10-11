// scriptmark: the numbers the script (scriptmark.lua, through
// serval_add_script's HEADERS) and the C harness (bench.c) share.

#ifndef SCRIPTMARK_GAME_H
#define SCRIPTMARK_GAME_H

#include "serval/serval.h"

// Generated at build time by tools/svlua.py and tools/svm.py: OBJ_* (DIRECTOR,
// WANDERER, BOUNCER, FOOD, PUFF), G_* (EATEN, FOOD_COUNT, PUFFS, RESTING) and
// the blob, scriptmark_script and scriptmark_script_size.
#include "scriptmark_script.h"

// Game components in the objects' masks: vm_collide(C_WANDERER, C_FOOD) makes
// the VM test wanderers against food.
#define C_WANDERER C_GAME(0)
#define C_BOUNCER C_GAME(1)
#define C_FOOD C_GAME(2)

// The field the entities move in: positions of their top-left corners.
#define FIELD_L 0
#define FIELD_T 16
#define FIELD_R 232
#define FIELD_B 152

#define WANDERERS 12
#define BOUNCERS 8
#define MAX_FOOD 8
#define MAX_PUFFS 6

// The sprites, 8x8 each (bench.c).
enum { SPR_WANDERER, SPR_BOUNCER, SPR_FOOD, SPR_PUFF, SPRITE_COUNT };

#endif // SCRIPTMARK_GAME_H
