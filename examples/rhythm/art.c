// The pictures, all drawn at boot (art_build) into EWRAM buffers that
// tileset_load, map_load and sprite_group_load copy to VRAM, as racer and
// servle do. No image files.
//
// The field (background 2) is one picture of the whole screen: the lanes in
// two pairs at the sides, dark runways between bright rails; at their foot
// the pads, a paw print set in an oval socket, each in its lane's colour
// with the button that plays it written under it (a left and a right
// triangle for the D-pad, B and A); between the pairs a stage under a night
// sky, stars, a spotlight falling on a round floor, and the savanna's grass
// and acacias black against the horizon. Its pixels are colour numbers
// given by pixel functions (field_pixel); the colours themselves pulse with
// the beat (field_pulse: play.c writes a stage of them a frame) and the pads
// flash (pad_flash), so the picture never changes, only its palettes. Each
// pad's tiles have their own bank, whose colours 1-7 copy the field's.
//
// The panel (background 1, in front of the sprites) is the strip across
// the top that the notes come out from under, with the song's progress
// groove; the text layer writes the song and the score on it.
//
// The sprites are drawn by functions too (the notes and pads share one paw
// print, made of five ellipses), but for the serval, an ASCII picture in
// parts: its head, body and legs drawn as their left halves and mirrored,
// its arms and tail laid over them, so the dance's frames share everything
// but an arm and a bob. The words and digits are a little 5x7 font, doubled.
//
// Tiles of the backgrounds are kept once: a tile already made (the plain
// floor, the empty sky) is used again rather than stored twice.

#include "art.h"

const u8 lane_x[LANES] = {24, 56, 184, 216};

// --- Colours ----------------------------------------------------------------------------

#define BLACK COLOR_RGB(0, 0, 0)
#define WHITE COLOR_RGB(255, 255, 255)

static const Color lane_hue[LANES] = {
    COLOR_RGB(255, 96, 140), // rose
    COLOR_RGB(255, 184, 48), // amber
    COLOR_RGB(64, 216, 150), // mint
    COLOR_RGB(88, 156, 255), // azure
};

// The field's colours at rest and at the beat's peak.
enum {
    F_BACKDROP,
    F_FLOOR, // a lane's runway
    F_GLOW,  // beside the rails, and between a pair's lanes
    F_RAIL,  // the pairs' edges
    F_STAGE, // the stage's floor
    F_SPOT,  // the spotlight, dithered over the sky
    F_STAR,
    F_SHADOW, // the grass and the acacias, black on the horizon
    F_SKY,    // 8-11: the sky, top to horizon
    F_SKY_LAST = F_SKY + 3,
};
_Static_assert(F_SKY_LAST + 1 == PULSE_COLORS, "the pulse covers the field's colours");
_Static_assert(F_SHADOW == FIELD_SHARED, "the pads' banks copy colours 1-7");

static const Color field_rest[PULSE_COLORS] = {
    COLOR_RGB(16, 12, 40),    COLOR_RGB(20, 16, 40),  COLOR_RGB(44, 36, 86),
    COLOR_RGB(110, 90, 200),  COLOR_RGB(110, 66, 38), COLOR_RGB(96, 80, 104),
    COLOR_RGB(180, 180, 230), COLOR_RGB(12, 8, 22),   COLOR_RGB(16, 12, 40),
    COLOR_RGB(28, 20, 62),    COLOR_RGB(50, 30, 88),  COLOR_RGB(92, 46, 106),
};
static const Color field_peak[PULSE_COLORS] = {
    COLOR_RGB(28, 22, 64),    COLOR_RGB(34, 28, 66),   COLOR_RGB(84, 70, 150),
    COLOR_RGB(230, 215, 255), COLOR_RGB(160, 104, 58), COLOR_RGB(220, 190, 150),
    COLOR_RGB(255, 255, 255), COLOR_RGB(12, 8, 22),    COLOR_RGB(28, 22, 64),
    COLOR_RGB(46, 34, 96),    COLOR_RGB(78, 48, 124),  COLOR_RGB(134, 68, 144),
};

// A pad's colours (8-13 of its bank).
enum { P_RIM, P_PAW, P_PAW_LIGHT, P_SOCKET, P_LABEL, P_PAW_EDGE };

// The panel's colours.
enum { N_BASE = 1, N_TOP, N_BORDER, N_GROOVE, N_GROOVE_TOP, N_SHADOW };
static const Color panel_colors[] = {
    0,
    COLOR_RGB(28, 22, 52),
    COLOR_RGB(60, 50, 100),
    COLOR_RGB(150, 120, 230),
    COLOR_RGB(10, 8, 22),
    COLOR_RGB(4, 2, 10),
    COLOR_RGB(8, 6, 16),
};

