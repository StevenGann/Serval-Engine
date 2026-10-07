// The splash screen's logo: a serval's head beside "SERVAL" over "ENGINE" in
// chunky letters, the mark-and-wordmark design (docs/core-api.md). Everything
// is drawn when the splash starts (as the examples draw their art at boot)
// from small ASCII pictures and a few rules (outline, bevel, a smoothed 2x
// scale, light and shade) on a canvas that is cut into 4bpp tiles in
// charblock 1. Pictures use one character per pixel: '.' is transparent,
// letters pick colors from a key string. "made with" goes above (the text
// layer's font, through the splash's grey bank).

#include "splash_art.h"

#include "internal.h"
#include "serval/text.h"

#include <tonc.h>

// --- The canvas ---------------------------------------------------------------

// Pixels, one byte each (a color index), cut into tiles row by row. Tiles
// with nothing drawn cost no VRAM (pack() skips them). EWRAM: it is not on
// any hot path. The canvas is the lockup's size, shown at (SCREEN_COL,
// SCREEN_ROW) of the map with "made with" above.
#define CANVAS_COLS 18
#define CANVAS_ROWS 6
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

// --- The logo ---------------------------------------------------------------------------
//
// Its colors, in the order the pictures' key string lists them: K outline
// (and pupils), O fur, L light fur, S shaded fur, D dark markings, W white,
// I inner ear, E eye, N nose; then the letters' first line (G, light H, dark
// J) and second (C, shade B); and X, the shade under the letters.

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
// cream, a deep warm brown outline.
static const u16 colors[16] = {
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
// clang-format on

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

// The lockup: the head at the left of the canvas, the name 8 pixels to its
// right, both vertically centered on each other, with light and shade. The
// head gets its outline, light from above on the ear rims and the top of
// the forehead, and shade inside the ears where they meet the fur; the
// letters' bottom row is a shade darker than their bevel. Last comes the
// shade under the chin: it borrows the second line's shade, a letter color,
// which the wordmark's outline and the letters' shade would otherwise take
// for letters.
#define HEAD_X 1
#define HEAD_Y 1
static void logo(void) {
    picture(&face, HEAD_X, HEAD_Y, head_keys);
    picture(&ear_left, HEAD_X, HEAD_Y, head_keys);
    picture(&ear_right, HEAD_X + 19, HEAD_Y, head_keys);
    outline(MK_K, 1);
    rim(MK_O, MK_K, MK_L);
    shade(MK_I, MK_I, MK_N, 1, 1u << MK_O);
    wordmark(45, 2);
    shade(MK_G, MK_B, MK_X, 1, 1u << MK_K);
    shade(MK_W, MK_W, MK_B, 1, (1u << MK_O) | (1u << MK_S) | (1u << MK_K));
}

// --- Into VRAM ----------------------------------------------------------------------------

// Cuts the canvas into tiles, row by row, from the start of charblock 1,
// and puts each on the map at the canvas's place on screen. Tiles with
// nothing drawn are skipped: the map keeps its empty tile 0 there. (The
// charblock has room for the whole canvas, so nothing is ever left out.)
static void pack(void) {
    u32* block = (u32*)&tile_mem[SERVAL_SPLASH_ART_CHARBLOCK][0];
    u32 base = SE_PALBANK(SERVAL_SPLASH_ART_BANK) + SERVAL_SPLASH_ART_CHARBLOCK * 512;
    u32 used = 0;
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
            if (!any)
                continue;
            for (int y = 0; y < 8; y++)
                block[used * 8 + (u32)y] = words[y];
            se_mem[31][(SCREEN_ROW + ty) * 32 + SCREEN_COL + tx] = (SCR_ENTRY)(base + used);
            used++;
        }
    }
}

void serval_splash_art_draw(u32 text_bank) {
    canvas_clear();
    logo();
    pack();
    for (u32 c = 1; c < 16; c++)
        pal_bg_bank[SERVAL_SPLASH_ART_BANK][c] = colors[c];
    serval_text_print_bank(TEXT_COL, TEXT_ROW, "made with", text_bank);
}
