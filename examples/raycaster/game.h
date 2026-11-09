#ifndef RAYCASTER_GAME_H
#define RAYCASTER_GAME_H

// Wisp Warren: what the game's files share. main.c describes the game;
// game.c holds its screens and the HUD, world.c the warren (the map, doors,
// the serval, wisps and things), view.c the first-person view (the rays and
// the columns they draw, on the engine's pixel surface), art.c the graphics
// and sound.c the sounds.

#include "serval/serval.h"

// --- 16.16 fixed point --------------------------------------------------------
//
// The world is measured in map cells, as 16.16 fixed point: the engine's
// FIXED has 8 fraction bits, enough for pixels on a screen, too few for
// rays, which multiply small errors by long distances.
typedef s32 Fix;
#define FIX_ONE 0x10000
// A constant, e.g. FIX(0.75). Constants only: the compiler folds the
// floating-point multiplication away (the GBA has no FPU).
#define FIX(n) ((Fix)((n) * 65536))

static inline Fix fix_mul(Fix a, Fix b) {
    return (Fix)(((int64_t)a * b) >> 16);
}

// An engine FIXED (24.8), such as fx_sin()'s result, as a Fix.
static inline Fix fix_from_fx(FIXED f) {
    return (Fix)f * (FIX_ONE / FX_ONE);
}

static inline FIXED fix_to_fx(Fix f) {
    return f >> 8;
}

// --- The map ------------------------------------------------------------------

#define GRID_W 32
#define GRID_H 32

// A map cell: 0 is open floor, 1-63 a wall with texture cell - 1 (TEX_*; a
// torch and the moon gate have several, shown in turn),
// and CELL_DOOR | n the door doors[n], which is open floor while it is open.
#define CELL_DOOR 0x80
extern u8 grid_cells[GRID_W * GRID_H];

static inline u32 grid_cell(int x, int y) {
    return grid_cells[y * GRID_W + x];
}

// Wall textures (art.c), each 64 x 64.
enum {
    TEX_STONE,
    TEX_MOSS,
    TEX_WOOD,
    TEX_BRICK,
    TEX_BANNER,
    TEX_TORCH,                 // 2 frames
    TEX_SHELF = TEX_TORCH + 2, // a bookcase
    TEX_BARS,                  // a barred window
    TEX_DRAIN,
    TEX_RELIEF, // a carved moon
    TEX_DOOR,
    TEX_LOCK,
    TEX_GATE, // 4 frames
    TEX_COUNT = TEX_GATE + 4
};
#define TEX_SIZE 64

// A door slides sideways into the wall: `open` is how many of its 64 texels
// have gone, 0 closed to 64 open. `vertical`: it stands across a corridor
// that runs east-west (its plane is a north-south line through the middle of
// its cell), else across one running north-south.
#define MAX_DOORS 24
#define DOOR_OPEN 64
#define DOOR_PASSABLE 52 // open enough to walk through
enum { DOOR_SHUT, DOOR_OPENING, DOOR_WAITING, DOOR_CLOSING };
typedef struct {
    u8 x, y;
    u8 open;
    u8 state; // DOOR_*
    u8 timer;
    bool vertical;
    bool locked; // needs the key
} Door;
extern Door doors[MAX_DOORS];

// --- Things -------------------------------------------------------------------

// Everything in the warren that isn't a wall: drawn as a picture facing the
// serval, 64 x 64 texels, one cell wide and tall, standing on the floor.
enum {
    THING_NONE, // a free slot
    THING_WISP, // the pale blue kind: comes close and strikes
    THING_HEX,  // the violet kind: hexes from afar, and takes more sparks
    THING_FLASK,
    THING_CRYSTALS,
    THING_KEY,
    THING_BRAZIER,
    THING_URN, // pushed by sparks; bursts at the third
    THING_PILLAR,
    THING_BLAST, // a burst urn's fire and smoke
    THING_PUFF,  // where a spark struck a wall
};

// The pictures of things (art.c). The hex wisp is drawn from the wisp's
// pictures in its own colors (SHADE_HEX), and casts in two of its own.
enum {
    PIC_WISP,                     // drifting: 4 frames
    PIC_HEX = PIC_WISP,           // (the hex wisp's are the wisp's)
    PIC_WISP_REAR = PIC_WISP + 4, // rearing up to strike
    PIC_WISP_STRIKE,              //
    PIC_WISP_HURT,                //
    PIC_WISP_FADE,                // 3 frames
    PIC_HEX_CAST = PIC_WISP + 10, // gathering a hex, casting it: 2 frames
    PIC_FLASK = PIC_HEX_CAST + 2, //
    PIC_CRYSTALS,
    PIC_KEY,
    PIC_BRAZIER, // 3 frames
    PIC_URN = PIC_BRAZIER + 3,
    PIC_URN_CRACKED, // after one spark
    PIC_URN_BURNING, // after two: its oil alight
    PIC_PILLAR,
    PIC_BLAST,                // 4 frames
    PIC_PUFF = PIC_BLAST + 4, // 2 frames
    PIC_COUNT = PIC_PUFF + 2
};
#define PIC_SIZE 64

