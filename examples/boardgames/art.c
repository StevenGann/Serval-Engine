// The pictures, all drawn at boot (art_build) into EWRAM buffers that
// tileset_load, map_load and sprite_group_load copy to VRAM, as racer and
// rhythm do. No image files.
//
// The table is one picture the size of the map: green felt with a fine
// twill, the board's 64 squares of maple and walnut (the grain runs across
// the light squares and down the dark ones, like parquet, each square's
// grain its own), a walnut frame with a bevel, a brass inlay and a drop
// shadow on the felt, and the panel at the right, dark with a brass rim and
// engraved dividers. Each 16x16 cell of it is a metatile. Then the cells
// that change: every square again with the checkers numbers in its corner
// (as the board is seen either way up), and the frame's cells with the
// files and ranks in brass, either way up. A square's highlight is not a
// picture: its metatile names the same tiles in another palette bank.
//
// Tiles are kept once: a tile already made (the felt, the panel's inside)
// is used again rather than stored twice, found by a hash of its pixels.
//
// The sprites are ASCII pictures (the pieces and their icons), one character
// a pixel ('.' transparent), and shapes drawn by rule (the cursor, the dots
// and rings, the puff, the sparkle, the logo's letters from a 5x7 font
// blown up three times).

#include "art.h"

// --- Tiles ----------------------------------------------------------------------------------

#define BG_TILE_MAX 1024

static u32 bg_tiles[BG_TILE_MAX * 8] SERVAL_EWRAM_BSS;
u16 bg_tiles_used;
static u16 tile_head[256] SERVAL_EWRAM_BSS; // hash -> first tile (0xFFFF none)
static u16 tile_next[BG_TILE_MAX] SERVAL_EWRAM_BSS;

static u32 tile_hash(const u32* w) {
    u32 h = 0;
    for (int i = 0; i < 8; i++)
        h = (h ^ w[i]) * 0x9E3779B1u;
    return h >> 24;
}

// The tile with these 8 words: an existing one if any is the same, else a
// new one (tile 0 is the blank one, made first).
static u16 add_tile(const u32* words) {
    u32 h = tile_hash(words);
    for (u16 t = tile_head[h]; t != 0xFFFF; t = tile_next[t]) {
        const u32* old = bg_tiles + t * 8;
        int i = 0;
        while (i < 8 && old[i] == words[i])
            i++;
        if (i == 8)
            return t;
    }
    if (bg_tiles_used == BG_TILE_MAX)
        return 0; // never with this table (art_build's total is far below)
    u16 t = bg_tiles_used++;
    u32* out = bg_tiles + t * 8;
    for (int i = 0; i < 8; i++)
        out[i] = words[i];
    tile_next[t] = tile_head[h];
    tile_head[h] = t;
    return t;
}

// One metatile's pixels, a colour index each, cut into its four tiles.
static u8 cell[16][16] SERVAL_EWRAM_BSS; // used at boot only

static Metatile cell_metatile(int bank) {
    Metatile m = {.collision = MAP_EMPTY};
    for (int q = 0; q < 4; q++) {
        int x0 = (q & 1) * 8, y0 = (q >> 1) * 8;
        u32 words[8];
        for (int y = 0; y < 8; y++) {
            u32 word = 0;
            for (int x = 0; x < 8; x++)
                word |= (u32)cell[y0 + y][x0 + x] << (4 * x);
            words[y] = word;
        }
        m.se[q] = MAP_SE(add_tile(words), bank, 0);
    }
    return m;
}

// --- Colours ------------------------------------------------------------------------------------

// The squares' colours (colours 1-8 of BANK_SQUARES): maple, its grain and
// a glint; walnut, its grain and a glint; the checkers numbers (a raised
// figure and its shadow, on the dark squares).
enum {
    C_LIGHT = 1,
    C_LIGHT_GRAIN,
    C_LIGHT_GLINT,
    C_DARK,
    C_DARK_GRAIN,
    C_DARK_GLINT,
    C_NUMBER,
    C_NUMBER_SHADOW,
};

static const Color square_colors[SQUARE_COLORS] = {
    COLOR_RGB(236, 206, 156), COLOR_RGB(222, 188, 138), COLOR_RGB(246, 222, 180),
    COLOR_RGB(170, 110, 64),  COLOR_RGB(150, 94, 52),   COLOR_RGB(184, 124, 76),
    COLOR_RGB(208, 156, 102), COLOR_RGB(118, 72, 40),
};

_Static_assert((int)SQ_LAST == (int)BANK_LAST && (int)SQ_SELECTED == (int)BANK_SELECTED &&
                   (int)SQ_ALERT == (int)BANK_ALERT,
               "a square's state is its palette bank");

// The highlights: the squares' colours mixed toward a tint.
#define LAST_TINT COLOR_RGB(250, 226, 80)
#define LAST_AMOUNT 110
#define SELECTED_TINT COLOR_RGB(110, 220, 150)
#define SELECTED_AMOUNT 130
#define ALERT_TINT COLOR_RGB(236, 36, 30)

Color alert_colors[ALERT_LEVELS][SQUARE_COLORS];

// The table's colours (BANK_TABLE).
enum {
    T_FELT = 1,
    T_FELT_WEAVE,
    T_SHADOW, // on the felt
    T_OUTLINE,
    T_WALNUT,
    T_WALNUT_LIGHT,
    T_WALNUT_DARK,
    T_BRASS,
    T_BRASS_DARK,
    T_PANEL,
    T_PANEL_DARK,
    T_RIM,
    T_RIM_DARK,
    T_PANEL_LIGHT,
};

