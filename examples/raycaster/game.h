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

// A map cell: 0 is open floor, 1-63 a wall with texture cell - 1 (TEX_*),
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
    TEX_DOOR,
    TEX_LOCK,
    TEX_GATE,
    TEX_COUNT
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
// serval, 32 x 32 texels, one cell wide and tall, standing on the floor.
enum {
    THING_NONE, // a free slot
    THING_WISP, // the pale blue kind: comes close and strikes
    THING_HEX,  // the violet kind: hexes from afar, and takes more sparks
    THING_FLASK,
    THING_CRYSTALS,
    THING_KEY,
    THING_BRAZIER,
    THING_URN,
    THING_PILLAR,
};

// The pictures of things (art.c): each wisp frame once per kind.
enum {
    PIC_WISP,                // drifting (2), striking, hurt, fading (2)
    PIC_HEX = PIC_WISP + 6,  // the same six in violet
    PIC_FLASK = PIC_HEX + 6, //
    PIC_CRYSTALS,
    PIC_KEY,
    PIC_BRAZIER, // 2 frames
    PIC_URN = PIC_BRAZIER + 2,
    PIC_PILLAR,
    PIC_COUNT
};
#define PIC_SIZE 32

// What a wisp is doing.
enum { WISP_ASLEEP, WISP_CHASING, WISP_STRIKING, WISP_HURT, WISP_FADING };

#define MAX_THINGS 72
typedef struct {
    Fix x, y;
    u8 kind;  // THING_*
    u8 pic;   // PIC_*: what view.c draws
    u8 state; // wisps: WISP_*
    u8 timer; // ticks left in the state
    u8 rest;  // wisps: ticks before the next strike
    s8 health;
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

// The surface's palette: 31 base colors at 8 brightness levels. Color number
// = level * 32 + base color; level 0 is the brightest. `tint` picks the
// table: the plain one, or the red one shown for a moment when hurt.
#define SHADES 8
#define SHADE(level) ((level) << 5)
#define BASE_CEILING 6
#define BASE_FLOOR 31
enum { TINT_NONE, TINT_HURT, TINT_COUNT };
const Color* art_palette(u32 tint);

// Sprites: the HUD over the view.
enum {
    SPR_WAND,  // 32x64: at rest, and two frames of the flash
    SPR_GLYPH, // 8x16: GLYPH_* frames
    SPR_ICON,  // 16x16: ICON_* frames
    SPR_CROSS, // 8x8
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
void sound_music(bool on); // the warren's tune

// --- The game (game.c) -------------------------------------------------------------

void game_init(void);
void game_frame(void);

#endif // RAYCASTER_GAME_H
