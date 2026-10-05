// Graphics: every sprite and background tile, built when the game boots
// (art_build) from small ASCII pictures and a few drawing rules, into EWRAM
// buffers that sprite_group_load and tileset_load copy to VRAM. Pictures use
// one character per pixel: '.' is transparent, hex digits are palette colors,
// '#' is "the shape" (colored by the code that converts it).
//
// How the cards are made
// ----------------------
// A card is 32x48 pixels. 52 different faces as whole sprites would take 52 x
// 24 tiles = 39 KB, more than the 32 KB of sprite VRAM, before the back, the
// flip animation or anything else. So a card on screen is composed of up to
// four hardware sprites that share tiles between all cards:
//
//   - the base (SPR_CARD, 32x64 with the card in rows 8-55): a blank face, a
//     court card's face with a gold frame, or the back; plus squashed widths
//     of the face and back for the flip (the engine rotates sprites but can't
//     scale them, so the flip is an animation: 8 frames, 8 KB);
//   - the index (SPR_INDEX, 8x16): rank over a small suit, top-left, and the
//     same flipped H+V bottom-right (52 frames of 2 tiles: 3.3 KB);
//   - the middle: a big suit (SPR_PIP 16x16) on number cards, a chunkier one
//     (SPR_ACE 32x32) on aces, an emblem (SPR_COURT) on jacks, queens and
//     kings, which are printed on golden paper (a frame of their own).
//
// That's 582 tiles (18 KB) for all sprites, cards included: the colored
// variants (gold-outlined cards, shadows, greyed buttons, gold and red digits
// and letters) are the same tiles drawn with another palette
// (SPRITE_PALETTE). The cost is
// paid in hardware sprites (4 per face-up card, 128 in all) and, for a tilted
// card, in the per-scanline sprite budget: a rotated sprite takes the
// double-size box and twice the cycles per pixel, so table.c tilts at most a
// few cards at once. The alternatives: drawing cards into background tiles
// (cheap in sprites, but cards could only sit on the 8-pixel grid, not slide,
// tilt or overlap freely), or composing each dealt card's face into its own
// VRAM tiles at runtime (one sprite per card, 24 tiles each: about 40 cards on
// screen; the engine has no API to write sprite tiles after loading a group).
// Rotating a composed card means rotating each piece about the card's center
// (table.c); a metasprite asset type that the engine rotates as a whole would
// make that, and the per-card bookkeeping, unnecessary.

#include "game.h"

// --- Palettes ----------------------------------------------------------------

// PAL_* are in game.h: table.c and game.c draw with some of them.

// Card colors: 1 outline, 2 paper, 3 paper shade, 4-5 red, 6 ink, 7-8 gold,
// 9-B the back's violet, C white, D ink highlight, E red highlight, F the
// court cards' golden paper.
#define CARD_COLORS(outline)                                                                       \
    {0,                                                                                            \
     outline,                                                                                      \
     COLOR_RGB(252, 248, 236),                                                                     \
     COLOR_RGB(214, 204, 186),                                                                     \
     COLOR_RGB(232, 52, 72),                                                                       \
     COLOR_RGB(150, 24, 52),                                                                       \
     COLOR_RGB(36, 36, 60),                                                                        \
     COLOR_RGB(252, 206, 80),                                                                      \
     COLOR_RGB(196, 124, 36),                                                                      \
     COLOR_RGB(70, 34, 128),                                                                       \
     COLOR_RGB(116, 62, 188),                                                                      \
     COLOR_RGB(176, 128, 246),                                                                     \
     COLOR_RGB(255, 255, 255),                                                                     \
     COLOR_RGB(104, 108, 150),                                                                     \
     COLOR_RGB(255, 150, 160),                                                                     \
     COLOR_RGB(255, 228, 160)}
#define SHADOW COLOR_RGB(10, 14, 24)
#define DIGIT_COLORS(light, base, dark) {0, COLOR_RGB(20, 12, 30), light, base, dark}
#define LETTER_COLORS(outline, light, base, dark) {0, outline, light, base, dark}

