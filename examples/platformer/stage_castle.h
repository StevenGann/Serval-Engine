// Stage 1-4, the castle: declarations shared by its files (stage_castle.c,
// art_castle.c, sound_castle.c and boss.c, the dragon).

#ifndef PLATFORMER_STAGE_CASTLE_H
#define PLATFORMER_STAGE_CASTLE_H

#include "game.h"

// Its sprites (castle_sprites[]), IDs from SPR_STAGE(STAGE_CASTLE) on.
enum {
    CS_SALAMANDER,
    CS_SALAMANDER_FLAT,
    CS_FIREBALL,
    CS_EMBER,
    CS_BREATH,
    CS_DRAGON_PART,
    CS_DRAGON
};
#define SPR_SALAMANDER (SPR_STAGE(STAGE_CASTLE) + CS_SALAMANDER)           // walking (animated)
#define SPR_SALAMANDER_FLAT (SPR_STAGE(STAGE_CASTLE) + CS_SALAMANDER_FLAT) // stomped
#define SPR_FIREBALL (SPR_STAGE(STAGE_CASTLE) + CS_FIREBALL) // a fire bar's ball, 8x8: 2 frames
#define SPR_EMBER (SPR_STAGE(STAGE_CASTLE) + CS_EMBER)       // leaping from the lava (animated)
#define SPR_BREATH (SPR_STAGE(STAGE_CASTLE) + CS_BREATH)     // the dragon's fire (animated)
#define SPR_DRAGON_PART (SPR_STAGE(STAGE_CASTLE) + CS_DRAGON_PART) // the dragon's 16x16 pieces
#define SPR_DRAGON (SPR_STAGE(STAGE_CASTLE) + CS_DRAGON) // the dragon: a metasprite, DRAGON_*

// Frames of the dragon (SPR_DRAGON). Faces left.
enum {
    DRAGON_STAND,
    DRAGON_WALK1,
    DRAGON_WALK2,
    DRAGON_CROUCH,
    DRAGON_HOP,
    DRAGON_BREATHE,
    DRAGON_ROAR,
    DRAGON_HURT,
    DRAGON_FRAME_COUNT
};

// Palettes of its sprite group, after the blocks' (PAL_STAGE_BLOCKS). The
// dragon's flash is its own palette mixed toward white (castle_load_palettes).
enum {
    CS_PAL_FIRE = PAL_STAGE_BLOCKS + 1,
    CS_PAL_SALAMANDER,
    CS_PAL_DRAGON,
    CS_PAL_DRAGON_FLASH,
    CS_PALETTE_COUNT
};

// Its metatiles, after the shared ones (art.h).
enum {
    MT_WALL = MT_SHARED_COUNT, // walls and ceilings
    MT_WALL_EDGE,              // a ceiling's lowest row
    MT_LAVA_TOP,               // the lava's surface: only a picture
    MT_LAVA,                   // lava: open, TAG_HAZARD
    MT_LAVA_DEEP,              // lava below that: solid, TAG_HAZARD (embers rest on it)
    MT_BRIDGE,                 // the dragon's bridge: solid, collapses
    MT_LEVER,                  // past the dragon: decoration, pulled by walking into it
    MT_LEVER_PULLED,
    MT_FIREBAR, // a fire bar's block: solid
    MT_TORCH,   // decoration, its flame animated
    MT_CHAIN,   // decoration
    CASTLE_MT_COUNT
};

// Its own components.
#define C_EMBER C_STAGE(0)  // an ember leaping out of the lava (stage_castle.c)
#define C_DRAGON C_STAGE(1) // the dragon (boss.c)
#define C_BREATH C_STAGE(2) // its fireballs

// Its own spawn kinds.
enum { SPAWN_EMBER = SPAWN_STAGE, SPAWN_DRAGON };

// Its sound effects (castle_sounds[]), IDs from SND_STAGE(STAGE_CASTLE) on.
#define SND_ROAR (SND_STAGE(STAGE_CASTLE) + 0)    // the dragon wakes
#define SND_STING (SND_STAGE(STAGE_CASTLE) + 1)   // ...with a few notes of alarm
#define SND_BREATH (SND_STAGE(STAGE_CASTLE) + 2)  // it breathes a fireball
#define SND_HOP (SND_STAGE(STAGE_CASTLE) + 3)     // it lands from a hop
#define SND_LEVER (SND_STAGE(STAGE_CASTLE) + 4)   // the lever is pulled
#define SND_CRUMBLE (SND_STAGE(STAGE_CASTLE) + 5) // a piece of the bridge falls
#define SND_SPLASH (SND_STAGE(STAGE_CASTLE) + 6)  // into the lava
#define SND_LEAP (SND_STAGE(STAGE_CASTLE) + 7)    // an ember leaps

// Animated tiles (tileset_set_tiles): the bonus block's first tile, the
// lava's surface and the torches' flames (two tiles each), and the ending's
// twinkling stars.
#define CASTLE_BONUS_TILE 1
#define LAVA_TILE 5
#define LAVA_FRAMES 4
#define TORCH_TILE 7
#define TORCH_FRAMES 3
#define STAR_TILE 104
#define STAR_TILE_COUNT 8
#define STAR_FRAMES 3
extern const u32 lava_tiles[LAVA_FRAMES][2 * 8];
extern const u32 torch_tiles[TORCH_FRAMES][2 * 8];
extern const u32 star_tiles[STAR_FRAMES][STAR_TILE_COUNT * 8];

// The hall's dark air, and the lava's glow it pulses toward.
#define CASTLE_BACKDROP COLOR_RGB(30, 8, 10)
#define CASTLE_GLOW COLOR_RGB(96, 26, 12)
// The ending's night sky, and where the den's opening is on that screen.
#define NIGHT_SKY COLOR_RGB(16, 22, 56)
#define ENDING_GROUND_Y 128
#define ENDING_DEN_X 150

extern const Tileset castle_tileset;
extern const Metatile castle_metatiles[CASTLE_MT_COUNT];
extern const MapLayer castle_far_layer; // the far colonnade, on background 3
extern const SpriteGroup castle_group;
extern const MapLayer ending_far_layer, ending_near_layer; // the ending's night
extern const PsgSong castle_song, ending_song;
// Fills the tileset's and the sprite group's palettes in RAM: the far wall
// and the dragon's flash mixed with color_mix().
void castle_load_palettes(void);

// The bridge and the lever, found as the level is built (stage_castle.c).
extern int bridge_first_mx, bridge_last_mx, bridge_my;
extern int lever_mx, lever_my;

// The dragon (boss.c), called by the stage's hooks.
void boss_reset(void);              // the stage (re)starts
void boss_spawn(const Spawn* s);    // its spawn comes into view
void boss_update(void);             // before the movement systems
void boss_after_move(void);         // after them
void boss_effects(Color* backdrop); // the lever's flash
bool boss_holds_serval(void);       // the bridge falls: the serval watches

#endif // PLATFORMER_STAGE_CASTLE_H