Color field_pulse[PULSE_LEVELS][PULSE_COLORS];
Color pad_flash[LANES][FLASH_LEVELS][PAD_COLORS];

static u16 bg_palettes[BG_BANKS * 16];
static u16 obj_palettes[OBJ_PALETTES * 16];

// Mixes toward `to` by `level` of `levels - 1` steps.
static Color stage_mix(Color from, Color to, int level, int levels) {
    return color_mix(from, to, (u32)((level * 256 + (levels - 1) / 2) / (levels - 1)));
}

static void pad_colors(Color* out, Color h, int level) {
    static Color rest[PAD_COLORS], peak[PAD_COLORS];
    rest[P_RIM] = color_mix(h, BLACK, 110);
    peak[P_RIM] = h;
    rest[P_PAW] = color_mix(h, BLACK, 150);
    peak[P_PAW] = color_mix(h, WHITE, 140);
    rest[P_PAW_LIGHT] = color_mix(h, BLACK, 90);
    peak[P_PAW_LIGHT] = WHITE;
    rest[P_SOCKET] = COLOR_RGB(22, 16, 40);
    peak[P_SOCKET] = color_mix(h, BLACK, 150);
    rest[P_LABEL] = color_mix(h, BLACK, 60);
    peak[P_LABEL] = color_mix(h, WHITE, 160);
    rest[P_PAW_EDGE] = COLOR_RGB(10, 6, 20);
    peak[P_PAW_EDGE] = color_mix(h, BLACK, 180);
    for (int c = 0; c < PAD_COLORS; c++)
        out[c] = stage_mix(rest[c], peak[c], level, FLASH_LEVELS);
}

// A word palette: an outline, then five colours from `top` to `foot`.
static void word_palette(u16* p, Color outline, Color top, Color foot) {
    p[1] = outline;
    for (int i = 0; i < 5; i++)
        p[2 + i] = color_mix(top, foot, (u32)(i * 64));
}

static void build_palettes(void) {
    for (int l = 0; l < PULSE_LEVELS; l++)
        for (int c = 0; c < PULSE_COLORS; c++)
            field_pulse[l][c] = stage_mix(field_rest[c], field_peak[c], l, PULSE_LEVELS);
    for (int c = 0; c < PULSE_COLORS; c++)
        bg_palettes[BANK_FIELD * 16 + c] = field_rest[c];
    for (int lane = 0; lane < LANES; lane++) {
        for (int l = 0; l < FLASH_LEVELS; l++)
            pad_colors(pad_flash[lane][l], lane_hue[lane], l);
        u16* bank = &bg_palettes[(BANK_PAD + lane) * 16];
        for (int c = 1; c <= FIELD_SHARED; c++)
            bank[c] = field_rest[c];
        for (int c = 0; c < PAD_COLORS; c++)
            bank[PAD_COLOR0 + c] = pad_flash[lane][0][c];
    }
    for (u32 c = 0; c < sizeof panel_colors / sizeof panel_colors[0]; c++)
        bg_palettes[BANK_PANEL * 16 + c] = panel_colors[c];

    for (int lane = 0; lane < LANES; lane++) {
        u16* p = &obj_palettes[(PAL_LANE + lane) * 16];
        Color h = lane_hue[lane];
        p[1] = color_mix(h, BLACK, 180);
        p[2] = h;
        p[3] = color_mix(h, WHITE, 110);
        p[4] = color_mix(h, WHITE, 220);
        p[5] = color_mix(h, BLACK, 80);
    }
    static const Color serval[] = {
        0,
        COLOR_RGB(44, 26, 18),    // K outline
        COLOR_RGB(232, 168, 72),  // G coat
        COLOR_RGB(250, 236, 204), // C cream
        COLOR_RGB(40, 24, 16),    // S spots
        COLOR_RGB(240, 140, 152), // P pink
        COLOR_RGB(168, 212, 88),  // E eyes
        COLOR_RGB(255, 255, 255), // W glint
        COLOR_RGB(72, 46, 36),    // D the ears' dark rims
    };
    for (u32 c = 0; c < sizeof serval / sizeof serval[0]; c++)
        obj_palettes[PAL_SERVAL * 16 + c] = serval[c];
    word_palette(&obj_palettes[PAL_GOLD * 16], COLOR_RGB(60, 28, 8), COLOR_RGB(255, 252, 210),
                 COLOR_RGB(240, 140, 30));
    word_palette(&obj_palettes[PAL_MINT * 16], COLOR_RGB(8, 48, 40), COLOR_RGB(220, 255, 240),
                 COLOR_RGB(40, 190, 140));
    word_palette(&obj_palettes[PAL_GREY * 16], COLOR_RGB(24, 24, 40), COLOR_RGB(210, 210, 230),
                 COLOR_RGB(110, 110, 150));
    word_palette(&obj_palettes[PAL_WHITE * 16], COLOR_RGB(30, 24, 60), COLOR_RGB(255, 255, 255),
                 COLOR_RGB(190, 180, 240));
}

