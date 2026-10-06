// Shared declarations of the shoot-'em-up's files: game.c (states, HUD,
// scrolling, the stage's flow), player.c (the ship, its shots, bombs and
// power-ups), enemies.c (waves, enemies, their bullets, explosions), boss.c,
// stage.c (the scrolling map), art.c (graphics, converted from ASCII art at
// boot), sound.c and scores.c.

#ifndef SHMUP_GAME_H
#define SHMUP_GAME_H

#include "serval/serval.h"

// --- Screen layout -----------------------------------------------------------
//
// The GBA's screen is 240x160, wider than tall, which is the wrong shape for a
// vertical shooter: on the full width, enemies would have far more room to
// come from the sides than from the top, and a HUD over the action would cover
// bullets. So the playfield is the left 176 pixels (11 metatiles), and the
// right 64 pixels are a HUD panel (8 text columns): score, high score, ships,
// bombs and power never cover the action, and the field is a little closer to
// the tall shape these games are designed for.

#define FIELD_W 176
#define FIELD_H SCREEN_H
#define PANEL_COL 22 // the panel's first text column (FIELD_W / 8)

// --- Entities ----------------------------------------------------------------

// Game components (C_GAME bits): what an entity is.
#define C_PLAYER C_GAME(0)
#define C_SHOT C_GAME(1)    // the player's shots
#define C_ENEMY C_GAME(2)   // anything the player's shots can hit (boss parts too)
#define C_EBULLET C_GAME(3) // enemy bullets
#define C_FX C_GAME(4)      // explosions and sparks
#define C_ITEM C_GAME(5)    // power-ups
#define C_GROUND C_GAME(6)  // fixed to the map (turrets); everything else lives on the screen

// Entity budget. The engine has 128 entities and 128 hardware sprites, and
// every entity here has a sprite, so the caps below add up to at most 120
// sprites, leaving room for the 3 drawn by hand (the focus hitbox and two HUD
// icons): 120 + 3 = 123 of 128. A cannon's head is a metasprite of two
// pieces, one more hardware sprite each: with the four a fort shows at once,
// 127.
//   player 1 + shots 20 + enemies 14 + boss parts 3 + enemy bullets 48
//   + explosions and sparks 32 + items 2 = 120
// Each spawn checks its cap (counted once per frame with ecs_count), so the
// engine never runs out of entities. Overload degrades gracefully: past a cap
// the extra shot, bullet or spark is simply not created (a boss spiral gets a
// gap, an explosion fewer sparks), instead of failing somewhere else.
#define MAX_SHOTS 20
#define MAX_ENEMIES 17 // 14 enemies plus the boss's 3 parts, which share the C_ENEMY cap
#define MAX_EBULLETS 48
#define MAX_FX 32
#define MAX_ITEMS 2

// The per-scanline sprite budget: the GBA draws sprites of one scanline in
// 1,210 cycles, an unrotated sprite costing its width (8 for an 8x8 bullet)
// and a rotated one 2 x its doubled width + 10 (74 for a 16x16 spinner). All
// 48 enemy bullets on one line cost 384, so bullets bunching up don't reach
// it; what could is many rotated spinners on one line (waves have at most 4)
// next to the 64-pixel boss. The forts' cannons are two rotated pieces each
// (a 32 x 32 dome and 32 x 16 barrels: 276 cycles a line), at most two on a
// line. Should a line run out, the hardware drops the sprites with the
// highest OAM numbers, which sys_render_by_depth gives to the lowest depth:
// the player's own shots go first, enemy bullets last. In debug builds the
// SELECT readout counts what a line lost (sprite_stats_scanlines).
enum {
    DEPTH_BOSS = 0,
    DEPTH_GROUND,
    DEPTH_SHOT,
    DEPTH_ENEMY,
    DEPTH_FX,
    DEPTH_ITEM,
    DEPTH_PLAYER,
    DEPTH_EBULLET
};

// Two kinds of position. Everything that flies (the ship, enemies, bullets,
// effects, items) lives on the screen: spawn() gives it SPRITE_SCREEN, so
// sys_render_by_depth draws it where it is and the camera climbing the map
// doesn't carry it along. Turrets (C_GROUND) sit on the map, in world
// coordinates, and scroll down with it. center_x/center_y and screen_y give
// screen coordinates for both; only collisions with turrets need the camera
// (enemies.c).

// Per-entity game data, beside the engine's component pools (game.c).
extern u8 kind[MAX_ENT];   // C_ENEMY: EnemyKind; C_ITEM: ItemKind; C_SHOT: damage
extern s16 hp[MAX_ENT];    // C_ENEMY: hit points
extern u16 timer[MAX_ENT]; // frames alive (enemies), or frames left (effects, items)
extern u8 flash[MAX_ENT];  // C_ENEMY: frames left showing the white "hit" palette
extern u8 drops[MAX_ENT];  // C_ENEMY: the item it leaves (ITEM_NONE, ...)

