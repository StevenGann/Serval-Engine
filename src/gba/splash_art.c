// The splash screen's logo: four candidate styles, drawn when the splash
// starts (as the examples draw their art at boot) from small ASCII pictures
// and a few rules (outline, bevel, a smoothed 2x scale, a row gradient) on a
// canvas that is cut into 4bpp tiles in charblock 1. Pictures use one
// character per pixel: '.' is transparent, letters pick colors from a key
// string. Every style says "made with" (the text layer's font, through the
// splash's grey bank) and "SERVAL ENGINE" (its own lettering).
//
// Once a style is picked the other three go, with the cycling (splash.c).

#include "splash_art.h"

#include "../core/splash_internal.h"
#include "internal.h"
#include "serval/screen.h"
#include "serval/text.h"

#include <tonc.h>

// --- The canvas ---------------------------------------------------------------

// 128 tiles, the most a style may take. EWRAM: it is not on any hot path.
static u8 canvas[SERVAL_SPLASH_ART_TILES * 64] SERVAL_EWRAM_BSS;
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

static void put(int x, int y, int c) {
    if (x >= 0 && y >= 0 && x < canvas_w && y < canvas_h)
        canvas[y * canvas_w + x] = (u8)c;
}

static void unmark(void) {
    for (int i = 0; i < canvas_w * canvas_h; i++)
        canvas[i] &= (u8)~MARK;
}

static void rect(int x, int y, int w, int h, int c) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            put(px, py, c);
}

// A filled circle of radius r around a pixel.
static void disc(int cx, int cy, int r, int c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r / 2)
                put(cx + dx, cy + dy, c);
}

// An isosceles triangle, apex up at (ax, ay), base `w` wide `h` rows down.
static void triangle(int ax, int ay, int w, int h, int c) {
    for (int i = 0; i < h; i++) {
        int half = (i + 1) * w / (2 * h);
        for (int dx = -half; dx <= half; dx++)
            put(ax + dx, ay + i, c);
    }
}

// Draws a picture (`w` characters per row, `h` rows) at (x, y): a character
// at index k of `keys` is color k, anything else is transparent. With
// `mirror`, the picture is the left half: it is drawn again flipped to the
// right of itself.
static void picture(const char* rows, int w, int h, int x, int y, const char* keys, bool mirror) {
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            char ch = rows[py * w + px];
            int c = 0;
            for (int k = 1; keys[k]; k++)
                if (keys[k] == ch)
                    c = k;
            if (!c)
                continue;
            put(x + px, y + py, c);
            if (mirror)
                put(x + 2 * w - 1 - px, y + py, c);
        }
    }
}

