// Graphics: every sprite and background tile, drawn at boot (art_build) into
// EWRAM buffers that sprite_group_load and tileset_load copy to VRAM, as
// fireflies and blackjack do. The letters and the serval are ASCII pictures,
// one character per pixel ('.' transparent); the tiles, arrows, confetti and
// the background's spots are drawn by a few rules.
//
// One set of tile pictures shows every state of a cell: the palettes PAL_TYPED
// to PAL_CORRECT give the same art each state's face, letter and outline
// colors, and the script picks one per tile (SPRITE_PALETTE in its flags).
// Even the marks' badges are drawn on every letter tile: the correct mark's
// spots and the present mark's cross have colors of their own only in those
// palettes, and are the face's color in the others.
//
// Sprite VRAM, in 8x8 tiles (1,024 in all): the board's tiles 31 frames of 16
// (496), the cheer's glyphs 28 of 4 (112), the serval 4 frames of 64 (256),
// the arrows 2 of 2 and the confetti 5 of 1: 873.

#include "game.h"

// --- Palettes ----------------------------------------------------------------

// Colors 1-3 and 15 are the same in every tile and glyph palette: the empty
// slot (frame 0 of the tiles) and the outlines look the same whatever palette
// a tile or glyph has. The serval's palette is its own.
#define OUTLINE COLOR_RGB(23, 11, 40)
#define SLOT COLOR_RGB(33, 18, 56)
#define SLOT_EDGE COLOR_RGB(98, 74, 140)
#define SLOT_DARK COLOR_RGB(16, 7, 30) // the slot's inner shadow, top and left
enum { C_OUTLINE = 1, C_SLOT, C_SLOT_EDGE, C_FACE, C_INK, C_CONFETTI };
// After the confetti's five: a face's bevel (light along its top, shade along
// its bottom and under the letter), the marks' badges, the slot's shadow.
enum { C_LIGHT = 11, C_SHADE, C_SPOTS, C_DOT, C_SLOT_DARK };

// The confetti's five colors (PAL_GOLD and PAL_RED alike), C_CONFETTI on.
#define CONFETTI_COLORS                                                                            \
    COLOR_RGB(255, 170, 60), COLOR_RGB(96, 206, 244), COLOR_RGB(255, 132, 160),                    \
        COLOR_RGB(255, 241, 207), COLOR_RGB(190, 150, 255)

// The marks' colors: the serval's own amber for a letter in its place (with
// the coat's dark spots), sky blue for one elsewhere in the word (with a
// small cross), a dusky plum that sinks into the background for one not in
// it. A palette's badges are its face color where the mark has none.
#define TILE_PALETTE(face, ink, light, shade, spots, dot)                                          \
    {0, OUTLINE, SLOT, SLOT_EDGE, (face),  (ink),   0,     0,                                      \
     0, 0,       0,    (light),   (shade), (spots), (dot), SLOT_DARK}
#define TYPED_FACE COLOR_RGB(255, 241, 207)
#define ABSENT_FACE COLOR_RGB(72, 58, 104)
#define PRESENT_FACE COLOR_RGB(84, 190, 236)
#define CORRECT_FACE COLOR_RGB(246, 158, 46)

static const u16 palettes[PALETTE_COUNT][16] = {
    // A tile's face and letter, in each of its states.
    [PAL_TYPED] = TILE_PALETTE(TYPED_FACE, COLOR_RGB(43, 23, 70), COLOR_RGB(255, 255, 244),
                               COLOR_RGB(222, 198, 160), TYPED_FACE, TYPED_FACE),
    [PAL_ABSENT] = TILE_PALETTE(ABSENT_FACE, COLOR_RGB(168, 156, 206), COLOR_RGB(98, 84, 134),
                                COLOR_RGB(52, 40, 80), ABSENT_FACE, ABSENT_FACE),
    [PAL_PRESENT] = TILE_PALETTE(PRESENT_FACE, COLOR_RGB(12, 38, 74), COLOR_RGB(176, 234, 255),
                                 COLOR_RGB(42, 132, 192), PRESENT_FACE, COLOR_RGB(16, 64, 124)),
    [PAL_CORRECT] = TILE_PALETTE(CORRECT_FACE, COLOR_RGB(66, 28, 0), COLOR_RGB(255, 214, 128),
                                 COLOR_RGB(196, 104, 24), COLOR_RGB(150, 72, 16), CORRECT_FACE),
    // The cheer's glyphs, the arrows (C_FACE gold) and the confetti; a lost
    // round's answer in red. Their C_SHADE is the glyphs' lower half.
    [PAL_GOLD] = {0, OUTLINE, SLOT, SLOT_EDGE, COLOR_RGB(255, 218, 100), COLOR_RGB(61, 37, 0),
                  CONFETTI_COLORS, COLOR_RGB(255, 246, 200), COLOR_RGB(240, 150, 40), 0, 0,
                  SLOT_DARK},
    [PAL_RED] = {0, OUTLINE, SLOT, SLOT_EDGE, COLOR_RGB(255, 150, 150), COLOR_RGB(61, 37, 0),
                 CONFETTI_COLORS, COLOR_RGB(255, 222, 222), COLOR_RGB(220, 72, 96), 0, 0,
                 SLOT_DARK},
    // The serval: K outline, O ear fur, F face fur, P inner ear, W muzzle,
    // D spots, H the eyes' glint, N nose.
    [PAL_SERVAL] = {0, OUTLINE, COLOR_RGB(226, 162, 58), COLOR_RGB(238, 176, 68),
                    COLOR_RGB(246, 201, 176), COLOR_RGB(255, 241, 207), COLOR_RGB(61, 37, 0),
                    COLOR_RGB(255, 255, 255), COLOR_RGB(208, 96, 122)},
};
static const char serval_keys[] = ".KOFPWDHN";