// --- Shapes -------------------------------------------------------------------------------

// Inside the ellipse of middle (cx, cy) and radii (rx, ry), all in the same
// units.
static bool in_ellipse(int x, int y, int cx, int cy, int rx, int ry) {
    int dx = x - cx, dy = y - cy;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

// A paw print in a size x size box at (x0, y0): four toes over a broad
// pad, as five ellipses in 128ths of the box.
static bool in_paw(int x, int y, int x0, int y0, int size) {
    if (x < x0 || y < y0 || x >= x0 + size || y >= y0 + size)
        return false;
    int u = (2 * (x - x0) + 1) * 64 / size;
    int v = (2 * (y - y0) + 1) * 64 / size;
    return in_ellipse(u, v, 64, 86, 32, 27) || in_ellipse(u, v, 20, 50, 13, 16) ||
           in_ellipse(u, v, 46, 24, 13, 16) || in_ellipse(u, v, 82, 24, 13, 16) ||
           in_ellipse(u, v, 108, 50, 13, 16);
}

// The 5x7 letters, '#' set. Only the ones used.
static const char font_chars[] = "PERFCTGODMIS0123456789<>BA";
static const char* const font[][7] = {
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}, // P
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}, // E
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}, // R
    {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}, // F
    {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}, // C
    {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}, // T
    {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}, // G
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // O
    {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}, // D
    {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}, // M
    {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}, // I
    {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}, // S
    {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}, // 0
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}, // 1
    {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}, // 2
    {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}, // 3
    {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}, // 4
    {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}, // 5
    {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}, // 6
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}, // 7
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}, // 8
    {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}, // 9
    {"....#", "...##", "..###", ".####", "..###", "...##", "....#"}, // < (the D-pad's left)
    {"#....", "##...", "###..", "####.", "###..", "##...", "#...."}, // >
    {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}, // B
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // A
};

// Whether pixel (x, y) of glyph c (5x7) is set; false outside it.
static bool glyph_pixel(char c, int x, int y) {
    if (x < 0 || y < 0 || x >= 5 || y >= 7)
        return false;
    int g = 0;
    while (font_chars[g] && font_chars[g] != c)
        g++;
    return font_chars[g] && font[g][y][x] == '#';
}

// --- Background tiles ---------------------------------------------------------------------

// Room for the backgrounds' tiles; art_build() counts them into bg_tiles_used.
#define BG_TILE_MAX 640

static u32 bg_tiles[BG_TILE_MAX * 8] SERVAL_EWRAM_BSS;
static u16 bg_tiles_used;

// The tile with these 8 words: an existing one if any is the same, else a
// new one (tile 0 is the blank one, made first).
static u16 add_tile(const u32* words) {
    for (u16 t = 0; t < bg_tiles_used; t++) {
        const u32* old = bg_tiles + t * 8;
        int i = 0;
        while (i < 8 && old[i] == words[i])
            i++;
        if (i == 8)
            return t;
    }
    if (bg_tiles_used == BG_TILE_MAX)
        return 0; // never with these pictures: art_build() checks the count in debug builds
    u32* out = bg_tiles + bg_tiles_used * 8;
    for (int i = 0; i < 8; i++)
        out[i] = words[i];
    return bg_tiles_used++;
}

// The tile whose top-left pixel is (x, y) of a picture given by its pixel
// function, 4 bits a pixel.
static u16 make_tile(int (*pixel)(int x, int y), int x, int y) {
    u32 words[8];
    for (int row = 0; row < 8; row++) {
        u32 word = 0;
        for (int col = 0; col < 8; col++)
            word |= (u32)pixel(x + col, y + row) << (4 * col);
        words[row] = word;
    }
    return add_tile(words);
}

// --- The field ----------------------------------------------------------------------------