static const u16 palettes[PALETTE_COUNT][16] = {
    [PAL_CARD] = CARD_COLORS(COLOR_RGB(30, 22, 46)),
    [PAL_GLOW] = CARD_COLORS(COLOR_RGB(255, 222, 70)),
    [PAL_SHADOW] = {0, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW,
                    SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW},
    // 1 outline, 2-3 stripes; then base, dark, light for blue (10), red
    // (50), black (100) and violet (500).
    [PAL_CHIP] = {0, COLOR_RGB(18, 14, 28), COLOR_RGB(250, 250, 246), COLOR_RGB(190, 190, 200),
                  COLOR_RGB(56, 120, 232), COLOR_RGB(28, 58, 150), COLOR_RGB(130, 186, 255),
                  COLOR_RGB(226, 48, 60), COLOR_RGB(136, 20, 40), COLOR_RGB(255, 132, 132),
                  COLOR_RGB(44, 42, 60), COLOR_RGB(18, 16, 28), COLOR_RGB(104, 100, 136),
                  COLOR_RGB(176, 82, 230), COLOR_RGB(96, 36, 148), COLOR_RGB(230, 172, 255)},
    [PAL_DIGIT] =
        DIGIT_COLORS(COLOR_RGB(255, 255, 255), COLOR_RGB(206, 214, 236), COLOR_RGB(10, 8, 18)),
    [PAL_DIGIT_GOLD] =
        DIGIT_COLORS(COLOR_RGB(255, 246, 160), COLOR_RGB(250, 186, 50), COLOR_RGB(10, 8, 18)),
    [PAL_DIGIT_RED] =
        DIGIT_COLORS(COLOR_RGB(255, 170, 170), COLOR_RGB(240, 70, 80), COLOR_RGB(10, 8, 18)),
    [PAL_LETTER_GOLD] = LETTER_COLORS(COLOR_RGB(48, 20, 8), COLOR_RGB(255, 248, 176),
                                      COLOR_RGB(252, 196, 56), COLOR_RGB(204, 112, 24)),
    [PAL_LETTER_RED] = LETTER_COLORS(COLOR_RGB(44, 6, 18), COLOR_RGB(255, 176, 176),
                                     COLOR_RGB(238, 60, 72), COLOR_RGB(156, 20, 44)),
    [PAL_LETTER_SILVER] = LETTER_COLORS(COLOR_RGB(22, 20, 44), COLOR_RGB(255, 255, 255),
                                        COLOR_RGB(204, 210, 230), COLOR_RGB(124, 130, 166)),
    // 1 outline, 2 label, 3 label shadow; then base, dark, light for green,
    // red, orange and blue.
    [PAL_BUTTON] = {0, COLOR_RGB(18, 12, 28), COLOR_RGB(255, 255, 255), COLOR_RGB(30, 20, 40),
                    COLOR_RGB(52, 176, 92), COLOR_RGB(24, 100, 52), COLOR_RGB(130, 232, 150),
                    COLOR_RGB(228, 58, 70), COLOR_RGB(134, 24, 44), COLOR_RGB(255, 140, 140),
                    COLOR_RGB(246, 150, 40), COLOR_RGB(160, 80, 20), COLOR_RGB(255, 210, 120),
                    COLOR_RGB(56, 132, 236), COLOR_RGB(28, 66, 150), COLOR_RGB(140, 196, 255)},
    [PAL_BUTTON_OFF] = {0, COLOR_RGB(18, 12, 28), COLOR_RGB(150, 150, 160), COLOR_RGB(40, 40, 50),
                        COLOR_RGB(84, 86, 98), COLOR_RGB(52, 54, 64), COLOR_RGB(112, 114, 126),
                        COLOR_RGB(84, 86, 98), COLOR_RGB(52, 54, 64), COLOR_RGB(112, 114, 126),
                        COLOR_RGB(84, 86, 98), COLOR_RGB(52, 54, 64), COLOR_RGB(112, 114, 126),
                        COLOR_RGB(84, 86, 98), COLOR_RGB(52, 54, 64), COLOR_RGB(112, 114, 126)},
    // Sparks and confetti: 1 white, 2 yellow, 3 gold, 4 pink, 5 cyan, 6 green,
    // 7 violet.
    [PAL_FX] = {0, COLOR_RGB(255, 255, 255), COLOR_RGB(255, 240, 120), COLOR_RGB(246, 170, 40),
                COLOR_RGB(255, 100, 170), COLOR_RGB(90, 230, 255), COLOR_RGB(110, 240, 130),
                COLOR_RGB(190, 120, 255)},
};

// --- Tile buffers ------------------------------------------------------------

#define T_CARD 0                               // CF_COUNT frames of 32 tiles
#define T_NARROW_TOP (T_CARD + CF_COUNT * 32)  // 2 frames of 8
#define T_NARROW_BOTTOM (T_NARROW_TOP + 2 * 8) // 2 frames of 4
#define T_EDGE_TOP (T_NARROW_BOTTOM + 2 * 4)   // 4 tiles
#define T_EDGE_BOTTOM (T_EDGE_TOP + 4)         // 2 tiles
#define T_INDEX (T_EDGE_BOTTOM + 2)            // 52 frames of 2
#define T_PIP (T_INDEX + 52 * 2)               // 4 frames of 4
#define T_ACE (T_PIP + 4 * 4)                  // 4 frames of 16
#define T_COURT (T_ACE + 4 * 16)               // 6 frames of 4
#define T_CHIP (T_COURT + 6 * 4)               // CHIP_KINDS frames of 4
#define T_DIGIT (T_CHIP + CHIP_KINDS * 4)      // DIGIT_COUNT frames of 2
#define T_LETTER (T_DIGIT + DIGIT_COUNT * 2)
#define LETTER_COUNT 23
#define T_BUTTON (T_LETTER + LETTER_COUNT * 4) // BTN_COUNT frames of 8
#define T_SPARK (T_BUTTON + BTN_COUNT * 8)
#define T_CONFETTI (T_SPARK + SPARK_FRAMES)
#define OBJ_TILES (T_CONFETTI + CONFETTI_COLORS)

static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

// Background: tile 0 blank, then the swirl's 16x16 tiles (128 pixels square,
// repeating), then the ribbon layer's.
#define SWIRL_SIZE 128
#define SWIRL_TILES ((SWIRL_SIZE / 8) * (SWIRL_SIZE / 8))
#define BT_SWIRL 1
#define BT_RIBBON (BT_SWIRL + SWIRL_TILES)
#define BG_TILES (BT_RIBBON + SWIRL_TILES)

static u32 bg_tiles[BG_TILES * 8] SERVAL_EWRAM_BSS;

// --- A canvas to draw on, then cut into tiles --------------------------------

// Scratch buffers for building art at boot: EWRAM, since IWRAM is too small
// for them in Debug builds and they are not on any hot path.
static u8 canvas[64 * 64] SERVAL_EWRAM_BSS;
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

// Cuts the canvas into 4bpp tiles at `out`, row by row (1D mapping, as
// sprite frames are laid out). Returns the end.
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