static const Color table_colors[15] = {
    COLOR_RGB(30, 74, 58),  COLOR_RGB(36, 84, 66),    COLOR_RGB(16, 42, 34),
    COLOR_RGB(30, 16, 10),  COLOR_RGB(112, 66, 38),   COLOR_RGB(150, 96, 56),
    COLOR_RGB(88, 50, 28),  COLOR_RGB(238, 198, 112), COLOR_RGB(150, 104, 44),
    COLOR_RGB(42, 30, 32),  COLOR_RGB(34, 24, 26),    COLOR_RGB(222, 178, 92),
    COLOR_RGB(112, 78, 42), COLOR_RGB(58, 42, 42),    0,
};

static u16 bg_palettes[BG_BANKS * 16] SERVAL_EWRAM_BSS;

Tileset table_tileset = {
    .tiles = bg_tiles, // .tile_count: set by art_build, once the tiles are made
    .palettes = bg_palettes,
    .palette_count = BG_BANKS,
};

static void mix_squares(Color* out, Color tint, u32 amount) {
    for (int i = 0; i < SQUARE_COLORS; i++)
        out[i] = color_mix(square_colors[i], tint, amount);
}

static void build_palettes(void) {
    mix_squares(bg_palettes + BANK_SQUARES * 16 + 1, 0, 0);
    mix_squares(bg_palettes + BANK_LAST * 16 + 1, LAST_TINT, LAST_AMOUNT);
    mix_squares(bg_palettes + BANK_SELECTED * 16 + 1, SELECTED_TINT, SELECTED_AMOUNT);
    for (int level = 0; level < ALERT_LEVELS; level++)
        mix_squares(alert_colors[level], ALERT_TINT, 80 + 16 * (u32)level);
    for (int i = 0; i < SQUARE_COLORS; i++)
        bg_palettes[BANK_ALERT * 16 + 1 + i] = alert_colors[0][i];
    for (int i = 0; i < 15; i++)
        bg_palettes[BANK_TABLE * 16 + 1 + i] = table_colors[i];
    bg_palettes[0] = table_colors[T_FELT - 1]; // the backdrop, should any show
}

// --- The table's picture ----------------------------------------------------------------------

// Map pixels: the squares are metatiles 1-8, the frame the 8 pixels round
// them, the panel from PANEL_MX on.
#define SQ0 16
#define SQ1 144 // one past the last square pixel
#define FRAME0 8
#define FRAME1 152
#define PANEL_MX (PANEL_X + MAP_SCROLL_X)

// A gentle wobble along the grain, 0-2 pixels.
static const u8 wobble[16] = {0, 0, 1, 1, 2, 2, 2, 1, 1, 0, 0, 0, 1, 1, 1, 0};

// Wood: 0 plain, 1 a grain line, 2 a glint, at `along` pixels along the
// grain and `across` across it; `seed` makes each piece of wood its own.
static int wood(int along, int across, int seed) {
    int k = across * 2 + wobble[(along + seed * 5) & 15] + seed * 3;
    int m = k % 9;
    if (m == 0 || (m == 1 && ((along * 3 + seed) & 7) == 0))
        return 1;
    if (m == 5 && ((along + seed * 7) & 7) < 3)
        return 2;
    return 0;
}

static int square_pixel(int x, int y) {
    int col = (x - SQ0) >> 4, row = (y - SQ0) >> 4;
    int u = (x - SQ0) & 15, v = (y - SQ0) & 15;
    int seed = (row * 8 + col) * 7 + 3;
    if (((col + row) & 1) == 0) {
        static const u8 light[3] = {C_LIGHT, C_LIGHT_GRAIN, C_LIGHT_GLINT};
        return light[wood(u + col * 16, v, seed)];
    }
    static const u8 dark[3] = {C_DARK, C_DARK_GRAIN, C_DARK_GLINT};
    return dark[wood(v + row * 16, u, seed)];
}

static int frame_pixel(int x, int y) {
    int left = x - FRAME0, right = FRAME1 - 1 - x, top = y - FRAME0, bottom = FRAME1 - 1 - y;
    int d = left, side = 0; // 0 left, 1 right, 2 top, 3 bottom
    if (right < d)
        d = right, side = 1;
    if (top < d)
        d = top, side = 2;
    if (bottom < d)
        d = bottom, side = 3;
    // The outer corners are rounded off by a pixel.
    int corners = (left == 0) + (right == 0) + (top == 0) + (bottom == 0);
    if (corners == 2)
        return T_FELT;
    if (d == 0)
        return T_OUTLINE;
    if (d == 1)
        return side == 0 || side == 2 ? T_WALNUT_LIGHT : T_WALNUT_DARK;
    if (d == 7)
        return T_BRASS;
    int grain = side < 2 ? wood(y, d, side * 11 + 5) : wood(x, d, side * 11 + 2);
    return grain == 1 ? T_WALNUT_DARK : grain == 2 ? T_WALNUT_LIGHT : T_WALNUT;
}