// Gives everything of color `shape_min` or above an edge: pixels below
// `shape_min` (transparent, or a background drawn in lower colors) next to
// (or, with `diagonal`, also diagonally next to) such a pixel get `color`.
static void outline(int color, bool diagonal, int shape_min) {
    for (int y = 0; y < canvas_h; y++) {
        for (int x = 0; x < canvas_w; x++) {
            if (get(x, y) >= shape_min)
                continue;
            bool near = false;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if ((dx && dy && !diagonal) || (!dx && !dy))
                        continue;
                    int n = get(x + dx, y + dy);
                    if (n >= shape_min && !(canvas[(y + dy) * canvas_w + x + dx] & MARK) &&
                        x + dx >= 0 && y + dy >= 0 && x + dx < canvas_w && y + dy < canvas_h)
                        near = true;
                }
            }
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

// Recolors pixels of `from` row by row from `y`: row y + i gets colors[i]
// (and the last color below the table).
static void gradient(int from, int y, const u8* colors, int count) {
    for (int py = y; py < canvas_h; py++) {
        int c = colors[py - y < count ? py - y : count - 1];
        for (int px = 0; px < canvas_w; px++)
            if (get(px, py) == from)
                put(px, py, c);
    }
}

// --- Lettering --------------------------------------------------------------------
//
// Two capital alphabets with just the letters the splash needs, drawn as
// pictures ('#' is ink), left-aligned in their cells so a glyph's width is
// its rightmost inked column. Scale 2 doubles a glyph with rounded corners
// and smoothed diagonals (each source pixel's 2x2 block takes the color of
// its neighbors where two agree: the "EPX" rule), so the chunky letters
// don't look like doubled pixels.

typedef struct {
    const char* letters; // the glyph order
    const char* rows;    // w * h characters per glyph
    int w, h;
} Font;

// Chunky: 7x10, two-pixel strokes.
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
    "##....." "##....." "##....." "#######" "#######"
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

// Small: 5x7, one-pixel strokes.
// clang-format off
static const Font small = {
    "SERVALNGIMDWTH",
    // S
    ".###." "#...#" "#...." ".###."
    "....#" "#...#" ".###."
    // E
    "#####" "#...." "#...." "####."
    "#...." "#...." "#####"
    // R
    "####." "#...#" "#...#" "####."
    "#.#.." "#..#." "#...#"
    // V
    "#...#" "#...#" "#...#" "#...#"
    "#...#" ".#.#." "..#.."
    // A
    ".###." "#...#" "#...#" "#####"
    "#...#" "#...#" "#...#"
    // L
    "#...." "#...." "#...." "#...."
    "#...." "#...." "#####"
    // N
    "#...#" "##..#" "##..#" "#.#.#"
    "#..##" "#..##" "#...#"
    // G
    ".###." "#...#" "#...." "#.###"
    "#...#" "#...#" ".###."
    // I
    "###.." ".#..." ".#..." ".#..."
    ".#..." ".#..." "###.."
    // M
    "#...#" "##.##" "#.#.#" "#.#.#"
    "#...#" "#...#" "#...#"
    // D
    "####." "#...#" "#...#" "#...#"
    "#...#" "#...#" "####."
    // W
    "#...#" "#...#" "#...#" "#.#.#"
    "#.#.#" "##.##" "#...#"
    // T
    "#####" "..#.." "..#.." "..#.."
    "..#.." "..#.." "..#.."
    // H
    "#...#" "#...#" "#...#" "#####"
    "#...#" "#...#" "#...#",
    5, 7};
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
    if (g < 0)
        return f->w / 2 + 1; // a space
    int w = 0;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++)
            if (inked(f, g, x, y))
                w = x + 1 > w ? x + 1 : w;
    return w;
}

// Draws a glyph at (x, y) in `color`, at scale 1 or 2, sheared to the right
// by one pixel per `slant` rows (0: upright). Returns its advance.
static int glyph(const Font* f, char c, int x, int y, int color, int scale, int slant) {
    int g = glyph_index(f, c);
    int height = f->h * scale;
    if (g < 0)
        return glyph_width(f, g) * scale;
    for (int sy = 0; sy < f->h; sy++) {
        for (int sx = 0; sx < f->w; sx++) {
            bool p = inked(f, g, sx, sy);
            if (scale == 1) {
                if (p)
                    put(x + sx + (slant ? (height - 1 - sy) / slant : 0), y + sy, color);
                continue;
            }
            bool up = inked(f, g, sx, sy - 1), down = inked(f, g, sx, sy + 1);
            bool left = inked(f, g, sx - 1, sy), right = inked(f, g, sx + 1, sy);
            bool q[4] = {p, p, p, p}; // top-left, top-right, bottom-left, bottom-right
            if (up != down && left != right) {
                q[0] = left == up ? left : p;
                q[1] = up == right ? right : p;
                q[2] = down == left ? left : p;
                q[3] = right == down ? right : p;
            }
            for (int k = 0; k < 4; k++) {
                int oy = sy * 2 + k / 2, ox = sx * 2 + k % 2;
                if (q[k])
                    put(x + ox + (slant ? (height - 1 - oy) / slant : 0), y + oy, color);
            }
        }
    }
    return glyph_width(f, g) * scale;
}

static int label_width(const Font* f, const char* s, int scale, int tracking) {
    int w = 0;
    for (; *s; s++)
        w += glyph_width(f, glyph_index(f, *s)) * scale + tracking;
    return w - tracking;
}

// Draws a string; `tracking` is the gap between letters.
static void label(const Font* f, const char* s, int x, int y, int color, int scale, int tracking,
                  int slant) {
    for (; *s; s++)
        x += glyph(f, *s, x, y, color, scale, slant) + tracking;
}

