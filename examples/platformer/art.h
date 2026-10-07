// Graphics every stage shares (art.c): the serval, gems, the fish, the goal
// banner, sparkles, HUD icons, bouncing blocks and brick debris, and the
// sprite table. Also the numbers every stage's art (art_<name>.c) follows:
// sprite IDs, palette slots, shared metatiles and tags.

#ifndef PLATFORMER_ART_H
#define PLATFORMER_ART_H

#include "serval/serval.h"

// The stages, in the order they are played.
enum { STAGE_OVERWORLD, STAGE_UNDERGROUND, STAGE_TREETOPS, STAGE_CASTLE, STAGE_COUNT };

// --- Sprites -----------------------------------------------------------------

// Sprite IDs (the sprite table's indices). The ones every stage uses come
// first, loaded once (global_group); then each stage has STAGE_SPRITES IDs of
// its own, from SPR_STAGE(stage) on, for its <name>_sprites[] (art_<name>.c),
// loaded with the stage. The sprite table holds them all.
enum {
    SPR_SERVAL_SMALL, // frames: SERVAL_STAND ... SERVAL_DEAD
    SPR_SERVAL_BIG,   // frames: SERVAL_STAND ... SERVAL_SKID
    SPR_GEM,          // spinning (animated: frame_order, one frame mirrored)
    SPR_FISH,
    SPR_BANNER,
    SPR_SPARKLE, // 8x8, twinkling (animated)
    SPR_HUD_GEM, // 8x8 HUD icons
    SPR_HUD_SERVAL,
    SPR_HUD_CLOCK,
    SPR_GLOBAL_COUNT,
    // In every stage's group rather than the global one: their colors are
    // the stage's (its palette slot PAL_STAGE_BLOCKS).
    SPR_BLOCK = SPR_GLOBAL_COUNT, // a bouncing block: BLOCK_FRAME_*
    SPR_DEBRIS,                   // 8x8, tumbling (animated: frame_order, flipped)
    SPR_SHARED_COUNT
};
#define STAGE_SPRITES 24
#define SPR_STAGE(stage) (SPR_SHARED_COUNT + (stage) * STAGE_SPRITES)
#define SPRITE_COUNT SPR_STAGE(STAGE_COUNT)

// Frames of the serval sprites.
enum { SERVAL_STAND, SERVAL_RUN1, SERVAL_RUN2, SERVAL_JUMP, SERVAL_SKID, SERVAL_DEAD };
enum { BLOCK_FRAME_BONUS, BLOCK_FRAME_USED, BLOCK_FRAME_BRICK };

// Palettes of the global group.
enum { PAL_SERVAL, PAL_GEM, PAL_FISH, PALETTE_COUNT };
// A stage's group starts with SPR_BLOCK and SPR_DEBRIS, and its palette slot 0
// is their colors: the stage's bricks (colors 2-5, as in the bricks of its
// tileset), and the gold bonus block and brown used block (colors 1 and 6-15,
// as in the overworld's; art_overworld.c). The stage's own sprites use slots
// 1 and up: 16 palette banks, less the global group's 3 and this one, leave
// 12.
#define PAL_STAGE_BLOCKS 0

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup global_group;

// Each stage's own sprites, in its block of IDs (art_<name>.c).
extern const SpriteAsset overworld_sprites[STAGE_SPRITES];
extern const SpriteAsset underground_sprites[STAGE_SPRITES];
extern const SpriteAsset treetops_sprites[STAGE_SPRITES];
extern const SpriteAsset castle_sprites[STAGE_SPRITES];

// --- Backgrounds -------------------------------------------------------------

// Metatile tags (Metatile.collision): what a block does when hit or touched.
// A bonus block with a fish is told apart by its number (MT_BONUS_FISH), so
// a tag is free for hazards.
#define TAG_BONUS MAP_TAG(0)  // gives a gem when hit from below (then used)
#define TAG_BRICK MAP_TAG(1)  // breaks when a big serval hits it (with TAG_BONUS: gems)
#define TAG_GEM MAP_TAG(2)    // a gem in the air, collected by touching it
#define TAG_HAZARD MAP_TAG(3) // hurts the serval touching it (the stage's hazard hook)

// Metatiles every stage's table starts with, at the same numbers, so the
// level's legend (level.c) and the blocks work in every stage; each stage
// draws them its own way (its art_<name>.c). Its own metatiles follow, from
// MT_SHARED_COUNT on.
enum {
    MT_EMPTY,
    MT_GROUND_TOP, // the floor's surface
    MT_GROUND,     // below it
    MT_STONE,      // a hard block: staircases, pillars
    MT_BRICK,      // TAG_BRICK
    MT_GEM_BRICK,  // looks like MT_BRICK; TAG_BRICK | TAG_BONUS: holds gems
    MT_BONUS,      // TAG_BONUS: a gem, then MT_USED
    MT_BONUS_FISH, // looks like MT_BONUS; TAG_BONUS: a fish, then MT_USED
    MT_USED,
    MT_HIDDEN, // solid, drawn by a bouncing block's sprite while it bounces
    MT_GEM,    // TAG_GEM
    MT_ONEWAY, // 2 x 1: a one-way platform (MAP_ONEWAY)
    MT_POLE_TOP = MT_ONEWAY + 2,
    MT_POLE,
    MT_EXIT, // 4 x 3, row by row: the den, or the stage's own exit
    MT_SHARED_COUNT = MT_EXIT + 12
};

// The bonus block's glint: its four tiles (from the stage's bonus_tile on,
// game.h) are replaced by these, frame by frame, the last one the plain
// block (tileset_set_tiles). Colors 1 and 6-9 of the block's palette bank.
#define BONUS_TILE_COUNT 4
#define BONUS_GLINT_FRAMES 5
extern const u32 bonus_glint_tiles[BONUS_GLINT_FRAMES][BONUS_TILE_COUNT * 8];

#endif // PLATFORMER_ART_H