static int panel_pixel(int x, int y) {
    if (x == PANEL_MX)
        return T_OUTLINE;
    if (x == PANEL_MX + 1)
        return T_RIM;
    if (x == PANEL_MX + 2)
        return T_RIM_DARK;
    // Engraved dividers under text rows 3, 6, 9 and 16, and a lighter plate
    // behind the game's name.
    int sy = y;
    if (x >= PANEL_MX + 4 && x < PANEL_MX + 4 + 76) {
        if (sy == 27 || sy == 51 || sy == 75 || sy == 131)
            return T_OUTLINE;
        if (sy == 28 || sy == 52 || sy == 76 || sy == 132)
            return T_RIM_DARK;
    }
    if (sy < 27)
        return T_PANEL_LIGHT;
    return ((x + y) & 3) == 0 && ((x - y) & 7) == 0 ? T_PANEL_DARK : T_PANEL;
}

// The table at map pixel (x, y): a colour of BANK_TABLE, or for a square a
// colour of the squares' banks.
static int table_pixel(int x, int y) {
    if (x >= PANEL_MX)
        return panel_pixel(x, y);
    if (x >= SQ0 && x < SQ1 && y >= SQ0 && y < SQ1)
        return square_pixel(x, y);
    if (x >= FRAME0 && x < FRAME1 && y >= FRAME0 && y < FRAME1)
        return frame_pixel(x, y);
    // The frame's shadow on the felt, down and to the right.
    if ((x >= FRAME1 && x < FRAME1 + 3 && y >= FRAME0 + 3 && y < FRAME1 + 3) ||
        (y >= FRAME1 && y < FRAME1 + 3 && x >= FRAME0 + 3 && x < FRAME1 + 3))
        return T_SHADOW;
    if (x == PANEL_MX - 1 || x == PANEL_MX - 2)
        return T_SHADOW;
    return ((x + 2 * y) % 6 == 0) ? T_FELT_WEAVE : T_FELT;
}

static void draw_cell(int cx, int cy) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            cell[y][x] = (u8)table_pixel(cx * 16 + x, cy * 16 + y);
}

// --- Small figures ------------------------------------------------------------------------------

// A 3x5 font for the frame's labels (the files as capitals, which read
// better than small letters this size) and the checkers numbers: each row 3
// bits, the leftmost pixel the highest bit.
static const u8 digits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};
static const u8 files[8][5] = {
    {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6},
    {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5},
};

// A figure into the cell at (x0, y0) in `ink`, with a shadow a pixel down
// and to the right.
static void figure(const u8* rows, int x0, int y0, int ink, int shadow) {
    for (int pass = 0; pass < 2; pass++)
        for (int y = 0; y < 5; y++)
            for (int x = 0; x < 3; x++)
                if (rows[y] & (4 >> x)) {
                    int px = x0 + x + 1 - pass, py = y0 + y + 1 - pass;
                    if (px < 16 && py < 16)
                        cell[py][px] = (u8)(pass ? ink : shadow);
                }
}

// --- The map
// --------------------------------------------------------------------------------------

static Metatile table_metatiles[MT_COUNT] SERVAL_EWRAM_BSS;
static u16 table_cells[TABLE_W * TABLE_H] SERVAL_EWRAM_BSS;

const MapLayer table_layer = {
    .width = TABLE_W,
    .height = TABLE_H,
    .cells = table_cells,
    .metatiles = table_metatiles,
    .metatile_count = MT_COUNT,
    .bg = BOARD_BG,
    .flags = MAP_LAYER_FIXED,
};

// The checkers number of the dark square seen at (col, row), 1-32, as the
// standard numbering has it with Light at the bottom: 1-4 on the top row.
static int checkers_number(int col, int row, bool flipped) {
    if (flipped)
        col = 7 - col, row = 7 - row;
    return row * 4 + (col >> 1) + 1;
}

static void build_table(void) {
    for (int cy = 0; cy < TABLE_H; cy++) {
        for (int cx = 0; cx < TABLE_W; cx++) {
            bool square = cx >= 1 && cx <= 8 && cy >= 1 && cy <= 8;
            draw_cell(cx, cy);
            table_metatiles[MT_CELL(cx, cy)] = cell_metatile(square ? BANK_SQUARES : BANK_TABLE);
            table_cells[cy * TABLE_W + cx] = (u16)MT_CELL(cx, cy);
        }
    }
    // Every square in every state, with and without the numbers.
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            for (int numbers = 0; numbers < NUM_VARIANTS; numbers++) {
                draw_cell(col + 1, row + 1);
                if (numbers != NUM_NONE && ((col + row) & 1)) {
                    int n = checkers_number(col, row, numbers == NUM_FLIPPED);
                    if (n >= 10) {
                        figure(digits[n / 10], 1, 1, C_NUMBER, C_NUMBER_SHADOW);
                        figure(digits[n % 10], 5, 1, C_NUMBER, C_NUMBER_SHADOW);
                    } else {
                        figure(digits[n], 1, 1, C_NUMBER, C_NUMBER_SHADOW);
                    }
                }
                Metatile m = cell_metatile(BANK_SQUARES);
                for (int state = 0; state < SQ_STATES; state++) {
                    Metatile s = m;
                    for (int q = 0; q < 4; q++)
                        s.se[q] = (u16)((m.se[q] & 0x0FFF) | (state << 12));
                    table_metatiles[MT_SQUARE(state, numbers, col, row)] = s;
                }
            }
        }
    }
    // The labels: ranks down the left (row 0 at the top), files along the
    // bottom, either way up. The left band is the right half of metatile
    // column 0, the bottom band the top half of metatile row 9.
    for (int flip = 0; flip < 2; flip++) {
        for (int i = 0; i < 8; i++) {
            draw_cell(0, i + 1);
            int rank = flip ? i : 7 - i;
            figure(digits[rank + 1], 11, 5, T_BRASS, T_OUTLINE);
            table_metatiles[MT_RANK(flip, i)] = cell_metatile(BANK_TABLE);
            draw_cell(i + 1, 9);
            int file = flip ? 7 - i : i;
            figure(files[file], 6, 1, T_BRASS, T_OUTLINE);
            table_metatiles[MT_FILE(flip, i)] = cell_metatile(BANK_TABLE);
        }
    }
}

