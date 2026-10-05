// Graphics of the platformer: sprites, the tileset, metatiles and the
// parallax layer (art.c, converted from ASCII pixel art).

#ifndef PLATFORMER_ART_H
#define PLATFORMER_ART_H

#include "serval/serval.h"

// Sprite IDs (the sprite table's indices).
enum {
    SPR_SERVAL_SMALL, // frames: SERVAL_STAND ... SERVAL_DEAD
    SPR_SERVAL_BIG,   // frames: SERVAL_STAND ... SERVAL_SKID
    SPR_BEETLE,       // walking (animated: sys_animate)
    SPR_BEETLE_FLAT,  // stomped
    SPR_FROG,         // sitting, hopping
    SPR_GEM,          // spinning (animated: frame_order, one frame mirrored)
    SPR_FISH,
    SPR_BANNER,
    SPR_SPARKLE, // 8x8, twinkling (animated)
    SPR_HUD_GEM, // 8x8 HUD icons
    SPR_HUD_SERVAL,
    SPR_HUD_CLOCK,
    SPR_BLOCK,  // a bouncing block: BLOCK_FRAME_*
    SPR_DEBRIS, // 8x8, tumbling (animated: frame_order, one frame flipped)
    SPRITE_COUNT
};

// Frames of the serval sprites.
enum { SERVAL_STAND, SERVAL_RUN1, SERVAL_RUN2, SERVAL_JUMP, SERVAL_SKID, SERVAL_DEAD };
enum { BLOCK_FRAME_BONUS, BLOCK_FRAME_USED, BLOCK_FRAME_BRICK };

// Sprite palettes (palette slots of the sprite group).
enum { PAL_SERVAL, PAL_BEETLE, PAL_FROG, PAL_GEM, PAL_FISH, PAL_BLOCKS, PALETTE_COUNT };

// Background palette banks.
enum { BANK_TERRAIN, BANK_BLOCKS, BANK_GOAL, BANK_FAR, BANK_GEM, BANK_COUNT };

// Metatile tags (Metatile.collision): what a block does when hit or touched.
#define TAG_BONUS MAP_TAG(0) // gives a gem when hit from below (then used)...
#define TAG_POWER MAP_TAG(1) // ...or, with this tag, a fish
#define TAG_BRICK MAP_TAG(2) // breaks when a big serval hits it (with TAG_BONUS: gems)
#define TAG_GEM MAP_TAG(3)   // a gem in the air, collected by touching it

// Metatiles of the playfield (background 2) and foreground (background 1).
// Pieces wider than one metatile continue with MT_X + 1, ...
enum {
    MT_EMPTY,
    MT_GROUND_TOP,
    MT_GROUND,
    MT_STONE,
    MT_BRICK,
    MT_GEM_BRICK,
    MT_BONUS,
    MT_BONUS_FISH,
    MT_USED,
    MT_HIDDEN,
    MT_GEM,
    MT_BUSH,                      // 2 x 1
    MT_STUMP_TOP = MT_BUSH + 2,   // 2 x 1
    MT_STUMP = MT_STUMP_TOP + 2,  // 2 x 1
    MT_STUMP_BASE = MT_STUMP + 2, // 2 x 1
    MT_POLE_TOP = MT_STUMP_BASE + 2,
    MT_POLE,
    MT_DEN,                         // 4 x 3, row by row
    MT_TALL_GRASS = MT_DEN + 12,    // 2 x 1
    MT_FLOWERS = MT_TALL_GRASS + 2, // 2 x 1
    MT_COUNT = MT_FLOWERS + 2
};

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;
extern const Tileset tileset;

// The bonus block's tiles in the tileset, and the frames of its glint, the
// last one the plain block (for tileset_set_tiles).
#define TILE_BONUS 15
#define BONUS_TILE_COUNT 4
#define BONUS_GLINT_FRAMES 5
extern const u32 bonus_glint_tiles[BONUS_GLINT_FRAMES][BONUS_TILE_COUNT * 8];
extern const Metatile metatiles[MT_COUNT];
extern const MapLayer far_layer; // the parallax layer, on background 3

#endif // PLATFORMER_ART_H