// What a wisp is doing.
enum { WISP_ASLEEP, WISP_CHASING, WISP_STRIKING, WISP_HURT, WISP_FADING };

#define MAX_THINGS 80
typedef struct {
    Fix x, y;
    Fix vx, vy; // an urn sliding: cells a tick
    u8 kind;    // THING_*
    u8 pic;     // PIC_*: what view.c draws
    u8 state;   // wisps: WISP_*
    u8 timer;   // ticks left in the state; an urn: to its burst, set by another's
    u8 rest;    // wisps: ticks before the next strike
    s8 health;  // wisps and urns: sparks they can still take
    bool glows; // drawn at full brightness however far away
} Thing;
extern Thing things[MAX_THINGS];

// --- The serval ---------------------------------------------------------------

typedef struct {
    Fix x, y;
    u16 angle; // the engine's angles: 0 is east (+x), growing clockwise (+y is south)
    s16 health;
    s16 sparks;
    bool has_key;
    u8 fire_wait; // ticks until the wand can fire again
    u8 flash;     // ticks the wand's flash still shows
    u8 hurt;      // ticks the screen stays red
    u8 recharge;  // ticks until a spark comes back by itself
    u8 shake;     // ticks the view still shakes: a burst, a blow
    u8 dazzle;    // ticks the screen stays bright from a burst
    u8 glint;     // ticks it glints gold from a pickup
} Player;
extern Player player;

// The run so far.
typedef struct {
    u32 ticks;  // 30 a second
    u16 wisps;  // in the warren at the start
    u16 downed; // of them
    bool won;   // the serval went through the moon gate
    bool over;  // won or lost: the run has ended
    u8 difficulty;
} Run;
extern Run run;

enum { EASY, NORMAL, HARD, DIFFICULTY_COUNT };

// What the player asks for in a tick (game.c reads the buttons).
typedef struct {
    s8 forward; // -1 back, 1 forward
    s8 strafe;  // -1 left, 1 right
    s8 turn;    // -1 left, 1 right
    bool fire;
    bool use;
} Controls;

// world.c
void world_start(u32 difficulty);       // builds the warren from its text
void world_tick(const Controls* input); // one step, 30 times a second

// --- The view (view.c) ---------------------------------------------------------

// The picture is VIEW_COLS columns of VIEW_ROWS pixels, each column two
// screen pixels wide.
#define VIEW_COLS 120
#define VIEW_ROWS 160

// Sets up the view's tables; once, at boot.
void view_init(void);
// Notes where the serval stands and looks and which things it can see, for
// the columns drawn until the next call. A picture is drawn over two frames
// (game.c), and both halves must show the same moment.
void view_begin(void);
// Draws columns `first` to `last` - 1 of the picture into the surface page
// being drawn.
void view_draw(int first, int last);

// --- Art (art.c) ----------------------------------------------------------------

// Converts the ASCII art to textures, pictures and sprite tiles; at boot.
void art_build(void);
// The wall textures and things' pictures, one byte per texel (a base color,
// 1-31; in pictures 0 is transparent), stored column after column so that a
// column on screen reads its texels from consecutive bytes.
extern u8 wall_texels[TEX_COUNT][TEX_SIZE * TEX_SIZE];
extern u8 pic_texels[PIC_COUNT][PIC_SIZE * PIC_SIZE];
// For each column x of a picture, its opaque texels: from pic_spans[p][2 * x]
// to pic_spans[p][2 * x + 1] - 1 (none if the two are equal), so the view
// skips the air above and below.
extern u8 pic_spans[PIC_COUNT][PIC_SIZE * 2];

// The surface's palette: 31 base colors at 7 brightness levels. Color number
// = level * 32 + base color; level 0 is the brightest. The eighth row of 32
// is the hex wisp's: its colors in the places of the wisp's, so the two share
// their pictures. `tint` picks the table: the plain one, the red one shown
// for a moment when hurt, the bright one of a burst urn's flash, or the
// gold glint of a pickup.
#define SHADES 7
#define SHADE(level) ((level) << 5)
#define SHADE_HEX SHADE(SHADES)
#define BASE_CEILING 6
#define BASE_FLOOR 31
enum { TINT_NONE, TINT_HURT, TINT_BLAST, TINT_GLINT, TINT_COUNT };
const Color* art_palette(u32 tint);

// Sprites: the HUD over the view.
enum {
    SPR_WAND,  // 64x64: at rest, and two frames of the flash
    SPR_GLYPH, // 8x16: GLYPH_* frames
    SPR_ICON,  // 16x16: ICON_* frames
    SPR_CROSS, // 8x8
    // The title's, menu's and results' (title.c), in groups of their own:
    // hud_group is the sprites before these.
    SPR_LOGO,   // 32x32: the logo's letters, LOGO_* frames
    SPR_START,  // 32x16: PRESS START on the title, a label's frames
    SPR_STORY,  // 32x8: the title's line of story, a label's frames
    SPR_WORD,   // 32x16: the menu's and results' labels in the big font
    SPR_LINE,   // 32x8: and in the small font
    SPR_PANEL,  // 32x16: the glass band, PANEL_* frames
    SPR_CURSOR, // 16x16: the menu's spark, 2 frames
    SPRITE_COUNT
};
enum { GLYPH_0, GLYPH_P = 10, GLYPH_A, GLYPH_U, GLYPH_S, GLYPH_E, GLYPH_D, GLYPH_PERCENT };
enum { ICON_HEART, ICON_CRYSTAL, ICON_KEY };
extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup hud_group;