// --- A canvas to draw on, then cut into tiles --------------------------------

// Big enough for every picture: the largest are the layers of spots, the far
// one 80x80 and the near one 96x64. EWRAM: boot-time scratch, on no hot path.
static u8 canvas[96 * 80] SERVAL_EWRAM_BSS;
static int canvas_w, canvas_h;

static void canvas_begin(int w, int h) {
    canvas_w = w;
    canvas_h = h;
    for (int i = 0; i < w * h; i++)
        canvas[i] = 0;
}

static u8 get(int x, int y) {
    if (x < 0 || y < 0 || x >= canvas_w || y >= canvas_h)
        return 0;
    return canvas[y * canvas_w + x];
}

static void put(int x, int y, int c) {
    if (x >= 0 && y >= 0 && x < canvas_w && y < canvas_h)
        canvas[y * canvas_w + x] = (u8)c;
}

// Cuts the canvas into 4bpp tiles at `out`, row by row (1D mapping, as sprite
// frames are laid out). Returns the end.
static u32* canvas_pack(u32* out) {
    for (int ty = 0; ty < canvas_h / 8; ty++) {
        for (int tx = 0; tx < canvas_w / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)get(tx * 8 + x, ty * 8 + y) << (4 * x);
                *out++ = word;
            }
        }
    }
    return out;
}

// Draws `count` rows of an ASCII picture at (x, y): each character found in
// `keys` is the color of its place there; '.' and others draw nothing.
static void draw(const char* const* rows, int count, int x, int y, const char* keys) {
    for (int py = 0; py < count; py++) {
        for (int px = 0; rows[py][px]; px++) {
            for (int k = 1; keys[k]; k++)
                if (rows[py][px] == keys[k])
                    put(x + px, y + py, k);
        }
    }
}

// Outlines everything drawn: transparent pixels next to a drawn one, also
// diagonally, get `color`. Its copy of the canvas is the canvas's size, so any
// picture fits.
static void outline(int color) {
    static u8 copy[sizeof canvas] SERVAL_EWRAM_BSS;
    for (int i = 0; i < canvas_w * canvas_h; i++)
        copy[i] = canvas[i];
    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
            if (copy[y * canvas_w + x])
                continue;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < canvas_w && ny < canvas_h &&
                        copy[ny * canvas_w + nx])
                        put(x, y, color);
                }
            }
        }
    }
}

// --- The letters ---------------------------------------------------------------
//
// A chunky font of 12x14 pixels, about twice the engine's 8x8 text: three-pixel
// strokes, rounded where the letter is round. Index 1-26 is A-Z (a letter's
// code in the script), GLYPH_BANG '!'.

