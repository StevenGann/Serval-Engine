// Shared declarations of the platformer's files. The frame every stage runs
// in: game.c (states, HUD, camera, goal, stage select and saving), level.c
// (building a stage's level, its blocks and hazards), player.c (the serval),
// objects.c (enemies, items, effects), art.c and sound.c (what every stage
// shares). Each stage is a module of its own files: stage_<name>.c (its level
// and hooks), art_<name>.c, sound_<name>.c and stage_<name>.h.

#ifndef PLATFORMER_GAME_H
#define PLATFORMER_GAME_H

#include "art.h"

#define TILE 16 // metatile size in pixels

// --- Stages ------------------------------------------------------------------

// What the level places besides metatiles, from left to right. The kinds
// below are the frame's (objects.c); a stage's own start at SPAWN_STAGE, and
// its spawn hook creates them.
typedef enum { SPAWN_WALKER, SPAWN_HOPPER, SPAWN_STAGE } SpawnKind;
typedef struct {
    u16 mx;  // metatile position
    u8 my;   //
    u8 kind; // SpawnKind, or SPAWN_STAGE + the stage's own number
} Spawn;

// A stage: its level, its art and music, and hooks for what it does beyond
// what every stage does. stage_<name>.c defines one; game.c plays them in
// order (stages[]). The hooks are optional (NULL: nothing to do).
typedef struct {
    const char* name; // "1-1": the stage card and the HUD show "STAGE 1-1"

    // The level, written as text: `screens` screens of 16 columns by
    // `height` rows (at most LEVEL_MAX_CELLS cells in all), one row after
    // another, screen after screen. The characters are level.c's legend plus
    // the stage's own (cell below).
    const char (*text)[16 + 1];
    u8 screens, height;
    u16 time; // the countdown's start

    // Art (art_<name>.c). The stage's tileset, and the metatiles of its
    // playfield and foreground: the shared ones (MT_EMPTY ... art.h) first.
    const Tileset* tileset;
    const Metatile* metatiles;
    u16 metatile_count;
    u16 bonus_tile;            // the bonus block's first tile in the tileset (its glint)
    const MapLayer* far_layer; // background 3 (parallax), or NULL for none
    Color backdrop;            // what shows where no layer draws
    // Its sprite group: SPR_BLOCK, SPR_DEBRIS and its own sprites, loaded
    // after the global group's mark when the stage starts.
    const SpriteGroup* sprites;
    u16 walker_sprite, walker_flat_sprite; // for 'e' (SPAWN_WALKER): walking, stomped
    u16 hopper_sprite;                     // for 'r' (SPAWN_HOPPER): sitting, hopping

    // Music (sound_<name>.c): looping, sped up to hurry_tempo when time runs low.
    const PsgSong* song;
    u16 hurry_tempo;

    // The stage is about to load (on the black stage card, or behind the
    // title), before its tileset and sprite group: builds data its art
    // needs at run time, e.g. palettes mixed with color_mix() into RAM that
    // its tileset points at.
    void (*load)(void);
    // Turns a character of the level text that level.c's legend doesn't
    // know into a metatile of the playfield (returned) and of the foreground
    // (*front, MT_EMPTY unless set). It may also add spawns of the stage's
    // own kinds (level_add_spawn). Called for every cell when the stage is
    // built, column by column from the left.
    u16 (*cell)(char c, int mx, int my, u16* front);
    // The stage (re)starts: its map is shown and the serval placed (also at
    // a restart after losing a life). Resets the stage's own state, creates
    // what is there from the start (a boss).
    void (*start)(void);
    // Creates the object for a spawn of the stage's own kind as it comes into
    // view (within a metatile of the screen's right edge).
    void (*spawn)(const Spawn* s);
    // Every frame of play: before the movement systems (set velocities) and
    // after them (contacts, despawning). Not while the serval grows,
    // shrinks or dies, nor during the goal.
    void (*update)(void);
    void (*after_move)(void);
    // The serval touches a metatile tagged TAG_HAZARD. NULL: player_hurt().
    void (*hazard)(void);
    // Every frame the stage is on screen (playing, dying, at the goal; not
    // paused nor on the title), before drawing: screen-wide effects, e.g. the
    // backdrop's color pulsing with color_mix() (screen_set_backdrop).
    void (*effects)(void);
    // Draws what sys_render doesn't, after it (so behind the entities):
    // sprite_draw*() at world position minus (cam_x, cam_y).
    void (*draw)(void);
    // The last stage only: the ending, after its "STAGE CLEAR!". Starts on
    // a black screen (text cleared, no map, no entities); ending_update runs
    // every frame and returns true when done (or START is pressed), and the
    // game fades back to the title. NULL: straight to the title.
    void (*ending_start)(void);
    bool (*ending_update)(void);
} StageDef;

extern const StageDef stage_overworld, stage_underground, stage_treetops, stage_castle;
extern const StageDef* stage; // the stage being played (or shown on the title)

