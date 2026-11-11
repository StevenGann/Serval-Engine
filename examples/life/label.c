// Text without a text layer (the surface leaves none, surface.h): lines
// composed with the game's own font into 32-pixel-wide sprite frames, so a
// line costs a hardware sprite per 32 pixels whatever it says. (The
// raycaster's menus are made the same way.)
//
// A line is first drawn a bit per pixel, its two colors apart, up to 256
// pixels by 16; the shadow (small text) or the outline (big text) is those
// bits shifted about; then the three go into the tiles' four bits per pixel
// eight pixels at a time, through a table that spreads a byte's bits over a
// word's nibbles. A frame whose tiles come out the same as before isn't sent
// again, so a counter that changes every frame sends one or two frames.

#include "life.h"

#include <string.h>

// --- The font ---------------------------------------------------------------------
//
// 5 x 7, drawn for this game. A glyph is as wide as its rightmost column;
// glyphs stand a pixel apart, a space is 3 pixels and a tab goes on from
// pixel 64. Besides letters,
// digits and punctuation: '}' play, '|' pause, '*' a cell.

typedef struct {
    char c;
    const char* rows; // 7 rows of 5, '#' set
} Glyph;

static const Glyph glyphs[] = {
    {'A', ".###."
          "#...#"
          "#...#"
          "#####"
          "#...#"
          "#...#"
          "#...#"},
    {'B', "####."
          "#...#"
          "#...#"
          "####."
          "#...#"
          "#...#"
          "####."},
    {'C', ".###."
          "#...#"
          "#...."
          "#...."
          "#...."
          "#...#"
          ".###."},
    {'D', "####."
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          "####."},
    {'E', "#####"
          "#...."
          "#...."
          "####."
          "#...."
          "#...."
          "#####"},
    {'F', "#####"
          "#...."
          "#...."
          "####."
          "#...."
          "#...."
          "#...."},
    {'G', ".###."
          "#...#"
          "#...."
          "#.###"
          "#...#"
          "#...#"
          ".####"},
    {'H', "#...#"
          "#...#"
          "#...#"
          "#####"
          "#...#"
          "#...#"
          "#...#"},
    {'I', "###.."
          ".#..."
          ".#..."
          ".#..."
          ".#..."
          ".#..."
          "###.."},
    {'J', "..###"
          "...#."
          "...#."
          "...#."
          "#..#."
          "#..#."
          ".##.."},
    {'K', "#...#"
          "#..#."
          "#.#.."
          "##..."
          "#.#.."
          "#..#."
          "#...#"},
    {'L', "#...."
          "#...."
          "#...."
          "#...."
          "#...."
          "#...."
          "#####"},
    {'M', "#...#"
          "##.##"
          "#.#.#"
          "#.#.#"
          "#...#"
          "#...#"
          "#...#"},
    {'N', "#...#"
          "##..#"
          "##..#"
          "#.#.#"
          "#..##"
          "#..##"
          "#...#"},
    {'O', ".###."
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          ".###."},
    {'P', "####."
          "#...#"
          "#...#"
          "####."
          "#...."
          "#...."
          "#...."},
    {'Q', ".###."
          "#...#"
          "#...#"
          "#...#"
          "#.#.#"
          "#..#."
          ".##.#"},
    {'R', "####."
          "#...#"
          "#...#"
          "####."
          "#.#.."
          "#..#."
          "#...#"},
    {'S', ".####"
          "#...."
          "#...."
          ".###."
          "....#"
          "....#"
          "####."},
    {'T', "#####"
          "..#.."
          "..#.."
          "..#.."
          "..#.."
          "..#.."
          "..#.."},
    {'U', "#...#"
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          ".###."},
    {'V', "#...#"
          "#...#"
          "#...#"
          "#...#"
          "#...#"
          ".#.#."
          "..#.."},
    {'W', "#...#"
          "#...#"
          "#...#"
          "#.#.#"
          "#.#.#"
          "##.##"
          "#...#"},
    {'X', "#...#"
          "#...#"
          ".#.#."
          "..#.."
          ".#.#."
          "#...#"
          "#...#"},
    {'Y', "#...#"
          "#...#"
          ".#.#."
          "..#.."
          "..#.."
          "..#.."
          "..#.."},
    {'Z', "#####"
          "....#"
          "...#."
          "..#.."
          ".#..."
          "#...."
          "#####"},
    {'0', ".###."
          "#...#"
          "#..##"
          "#.#.#"
          "##..#"
          "#...#"
          ".###."},
    {'1', ".#..."
          "##..."
          ".#..."
          ".#..."
          ".#..."
          ".#..."
          "###.."},
    {'2', ".###."
          "#...#"
          "....#"
          "...#."
          "..#.."
          ".#..."
          "#####"},
    {'3', "####."
          "....#"
          "....#"
          ".###."
          "....#"
          "....#"
          "####."},
    {'4', "...#."
          "..##."
          ".#.#."
          "#..#."
          "#####"
          "...#."
          "...#."},
    {'5', "#####"
          "#...."
          "####."
          "....#"
          "....#"
          "#...#"
          ".###."},
    {'6', "..##."
          ".#..."
          "#...."
          "####."
          "#...#"
          "#...#"
          ".###."},
    {'7', "#####"
          "....#"
          "...#."
          "..#.."
          ".#..."
          ".#..."
          ".#..."},
    {'8', ".###."
          "#...#"
          "#...#"
          ".###."
          "#...#"
          "#...#"
          ".###."},
    {'9', ".###."
          "#...#"
          "#...#"
          ".####"
          "....#"
          "...#."
          ".##.."},
    {'.', "....."
          "....."
          "....."
          "....."
          "....."
          "....."
          "#...."},
    {',', "....."
          "....."
          "....."
          "....."
          "....."
          ".#..."
          "#...."},
    {':', "....."
          "....."
          "#...."
          "....."
          "....."
          "#...."
          "....."},
    {'/', "....#"
          "...#."
          "...#."
          "..#.."
          ".#..."
          ".#..."
          "#...."},
    {'-', "....."
          "....."
          "....."
          "###.."
          "....."
          "....."
          "....."},
    {'&', ".#..."
          "#.#.."
          "#.#.."
          ".#..."
          "#.#.#"
          "#..#."
          ".##.#"},
    {'\'', "#...."
           "#...."
           "....."
           "....."
           "....."
           "....."
           "....."},
    {'!', "#...."
          "#...."
          "#...."
          "#...."
          "#...."
          "....."
          "#...."},
    {'?', ".###."
          "#...#"
          "....#"
          "...#."
          "..#.."
          "....."
          "..#.."},
    {'<', "...#."
          "..#.."
          ".#..."
          "#...."
          ".#..."
          "..#.."
          "...#."},
    {'>', "#...."
          ".#..."
          "..#.."
          "...#."
          "..#.."
          ".#..."
          "#...."},
    {'+', "....."
          "..#.."
          "..#.."
          "#####"
          "..#.."
          "..#.."
          "....."},
    {'(', ".#..."
          "#...."
          "#...."
          "#...."
          "#...."
          "#...."
          ".#..."},
    {')', "#...."
          ".#..."
          ".#..."
          ".#..."
          ".#..."
          ".#..."
          "#...."},
    {'}', "#...."
          "##..."
          "###.."
          "####."
          "###.."
          "##..."
          "#...."},
    {'|', "....."
          "##.##"
          "##.##"
          "##.##"
          "##.##"
          "##.##"
          "....."},
    {'*', "....."
          "....."
          ".###."
          ".###."
          ".###."
          "....."
          "....."},
};
#define GLYPHS ((int)(sizeof(glyphs) / sizeof(glyphs[0])))
#define FONT_ROWS 7
#define SPACE 3
#define NO_GLYPH 255
#define TAB 64 // where a '\t' goes on to