#define FONT_W 12
#define FONT_H 14
// Six letters side by side, one row of pixels per line: A-F, G-L, M-R, S-X,
// then Y, Z and !.
// clang-format off
#define FONT_PER_ROW 6
static const char* const font_art[5][FONT_H] = {
    {
        "..########.." "##########.." "..##########" "#########..." "############" "############",
        ".##########." "###########." ".###########" "##########.." "############" "############",
        "############" "############" "############" "###########." "############" "############",
        "###......###" "###......###" "###........." "###.....####" "###........." "###.........",
        "###......###" "###......###" "###........." "###......###" "###........." "###.........",
        "###......###" "###.....###." "###........." "###......###" "###........." "###.........",
        "############" "##########.." "###........." "###......###" "#########..." "#########...",
        "############" "##########.." "###........." "###......###" "#########..." "#########...",
        "###......###" "###.....###." "###........." "###......###" "###........." "###.........",
        "###......###" "###......###" "###........." "###......###" "###........." "###.........",
        "###......###" "###......###" "###........." "###.....####" "###........." "###.........",
        "###......###" "############" "############" "###########." "############" "###.........",
        "###......###" "###########." ".###########" "##########.." "############" "###.........",
        "###......###" "##########.." "..##########" "#########..." "############" "###.........",
    },
    {
        "..##########" "###......###" "############" "....########" "###.....####" "###.........",
        ".###########" "###......###" "############" "....########" "###....####." "###.........",
        "############" "###......###" "############" "....########" "###...####.." "###.........",
        "###........." "###......###" "....####...." ".........###" "###..####..." "###.........",
        "###........." "###......###" "....####...." ".........###" "###.####...." "###.........",
        "###........." "###......###" "....####...." ".........###" "#######....." "###.........",
        "###...######" "############" "....####...." ".........###" "######......" "###.........",
        "###...######" "############" "....####...." ".........###" "######......" "###.........",
        "###......###" "###......###" "....####...." "###......###" "#######....." "###.........",
        "###......###" "###......###" "....####...." "###......###" "###.####...." "###.........",
        "###......###" "###......###" "....####...." "###......###" "###..####..." "###.........",
        "############" "###......###" "############" "############" "###...####.." "############",
        ".##########." "###......###" "############" ".##########." "###....####." "############",
        "..########.." "###......###" "############" "..########.." "###.....####" "############",
    },
    {
        "###......###" "###......###" "..########.." "##########.." "..########.." "##########..",
        "####....####" "####.....###" ".##########." "###########." ".##########." "###########.",
        "#####..#####" "#####....###" "############" "############" "############" "############",
        "############" "######...###" "###......###" "###......###" "###......###" "###......###",
        "###.####.###" "###.###..###" "###......###" "###......###" "###......###" "###......###",
        "###..##..###" "###.###..###" "###......###" "###......###" "###......###" "###......###",
        "###......###" "###..###.###" "###......###" "############" "###......###" "############",
        "###......###" "###..###.###" "###......###" "###########." "###......###" "###########.",
        "###......###" "###...######" "###......###" "##########.." "###..###.###" "##########..",
        "###......###" "###...######" "###......###" "###........." "###...######" "###...####..",
        "###......###" "###....#####" "###......###" "###........." "###....####." "###....####.",
        "###......###" "###.....####" "############" "###........." "############" "###....####.",
        "###......###" "###......###" ".##########." "###........." ".###########" "###.....####",
        "###......###" "###......###" "..########.." "###........." "..#######.##" "###......###",
    },
    {
        "..##########" "############" "###......###" "###......###" "###......###" "###......###",
        ".###########" "############" "###......###" "###......###" "###......###" "####....####",
        "############" "############" "###......###" "###......###" "###......###" ".###....###.",
        "###........." "....####...." "###......###" "###......###" "###......###" ".####..####.",
        "###........." "....####...." "###......###" "###......###" "###......###" "..########..",
        "###........." "....####...." "###......###" "####....####" "###......###" "...######...",
        "###########." "....####...." "###......###" ".###....###." "###......###" "....####....",
        ".###########" "....####...." "###......###" ".####..####." "###..##..###" "....####....",
        ".........###" "....####...." "###......###" "..###..###.." "###.####.###" "...######...",
        ".........###" "....####...." "###......###" "..########.." "############" "..########..",
        ".........###" "....####...." "###......###" "...######..." "############" ".####..####.",
        "############" "....####...." "############" "...######..." "#####..#####" ".###....###.",
        "###########." "....####...." ".##########." "....####...." "####....####" "####....####",
        "##########.." "....####...." "..########.." "....####...." "###......###" "###......###",
    },
    {
        "###......###" "############" "....####....",
        "####....####" "############" "....####....",
        ".###....###." "############" "....####....",
        ".####..####." "........####" "....####....",
        "..########.." ".......####." "....####....",
        "...######..." "......####.." "....####....",
        "....####...." ".....####..." "....####....",
        "....####...." "....####...." "....####....",
        "....####...." "...####....." "....####....",
        "....####...." "..####......" "............",
        "....####...." ".####......." "............",
        "....####...." "############" "....####....",
        "....####...." "############" "....####....",
        "....####...." "############" "....####....",
    },
};
// clang-format on

// Whether pixel (x, y) of glyph g (1-26 a letter, GLYPH_BANG '!') is ink.
static bool font_ink(int g, int x, int y) {
    return font_art[(g - 1) / FONT_PER_ROW][y][(g - 1) % FONT_PER_ROW * FONT_W + x] == '#';
}

// --- The board's tiles -----------------------------------------------------------
//
// A tile is 22x22 with a one-pixel outline, rounded corners and a hard shadow
// two pixels deep below it, in a 32x32 frame at (TILE_LEFT, TILE_TOP), the
// script's TILE_INSET plus the one pixel between a 24-pixel cell's edge and
// the tile. The letter sits in its middle.

#define TILE_SIZE 22
#define TILE_LEFT 5
#define TILE_TOP 5
#define SHADOW 2