// --- Sprite pictures ----------------------------------------------------------------------------

// Piece keys: K outline, d D M L W shades from deep to the highlight (the
// light comes from the top left), E the serval's eyes, G g H gold (the
// king's cross, the checkers crown).
static const char piece_keys[] = ".KdDMLWEGgH";

#define PIECE_PALETTE(K, d, D, M, L, W)                                                            \
    {0,                                                                                            \
     K,                                                                                            \
     d,                                                                                            \
     D,                                                                                            \
     M,                                                                                            \
     L,                                                                                            \
     W,                                                                                            \
     COLOR_RGB(236, 178, 44),                                                                      \
     COLOR_RGB(242, 198, 70),                                                                      \
     COLOR_RGB(176, 114, 30),                                                                      \
     COLOR_RGB(255, 242, 170)}

static const u16 obj_palettes[OBJ_PALETTES][16] = {
    [PAL_IVORY] =
        PIECE_PALETTE(COLOR_RGB(40, 26, 22), COLOR_RGB(160, 150, 148), COLOR_RGB(202, 196, 192),
                      COLOR_RGB(236, 232, 222), COLOR_RGB(248, 246, 238), COLOR_RGB(255, 255, 255)),
    [PAL_EBONY] =
        PIECE_PALETTE(COLOR_RGB(14, 8, 10), COLOR_RGB(30, 24, 30), COLOR_RGB(48, 40, 48),
                      COLOR_RGB(72, 64, 72), COLOR_RGB(104, 96, 104), COLOR_RGB(148, 140, 148)),
    // The cursor (outline, yellow, its light edge) and the marks (orange,
    // dim, light).
    [PAL_UI] = {0, COLOR_RGB(48, 26, 10), COLOR_RGB(255, 228, 96), COLOR_RGB(255, 250, 208),
                COLOR_RGB(255, 140, 36), COLOR_RGB(196, 92, 24), COLOR_RGB(255, 200, 120)},
    // The dots and rings: one dark green, blended into the squares.
    [PAL_TARGET] = {0, COLOR_RGB(20, 52, 34)},
    // The puff (shade, grey, cream) and the sparkle (gold, white, deep gold).
    [PAL_PUFF] = {0, COLOR_RGB(132, 120, 108), COLOR_RGB(204, 194, 178), COLOR_RGB(250, 244, 230),
                  COLOR_RGB(255, 214, 80), COLOR_RGB(255, 255, 255), COLOR_RGB(224, 150, 40)},
    // The logo: outline, shadow, a glint, gold from light to bronze, ivory
    // and its shade (the ampersand).
    [PAL_LOGO] = {0, COLOR_RGB(44, 20, 14), COLOR_RGB(16, 30, 24), COLOR_RGB(255, 252, 226),
                  COLOR_RGB(255, 228, 122), COLOR_RGB(250, 190, 70), COLOR_RGB(222, 140, 42),
                  COLOR_RGB(172, 92, 30), COLOR_RGB(248, 244, 232), COLOR_RGB(198, 192, 184)},
};

