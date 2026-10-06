// Shared declarations of the breakout example's files: game.c (screens,
// states, HUD, fades), play.c (paddle, balls, bricks, power-ups), levels.c
// (brick layouts and backgrounds), scores.c (the saved table), art.c and
// sound.c.

#ifndef BREAKOUT_GAME_H
#define BREAKOUT_GAME_H

#include "serval/serval.h"

// --- The field ---------------------------------------------------------------
//
// The screen's top row is the HUD; under it a pipe frames the field on three
// sides (background 2). Balls bounce inside the frame and are lost below the
// bottom of the screen.

#define FIELD_LEFT 8
#define FIELD_RIGHT (SCREEN_W - 8)
#define FIELD_TOP 16
#define BRICK_W 16
#define BRICK_H 8
#define BRICK_COLS 14 // (FIELD_RIGHT - FIELD_LEFT) / BRICK_W
#define BRICK_ROWS 10
#define BRICKS_TOP 32 // the first brick row's y
#define PADDLE_Y 144  // the paddle's top
#define PADDLE_H 8
#define BALL_SIZE 6

// --- Art (art.c) -------------------------------------------------------------

enum {
    SPR_BRICK, // frames BRICK_FRAME_*; each color is a palette (SPRITE_PALETTE(PAL_*))
    SPR_BALL,
    SPR_PADDLE_LEFT, // the paddle's 16x8 pieces: left end, middle (wide paddle only), right end
    SPR_PADDLE_MIDDLE,
    SPR_PADDLE_RIGHT,
    SPR_PADDLE,       // metasprites of those pieces, drawn as one from the paddle's top-left:
    SPR_PADDLE_WIDE,  // two ends (32 pixels), and with the middle between them (48)
    SPR_CAPSULE_WIDE, // falling power-ups: one per PowerKind, in order
    SPR_CAPSULE_MULTI,
    SPR_CAPSULE_SLOW,
    SPR_CAPSULE_CATCH,
    SPR_CAPSULE_LIFE,
    SPR_BURST, // a broken brick's dust (plays once)
    SPR_PAW,   // a life on the HUD
    SPRITE_COUNT
};
enum { BRICK_FRAME_PLAIN, BRICK_FRAME_SILVER, BRICK_FRAME_CRACKED, BRICK_FRAME_GOLD };

// The sprite group's palettes. A sprite is drawn with its own unless its
// flags pick another with SPRITE_PALETTE(n): one brick sprite takes every
// brick color, PAL_FLASH (white) for a few frames after a hit that didn't
// break it, and PAL_CATCH makes the paddle glow while it catches balls.
enum {
    PAL_RED,
    PAL_ORANGE,
    PAL_YELLOW,
    PAL_GREEN,
    PAL_BLUE,
    PAL_PURPLE,
    PAL_SILVER,
    PAL_GOLD,
    PAL_FLASH,
    PAL_BALL,
    PAL_PADDLE,
    PAL_CATCH,
    PALETTE_COUNT
};
#define BURST_LAST_FRAME 3 // blank: the dust is gone

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;

// Background: one tileset; the frame on background 2, each level's pattern
// (serval spots in its own colors) on background 3.
extern const Tileset tileset;
extern const MapLayer frame_layer;
#define LEVEL_STYLES 4
extern const MapLayer pattern_layers[LEVEL_STYLES];

// --- Levels (levels.c) -------------------------------------------------------

// Brick kinds; a level's text names them with one character each.
typedef enum {
    BRICK_NONE,
    BRICK_RED,
    BRICK_ORANGE,
    BRICK_YELLOW,
    BRICK_GREEN,
    BRICK_BLUE,
    BRICK_PURPLE,
    BRICK_SILVER, // two hits
    BRICK_GOLD,   // unbreakable
} BrickKind;

typedef struct {
    const char* name;
    const char* rows[BRICK_ROWS]; // BRICK_COLS characters each; NULL ends the layout early
} Level;

#define LEVEL_COUNT 4
extern const Level levels[LEVEL_COUNT];
BrickKind brick_kind_of(char c);

// --- Play (play.c) -----------------------------------------------------------

typedef enum {
    POWER_WIDE,
    POWER_MULTI,
    POWER_SLOW,
    POWER_CATCH,
    POWER_LIFE,
    POWER_COUNT
} PowerKind;

// What happened during a frame of play, for game.c.
typedef enum { PLAY_ON, PLAY_BALL_LOST, PLAY_CLEARED } PlayResult;

void play_start_level(int level); // creates the bricks, the paddle and a ball on it
void play_serve(void);            // after a lost ball: paddle reset, a new ball on it
PlayResult play_update(void);     // one frame of play (A launches a ball held by the paddle)
void play_draw(void);             // every entity, and the paddle (drawn by hand)
bool play_holding_ball(void);     // a ball waits on the paddle

// --- Game (game.c) -----------------------------------------------------------

void game_init(void);
void game_frame(void);
void add_score(int points);
void add_life(void);

// --- High scores (scores.c) --------------------------------------------------

#define SCORES_COUNT 5
void scores_load(void);
void scores_reset(void);
int scores_add(int score, int level); // its place (0 = best) once saved, or -1
int scores_best(void);
void scores_draw(int first_row, int highlight); // highlight: the place to show in yellow, or -1

// --- Sound (sound.c) ---------------------------------------------------------

enum {
    SND_PADDLE,
    SND_WALL,
    SND_BRICK_RED, // one pitch per brick color, low to high
    SND_BRICK_ORANGE,
    SND_BRICK_YELLOW,
    SND_BRICK_GREEN,
    SND_BRICK_BLUE,
    SND_BRICK_PURPLE,
    SND_CRACK, // a silver brick's first hit
    SND_CLANK, // a gold brick
    SND_LAUNCH,
    SND_CATCH,
    SND_POWER_UP,
    SND_LIFE_UP,
    SND_BALL_LOST,
    SND_START,
    SND_GAME_OVER,
    SND_PAUSE,
    SND_RESET,
    SOUND_COUNT
};
extern const PsgSound* const sound_table[SOUND_COUNT];
extern const PsgSong level_song; // the levels' tune, looping
extern const PsgSong clear_song; // a short fanfare when a level is cleared, once

// --- Game components (C_GAME bits) -------------------------------------------

#define C_BRICK C_GAME(0)
#define C_BREAKABLE C_GAME(1) // bricks that count toward clearing the level
#define C_BALL C_GAME(2)
#define C_POWER C_GAME(3) // a falling capsule
#define C_BURST C_GAME(4)

#endif // BREAKOUT_GAME_H
