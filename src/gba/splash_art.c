// The splash screen's logo: a serval's head and "SERVAL" over "ENGINE" in
// chunky letters, the mark-and-wordmark design, in four variations while the
// final one is picked (docs/open-questions.md). Everything is drawn when the
// splash starts (as the examples draw their art at boot) from small ASCII
// pictures and a few rules (outline, bevel, a smoothed 2x scale, a drop
// shadow) on a canvas that is cut into 4bpp tiles in charblock 1. Pictures
// use one character per pixel: '.' is transparent, letters pick colors from
// a key string. Every style says "made with" above (the text layer's font,
// through the splash's grey bank).
//
// The styles share the lettering and the drawing rules and differ along one
// axis each: 0 is the baseline, 1 stacks the head above the name, 2 redraws
// the head with more character, 3 recolors the baseline for the night.
// Once one is picked the other three go, with the cycling (splash.c).

#include "splash_art.h"

#include "../core/splash_internal.h"
#include "../core/warn.h"
#include "internal.h"
#include "serval/screen.h"
#include "serval/text.h"

#include <tonc.h>

// --- The canvas ---------------------------------------------------------------

// Pixels, one byte each (a color index), cut into tiles row by row. Tiles
// with nothing drawn cost no VRAM (pack() skips them), so a canvas may be
// larger than a style's tile budget. EWRAM: it is not on any hot path.
#define CANVAS_TILES_MAX 176 // 13 x 13 with room to spare
static u8 canvas[CANVAS_TILES_MAX * 64] SERVAL_EWRAM_BSS;
static int canvas_w, canvas_h;

// The rules below work in place: during a pass, a changed pixel carries
// MARK so that it still counts as what it was.
#define MARK 0x80

static void canvas_begin(int w, int h) {
    canvas_w = w;
    canvas_h = h;
    for (int i = 0; i < w * h; i++)
        canvas[i] = 0;
}

static int get(int x, int y) {
    if (x < 0 || y < 0 || x >= canvas_w || y >= canvas_h)
        return 0;
    return canvas[y * canvas_w + x] & ~MARK;
}

// The pixel as drawn before the current pass (0 if it was changed by it).
static int get_unmarked(int x, int y) {
    if (x < 0 || y < 0 || x >= canvas_w || y >= canvas_h)
        return 0;
    u8 c = canvas[y * canvas_w + x];
    return (c & MARK) ? 0 : c;
}

static void put(int x, int y, int c) {
    if (x >= 0 && y >= 0 && x < canvas_w && y < canvas_h)
        canvas[y * canvas_w + x] = (u8)c;
}

static void unmark(void) {
    for (int i = 0; i < canvas_w * canvas_h; i++)
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
    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
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
    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
            if (canvas[y * canvas_w + x] != base)
                continue;
            if (!get(x, y + 1) || !get(x + 1, y))
                put(x, y, dark | MARK);
            else if (!get(x, y - 1) || !get(x - 1, y))
                put(x, y, light | MARK);
        }
    }
    unmark();
}

// A drop shadow: empty pixels (dx, dy) away from anything drawn get `color`.
static void shadow(int color, int dx, int dy) {
    for (int y = 0; y < canvas_h; y++)
        for (int x = 0; x < canvas_w; x++)
            if (!get(x, y) && get_unmarked(x - dx, y - dy))
                put(x, y, color | MARK);
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
// markings, W white, I inner ear, E eye, Z the drop shadow (style 3), N nose;
// then the letters' first line (G, light H, dark J) and second (C, shade B).

enum {
    MK_K = 1,
    MK_O,
    MK_L,
    MK_S,
    MK_D,
    MK_W,
    MK_I,
    MK_E,
    MK_Z,
    MK_N,
    MK_G,
    MK_H,
    MK_J,
    MK_C,
    MK_B
};
static const char head_keys[] = ".KOLSDWIEZN";

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
    0,                        // Z (unused)
    COLOR_RGB(206, 107, 107), // N
    COLOR_RGB(247, 181, 49),  // G
    COLOR_RGB(255, 231, 132), // H
    COLOR_RGB(165, 107, 24),  // J
    COLOR_RGB(255, 247, 214), // C
    COLOR_RGB(181, 165, 132), // B
};

// Night: a slate-blue outline and a soft blue shadow, a warmer gold on the
// head, "SERVAL" in cream and "ENGINE" in ice blue.
static const u16 colors_night[16] = {
    0,
    COLOR_RGB(24, 33, 57),    // K
    COLOR_RGB(239, 165, 49),  // O
    COLOR_RGB(255, 222, 132), // L
    COLOR_RGB(189, 107, 33),  // S
    COLOR_RGB(66, 41, 24),    // D
    COLOR_RGB(255, 247, 231), // W
    COLOR_RGB(247, 198, 173), // I
    COLOR_RGB(173, 231, 82),  // E
    COLOR_RGB(33, 49, 90),    // Z
    COLOR_RGB(214, 115, 115), // N
    COLOR_RGB(255, 243, 214), // G
    COLOR_RGB(255, 255, 247), // H
    COLOR_RGB(198, 181, 140), // J
    COLOR_RGB(189, 214, 239), // C
    COLOR_RGB(115, 140, 181), // B
};