static int hex(char c) {
    if (c >= '1' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

// Draws an ASCII picture at (x, y): hex digits as colors; '#' as `ink`; the
// letters r, s, l as the suit colors passed (base, dark, light).
static void blit(const char* const* rows, int w, int h, int x, int y, int ink, const u8* suit) {
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            char c = rows[py][px];
            int color = c == '#'   ? ink
                        : c == 'r' ? suit[0]
                        : c == 's' ? suit[1]
                        : c == 'l' ? suit[2]
                                   : hex(c);
            if (color)
                put(x + px, y + py, color);
        }
    }
}

// Shades a flat shape for a chunky, beveled look: pixels of color `base`
// with nothing of the shape below or to the right turn `dark`, those with
// nothing above or to the left turn `light`.
static void bevel(int base, int dark, int light) {
    static u8 copy[64 * 64] SERVAL_EWRAM_BSS;
    for (int i = 0; i < canvas_w * canvas_h; i++)
        copy[i] = canvas[i];
    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
            if (copy[y * canvas_w + x] != base)
                continue;
            bool down = y + 1 < canvas_h && copy[(y + 1) * canvas_w + x];
            bool right = x + 1 < canvas_w && copy[y * canvas_w + x + 1];
            bool up = y > 0 && copy[(y - 1) * canvas_w + x];
            bool left = x > 0 && copy[y * canvas_w + x - 1];
            if (!down || !right)
                put(x, y, dark);
            else if (!up || !left)
                put(x, y, light);
        }
    }
}

// Outlines everything drawn: transparent pixels next to (or, with
// `diagonal`, also diagonally next to) a drawn one get `color`.
static void outline(int color, bool diagonal) {
    static u8 copy[64 * 64] SERVAL_EWRAM_BSS;
    for (int i = 0; i < canvas_w * canvas_h; i++)
        copy[i] = canvas[i];
    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
            if (copy[y * canvas_w + x])
                continue;
            bool near = false;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if ((dx && dy && !diagonal) || (!dx && !dy))
                        continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < canvas_w && ny < canvas_h &&
                        copy[ny * canvas_w + nx])
                        near = true;
                }
            }
            if (near)
                put(x, y, color);
        }
    }
}

// --- Cards -------------------------------------------------------------------

enum { ART_FACE, ART_COURT, ART_BACK };
static u8 card_art[CARD_W * CARD_H];

// The card's rounded corners: inside the card or not.
static bool card_inside(int x, int y) {
    if (x < 0 || y < 0 || x >= CARD_W || y >= CARD_H)
        return false;
    int dx = int_max(0, int_max(3 - x, x - (CARD_W - 4)));
    int dy = int_max(0, int_max(3 - y, y - (CARD_H - 4)));
    return dx * dx + dy * dy <= 10;
}

// Draws a full-size card into card_art: an outlined rounded rectangle of
// paper (lit at the top-left, shaded at the bottom-right; golden for court
// cards), or the back: a violet lattice inside a paper border, around a gold
// diamond.
static void draw_card_art(int kind) {
    for (int y = 0; y < CARD_H; y++) {
        for (int x = 0; x < CARD_W; x++) {
            int c = 0;
            if (!card_inside(x, y)) {
                c = 0;
            } else if (!card_inside(x - 1, y) || !card_inside(x + 1, y) || !card_inside(x, y - 1) ||
                       !card_inside(x, y + 1)) {
                c = 1; // the outline
            } else if (kind == ART_BACK) {
                int ix = x - 3, iy = y - 3; // inside the paper border
                if (ix < 0 || iy < 0 || ix > CARD_W - 7 || iy > CARD_H - 7) {
                    c = 2;
                } else if (ix == 0 || iy == 0 || ix == CARD_W - 7 || iy == CARD_H - 7) {
                    c = 7; // a gold line around the pattern
                } else {
                    int d = int_abs(2 * x - (CARD_W - 1)) + int_abs(2 * y - (CARD_H - 1));
                    bool a = (x + y) % 6 == 0, b = (x - y + 60) % 6 == 0;
                    c = d <= 3    ? 12 // the diamond's sparkle
                        : d <= 7  ? 11 //
                        : d <= 12 ? 7  // gold
                        : d <= 15 ? 1  // its dark rim
                        : a && b  ? 11 // the lattice's crossings
                        : a || b  ? 10 //
                                  : 9;
                }
            } else {
                bool edge_rb =
                    !card_inside(x + 2, y) || !card_inside(x, y + 2) || !card_inside(x, y + 3);
                bool edge_lt = !card_inside(x - 2, y) || !card_inside(x, y - 2);
                // Court cards are printed on golden paper.
                c = edge_rb ? 3 : edge_lt ? 12 : kind == ART_COURT ? 15 : 2;
            }
            card_art[y * CARD_W + x] = (u8)c;
        }
    }
}

// The card in card_art squashed to `width` pixels (nearest neighbor),
// centered, in a frame `frame_w` x `frame_h` showing the card's rows from
// `first_row` on (-8 for the 32x64 frames: the card in rows 8-55).
static u32* pack_card(u32* out, int width, int frame_w, int first_row, int frame_h) {
    canvas_begin(frame_w, frame_h);
    int left = (frame_w - width) / 2;
    for (int sx = 0; sx < width; sx++) {
        int src = width > 1 ? sx * (CARD_W - 1) / (width - 1) : 0;
        for (int y = 0; y < CARD_H; y++)
            put(left + sx, y - first_row, card_art[y * CARD_W + src]);
    }
    return canvas_pack(out);
}

// The narrow flip steps: no sprite is 16x64 or 8x64, so they are a 32-pixel
// tall top over a 16-pixel bottom.
static void pack_narrow(int width, int frame) {
    int w = width > 8 ? 16 : 8;
    u32* top = obj_tiles + (w == 16 ? T_NARROW_TOP + frame * 8 : T_EDGE_TOP) * 8;
    u32* bottom = obj_tiles + (w == 16 ? T_NARROW_BOTTOM + frame * 4 : T_EDGE_BOTTOM) * 8;
    pack_card(top, width, w, 0, 32);
    pack_card(bottom, width, w, 32, 16);
}