static u8 glyph_of[96] SERVAL_EWRAM_BSS;
static u8 glyph_bits[GLYPHS][FONT_ROWS] SERVAL_EWRAM_BSS;
static u8 glyph_width[GLYPHS] SERVAL_EWRAM_BSS;
static u32 spread[256] SERVAL_EWRAM_BSS; // bit x of the byte as nibble x

void labels_build(void) {
    for (int i = 0; i < 96; i++)
        glyph_of[i] = NO_GLYPH;
    for (int g = 0; g < GLYPHS; g++) {
        u32 all = 0;
        for (int y = 0; y < FONT_ROWS; y++) {
            u32 bits = 0;
            for (int x = 0; x < 5; x++)
                if (glyphs[g].rows[y * 5 + x] == '#')
                    bits |= 1u << x;
            glyph_bits[g][y] = (u8)bits;
            all |= bits;
        }
        int w = 0;
        while (all >> w)
            w++;
        glyph_width[g] = (u8)w;
        glyph_of[glyphs[g].c - 32] = (u8)g;
    }
    for (u32 i = 0; i < 256; i++) {
        u32 nibbles = 0;
        for (int b = 0; b < 8; b++)
            if (i >> b & 1)
                nibbles |= 1u << (4 * b);
        spread[i] = nibbles;
    }
}