// Creates an entity with a sprite and a w x h hitbox centered on (cx, cy)
// (24.8 pixels: on the screen, or in the world with C_GROUND). Returns its
// slot, or MAX_ENT if none is free.
u32 spawn(u32 components, u16 sprite, int w, int h, FIXED cx, FIXED cy);
FIXED center_x(u32 i); // the hitbox's center on screen
FIXED center_y(u32 i);
int screen_y(u32 i); // the hitbox's top on screen
void destroy_all(u32 components);

// The slots of each kind of entity, gathered once per frame with ecs_gather
// (game.c). Looping over these is much cheaper than ECS_FOR_EACH, which
// visits all 128 slots every time (about 4,000 cycles from ROM, however few
// match): testing each of 20 shots against the enemies with an ECS_FOR_EACH
// per shot took a quarter of the frame. Entities created during the frame
// join the lists next frame; destroyed ones are skipped by testing ent_has()
// before use.
typedef struct {
    u8 slot[MAX_ENT];
    int count;
} SlotList;
extern SlotList shots, enemies, ebullets, items, effects;

// Live entities of each kind: the lists' counts at the start of the frame,
// raised by each spawn (so a frame's spawns respect the caps).
extern int shot_count, enemy_count, ebullet_count, fx_count, item_count;

// --- Scrolling (game.c) -------------------------------------------------------

// The camera's world y: the top of the screen. It moves up one pixel a frame
// until it reaches the top of the stage map, where the boss waits; the map
// and turrets scroll down the screen, flying things stay where they are.
extern int cam_y;

// --- Game state (game.c) -----------------------------------------------------

extern int score;
extern int stage_frame;   // frames since the stage started scrolling
extern bool player_alive; // false while waiting to respawn and after game over
void add_score(int points);

void game_init(void);
void game_frame(void);

// --- Player (player.c) -------------------------------------------------------

extern u32 player; // slot of the ship, valid while player_alive
extern int lives, bombs, power;
extern int invulnerable; // frames left blinking after (re)spawning
extern int bomb_glow;    // frames left of a bomb's white flash
#define MAX_POWER 4
void player_reset(void);  // a new game
void player_spawn(void);  // at the bottom of the field, blinking
void player_update(void); // input, firing, bombs: before sys_movement
void player_draw(void);   // the focus hitbox: after sys_render_by_depth
void player_hit(void);    // touched a bullet or an enemy
bool player_respawn_due(void);
FIXED player_x(void); // the hitbox's center (world pixels)
FIXED player_y(void);
void player_collect(u32 item);

// --- Enemies (enemies.c) -----------------------------------------------------

typedef enum {
    ENEMY_DART,    // small fighter: lines and swoops, an aimed shot
    ENEMY_CARRIER, // orange dart that carries a power-up
    ENEMY_SPINNER, // spinning seed: weaves, three-way shots
    ENEMY_GUNSHIP, // armored: hovers and fires spreads and rings
    ENEMY_TURRET,  // on the station hull: aimed bursts
    ENEMY_CANNON,  // big turret on the hull: turns to track the ship, fires along its barrels
    ENEMY_BOSS,    // the boss's core (boss.c)
    ENEMY_POD,     // the boss's gun pods (boss.c)
} EnemyKind;

typedef enum { ITEM_NONE, ITEM_POWER, ITEM_BOMB } ItemKind;

void enemies_reset(void);
void enemies_spawn_waves(void);  // the stage's wave table, by stage_frame
void spawn_turret(int x, int y); // world pixels, the center (stage.c)
void spawn_cannon(int x, int y); // world pixels, the center of its 2 x 2 emplacement
void enemies_update(void);       // movement patterns and firing: before sys_movement
void enemies_collide(void);      // shots against enemies, the player against danger
void enemies_cull(void);         // removes what has left the field
void enemy_damage(u32 i, int damage);
void explode(FIXED x, FIXED y, int booms, int sparks);
void clear_bullets(bool score_them); // all enemy bullets vanish (a bomb, a respawn)
void spawn_item(FIXED x, FIXED y, ItemKind item);

// Enemy bullets. Angles are u16 turns (math.h); speed in pixels per frame.
bool fire_bullet(FIXED x, FIXED y, u16 angle, FIXED speed, u8 frame);
u16 aim_at_player(FIXED x, FIXED y);
void fire_spread(FIXED x, FIXED y, u16 angle, int count, u16 step, FIXED speed, u8 frame);
void fire_ring(FIXED x, FIXED y, int count, u16 offset, FIXED speed, u8 frame);
#define BULLET_PINK 0 // bullet sprite frames
#define BULLET_BLUE 1

// --- Boss (boss.c) -----------------------------------------------------------

void boss_reset(void);
void boss_start(void);           // enters from the top
void boss_update(void);          // movement and attacks
bool boss_active(void);          // entered and not yet destroyed
bool boss_defeated(void);        // its final explosion is over
void boss_part_destroyed(u32 i); // a pod or the core ran out of hit points
void boss_bomb(void);            // a bomb hits it

// --- Stage (stage.c) ---------------------------------------------------------