// --- Level (level.c) ---------------------------------------------------------

#define LEVEL_MAX_CELLS (16 * 16 * 16) // e.g. 16 screens of 16 rows
#define MAX_SPAWNS 48

extern int level_w, level_h;             // the stage's level in metatiles...
extern int level_pixel_w, level_pixel_h; // ...and in pixels
extern Spawn spawns[MAX_SPAWNS];
extern int spawn_count;
extern int start_mx, start_my;           // where the serval starts...
extern int checkpoint_mx, checkpoint_my; // ...and restarts once past the checkpoint
extern int pole_mx;                      // the goal pole's column, or -1 without one
extern int pole_top_my, pole_base_my;    // its knob and its stone base
extern int exit_x;                       // world x of the exit (the den)'s left edge...
extern int den_door_x;                   // ...and of its opening (center)

// Converts the stage's level text into map cells and the spawn list, in RAM.
void level_build(void);
// Shows the level: the playfield, the foreground and the parallax layer.
// Also undoes every change to the playfield (broken and used blocks).
void level_show(void);
// For a stage's cell hook, while the level is built: adds a spawn; the
// character at (mx, my) ('.' outside the level); how many cells left of or
// above it hold the same character (for pieces wider or taller than a
// metatile: which half, or which column, this is).
void level_add_spawn(int kind, int mx, int my);
char level_text_at(int mx, int my);
int level_run_left(int mx, int my);
int level_run_up(int mx, int my);
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
void player_after_move(void);               // blocks, gems, hazards, pits: after it
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

// Per-entity data of the game's objects, beside the engine's component
// pools; stages use them for their own objects too.
extern u8 obj_kind[MAX_ENT];   // C_ENEMY: SpawnKind
extern s8 obj_dir[MAX_ENT];    // -1 left, 1 right
extern s16 obj_timer[MAX_ENT]; // frames: until a frog hops, an effect ends...

void objects_reset(int first_mx); // spawns start at this column
void objects_spawn_ahead(void);   // creates enemies coming into view
void objects_update(void);        // before sys_map_movement
void objects_after_move(void);    // after it: turning, player contact, despawning
// Creates an entity with C_POS | C_SPR | components at world (x, y);
// returns its slot, or MAX_ENT if the pool is full.
u32 object_create(u32 components, u16 sprite, int x, int y);
// Knocks an enemy (C_ENEMY) out, as a block hit from below does.
void enemy_knock(u32 i);
// Stomps an enemy: a walker goes flat, any other kind vanishes in a sparkle.
void enemy_squash(u32 i);
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
void start_goal(void);      // the serval caught the goal pole
void goal_start_exit(void); // the serval walks into the exit, ending the stage

// Camera position in world pixels (follows the serval, never back left).
extern int cam_x, cam_y;

// --- Sound (sound.c) ---------------------------------------------------------

// Sound effect IDs: the shared ones, then STAGE_SOUNDS for each stage from
// SND_STAGE(stage) on, its <name>_sounds[] (sound_<name>.c). One table holds
// them all (psg_table_set, once).
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
    SOUND_SHARED_COUNT
};
#define STAGE_SOUNDS 6
#define SND_STAGE(stage) (SOUND_SHARED_COUNT + (stage) * STAGE_SOUNDS)
#define SOUND_COUNT SND_STAGE(STAGE_COUNT)

extern const PsgSound* const sound_table[SOUND_COUNT];
extern const PsgSong goal_song; // a short fanfare after the goal, once
extern const PsgSound overworld_sounds[STAGE_SOUNDS];
extern const PsgSound underground_sounds[STAGE_SOUNDS];
extern const PsgSound treetops_sounds[STAGE_SOUNDS];
extern const PsgSound castle_sounds[STAGE_SOUNDS];

// Sounds whose priority is at least this are jingles: the short effects wait
// until they end.
#define JINGLE_PRIORITY 1

// --- Entities ----------------------------------------------------------------

// Game components (C_GAME bits): what an entity is. C_STAGE(0) to C_STAGE(5)
// are the stage's own: only one stage plays at a time, so they share the bits.
#define C_PLAYER C_GAME(0)
#define C_ENEMY C_GAME(1)
#define C_FISH C_GAME(2)
#define C_GEM_POP C_GAME(3)
#define C_BUMP C_GAME(4)
#define C_DEBRIS C_GAME(5)
#define C_SPARKLE C_GAME(6)
#define C_SQUASHED C_GAME(7) // a stomped enemy, flat for a moment
#define C_KNOCKED C_GAME(8)  // an enemy knocked out from below, on its back
#define C_STAGE(n) C_GAME(9 + (n))

#define GRAVITY (FX_ONE / 4) // pixels per frame per frame (physics_set_gravity)
#define MAX_FALL FX(5)       // terminal speed in pixels per frame (body_max_fall)

#endif // PLATFORMER_GAME_H