// Centered on `cx`.
static void label_centered(const Font* f, const char* s, int cx, int y, int color, int scale,
                           int tracking, int slant) {
    label(f, s, cx - label_width(f, s, scale, tracking) / 2, y, color, scale, tracking, slant);
}

// --- Style 0: mark and wordmark ----------------------------------------------------
//
// A serval's head, front on, beside "SERVAL" over "ENGINE" in chunky gold
// and cream letters: the classic studio layout. The head is drawn as its
// left half and mirrored.

// K outline, O fur, L light fur, S shaded fur, D dark markings, W white,
// I inner ear, E eye, P pupil, N nose; then the letters' gold (G, light H,
// dark J) and cream (C, shade B).
static const u16 colors_mark[16] = {
    0,
    COLOR_RGB(24, 16, 8),     // 1 K
    COLOR_RGB(231, 156, 57),  // 2 O
    COLOR_RGB(255, 214, 123), // 3 L
    COLOR_RGB(181, 107, 41),  // 4 S
    COLOR_RGB(57, 33, 16),    // 5 D
    COLOR_RGB(255, 247, 231), // 6 W
    COLOR_RGB(247, 198, 173), // 7 I
    COLOR_RGB(165, 231, 82),  // 8 E
    COLOR_RGB(16, 24, 16),    // 9 P
    COLOR_RGB(206, 107, 107), // 10 N
    COLOR_RGB(247, 181, 49),  // 11 G
    COLOR_RGB(255, 231, 132), // 12 H
    COLOR_RGB(165, 107, 24),  // 13 J
    COLOR_RGB(255, 247, 214), // 14 C
    COLOR_RGB(181, 165, 132), // 15 B
};
enum {
    MK_K = 1,
    MK_O,
    MK_L,
    MK_S,
    MK_D,
    MK_W,
    MK_I,
    MK_E,
    MK_P,
    MK_N,
    MK_G,
    MK_H,
    MK_J,
    MK_C,
    MK_B
};

#define HEAD_W 16 // the left half
#define HEAD_H 40
static const char head_keys[] = ".KOLSDWIEPN";
static const char head[HEAD_W * HEAD_H + 1] = "....D..........."
                                              "...DID.........."
                                              "...DIID........."
                                              "..DIIID........."
                                              "..DIIIID........"
                                              ".DIIIIID........"
                                              ".DIWWIIID......."
                                              ".DIWWIIIID......"
                                              "DIIWIIIIID......"
                                              "DIIIIIIIIID....."
                                              "DIIIIIIIIIID...."
                                              "DIIIIIIIIIID...."
                                              "DIIIIIIIIIIDOOOO"
                                              "DOIIIIIIIIIDOOOO"
                                              "DOOIIIIIIIDOOODO"
                                              ".DOOOIIIIDOOOODO"
                                              ".DOOOOOOOOOOOODO"
                                              ".SOOOOOOOOOOODOO"
                                              ".SOOOOOOOOOOODOO"
                                              "SOOOOLLLLOOOODOO"
                                              "SOOOLLLLLLOOODOO"
                                              "SOOLLEEEELLDDOOO"
                                              "SOOLLEPPELLDOOOO"
                                              "SOOLLEPPELLDOOOO"
                                              "SOOLLLEELLLDOOOO"
                                              "SOOOLLLLLLLOOOOO"
                                              "SOOODLLLLLOOOOOO"
                                              "SOOOOOOOOOOOOOOO"
                                              "SSOOOOOOODOOOWWW"
                                              ".SOOOOOOOOOOWWWW"
                                              ".SOOOODOOOOWWWNN"
                                              ".SSOOOOOOOOWWWNN"
                                              "..SOOOOOOOOWWWWD"
                                              "..SSOOOOOOOWWWWD"
                                              "...SSOOOOOOOWWDW"
                                              "....SSOOOOOOWWWW"
                                              ".....SSOOOOOOWWW"
                                              "......SSOOOOOOWW"
                                              ".......SSSOOOOOW"
                                              ".........SSSSSSW";