#define PAD_TILE_ROW 15 // the pads' tiles: rows 15-18 of each lane (lines 120-151)
#define PAD_TILE_ROWS 4
#define LABEL_Y 145 // the buttons' names under the pads: lines 145-151
#define SOCKET_RX 14
#define SOCKET_RY 12
#define PAD_PAW 20 // the pad's paw print: 20x20

static const char lane_label[LANES] = {'<', '>', 'B', 'A'};

// The lane whose 32 columns hold x, or -1.
static int lane_of(int x) {
    for (int l = 0; l < LANES; l++)
        if (x >= lane_x[l] - LANE_W / 2 && x < lane_x[l] + LANE_W / 2)
            return l;
    return -1;
}

// A pad at the foot of lane l: the socket, the paw print in it, the label.
// Returns a colour, or -1 where the runway shows through.
static int pad_pixel(int l, int x, int y) {
    int cx = lane_x[l];
    int px0 = cx - PAD_PAW / 2, py0 = PAD_Y - PAD_PAW / 2;
    if (in_paw(x, y, px0, py0, PAD_PAW)) {
        if (!in_paw(x - 1, y - 1, px0, py0, PAD_PAW))
            return PAD_COLOR0 + P_PAW_LIGHT;
        return PAD_COLOR0 + P_PAW;
    }
    // In half pixels from the socket's middle (between two pixels).
    int u = 2 * (x - cx) + 1, v = 2 * (y - PAD_Y) + 1;
    if (in_ellipse(u, v, 0, 0, 2 * SOCKET_RX, 2 * SOCKET_RY)) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (in_paw(x + dx, y + dy, px0, py0, PAD_PAW))
                    return PAD_COLOR0 + P_PAW_EDGE;
        if (!in_ellipse(u, v, 0, 0, 2 * SOCKET_RX - 3, 2 * SOCKET_RY - 3))
            return PAD_COLOR0 + P_RIM;
        return PAD_COLOR0 + P_SOCKET;
    }
    if (glyph_pixel(lane_label[l], x - (cx - 3), y - LABEL_Y))
        return PAD_COLOR0 + P_LABEL;
    return -1;
}

// A lane's runway: dark, a glow inside each pair's rails, a faint line
// between the pair's two lanes.
static int runway_pixel(int x) {
    int pair_left = x < STAGE_X ? lane_x[0] - LANE_W / 2 : lane_x[2] - LANE_W / 2;
    int i = x - pair_left; // 0-63 across the pair
    if (i == 0 || i == 2 * LANE_W - 1)
        return F_RAIL;
    if (i == 1 || i == 2 * LANE_W - 2 || i == LANE_W - 1 || i == LANE_W)
        return F_GLOW;
    return F_FLOOR;
}

// Small hash of a pixel, for the stars.
static u32 hash(int x, int y) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// The sky's band at line y, dithered where two meet.
static int sky_pixel(int x, int y) {
    static const u8 edges[3] = {56, 88, 112};
    int band = 0;
    while (band < 3 && y >= edges[band] + 2)
        band++;
    if (band < 3 && y >= edges[band] - 2 && ((x + y) & 1))
        band++;
    return F_SKY + band;
}

// The horizon's black grass and two acacias.
static bool in_shadow(int x, int y) {
    int grass = 128 - ((x * 7) % 5) - ((x * 3) % 4) - (x % 9 == 0 ? 3 : 0);
    if (y >= grass)
        return true;
    // An acacia: a trunk and a flat crown; a smaller one to the right.
    if ((x == 83 || x == 84) && y >= 108 && y < 130)
        return true;
    if (in_ellipse(2 * x, 2 * y, 168, 216, 26, 7) || in_ellipse(2 * x, 2 * y, 158, 210, 12, 5))
        return true;
    if (x == 156 && y >= 114 && y < 130)
        return true;
    return in_ellipse(2 * x, 2 * y, 312, 228, 20, 5);
}

static int stage_pixel(int x, int y) {
    // The floor: an ellipse at the serval's feet, its rim catching the light.
    int u = 2 * (x - STAGE_X) + 1, v = 2 * (y - STAGE_FOOT) + 1;
    if (in_ellipse(u, v, 0, 0, 92, 20))
        return in_ellipse(u, v + 4, 0, 0, 92, 20) ? F_STAGE : F_RAIL;
    if (in_shadow(x, y))
        return F_SHADOW;
    // The spotlight: a cone from the panel down to the floor, every other
    // pixel lit.
    int half = 6 + (y - FIELD_TOP) * 34 / (STAGE_FOOT - FIELD_TOP);
    if (y >= FIELD_TOP && x >= STAGE_X - half && x < STAGE_X + half && ((x + y) & 1) == 0)
        return F_SPOT;
    if (y < 100 && hash(x, y) % 97 == 0)
        return F_STAR;
    return sky_pixel(x, y);
}

