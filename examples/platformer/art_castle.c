// Stage 1-4, the castle: its art.
//
// A placeholder: the stage borrows the underground's tileset, metatiles,
// parallax layer and sprite group (art_underground.c: its rock, spikes and
// woodlice), so it has no sprites of its own yet. Its own tileset (stone
// halls, lava, the bridge and the lever), metatiles and sprites (the dragon,
// its fireballs) go here, converted from ASCII pixel art as art.c says.

#include "stage_castle.h"

// The stage's sprites, IDs SPR_STAGE(STAGE_CASTLE) + n: none yet.
const SpriteAsset castle_sprites[STAGE_SPRITES];
