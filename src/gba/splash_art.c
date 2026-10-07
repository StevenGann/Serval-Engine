// The splash screen's logo: a serval's head and "SERVAL" over "ENGINE" in
// chunky letters, the mark-and-wordmark design, in four variations while the
// final one is picked (docs/open-questions.md). Everything is drawn when the
// splash starts (as the examples draw their art at boot) from small ASCII
// pictures and a few rules (outline, bevel, a smoothed 2x scale, the finish's
// light and shade) on a canvas that is cut into 4bpp tiles in charblock 1.
// Pictures use one character per pixel: '.' is transparent, letters pick
// colors from a key string. Every style says "made with" above (the text
// layer's font, through the splash's grey bank).
//
// Style 0 is the head as chosen: bigger eyes, whisker dots, enormous ears
// (the left upright and notched, the right swivelled out), beside the name.
// The others are that drawing with one decision changed, as small patches
// over it: 1 the expression, 2 the markings, 3 the finish. Once one is
// picked the other three go, with the cycling (splash.c).

#include "splash_art.h"

#include "../core/splash_internal.h"
#include "../core/warn.h"
#include "internal.h"
#include "serval/screen.h"
#include "serval/text.h"

#include <tonc.h>

// --- The canvas ---------------------------------------------------------------

// Pixels, one byte each (a color index), cut into tiles row by row. Tiles
// with nothing drawn cost no VRAM (pack() skips them). EWRAM: it is not on
// any hot path. Every style draws the same lockup on the same canvas, shown
// at the same place on screen, with "made with" above.
#define CANVAS_COLS 18
#define CANVAS_ROWS 6
#define CANVAS_TILES (CANVAS_COLS * CANVAS_ROWS)
#define CANVAS_W (CANVAS_COLS * 8)
#define CANVAS_H (CANVAS_ROWS * 8)
#define SCREEN_COL 6
#define SCREEN_ROW 8
#define TEXT_COL 10
#define TEXT_ROW 6
static u8 canvas[CANVAS_W * CANVAS_H] SERVAL_EWRAM_BSS;

// The rules below work in place: during a pass, a changed pixel carries
// MARK so that it still counts as what it was.
#define MARK 0x80

static void canvas_clear(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 0;
}

static int get(int x, int y) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H)
        return 0;
    return canvas[y * CANVAS_W + x] & ~MARK;
}

// The pixel as drawn before the current pass (0 if it was changed by it).
static int get_unmarked(int x, int y) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H)
        return 0;
    u8 c = canvas[y * CANVAS_W + x];
    return (c & MARK) ? 0 : c;
}

static void put(int x, int y, int c) {
    if (x >= 0 && y >= 0 && x < CANVAS_W && y < CANVAS_H)
        canvas[y * CANVAS_W + x] = (u8)c;
}

static void unmark(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] &= (u8)~MARK;
}

// A picture: `w` characters per row, `h` rows. A character at index k of
// `keys` is color k, anything else is transparent. With `mirror`, the rows
// are the left half: the picture is drawn again flipped to the right of
// itself.
typedef struct {
    const char* rows;
    u8 w, h;
    bool mirror;
} Picture;

static void picture(const Picture* p, int x, int y, const char* keys) {
    for (int py = 0; py < p->h; py++) {
        for (int px = 0; px < p->w; px++) {
            char ch = p->rows[py * p->w + px];
            int c = 0;
            for (int k = 1; keys[k]; k++)
                if (keys[k] == ch)
                    c = k;
            if (!c)
                continue;
            put(x + px, y + py, c);
            if (p->mirror)
                put(x + 2 * p->w - 1 - px, y + py, c);
        }
    }
}

// Gives everything of color `shape_min` or above an edge: pixels below
// `shape_min` (transparent, or a background drawn in lower colors) next to
// (also diagonally) such a pixel get `color`.
static void outline(int color, int shape_min) {
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            if (get(x, y) >= shape_min)
                continue;
            bool near = false;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if ((dx || dy) && get_unmarked(x + dx, y + dy) >= shape_min)
                        near = true;
            if (near)
                put(x, y, color | MARK);
        }
    }
    unmark();
}