static int field_pixel(int x, int y) {
    int l = lane_of(x);
    if (l < 0)
        return stage_pixel(x, y);
    if (y >= PAD_TILE_ROW * 8 && y < (PAD_TILE_ROW + PAD_TILE_ROWS) * 8) {
        int c = pad_pixel(l, x, y);
        if (c >= 0)
            return c;
    }
    return runway_pixel(x);
}

#define FIELD_MAP_W 15 // metatiles: the whole screen
#define FIELD_MAP_H 10
static Metatile field_metatiles[FIELD_MAP_W * FIELD_MAP_H] SERVAL_EWRAM_BSS;
static u16 field_cells[FIELD_MAP_W * FIELD_MAP_H] SERVAL_EWRAM_BSS;

static u16 field_se(int tx, int ty) {
    int x = tx * 8, y = ty * 8;
    int l = lane_of(x);
    int bank = l >= 0 && ty >= PAD_TILE_ROW && ty < PAD_TILE_ROW + PAD_TILE_ROWS ? BANK_PAD + l
                                                                                 : BANK_FIELD;
    return MAP_SE(make_tile(field_pixel, x, y), bank, 0);
}

// --- The panel ----------------------------------------------------------------------------

#define PANEL_MAP_H 2 // metatiles: lines 0-31, of which the panel is 0-21

static int panel_pixel(int x, int y) {
    if (y >= FIELD_TOP)
        return 0;
    if (y == FIELD_TOP - 2)
        return N_BORDER;
    if (y == FIELD_TOP - 1)
        return N_SHADOW;
    // The groove: four lines, rounded at its ends.
    int g = y - (PROGRESS_Y - 2); // 0-3
    if (g >= 0 && g < 4 && x >= PROGRESS_X - 2 && x < PROGRESS_X + PROGRESS_W + 2) {
        bool end = x == PROGRESS_X - 2 || x == PROGRESS_X + PROGRESS_W + 1;
        if (!(end && (g == 0 || g == 3)))
            return g == 0 ? N_GROOVE_TOP : N_GROOVE;
    }
    return y == 0 ? N_TOP : N_BASE;
}

static Metatile panel_metatiles[FIELD_MAP_W * PANEL_MAP_H] SERVAL_EWRAM_BSS;
static u16 panel_cells[FIELD_MAP_W * PANEL_MAP_H] SERVAL_EWRAM_BSS;

static void build_layer(Metatile* metatiles, u16* cells, int h, int (*pixel)(int, int), int bank,
                        bool field) {
    for (int my = 0; my < h; my++) {
        for (int mx = 0; mx < FIELD_MAP_W; mx++) {
            Metatile* m = &metatiles[my * FIELD_MAP_W + mx];
            for (int i = 0; i < 4; i++) {
                int tx = 2 * mx + (i & 1), ty = 2 * my + (i >> 1);
                m->se[i] =
                    field ? field_se(tx, ty) : MAP_SE(make_tile(pixel, tx * 8, ty * 8), bank, 0);
            }
            m->collision = MAP_EMPTY;
            cells[my * FIELD_MAP_W + mx] = (u16)(my * FIELD_MAP_W + mx);
        }
    }
}

static const Tileset tileset = {
    .tiles = bg_tiles,
    .palettes = bg_palettes,
    .palette_count = BG_BANKS,
};
static u16 tileset_tiles; // bg_tiles_used once built

static const MapLayer field_layer = {
    .width = FIELD_MAP_W,
    .height = FIELD_MAP_H,
    .cells = field_cells,
    .metatiles = field_metatiles,
    .metatile_count = FIELD_MAP_W * FIELD_MAP_H,
    .bg = FIELD_BG,
    .flags = MAP_LAYER_FIXED,
};

static const MapLayer panel_layer = {
    .width = FIELD_MAP_W,
    .height = PANEL_MAP_H,
    .cells = panel_cells,
    .metatiles = panel_metatiles,
    .metatile_count = FIELD_MAP_W * PANEL_MAP_H,
    .bg = PANEL_BG,
    .flags = MAP_LAYER_FIXED,
};

// --- Sprites ------------------------------------------------------------------------------