// Ranks, 6x9 and bold, shared by the card indexes and the big digits ('0'
// to '9', then A, J, Q, K, +, -, /).
#define GLYPH_W 6
#define GLYPH_H 9
static const char* const glyphs[][GLYPH_H] = {
    {".####.", "##..##", "##..##", "##.###", "###.##", "##..##", "##..##", "##..##", ".####."},
    {"..##..", ".###..", "####..", "..##..", "..##..", "..##..", "..##..", "..##..", "######"},
    {".####.", "##..##", "....##", "....##", "...##.", "..##..", ".##...", "##....", "######"},
    {".####.", "##..##", "....##", "....##", "..###.", "....##", "....##", "##..##", ".####."},
    {"...##.", "..###.", ".####.", "##.##.", "##.##.", "######", "...##.", "...##.", "...##."},
    {"######", "##....", "##....", "#####.", "....##", "....##", "....##", "##..##", ".####."},
    {"..###.", ".##...", "##....", "#####.", "##..##", "##..##", "##..##", "##..##", ".####."},
    {"######", "....##", "....##", "...##.", "...##.", "..##..", "..##..", "..##..", "..##.."},
    {".####.", "##..##", "##..##", "##..##", ".####.", "##..##", "##..##", "##..##", ".####."},
    {".####.", "##..##", "##..##", "##..##", ".#####", "....##", "....##", "...##.", ".###.."},
    {"..##..", ".####.", "##..##", "##..##", "##..##", "######", "##..##", "##..##", "##..##"},
    {"..####", "....##", "....##", "....##", "....##", "....##", "##..##", "##..##", ".####."},
    {".####.", "##..##", "##..##", "##..##", "##..##", "##.###", "##..##", ".###.#", "...#.."},
    {"##..##", "##.##.", "####..", "###...", "###...", "####..", "##.##.", "##..##", "##..##"},
    {"......", "......", "..##..", "..##..", "######", "######", "..##..", "..##..", "......"},
    {"......", "......", "......", "......", "######", "######", "......", "......", "......"},
    {"....##", "....##", "...##.", "...##.", "..##..", ".##...", ".##...", "##....", "##...."},
    // The chip icon beside the bankroll.
    {"......", ".####.", "#.##.#", "##..##", "#....#", "##..##", "#.##.#", ".####.", "......"},
};
enum { G_A = 10, G_J, G_Q, G_K, G_PLUS, G_MINUS, G_SLASH, G_CHIP };

// "10" doesn't fit the 6-pixel glyphs: a narrower one and a zero side by side.
static const char* const ten_art[GLYPH_H] = {
    "##.####.", "##.#..#.", "##.#..#.", "##.#..#.", "##.#..#.",
    "##.#..#.", "##.#..#.", "##.#..#.", "##.####.",
};

static void blit_glyph(int g, int x, int y, int ink) {
    blit(glyphs[g], GLYPH_W, GLYPH_H, x, y, ink, 0);
}