static void build_mark(void) {
    picture(head, HEAD_W, HEAD_H, 2, 4, head_keys, true);
    outline(MK_K, true, 1);
    label(&chunky, "SERVAL", 44, 2, MK_G, 2, 2, 0);
    label(&chunky, "ENGINE", 44, 26, MK_C, 2, 2, 0);
    bevel(MK_G, MK_J, MK_H);
    bevel(MK_C, MK_B, MK_C);
    outline(MK_K, true, MK_G);
}

// --- Style 1: emblem -----------------------------------------------------------------
//
// A navy roundel with a gold rim, a leaping serval across it and a red band
// carrying the name: a cartridge-label badge.

// N navy, M lighter navy, R rim gold, S rim light, T rim dark, G serval gold,
// D serval spots, B band red, A band dark, C band light, W cream text,
// K outline, Y star.
static const u16 colors_emblem[16] = {
    0,
    COLOR_RGB(24, 41, 99),    // 1 N
    COLOR_RGB(49, 74, 148),   // 2 M
    COLOR_RGB(239, 181, 66),  // 3 R
    COLOR_RGB(255, 231, 132), // 4 S
    COLOR_RGB(156, 107, 24),  // 5 T
    COLOR_RGB(247, 198, 99),  // 6 G
    COLOR_RGB(41, 57, 115),   // 7 D
    COLOR_RGB(181, 33, 49),   // 8 B
    COLOR_RGB(107, 16, 33),   // 9 A
    COLOR_RGB(222, 66, 82),   // 10 C
    COLOR_RGB(255, 247, 214), // 11 W
    COLOR_RGB(8, 8, 16),      // 12 K
    COLOR_RGB(255, 255, 198), // 13 Y
};
enum { EM_N = 1, EM_M, EM_R, EM_S, EM_T, EM_G, EM_D, EM_B, EM_A, EM_C, EM_W, EM_K, EM_Y };
// (head_keys above and leap_keys below list the picture letters in enum
// order, so a letter's index is its color.)

// The leap, facing right: ears up, forelegs reaching, hind legs trailing,
// the short tail out behind.
#define LEAP_W 48
#define LEAP_H 26
static const char leap_keys[] = ".NMRSTGD"; // as the enum below
static const char leap[LEAP_W * LEAP_H + 1] = "..................................GG.....GG....."
                                              "..................................GGG...GGG....."
                                              "..................................GGGG.GGGG....."
                                              "..................................GGGGGGGGG....."
                                              "...............................GGGGGGGGGGGG....."
                                              "............................GGGGGGGGGGGGGGGG...."
                                              ".........................GGGGGGGGGGGGGGGGGGGG..."
                                              "..........GG...........GGGGGGGGGGGGGGGGGGGGGGG.."
                                              ".........GGG.........GGGGGGGGGGGGGGGGGGGGGGGG..."
                                              "........GGG.........GGGGGGGGGGGGGGGGGGGGGGG....."
                                              ".......GGG.........GGGGGGGGGGGGGGGGGGGGGG......."
                                              ".......GGG........GGGGGGGGGGGGGGGGGGGGGG........"
                                              ".......GGG.......GGGGGGGGGGGGGGGGGGGGG.........."
                                              "........GGGGGGGGGGGGGGGGGG...GGGGGGGGGG........."
                                              ".........GGGGGGGGGGGGGGGGG....GGGGG.GGGGGG......"
                                              "..........GGGGG..GGGGG.........GGGG..GGGGGG....."
                                              ".........GGGGG....GGGGG.........GGGG..GGGGG....."
                                              "........GGGG.......GGGGG.........GGGG..GGGGG...."
                                              ".......GGGG.........GGGG..........GGGG..GGGG...."
                                              "......GGGG...........GGGG..........GGGG..GGGG..."
                                              ".....GGGG.............GGGG..........GGGG..GGG..."
                                              "....GGGG...............GGGG..........GGG...GGG.."
                                              "...GGGG.................GGG...........GGG..GGG.."
                                              "..GGG....................GGG...........GGG..GG.."
                                              ".GGG......................GG............GG...G.."
                                              ".GG........................G.............G......";

