// Stage 1-2, the underground: declarations shared by its files
// (stage_underground.c, art_underground.c, sound_underground.c).

#ifndef PLATFORMER_STAGE_UNDERGROUND_H
#define PLATFORMER_STAGE_UNDERGROUND_H

#include "game.h"

// Its sprites (underground_sprites[]), IDs from SPR_STAGE(STAGE_UNDERGROUND) on.
enum { UG_LOUSE, UG_LOUSE_BALL, UG_BAT_HANG, UG_BAT_FLY };
#define SPR_LOUSE (SPR_STAGE(STAGE_UNDERGROUND) + UG_LOUSE)           // walking (animated)
#define SPR_LOUSE_BALL (SPR_STAGE(STAGE_UNDERGROUND) + UG_LOUSE_BALL) // stomped
#define SPR_BAT_HANG (SPR_STAGE(STAGE_UNDERGROUND) + UG_BAT_HANG)     // asleep
#define SPR_BAT_FLY (SPR_STAGE(STAGE_UNDERGROUND) + UG_BAT_FLY)       // flapping (animated)

// Palettes of its sprite group, after the blocks' (PAL_STAGE_BLOCKS).
enum { UG_PAL_LOUSE = PAL_STAGE_BLOCKS + 1, UG_PAL_BAT, UG_PALETTE_COUNT };

// Its metatiles, after the shared ones (art.h).
enum {
    MT_CEILING = MT_SHARED_COUNT, // rock
    MT_CEILING_EDGE,              // its lowest row, ragged
    MT_SPIKES,                    // crystal spikes: solid, TAG_HAZARD
    MT_STALACTITES,               // decoration under the ceiling
    MT_MUSHROOMS,                 // glowing mushrooms in front of the sprites
    UG_MT_COUNT
};

// Its own spawn kinds.
enum { SPAWN_BAT = SPAWN_STAGE };

// Its sound effects (underground_sounds[]), IDs from SND_STAGE(STAGE_UNDERGROUND) on.
#define SND_BAT (SND_STAGE(STAGE_UNDERGROUND) + 0) // a bat wakes: a squeak

// The bonus block's first tile in the tileset.
#define UG_BONUS_TILE 1
// The cave's dark air, and the faint glow it pulses toward.
#define UG_BACKDROP COLOR_RGB(10, 10, 26)
#define UG_GLOW COLOR_RGB(44, 30, 92)

extern const Tileset underground_tileset;
extern const Metatile underground_metatiles[UG_MT_COUNT];
extern const MapLayer underground_far_layer; // columns of rock, on background 3
extern const SpriteGroup underground_group;
extern const PsgSong underground_song;
// Fills the tileset's palettes in RAM, the far rock dimmed with color_mix().
void underground_load_palettes(void);

#endif // PLATFORMER_STAGE_UNDERGROUND_H
