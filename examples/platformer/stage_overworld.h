// Stage 1-1, the overworld: declarations shared by its files
// (stage_overworld.c, art_overworld.c, sound_overworld.c).

#ifndef PLATFORMER_STAGE_OVERWORLD_H
#define PLATFORMER_STAGE_OVERWORLD_H

#include "game.h"

// Its sprites (overworld_sprites[]), IDs from SPR_STAGE(STAGE_OVERWORLD) on.
enum { OW_BEETLE, OW_BEETLE_FLAT, OW_FROG };
#define SPR_BEETLE (SPR_STAGE(STAGE_OVERWORLD) + OW_BEETLE)           // walking (animated)
#define SPR_BEETLE_FLAT (SPR_STAGE(STAGE_OVERWORLD) + OW_BEETLE_FLAT) // stomped
#define SPR_FROG (SPR_STAGE(STAGE_OVERWORLD) + OW_FROG)               // sitting, hopping

// Palettes of its sprite group, after the blocks' (PAL_STAGE_BLOCKS).
enum { OW_PAL_BEETLE = PAL_STAGE_BLOCKS + 1, OW_PAL_FROG, OW_PALETTE_COUNT };

// Its metatiles, after the shared ones (art.h). Pieces wider than one
// metatile continue with MT_X + 1, ...
enum {
    MT_BUSH = MT_SHARED_COUNT,         // 2 x 1
    MT_STUMP_TOP = MT_BUSH + 2,        // 2 x 1
    MT_STUMP = MT_STUMP_TOP + 2,       // 2 x 1
    MT_STUMP_BASE = MT_STUMP + 2,      // 2 x 1
    MT_TALL_GRASS = MT_STUMP_BASE + 2, // 2 x 1
    MT_FLOWERS = MT_TALL_GRASS + 2,    // 2 x 1
    OW_MT_COUNT = MT_FLOWERS + 2
};

// The bonus block's first tile in the tileset.
#define OW_BONUS_TILE 15

extern const Tileset overworld_tileset;
extern const Metatile overworld_metatiles[OW_MT_COUNT];
extern const MapLayer overworld_far_layer; // hills and clouds, on background 3
extern const SpriteGroup overworld_group;
extern const PsgSong overworld_song;

#endif // PLATFORMER_STAGE_OVERWORLD_H
