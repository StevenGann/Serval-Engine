// Stage 1-3, the treetops: declarations shared by its files
// (stage_treetops.c, art_treetops.c, sound_treetops.c).

#ifndef PLATFORMER_STAGE_TREETOPS_H
#define PLATFORMER_STAGE_TREETOPS_H

#include "game.h"

// Its sprites (treetops_sprites[]), IDs from SPR_STAGE(STAGE_TREETOPS) on.
enum { TT_CATERPILLAR, TT_CATERPILLAR_FLAT, TT_TREE_FROG, TT_BIRD, TT_BURR };
#define SPR_CATERPILLAR (SPR_STAGE(STAGE_TREETOPS) + TT_CATERPILLAR)           // walking (animated)
#define SPR_CATERPILLAR_FLAT (SPR_STAGE(STAGE_TREETOPS) + TT_CATERPILLAR_FLAT) // stomped
#define SPR_TREE_FROG (SPR_STAGE(STAGE_TREETOPS) + TT_TREE_FROG)               // sitting, hopping
#define SPR_BIRD (SPR_STAGE(STAGE_TREETOPS) + TT_BIRD) // flapping (animated)
#define SPR_BURR (SPR_STAGE(STAGE_TREETOPS) + TT_BURR) // spinning (animated)

// Palettes of its sprite group, after the blocks' (PAL_STAGE_BLOCKS).
enum {
    TT_PAL_CATERPILLAR = PAL_STAGE_BLOCKS + 1,
    TT_PAL_FROG,
    TT_PAL_BIRD,
    TT_PAL_BURR,
    TT_PALETTE_COUNT
};

// Its metatiles, after the shared ones (art.h). A canopy is MT_CANOPY_LEFT,
// then MT_ONEWAY and MT_ONEWAY + 1 in turn, then MT_CANOPY_RIGHT, all one-way.
// Pieces wider than one metatile continue with MT_X + 1, ...
enum {
    MT_CANOPY_LEFT = MT_SHARED_COUNT,
    MT_CANOPY_RIGHT,
    MT_TRUNK,                          // 2 x 1, not solid
    MT_TRUNK_TOP = MT_TRUNK + 2,       // 2 x 1: a trunk's top, branching under its canopy
    MT_GREAT_TRUNK = MT_TRUNK_TOP + 2, // 4 x 1: the great tree's trunk, above the exit
    MT_FRONDS = MT_GREAT_TRUNK + 4,    // leaves in front of the sprites
    TT_MT_COUNT
};

// Its own spawn kinds.
enum { SPAWN_BIRD = SPAWN_STAGE, SPAWN_BURRS };

// Its sound effects (treetops_sounds[]), IDs from SND_STAGE(STAGE_TREETOPS) on.
#define SND_BIRD (SND_STAGE(STAGE_TREETOPS) + 0) // a bird comes into view: a chirp
#define SND_BURR (SND_STAGE(STAGE_TREETOPS) + 1) // a burr drops from the leaves: a rustle

// The bonus block's first tile in the tileset.
#define TT_BONUS_TILE 1
// The daytime sky.
#define TT_BACKDROP COLOR_RGB(130, 196, 246)

extern const Tileset treetops_tileset;
extern const Metatile treetops_metatiles[TT_MT_COUNT];
extern const MapLayer treetops_far_layer; // clouds and the forest far below, on background 3
extern const SpriteGroup treetops_group;
extern const PsgSong treetops_song;

#endif // PLATFORMER_STAGE_TREETOPS_H