// The canvas sprites are painted in, a colour number a pixel, then cut into
// tiles (pack). Big enough for a word's two halves and the serval.
#define CANVAS_W 128
#define CANVAS_H 64
static u8 canvas[CANVAS_W * CANVAS_H] SERVAL_EWRAM_BSS;

static void canvas_clear(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 0;
}

static void put(int x, int y, int c) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        canvas[y * CANVAS_W + x] = (u8)c;
}

static u8 at(int x, int y) {
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
        return 0;
    return canvas[y * CANVAS_W + x];
}

// Cuts a w x h block from (x0, y0) of the canvas into 4bpp tiles, row by
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

// Every frame's tiles: the note 4, the burst 48, the serval 192, the words
// 224, the digits 40, the marker 1.
#define OBJ_TILES                                                                                  \
    (4 + BURST_FRAMES * 16 + DANCE_FRAMES * 64 + WORD_COUNT * WORD_PIECES * 8 + 10 * 4 + 1)
static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

static SpriteAsset sprites[SPRITE_COUNT] SERVAL_EWRAM_BSS;
static const SpriteAsset* sprite_table[SPRITE_COUNT];

static const SpriteGroup sprite_group = {
    .palettes = obj_palettes,
    .sprite_count = SPRITE_COUNT,
    .palette_count = OBJ_PALETTES,
};

static void sprite(int id, u8 size, int frames, int palette, const u32* tiles, int ox, int oy) {
    sprites[id] = (SpriteAsset){
        .tiles = tiles,
        .size = size,
        .frame_count = (u8)frames,
        .palette_slot = (u8)palette,
        .origin_x = (s8)ox,
        .origin_y = (s8)oy,
    };
}

// Around everything painted with a colour from 2 up: colour 1, the outline.
static void outline(int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (at(x, y))
                continue;
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; dy++)
                for (int dx = -1; dx <= 1 && !edge; dx++)
                    edge = at(x + dx, y + dy) >= 2;
            if (edge)
                put(x, y, 1);
        }
}

// The note: a paw print, lit from the top left, shaded at the bottom right.
static u32* build_note(u32* out) {
    canvas_clear();
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (in_paw(x, y, 1, 1, 14))
                put(x, y,
                    !in_paw(x - 1, y - 1, 1, 1, 14)   ? 3
                    : !in_paw(x + 1, y + 1, 1, 1, 14) ? 5
                                                      : 2);
    outline(16, 16);
    sprite(SPR_NOTE, SPRITE_16x16, 1, PAL_LANE, out, 8, 8);
    return pack(0, 0, 16, 16, out);
}

// The burst: a ring opening from the pad, with eight sparks around it,
// white, then the lane's light colour, then its colour, only the sparks.
static u32* build_burst(u32* out) {
    static const s8 dirs[8][2] = {{16, 0},  {11, 11},   {0, 16},  {-11, 11},
                                  {-16, 0}, {-11, -11}, {0, -16}, {11, -11}};
    static const u8 radius[BURST_FRAMES] = {6, 10, 13};
    static const u8 ring[BURST_FRAMES] = {4, 3, 0};
    static const u8 spark[BURST_FRAMES] = {4, 4, 3};
    const u32* first = out;
    for (int f = 0; f < BURST_FRAMES; f++) {
        canvas_clear();
        int r = radius[f];
        if (ring[f]) {
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    int u = 2 * x - 31, v = 2 * y - 31, d = u * u + v * v;
                    if (d >= (2 * r - 1) * (2 * r - 1) && d <= (2 * r + 1) * (2 * r + 1))
                        put(x, y, ring[f]);
                }
        }
        for (int i = 0; i < 8; i++) {
            int sx = 16 + dirs[i][0] * (r + 2) / 16, sy = 16 + dirs[i][1] * (r + 2) / 16;
            put(sx, sy, spark[f]);
            put(sx - 1, sy, 2);
            put(sx + 1, sy, 2);
            put(sx, sy - 1, 2);
            put(sx, sy + 1, 2);
        }
        out = pack(0, 0, 32, 32, out);
    }
    sprite(SPR_BURST, SPRITE_32x32, BURST_FRAMES, PAL_LANE, first, 16, 16);
    return out;
}

// The serval, facing us: '.' transparent, K outline, G its coat, C cream,
// S spots, P pink, E eyes, W a glint, D the ears' dark rims.
static const char serval_keys[] = " KGCSPEWD";