// Small suits for the index, 7x6, in the order of SUIT_*.
static const char* const small_suits[4][6] = {
    {"...#...", "..###..", ".#####.", "#######", "#######", "..###.."},
    {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."},
    {"...#...", "..###..", ".#####.", ".#####.", "..###..", "...#..."},
    {"..###..", "..###..", "#######", "#######", "...#...", "..###.."},
};

// Big suits, 16x16 shapes ('#'), beveled when converted.
static const char* const pips[4][16] = {
    {
        "................",
        ".......##.......",
        "......####......",
        ".....######.....",
        "....########....",
        "...##########...",
        "..############..",
        ".##############.",
        "################",
        "################",
        "################",
        ".######..######.",
        "..####.##.####..",
        ".......##.......",
        "......####......",
        ".....######.....",
    },
    {
        "................",
        "................",
        "..####....####..",
        ".######..######.",
        "################",
        "################",
        "################",
        "################",
        ".##############.",
        "..############..",
        "...##########...",
        "....########....",
        ".....######.....",
        "......####......",
        ".......##.......",
        "................",
    },
    {
        "................",
        ".......##.......",
        "......####......",
        "......####......",
        ".....######.....",
        "....########....",
        "...##########...",
        "..############..",
        "..############..",
        "...##########...",
        "....########....",
        ".....######.....",
        "......####......",
        "......####......",
        ".......##.......",
        "................",
    },
    {
        "................",
        "......####......",
        ".....######.....",
        ".....######.....",
        ".....######.....",
        "..###.####.###..",
        ".#####.##.#####.",
        "################",
        "################",
        "################",
        ".#####.##.#####.",
        "..###..##..###..",
        ".......##.......",
        "......####......",
        ".....######.....",
        "................",
    },
};

// Court emblems, 16x16: crown (king), rose (queen), shield (jack). 7-8
// gold, C white, 1 outline; r, s, l the suit's base, dark and light colors.
static const char* const courts[3][16] = {
    {
        "................",
        "................",
        ".1.....11.....1.",
        "171...1771...171",
        "1771.177771.1771",
        "1777177777717771",
        "1777777777777771",
        "178rr78lr87rr871",
        "178sr78sr87sr871",
        "1788888888888881",
        "1777777777777771",
        "17C7C7C7C7C7C771",
        "1888888888888881",
        ".11111111111111.",
        "................",
        "................",
    },
    {
        "................",
        "................",
        ".....111111.....",
        "....1lrrrrs1....",
        "...1lrsrrrrs1...",
        "...1rrrsrrsr1...",
        "...1rsrrssrr1...",
        "...1srrrrrrs1...",
        "....1ssrrss1....",
        ".....111111.....",
        ".....1781.......",
        "..1111781.......",
        ".17777781.......",
        "..1111781.......",
        ".....1781.......",
        "................",
    },
    {
        "................",
        "................",
        "..111111111111..",
        "..177777777771..",
        "..18rrrrrrrr81..",
        "..18rlrrrrrr81..",
        "..18rrCrrCrr81..",
        "..18rrrCCrrr81..",
        "..18rrrrrrrr81..",
        "...18rrrrrr81...",
        "...18rrrrrr81...",
        "....18rrrr81....",
        ".....18rr81.....",
        "......1881......",
        ".......11.......",
        "................",
    },
};

// The suits' colors: base, dark, light (card palette).
static const u8 suit_red[3] = {4, 5, 14};
static const u8 suit_black[3] = {6, 1, 13};

static const u8* suit_colors(int suit) {
    return suit == SUIT_HEARTS || suit == SUIT_DIAMONDS ? suit_red : suit_black;
}

static void build_cards(void) {
    u32* out = obj_tiles + T_CARD * 8;
    draw_card_art(ART_COURT);
    out = pack_card(out, 32, 32, -8, 64); // CF_COURT
    draw_card_art(ART_FACE);
    out = pack_card(out, 32, 32, -8, 64); // CF_FACE
    out = pack_card(out, 24, 32, -8, 64);
    pack_narrow(14, NF_FACE_14);
    pack_narrow(4, 0); // the edge
    draw_card_art(ART_BACK);
    out = pack_card(out, 24, 32, -8, 64); // CF_BACK_24
    pack_card(out, 32, 32, -8, 64);       // CF_BACK
    pack_narrow(14, NF_BACK_14);

    // Indexes: rank over suit, in the suit's ink.
    out = obj_tiles + T_INDEX * 8;
    for (int rank = 1; rank <= 13; rank++) {
        for (int suit = 0; suit < 4; suit++) {
            int ink = suit_colors(suit)[0];
            canvas_begin(8, 16);
            if (rank == 10)
                blit(ten_art, 8, GLYPH_H, 0, 0, ink, 0);
            else
                blit_glyph(rank == 1 ? G_A : rank > 10 ? G_J + rank - 11 : rank, 1, 0, ink);
            blit(small_suits[suit], 7, 6, 1, 10, ink, 0);
            out = canvas_pack(out);
        }
    }

    // Big suits, and the ace's: the same shapes half as big again.
    out = obj_tiles + T_PIP * 8;
    for (int suit = 0; suit < 4; suit++) {
        const u8* c = suit_colors(suit);
        canvas_begin(16, 16);
        blit(pips[suit], 16, 16, 0, 0, c[0], 0);
        bevel(c[0], c[1], c[2]);
        out = canvas_pack(out);
    }
    for (int suit = 0; suit < 4; suit++) {
        const u8* c = suit_colors(suit);
        canvas_begin(32, 32);
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 24; x++)
                if (pips[suit][y * 2 / 3][x * 2 / 3] == '#')
                    put(4 + x, 4 + y, c[0]);
        bevel(c[0], c[1], c[2]);
        out = canvas_pack(out);
    }

    // Court emblems: red suits, then black.
    out = obj_tiles + T_COURT * 8;
    for (int color = 0; color < 2; color++) {
        for (int court = 0; court < 3; court++) {
            canvas_begin(16, 16);
            blit(courts[court], 16, 16, 0, 0, 0, color ? suit_black : suit_red);
            out = canvas_pack(out);
        }
    }
}

// --- Chips -------------------------------------------------------------------

// A chip seen from above at a slant: an ellipse (rim with white stripes, a
// lighter middle) on a three-pixel side band. Stacked 3 pixels apart they
// make a pile. Colors: chip palette, base/dark/light per denomination.
static void build_chips(void) {
    u32* out = obj_tiles + T_CHIP * 8;
    for (int kind = 0; kind < CHIP_KINDS; kind++) {
        int base = 4 + kind * 3, dark = base + 1, light = base + 2;
        canvas_begin(16, 16);
        for (int y = 0; y < 13; y++) {
            for (int x = 0; x < 16; x++) {
                // Ellipse 15 x 9 centered at (7.5, 4) in doubled coordinates.
                int X = 2 * x - 15;
                int top = X * X * 64 + (2 * y - 8) * (2 * y - 8) * 225;
                bool on_top = top <= 225 * 64;
                bool on_side = false;
                for (int d = 1; d <= 3 && !on_top; d++) {
                    int Y = 2 * (y - d) - 8;
                    on_side |= X * X * 64 + Y * Y * 225 <= 225 * 64 && 2 * y > 8;
                }
                bool stripe = int_abs(X) <= 2 || int_abs(X) >= 11;
                if (on_top)
                    put(x, y,
                        top <= 225 * 64 * 2 / 5 ? light
                        : top <= 225 * 64 / 2   ? base
                        : stripe                ? 2
                                                : base);
                else if (on_side)
                    put(x, y, stripe ? 3 : dark);
            }
        }
        // An outline around the whole chip.
        outline(1, false);
        out = canvas_pack(out);
    }
}

// --- Digits, letters, buttons ---------------------------------------------------

