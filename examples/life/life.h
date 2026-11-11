#ifndef LIFE_LIFE_H
#define LIFE_LIFE_H

// What Life's GBA side shares between its files: the sprites and their
// palettes (art.c), text composed into sprites (label.c), the world and its
// screens (game.c, title.c, menu.c) and the sound (sound.c).

#include <serval/serval.h>

#include "life_rules.h"
#include "life_save.h"

// --- Sprites (art.c) -------------------------------------------------------------------
//
// The surface leaves sprites 512 tiles: 510 are used, all in one group
// loaded at boot. Text is composed into TEXT and BIG frames at run time.

enum {
    SPR_CURSOR, // 8 x 8: the ring around a cell at 2x, and at 4x
    SPR_TEXT,   // 32 x 8: small text, TEXT_FRAMES of them
    SPR_BIG,    // 32 x 16: the small font doubled, BIG_FRAMES
    SPR_LOGO,   // 32 x 64: L, I, F, E made of cells
    SPR_GLASS,  // 32 x 32: the menu's band (CLEAR_GLASS, EDGE_GLASS)
    SPR_STRIP,  // 32 x 8: the HUD's band
    SPRITE_COUNT
};
#define TEXT_FRAMES 76
#define BIG_FRAMES 5
#define LOGO_LETTERS 4
enum { GLASS_FILL, GLASS_EDGE };

// The group's palettes, SPRITE_PALETTE(n).
enum {
    PAL_TEXT,   // 1 ink, 2 shadow, 3 the theme's accent
    PAL_PICKED, // the menu's chosen row
    PAL_SOFT,   // the menu's other rows, hints
    PAL_PULSE,  // PRESS START, its ink pulsing
    PAL_CURSOR, // 1 the ring, pulsing; 2 its dark edge
    PAL_GLASS,  // 1 the glass, 2 the bright rule, 3 a faint line
    PAL_LOGO,   // 1-6 cells of six ages, 7 their lit corner, 8 shadow
    PAL_COUNT
};

void art_build(void);       // at boot: tiles, the themes' colors, the group
void art_load(void);        // loads the group (after surface_begin())
void art_frame(void);       // every frame: pulsing colors
int logo_width(u32 letter); // of L, I, F and E, in pixels

// The themes: the surface's colors and the backdrop's gradient.
extern const char* const theme_names[LIFE_THEMES];
// Shows theme `theme` with everything mixed `dim` (0-256) toward its night.
// Costs about 30,000 cycles when either changes; nothing otherwise.
void art_show(u32 theme, u32 dim);
// While the stamp shows: its preview's cells breathing.
void art_preview_pulse(void);

// The surface's colors past the cells': the stamp's preview (game.c).
#define COLOR_PREVIEW 16      // a cell the stamp sets, over an empty one
#define COLOR_PREVIEW_LIVE 17 // ... over a live one
#define COLOR_PREVIEW_KILL 18 // a live cell in the stamp's box it would clear
#define COLOR_PREVIEW_BOX 19  // an empty cell in the stamp's box

// --- Labels (label.c) ----------------------------------------------------------------
//
// A line of text composed with the game's font into frames of a sprite, a
// sprite per 32 pixels. Composing compares each frame with what it held and
// queues only those that changed for video memory, at most
// SPRITE_MAX_TILE_UPDATES a frame (labels_flush()).

typedef struct {
    u8 sprite; // SPR_TEXT or SPR_BIG
    u8 first;  // its first frame
    u8 blocks; // frames, 32 pixels each
    u8 width;  // of the text, in pixels
    char text[48];
} Label;

void labels_build(void);
// A label of `blocks` frames from `first`, showing `text`.
void label_init(Label* label, u32 sprite, u32 first, u32 blocks, const char* text);
// Composes `text` if it differs from what the label shows. '~' switches to
// the accent color and back.
void label_set(Label* label, const char* text);
void label_draw(const Label* label, int x, int y, u16 flags);
void label_draw_centered(const Label* label, int y, u16 flags);
// The tiles labels are composed into, for the sprites' assets.
u32* label_tiles(u32 sprite);
// After the group loads: what the buffers hold is in video memory.
void labels_loaded(void);
// Queues the frames that changed (call once a frame, before frame_end()).
void labels_flush(void);

// --- Sound (sound.c) -----------------------------------------------------------------

enum {
    SFX_ON,     // a cell toggled on
    SFX_OFF,    // ... off
    SFX_STAMP,  // a stamp put down
    SFX_TURN,   // the stamp turned
    SFX_CLEAR,  // the world emptied
    SFX_RANDOM, // a soup
    SFX_MOVE,   // the menu's cursor
    SFX_CHANGE, // a menu value changed
    SFX_OPEN,   // menu opened
    SFX_CLOSE,
    SFX_START, // the title left
    SFX_RUN,   // run
    SFX_PAUSE, // pause
    SFX_STEP,  // one step by hand
    SFX_ZOOM_IN,
    SFX_ZOOM_OUT,
    SFX_COUNT
};
void sound_init(void);
void sound_play(u32 sfx); // unless the sound is off
void sound_set(bool on);  // SOUND ON / OFF
void sound_generation(const LifeStats* st, u32 births, u32 deaths, u32 speed);

// --- The world and its screens ---------------------------------------------------

extern const u8 speeds[LIFE_SPEEDS]; // generations a second

// game.c
void game_init(void);        // the world restored from the save, or seeded
void game_title_frame(void); // the world running behind the title
void game_start(void);       // from the title: paused, the cursor in the middle
void game_frame(void);       // play: returns to the menu through game_wants_menu()
bool game_wants_menu(void);
void game_resume(void); // back from the menu
void game_save(void);   // if anything changed since the last save
LifeSettings* game_settings(void);
void game_apply_rule(void); // after settings->rule changed
void game_clear(void);
void game_soup(void);
void game_draw_world(void); // redraws the page (after the menu changed it)

// title.c
void title_build(void);
void title_enter(void);
bool title_frame(void); // false once the title has gone

// menu.c
void menu_build(void);
void menu_open(void);
bool menu_frame(void); // false once it closes

#endif // LIFE_LIFE_H
