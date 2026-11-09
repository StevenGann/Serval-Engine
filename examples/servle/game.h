// Shared declarations of the servle example: what main.c (the C glue),
// servle.lua (the game's rules in the Lua subset, through serval_add_script's
// HEADERS), art.c and sound.c all name. In a project made with Studio Advance
// this is the generated header that ties the C glue to the script blob: the
// asset numbers both sides use, plus the object and global numbers the tools
// write into servle_script.h from the script.

#ifndef SERVLE_GAME_H
#define SERVLE_GAME_H

#include "serval/serval.h"

// --- The script blob (servle.lua) -----------------------------------------------

// Generated at build time by tools/svlua.py and tools/svm.py: OBJ_* (GAME,
// TILE and the script's six other objects), G_* (ANSWER, ROW, COL, STATE and
// its other globals), their counts, and the blob itself: servle_script and
// servle_script_size. Game runs as a thread with no entity (main.c starts its
// room_start handler); the script spawns every other object's entities.
#include "servle_script.h"

// --- Marks ---------------------------------------------------------------------

// What a letter of an entered guess is: also the tile's palette (PAL_* below),
// and the text style the alphabet strip shows the letter in.
#define MARK_NONE 0    // not marked yet (a typed or empty tile; a letter not tried)
#define MARK_ABSENT 1  // not in the word
#define MARK_PRESENT 2 // in the word, another place
#define MARK_CORRECT 3 // right letter, right place

// --- Layout ----------------------------------------------------------------------

// The board: 5 x 6 cells at a 24-pixel pitch, 120 x 144 pixels, its top-left
// corner here.
#define BOARD_X 60
#define BOARD_Y 2
#define CELL 24
#define STRIP_ROW 19 // the alphabet strip and messages (text row)
#define STRIP_COL 2  // where A is; Z is 25 columns right of it

// --- Graphics (art.c) ----------------------------------------------------------

enum {
    SPR_TILE,     // 32x32, the 22x22 tile centred in it: frame 0 an empty slot, 1-26
                  // the letters, then TILE_SQUASH (27) to TILE_SQUASH + 2: the blank
                  // tile squashed for a flip; TILE_CURSOR (30): the cursor's ring
    SPR_GLYPH,    // 16x16: frames 1-26 the letters, 27 '!'; frame 0 empty. For the cheer
    SPR_ARROW,    // the cursor's arrows
    SPR_CONFETTI, // a falling scrap of a win
    SPR_SERVAL,   // the serval's face: frames SERVAL_IDLE, SERVAL_BLINK, SERVAL_HAPPY, SERVAL_SAD
    SPRITE_COUNT
};

// Frames past the letters (a letter's frame is its code, 1-26).
#define TILE_SQUASH 27 // SPR_TILE: 27-29, the blank tile ever flatter, for a flip
#define TILE_CURSOR 30 // SPR_TILE: a ring around a cell's tile, transparent inside
#define TILE_FRAMES 31
#define GLYPH_BANG 27 // SPR_GLYPH: '!'
#define GLYPH_FRAMES 28
#define ARROW_UP 0 // SPR_ARROW: pointing up (drawn above the cell), and down (below)
#define ARROW_DOWN 1
#define CONFETTI_FRAMES 5 // SPR_CONFETTI: a scrap in each of five colors
enum { SERVAL_IDLE, SERVAL_BLINK, SERVAL_HAPPY, SERVAL_SAD, SERVAL_FRAMES };

// Sprite palettes: PAL_ABSENT to PAL_CORRECT equal their marks, and PAL_TYPED
// equals MARK_NONE, so a tile's palette is its mark.
enum {
    PAL_TYPED,
    PAL_ABSENT,
    PAL_PRESENT,
    PAL_CORRECT,
    PAL_GOLD,
    PAL_RED,
    PAL_SERVAL,
    PALETTE_COUNT
};

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;
// The background: spots on plum, two layers that wrap. The far one moves at
// half the camera's speed (the script drifts the camera), so they slide past
// each other.
extern const Tileset spots_tileset;
extern const MapLayer spots_far_layer;  // background 3, darker spots
extern const MapLayer spots_near_layer; // background 1, lighter spots

void art_build(void); // draws the art into EWRAM at boot

// --- Sound (sound.c) -------------------------------------------------------------

enum {
    SND_TICK,         // a letter spun
    SND_MOVE,         // the cursor moved
    SND_BACK,         // a letter cleared
    SND_BUZZ,         // refused: an empty cell, or a word not in the list
    SND_MARK_ABSENT,  // a tile of an entered guess shows its mark
    SND_MARK_PRESENT, //
    SND_MARK_CORRECT, //
    SND_START,        // a game begins (leaving the title)
    SND_WIN,
    SND_LOSE,
    SND_RESET, // the statistics reset on the title
    SOUND_COUNT
};
extern const PsgSound* const sound_table[SOUND_COUNT];

enum { SONG_THEME, SONG_COUNT };
extern const PsgSong* const songs[SONG_COUNT];

#endif // SERVLE_GAME_H