// The head, front on: tall rounded ears, forehead stripes, cheek spots, a
// white muzzle. Drawn as its left half and mirrored.
// clang-format off
static const Picture head = {
    "....OOO........."
    "...OIIOO........"
    "...OIIIO........"
    "..OIIIIOO......."
    "..OIIIIIO......."
    "..OIIIIIIO......"
    ".OIIIIIIIO......"
    ".OIWIIIIIIO....."
    ".OIWWIIIIIO....."
    ".OIWWIIIIIIO...."
    "OIIWWIIIIIIO..OO"
    "OOIIWIIIIIIIOOOO"
    "OOOIIIIIIIIOOOOD"
    "OOOOIIIIIIOOODOD"
    "OOOOOIIIIOOODOOD"
    "OOOOOOOOOOODOOOD"
    "OOOOOOOOOOODOOOD"
    "SOOOOOOOOOODOOOO"
    "SOOOOOOOOOOOOOOO"
    "SOOOOOLLLLOOOOOO"
    "SOOOOOEEEOOOOOOO"
    "SOOOOEWKKEOOOOOO"
    "SOOOOEEKKEOOOOOO"
    "SOOOOOEEEOOOOOOO"
    "SOOOOOLLLLOOOOOO"
    "SOOOOOOOOOOOOOOO"
    "SSOODOOOOOOOOOOO"
    ".SOOOOOOOOOWWWWW"
    ".SOOOOODOOWWWWWW"
    ".SOOOOOOOOWWWWNN"
    ".SSOOOOOOOWWWWNN"
    "..SOOODOOOWWWWWD"
    "..SOOOOOOOWWDWDW"
    "..SSOOOOOOWWWDWW"
    "...SSOOOOOWWWWWW"
    "....SSOOOOOWWWWW"
    ".....SSOOOOOWWWW"
    "......SSOOOOOWWW"
    ".......SSOOOOWWW"
    ".........SSSSSSW",
    16, 40, true};

// The same head at 46x46 for the stacked layout.
static const Picture head_big = {
    ".....OOOO.............."
    "....OIIIOO............."
    "....OIIIIOO............"
    "...OIIIIIIO............"
    "...OIIIIIIOO..........."
    "...OIIIIIIIO..........."
    "..OIIIIIIIIOO.........."
    "..OIIIIIIIIIO.........."
    "..OIWIIIIIIIOO........."
    ".OIIWWIIIIIIIO........."
    ".OIIWWIIIIIIIOO........"
    ".OIIWWWIIIIIIIO........"
    "OIIIWWWIIIIIIIOO......."
    "OIIIIWWIIIIIIIIOO...OOO"
    "OOIIIIWIIIIIIIIOOOOOOOO"
    "OOOIIIIIIIIIIIOOOOOOOOD"
    "OOOOIIIIIIIIIOOOOOODOOD"
    "OOOOOIIIIIIIOOOOOODOOOD"
    "OOOOOOOIIIOOOOOOOODOOOD"
    "SOOOOOOOOOOOOOOOOODOOOD"
    "SOOOOOOOOOOOOOOOOODOOOD"
    "SOOOOOOOOOOOOOOOOOOOOOO"
    "SOOOOOOOLLLLLLOOOOOOOOO"
    "SOOOOOOOOEEEEOOOOOOOOOO"
    "SOOOOOOOEEEEEEOOOOOOOOO"
    "SOOOOOOEWWEKKKEOOOOOOOO"
    "SOOOOOOEWEEKKKEOOOOOOOO"
    "SOOOOOOEEEEKKKEOOOOOOOO"
    "SOOOOOOOEEEEEEOOOOOOOOO"
    "SOOOOOOOOEEEEOOOOOOOOOO"
    "SOOOOOOOLLLLLLOOOOOOOOO"
    "SSOOOODOOOOOOOOOOOOOOOO"
    ".SOOOOOOOOOOOOOOWWWWWWW"
    ".SOOOOOOOOODOOOWWWWWWWW"
    ".SOOOOOOOOOOOOWWWWWWNNN"
    ".SSOOOOOOOOOOOWWWWWWNNN"
    "..SOOOOOOOOOOOWWWWWWWNN"
    "..SOOOOOODOOOOWWWWWWWWD"
    "..SSOOOOOOOOOOWWWWWWWWD"
    "...SOOOOOOOOOOWWWWDWWDW"
    "...SSOOOOOOOOOWWWWWDDWW"
    "....SSOOOOOOOOWWWWWWWWW"
    ".....SSOOOOOOOOWWWWWWWW"
    "......SSSOOOOOOOWWWWWWW"
    "........SSSOOOOOOOWWWWW"
    "...........SSSSSSSSSSWW",
    23, 46, true};