// A box with a rounded outline: `fill` inside, `edge` on its border, and with
// `shadow`, the outline color in the two rows below it.
static void box(int x0, int y0, int w, int h, int fill, int edge, bool shadow) {
    for (int y = 0; y < h + (shadow ? SHADOW : 0); y++) {
        for (int x = 0; x < w; x++) {
            bool side_x = x == 0 || x == w - 1, side_y = y == 0 || y == h - 1;
            bool near_x = x <= 1 || x >= w - 2, near_y = y <= 1 || y >= h - 2;
            if (y >= h) { // the shadow: the box's bottom edge moved down
                if (!side_x)
                    put(x0 + x, y0 + y, C_OUTLINE);
                continue;
            }
            if (side_x && side_y)
                continue; // the corners are cut
            put(x0 + x, y0 + y, side_x || side_y || (near_x && near_y) ? edge : fill);
        }
    }
}

// Recolors the inside of a box drawn by box(): the face's pixels on its row
// `y` (counted from the box's top) take `color`, the rounded corners left as
// they are.
static void box_row(int x0, int y0, int w, int y, int color) {
    for (int x = 2; x < w - 2; x++)
        put(x0 + x, y0 + y, color);
}

// A tile's face: the box, lit along its top row and shaded along its bottom
// two, so it stands up from the board.
static void face(int x0, int y0, int h) {
    box(x0, y0, TILE_SIZE, h, C_FACE, C_OUTLINE, true);
    if (h >= 6) {
        box_row(x0, y0, TILE_SIZE, 1, C_LIGHT);
        box_row(x0, y0, TILE_SIZE, h - 2, C_SHADE);
    }
    if (h >= 10)
        box_row(x0, y0, TILE_SIZE, h - 3, C_SHADE);
}

// The tile flattened to `h` rows about its middle, without its letter.
static void squashed_tile(int h) {
    face(TILE_LEFT, TILE_TOP + (TILE_SIZE - h) / 2, h);
}

// The marks' badges, in the margins the letter leaves (x 2-4 and 18-20, a
// tile's own pixels): the correct mark's spots, a serval's coat, irregular
// on purpose, and the present mark's small cross in the top-right corner.
// Each is the face's color in the other palettes.
typedef struct {
    u8 x, y, w, h;
} Badge;
static const Badge coat_spots[] = {{2, 3, 3, 2},  {2, 9, 2, 3},   {3, 15, 2, 2},
                                   {18, 4, 2, 3}, {19, 10, 2, 2}, {18, 15, 2, 2}};
static const Badge dot = {17, 2, 3, 3};

// A badge is a rectangle, its corners cut when it is 3 pixels or more each way.
static void badge(const Badge* b, int color) {
    for (int y = 0; y < b->h; y++) {
        for (int x = 0; x < b->w; x++) {
            bool corner = (x == 0 || x == b->w - 1) && (y == 0 || y == b->h - 1);
            if (!(corner && b->w >= 3 && b->h >= 3))
                put(TILE_LEFT + b->x + x, TILE_TOP + b->y + y, color);
        }
    }
}

// The empty slot: a well, its inside shadowed along the top and left.
static void slot(void) {
    box(TILE_LEFT, TILE_TOP, TILE_SIZE, TILE_SIZE, C_SLOT, C_SLOT_EDGE, false);
    box_row(TILE_LEFT, TILE_TOP, TILE_SIZE, 1, C_SLOT_DARK);
    for (int y = 2; y < TILE_SIZE - 2; y++)
        put(TILE_LEFT + 1, TILE_TOP + y, C_SLOT_DARK);
}

#define SQUASH_HEIGHTS 3
static const u8 squash_heights[SQUASH_HEIGHTS] = {14, 7, 3};

// The cursor's ring: two pixels wide, in the two-pixel gap between the cell's
// tile and its neighbours (over the shadow below it), corners cut, with an
// outline inside and out so it reads on an empty slot and on a cream tile.
static void cursor_ring(void) {
    int lo = TILE_LEFT - 2, hi = TILE_LEFT + TILE_SIZE + 1; // the ring's outer edges
    for (int y = lo; y <= hi; y++) {
        for (int x = lo; x <= hi; x++) {
            bool edge_x = x <= lo + 1 || x >= hi - 1, edge_y = y <= lo + 1 || y >= hi - 1;
            bool corner = (x == lo || x == hi) && (y == lo || y == hi);
            if ((edge_x || edge_y) && !corner)
                put(x, y, C_FACE);
        }
    }
    outline(C_OUTLINE);
}

// --- The serval ------------------------------------------------------------------
//
// Its face, after the sketch's: big ears with dark tips, a golden spotted
// head, a cream muzzle. 48x48 in a 64x64 frame at (8, 8), the sprite's origin,
// so an entity's position is the 48x48 box's top-left. Ears up or drooping
// (sad), then the head over them, then the eyes of the frame.

