// The castle's boss (stage 1-4): the slot for the dragon.
//
// A placeholder: there is no boss yet. stage_castle.c's hooks call these, so
// the dragon (a metasprite, its fireballs, the bridge it stands on and the
// lever past it) can live here, in the castle's own files, using the frame's
// helpers (game.h): object_create(), the stage's components C_STAGE(0) to
// C_STAGE(5), player_hurt(), camera_stop_x to hold the camera at its arena,
// map_set_cell() for the bridge, and goal_start_exit() once it has fallen.

#include "stage_castle.h"

void boss_start(void) {}

void boss_update(void) {}

void boss_after_move(void) {}