// Its left halves (16 wide), mirrored for the right: the head and its great
// ears (from the figure's top), the body (from line 26), the legs (42).
static const char* const serval_head[26] = {
    "...KKKK.........", "..KDDDDK........", "..KDCCDDK.......", ".KDCCCCDK.......",
    ".KDCPPCCDK......", ".KDCPPPCDK......", ".KDCPPPCDK......", ".KDCPPPCDK......",
    ".KDCCPPCDK......", "..KDCPPCDKKKK...", "..KDCCCDKGGGGKKK", "...KDCCKGGSGGGGG",
    "....KDKGSGGGSGGG", ".....KGGGGGGGGGG", ".....KGGGGGGGGGG", ".....KGKKKKGGGGG",
    ".....KGKEWKGGGGG", ".....KGKEEKGGGGC", ".....KGGKKGGGGCC", ".....KGGGGGGGCCC",
    "......KGGGGGCCPP", "......KGGGCCCCKP", ".......KGCCCCCCK", ".......KGCCCCKKC",
    "........KCCCCCCC", ".........KKCCCCC",
};
static const char* const serval_body[16] = {
    "..........KGCCCC", ".........KGGCCCC", "........KGGSCCCC", ".......KGSGGCCCC",
    "......KGGGGGCCCC", "......KGSGGSGCCC", "......KGGGGGGCCC", "......KGGSGGGCCC",
    "......KGSGGGSGCC", "......KGGGGGGGCC", "......KGGSGGGGCC", "......KGSGGGSGGC",
    "......KGGGGGGGGG", "......KGGSGGGSGG", "......KGGGGGGGGG", ".......KGGGGGGKK",
};
static const char* const serval_legs[13] = {
    ".......KGGGGGK..", ".......KGSGGGK..", ".......KGGGGGK..", ".......KGGGSGK..",
    ".......KGGGGGK..", ".......KGSGGGK..", ".......KGGGGGK..", ".......KGGGSGK..",
    ".......KGGGGGK..", ".......KGGGGGK..", "......KCCCCCCK..", "......KCCCCCCK..",
    "......KKKKKKKK..",
};
// An arm hanging, its top-left at (4, 29) of the figure (the right arm is
// it mirrored).
static const char* const serval_arm_down[14] = {
    "..KK..", ".KGGK.", ".KGGGK", ".KGSGK", ".KGGGK", ".KGGGK", ".KGGSK",
    ".KGGGK", ".KGSGK", ".KGGGK", ".KGGGK", "KCCCCK", "KCCCCK", ".KKKK.",
};
// The left arm raised, waving, its top-left at (-6, 11).
static const char* const serval_arm_up[20] = {
    ".KKKKKK..........", "KKCCCCKK.........", "KCCCCCCK.........", "KCCCCCCK.........",
    "KCCCCCCK.........", "KCCCCCCKK........", "KKCCCCGGK........", ".KKGGGGGKK.......",
    "..KKGGGGGKK......", "...KGGGGSGK......", "...KKGSGGGKK.....", "....KKGGGGGKK....",
    ".....KGGGGGGK....", ".....KKGGGGSKK...", "......KKGSGGGKK..", ".......KGGGGGGK..",
    ".......KKGGGGGKK.", "........KKGGGGSKK", ".........KGGSGGGG", ".........KKGGGGGG",
};
// The tail, ringed, curling up at its right, from line 28.
static const char* const serval_tail[15] = {
    "............................KK..", "...........................KSSK.",
    "...........................KSSK.", "...........................KGGK.",
    "...........................KGGK.", "...........................KSSK.",
    "...........................KGGK.", "...........................KGGK.",
    "..........................KSSK..", "..........................KGGK..",
    "..........................KGGK..", ".........................KSSK...",
    "........................KGGK....", ".......................KGGK.....",
    "......................KKK.......",
};

#define SERVAL_X 16 // the 32-wide figure's left in the 64x64 frame
#define SERVAL_Y 8  // its top: its feet on the frame's last line

static int serval_key(char c) {
    for (int k = 1; serval_keys[k]; k++)
        if (serval_keys[k] == c)
            return k;
    return 0;
}

// Rows of an ASCII part onto the canvas, its top-left at (x0, y0) of the
// figure; mirrored, flipped about the figure's middle.
static void serval_part(const char* const* rows, int h, int x0, int y0, bool mirror) {
    for (int y = 0; y < h; y++)
        for (int x = 0; rows[y][x]; x++)
            if (rows[y][x] != '.') {
                int fx = x0 + x;
                put(SERVAL_X + (mirror ? 31 - fx : fx), SERVAL_Y + y0 + y, serval_key(rows[y][x]));
            }
}