// Rows 1-21 of the face.
static const char* const ears_up[21] = {"............K......................K............",
                                        "............K......................K............",
                                        "...........KKK....................KKK...........",
                                        "...........KKKK..................KKKK...........",
                                        "...........KKKKK................KKKKK...........",
                                        "...........KKKKK................KKKKK...........",
                                        "..........KKKOOOK..............KOOOKKK..........",
                                        "..........KKOPOOOK............KOOOPOKK..........",
                                        "..........KOOPPOOOK..........KOOOPPOOK..........",
                                        "..........KOOPPPOOK..........KOOPPPOOK..........",
                                        "..........KOOPPPOOOK........KOOOPPPOOK..........",
                                        "..........KOPPPPPOOOK......KOOOPPPPPOK..........",
                                        "..........KOPPPPPPOOOK....KOOOPPPPPPOK..........",
                                        ".........KOOPPPPPPPOOK....KOOPPPPPPPOOK.........",
                                        ".........KOOPPPPPOOOOOK..KOOOOOPPPPPOOK.........",
                                        ".........KOOPPOOOOOOKK....KKOOOOOOPPOOK.........",
                                        ".........KOOOOOOOKKK........KKKOOOOOOOK.........",
                                        ".........KOOOOKKK..............KKKOOOOK.........",
                                        "........KOOOKK....................KKOOOK........",
                                        "........KKKK........................KKKK........",
                                        "........K..............................K........"};

// Sad: rows 7-27, each ear turned 22 degrees out and down.
static const char* const ears_droop[21] = {".....K....................................K.....",
                                           ".....KKK................................KKK.....",
                                           ".....KKKKK............................KKKKK.....",
                                           ".....KKKKKK..........................KKKKKK.....",
                                           "......KKKOOKK......................KKOOKKK......",
                                           "......KKPOOOOKK..................KKOOOOPKK......",
                                           "......KKOPOOOOOK................KOOOOOPOKK......",
                                           "......KOOPPPOOOOKK............KKOOOOPPPOOK......",
                                           ".......KOPPPPPOOOOK..........KOOOOPPPPPOK.......",
                                           ".......KOPPPPPPOOOOKK......KKOOOOPPPPPPOK.......",
                                           ".......KOOPPPPPPPOOK........KOOPPPPPPPOOK.......",
                                           ".......KOOPPPPPOOOK..........KOOOPPPPPOOK.......",
                                           ".......KOOPPPPOOOK............KOOOPPPPOOK.......",
                                           "........KOOPPOOOK..............KOOOPPOOK........",
                                           "........KOOOOOOK................KOOOOOOK........",
                                           "........KOOOOOK..................KOOOOOK........",
                                           "........KOOOOK....................KOOOOK........",
                                           "........KOOOK......................KOOOK........",
                                           ".........KOK........................KOK.........",
                                           ".........KK..........................KK.........",
                                           ".........K............................K........."};

// Rows 12-40.
static const char* const head[29] = {"..................KKKKKKKKKKKK..................",
                                     ".............KKKKKFFFFFFFFFFFFKKKKK.............",
                                     "............KFFFFFFFFFFDDFFFFFFFFFFK............",
                                     "..........KKFFFFFFFFDDFDDFDDFFFFFFFFKK..........",
                                     ".........KFFFFFFFFFFDDFDDFDDFFFFFFFFFFK.........",
                                     ".........KFFFFFFFFFFDDFDDFDDFFFFFFFFFFK.........",
                                     "........KFFFFFFFFFFFDDFDDFDDFFFFFFFFFFFK........",
                                     ".......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK.......",
                                     ".......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK.......",
                                     ".......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK.......",
                                     ".......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK.......",
                                     ".......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK.......",
                                     "......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK......",
                                     "......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK......",
                                     "......KFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFK......",
                                     "......KFFFDDFFFFFFFFFFFFFFFFFFFFFFFFDDFFFK......",
                                     "......KFFFDDFFFFFFFFWWWWWWWWFFFFFFFFDDFFFK......",
                                     ".......KFFFFFFFFFFWWWWWWWWWWWWFFFFFFFFFFK.......",
                                     ".......KFFFFFFFFFWWWWWNNNNWWWWWFFFFFFFFFK.......",
                                     ".......KFFFFDDFFWWWWWWWNNWWWWWWWFFDDFFFFK.......",
                                     ".......KFFFFDDFWWWWWWWWKKWWWWWWWWFDDFFFFK.......",
                                     ".......KFFFFFFFWWWWWWWWKKWWWWWWWWFFFFFFFK.......",
                                     "........KFFFFFFWWWWKWWWKKWWWKWWWWFFFFFFK........",
                                     ".........KFFFFFWWWWKKKKKKKKKKWWWWFFFFFK.........",
                                     ".........KFFFFFFWWWWWWWWWWWWWWWWFFFFFFK.........",
                                     "..........KKFFFFFWWWWWWWWWWWWWWFFFFFKK..........",
                                     "............KFFFFFWWWWWWWWWWWWFFFFFK............",
                                     ".............KKKKKFFWWWWWWWWFFKKKKK.............",
                                     "..................KKKKKKKKKKKK.................."};