// --- Sound (sound.c) --------------------------------------------------------------

enum {
    SND_SPARK,
    SND_DRY, // the wand with no sparks
    SND_WISP_HURT,
    SND_WISP_GONE,
    SND_WISP_WAKES,
    SND_STRIKE, // a wisp strikes
    SND_HEX,    // a hex wisp casts
    SND_HURT,   // the serval is hit
    SND_URN,    // a spark knocks an urn
    SND_BLAST,  // and an urn bursts: its roar
    SND_BOOM,   // and its thump
    SND_PICKUP,
    SND_KEY,
    SND_DOOR,
    SND_LOCKED,
    SND_UNLOCK,
    SND_MOVE, // menus
    SND_CONFIRM,
    SND_PAUSE,
    SND_WON,
    SND_LOST,
    SOUND_COUNT
};
void sound_init(void);
// The music: one tune at a time. Asking for the tune that is playing changes
// nothing, so screens can ask on every entry.
enum { TUNE_NONE, TUNE_TITLE, TUNE_WARREN, TUNE_COUNT };
void sound_music(u32 tune);

// --- The title, the menu and the results (title.c) ---------------------------------

// Converts the logo and the fonts, and mixes the view's colors for these
// screens; at boot, after art_build().
void title_build(void);

// Their sprites, in two groups that are never loaded together: the title's
// (SPR_LOGO, SPR_START, SPR_STORY) and the menu's and results' (the rest).
extern const SpriteAsset title_sprites[SPRITE_COUNT - SPR_LOGO];
extern const SpriteGroup title_group, menu_group;
enum {
    LOGO_W,
    LOGO_I,
    LOGO_S,
    LOGO_P,
    LOGO_A,
    LOGO_R,
    LOGO_E,
    LOGO_N,
    LOGO_I_FLICKER,
    LOGO_MOON,
    LOGO_FRAMES
};
enum { PANEL_FILL, PANEL_EDGE, PANEL_FRAMES };
// Palettes, for SPRITE_PALETTE(): the first two are in both groups, then the
// title's and the menu's own.
enum { PAL_TEXT, PAL_PULSE, PAL_WISP, PAL_WARREN, PAL_GLINT };
enum { PAL_GOLD = PAL_PULSE + 1, PAL_SOFT, PAL_RED, PAL_GLASS, PAL_BAR };

// A line of text as sprites: `blocks` frames of `sprite`, 32 pixels each,
// from frame `first`, drawn side by side.
typedef struct {
    u8 sprite; // SPR_START, SPR_STORY, SPR_WORD (the big font) or SPR_LINE (the small)
    u8 first;
    u8 blocks;
    u8 width; // of the text, in pixels
} Label;
// Composes `text` into the next free frames of `sprite`: as many as it
// needs, or `blocks` of them (room for label_set()). In the small font, text
// between two '~' is in the palette's second color, and + @ $ < > are the
// D-pad and the A, B, L and R buttons. The frames are in RAM: they show once
// the sprite's group is loaded after this. labels_clear() gives all of a
// sprite's frames back.
Label label_make(u32 sprite, const char* text, u32 blocks);
void labels_clear(u32 sprite);
// Changes a label on screen (sprite_set_tiles(): one label a frame).
void label_set(Label* label, const char* text);
void label_draw(Label label, int x, int y, u16 flags);
void label_draw_centered(Label label, int y, u16 flags);

// The logo, `time` frames after it began to arrive (its letters drop in one
// after another: all are in place after logo_frames()), and `leaving` frames
// after it began to leave, upward (0: it stays).
void logo_draw(int time, int leaving);
int logo_frames(void);
// A band of glass across the screen with a rule along its top and bottom, and
// a bar of `blocks` x 32 pixels by 20: SPR_PANEL's frames, drawn with `flags`
// (SPRITE_BLEND, and for the bar SPRITE_PALETTE(PAL_BAR)). Draw them after
// what stands on them: of two sprites, the one drawn first is in front.
void band_draw(int top, int height, u16 flags);
void bar_draw(int x, int y, int blocks, u16 flags);

// The surface's colors behind these screens, in place of art_palette()'s: the
// view dimmed toward night blue, and the last picture of a run for the results.
enum { VIEW_TITLE, VIEW_BETWEEN, VIEW_MENU, VIEW_WON, VIEW_LOST, VIEW_COUNT };
const Color* view_colors(u32 which);

// --- The game (game.c) -------------------------------------------------------------

void game_init(void);
void game_frame(void);

#endif // RAYCASTER_GAME_H