// The head with more character (style 2): the face, with bigger eyes and
// whisker dots, is mirrored; the ears are drawn over it, the left one
// upright and notched, the right one swivelled out as if listening.
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
#define WORDMARK_W 92
static void wordmark(int x, int y) {
    label(&chunky, "SERVAL", x, y, MK_G, 2);
    label(&chunky, "ENGINE", x, y + 24, MK_C, 2);
    bevel(MK_G, MK_J, MK_H);
    bevel(MK_C, MK_B, MK_C);
    outline(MK_K, MK_G);
}

// Style 0, the baseline: the 32x40 head beside the name, both vertically
// centered on each other, 7 pixels apart. (Style 3 draws the same lockup a
// pixel higher and adds the shadow below and to the right.)
static void lockup(int y, bool with_shadow) {
    picture(&head, 2, y + 2, head_keys);
    outline(MK_K, 1);
    wordmark(43, y);
    if (with_shadow)
        shadow(MK_Z, 2, 2);
}

static void build_baseline(void) {
    lockup(2, false);
}

// Style 1, the composition: the 46x46 head centered above the name.
static void build_stacked(void) {
    picture(&head_big, 29, 2, head_keys);
    outline(MK_K, 1);
    wordmark(6, 58);
}

// Style 2, the head: the 36x44 character head beside the name.
static void build_character(void) {
    picture(&face, 1, 1, head_keys);
    picture(&ear_left, 1, 1, head_keys);
    picture(&ear_right, 20, 1, head_keys);
    outline(MK_K, 1);
    wordmark(45, 2);
}

// Style 3, the finish: the baseline in the night palette with a drop shadow.
static void build_night(void) {
    lockup(1, true);
}

// --- The styles -----------------------------------------------------------------------------

typedef struct {
    void (*build)(void);
    const u16* colors;
    u8 cols, rows;         // the canvas, in tiles
    u8 col, row;           // where it goes on screen
    u8 text_col, text_row; // "made with"
} Style;

static const Style styles[SERVAL_SPLASH_STYLES] = {
    {build_baseline, colors_day, 18, 6, 6, 8, 10, 6},
    {build_stacked, colors_day, 13, 13, 8, 4, 10, 2},
    {build_character, colors_day, 18, 6, 6, 8, 10, 6},
    {build_night, colors_night, 18, 6, 6, 8, 10, 6},
};

// Which tile in charblock 1 each canvas tile became (BLANK: nothing drawn,
// shown as the map's empty tile 0).
#define BLANK 0xFF
static u8 tile_of[SERVAL_SPLASH_STYLES][CANVAS_TILES_MAX] SERVAL_EWRAM_BSS;

// Cuts the canvas into tiles, row by row, at a style's place in charblock 1,
// skipping tiles with nothing drawn. Tiles past the style's budget are left
// blank (a warning in debug builds).
static void pack(u32 style) {
    u32* block = (u32*)&tile_mem[SERVAL_SPLASH_ART_CHARBLOCK][style * SERVAL_SPLASH_ART_TILES];
    u32 used = 0, dropped = 0;
    for (int ty = 0; ty < canvas_h / 8; ty++) {
        for (int tx = 0; tx < canvas_w / 8; tx++) {
            u32 words[8], any = 0;
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)get(tx * 8 + x, ty * 8 + y) << (4 * x);
                words[y] = word;
                any |= word;
            }
            u8* slot = &tile_of[style][ty * (canvas_w / 8) + tx];
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
        if (st->cols * st->rows > CANVAS_TILES_MAX) {
            SERVAL_WARN("splash: logo style %u's canvas (%u tiles) is over CANVAS_TILES_MAX (%u)",
                        (unsigned)s, (unsigned)(st->cols * st->rows), (unsigned)CANVAS_TILES_MAX);
            continue;
        }
        canvas_begin(st->cols * 8, st->rows * 8);
        st->build();
        pack(s);
        for (u32 c = 1; c < 16; c++)
            pal_bg_bank[SERVAL_SPLASH_ART_FIRST_BANK + s][c] = st->colors[c];
    }
}

void serval_splash_art_show(u32 style, u32 text_bank) {
    const Style* st = &styles[style];
    memset32(&se_mem[31][0], 0, SERVAL_SPLASH_ART_ROWS * 32 * sizeof(SCR_ENTRY) / 4);
    u32 base = SE_PALBANK(SERVAL_SPLASH_ART_FIRST_BANK + style) +
               SERVAL_SPLASH_ART_CHARBLOCK * 512 + style * SERVAL_SPLASH_ART_TILES;
    for (u32 ty = 0; ty < st->rows; ty++) {
        for (u32 tx = 0; tx < st->cols; tx++) {
            u8 t = tile_of[style][ty * st->cols + tx];
            if (t != BLANK)
                se_mem[31][(st->row + ty) * 32 + st->col + tx] = (SCR_ENTRY)(base + t);
        }
    }
    serval_text_print_bank(st->text_col, st->text_row, "made with", text_bank);
}