// Shades a flat shape for a chunky look: pixels of `base` with nothing drawn
// below or to the right turn `dark`, those with nothing above or to the left
// turn `light`.
static void bevel(int base, int dark, int light) {
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            if (canvas[y * CANVAS_W + x] != base)
                continue;
            if (!get(x, y + 1) || !get(x + 1, y))
                put(x, y, dark | MARK);
            else if (!get(x, y - 1) || !get(x - 1, y))
                put(x, y, light | MARK);
        }
    }
    unmark();
}

// Light from above: pixels of `base` right under an `edge` pixel turn
// `light`. (No marks: the pass never changes an `edge` pixel.)
static void rim(int base, int edge, int light) {
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            if (get(x, y) == base && get(x, y - 1) == edge)
                put(x, y, light);
}

// Shade at the foot of a shape: pixels of a color from `base_min` to
// `base_max` with a color of `below` (a bit per color index) within `depth`
// pixels under them turn `dark`.
static void shade(int base_min, int base_max, int dark, int depth, u32 below) {
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            int c = get(x, y);
            if (c < base_min || c > base_max)
                continue;
            bool foot = false;
            for (int d = 1; d <= depth; d++)
                if (below & (1u << get_unmarked(x, y + d)))
                    foot = true;
            if (foot)
                put(x, y, dark | MARK);
        }
    }
    unmark();
}

// --- Lettering --------------------------------------------------------------------
//
// A capital alphabet with just the letters the name needs, drawn as pictures
// ('#' is ink), left-aligned in their cells so a glyph's width is its
// rightmost inked column: 7x10, two-pixel strokes, the L a column narrower
// so "SERVAL" and "ENGINE" come out the same width. Scale 2 doubles a glyph
// with rounded corners and smoothed diagonals (each source pixel's 2x2 block
// takes the color of its neighbors where two agree: the "EPX" rule), so the
// chunky letters don't look like doubled pixels.

typedef struct {
    const char* letters; // the glyph order
    const char* rows;    // w * h characters per glyph
    int w, h;
} Font;

// clang-format off
static const Font chunky = {
    "SERVALNGI",
    // S
    ".#####." "#######" "##...##" "##....." ".#####."
    "..#####" ".....##" "##...##" "#######" ".#####."
    // E
    "#######" "#######" "##....." "##....." "#####.."
    "#####.." "##....." "##....." "#######" "#######"
    // R
    "######." "#######" "##...##" "##...##" "#######"
    "######." "##.##.." "##..##." "##...##" "##...##"
    // V
    "##...##" "##...##" "##...##" "##...##" "##...##"
    "##...##" ".##.##." ".##.##." "..###.." "...#..."
    // A
    "..###.." ".#####." "##...##" "##...##" "#######"
    "#######" "##...##" "##...##" "##...##" "##...##"
    // L
    "##....." "##....." "##....." "##....." "##....."
    "##....." "##....." "##....." "######." "######."
    // N
    "##...##" "###..##" "###..##" "####.##" "##.#.##"
    "##.####" "##..###" "##..###" "##...##" "##...##"
    // G
    ".#####." "#######" "##...##" "##....." "##....."
    "##.####" "##.####" "##...##" "#######" ".#####."
    // I
    "######." "######." "..##..." "..##..." "..##..."
    "..##..." "..##..." "..##..." "######." "######.",
    7, 10};
// clang-format on

static int glyph_index(const Font* f, char c) {
    for (int g = 0; f->letters[g]; g++)
        if (f->letters[g] == c)
            return g;
    return -1;
}

static bool inked(const Font* f, int g, int x, int y) {
    if (x < 0 || y < 0 || x >= f->w || y >= f->h)
        return false;
    return f->rows[(g * f->h + y) * f->w + x] == '#';
}

static int glyph_width(const Font* f, int g) {
    int w = 0;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++)
            if (inked(f, g, x, y))
                w = x + 1 > w ? x + 1 : w;
    return w;
}