// --- Drawing a line ----------------------------------------------------------------

#define LINE_WORDS 8 // 32 pixels each
#define LINE_ROWS 16
// Rows 1 to 16 hold the line; a row of nothing above and below spares the
// shifting its edge cases.
static u32 ink[LINE_ROWS + 2][LINE_WORDS] SERVAL_EWRAM_BSS;
static u32 accent[LINE_ROWS + 2][LINE_WORDS] SERVAL_EWRAM_BSS;

// ORs `bits` (bit 0 leftmost) into a row of a plane from pixel x on.
static inline void put_bits(u32 (*plane)[LINE_WORDS], int x, int y, u32 bits) {
    if (x < 0 || x >= LINE_WORDS * 32)
        return;
    u32 w = (u32)x >> 5, shift = (u32)x & 31;
    plane[1 + y][w] |= bits << shift;
    if (shift && w + 1 < LINE_WORDS)
        plane[1 + y][w + 1] |= bits >> (32 - shift);
}

// A glyph row's 5 bits with each doubled (for big text).
static inline u32 doubled(u32 bits) {
    u32 out = 0;
    for (int b = 0; b < 5; b++)
        if (bits >> b & 1)
            out |= 3u << (2 * b);
    return out;
}

// Draws `text` from (x, y), each font pixel `scale` x `scale` (1 or 2), and
// returns the x after its last pixel.
static int print(const char* text, int x, int y, int scale) {
    bool second = false;
    int end = x;
    for (; *text; text++) {
        u32 c = (u32)(u8)*text;
        if (c == '~') {
            second = !second;
            continue;
        }
        if (c == '\t') {
            x = TAB * scale;
            continue;
        }
        u32 g = c >= 32 && c < 128 ? glyph_of[c - 32] : NO_GLYPH;
        if (g == NO_GLYPH) {
            x += SPACE * scale;
            continue;
        }
        u32(*plane)[LINE_WORDS] = second ? accent : ink;
        for (int row = 0; row < FONT_ROWS; row++) {
            u32 bits = glyph_bits[g][row];
            if (scale == 1) {
                put_bits(plane, x, y + row, bits);
            } else {
                bits = doubled(bits);
                put_bits(plane, x, y + row * 2, bits);
                put_bits(plane, x, y + row * 2 + 1, bits);
            }
        }
        end = x + glyph_width[g] * scale;
        x = end + scale;
    }
    return end;
}

// Frame `b` of the line (32 pixels by 8 x `tile_rows`) as tiles: 1 ink,
// 2 the shadow (to the right, below and both) or the outline all around,
// 3 accent.
static void to_tiles(u32* tiles, int b, int tile_rows, bool outline) {
    // What is lit in the frame's rows, and the pixel just left and just
    // right of the frame on each (from the frames beside it).
    u32 lit[LINE_ROWS + 2], from_left[LINE_ROWS + 2], from_right[LINE_ROWS + 2];
    int rows = tile_rows * 8 + 2;
    for (int y = 0; y < rows; y++) {
        lit[y] = ink[y][b] | accent[y][b];
        from_left[y] = b ? (ink[y][b - 1] | accent[y][b - 1]) >> 31 : 0;
        from_right[y] = b + 1 < LINE_WORDS ? (ink[y][b + 1] | accent[y][b + 1]) << 31 : 0;
    }
    for (int ty = 0; ty < tile_rows; ty++) {
        for (int r = 0; r < 8; r++) {
            int y = 1 + ty * 8 + r;
            u32 here = lit[y], up = lit[y - 1];
            u32 dark = here << 1 | from_left[y] | up | up << 1 | from_left[y - 1];
            if (outline) {
                u32 down = lit[y + 1];
                dark |= down | down << 1 | from_left[y + 1] | here >> 1 | from_right[y] | up >> 1 |
                        from_right[y - 1] | down >> 1 | from_right[y + 1];
            }
            dark &= ~here;
            u32 i = ink[y][b], a = accent[y][b];
            u32* out = tiles + ty * 4 * 8 + r;
            for (int tx = 0; tx < 4; tx++, i >>= 8, a >>= 8, dark >>= 8)
                out[tx * 8] = spread[i & 255] | spread[dark & 255] * 2 | spread[a & 255] * 3;
        }
    }
}