static const char* const piece_art[PIECE_FRAMES][16] = {
    [FRAME_PAWN] =
        {
            "................",
            "................",
            "................",
            "................",
            "......KKKK......",
            ".....KWLMDK.....",
            "....KWLMMMDK....",
            "....KLMMMMDK....",
            ".....KLMMDK.....",
            "....KKKKKKKK....",
            "....KLMMMMDK....",
            ".....KLMMDK.....",
            "....KLMMMMDK....",
            "...KLMMMMMMDK...",
            "...KKKKKKKKKK...",
            "................",
        },
    // A serval's head, face on: the great ears, amber eyes, the spots on its
    // brow, a pale muzzle.
    [FRAME_KNIGHT] =
        {
            "..KK........KK..",
            ".KLLK......KMDK.",
            ".KLWLK....KMMDK.",
            ".KLWMLK..KMMMDK.",
            ".KLMMMKKKKMMMDK.",
            "..KLMDMMMMDMDK..",
            ".KLMMMMDDMMMMDK.",
            ".KLKEKMMMMKEKDK.",
            ".KLMMMMMMMMMMDK.",
            "..KLMMWKKWMMDK..",
            "...KLMWWWWMDK...",
            "....KKKMMKKK....",
            "....KLMMMMDK....",
            "...KLMMMMMMDK...",
            "...KKKKKKKKKK...",
            "................",
        },
    [FRAME_BISHOP] =
        {
            "................",
            ".......KK.......",
            "......KWLK......",
            ".......KK.......",
            "......KLMK......",
            ".....KWLMDK.....",
            "....KWLMKDDK....",
            "....KLMKMMDK....",
            "....KLKMMMDK....",
            ".....KLMMDK.....",
            "....KKKKKKKK....",
            ".....KLMMDK.....",
            "....KLMMMMDK....",
            "...KLMMMMMMDK...",
            "...KKKKKKKKKK...",
            "................",
        },
    [FRAME_ROOK] =
        {
            "................",
            "................",
            "..KKK.KKKK.KKK..",
            "..KWK.KLMK.KDK..",
            "..KWKKKLMKKKDK..",
            "..KWLLMMMMMDDK..",
            "..KKKKKKKKKKKK..",
            "....KLMMMMDK....",
            "....KLMMMMDK....",
            "....KLMMMMDK....",
            "....KLMMMMDK....",
            "...KKKKKKKKKK...",
            "...KLMMMMMMDK...",
            "..KLMMMMMMMMDK..",
            "..KKKKKKKKKKKK..",
            "................",
        },
    // The queen: a coronet of three points, each with a pearl.
    [FRAME_QUEEN] =
        {
            "..KK...KK...KK..",
            ".KWLK.KWLK.KLDK.",
            "..KK...KK...KK..",
            "..KLK.KLMK.KDK..",
            "..KLMKKLMMKKDK..",
            "..KLMMLMMMMMDK..",
            "...KLMMMMMMDK...",
            "....KKKKKKKK....",
            "....KLMMMMDK....",
            ".....KLMMDK.....",
            ".....KLMMDK.....",
            "....KLMMMMDK....",
            "...KLMMMMMMDK...",
            "..KLMMMMMMMMDK..",
            "..KKKKKKKKKKKK..",
            "................",
        },
    // The king: a gold cross over a domed crown.
    [FRAME_KING] =
        {
            "......KKKK......",
            "....KKKHGKKK....",
            "....KHHHGGgK....",
            "....KKKHgKKK....",
            "......KHgK......",
            "...KKKKKKKKKK...",
            "..KWLMMMMMMMDK..",
            "..KLMMMMMMMMDK..",
            "...KLMMMMMMDK...",
            "....KKKKKKKK....",
            ".....KLMMDK.....",
            "....KLMMMMDK....",
            "...KLMMMMMMDK...",
            "..KLMMMMMMMMDK..",
            "..KKKKKKKKKKKK..",
            "................",
        },
    // A checkers man: a disc seen from a little above, a paw print pressed
    // into its face.
    [FRAME_MAN] =
        {
            "................",
            "................",
            "................",
            "....KKKKKKKK....",
            "..KKWWLLLLMMKK..",
            ".KWLLLdLLdMMMMK.",
            ".KWLdLLMMMMdMMK.",
            ".KLLLMMddMMMMDK.",
            ".KLMMMddddMMMDK.",
            ".KLMMMddddMMMDK.",
            ".KKMMMMMMMMMMKK.",
            ".KDKKMMMMMMKKdK.",
            ".KDDDKKKKKKDDdK.",
            "..KDDDDDDDDDdK..",
            "...KKKKKKKKKK...",
            "................",
        },
    // A checkers king: the disc with a gold crown on its face.
    [FRAME_CROWNED] =
        {
            "................",
            "................",
            "................",
            "....KKKKKKKK....",
            "..KKWWLKKLMMKK..",
            ".KWLKKLKHKLKKMK.",
            ".KWLKHKKGKKGKMK.",
            ".KLLKHGHGGGgKDK.",
            ".KLMKHGGGGGgKDK.",
            ".KLMMKKKKKKKMDK.",
            ".KKMMMMMMMMMMKK.",
            ".KDKKMMMMMMKKdK.",
            ".KDDDKKKKKKDDdK.",
            "..KDDDDDDDDDdK..",
            "...KKKKKKKKKK...",
            "................",
        },
};

// The panel's icons: each piece in 8x8.
static const char* const icon_art[ICON_FRAMES][8] = {
    [FRAME_PAWN] = {"........", "...KK...", "..KLDK..", "..KMDK..", "...KK...", "..KLDK..",
                    ".KLMMDK.", ".KKKKKK."},
    [FRAME_KNIGHT] = {".K....K.", "KLK..KDK", "KLMKKMDK", "KMEMMEDK", ".KMMMDK.", "..KKKK..",
                      ".KLMMDK.", ".KKKKKK."},
    [FRAME_BISHOP] = {"...KK...", "..KLMK..", ".KLMKDK.", ".KLKMDK.", "..KMDK..", "..KKKK..",
                      ".KLMMDK.", ".KKKKKK."},
    [FRAME_ROOK] = {"KK.KK.KK", "KLKLMKDK", "KKKKKKKK", ".KLMMDK.", ".KLMMDK.", ".KKKKKK.",
                    "KLMMMMDK", "KKKKKKKK"},
    [FRAME_QUEEN] = {"K..KK..K", "KK.KK.KK", "KLKLMKDK", "KLMMMMDK", ".KLMMDK.", "..KKKK..",
                     ".KLMMDK.", "KKKKKKKK"},
    [FRAME_KING] = {"...GG...", "..GGGg..", "...Gg...", ".KKKKKK.", "KLMMMMDK", ".KLMMDK.",
                    ".KLMMDK.", "KKKKKKKK"},
    [FRAME_MAN] = {"........", "........", "..KKKK..", ".KWLMDK.", ".KLddDK.", ".KKMMKK.",
                   ".KDKKdK.", "..KKKK.."},
    [FRAME_CROWNED] = {"........", ".G.G.G..", ".GGGGG..", ".KKKKKK.", ".KLMMDK.", ".KKMMKK.",
                       ".KDKKdK.", "..KKKK.."},
    [ICON_CHIP] = {"........", "..KKKK..", ".KWLMDK.", ".KLMMDK.", ".KMMDDK.", "..KKKK..",
                   "........", "........"},
};