static void build_emblem(void) {
    // The roundel: rim, inner line, field, a few stars.
    disc(48, 30, 30, EM_R);
    disc(48, 30, 27, EM_T);
    disc(48, 30, 26, EM_N);
    put(33, 11, EM_Y);
    put(27, 22, EM_Y);
    put(69, 31, EM_Y);
    picture(leap, LEAP_W, LEAP_H, 22, 12, leap_keys, false);
    bevel(EM_R, EM_T, EM_S);
    // The band, forked at both ends, in front of everything.
    rect(3, 40, 90, 16, EM_A);
    rect(4, 41, 88, 14, EM_B);
    rect(4, 41, 88, 2, EM_C);
    for (int y = 40; y < 56; y++) {
        int depth = 5 - (y < 48 ? 47 - y : y - 48);
        for (int d = 0; d < depth; d++) {
            put(3 + d, y, 0);
            put(92 - d, y, 0);
        }
    }
    label_centered(&small, "SERVAL ENGINE", 48, 45, EM_W, 1, 1, 0);
    outline(EM_K, false, 1);
}

// --- Style 2: minimal geometric --------------------------------------------------------
//
// An abstract head from a disc and two tall triangles, in one gold, with
// the eyes, nose and a few spots cut out dark; the name in small spaced
// capitals under it. Three tones.

static const u16 colors_minimal[16] = {
    0,
    COLOR_RGB(247, 181, 66),  // 1 gold
    COLOR_RGB(57, 33, 24),    // 2 dark
    COLOR_RGB(239, 239, 231), // 3 off-white
    COLOR_RGB(181, 123, 33),  // 4 deep gold
};
enum { MN_GOLD = 1, MN_DARK, MN_WHITE, MN_DEEP };

static void build_minimal(void) {
    const int cx = 56;
    triangle(cx - 10, 0, 15, 20, MN_GOLD);
    triangle(cx + 10, 0, 15, 20, MN_GOLD);
    disc(cx, 25, 12, MN_GOLD);
    triangle(cx - 10, 4, 7, 12, MN_DEEP); // inner ears
    triangle(cx + 10, 4, 7, 12, MN_DEEP);
    rect(cx - 6, 24, 2, 2, MN_DARK); // eyes
    rect(cx + 5, 24, 2, 2, MN_DARK);
    rect(cx - 1, 29, 2, 2, MN_DARK); // nose
    put(cx - 11, 29, MN_DARK);       // spots
    put(cx - 9, 32, MN_DARK);
    put(cx + 10, 29, MN_DARK);
    put(cx + 8, 32, MN_DARK);
    label_centered(&small, "SERVAL ENGINE", cx, 41, MN_WHITE, 1, 3, 0);
}

// --- Style 3: retro hardware -------------------------------------------------------------
//
// A brushed-steel plate with a beveled edge, four screws, "SERVAL" in
// chrome italics and "ENGINE" engraved beneath: a console's boot screen.

static const u16 colors_plate[16] = {
    0,
    COLOR_RGB(16, 16, 24),    // 1 outline
    COLOR_RGB(74, 74, 90),    // 2 dark bevel
    COLOR_RGB(115, 115, 132), // 3 plate
    COLOR_RGB(132, 132, 148), // 4 brushed line
    COLOR_RGB(189, 189, 206), // 5 light bevel
    COLOR_RGB(90, 90, 107),   // 6 groove
    COLOR_RGB(165, 165, 181), // 7 screw
    COLOR_RGB(255, 255, 255), // 8 chrome white
    COLOR_RGB(222, 239, 255), // 9 chrome light
    COLOR_RGB(115, 165, 222), // 10 chrome sky
    COLOR_RGB(41, 49, 74),    // 11 chrome horizon
    COLOR_RGB(156, 165, 181), // 12 chrome lower
    COLOR_RGB(231, 231, 239), // 13 chrome lower light
    COLOR_RGB(41, 41, 57),    // 14 engraved
    COLOR_RGB(214, 214, 231), // 15 engraved light
};
enum {
    PL_K = 1,
    PL_DARK,
    PL_PLATE,
    PL_BRUSH,
    PL_LIGHT,
    PL_GROOVE,
    PL_SCREW,
    PL_WHITE,
    PL_CHROME1,
    PL_SKY,
    PL_HORIZON,
    PL_LOWER,
    PL_LOWER2,
    PL_ENGRAVED,
    PL_ENGRAVED2
};