// Draws a glyph at (x, y) in `color` at scale 2 (see above). Returns its
// advance.
static int glyph(const Font* f, char c, int x, int y, int color) {
    int g = glyph_index(f, c);
    if (g < 0)
        return 0;
    for (int sy = 0; sy < f->h; sy++) {
        for (int sx = 0; sx < f->w; sx++) {
            bool p = inked(f, g, sx, sy);
            bool up = inked(f, g, sx, sy - 1), down = inked(f, g, sx, sy + 1);
            bool left = inked(f, g, sx - 1, sy), right = inked(f, g, sx + 1, sy);
            bool q[4] = {p, p, p, p}; // top-left, top-right, bottom-left, bottom-right
            if (up != down && left != right) {
                q[0] = left == up ? left : p;
                q[1] = up == right ? right : p;
                q[2] = down == left ? left : p;
                q[3] = right == down ? right : p;
            }
            for (int k = 0; k < 4; k++)
                if (q[k])
                    put(x + sx * 2 + k % 2, y + sy * 2 + k / 2, color);
        }
    }
    return glyph_width(f, g) * 2;
}

// Draws a word at scale 2 with `tracking` pixels between letters.
static void label(const Font* f, const char* s, int x, int y, int color, int tracking) {
    for (; *s; s++)
        x += glyph(f, *s, x, y, color) + tracking;
}

// --- The family ------------------------------------------------------------------------
//
// The colors every style has, in the order the pictures' key string lists
// them: K outline (and pupils), O fur, L light fur, S shaded fur, D dark
// markings, W white, I inner ear, E eye, N nose; then the letters' first
// line (G, light H, dark J) and second (C, shade B); and X, the finish's
// shade under the letters (style 3; unused by the others).

enum {
    MK_K = 1,
    MK_O,
    MK_L,
    MK_S,
    MK_D,
    MK_W,
    MK_I,
    MK_E,
    MK_N,
    MK_G,
    MK_H,
    MK_J,
    MK_C,
    MK_B,
    MK_X
};
static const char head_keys[] = ".KOLSDWIEN";

// Daylight: a gold serval with green eyes, "SERVAL" in gold, "ENGINE" in
// cream, a near-black outline.
static const u16 colors_day[16] = {
    0,
    COLOR_RGB(24, 16, 8),     // K
    COLOR_RGB(231, 156, 57),  // O
    COLOR_RGB(255, 214, 123), // L
    COLOR_RGB(181, 107, 41),  // S
    COLOR_RGB(57, 33, 16),    // D
    COLOR_RGB(255, 247, 231), // W
    COLOR_RGB(247, 198, 173), // I
    COLOR_RGB(165, 231, 82),  // E
    COLOR_RGB(206, 107, 107), // N
    COLOR_RGB(247, 181, 49),  // G
    COLOR_RGB(255, 231, 132), // H
    COLOR_RGB(165, 107, 24),  // J
    COLOR_RGB(255, 247, 214), // C
    COLOR_RGB(181, 165, 132), // B
    0,                        // X (unused)
};

// The markings' daylight: the same with a darker nose.
static const u16 colors_marked[16] = {
    0,
    COLOR_RGB(24, 16, 8),     // K
    COLOR_RGB(231, 156, 57),  // O
    COLOR_RGB(255, 214, 123), // L
    COLOR_RGB(181, 107, 41),  // S
    COLOR_RGB(57, 33, 16),    // D
    COLOR_RGB(255, 247, 231), // W
    COLOR_RGB(247, 198, 173), // I
    COLOR_RGB(165, 231, 82),  // E
    COLOR_RGB(140, 57, 57),   // N
    COLOR_RGB(247, 181, 49),  // G
    COLOR_RGB(255, 231, 132), // H
    COLOR_RGB(165, 107, 24),  // J
    COLOR_RGB(255, 247, 214), // C
    COLOR_RGB(181, 165, 132), // B
    0,                        // X (unused)
};