// --- The canvas
// -----------------------------------------------------------------------------------

#define CANVAS_W LOGO_W
#define CANVAS_H LOGO_H
static u8 canvas[CANVAS_W * CANVAS_H] SERVAL_EWRAM_BSS;

static void canvas_clear(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 0;
}

static void put(int x, int y, int c) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        canvas[y * CANVAS_W + x] = (u8)c;
}

static int get(int x, int y) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        return canvas[y * CANVAS_W + x];
    return 0;
}

static int key_of(const char* keys, char c) {
    for (int k = 1; keys[k]; k++)
        if (keys[k] == c)
            return k;
    return 0;
}

static void paint(const char* const* rows, int h, const char* keys, int x0, int y0) {
    for (int y = 0; y < h; y++)
        for (int x = 0; rows[y][x]; x++)
            if (rows[y][x] != '.')
                put(x0 + x, y0 + y, key_of(keys, rows[y][x]));
}

// Cuts a w x h block of the canvas from (x0, y0) into 4bpp tiles, row by
// row (a sprite frame's layout). Returns the next free word.
static u32* pack(int x0, int y0, int w, int h, u32* out) {
    for (int ty = 0; ty < h / 8; ty++) {
        for (int tx = 0; tx < w / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                const u8* row = canvas + (y0 + ty * 8 + y) * CANVAS_W + x0 + tx * 8;
                for (int x = 0; x < 8; x++)
                    word |= (u32)row[x] << (4 * x);
                *out++ = word;
            }
        }
    }
    return out;
}

// --- The sprites
// ----------------------------------------------------------------------------------

#define OBJ_TILE_MAX 320
#define FRAMES_16(n) ((n) * 4)
_Static_assert(FRAMES_16(PIECE_FRAMES + 2 + TARGET_FRAMES + 2 + 4 * PUFF_FRAMES + SPARKLE_FRAMES) +
                       ICON_FRAMES + LOGO_FRAMES * 32 <=
                   OBJ_TILE_MAX,
               "the sprites outgrow obj_tiles: raise OBJ_TILE_MAX");
static u32 obj_tiles[OBJ_TILE_MAX * 8] SERVAL_EWRAM_BSS;
u16 obj_tiles_used;

static SpriteAsset sprites[SPRITE_COUNT] SERVAL_EWRAM_BSS;
const SpriteAsset* sprite_table[SPRITE_COUNT];

const SpriteGroup sprite_group = {
    .palettes = &obj_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = OBJ_PALETTES,
};

static void sprite(int id, u8 size, int frames, int palette, const u32* tiles) {
    sprites[id] = (SpriteAsset){
        .tiles = tiles,
        .size = size,
        .frame_count = (u8)frames,
        .palette_slot = (u8)palette,
    };
}

// The squared distance of pixel (x, y) of a 16x16 frame from its middle,
// in quarter pixels (a radius r is 4r^2).
static int d2(int x, int y) {
    int dx = 2 * x - 15, dy = 2 * y - 15;
    return dx * dx + dy * dy;
}

// Outlines what is drawn in the 16x16 block at (x0, 0) with `outline`:
// every empty pixel beside a drawn one.
static void outline16(int x0, int outline) {
    static u8 drawn[16][16] SERVAL_EWRAM_BSS; // at boot only
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            drawn[y][x] = get(x0 + x, y) != 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            if (drawn[y][x])
                continue;
            bool near = false;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < 16 && ny >= 0 && ny < 16 && drawn[ny][nx])
                        near = true;
                }
            if (near)
                put(x0 + x, y, outline);
        }
}

// The cursor: four corner brackets, yellow with a light outer edge and a
// dark outline, out at the square's edge (frame 0) and a pixel in (1).
static void draw_cursor(int frame) {
    int x0 = frame * 16, in = 1 + frame;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int u = x < 8 ? x - in : 15 - in - x; // distance in from the near edge
            int v = y < 8 ? y - in : 15 - in - y;
            if (u < 0 || v < 0)
                continue;
            if ((u < 2 && v < 5) || (v < 2 && u < 5))
                put(x0 + x, y, (u == 0 || v == 0) ? 3 : 2);
        }
    outline16(x0, 1);
}

static void draw_target(int frame) {
    int x0 = frame * 16;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int d = d2(x, y);
            if (frame == TARGET_DOT ? d <= 40 : (d >= 132 && d <= 228))
                put(x0 + x, y, 1);
        }
}

// The mark: an orange ring round the piece, bright (frame 0) or dim.
static void draw_mark(int frame) {
    int x0 = frame * 16;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int d = d2(x, y);
            if (d >= 132 && d <= 232)
                put(x0 + x, y, d < 170 ? (frame == 0 ? 6 : 4) : d < 200 ? (frame == 0 ? 4 : 5) : 1);
        }
}

// The puff: a round cloud that breaks into six and spreads out, thinning
// away. Per frame: how far the six are from the middle and their radius,
// in sixteenths of a pixel (frame 0 is one cloud in the middle).
static const u8 puff_shape[PUFF_FRAMES][2] = {{0, 120}, {96, 72}, {150, 56}, {190, 40}};
static const s16 puff_dir[6][2] = {{256, 0},  {128, 222},   {-128, 222},
                                   {-256, 0}, {-128, -222}, {128, -222}};