// Big outlined digits for the bankroll, totals and pops: the 6x9 glyphs, lit
// on top, with a dark outline and a drop shadow, in 8x16 frames.
static void build_digits(void) {
    static const u8 order[DIGIT_COUNT] = {0, 1, 2, 3,      4,       5,       6,
                                          7, 8, 9, G_PLUS, G_MINUS, G_SLASH, G_CHIP};
    u32* out = obj_tiles + T_DIGIT * 8;
    for (int i = 0; i < DIGIT_COUNT; i++) {
        canvas_begin(8, 16);
        blit_glyph(order[i], 1, 3, 3);
        for (int y = 3; y < 7; y++)
            for (int x = 0; x < 8; x++)
                if (get(x, y) == 3)
                    put(x, y, 2);
        outline(1, true);
        // The shadow: a second outline color under the bottom edge.
        for (int y = 14; y > 0; y--)
            for (int x = 0; x < 8; x++)
                if (get(x, y - 1) == 1 && !get(x, y))
                    put(x, y, 4);
        out = canvas_pack(out);
    }
}

// Banner letters: a 5x7 font at twice the size, lit on top, beveled at the
// bottom, outlined, in 16x16 frames.
static const char letters[LETTER_COUNT + 1] = "ABCDEFHIJKLNOPRSTUWY!-'";
static const char* const font5x7[LETTER_COUNT][7] = {
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // A
    {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}, // B
    {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}, // C
    {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}, // D
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}, // E
    {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}, // F
    {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // H
    {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}, // I
    {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}, // J
    {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}, // K
    {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}, // L
    {"#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"}, // N
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // O
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}, // P
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}, // R
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."}, // S
    {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}, // T
    {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // U
    {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}, // W
    {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}, // Y
    {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}, // !
    {".....", ".....", ".....", "#####", ".....", ".....", "....."}, // -
    {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}, // '
};

int letter_frame(char c) {
    for (int i = 0; i < LETTER_COUNT; i++)
        if (letters[i] == c)
            return i;
    return -1;
}

static void build_letters(void) {
    u32* out = obj_tiles + T_LETTER * 8;
    for (int i = 0; i < LETTER_COUNT; i++) {
        canvas_begin(16, 16);
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (font5x7[i][y][x] == '#')
                    for (int k = 0; k < 4; k++)
                        put(2 + 2 * x + (k & 1), 1 + 2 * y + (k >> 1), y < 3 ? 2 : 3);
        // The bottom edge of each stroke darker: a chunky bevel.
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                if (get(x, y) >= 2 && !get(x, y + 1))
                    put(x, y, 4);
        outline(1, true);
        out = canvas_pack(out);
    }
}

// Tiny 3x5 capitals for the button labels.
static const char tiny_letters[] = "ABDEHILNOPSTU";
static const char* const tiny_font[][5] = {
    {"###", "#.#", "###", "#.#", "#.#"}, {"##.", "#.#", "##.", "#.#", "##."},
    {"##.", "#.#", "#.#", "#.#", "##."}, {"###", "#..", "##.", "#..", "###"},
    {"#.#", "#.#", "###", "#.#", "#.#"}, {"###", ".#.", ".#.", ".#.", "###"},
    {"#..", "#..", "#..", "#..", "###"}, {"#.#", "###", "###", "###", "#.#"},
    {"###", "#.#", "#.#", "#.#", "###"}, {"###", "#.#", "###", "#..", "#.."},
    {"###", "#..", "###", "..#", "###"}, {"###", ".#.", ".#.", ".#.", ".#."},
    {"#.#", "#.#", "#.#", "#.#", "###"},
};

static void tiny_print(const char* s, int x, int y, int color) {
    for (; *s; s++, x += 4) {
        for (int i = 0; tiny_letters[i]; i++) {
            if (tiny_letters[i] != *s)
                continue;
            for (int py = 0; py < 5; py++)
                for (int px = 0; px < 3; px++)
                    if (tiny_font[i][py][px] == '#')
                        put(x + px, y + py, color);
        }
    }
}

// Buttons: a rounded slab with a darker side under it, lit on top, and a
// label with a drop shadow. Colors per button (button palette).
static void build_buttons(void) {
    static const char* const labels[BTN_COUNT] = {"HIT", "STAND", "DOUBLE", "SPLIT", "DEAL"};
    static const u8 colors[BTN_COUNT] = {4, 7, 10, 13, 10}; // green, red, orange, blue, orange
    u32* out = obj_tiles + T_BUTTON * 8;
    for (int b = 0; b < BTN_COUNT; b++) {
        int base = colors[b];
        canvas_begin(32, 16);
        for (int y = 1; y < 15; y++) {
            for (int x = 1; x < 31; x++) {
                bool corner = (x == 1 || x == 30) && (y == 1 || y == 14);
                if (corner)
                    continue;
                put(x, y, y >= 11 ? base + 1 : y <= 2 ? base + 2 : base);
            }
        }
        outline(1, false);
        int len = 0;
        while (labels[b][len])
            len++;
        int x = 16 - (len * 4 - 1) / 2;
        tiny_print(labels[b], x, 5, 3);
        tiny_print(labels[b], x, 4, 2);
        out = canvas_pack(out);
    }
}

// Sparks (white to gold, shrinking) and confetti (one color per frame).
static const char* const spark_art[8] = {
    "...1.......1.......2.......3....", "...1.......1.......2............",
    "..121......2.......3.......3....", "1122211..12221...2232........3..",
    "..121......2.......3............", "...1.......1.......2.......3....",
    "...1............................", "................................",
};
static const char* const confetti_art[8] = {
    "................................", "..44.......5........66..........",
    "..444.....555.......66.....77...", "...44.....555......66.....777...",
    "..........55.......66.......77..", "....................6...........",
    "................................", "................................",
};