extern int stage_start_y; // the camera's y when the stage starts; it ends at 0 (the boss)

void stage_build(void);               // the stage map from its segments (once, at boot)
void stage_show(void);                // loads the layers (the map's turret pads restored)
void stage_hide(void);                // unloads them (the title has only the stars)
void stage_spawn_ground(void);        // creates turrets as their pads scroll into view
void stage_destroy_pad(int x, int y); // a turret's pad becomes a crater
void stage_destroy_gun(int x, int y); // a cannon's emplacement too (its center)
void stage_animate(void);             // the hull's blinking lights (tileset_set_tiles)

// --- Art (art.c) -------------------------------------------------------------

enum {
    SPR_SHIP, // frames: SHIP_FRAME_*
    SPR_SHOT, // frames: SHOT_FRAME_*
    SPR_HITBOX,
    SPR_ICON_SHIP,
    SPR_ICON_BOMB,
    SPR_DART, // 2 frames (animated); the carrier is a dart with PAL_CARRIER
    SPR_SPINNER,
    SPR_GUNSHIP,
    SPR_TURRET,     // eye open, eye shut
    SPR_BULLET,     // BULLET_PINK, BULLET_BLUE
    SPR_SPARK,      // 3 frames, played once (animated)
    SPR_BOOM,       // 5 frames, played once (animated)
    SPR_ITEM_POWER, // glowing on and off (animated)
    SPR_ITEM_BOMB,
    SPR_BOSS,
    SPR_POD,
    SPR_DOME,    // the cannon's pieces...
    SPR_BARRELS, // ...closed, firing
    SPR_CANNON,  // the cannon's head: a metasprite of the two, frames closed and firing
    SPRITE_COUNT
};

// The sprite group's palettes. Each sprite has its own (art.c); the others
// recolor it when drawn with SPRITE_PALETTE: the carrier's orange, the white
// "hit" flash and the boss's red last phase.
enum {
    PAL_PLAYER,
    PAL_ENEMY,
    PAL_CARRIER, // the enemy palette in orange and gold
    PAL_HEAVY,
    PAL_BOSS,
    PAL_RAGE, // the boss's palette in red, for its last phase
    PAL_FIRE,
    PAL_ITEM,
    PAL_FLASH, // every color white: a sprite drawn with it flashes when hit
    PALETTE_COUNT
};
enum { SHIP_FRAME_LEVEL, SHIP_FRAME_LEVEL2, SHIP_FRAME_BANK, SHIP_FRAME_BANK2 };
enum { SHOT_FRAME_BOLT, SHOT_FRAME_HEAVY, SHOT_FRAME_NEEDLE };
#define SPARK_FRAMES 12 // how long a spark's animation lasts
#define BOOM_FRAMES 20  // how long an explosion's animation lasts

// Background tiles and metatiles the stage map uses (art.c builds them).
enum {
    MT_EMPTY,
    MT_ROCK,
    MT_BIG_ROCK, // 2 x 2: MT_BIG_ROCK + 0..3, row by row
    MT_CRYSTAL = MT_BIG_ROCK + 4,
    MT_PAD,
    MT_CRATER,
    MT_HULL_LIGHT,
    MT_GUN,                      // 2 x 2: a cannon's emplacement, MT_GUN + 0..3, row by row
    MT_GUN_CRATER = MT_GUN + 4,  // 2 x 2: what a destroyed cannon leaves
    MT_HULL = MT_GUN_CRATER + 4, // 16 auto-tiled pieces: MT_HULL + neighbors (HULL_UP | ...)
    MT_COUNT = MT_HULL + 16
};
#define HULL_UP 1
#define HULL_DOWN 2
#define HULL_LEFT 4
#define HULL_RIGHT 8

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;
extern const Tileset tileset;
extern Metatile stage_metatiles[MT_COUNT];
extern const MapLayer stars_layer; // background 3, wrapping, half speed
extern const MapLayer panel_layer; // background 1: the HUD panel
void art_build(void);              // converts the ASCII art (once, at boot)
void art_light_frame(u32 frame, const u32** tiles, u16* first); // the hull light's tiles

// --- Sound (sound.c) ---------------------------------------------------------

enum {
    SND_SHOT,
    SND_POP,  // a small enemy explodes
    SND_BOOM, // a big one
    SND_PLAYER_DIE,
    SND_BOMB,
    SND_ITEM,
    SND_POWER_MAX,
    SND_EXTEND,
    SND_WARNING,
    SND_PAUSE,
    SND_START,
    SND_GAME_OVER,
    SND_LETTER,
    SND_MOVE,
    SND_CONFIRM,
    SND_TALLY,
    SOUND_COUNT
};
extern const PsgSound* const sound_table[SOUND_COUNT];
extern const PsgSong stage_song; // "Veldt Run", looping
extern const PsgSong boss_song;  // "Lantern Core", looping
extern const PsgSong clear_song; // a fanfare, once
#define BOSS_TEMPO 168
#define BOSS_RAGE_TEMPO 192

#endif // SHMUP_GAME_H