static bool in_puff(int frame, int x, int y) {
    int px = x * 16 + 8 - 256, py = y * 16 + 8 - 256; // from the middle, sixteenths
    int far = puff_shape[frame][0], r = puff_shape[frame][1];
    for (int b = 0; b < 6; b++) {
        int dx = px - puff_dir[b][0] * far / 256, dy = py - puff_dir[b][1] * far / 256;
        if (dx * dx + dy * dy <= r * r)
            return true;
    }
    return false;
}

static void draw_puff(int frame) {
    int x0 = frame * PUFF_SIDE;
    for (int y = 0; y < PUFF_SIDE; y++)
        for (int x = 0; x < PUFF_SIDE; x++) {
            if (!in_puff(frame, x, y) || (frame == PUFF_FRAMES - 1 && ((x + y) & 1)))
                continue;
            // Lit from the top left: cream, grey in the lower right, and a
            // shade at the edges away from the light.
            bool edge = !in_puff(frame, x + 1, y) || !in_puff(frame, x, y + 1);
            put(x0 + x, y, edge ? 1 : (!in_puff(frame, x - 1, y - 1) ? 3 : 2));
        }
}

// The sparkle: three four-pointed stars that grow and fade, gold with
// white hearts.
static void draw_sparkle(int frame) {
    static const u8 stars[3][2] = {{4, 4}, {11, 6}, {6, 11}};
    static const u8 arm[SPARKLE_FRAMES][3] = {{3, 1, 2}, {2, 3, 1}, {1, 2, 3}};
    int x0 = frame * 16;
    for (int s = 0; s < 3; s++) {
        int cx = stars[s][0], cy = stars[s][1], a = arm[frame][s];
        for (int k = 1; k <= a; k++) {
            int c = k == a ? 6 : 4;
            put(x0 + cx + k, cy, c);
            put(x0 + cx - k, cy, c);
            put(x0 + cx, cy + k, c);
            put(x0 + cx, cy - k, c);
        }
        put(x0 + cx, cy, 5);
    }
}

// --- The logo
// -------------------------------------------------------------------------------------

// A 5x7 font for the logo's letters, each row 5 bits (the leftmost pixel
// the highest).
static const char logo_chars[] = "PAWSKING&";
static const u8 logo_font[][7] = {
    {30, 17, 17, 30, 16, 16, 16}, // P
    {14, 17, 17, 31, 17, 17, 17}, // A
    {17, 17, 17, 21, 21, 27, 17}, // W
    {15, 16, 16, 14, 1, 1, 30},   // S
    {17, 18, 20, 24, 20, 18, 17}, // K
    {14, 4, 4, 4, 4, 4, 14},      // I
    {17, 25, 21, 19, 17, 17, 17}, // N
    {15, 16, 16, 23, 17, 17, 15}, // G
    {12, 18, 20, 8, 21, 18, 13},  // &
};

#define LOGO_SCALE 3
#define LOGO_ADVANCE (6 * LOGO_SCALE)

// The letters' pixels go into the canvas as 1 (gold) or 2 (ivory: the
// ampersand); shading comes after.
static void logo_line(const char* s, int y0) {
    int w = 0;
    for (const char* c = s; *c; c++)
        w += *c == ' ' ? LOGO_ADVANCE / 2 : LOGO_ADVANCE;
    w -= LOGO_SCALE;
    int x = (LOGO_W - w) / 2;
    for (const char* c = s; *c; c++) {
        if (*c == ' ') {
            x += LOGO_ADVANCE / 2;
            continue;
        }
        int g = 0;
        while (logo_chars[g] != *c)
            g++;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (logo_font[g][row] & (16 >> col))
                    for (int j = 0; j < LOGO_SCALE * LOGO_SCALE; j++)
                        put(x + col * LOGO_SCALE + j % LOGO_SCALE,
                            y0 + row * LOGO_SCALE + j / LOGO_SCALE, *c == '&' ? 2 : 1);
        x += LOGO_ADVANCE;
    }
}

// Shades the letters: gold from a glint at the top through to bronze at
// the foot of each line (lines LOGO_LINE_H tall from y0s), the ampersand
// ivory; then a dark outline round them and a shadow down and right.
#define LOGO_LINE_H (7 * LOGO_SCALE)
static void logo_shade(const int* y0s, int lines) {
    static const u8 gold[LOGO_LINE_H] = {3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5,
                                         5, 5, 5, 6, 6, 6, 6, 6, 7, 7};
    for (int l = 0; l < lines; l++)
        for (int y = y0s[l]; y < y0s[l] + LOGO_LINE_H; y++)
            for (int x = 0; x < LOGO_W; x++) {
                int c = get(x, y);
                if (c == 1)
                    put(x, y, gold[y - y0s[l]]);
                else if (c == 2)
                    put(x, y, y - y0s[l] < 2 ? 3 : y - y0s[l] > LOGO_LINE_H - 5 ? 9 : 8);
            }
    // Outline (1) every empty pixel beside a letter, then the shadow (2)
    // two pixels down and right of every letter or outline pixel.
    for (int y = 0; y < LOGO_H; y++)
        for (int x = 0; x < LOGO_W; x++) {
            if (get(x, y))
                continue;
            for (int k = 0; k < 9; k++) {
                int c = get(x + k % 3 - 1, y + k / 3 - 1);
                if (c >= 3) {
                    put(x, y, 1);
                    break;
                }
            }
        }
    for (int y = LOGO_H - 1; y >= 0; y--)
        for (int x = LOGO_W - 1; x >= 0; x--)
            if (!get(x, y) && get(x - 2, y - 2) && get(x - 2, y - 2) != 2)
                put(x, y, 2);
}