// The eyes, rows 21-28, columns 12-35 of the face, by frame.
static const char* const eyes[SERVAL_FRAMES][8] = {
    [SERVAL_IDLE] = {"........................", "...KKKK..........KKKK...",
                     "..KKKHKK........KKKHKK..", "..KKKKKK........KKKKKK..",
                     "..KKKKKK........KKKKKK..", "..KKKKKK........KKKKKK..",
                     "...KKKK..........KKKK...", "........................"},
    [SERVAL_BLINK] = {"........................", "........................",
                      "........................", "........................",
                      "..KKKKKK........KKKKKK..", "........................",
                      "........................", "........................"},
    [SERVAL_HAPPY] = {"........................", "....KK............KK....",
                      "...KKKK..........KKKK...", "..KKKKKK........KKKKKK..",
                      ".KKK..KKK......KKK..KKK.", ".KK....KK......KK....KK.",
                      "........................", "........................"},
    [SERVAL_SAD] = {"........................", "........................",
                    ".......KK......KK.......", "....KKKKK......KKKKK....",
                    ".KKKKK............KKKKK.", ".KK..................KK.",
                    "........................", "........................"},
};

#define EYES_X 12
#define EYES_Y 21

static void serval_frame(int frame) {
    canvas_begin(64, 64);
    if (frame == SERVAL_SAD)
        draw(ears_droop, 21, 8, 8 + 7, serval_keys);
    else
        draw(ears_up, 21, 8, 8 + 1, serval_keys);
    draw(head, 29, 8, 8 + 12, serval_keys);
    draw(eyes[frame], 8, 8 + EYES_X, 8 + EYES_Y, serval_keys);
}

// --- Arrows and confetti -----------------------------------------------------------

// The cursor's arrows: a gold triangle with an outline, 10x7, at (3, 0) of a
// 16x8 frame. ARROW_UP points up; ARROW_DOWN is the same picture upside down.
static const char* const arrow[5] = {"...##...", "..####..", ".######.", "########", "########"};

// The confetti's scraps, 5x5, one per color: a square, a streamer each way, a
// diamond, a star.
static const char* const confetti[CONFETTI_FRAMES][5] = {
    {".....", ".###.", ".###.", ".###.", "....."},
    {".....", "#....", ".###.", "....#", "....."},
    {"..#..", "..#..", "..#..", "..#..", "....."},
    {"..#..", ".###.", "#####", ".###.", "..#.."},
    {"..#..", ".#.#.", "#...#", ".#.#.", "..#.."}};

// --- Sprites ---------------------------------------------------------------------

#define TILE_TILES 16   // 32x32
#define GLYPH_TILES 4   // 16x16
#define ARROW_TILES 2   // 16x8
#define SERVAL_TILES 64 // 64x64
#define T_TILE 0
#define T_GLYPH (T_TILE + TILE_FRAMES * TILE_TILES)
#define T_ARROW (T_GLYPH + GLYPH_FRAMES * GLYPH_TILES)
#define T_CONFETTI (T_ARROW + 2 * ARROW_TILES)
#define T_SERVAL (T_CONFETTI + CONFETTI_FRAMES)
#define OBJ_TILES (T_SERVAL + SERVAL_FRAMES * SERVAL_TILES)

static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_TILE] = {.size = SPRITE_32x32,
                  .tiles = obj_tiles + T_TILE * 8,
                  .frame_count = TILE_FRAMES,
                  .palette_slot = PAL_TYPED},
    [SPR_GLYPH] = {.size = SPRITE_16x16,
                   .tiles = obj_tiles + T_GLYPH * 8,
                   .frame_count = GLYPH_FRAMES,
                   .palette_slot = PAL_GOLD},
    [SPR_ARROW] = {.size = SPRITE_16x8,
                   .tiles = obj_tiles + T_ARROW * 8,
                   .frame_count = 2,
                   .palette_slot = PAL_GOLD},
    [SPR_CONFETTI] = {.size = SPRITE_8x8,
                      .tiles = obj_tiles + T_CONFETTI * 8,
                      .frame_count = CONFETTI_FRAMES,
                      .palette_slot = PAL_GOLD},
    [SPR_SERVAL] = {.size = SPRITE_64x64,
                    .tiles = obj_tiles + T_SERVAL * 8,
                    .frame_count = SERVAL_FRAMES,
                    .palette_slot = PAL_SERVAL,
                    .origin_x = 8,
                    .origin_y = 8},
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_TILE] = &sprites[SPR_TILE],     [SPR_GLYPH] = &sprites[SPR_GLYPH],
    [SPR_ARROW] = &sprites[SPR_ARROW],   [SPR_CONFETTI] = &sprites[SPR_CONFETTI],
    [SPR_SERVAL] = &sprites[SPR_SERVAL],
};

const SpriteGroup sprite_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

