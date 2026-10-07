// Stage 1-4, the castle: declarations shared by its files (stage_castle.c,
// art_castle.c, sound_castle.c, boss.c).
//
// A placeholder: a short level that borrows the underground's art, ending at
// an exit, then a placeholder ending. The castle's own art, hazards, the
// dragon (boss.c), its music and the ending replace it
// (docs/examples-roadmap.md).

#ifndef PLATFORMER_STAGE_CASTLE_H
#define PLATFORMER_STAGE_CASTLE_H

#include "game.h"

// Its sprites (castle_sprites[]), IDs from SPR_STAGE(STAGE_CASTLE) on: none
// yet.

// Its sound effects (castle_sounds[]), IDs from SND_STAGE(STAGE_CASTLE) on:
// none yet.

extern const PsgSong castle_song;

// The boss (boss.c), called by the stage's hooks (stage_castle.c).
void boss_start(void);      // the stage (re)starts
void boss_update(void);     // before the movement systems
void boss_after_move(void); // after them

#endif // PLATFORMER_STAGE_CASTLE_H