// A left half and its mirror.
static void serval_halves(const char* const* rows, int h, int y0) {
    serval_part(rows, h, 0, y0, false);
    serval_part(rows, h, 0, y0, true);
}

static u32* build_serval(u32* out) {
    const u32* first = out;
    for (int f = 0; f < DANCE_FRAMES; f++) {
        canvas_clear();
        int bob = f == DANCE_BOB ? 2 : 0; // all but the legs down two lines
        serval_halves(serval_legs, 13, 42);
        serval_part(serval_tail, 15, 0, 28 + bob, false);
        serval_halves(serval_body, 16, 26 + bob);
        serval_halves(serval_head, 26, bob);
        serval_part(serval_arm_down, 14, 4, 29 + bob, true);
        if (f == DANCE_PAW)
            serval_part(serval_arm_up, 20, -6, 11 + bob, false);
        else
            serval_part(serval_arm_down, 14, 4, 29 + bob, false);
        out = pack(0, 0, 64, 64, out);
    }
    sprite(SPR_SERVAL, SPRITE_64x64, DANCE_FRAMES, PAL_SERVAL, first, 32, 63);
    return out;
}

// Text in the doubled letters (10x14, 12 apart), its top-left at (x0, y0),
// shaded from colour 2 at the top to 6 at the foot.
static void letters(const char* s, int x0, int y0) {
    for (; *s; s++, x0 += 12)
        for (int y = 0; y < 14; y++)
            for (int x = 0; x < 10; x++)
                if (glyph_pixel(*s, x / 2, y / 2))
                    put(x0 + x, y0 + y, 2 + y * 5 / 14);
}

static u32* build_words(u32* out) {
    static const char* const words[WORD_COUNT] = {"PERFECT", "GOOD", "MISS", "1", "2", "3", "4"};
    const u32* first = out;
    for (int w = 0; w < WORD_COUNT; w++) {
        canvas_clear();
        int n = 0;
        while (words[w][n])
            n++;
        letters(words[w], (128 - (n * 12 - 2)) / 2, 1);
        outline(128, 16);
        for (int piece = 0; piece < WORD_PIECES; piece++)
            out = pack(piece * 32, 0, 32, 16, out);
    }
    sprite(SPR_WORD, SPRITE_32x16, WORD_COUNT * WORD_PIECES, PAL_WHITE, first, 0, 0);
    return out;
}

static u32* build_digits(u32* out) {
    const u32* first = out;
    for (int d = 0; d < 10; d++) {
        canvas_clear();
        char s[2] = {(char)('0' + d), 0};
        letters(s, 3, 1);
        outline(16, 16);
        out = pack(0, 0, 16, 16, out);
    }
    sprite(SPR_DIGIT, SPRITE_16x16, 10, PAL_WHITE, first, 0, 0);
    return out;
}

// The progress marker: a small diamond.
static u32* build_marker(u32* out) {
    canvas_clear();
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int d = int_abs(2 * x - 7) + int_abs(2 * y - 7);
            if (d <= 5)
                put(x, y, 2);
            else if (d <= 7)
                put(x, y, 1);
        }
    sprite(SPR_MARKER, SPRITE_8x8, 1, PAL_WHITE, out, 4, 4);
    return pack(0, 0, 8, 8, out);
}

static void build_sprites(void) {
    u32* out = obj_tiles;
    out = build_note(out);
    out = build_burst(out);
    out = build_serval(out);
    out = build_words(out);
    out = build_digits(out);
    out = build_marker(out);
    for (int i = 0; i < SPRITE_COUNT; i++)
        sprite_table[i] = &sprites[i];
}

// --- Building and loading -----------------------------------------------------------------

void art_build(void) {
    build_palettes();
    u32 blank[8] = {0};
    bg_tiles_used = 0;
    add_tile(blank); // tile 0
    build_layer(field_metatiles, field_cells, FIELD_MAP_H, field_pixel, BANK_FIELD, true);
    build_layer(panel_metatiles, panel_cells, PANEL_MAP_H, panel_pixel, BANK_PANEL, false);
    tileset_tiles = bg_tiles_used;
    build_sprites();
}

void art_load(void) {
    Tileset t = tileset;
    t.tile_count = tileset_tiles;
    tileset_load(&t);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
}

void art_show_play(void) {
    map_load(&field_layer);
    map_load(&panel_layer);
}

void art_hide_play(void) {
    map_unload(FIELD_BG);
    map_unload(PANEL_BG);
}