static void build_sprites(void) {
    u32* out = obj_tiles;
    // T_TILE: the empty slot, the letters, the squashed blank tile, the ring.
    canvas_begin(32, 32);
    slot();
    out = canvas_pack(out);
    for (int letter = 1; letter <= 26; letter++) {
        canvas_begin(32, 32);
        face(TILE_LEFT, TILE_TOP, TILE_SIZE);
        for (u32 i = 0; i < sizeof coat_spots / sizeof coat_spots[0]; i++)
            badge(&coat_spots[i], C_SPOTS);
        badge(&dot, C_DOT);
        // The letter in the middle of the tile, 5 pixels from its sides and
        // 4 from its top and bottom, pressed in: shade a pixel below it.
        for (int y = 0; y < FONT_H; y++)
            for (int x = 0; x < FONT_W; x++)
                if (font_ink(letter, x, y))
                    put(TILE_LEFT + 5 + x, TILE_TOP + 5 + y, C_SHADE);
        for (int y = 0; y < FONT_H; y++)
            for (int x = 0; x < FONT_W; x++)
                if (font_ink(letter, x, y))
                    put(TILE_LEFT + 5 + x, TILE_TOP + 4 + y, C_INK);
        out = canvas_pack(out);
    }
    for (int s = 0; s < SQUASH_HEIGHTS; s++) {
        canvas_begin(32, 32);
        squashed_tile(squash_heights[s]);
        out = canvas_pack(out);
    }
    canvas_begin(32, 32);
    cursor_ring();
    out = canvas_pack(out);
    // T_GLYPH: frame 0 empty, then the letters and '!' in C_FACE (gold or
    // red, by the palette), lit along the top and deeper in the lower half,
    // outlined.
    canvas_begin(16, 16);
    out = canvas_pack(out);
    for (int g = 1; g < GLYPH_FRAMES; g++) {
        canvas_begin(16, 16);
        for (int y = 0; y < FONT_H; y++)
            for (int x = 0; x < FONT_W; x++)
                if (font_ink(g, x, y))
                    put(2 + x, 1 + y, y < 2 ? C_LIGHT : y >= FONT_H - 5 ? C_SHADE : C_FACE);
        outline(C_OUTLINE);
        out = canvas_pack(out);
    }
    // T_ARROW: up, then down.
    for (int f = 0; f < 2; f++) {
        canvas_begin(16, 8);
        for (int y = 0; y < 5; y++)
            for (int x = 0; arrow[y][x]; x++)
                if (arrow[y][x] == '#')
                    put(4 + x, 1 + (f == ARROW_UP ? y : 4 - y), C_FACE);
        outline(C_OUTLINE);
        out = canvas_pack(out);
    }
    // T_CONFETTI: a scrap in each color and shape, in the middle of its 8x8.
    for (int f = 0; f < CONFETTI_FRAMES; f++) {
        canvas_begin(8, 8);
        draw(confetti[f], 5, 2, 2, ".#");
        for (int y = 2; y < 7; y++)
            for (int x = 2; x < 7; x++)
                if (get(x, y))
                    put(x, y, C_CONFETTI + f);
        out = canvas_pack(out);
    }
    // T_SERVAL
    for (int f = 0; f < SERVAL_FRAMES; f++) {
        serval_frame(f);
        out = canvas_pack(out);
    }
}

// --- The background ------------------------------------------------------------
//
// Spots on plum, after the sketch's background: soft ovals on two layers that
// repeat (MAP_LAYER_WRAP). The far one, a pattern 80 pixels square, is on
// background 3 and moves at half the camera's speed; the near one, 96x64, on
// background 1, moves with the camera. The script drifts the camera, so the
// two slide past each other. Background 1 is in front of sprites drawn
// normally: the script draws every sprite with SPRITE_ABOVE_FOREGROUND.
// Background 2, the playfield, stays empty, as a playfield would clamp the
// camera to its map (map.h, camera_set). Behind them, the backdrop: a
// gradient, a color per line (backdrop_colors, below).

enum { BG_FAR = 1, BG_NEAR }; // their colors in background palette 0
static const u16 spot_colors[16] = {0, COLOR_RGB(68, 38, 104), COLOR_RGB(82, 46, 116)};

// A spot: an oval of radii (rx, ry) about (x, y), in pixels.
typedef struct {
    u8 x, y, rx, ry;
} Spot;

// A serval's coat rather than polka dots: spots of mixed sizes, some in
// pairs that run into each other, scattered unevenly.
static const Spot far_spots[] = {{22, 20, 11, 7}, {31, 26, 6, 5}, {60, 50, 7, 10}, {40, 70, 6, 5},
                                 {70, 12, 4, 4},  {8, 52, 5, 6},  {52, 30, 3, 3},  {14, 76, 3, 2}};
static const Spot near_spots[] = {
    {30, 40, 6, 8}, {36, 47, 4, 4}, {70, 14, 9, 5}, {80, 50, 3, 3}, {10, 8, 3, 4}};

#define FAR_W 5 // metatiles: 80x80 pixels
#define FAR_H 5
#define NEAR_W 6 // 96x64
#define NEAR_H 4
#define FAR_TILES (FAR_W * FAR_H * 4)
#define NEAR_TILES (NEAR_W * NEAR_H * 4)
#define T_FAR 1 // tile 0 is blank
#define T_NEAR (T_FAR + FAR_TILES)
#define BG_TILES (T_NEAR + NEAR_TILES)

