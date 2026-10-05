// Shared declarations of the platformer's files: game.c (states, HUD,
// camera), level.c (the level and its blocks), player.c (the serval),
// objects.c (enemies, items, effects) and sound.c.

#ifndef PLATFORMER_GAME_H
#define PLATFORMER_GAME_H

#include "art.h"

// --- Level (level.c) ---------------------------------------------------------

#define TILE 16           // metatile size in pixels
#define LEVEL_W (14 * 16) // in metatiles: 14 screens of 16
#define LEVEL_H 13        // in metatiles: a little taller than the screen
#define LEVEL_PIXEL_W (LEVEL_W * TILE)
#define LEVEL_PIXEL_H (LEVEL_H * TILE)

// What the level places besides metatiles, from left to right.
typedef enum { SPAWN_BEETLE, SPAWN_FROG } SpawnKind;
typedef struct {
    u8 kind;   // SpawnKind
    u8 mx, my; // metatile position
} Spawn;
#define MAX_SPAWNS 32

extern Spawn spawns[MAX_SPAWNS];
extern int spawn_count;
extern int start_mx, start_my;           // where the serval starts...
extern int checkpoint_mx, checkpoint_my; // ...and restarts once past the checkpoint
extern int pole_mx;                      // the goal pole's column
extern int pole_top_my, pole_base_my;    // its knob and its stone base
extern int den_door_x;                   // world x of the den's opening (center)

// Converts the level's text into map cells and the spawn list (once, at boot).
void level_build(void);
// Shows the level: the playfield, the foreground and the parallax layer.
// Also undoes every change to the playfield (broken and used blocks).
void level_show(void);
// A block at metatile (mx, my) was hit from below by the serval.
void level_hit_block(int mx, int my, bool big);
// Collects floating gems touching the rectangle (world pixels).
void level_collect_gems(int x, int y, int w, int h);
// Finishes a block's bounce: puts the metatile it ends as back in the map.
void level_bump_done(int mx, int my, u16 metatile);

// --- Player (player.c) -------------------------------------------------------

typedef enum { PLAYER_NORMAL, PLAYER_GROWING, PLAYER_SHRINKING, PLAYER_DYING } PlayerMode;

extern u32 player;      // the serval's entity slot
extern bool player_big; // grown by a fish
extern PlayerMode player_mode;
extern int player_invulnerable; // frames left blinking after shrinking
extern bool player_hidden;      // inside the den

void player_spawn(int x, int y);            // (x, y): world position of the left end of its feet
void player_control(void);                  // input: before sys_map_movement
void player_after_move(void);               // blocks, gems, pits: after it
void player_auto_walk(void);                // walking to the den
void player_update_mode(void);              // growing, shrinking, dying
void player_update_sprite(bool goal_slide); // picks its frame: before sys_render
void player_grow(void);
void player_hurt(void);
void player_die(bool fell_in_pit);
void player_bounce(void); // after stomping an enemy
bool player_dying_done(void);
int player_center_x(void);

// --- Objects (objects.c) -----------------------------------------------------

void objects_reset(int first_mx); // spawns start at this column
void objects_spawn_ahead(void);   // creates enemies coming into view
void objects_update(void);        // before sys_map_movement
void objects_after_move(void);    // after it: turning, player contact, despawning
void spawn_gem_pop(int mx, int my);
void spawn_fish(int mx, int my);
void spawn_bump(int mx, int my, u8 frame, u16 final_metatile);
void spawn_debris(int mx, int my);
void spawn_sparkle(int x, int y);
void knock_enemies_on(int mx, int my); // enemies on top of a block hit from below

// --- Game (game.c) -----------------------------------------------------------

void game_init(void);
void game_frame(void);
void add_score(int points);
void add_gem(void);
void add_life(void);
void reach_checkpoint(void);
void start_goal(void);

// Camera position in world pixels (follows the serval, never back left).
extern int cam_x, cam_y;

// --- Sound (sound.c) ---------------------------------------------------------

enum {
    SND_JUMP,
    SND_JUMP_BIG,
    SND_GEM,
    SND_BUMP,
    SND_BREAK,
    SND_POWER_APPEARS,
    SND_POWER_UP,
    SND_SHRINK,
    SND_STOMP,
    SND_KICK,
    SND_DEATH,
    SND_ONE_UP,
    SND_POLE,
    SND_TALLY,
    SND_HURRY,
    SND_PAUSE,
    SND_START,
    SND_GAME_OVER,
    SOUND_COUNT
};
extern const PsgSound* const sound_table[SOUND_COUNT];
extern const PsgSong level_song; // the level's tune, looping
extern const PsgSong goal_song;  // a short fanfare after the goal pole, once

// Game components (C_GAME bits): what an entity is.
#define C_PLAYER C_GAME(0)
#define C_ENEMY C_GAME(1)
#define C_FISH C_GAME(2)
#define C_GEM_POP C_GAME(3)
#define C_BUMP C_GAME(4)
#define C_DEBRIS C_GAME(5)
#define C_SPARKLE C_GAME(6)

#define GRAVITY (FX_ONE / 4) // pixels per frame per frame (physics_set_gravity)
#define MAX_FALL 5           // terminal speed in pixels per frame (body_max_fall)

#endif // PLATFORMER_GAME_H