static u32* build_logo(u32* out) {
    canvas_clear();
    static const int y0s[2] = {4, 36};
    logo_line("PAWS &", y0s[0]);
    logo_line("KINGS", y0s[1]);
    logo_shade(y0s, 2);
    const u32* tiles = out;
    for (int f = 0; f < LOGO_FRAMES; f++)
        out = pack((f & 1) * 64, (f >> 1) * 32, 64, 32, out);
    sprite(SPR_LOGO, SPRITE_64x32, LOGO_FRAMES, PAL_LOGO, tiles);
    return out;
}

static void build_sprites(void) {
    u32* out = obj_tiles;
    const u32* tiles;

    canvas_clear();
    for (int f = 0; f < PIECE_FRAMES; f++)
        paint(piece_art[f], 16, piece_keys, f * 16, 0);
    tiles = out;
    for (int f = 0; f < PIECE_FRAMES; f++)
        out = pack(f * 16, 0, 16, 16, out);
    sprite(SPR_PIECE, SPRITE_16x16, PIECE_FRAMES, PAL_IVORY, tiles);

    canvas_clear();
    for (int f = 0; f < ICON_FRAMES; f++)
        paint(icon_art[f], 8, piece_keys, f * 8, 0);
    tiles = out;
    for (int f = 0; f < ICON_FRAMES; f++)
        out = pack(f * 8, 0, 8, 8, out);
    sprite(SPR_ICON, SPRITE_8x8, ICON_FRAMES, PAL_IVORY, tiles);

    canvas_clear();
    draw_cursor(0);
    draw_cursor(1);
    tiles = out;
    out = pack(0, 0, 16, 16, out);
    out = pack(16, 0, 16, 16, out);
    sprite(SPR_CURSOR, SPRITE_16x16, 2, PAL_UI, tiles);

    canvas_clear();
    tiles = out;
    for (int f = 0; f < TARGET_FRAMES; f++) {
        draw_target(f);
        out = pack(f * 16, 0, 16, 16, out);
    }
    sprite(SPR_TARGET, SPRITE_16x16, TARGET_FRAMES, PAL_TARGET, tiles);

    canvas_clear();
    tiles = out;
    for (int f = 0; f < 2; f++) {
        draw_mark(f);
        out = pack(f * 16, 0, 16, 16, out);
    }
    sprite(SPR_MARK, SPRITE_16x16, 2, PAL_UI, tiles);

    canvas_clear();
    tiles = out;
    for (int f = 0; f < PUFF_FRAMES; f++) {
        draw_puff(f);
        out = pack(f * PUFF_SIDE, 0, PUFF_SIDE, PUFF_SIDE, out);
    }
    sprite(SPR_PUFF, SPRITE_32x32, PUFF_FRAMES, PAL_PUFF, tiles);

    canvas_clear();
    tiles = out;
    for (int f = 0; f < SPARKLE_FRAMES; f++) {
        draw_sparkle(f);
        out = pack(f * 16, 0, 16, 16, out);
    }
    sprite(SPR_SPARKLE, SPRITE_16x16, SPARKLE_FRAMES, PAL_PUFF, tiles);

    out = build_logo(out);

    obj_tiles_used = (u16)((out - obj_tiles) / 8);
    for (int i = 0; i < SPRITE_COUNT; i++)
        sprite_table[i] = &sprites[i];
}

// The table and the sprites dimmed (art_set_dim): every colour but the
// logo's mixed most of the way toward a dark brown, at boot, so that
// dimming is only a copy.
#define DIM_TINT COLOR_RGB(16, 10, 8)
#define DIM_AMOUNT 150
static Color dim_bg[BG_BANKS * 16] SERVAL_EWRAM_BSS;
static Color dim_obj[OBJ_PALETTES * 16] SERVAL_EWRAM_BSS;

static void build_dim(void) {
    for (int i = 0; i < BG_BANKS * 16; i++)
        dim_bg[i] = color_mix(bg_palettes[i], DIM_TINT, DIM_AMOUNT);
    // The logo stays bright over the dimmed table on the title.
    for (int i = 0; i < OBJ_PALETTES * 16; i++)
        dim_obj[i] = i / 16 == PAL_LOGO ? (&obj_palettes[0][0])[i]
                                        : color_mix((&obj_palettes[0][0])[i], DIM_TINT, DIM_AMOUNT);
}

void art_set_dim(bool on) {
    // Colour 0 of bank 0 is the backdrop, left alone; the alert bank's
    // colours are the flash's (view.c writes them).
    tileset_set_colors(1, (on ? dim_bg : bg_palettes) + 1, BG_BANKS * 16 - 1);
    sprite_set_colors(SPR_PIECE, 0, on ? dim_obj : &obj_palettes[0][0], OBJ_PALETTES * 16);
}

// --- Building and loading
// -------------------------------------------------------------------------

void art_build(void) {
    for (int i = 0; i < 256; i++)
        tile_head[i] = 0xFFFF;
    u32 blank[8] = {0};
    bg_tiles_used = 0;
    add_tile(blank); // tile 0
    build_palettes();
    build_table();
    table_tileset.tile_count = bg_tiles_used;
    build_sprites();
    build_dim();
}

void art_load(void) {
    tileset_load(&table_tileset);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    camera_set(0, 0);
    map_load(&table_layer);
    map_set_scroll(BOARD_BG, MAP_SCROLL_X, 0);
}