// --- Labels and their frames -------------------------------------------------------

static u32 text_tiles[TEXT_FRAMES * 4 * 8] SERVAL_EWRAM_BSS;
static u32 big_tiles[BIG_FRAMES * 8 * 8] SERVAL_EWRAM_BSS;
static bool text_dirty[TEXT_FRAMES] SERVAL_EWRAM_BSS;
static bool big_dirty[BIG_FRAMES] SERVAL_EWRAM_BSS;
static bool loaded;

u32* label_tiles(u32 sprite) {
    return sprite == SPR_BIG ? big_tiles : text_tiles;
}

static void compose(Label* label) {
    bool big = label->sprite == SPR_BIG;
    int tile_rows = big ? 2 : 1;
    int words = tile_rows * 4 * 8;
    // The words the label's frames read: its own and one past.
    int clear = label->blocks + 1 < LINE_WORDS ? label->blocks + 1 : LINE_WORDS;
    for (int y = 0; y < tile_rows * 8 + 2; y++)
        for (int w = 0; w < clear; w++)
            ink[y][w] = accent[y][w] = 0;
    // Big text is outlined, so it starts a pixel in and down.
    int end = print(label->text, big, big, big ? 2 : 1);
    label->width = (u8)(end + 1);
    u32* tiles = label_tiles(label->sprite);
    bool* dirty = big ? big_dirty : text_dirty;
    static u32 frame[8 * 8];
    // The frames past the text's end aren't drawn (label_draw()).
    for (int b = 0; b < label->blocks && b * 32 < label->width; b++) {
        to_tiles(frame, b, tile_rows, big);
        u32* at = tiles + (label->first + b) * words;
        if (memcmp(at, frame, (size_t)words * 4) != 0) {
            memcpy(at, frame, (size_t)words * 4);
            dirty[label->first + b] = true;
        }
    }
}

void label_init(Label* label, u32 sprite, u32 first, u32 blocks, const char* text) {
    label->sprite = (u8)sprite;
    label->first = (u8)first;
    label->blocks = (u8)blocks;
    label->text[0] = 1; // never a text: the first label_set() composes
    label->text[1] = 0;
    label_set(label, text);
}

// (The engine's ROMs link no C library: memcpy and its kin only, no strcmp.)
static bool same_text(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void label_set(Label* label, const char* text) {
    if (same_text(label->text, text))
        return;
    size_t n = strlen(text);
    if (n >= sizeof label->text)
        n = sizeof label->text - 1;
    memcpy(label->text, text, n);
    label->text[n] = 0;
    compose(label);
}

void label_draw(const Label* label, int x, int y, u16 flags) {
    for (int b = 0; b < label->blocks && b * 32 < label->width; b++)
        sprite_draw(label->sprite, (u8)(label->first + b), x + b * 32, y, flags);
}

void label_draw_centered(const Label* label, int y, u16 flags) {
    label_draw(label, (SCREEN_W - label->width) / 2, y, flags);
}

void labels_loaded(void) {
    loaded = true;
    memset(text_dirty, 0, sizeof text_dirty);
    memset(big_dirty, 0, sizeof big_dirty);
}

void labels_flush(void) {
    if (!loaded)
        return;
    int budget = SPRITE_MAX_TILE_UPDATES;
    for (int f = 0; f < BIG_FRAMES && budget; f++) {
        if (big_dirty[f]) {
            sprite_set_tiles(SPR_BIG, (u8)f, big_tiles + f * 8 * 8);
            big_dirty[f] = false;
            budget--;
        }
    }
    for (int f = 0; f < TEXT_FRAMES && budget; f++) {
        if (text_dirty[f]) {
            sprite_set_tiles(SPR_TEXT, (u8)f, text_tiles + f * 4 * 8);
            text_dirty[f] = false;
            budget--;
        }
    }
}