// The finish's daylight: a deep warm brown outline instead of black, and the
// shade under the letters.
static const u16 colors_warm[16] = {
    0,
    COLOR_RGB(90, 41, 24),    // K
    COLOR_RGB(231, 156, 57),  // O
    COLOR_RGB(255, 214, 123), // L
    COLOR_RGB(181, 107, 41),  // S
    COLOR_RGB(57, 33, 16),    // D
    COLOR_RGB(255, 247, 231), // W
    COLOR_RGB(247, 198, 173), // I
    COLOR_RGB(165, 231, 82),  // E
    COLOR_RGB(206, 107, 107), // N
    COLOR_RGB(247, 181, 49),  // G
    COLOR_RGB(255, 231, 132), // H
    COLOR_RGB(165, 107, 24),  // J
    COLOR_RGB(255, 247, 214), // C
    COLOR_RGB(181, 165, 132), // B
    COLOR_RGB(123, 74, 24),   // X
};

// The head, 36x44, front on: the face, with big eyes and whisker dots, is
// mirrored; the ears are drawn over it, the left one upright and notched,
// the right one swivelled out as if listening, a dark strip hinting at its
// back.
// clang-format off
static const Picture face = {
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    ".................."
    "............OOOOOO"
    "..........OOOOOOOO"
    "........OOOOOOOOOO"
    "......OOOOOOOOOOOD"
    "..OOOOOOOOOOOODOOD"
    ".OOOOOOOOOOOOODOOD"
    "SOOOOOOOOOOOOODOOD"
    "SOOOOOOOOOOOOODOOD"
    "SOOOOOOOOOOOOODOOO"
    "SOOOOOOOOOOOOOOOOO"
    "SOOOOOLLLLLLOOOOOO"
    "SOOOOOOEEEEOOOOOOO"
    "SOOOOOEWWKKEOOOOOO"
    "SOOOOOEWEKKEOOOOOO"
    "SOOOOOEEEKKEOOOOOO"
    "SOOOOOOEEEEOOOOOOO"
    "SOOOOOOLLLLOOOOOOO"
    "SOOOOOOOOOOOOOOOOO"
    "SSOODOOOOOOOOOOOOO"
    ".SOOOOOODOOOOWWWWW"
    ".SOOOOOOOOOOWWDWWW"
    ".SOOODOOOOOWWWWWNN"
    ".SSOOOOOOOOWWDWWNN"
    "..SOOOOOOOOWWWWWWD"
    "..SOOODOOOOWWWDWDW"
    "..SSOOOOOOOWWWWDWW"
    "...SSOOOOOOWWWWWWW"
    "....SSOOOOOOWWWWWW"
    ".....SSOOOOOOOWWWW"
    "......SSSOOOOOOWWW"
    ".........SSSSSSSWW",
    18, 44, true};
static const Picture ear_left = {
    ".....OOO........"
    "....OIIOO......."
    "....OIIIO......."
    "...OIIIIOO......"
    "...OIIIIIO......"
    "....OIIIIIO....."
    ".....OIIIIO....."
    "...OOIIIIIIO...."
    "..OIIIIIIIIO...."
    "..OIWIIIIIIIO..."
    ".OIIWWIIIIIIO..."
    ".OIIWWIIIIIIIO.."
    "OIIIWWIIIIIIIIO."
    "OIIIIWIIIIIIIIIO"
    "OOIIIIIIIIIIIIIO"
    "OOOIIIIIIIIIIIOO"
    "OOOOIIIIIIIIIOOO"
    "OOOOOIIIIIIIOOOO"
    "OOOOOOOIIIOOOOOO",
    16, 19, false};
static const Picture ear_right = {
    "............OOOO."
    "...........OIIDOO"
    "...........OIIIDO"
    "..........OIIIIDO"
    ".........OIIIIIDO"
    ".........OIIIIIDO"
    "........OIIIIIIDO"
    ".......OIIIIIIIDO"
    ".......OIIIIIIIDO"
    "......OIIIIIIIIOO"
    ".....OIIIIIIIIIO."
    "....OIIIIIIIIIIO."
    "....OIIIIIIIIIOO."
    "...OIIIIIIIIIOOO."
    "..OOIIIIIIIOOOOO."
    "..OOOIIIIIOOOOOO."
    "...OOOOOOOOOOOOO.",
    17, 17, false};