static const u8 chrome[20] = {PL_WHITE,   PL_WHITE,  PL_CHROME1, PL_CHROME1, PL_CHROME1,
                              PL_SKY,     PL_SKY,    PL_SKY,     PL_SKY,     PL_HORIZON,
                              PL_HORIZON, PL_LOWER,  PL_LOWER,   PL_LOWER,   PL_LOWER2,
                              PL_LOWER2,  PL_LOWER2, PL_LOWER2,  PL_WHITE,   PL_WHITE};

static void screw(int x, int y) {
    disc(x, y, 2, PL_SCREW);
    put(x - 1, y, PL_DARK);
    put(x, y, PL_DARK);
    put(x + 1, y, PL_DARK);
}

static void build_plate(void) {
    rect(1, 1, 158, 46, PL_LIGHT);
    rect(3, 3, 156, 44, PL_DARK);
    rect(3, 3, 154, 42, PL_PLATE);
    for (int y = 3; y < 45; y += 2)
        rect(3, y, 154, 1, PL_BRUSH);
    // Rounded corners, and a groove inside the bevel.
    put(1, 1, 0);
    put(158, 1, 0);
    put(1, 46, 0);
    put(158, 46, 0);
    rect(7, 6, 147, 1, PL_GROOVE);
    rect(6, 6, 1, 36, PL_GROOVE);
    rect(7, 41, 147, 1, PL_LIGHT);
    rect(153, 7, 1, 35, PL_LIGHT);
    screw(10, 10);
    screw(149, 10);
    screw(10, 37);
    screw(149, 37);
    outline(PL_K, false, 1);
    // The name.
    label_centered(&chunky, "SERVAL", 78, 6, PL_WHITE, 2, 2, 4);
    gradient(PL_WHITE, 6, chrome, 20);
    outline(PL_K, true, PL_WHITE);
    label_centered(&chunky, "ENGINE", 81, 30, PL_ENGRAVED2, 1, 8, 0);
    label_centered(&chunky, "ENGINE", 80, 29, PL_ENGRAVED, 1, 8, 0);
}

// --- The styles -----------------------------------------------------------------------------

typedef struct {
    void (*build)(void);
    const u16* colors;
    u8 col, row, w, h;     // the logo's tiles on screen
    u8 text_col, text_row; // "made with"
} Style;

static const Style styles[SERVAL_SPLASH_STYLES] = {
    {build_mark, colors_mark, 6, 8, 18, 6, 10, 6},
    {build_emblem, colors_emblem, 9, 7, 12, 8, 10, 5},
    {build_minimal, colors_minimal, 8, 8, 14, 6, 10, 6},
    {build_plate, colors_plate, 5, 8, 20, 6, 10, 6},
};

// Cuts the canvas into tiles, row by row, at a style's place in charblock 1.
static void pack(u32 style) {
    u32* dst = (u32*)&tile_mem[SERVAL_SPLASH_ART_CHARBLOCK][style * SERVAL_SPLASH_ART_TILES];
    for (int ty = 0; ty < canvas_h / 8; ty++) {
        for (int tx = 0; tx < canvas_w / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)get(tx * 8 + x, ty * 8 + y) << (4 * x);
                *dst++ = word;
            }
        }
    }
}

void serval_splash_art_load(void) {
    for (u32 s = 0; s < SERVAL_SPLASH_STYLES; s++) {
        const Style* st = &styles[s];
        canvas_begin(st->w * 8, st->h * 8);
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
    for (u32 ty = 0; ty < st->h; ty++)
        for (u32 tx = 0; tx < st->w; tx++)
            se_mem[31][(st->row + ty) * 32 + st->col + tx] = (SCR_ENTRY)(base + ty * st->w + tx);
    serval_text_print_bank(st->text_col, st->text_row, "made with", text_bank);
}
