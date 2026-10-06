// Shared declarations of the fireflies example: what main.c (the C glue),
// script.c (the bytecode listing), art.c and sound.c all name. In a project
// made with Studio Advance this is the generated header that ties the C glue
// to the script blob: the object, global and asset numbers both sides use.

#ifndef FIREFLIES_GAME_H
#define FIREFLIES_GAME_H

#include "serval/serval.h"

// --- The scripts' objects and globals ------------------------------------------

// Objects of the script blob. Room and Spawner run as threads with no entity
// (main.c starts their Room Start handlers); the others are entities SPAWNed
// by the scripts.
enum {
    OBJ_ROOM,    // the round: HUD, timer, music, the end and the restart request
    OBJ_SPAWNER, // a new firefly every 40-90 frames, at most 8 at once
    OBJ_PLAYER,  // the serval, walked with the D-pad
    OBJ_FIREFLY, // wanders, blinks, fades out after a while; caught on touch
    OBJ_SPARKLE, // the burst where a firefly was caught
    OBJ_RESTING, // the serval sitting down when time is up (no longer catches)
    OBJ_COUNT
};

// Globals (vm_global): the scripts' shared state. C reads G_RESTART only.
enum {
    G_SCORE,     // fireflies caught this round
    G_TIME,      // seconds left
    G_LIVE,      // fireflies alive (their Create and Destroy keep count)
    G_PLAYING,   // 1 while the round runs
    G_RESTART,   // set by the Room when START is pressed after the round
    G_SPAWN_MIN, // the Spawner's delay range, in frames; shrinks every 10 points
    G_SPAWN_MAX, //
    G_PLAYER,    // the serval's entity handle
    G_COUNT
};

// Game components in the objects' masks, so the C glue can find the pairs
// to test for collisions.
#define C_PLAYER C_GAME(0)
#define C_FIREFLY C_GAME(1)

// --- Layout --------------------------------------------------------------------

#define FIELD_TOP 24 // bodies stay below the treeline (physics_set_bounds)
#define ROUND_SECONDS 60
#define MAX_FIREFLIES 8

// --- Graphics (art.c) ----------------------------------------------------------

enum {
    SPR_SERVAL_IDLE,  // 32x32, standing, blinking now and then
    SPR_SERVAL_WALK,  // 32x32, a four-frame walk
    SPR_SERVAL_SIT,   // 32x32, sitting (time is up), blinking
    SPR_FIREFLY,      // 8x8, blinking (FIREFLY_FRAMES frames, looping)
    SPR_FIREFLY_FADE, // 8x8, its glow dissolving (plays once)
    SPR_SPARKLE,      // 16x16, a burst of light (plays once)
    SPRITE_COUNT
};
#define FIREFLY_FRAMES 6

// The serval's body (its catching box) inside its 32x32 frames, and where
// the sprite's origin puts the frame around it.
#define SERVAL_BODY_W 24
#define SERVAL_BODY_H 20

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;
extern const Tileset meadow_tileset;
extern const MapLayer meadow_layer; // background 3: sky, treeline, meadow
extern const MapLayer grass_layer;  // background 1: tall grass in front

// Firefly flight paths, reached by index from the scripts (vm_bind).
enum { PATH_DRIFT, PATH_LOOP, PATH_ZIGZAG, PATH_COUNT };
extern const Path* const paths[PATH_COUNT];

void art_build(void); // draws the art into EWRAM at boot

// --- Sound (sound.c) -------------------------------------------------------------

enum { SND_START, SND_CHIME, SND_JINGLE, SND_TICK, SND_TIME_UP, SOUND_COUNT };
extern const PsgSound* const sound_table[SOUND_COUNT];

enum { SONG_DUSK, SONG_COUNT };
extern const PsgSong* const songs[SONG_COUNT];

// --- The script blob (script.c) ----------------------------------------------------

// Assembles the scripts into an EWRAM buffer. Returns the blob's size and
// sets *blob, or returns 0 and sets *error if the listing is broken.
u32 script_build(const u8** blob, const char** error);

#endif // FIREFLIES_GAME_H