// The expression (style 1), over the face: a knowing look. Both eyes get a
// dark lid and glance to the right, toward the name (the patch covers both
// eyes, so they aren't mirrored), and the right corner of the mouth lifts.
static const Picture eyes_knowing = {
    ".DDDD.............DDDD."
    "EEWEEE............EEWEEE"
    "EEEKKE............EEEKKE"
    "EEEKKE............EEEKKE",
    24, 4, false};
static const Picture mouth_smirk = {
    "..D"
    "DDW"
    ".W.",
    3, 3, false};

// The markings (style 2), over the right ear: its back is black with a bold
// white bar across it, as a serval's is.
static const Picture ear_back = {
    "..D."
    "..DD"
    ".DDD"
    "DDDD"
    "WWWW"
    "WWWW"
    "DDDD"
    ".DDD"
    "..DD"
    "...D",
    4, 10, false};
// clang-format on

// The markings' spots, in the face's coordinates (mirrored): a short outer
// forehead stripe below the ear, spots down the temple past the eye, and
// two more on the cheek.
static const u8 marking_spots[][2] = {
    {11, 19}, {11, 20}, {4, 22}, {2, 25}, {3, 28}, {9, 35}, {7, 40},
};

// "SERVAL" over "ENGINE" with the first line's top-left corner at (x, y):
// 92 x 44 pixels, beveled, then outlined. Drawn after the head and its
// outline (the letters' colors are the highest, so their outline leaves the
// head alone).
static void wordmark(int x, int y) {
    label(&chunky, "SERVAL", x, y, MK_G, 2);
    label(&chunky, "ENGINE", x, y + 24, MK_C, 2);
    bevel(MK_G, MK_J, MK_H);
    bevel(MK_C, MK_B, MK_C);
    outline(MK_K, MK_G);
}

// The lockup every style shares: the head at the left of the canvas, its
// outline, the name 8 pixels to its right, both vertically centered on each
// other. Patches over the head go between head() and lockup().
#define HEAD_X 1
#define HEAD_Y 1
static void head(void) {
    picture(&face, HEAD_X, HEAD_Y, head_keys);
    picture(&ear_left, HEAD_X, HEAD_Y, head_keys);
    picture(&ear_right, HEAD_X + 19, HEAD_Y, head_keys);
}

static void lockup(void) {
    outline(MK_K, 1);
    wordmark(45, 2);
}

// Style 0: the head as chosen.
static void build_chosen(void) {
    head();
    lockup();
}

// Style 1, the expression: the knowing look.
static void build_expression(void) {
    head();
    picture(&eyes_knowing, HEAD_X + 6, HEAD_Y + 24, head_keys);
    picture(&mouth_smirk, HEAD_X + 19, HEAD_Y + 36, head_keys);
    lockup();
}

// Style 2, the markings: the ear's back, a short outer forehead stripe,
// spots down the temples and more on the cheeks (and a darker nose, in the
// palette).
static void build_markings(void) {
    head();
    picture(&ear_back, HEAD_X + 31, HEAD_Y, head_keys);
    for (u32 i = 0; i < sizeof marking_spots / sizeof marking_spots[0]; i++) {
        int x = marking_spots[i][0], y = marking_spots[i][1];
        put(HEAD_X + x, HEAD_Y + y, MK_D);
        put(HEAD_X + 2 * face.w - 1 - x, HEAD_Y + y, MK_D);
    }
    lockup();
}