static void build_fx(void) {
    for (int f = 0; f < SPARK_FRAMES; f++) {
        canvas_begin(8, 8);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                put(x, y, hex(spark_art[y][f * 8 + x]));
        canvas_pack(obj_tiles + (T_SPARK + f) * 8);
    }
    for (int f = 0; f < CONFETTI_COLORS; f++) {
        canvas_begin(8, 8);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                put(x, y, hex(confetti_art[y][f * 8 + x]));
        canvas_pack(obj_tiles + (T_CONFETTI + f) * 8);
    }
}

// --- Sprites -----------------------------------------------------------------

static const u8 spark_times[SPARK_FRAMES] = {4, 4, 5, 6};

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_CARD] = {.size = SPRITE_32x64,
                  .tiles = obj_tiles + T_CARD * 8,
                  .frame_count = CF_COUNT,
                  .palette_slot = PAL_CARD},
    [SPR_NARROW_TOP] = {.size = SPRITE_16x32,
                        .tiles = obj_tiles + T_NARROW_TOP * 8,
                        .frame_count = 2,
                        .palette_slot = PAL_CARD},
    [SPR_NARROW_BOTTOM] = {.size = SPRITE_16x16,
                           .tiles = obj_tiles + T_NARROW_BOTTOM * 8,
                           .frame_count = 2,
                           .palette_slot = PAL_CARD},
    [SPR_EDGE_TOP] = {.size = SPRITE_8x32,
                      .tiles = obj_tiles + T_EDGE_TOP * 8,
                      .palette_slot = PAL_CARD},
    [SPR_EDGE_BOTTOM] = {.size = SPRITE_8x16,
                         .tiles = obj_tiles + T_EDGE_BOTTOM * 8,
                         .palette_slot = PAL_CARD},
    [SPR_INDEX] = {.size = SPRITE_8x16,
                   .tiles = obj_tiles + T_INDEX * 8,
                   .frame_count = 52,
                   .palette_slot = PAL_CARD},
    [SPR_PIP] = {.size = SPRITE_16x16,
                 .tiles = obj_tiles + T_PIP * 8,
                 .frame_count = 4,
                 .palette_slot = PAL_CARD},
    [SPR_ACE] = {.size = SPRITE_32x32,
                 .tiles = obj_tiles + T_ACE * 8,
                 .frame_count = 4,
                 .palette_slot = PAL_CARD},
    [SPR_COURT] = {.size = SPRITE_16x16,
                   .tiles = obj_tiles + T_COURT * 8,
                   .frame_count = 6,
                   .palette_slot = PAL_CARD},
    [SPR_CHIP] = {.size = SPRITE_16x16,
                  .tiles = obj_tiles + T_CHIP * 8,
                  .frame_count = CHIP_KINDS,
                  .palette_slot = PAL_CHIP},
    [SPR_DIGIT] = {.size = SPRITE_8x16,
                   .tiles = obj_tiles + T_DIGIT * 8,
                   .frame_count = DIGIT_COUNT,
                   .palette_slot = PAL_DIGIT},
    [SPR_LETTER] = {.size = SPRITE_16x16,
                    .tiles = obj_tiles + T_LETTER * 8,
                    .frame_count = LETTER_COUNT,
                    .palette_slot = PAL_LETTER_GOLD},
    [SPR_BUTTON] = {.size = SPRITE_32x16,
                    .tiles = obj_tiles + T_BUTTON * 8,
                    .frame_count = BTN_COUNT,
                    .palette_slot = PAL_BUTTON},
    [SPR_SPARK] = {.size = SPRITE_8x8,
                   .tiles = obj_tiles + T_SPARK * 8,
                   .frame_count = SPARK_FRAMES,
                   .frame_times = spark_times,
                   .flags = SPRITE_ASSET_ANIM_ONCE,
                   .palette_slot = PAL_FX,
                   .origin_x = 4,
                   .origin_y = 4},
    [SPR_CONFETTI] = {.size = SPRITE_8x8,
                      .tiles = obj_tiles + T_CONFETTI * 8,
                      .frame_count = CONFETTI_COLORS,
                      .palette_slot = PAL_FX,
                      .origin_x = 4,
                      .origin_y = 4},
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_CARD] = &sprites[SPR_CARD],
    [SPR_NARROW_TOP] = &sprites[SPR_NARROW_TOP],
    [SPR_NARROW_BOTTOM] = &sprites[SPR_NARROW_BOTTOM],
    [SPR_EDGE_TOP] = &sprites[SPR_EDGE_TOP],
    [SPR_EDGE_BOTTOM] = &sprites[SPR_EDGE_BOTTOM],
    [SPR_INDEX] = &sprites[SPR_INDEX],
    [SPR_PIP] = &sprites[SPR_PIP],
    [SPR_ACE] = &sprites[SPR_ACE],
    [SPR_COURT] = &sprites[SPR_COURT],
    [SPR_CHIP] = &sprites[SPR_CHIP],
    [SPR_DIGIT] = &sprites[SPR_DIGIT],
    [SPR_LETTER] = &sprites[SPR_LETTER],
    [SPR_BUTTON] = &sprites[SPR_BUTTON],
    [SPR_SPARK] = &sprites[SPR_SPARK],
    [SPR_CONFETTI] = &sprites[SPR_CONFETTI],
};

const SpriteGroup sprite_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

// --- The swirling background -------------------------------------------------
//
// Two layers of marbled paint, each 128 pixels square and repeating
// (MAP_LAYER_WRAP): background 3 is the paint, background 2 a dithered veil
// of lighter paint over it. They are computed here from sines at boot
// (warped twice so the stripes curl), and set in motion in game.c with
// map_set_scroll: the veil moves half as far, so the two drift over each
// other. Both are MAP_LAYER_FIXED: the camera doesn't move them, nor
// anything else on screen (sprites and text). A seamless pattern needs periodic functions: every
// angle below is a whole number of turns across the 128 pixels.