static u32 bg_tiles[BG_TILES * 8] SERVAL_EWRAM_BSS;
static Metatile far_metatiles[FAR_W * FAR_H] SERVAL_EWRAM_BSS;
static Metatile near_metatiles[NEAR_W * NEAR_H] SERVAL_EWRAM_BSS;
static u16 far_cells[FAR_W * FAR_H] SERVAL_EWRAM_BSS;
static u16 near_cells[NEAR_W * NEAR_H] SERVAL_EWRAM_BSS;

// The distance from pixel a's centre to b along an axis that wraps every
// `size` pixels, in half pixels (both inside the pattern, so no division).
static int wrapped(int a, int b, int size) {
    int d = 2 * a + 1 - 2 * b;
    if (d < 0)
        d = -d;
    return d > size ? 2 * size - d : d;
}

// Draws a pattern of w x h metatiles: its spots in `color`, then its tiles
// from `first` on and a metatile per 16x16 block, which its cells name in
// order.
static void build_spots(const Spot* spots, int count, int w, int h, int color, int first,
                        Metatile* metatiles, u16* cells) {
    int pw = w * 16, ph = h * 16;
    canvas_begin(pw, ph);
    for (int i = 0; i < count; i++) {
        const Spot* s = &spots[i];
        int rx2 = s->rx * s->rx, ry2 = s->ry * s->ry;
        for (int y = 0; y < ph; y++) {
            int dy = wrapped(y, s->y, ph);
            for (int x = 0; x < pw; x++) {
                int dx = wrapped(x, s->x, pw);
                if (dx * dx * ry2 + dy * dy * rx2 <= 4 * rx2 * ry2)
                    put(x, y, color);
            }
        }
    }
    canvas_pack(bg_tiles + first * 8);
    // canvas_pack laid the tiles out row by row, pw / 8 to a row.
    int row = pw / 8;
    for (int my = 0; my < h; my++) {
        for (int mx = 0; mx < w; mx++) {
            int t = first + 2 * my * row + 2 * mx;
            Metatile* m = &metatiles[my * w + mx];
            m->se[0] = MAP_SE(t, 0, 0);
            m->se[1] = MAP_SE(t + 1, 0, 0);
            m->se[2] = MAP_SE(t + row, 0, 0);
            m->se[3] = MAP_SE(t + row + 1, 0, 0);
            m->collision = MAP_EMPTY;
            cells[my * w + mx] = (u16)(my * w + mx);
        }
    }
}

const Tileset spots_tileset = {
    .tiles = bg_tiles, .tile_count = BG_TILES, .palettes = spot_colors, .palette_count = 1};

const MapLayer spots_far_layer = {
    .width = FAR_W,
    .height = FAR_H,
    .cells = far_cells,
    .metatiles = far_metatiles,
    .metatile_count = FAR_W * FAR_H,
    .bg = 3,
    .flags = MAP_LAYER_WRAP,
    .scroll_factor = FX_ONE / 2,
};

const MapLayer spots_near_layer = {
    .width = NEAR_W,
    .height = NEAR_H,
    .cells = near_cells,
    .metatiles = near_metatiles,
    .metatile_count = NEAR_W * NEAR_H,
    .bg = 1,
    .flags = MAP_LAYER_WRAP,
};

// Behind the spots, a dusk: the backdrop runs from deep plum at the top of
// the screen to a warmer, redder plum at the bottom, a color per line
// (raster_backdrop, main.c). A color has 32 levels per channel, so a gentle
// gradient would show bands: a channel between two levels takes the upper
// one on some lines and the lower one on others, by an ordered pattern of
// four lines, and the eye mixes them.
Color backdrop_colors[SCREEN_H] SERVAL_EWRAM_BSS;

static void build_backdrop(void) {
    static const u8 top[3] = {30, 16, 56}, bottom[3] = {62, 26, 74};
    static const u8 threshold[4] = {1, 5, 3, 7}; // of the 8 steps between levels
    for (int y = 0; y < SCREEN_H; y++) {
        int c[3];
        for (int i = 0; i < 3; i++) {
            int v = top[i] + (bottom[i] - top[i]) * y / (SCREEN_H - 1);
            c[i] = (v & ~7) + ((v & 7) >= threshold[y & 3] ? 8 : 0);
        }
        backdrop_colors[y] = COLOR_RGB(c[0], c[1], c[2]);
    }
}

void art_build(void) {
    build_sprites();
    build_spots(far_spots, sizeof far_spots / sizeof far_spots[0], FAR_W, FAR_H, BG_FAR, T_FAR,
                far_metatiles, far_cells);
    build_spots(near_spots, sizeof near_spots / sizeof near_spots[0], NEAR_W, NEAR_H, BG_NEAR,
                T_NEAR, near_metatiles, near_cells);
    build_backdrop();
}