// Style 3, the finish: a rendering pass over the chosen drawing. Light from
// above on the ear rims and the top of the forehead, shade inside the ears
// where they meet the head and under the chin, the letters' bottom edge a
// shade darker than their bevel (and the outline a deep warm brown, in the
// palette).
static void build_finish(void) {
    head();
    outline(MK_K, 1);
    rim(MK_O, MK_K, MK_L);
    shade(MK_I, MK_I, MK_N, 1, 1u << MK_O);
    wordmark(45, 2);
    shade(MK_G, MK_B, MK_X, 1, 1u << MK_K);
    // Last: the chin borrows the second line's shade, a letter color, which
    // the wordmark's outline and the pass above would otherwise take for
    // letters.
    shade(MK_W, MK_W, MK_B, 1, (1u << MK_O) | (1u << MK_S) | (1u << MK_K));
}

// --- The styles -----------------------------------------------------------------------------

typedef struct {
    void (*build)(void);
    const u16* colors;
} Style;

static const Style styles[SERVAL_SPLASH_STYLES] = {
    {build_chosen, colors_day},
    {build_expression, colors_day},
    {build_markings, colors_marked},
    {build_finish, colors_warm},
};

// Which tile in charblock 1 each canvas tile became (BLANK: nothing drawn,
// shown as the map's empty tile 0).
#define BLANK 0xFF
static u8 tile_of[SERVAL_SPLASH_STYLES][CANVAS_TILES] SERVAL_EWRAM_BSS;

// Cuts the canvas into tiles, row by row, at a style's place in charblock 1,
// skipping tiles with nothing drawn. Tiles past the style's budget are left
// blank (a warning in debug builds).
static void pack(u32 style) {
    u32* block = (u32*)&tile_mem[SERVAL_SPLASH_ART_CHARBLOCK][style * SERVAL_SPLASH_ART_TILES];
    u32 used = 0, dropped = 0;
    for (int ty = 0; ty < CANVAS_ROWS; ty++) {
        for (int tx = 0; tx < CANVAS_COLS; tx++) {
            u32 words[8], any = 0;
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)get(tx * 8 + x, ty * 8 + y) << (4 * x);
                words[y] = word;
                any |= word;
            }
            u8* slot = &tile_of[style][ty * CANVAS_COLS + tx];
            if (!any) {
                *slot = BLANK;
            } else if (used < SERVAL_SPLASH_ART_TILES) {
                for (int y = 0; y < 8; y++)
                    block[used * 8 + (u32)y] = words[y];
                *slot = (u8)used++;
            } else {
                *slot = BLANK;
                dropped++;
            }
        }
    }
    if (dropped)
        SERVAL_WARN("splash: logo style %u needs %u tiles over its %u; the rest is blank",
                    (unsigned)style, (unsigned)dropped, (unsigned)SERVAL_SPLASH_ART_TILES);
}

void serval_splash_art_load(void) {
    for (u32 s = 0; s < SERVAL_SPLASH_STYLES; s++) {
        const Style* st = &styles[s];
        canvas_clear();
        st->build();
        pack(s);
        for (u32 c = 1; c < 16; c++)
            pal_bg_bank[SERVAL_SPLASH_ART_FIRST_BANK + s][c] = st->colors[c];
    }
}

void serval_splash_art_show(u32 style, u32 text_bank) {
    memset32(&se_mem[31][0], 0, SERVAL_SPLASH_ART_ROWS * 32 * sizeof(SCR_ENTRY) / 4);
    u32 base = SE_PALBANK(SERVAL_SPLASH_ART_FIRST_BANK + style) +
               SERVAL_SPLASH_ART_CHARBLOCK * 512 + style * SERVAL_SPLASH_ART_TILES;
    for (u32 ty = 0; ty < CANVAS_ROWS; ty++) {
        for (u32 tx = 0; tx < CANVAS_COLS; tx++) {
            u8 t = tile_of[style][ty * CANVAS_COLS + tx];
            if (t != BLANK)
                se_mem[31][(SCREEN_ROW + ty) * 32 + SCREEN_COL + tx] = (SCR_ENTRY)(base + t);
        }
    }
    serval_text_print_bank(TEXT_COL, TEXT_ROW, "made with", text_bank);
}