static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// fx_sin's values in a table of our own: the swirl takes eight sines per
// pixel, and inline lookups are much faster than calls (the art is built
// in about a quarter of a second instead of a second).
static s16 sines[1024] SERVAL_EWRAM_BSS;
#define SIN(angle) sines[((u32)(angle) >> 6) & 1023]

// Colors 1-5 of the paint, dark to light.
static int swirl_color(int x, int y) {
    int u = x * (65536 / SWIRL_SIZE), v = y * (65536 / SWIRL_SIZE);
    int s1 = SIN(u + v);
    int s2 = SIN(v + 37 * s1);
    int a = SIN(u + 57 * s2);
    int s3 = SIN(2 * v);
    int s4 = SIN(u - 28 * s3);
    int b = SIN(v - 49 * s4);
    // Stripes along the contours of a + b, dithered into 5 shades.
    int t = SIN((a + b) * 128);
    int value = (t + 256) * 4 / 2 + (((bayer[y & 3][x & 3] * 16 - 120) * 205) >> 8);
    return int_clamp((value + 128) >> 8, 0, 4) + 1;
}

// Color 6 or transparent: a checkerboard veil (it reads as see-through) where
// another warped wave is high, sparser at its edges.
static int ribbon_color(int x, int y) {
    int u = x * (65536 / SWIRL_SIZE), v = y * (65536 / SWIRL_SIZE);
    int s1 = SIN(2 * v - u);
    int c = SIN(u + v + 70 * s1);
    if (c > 170 && ((x + y) & 1))
        return 6;
    if (c > 120 && (x & 1) && (y & 1))
        return 6;
    return 0;
}

static void build_swirl(u32* out, int (*color)(int, int)) {
    for (int ty = 0; ty < SWIRL_SIZE / 8; ty++) {
        for (int tx = 0; tx < SWIRL_SIZE / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)color(tx * 8 + x, ty * 8 + y) << (4 * x);
                *out++ = word;
            }
        }
    }
}

// One metatile per 16x16 block of each layer, built at boot.
#define SWIRL_METATILES ((SWIRL_SIZE / 16) * (SWIRL_SIZE / 16))
static Metatile swirl_metatiles[SWIRL_METATILES] SERVAL_EWRAM_BSS;
static Metatile ribbon_metatiles[SWIRL_METATILES] SERVAL_EWRAM_BSS;
static u16 swirl_cells[SWIRL_METATILES] SERVAL_EWRAM_BSS;

static void build_metatiles(Metatile* m, int first_tile) {
    int tiles_per_row = SWIRL_SIZE / 8;
    for (int my = 0; my < SWIRL_SIZE / 16; my++) {
        for (int mx = 0; mx < SWIRL_SIZE / 16; mx++) {
            int t = first_tile + my * 2 * tiles_per_row + mx * 2;
            Metatile* mt = &m[my * (SWIRL_SIZE / 16) + mx];
            mt->se[0] = MAP_SE(t, 0, 0);
            mt->se[1] = MAP_SE(t + 1, 0, 0);
            mt->se[2] = MAP_SE(t + tiles_per_row, 0, 0);
            mt->se[3] = MAP_SE(t + tiles_per_row + 1, 0, 0);
            mt->collision = MAP_EMPTY;
        }
    }
}

// Background colors: 1-5 the paint, dark to light; 6 the veil, between
// them, so it shimmers rather than shouts.
static const u16 table_bg[16] = {
    0,
    COLOR_RGB(10, 36, 34),
    COLOR_RGB(14, 48, 44),
    COLOR_RGB(18, 62, 54),
    COLOR_RGB(26, 78, 64),
    COLOR_RGB(36, 96, 76),
    COLOR_RGB(30, 84, 70),
};
static const u16 title_bg[16] = {
    0,
    COLOR_RGB(28, 10, 44),
    COLOR_RGB(44, 14, 64),
    COLOR_RGB(66, 22, 86),
    COLOR_RGB(96, 30, 106),
    COLOR_RGB(136, 46, 122),
    COLOR_RGB(110, 40, 118),
};

const Tileset table_tileset = {
    .tiles = bg_tiles, .tile_count = BG_TILES, .palettes = table_bg, .palette_count = 1};
const Tileset title_tileset = {
    .tiles = bg_tiles, .tile_count = BG_TILES, .palettes = title_bg, .palette_count = 1};

const MapLayer swirl_layer = {
    .width = SWIRL_SIZE / 16,
    .height = SWIRL_SIZE / 16,
    .cells = swirl_cells,
    .metatiles = swirl_metatiles,
    .metatile_count = SWIRL_METATILES,
    .bg = 3,
    .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP,
};

const MapLayer ribbon_layer = {
    .width = SWIRL_SIZE / 16,
    .height = SWIRL_SIZE / 16,
    .cells = swirl_cells,
    .metatiles = ribbon_metatiles,
    .metatile_count = SWIRL_METATILES,
    .bg = 2,
    .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP,
};

void art_build(void) {
    build_cards();
    build_chips();
    build_digits();
    build_letters();
    build_buttons();
    build_fx();

    for (int i = 0; i < 1024; i++)
        sines[i] = (s16)fx_sin((u16)(i << 6));
    build_swirl(bg_tiles + BT_SWIRL * 8, swirl_color); // tile 0 stays blank
    build_swirl(bg_tiles + BT_RIBBON * 8, ribbon_color);
    build_metatiles(swirl_metatiles, BT_SWIRL);
    build_metatiles(ribbon_metatiles, BT_RIBBON);
    for (int i = 0; i < SWIRL_METATILES; i++)
        swirl_cells[i] = (u16)i;
}
