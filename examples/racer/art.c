// Graphics: the road picture, the sky and the sprites, all drawn at boot
// (art_build) into EWRAM buffers that tileset_load, map_load and
// sprite_group_load copy to VRAM, as servle and blackjack do. No image files.
//
// The road picture (background 2) is one still picture of a straight road
// seen from above the car: on each line below the horizon the road for that
// line, road_width(y) pixels wide, centred at picture pixel 128 of the
// 256-pixel row, grass everywhere else. Its pixels hold no colours but a
// material (grass, rumble strip, tarmac, lane dash) and the line's group in
// its tile row (road_group()): colour 1 + material * 3 + group of the row's
// own palette bank. view.c gives those colours light, dark or haze every
// frame; that is all the stripes are. The picture is left-right symmetric,
// so only its left half is drawn: the right half is the same tiles flipped
// (MAP_SE_FLIP_H). Lines above the horizon are tile 0, transparent, so the
// sky shows through.
//
// The sky (background 3) is 512 pixels wide and 64 tall, and repeats
// sideways: bands of colour from blue-violet down to gold, dithered where
// they meet, a low sun, far hills and, in front of them, the savanna's
// flat-topped acacias in silhouette. Every shape is a function of x that
// repeats every 512 pixels, so the wrap shows no seam.
//
// The time of day is colours only: the same pictures, with the road's, the
// sky's and the sprites' colours mixed toward sunset and dusk at boot, in
// TIME_STEPS stages between each two (time_colors), for view.c to step
// through.
//
// Tiles are kept once: a tile already made (the plain grass, the tarmac,
// the sky's bands) is used again rather than stored twice.
//
// The sprites are ASCII pictures, one character per pixel ('.' transparent),
// painted into a canvas and cut into tiles. Symmetric ones (the jeep from
// behind and in front, the truck) are drawn as their left half, mirrored.
// Everything that stands by the road comes in four sizes: the full picture,
// and the picture reduced to a half, a quarter and an eighth, each pixel of
// a smaller one the commonest colour of the block of pixels it stands for.
// The logo and the countdown's numbers are a little 5x7 font, blown up.

#include "art.h"

// --- The tiles --------------------------------------------------------------------

// Room for the pictures' tiles (they take 145, the blank one included).
#define BG_TILE_MAX 256

static u32 bg_tiles[BG_TILE_MAX * 8] SERVAL_EWRAM_BSS;
u16 bg_tiles_used;

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
        return 0; // never with these pictures (art_build's total is far below)
    u32* out = bg_tiles + bg_tiles_used * 8;
    for (int i = 0; i < 8; i++)
        out[i] = words[i];
    return bg_tiles_used++;
}

// The tile whose top-left pixel is (x, y) of a picture given by its pixel
// function, 4 bits a pixel, pixel 0 in the low nibble.
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

// --- The road picture ---------------------------------------------------------------

#define ROAD_MAP_W 16 // metatiles: 256 pixels, a whole VRAM ring, so any offset works
// 176 pixels: the sky's 64 lines blank, the road's 96, and a blank strip of
// 16 that a shake brings in above the sky (art.h, SHAKE_MAX).
#define ROAD_MAP_H 11
#define ROAD_TILE_ROW0 (HORIZON / 8)
#define ROAD_HALF_TILES (ROAD_PICTURE_WIDTH / 2 / 8) // tile columns in the left half: 16

// Picture pixel (x, y), x in the left half (0-127): the material and group
// of the road or grass there, as a colour index of the line's bank.
static int road_pixel(int x, int y) {
    if (y < HORIZON || y >= SCREEN_H)
        return 0;
    int half = road_width(y) / 2;
    int u = ROAD_PICTURE_WIDTH / 2 - x; // pixels from the middle, 1 next to it
    // The parts of the road, as a share of its half width: the rumble strip
    // an eighth, the edge line inside it a fortieth (on the nearer lines
    // only), the centre dash a thirty-second each side.
    int rumble = (half + 4) / 8;
    int edge = half >= 20 ? (half + 20) / 40 : 0;
    int dash = (half + 8) / 32;
    int material;
    if (u > half)
        material = MAT_GRASS;
    else if (u > half - rumble)
        material = MAT_RUMBLE;
    else if (u > half - rumble - edge)
        return ROAD_EDGE_COLOR;
    else if (u <= dash)
        material = MAT_DASH;
    else
        material = MAT_SURFACE;
    return 1 + material * ROAD_GROUPS + road_group(y);
}

static Metatile road_metatiles[ROAD_MAP_W * ROAD_MAP_H] SERVAL_EWRAM_BSS;
static u16 road_cells[ROAD_MAP_W * ROAD_MAP_H] SERVAL_EWRAM_BSS;

// The screen entry of tile column tx, tile row ty of the picture: the left
// half's tile, or for the right half the mirror column's, flipped; bank =
// road row.
static u16 road_se(int tx, int ty) {
    if (ty < ROAD_TILE_ROW0 || ty >= ROAD_TILE_ROW0 + ROAD_ROWS)
        return MAP_SE(0, 0, 0);
    int bank = ty - ROAD_TILE_ROW0;
    if (tx < ROAD_HALF_TILES)
        return MAP_SE(make_tile(road_pixel, tx * 8, ty * 8), bank, 0);
    return MAP_SE(make_tile(road_pixel, (2 * ROAD_HALF_TILES - 1 - tx) * 8, ty * 8), bank,
                  MAP_SE_FLIP_H);
}

static void build_road(void) {
    for (int my = 0; my < ROAD_MAP_H; my++) {
        for (int mx = 0; mx < ROAD_MAP_W; mx++) {
            Metatile* m = &road_metatiles[my * ROAD_MAP_W + mx];
            m->se[0] = road_se(2 * mx, 2 * my);
            m->se[1] = road_se(2 * mx + 1, 2 * my);
            m->se[2] = road_se(2 * mx, 2 * my + 1);
            m->se[3] = road_se(2 * mx + 1, 2 * my + 1);
            m->collision = MAP_EMPTY;
            road_cells[my * ROAD_MAP_W + mx] = (u16)(my * ROAD_MAP_W + mx);
        }
    }
}

const MapLayer road_layer = {
    .width = ROAD_MAP_W,
    .height = ROAD_MAP_H,
    .cells = road_cells,
    .metatiles = road_metatiles,
    .metatile_count = ROAD_MAP_W * ROAD_MAP_H,
    .bg = ROAD_BG,
    .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP,
};

// --- The sky ---------------------------------------------------------------------------

#define SKY_MAP_W 32 // metatiles: 512 pixels
#define SKY_MAP_H 5  // the 64 lines above the horizon and a strip of its top colour
#define SKY_W (SKY_MAP_W * 16)
#define SKY_H 64 // the sky proper

enum {
    SKY_BANDS = 9,           // colours 1-9, top to bottom
    SKY_SUN = 1 + SKY_BANDS, // the sun's disc
    SKY_SUN_RIM,             // its edge
    SKY_HILLS,               // the far hills, hazy
    SKY_TREES,               // the acacias and the bush in front, dark
    SKY_COLORS = 15,
};

// An acacia: its trunk's x, the height of the canopy's flat top above the
// horizon line, and the canopy's half width, pixels.
typedef struct {
    s16 x;
    u8 height, half;
} Acacia;

static const Acacia acacias[] = {
    {38, 14, 11}, {120, 8, 5}, {142, 11, 8}, {236, 7, 4}, {300, 16, 13},
    {352, 9, 6},  {404, 6, 4}, {448, 12, 9}, {492, 8, 5},
};
#define ACACIA_COUNT ((int)(sizeof acacias / sizeof acacias[0]))

#define SUN_X 196
#define SUN_Y 44
#define SUN_R 11

// The hills' and the bush's heights above the bottom line, per column.
static u8 far_height[SKY_W] SERVAL_EWRAM_BSS;
static u8 near_height[SKY_W] SERVAL_EWRAM_BSS;

// A wave repeating `times` times across the sky, -amp .. amp pixels.
static int sky_wave(int x, int times, int phase, int amp) {
    return amp * fx_sin((u16)(x * times * (65536 / SKY_W) + phase)) / FX_ONE;
}

static void build_heights(void) {
    for (int x = 0; x < SKY_W; x++) {
        far_height[x] =
            (u8)(9 + sky_wave(x, 2, 0, 4) + sky_wave(x, 5, 9000, 2) + sky_wave(x, 11, 30000, 1));
        near_height[x] = (u8)(3 + (sky_wave(x, 29, 0, 2) + sky_wave(x, 47, 5000, 1)) / 2);
    }
}

// Whether (x, y) of the sky is part of an acacia.
static bool acacia_at(int x, int y) {
    int up = SKY_H - 1 - y; // pixels above the bottom line
    for (int i = 0; i < ACACIA_COUNT; i++) {
        const Acacia* a = &acacias[i];
        int dx = x - a->x;
        if (dx > SKY_W / 2)
            dx -= SKY_W;
        if (dx < -SKY_W / 2)
            dx += SKY_W;
        int adx = int_abs(dx);
        if (adx > a->half)
            continue;
        int top = a->height;
        // The canopy: a thin, flat-topped umbrella, ragged underneath.
        if (up == top && adx <= a->half - 2)
            return true;
        if (up == top - 1 && adx <= a->half)
            return true;
        if (up == top - 2 && adx <= a->half - 1 && (adx % 3 != 2 || adx < 3))
            return true;
        // The trunk, forking into the canopy.
        int fork = top - 3 - up; // 0 just under the canopy
        if (up < top - 2 && fork >= 0) {
            if (adx == 0 && fork >= 2)
                return true;
            if (fork < 2 && (adx == 2 - fork || adx == 0))
                return true;
        }
    }
    return false;
}

static int sky_pixel(int x, int y) {
    if (y >= SKY_H)
        return 1; // the spare strip a shake brings in at the top
    x &= SKY_W - 1;
    int up = SKY_H - 1 - y;
    if (up < near_height[x] || acacia_at(x, y))
        return SKY_TREES;
    if (up < far_height[x])
        return SKY_HILLS;
    int sx = x - SUN_X, sy = y - SUN_Y;
    int d2 = sx * sx + sy * sy;
    if (d2 <= (SUN_R - 2) * (SUN_R - 2))
        return SKY_SUN;
    if (d2 <= SUN_R * SUN_R)
        return SKY_SUN_RIM;
    // The bands, each about seven lines; in a band's last quarter the next
    // colour is mixed in on a checkerboard, in the quarter before on one
    // pixel in four.
    int t = y * SKY_BANDS * 4 / SKY_H;
    int band = t / 4, q = t % 4;
    if (band + 1 < SKY_BANDS) {
        if (q == 3 && ((x + y) & 1))
            band++;
        else if (q == 2 && (x & 3) == ((y & 1) << 1))
            band++;
    }
    return 1 + band;
}

static Metatile sky_metatiles[SKY_MAP_W * SKY_MAP_H] SERVAL_EWRAM_BSS;
static u16 sky_cells[SKY_MAP_W * SKY_MAP_H] SERVAL_EWRAM_BSS;

static void build_sky(void) {
    build_heights();
    for (int my = 0; my < SKY_MAP_H; my++) {
        for (int mx = 0; mx < SKY_MAP_W; mx++) {
            Metatile* m = &sky_metatiles[my * SKY_MAP_W + mx];
            int x = mx * 16, y = my * 16;
            m->se[0] = MAP_SE(make_tile(sky_pixel, x, y), SKY_BANK, 0);
            m->se[1] = MAP_SE(make_tile(sky_pixel, x + 8, y), SKY_BANK, 0);
            m->se[2] = MAP_SE(make_tile(sky_pixel, x, y + 8), SKY_BANK, 0);
            m->se[3] = MAP_SE(make_tile(sky_pixel, x + 8, y + 8), SKY_BANK, 0);
            m->collision = MAP_EMPTY;
            sky_cells[my * SKY_MAP_W + mx] = (u16)(my * SKY_MAP_W + mx);
        }
    }
}

const MapLayer sky_layer = {
    .width = SKY_MAP_W,
    .height = SKY_MAP_H,
    .cells = sky_cells,
    .metatiles = sky_metatiles,
    .metatile_count = SKY_MAP_W * SKY_MAP_H,
    .bg = SKY_BG,
    .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP,
};

// --- Sprite palettes ---------------------------------------------------------------------

// Jeep keys: K outline, T tyre, t tread and underside, G metal, H hub and
// headlamp, B body (khaki), D body in shade, L body's lit rim, R tail light,
// r its highlight, S seat, s seat's seam and the serval's spots, P number
// plate, O the serval's fur, W the white spots on the backs of its ears.
// The traffic uses the same keys, its body green, O and W its canvas roof.
static const char jeep_keys[] = ".KTtGHBDLRrSsPOW";

// Scenery keys: K outline, b and B bark (and the giraffe's mane), g G L
// leaves dark to light, e E H red earth dark to light (and the giraffe's
// patches), r R rock, Y the signs' yellow, C cream (the giraffe's coat), W
// white, D red.
static const char scenery_keys[] = ".KbBgGLeEHrRYCWD";

#define JEEP_COLORS                                                                                \
    0, COLOR_RGB(24, 16, 16), COLOR_RGB(40, 36, 36), COLOR_RGB(76, 70, 68),                        \
        COLOR_RGB(168, 164, 156), COLOR_RGB(216, 212, 200)
#define JEEP_BODY COLOR_RGB(196, 168, 104), COLOR_RGB(150, 124, 72), COLOR_RGB(228, 208, 150)
#define JEEP_REST COLOR_RGB(120, 70, 40), COLOR_RGB(70, 40, 24), COLOR_RGB(236, 228, 196)

static const u16 obj_palettes[OBJ_PALETTE_COUNT][16] = {
    [PAL_JEEP] = {JEEP_COLORS, JEEP_BODY, COLOR_RGB(120, 24, 24), COLOR_RGB(164, 48, 40), JEEP_REST,
                  COLOR_RGB(226, 162, 58), COLOR_RGB(240, 240, 230)},
    [PAL_JEEP_BRAKING] = {JEEP_COLORS, JEEP_BODY, COLOR_RGB(255, 40, 32), COLOR_RGB(255, 184, 160),
                          JEEP_REST, COLOR_RGB(226, 162, 58), COLOR_RGB(240, 240, 230)},
    [PAL_SCENERY] = {0, COLOR_RGB(24, 16, 12), COLOR_RGB(70, 44, 28), COLOR_RGB(120, 78, 44),
                     COLOR_RGB(52, 82, 36), COLOR_RGB(86, 120, 48), COLOR_RGB(132, 160, 70),
                     COLOR_RGB(120, 56, 32), COLOR_RGB(168, 88, 48), COLOR_RGB(206, 130, 78),
                     COLOR_RGB(96, 92, 88), COLOR_RGB(156, 150, 140), COLOR_RGB(232, 184, 48),
                     COLOR_RGB(246, 226, 170), COLOR_RGB(244, 244, 236), COLOR_RGB(200, 40, 32)},
    [PAL_TRAFFIC] = {JEEP_COLORS, COLOR_RGB(84, 120, 84), COLOR_RGB(56, 84, 60),
                     COLOR_RGB(132, 168, 120), COLOR_RGB(150, 28, 24), COLOR_RGB(200, 60, 44),
                     JEEP_REST, COLOR_RGB(220, 208, 168), COLOR_RGB(176, 160, 120)},
    // The logo and the countdown: a dark outline and shadow, then yellow at
    // the top of the letters down to red at their foot.
    // A glint of near white along each letter's top edges and a deep red
    // along its feet give the letters some depth.
    [PAL_LOGO] = {0, COLOR_RGB(56, 20, 36), COLOR_RGB(255, 250, 190), COLOR_RGB(255, 222, 96),
                  COLOR_RGB(255, 180, 60), COLOR_RGB(246, 128, 48), COLOR_RGB(214, 72, 44),
                  COLOR_RGB(255, 255, 244), COLOR_RGB(150, 40, 40)},
};

// --- Time of day ---------------------------------------------------------------------------

// One time of day: the road's light and dark segments (the dark dash is the
// tarmac's colour, so the dashes come and go with the stripes), the edge
// line, the sky's fifteen colours, and a tint the sprites are mixed toward.
typedef struct {
    Color light[MATERIALS], dark[MATERIALS];
    Color edge;
    Color sky[SKY_COLORS];
    Color tint;
    u8 tint_amount; // of 256
} TimeOfDay;

static const TimeOfDay times[TIMES_OF_DAY] =
    {
        // Late afternoon: golden grass, red and white rumble strips, warm grey
        // tarmac; the sky from blue-violet through rose to a pale gold.
        [TIME_AFTERNOON] =
            {
                .light = {COLOR_RGB(184, 172, 80), COLOR_RGB(240, 232, 216),
                          COLOR_RGB(128, 118, 114), COLOR_RGB(240, 236, 220)},
                .dark = {COLOR_RGB(150, 140, 60), COLOR_RGB(200, 52, 40), COLOR_RGB(110, 100, 98),
                         COLOR_RGB(110, 100, 98)},
                .edge = COLOR_RGB(224, 216, 192),
                .sky = {COLOR_RGB(64, 56, 132), COLOR_RGB(92, 66, 144), COLOR_RGB(128, 78, 148),
                        COLOR_RGB(166, 90, 140), COLOR_RGB(202, 106, 116), COLOR_RGB(228, 128, 92),
                        COLOR_RGB(242, 154, 80), COLOR_RGB(250, 182, 92), COLOR_RGB(252, 208, 124),
                        COLOR_RGB(255, 246, 206), COLOR_RGB(255, 224, 148), COLOR_RGB(156, 92, 116),
                        COLOR_RGB(70, 38, 54)},
                .tint = 0,
                .tint_amount = 0,
            },
        // Sunset: the grass and the road warmer, the sky deep orange and red,
        // the sun swollen and gold; the sprites lit orange.
        [TIME_SUNSET] =
            {
                .light = {COLOR_RGB(200, 148, 72), COLOR_RGB(246, 214, 190),
                          COLOR_RGB(134, 104, 100), COLOR_RGB(246, 218, 194)},
                .dark = {COLOR_RGB(164, 116, 54), COLOR_RGB(208, 54, 36), COLOR_RGB(112, 86, 86),
                         COLOR_RGB(112, 86, 86)},
                .edge = COLOR_RGB(232, 198, 168),
                .sky = {COLOR_RGB(48, 36, 104), COLOR_RGB(84, 44, 120), COLOR_RGB(132, 54, 118),
                        COLOR_RGB(180, 64, 100), COLOR_RGB(216, 82, 74), COLOR_RGB(238, 106, 56),
                        COLOR_RGB(248, 136, 52), COLOR_RGB(252, 166, 68), COLOR_RGB(254, 194, 100),
                        COLOR_RGB(255, 236, 166), COLOR_RGB(255, 196, 104), COLOR_RGB(140, 66, 94),
                        COLOR_RGB(60, 26, 42)},
                .tint = COLOR_RGB(255, 132, 64),
                .tint_amount = 56,
            },
        // Dusk: the light nearly gone, the land grey-blue, the sky indigo over a
        // last red glow, the sun sunk into it; the sprites in blue shadow.
        [TIME_DUSK] =
            {
                .light = {COLOR_RGB(100, 98, 76), COLOR_RGB(176, 166, 176), COLOR_RGB(80, 76, 92),
                          COLOR_RGB(182, 178, 188)},
                .dark = {COLOR_RGB(78, 78, 60), COLOR_RGB(136, 42, 46), COLOR_RGB(66, 62, 78),
                         COLOR_RGB(66, 62, 78)},
                .edge = COLOR_RGB(156, 152, 164),
                .sky = {COLOR_RGB(14, 14, 44), COLOR_RGB(24, 20, 60), COLOR_RGB(38, 28, 78),
                        COLOR_RGB(60, 38, 94), COLOR_RGB(90, 50, 102), COLOR_RGB(126, 62, 100),
                        COLOR_RGB(164, 80, 90), COLOR_RGB(196, 102, 82), COLOR_RGB(220, 130, 88),
                        COLOR_RGB(236, 150, 100), COLOR_RGB(226, 128, 92), COLOR_RGB(70, 44, 78),
                        COLOR_RGB(28, 16, 32)},
                .tint = COLOR_RGB(36, 36, 84),
                .tint_amount = 104,
            },
};

TimeColors time_colors[TIME_STAGES] SERVAL_EWRAM_BSS;

// A time of day's own colours.
static void build_time(TimeColors* c, const TimeOfDay* t) {
    for (int m = 0; m < MATERIALS; m++) {
        c->road[SHADE_LIGHT][m] = t->light[m];
        c->road[SHADE_DARK][m] = t->dark[m];
        c->road[SHADE_HAZE][m] = color_mix(t->light[m], t->dark[m], 128);
    }
    c->edge = t->edge;
    for (int i = 0; i < SKY_COLORS; i++)
        c->sky[i] = t->sky[i];
    for (int p = 0; p < TINTED_PALETTES; p++)
        for (int i = 0; i < 16; i++)
            c->sprites[p][i] = color_mix(obj_palettes[p][i], t->tint, t->tint_amount);
}

static void mix_colors(Color* out, const Color* a, const Color* b, int n, u32 amount) {
    for (int i = 0; i < n; i++)
        out[i] = color_mix(a[i], b[i], amount);
}

// A stage between two times of day: every colour mixed.
static void mix_time(TimeColors* c, const TimeColors* a, const TimeColors* b, u32 amount) {
    for (int s = 0; s < 3; s++)
        mix_colors(c->road[s], a->road[s], b->road[s], MATERIALS, amount);
    c->edge = color_mix(a->edge, b->edge, amount);
    mix_colors(c->sky, a->sky, b->sky, SKY_COLORS, amount);
    for (int p = 0; p < TINTED_PALETTES; p++)
        mix_colors(c->sprites[p], a->sprites[p], b->sprites[p], 16, amount);
}

static void build_times(void) {
    for (int t = 0; t < TIMES_OF_DAY; t++)
        build_time(&time_colors[TIME_STAGE(t)], &times[t]);
    for (int t = 0; t + 1 < TIMES_OF_DAY; t++)
        for (int s = 1; s < TIME_STEPS; s++)
            mix_time(&time_colors[TIME_STAGE(t) + s], &time_colors[TIME_STAGE(t)],
                     &time_colors[TIME_STAGE(t + 1)], (u32)(s * 256 / TIME_STEPS));
}

// --- The tileset -------------------------------------------------------------------------

#define BG_BANKS (SKY_BANK + 1)
static u16 bg_palettes[BG_BANKS * 16] SERVAL_EWRAM_BSS;

Tileset road_tileset = {
    .tiles = bg_tiles, // .tile_count: set by art_build, once the tiles are made
    .palettes = bg_palettes,
    .palette_count = BG_BANKS,
};

// The banks as loaded: the road's rows light with the edge line, and the
// sky's, in the afternoon (view.c sets the colours it wants on its first
// frame).
static void build_palettes(void) {
    const TimeColors* c = &time_colors[TIME_STAGE(TIME_AFTERNOON)];
    for (int bank = 0; bank < ROAD_ROWS; bank++) {
        u16* p = bg_palettes + bank * 16;
        for (int m = 0; m < MATERIALS; m++)
            for (int g = 0; g < ROAD_GROUPS; g++)
                p[1 + m * ROAD_GROUPS + g] = c->road[SHADE_LIGHT][m];
        p[ROAD_EDGE_COLOR] = c->edge;
    }
    for (int i = 0; i < SKY_COLORS; i++)
        bg_palettes[SKY_BANK * 16 + 1 + i] = c->sky[i];
}

// --- Sprite pictures -----------------------------------------------------------------------

// The jeep's left half; the right half is its mirror image.
static const char* const jeep_half[JEEP_H] = {
    "................................", "................................",
    "................................", "................................",
    ".....KKKKKKKKKKKKKKKKKKKKKKKKKKK", ".....KGKGGGGGGGGGGGGGGGGGGGGGGGG",
    ".....KGKKKKKKKKKKKKKKKKKKKKKKKKK", ".....KGK........................",
    ".....KGK........................", ".....KGK........................",
    ".....KGK....KKKKKKKKKK..........", ".....KGK...KSSSSSSSSSSK.........",
    ".....KGK..KSssssssssssSK.....KKK", "..KKKKKKKKKKKKKKKKKKKKKKKKKKKTTT",
    ".KLLLLLLLLLLLLLLLLLLLLLLLLLtTTTT", ".KDDKKKKKKBBBBBBBBBBBBBBBBKTTTTH",
    ".KDDKrrrrKBBBBBBBBBBBBBBBBKTTTHH", ".KDDKRRRRKBBBBBBBBBBBBBBBKTTTHHt",
    ".KDDKRRRRKBBBBBBBBBBBBBBBKtTTHHt", ".KDDKRRRRKBBBBBBBBBBBBBBBKTTTHHt",
    ".KDDKKKKKKBBBBBBBBBBBBBBBBKTTTHH", ".KDDDDDDDDDDDDDDDDDDDDDDDDKTTTTH",
    ".KDDDDDDDDDDDDDDDDDDDDDDDDDtTTTT", ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKTTT",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK", "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK", "..KTttTttTttTKtttttttttttKPPPPPP",
    "..KTTTTTTTTTTKtttttttttttKPKPKPK", "..KTttTttTttTKKKKKKKKKKKKKPPPPPP",
    "..KTTTTTTTTTTKKKKKKKKKKKKKKKKKKK", "...KKKKKKKKKK...................",
};

// The serval's head from behind, over the driver's (left) seat: its ears'
// black backs with their white spots, and the spotted back of its head.
#define SERVAL_ROWS 10
#define SERVAL_X 9
static const char* const serval_head[SERVAL_ROWS] = {
    "..K..........K..", ".KKK........KKK.", ".KKKK......KKKK.", ".KKWKK....KKWKK.",
    ".KKWWKKKKKKWWKK.", ".KKKKKOOOOKKKKK.", ".KKOOOsOOsOOOKK.", "KOOsOOOOOOOOsOOK",
    "KOOOOsOOOOsOOOOK", "KOOOOOOOOOOOOOOK",
};

// The jeep from the front, for a spin: headlamps, the grille, the bumper;
// its left half. The serval's face looks out over the wheel.
static const char* const front_half[JEEP_H] = {
    "................................", "................................",
    "................................", "................................",
    ".....KKKKKKKKKKKKKKKKKKKKKKKKKKK", ".....KGHGGGGGGGGGGGGGGGGGGGGGGGG",
    ".....KGKKKKKKKKKKKKKKKKKKKKKKKKK", ".....KGK........................",
    ".....KGK........................", ".....KGK........................",
    ".....KGK........................", ".....KGK........................",
    ".....KGK........................", "..KKKKKKKKKKKKKKKKKKKKKKKKKKKKKK",
    ".KLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL", ".KBBKKKKKKBBBBBBBBBBBBBBBBKGGKGG",
    ".KBKWWWWWKBBBKGGKGGKGGKGGKKGGKGG", ".KBKWHHHWKBBBKGGKGGKGGKGGKKGGKGG",
    ".KBKWHHHWKBBBKGGKGGKGGKGGKKGGKGG", ".KBKWWWWWKBBBKGGKGGKGGKGGKKGGKGG",
    ".KBBKKKKKKBBBKKKKKKKKKKKKKKKKKKK", ".KDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
    ".KDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD", ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK", "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK", "..KTttTttTttTKtttttttttttttttttt",
    "..KTTTTTTTTTTKtttttttttttttttttt", "..KTttTttTttTKKKKKKKKKKKKKKKKKKK",
    "..KTTTTTTTTTTKKKKKKKKKKKKKKKKKKK", "...KKKKKKKKKK...................",
};

#define FACE_ROWS 12
#define FACE_X 39
static const char* const serval_face[FACE_ROWS] = {
    "..K..........K..", ".KOK........KOK.", ".KOOK......KOOK.", ".KOWOK....KOWOK.",
    ".KOWOOKKKKOOWOK.", ".KOOOOOOOOOOOOK.", ".KOKKOOOOOOKKOK.", "KOOKHOOOOOOHKOOK",
    "KOOOOOOKKOOOOOOK", "KOsOOOOWWOOOOsOK", ".KOOOOOOOOOOOOK.", "..KKOOOOOOOOKK..",
};

// The jeep side on, nose to the left, the serval at the wheel: a spin's
// quarter turns (flipped for the other side).
static const char* const side_art[JEEP_H] = {
    "................................................................",
    "................................................................",
    ".........................KKKK...................................",
    "........................KGGGGK..................................",
    "........................KGGGGK...K..............................",
    "........................KGGKK...KOK......KKKK...................",
    ".......................KGGK.....KWOK....KSSSSK..................",
    ".......................KGGK.....KOOOKK..KSSSSK..................",
    "......................KGGGK....KOOsOOOK.KSSSSKKKKKKKK...........",
    "......................KGGK.....KOOOOKOKKSSSSSSSSSSSSSK..........",
    "......................KGGK..K...KOOOOOKKSSSSSSSSSSSSSK..........",
    ".....................KGGK....K...KOOsK.KSSSSSSSSSSSSSK..........",
    ".....................KGGKKKKKK.KKKKKKKKKSSSSSSSSSSSSSKKKKKKKK...",
    "...KKKKKKKKKKKKKKKKKKKGGLLLLLLKLLLLLLLLLLLLLLLLLLLLLLLLLLLLLK..",
    "..KLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLK..",
    "..KBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBKK.",
    ".KKBBBBBHHHBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBRRRBBBBTTK",
    "KGGBBBBBHHHBBBBBBBBBBBBBBBBBBBDDBBBBBBBBBBBBBBBBBBBBBBRRRBBBBTTK",
    "KGGBBBBBBBBBsssBBBBBBBBBBBBBBBDDBBBBBBBBBBBBBBBBsssBBBRRRBBBBTTK",
    "KGGBBBBBBBsssTsssBBBBBBBBBBBBBDDBBBBBBBBBBBBBBsssTsssBBBBBBBBTTK",
    "KGGBBBBBssTTTTTTTssBBBBBBBBBBBDDBBBBBBBBBBBBssTTTTTTTssBBBBBBTTK",
    "KGGBBBBBsTTTTTTTTTsBBBBBBBBBBBDDBBBBBBBBBBBBsTTTTTTTTTsBBBBBBTTK",
    ".KKDDDDsTTTTTGTTTTTsDDDDDDDDDDDDDDDDDDDDDDDsTTTTTGTTTTTsDDDDDTTK",
    "..KDDDDsTTTGGGGGTTTsDDDDDDDDDDDDDDDDDDDDDDDsTTTGGGGGTTTsDDDDDKK.",
    "...KKKKKTTTGGHGGTTTKKKKKKKKKKKKKKKKKKKKKKKKKTTTGGHGGTTTKKKKKK...",
    "......KTTTGGHHHGGTTTK.....................KTTTGGHHHGGTTTK.......",
    ".......KTTTGGHGGTTTK.......................KTTTGGHGGTTTK........",
    ".......KTTTGGGGGTTTK.......................KTTTGGGGGTTTK........",
    ".......KTTTTTGTTTTTK.......................KTTTTTGTTTTTK........",
    "........KTTTTTTTTTK.........................KTTTTTTTTTK.........",
    ".........KTTTTTTTK...........................KTTTTTTTK..........",
    "..........KKKTKKK.............................KKKTKKK...........",
};

// An acacia: the savanna's umbrella thorn, a flat top of leaves on a
// forking trunk.
static const char* const acacia_art[64] = {
    "................................................................",
    "................................................................",
    "................................................................",
    "................................................................",
    "................................................................",
    "................................................................",
    "................................................................",
    ".................KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK............",
    "...............KKLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLKK..........",
    "..............KLLLLLLLGLLLLLGGGGGGGGgLGGGLLLLLGLLLLLLLK.........",
    "..........K..KLLLGGgGGGGLGGGgGGGGGGLGgGGGGGGGGLGGGGGLLLK........",
    ".....KKKKKLKKLLGGGGGgGLGGGGGGgGGGLGGGGgGGGGGLGGgGGGGGGLLKKKK....",
    "...KKLLLLLLLLLGGGGGGLgGGGGGGGGgLGGGGGGGgGGLGGGGGgGGGGLGLLLLLKK..",
    "..KLLLLLLLGLLgGGGGLGGGgGGGGGGLGgGGGGGGGGLGGGGGGGGgGLGGGGLLLLLLK.",
    ".KLLLLGGGGGGGGgGLGGGGGGgGGGLGGGGgGGGGGLGGgGGGGGGGLgGGGGGGGGgLLLK",
    ".KLLGGgGGGGGGGLgGGGGGGGGgLGGGGGGGgGGLGGGGGgGGGGLGGGgGGGGGGLGgGLK",
    "KgGGGGGgGGGGLGGGgGGGGGGLGgGGGGGGGGLGGGGGGGGgGLGGGGGGgGGGLGGGGgGg",
    ".KgGGGGGgGLGGGGGGgGGGLGGGGgGGGGGLGGgGGGGGGGLgGGGGGGGGgLGGGGGGGgK",
    ".KGGGGGGLgGGGGGGGGgLGGGGGGGgGGLGGGGGgGGGGLGGGgGGGGGGLGgGGGGGgggK",
    "..KGGGLGGGgGGGGGGLGgGGGgGGGGLGGGGGGGGgGLGGGGGGgGGGLGGGGgGGggggK.",
    ".KggLGGGGGGgGggggGggggggggggggggggggggggGgggggggLGGGGGGGggggKK..",
    "..KgggggGggggggggggggggKggggggggggggggggggggggKgggggGgggggKK....",
    "...KgggggggggKBBBgKKKKK.KKKKKgKKBBBKKKKKgKKKKK.KgggggggggK......",
    "....KKKKgKKKK.KBBBBK.........K.KBBBK....K.....KBBBBKgKKKK.......",
    "........K......KBBBBK..........KBBBK.........KBBBBK.K...........",
    "................KBBBK..........KBBBK........KBBBBK..............",
    ".................KBBBK.........KBBBK........KBBBK...............",
    ".................KBBBBK........KBBBK.......KBBBK................",
    "..................KBBBK........KBBBK......KBBBBK................",
    "...................KBBBK.......KBBBK.....KBBBBK.................",
    "...................KBBBBK......KBBBK.....KBBBK..................",
    "....................KBBBK.....KBBBBK....KBBBK...................",
    ".....................KBBBK....KBBBK....KBBBBK...................",
    "......................KBBBK...KBBBK....KBBBK....................",
    "......................KBBBBK..KBBBK...KBBBK.....................",
    ".......................KBBBK..KBBBK..KBBBBK.....................",
    "........................KBBBK.KBBBK..KBBBK......................",
    "........................KBBBBKKBBBK.KBBBK.......................",
    ".........................KBBBKKBBBKKBBBBK.......................",
    "..........................KBBBKBBBKBBBBK........................",
    "..........................KBBBBBBBKBBBK.........................",
    "...........................KBBBBBBBBBK..........................",
    "............................KBBBBBBBBK..........................",
    ".............................KBKBBBBK...........................",
    ".............................KBKBBBK............................",
    ".............................KBKBbK.............................",
    ".............................KBKBbK.............................",
    ".............................KBKBbK.............................",
    ".............................KBKBbK.............................",
    ".............................KBKBbK.............................",
    ".............................KBKBbK.............................",
    ".............................KBKBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    ".............................KBbBbK.............................",
    "...........................KKKBbBbKKK...........................",
    "..........................KBBBBBBBBBBK..........................",
    "..........................KBBBBBBBBBBK..........................",
    ".........................KbbbbbbbbbbbbK.........................",
};

// A termite mound: a chimney of red earth, lit from the left.
static const char* const mound_art[64] = {
    "................................", "................................",
    "................................", "...............K................",
    ".............KKHKK..............", "............KHEEeeK.............",
    "...........KHHEEEeeK............", "...........KHHEeEeeK............",
    "...........KHHEEEeeK............", "...........KHHEEEeeK............",
    "...........KHHeEEeeK............", "...........KHHEEEeeK............",
    "...........KHHEEEeeK............", "...........KHHEEEeeK............",
    "...........KHHEEEeeK............", "..........KHHEEEEeeeK...........",
    "..........KHHEEEEeeeK...........", "..........KHHEEEeeeeK...........",
    "..........KHHEEEEeeeK...........", "..........KHHEEEEeeeK...........",
    "..........KHHKeeEeeeK...........", "..........KHHEEEEeeeK...........",
    "..........KHHEEEEeeeK...........", "..........KHHEeEEeeeK...........",
    ".........KHHHEEEEEeeeK..........", ".........KHHHEEEEEeeeK..........",
    ".........KHHHeEEEEeeeK..........", ".........KHHHEEEEeeeeKKKKK......",
    ".........KHHHHEEEEEEEeeeeeK.....", ".........KHHHHEEEEEEEeeeeeK.....",
    "........KHHHHEEEeEEEEeeeeeK.....", "........KHHHHEEEEEEEeeeeeeK.....",
    "........KHHHHEEEEEEEEeeeeeK.....", ".......KHHHHHKeeEEEEEeeeeeK.....",
    ".......KHHHHHEEEEEEeEeeeeeK.....", ".......KHHHHHEEEEEEEEeeeeeeK....",
    "......KHHHHHEEeEEEEEEeeeeeeK....", "......KHHHHHEEEEEEeEEeeeeeeK....",
    "......KHHHHHEEEEEEEEEeeeeeeK....", "......KHHHHHEeEEEEEEEeeeeeeK....",
    "......KHHHHHEEEEEeEEEeeeeeeK....", ".....KHHHHHEEEEEEEEEEeeeeeeK....",
    ".....KHHHHHHeEEEEEEEEEeeeeeeK...", ".....KHHHHHHEEEEeEEEEEeeeeeeK...",
    ".....KHHHHHHEEEEEEEEeEeeeeeeK...", ".....KHHHHHeEKeEEEEEEEeeeeeeK...",
    "....KHHHHHHEEEEeEEEEEeeeeeeeK...", "....KHHHHHHEEEEEEEEeEeeeeeeeK...",
    "....KHHHHHeEEEEEEEEEEeeeeeeeK...", "....KHHHHHHEEEeEEEEEEEeeeeeeeK..",
    "....KHHHHHHEEEEEEEeEEEeeeeeeeK..", "...KHHHHHeEEEEEEEEEEEEeeeeeeeK..",
    "....KHHHHHHEEeEEEEEEEEeeeeeeeK..", "....KHHHHHHEEEEEEeEEEEeeeeeeeK..",
    "....KHHHHHHEEEEEEEEEEeeeeeeeeK..", "...KHHHHHHEEeEEEEEEEEEeeeeeeeK..",
    "...KHHHHHHHEEEEEeEEEEEEeeeeeeeK.", "...KHHHHHHHEEEEEEEEEeEEeeeeeeeK.",
    "...KHHHHHHHeEEEEEEEEEEEeeeeeeeK.", "..KHHHHHHHEEEEEeEEEEEEeeeeeeeeK.",
    "..KHHHHHHHEEEEEEEEEeEEeeeeeeeeK.", "..KHHHHHHHeEEEEEEEEEEEeeeeeeeeK.",
    "..KHHHHHHHEEEEeEEEEEEEeeeeeeeeK.", ".KeeeeeeeeeeeeeeeeeeeeeeeeeeeeeK",
};

// A boulder, grass at its foot.
static const char* const rock_art[32] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "...............K................", ".........KKKKKKWKKKKKK..........",
    ".......KKWWWWWWWWWWWWWKK........", "......KWWWWWWWWWWRWWWWWWK.......",
    "....KKWWWWWRRRRRRRRRRRRRWKK.....", "...KWWWWWRRRRRRRRRRRrRRRRRrK....",
    "..KWWWWWRRRRRRRRRRRRrRRRRRRrK...", "..KWWWRRRrRRRRRRRRRrRRRRRRRrK...",
    ".KRRRRRRRRrRRRRRRRRrRRRRRRRrrK..", ".KRRRRRRRRRrRRRRRRRrRRRRRRRRrK..",
    ".KRRRRRRRRRrrRRRRRRrRRRRRRRRrK..", "KRRRRRRRRRRRrRRRRRrRRRRRRRRRrrK.",
    ".KRRRRRRRRRRRrRRRRrRRRRRRRRRRrK.", ".KRRRRRRRRRRRRrRRRRRRRRRRRRRrrK.",
    ".KRRRRRRRRRRRRRRRRRRRRRRRRRRrrrK", "..KRRRRRRRRRRRRRRRRRRRRRRRRRrrK.",
    ".KKrRRRRRRRRRRRRRRRRRRRRRRRrrrK.", "KGKGrGrrrrrrrrrrrrrrrrrrrGrGrrK.",
    "KGKGKGrrrrrrrrrrrrrrrrrrrGrGrK..", "KGGGGGGrrrrrrrrrrrrrrrrrrGGGGK..",
    ".KGKGKGKKrrrrrrrrrrrrrrrrrGKGK..", ".KGKGKGK.KKKKKKKKKKKKKKKKKGKGK..",
};

// A curve sign: two chevrons pointing left (flipped, right), the way the
// road is about to bend.
static const char* const sign_art[32] = {
    "................................", "................................",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", ".KCCCCCCCCCCCCCCCCCCCCCCCCCCCCK.",
    ".KYYYYYYYYYYYYYYYYYYYYYYYYYYYYK.", ".KYYYYYYYYYYKKKKYYYYYKKKKYYYYYK.",
    ".KYYYYYYYYYKKKKYYYYYKKKKYYYYYYK.", ".KYYYYYYYYKKKKYYYYYKKKKYYYYYYYK.",
    ".KYYYYYYYKKKKYYYYYKKKKYYYYYYYYK.", ".KYYYYYKKKKKYYYYKKKKKYYYYYYYYYK.",
    ".KYYYYYKKKKKYYYYKKKKKYYYYYYYYYK.", ".KYYYYYYYKKKKYYYYYKKKKYYYYYYYYK.",
    ".KYYYYYYYYKKKKYYYYYKKKKYYYYYYYK.", ".KYYYYYYYYYKKKKYYYYYKKKKYYYYYYK.",
    ".KYYYYYYYYYYKKKKYYYYYKKKKYYYYYK.", ".KYYYYYYYYYYYYYYYYYYYYYYYYYYYYK.",
    ".KYYYYYYYYYYYYYYYYYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYYYYYYYYYYYYYYYYYK.",
    "..KKKKKKKKKKKBbBKKKKKKKKKKKKKK..", "............KBbBK...............",
    "............KBbBK...............", "............KBbBK...............",
    "............KBbBK...............", "............KBbBK...............",
    "............KBbBK...............", "............KBbBK...............",
    "............KBbBK...............", "............KBbBK...............",
    "............KBbBK...............", "............KBbBK...............",
    "............KBbBK...............", "............KBbBK...............",
};

// A giraffe, facing left (flipped, right), its coat a net of cream between
// red-brown patches.
static const char* const giraffe_art[64] = {
    "....KbKKbK......................", "....KbKKbK......................",
    ".....KbKbK......................", ".....KbKbK......................",
    "....KKCKK.KK....................", "...KCCCCCKCCK...................",
    "..KCCKCCCCKK....................", "KKCCCCCCCCCK....................",
    "CCCCCCCCCCbK....................", "CCCCCCCCCCbK....................",
    "CEEECKCCCEKbK...................", "KKEKKKCCCEKbK...................",
    "..K...KCCCCbK...................", "......KEEECbbK..................",
    "......KEEECKbK..................", "......KEEECKbK..................",
    ".......KCCCCbbK.................", ".......KCCCCKbK.................",
    ".......KCCCCKbK.................", ".......KCCCCCbK.................",
    "........KCCCCKbK................", "........KECEEKbK................",
    "........KECEEKbK................", "........KECEEEbbK...............",
    ".........KCCCCKbK...............", ".........KEECCKbK...............",
    ".........KEECCKbbK..............", "..........KECCCKbK..............",
    "..........KCCCCKbK..............", "..........KEEECKbK..............",
    "..........KEEECKKbKK............", "...........KEECEKbKEKKKK........",
    "...........KCCCCCCCCCCCCKK..K...", "...........KCEEECCCCCEEECEKKbK..",
    "...........KCEEECCCCCEEECEEKbK..", "..........KECEEECCCCCEEECEEEbK..",
    "..........KCCCCCCCCCCCCCCCCCKbK.", ".........KCEEECEEECEEECEEECEEbK.",
    "..........KEEECEEECEEECEEECEKbK.", "..........KEEECEEECEEECEEECEKbK.",
    "...........KCCCCCCCCCCCCCCCKKbK.", "...........KCEEECEEECCCCCEEKKbK.",
    "...........KCKEECEEECCCCCKEK.KbK", "...........KCKEKCKKEKKCKCKEK.KbK",
    "...........KCKCKCK.K.KCKCKCK.KbK", "...........KEKCKEK...KCKEKCK..K.",
    "...........KEKCKCK...KCKEKCK....", "...........KEKCKCK...KCKEKCK....",
    "...........KCKCKCK...KCKCKCK....", "...........KCKEKCK...KCKCKCK....",
    "...........KCKEKCK...KCKCKCK....", "...........KCKEKCK...KCKCKCK....",
    "...........KCKCKCK...KCKCKCK....", "...........KCKCKCK...KCKCKCK....",
    "...........KCKCKCK...KCKCKCK....", "...........KCKCKCK...KCKCKCK....",
    "...........KCKCKCK...KCKCKCK....", "...........KCKCKCK...KCKCKCK....",
    "...........KCKCKCK...KCKCKCK....", "...........KCKCKCK...KCKCKCK....",
    "...........KCKCKCK...KCKCKCK....", "...........KKKKKK....KKKKKK.....",
    "...........KKKKKK....KKKKKK.....", "...........KKKKKK....KKKKKK.....",
};

// A safari truck from behind, its left half: the canvas roof, the open
// sides with their benches, the tailgate with a spare wheel, twin tyres.
static const char* const truck_half[64] = {
    "................................", "................................",
    "................................", "...................KKKKKKKKKKKKK",
    "...............KKKKOOOOOOOWOOOOO", "...........KKKKOOOWOOOOOOOWOOOOO",
    ".........KKOOOOOOOWOOOOOOOWOOOOO", ".......KKOWOOOOOOOWOOOOOOOWOOOOO",
    ".....KKOOOWOOOOOOOWOOOOOOOWOOOOO", "....KOOOOOWOOOOOOOWOOOOOOOWOOOOO",
    "...KOOOOOOWOOOOOOOWOOOOOOOWOOOOO", "...KOOOOOOWOOOOOOOWOOOOOOOWOOOOO",
    "..KOOOOOOOWOOOOOOOWOOOOOOOWOOOOO", "..KOOOOOOOWOOOOOOOWOOOOOOOWOOOOO",
    "..KOOOOOOOWOOOOOOOWOOOOOOOWOOOOO", "..KWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "..KWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "..KGGGssssssssssssssssssssssssGG",
    "..KGGGssssssssssssssssssssssssGG", "..KGGGssssssssssssssssssssssssGG",
    "..KGGGssssssssssssssssssssssssGG", "..KGGGssssssssssssssssssssssssGG",
    "..KGGGssssssssssssssssssssssssGG", "..KGGGssssssssssssssssssssssssGG",
    "..KGGGssSSSSSSSSSSSSSSSSSSSSssGG", "..KGGGssSSSSSSSSSSSSSSSSSSSSssGG",
    "..KGGGssSSSSSSSSSSSSSSSSSSSSssGG", "..KGGGssSSSSSSSSSSSSSSSSSSSSssGG",
    "..KGGGssssssssssssssssssssssssGG", "..KGGGssssssssssssssssssssssssGG",
    "..KGGGssssssssssssssssssssssssGG", ".KLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL",
    ".KLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL", ".KBBBBBBBBBBBBBBBBBBBBBBBBBBTTTT",
    ".KBBBBBBBBBBBBBBBBBBBBBBBBBTTTTT", ".KBBrrrrrBBBBBBBBBBBBBBBBBTTTTTT",
    ".KBBrrrrrBBBBBBBBBBBBBBBBBTTTTTT", ".KBBRRRRRBBBBBBBBBBBBBBBBTTTTTHH",
    ".KBBRRRRRBBBBBBBBBBBBBBBBTTTTHHH", ".KBBRRRRRBBBBBBBBBBBBBBBBTTTTHHH",
    ".KBBRRRRRBBBBBBBBBBBBBBBBTTTTHHH", ".KBBRRRRRBBBBBBBBBBBBBBBBTTTTTHH",
    ".KBBBBBBBBBBBBBBBBBBBBBBBBTTTTTT", ".KBBBBBBBBBBBBBBBBBBBBBBBBTTTTTT",
    ".KBBBBBBBBBBBBBBBBBBBBBBBBBTTTTT", ".KBBBBBBBBBBBBBBBBBBBBBBBBBBTTTT",
    ".KBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", ".KDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
    ".KDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD", ".KDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
    "KGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG", "KGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG",
    ".KKTTTTTTTTTTTTTTTTTKKKKPPKPPKPP", "..KTtTTtTTtTTtTTtTTTK..KPPPPPPPP",
    "..KTtTTtTTtTTtTTtTTTKKKKPPPPPPPP", "..KTtTTtTTtTTtTTtTTTtttttttttttt",
    "..KTtTTtTTtTTtTTtTTTtttttttttttt", "..KTtTTtTTtTTtTTtTTTtttttttttttt",
    "..KTtTTtTTtTTtTTtTTTtttttttttttt", "..KTtTTtTTtTTtTTtTTTKKKKKKKKKKKK",
    "..KTtTTtTTtTTtTTtTTTK...........", "..KTtTTtTTtTTtTTtTTTK...........",
    "..KTTTTTTTTTTTTTTTTTK...........", "...KKKKKKKKKKKKKKKKK............",
};

// The big letters (the logo, the countdown, FINISH and TIME UP), 5x7, '#'
// set. Only the ones they use.
static const char font_chars[] = "SAVNRUGO123FIHTMEP";
static const char* const font[][7] = {
    {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}, // S
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // A
    {"#...#", "#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."}, // V
    {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}, // N
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}, // R
    {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // U
    {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}, // G
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // O
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}, // 1
    {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}, // 2
    {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}, // 3
    {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}, // F
    {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}, // I
    {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // H
    {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}, // T
    {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}, // M
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}, // E
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}, // P
};
#define FONT_SCALE 4                  // each font pixel 4x4 screen pixels
#define FONT_ADVANCE (6 * FONT_SCALE) // a letter and the gap after it
#define LOGO_GRADIENT 5               // PAL_LOGO colours 2-6, top to foot
#define LOGO_GLINT 7                  // PAL_LOGO: a letter's top edges, lit
#define LOGO_SHADE 8                  // PAL_LOGO: a letter's feet, in shadow

// --- Painting -----------------------------------------------------------------------------

// The canvas the sprites are painted in, a colour index a pixel; then cut
// into tiles (pack). Big enough for the logo's two rows of three pieces.
#define CANVAS_W 192
#define CANVAS_H 64
static u8 canvas[CANVAS_W * CANVAS_H] SERVAL_EWRAM_BSS;
static u8 reduced[64 * 64] SERVAL_EWRAM_BSS; // one size class of an object

static void canvas_clear(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        canvas[i] = 0;
}

static void put(int x, int y, int c) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        canvas[y * CANVAS_W + x] = (u8)c;
}

static int key_of(const char* keys, char c) {
    for (int k = 1; keys[k]; k++)
        if (keys[k] == c)
            return k;
    return 0;
}

// An ASCII picture `h` rows tall onto the canvas at (x0, y0); '.' is left
// as it is, so pictures can be laid over one another.
static void paint(const char* const* rows, int h, const char* keys, int x0, int y0) {
    for (int y = 0; y < h; y++)
        for (int x = 0; rows[y][x]; x++)
            if (rows[y][x] != '.')
                put(x0 + x, y0 + y, key_of(keys, rows[y][x]));
}

// A symmetric picture from its left half (w / 2 wide), at the canvas's
// top-left.
static void paint_mirrored(const char* const* half, int w, int h, const char* keys) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w / 2; x++) {
            int c = key_of(keys, half[y][x]);
            put(x, y, c);
            put(w - 1 - x, y, c);
        }
    }
}

// Cuts a w x h block of pixels (pitch `pitch`) from (x0, y0) into 4bpp
// tiles, row by row (a sprite frame's layout). Returns the next free word.
static u32* pack(const u8* pixels, int pitch, int x0, int y0, int w, int h, u32* out) {
    for (int ty = 0; ty < h / 8; ty++) {
        for (int tx = 0; tx < w / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                const u8* row = pixels + (y0 + ty * 8 + y) * pitch + x0 + tx * 8;
                for (int x = 0; x < 8; x++)
                    word |= (u32)row[x] << (4 * x);
                *out++ = word;
            }
        }
    }
    return out;
}

// --- The sprites -----------------------------------------------------------------------------

// The objects' full sizes: build_objects() draws each at these, and the
// tile count below adds them up.
#define ACACIA_W 64
#define ACACIA_H 64
#define MOUND_W 32
#define MOUND_H 64
#define ROCK_W 32
#define ROCK_H 32
#define SIGN_W 32
#define SIGN_H 32
#define GIRAFFE_W 32
#define GIRAFFE_H 64
#define POST_W 16
#define POST_H 32
#define BANNER_W 64
#define BANNER_H 32
#define TRUCK_W 64
#define TRUCK_H 64

// Tiles of an object built by build_object(): its four size classes, each
// the picture reduced by 2^class in a box at least 8x8.
#define CLASS_SIDE(n, c) (((n) >> (c)) < 8 ? 8 : ((n) >> (c)))
#define CLASS_TILES(w, h, c) (CLASS_SIDE(w, c) * CLASS_SIDE(h, c) / 64)
#define OBJECT_TILES(w, h)                                                                         \
    (CLASS_TILES(w, h, 0) + CLASS_TILES(w, h, 1) + CLASS_TILES(w, h, 2) + CLASS_TILES(w, h, 3))
_Static_assert(ROAD_SIZE_CLASSES == 4, "OBJECT_TILES counts four size classes");

// Every frame's tiles: the jeep 160, the logo 160, the countdown 80 (three
// digits of 32x32 and GO in 64x32), FINISH and TIME UP 192, the dust 8, the
// objects 398: 998.
// art_build() counts what it packs into obj_tiles_used, which stays at this.
#define JEEP_TILES (JEEP_FRAMES * JEEP_W * JEEP_H / 64)
#define DUST_TILES (DUST_FRAMES * DUST_SIDE * DUST_SIDE / 64)
#define LETTER_TILES                                                                               \
    (LOGO_FRAMES * 64 * 32 / 64 + 3 * 32 * 32 / 64 + 64 * 32 / 64 +                                \
     WORDS * WORD_PIECES * 64 * 32 / 64)
#define OBJECTS_TILES                                                                              \
    (OBJECT_TILES(ACACIA_W, ACACIA_H) + OBJECT_TILES(MOUND_W, MOUND_H) +                           \
     OBJECT_TILES(ROCK_W, ROCK_H) + OBJECT_TILES(SIGN_W, SIGN_H) +                                 \
     OBJECT_TILES(GIRAFFE_W, GIRAFFE_H) + OBJECT_TILES(POST_W, POST_H) +                           \
     OBJECT_TILES(BANNER_W, BANNER_H) + OBJECT_TILES(JEEP_W, JEEP_H) +                             \
     OBJECT_TILES(TRUCK_W, TRUCK_H))
#define OBJ_TILE_MAX 1024
_Static_assert(JEEP_TILES + LETTER_TILES + DUST_TILES + OBJECTS_TILES <= OBJ_TILE_MAX,
               "the sprites' art outgrows obj_tiles: raise OBJ_TILE_MAX (1,024 at most)");
_Static_assert(OBJ_TILE_MAX <= 1024, "sprite tiles: 1,024 at most (the hardware's 32 KB)");
static u32 obj_tiles[OBJ_TILE_MAX * 8] SERVAL_EWRAM_BSS;
u16 obj_tiles_used;

static SpriteAsset sprites[SPRITE_COUNT] SERVAL_EWRAM_BSS;
const SpriteAsset* sprite_table[SPRITE_COUNT];
ObjectBox object_box[OBJECTS][ROAD_SIZE_CLASSES];

const SpriteGroup sprite_group = {
    .palettes = &obj_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = OBJ_PALETTE_COUNT,
};

// The hardware's sprite size for a w x h box.
static u8 sprite_size(int w, int h) {
    static const u8 sizes[][3] = {
        {8, 8, SPRITE_8x8},     {16, 16, SPRITE_16x16}, {32, 32, SPRITE_32x32},
        {64, 64, SPRITE_64x64}, {16, 8, SPRITE_16x8},   {32, 8, SPRITE_32x8},
        {32, 16, SPRITE_32x16}, {64, 32, SPRITE_64x32}, {8, 16, SPRITE_8x16},
        {8, 32, SPRITE_8x32},   {16, 32, SPRITE_16x32}, {32, 64, SPRITE_32x64},
    };
    for (u32 i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        if (sizes[i][0] == w && sizes[i][1] == h)
            return sizes[i][2];
    return 0; // not a hardware size: sprite_group_load() refuses the group (warns)
}

// A sprite of `frames` frames whose tiles start at `tiles`.
static void sprite(int id, int w, int h, int frames, int palette, const u32* tiles) {
    sprites[id] = (SpriteAsset){
        .tiles = tiles,
        .size = sprite_size(w, h),
        .frame_count = (u8)frames,
        .palette_slot = (u8)palette,
    };
}

// The jeep from behind, straight, into `straight`.
static u8 straight[JEEP_W * JEEP_H] SERVAL_EWRAM_BSS;

static void draw_straight_jeep(void) {
    canvas_clear();
    paint_mirrored(jeep_half, JEEP_W, JEEP_H, jeep_keys);
    paint(serval_head, SERVAL_ROWS, jeep_keys, SERVAL_X, 0);
    for (int y = 0; y < JEEP_H; y++)
        for (int x = 0; x < JEEP_W; x++)
            straight[y * JEEP_W + x] = canvas[y * CANVAS_W + x];
}

// A steering frame onto the canvas (dir -1 left, 1 right): the body leans
// into the turn over its wheels, the cab and the serval two pixels, the tub
// one, so the jeep seems to swing its nose in.
static void lean_jeep(int dir) {
    canvas_clear();
    for (int y = 0; y < JEEP_H; y++) {
        int shift = dir * (y < 13 ? 2 : y < 24 ? 1 : 0);
        for (int x = 0; x < JEEP_W; x++) {
            u8 c = straight[y * JEEP_W + x];
            if (c)
                put(x + shift, y, c);
        }
    }
}

static u32* build_jeep(u32* out) {
    const u32* first = out;
    draw_straight_jeep();
    static const s8 lean[3] = {[JEEP_STRAIGHT] = 0, [JEEP_LEFT] = -1, [JEEP_RIGHT] = 1};
    for (int f = JEEP_STRAIGHT; f <= JEEP_RIGHT; f++) {
        lean_jeep(lean[f]);
        out = pack(canvas, CANVAS_W, 0, 0, JEEP_W, JEEP_H, out);
    }
    canvas_clear();
    paint_mirrored(front_half, JEEP_W, JEEP_H, jeep_keys);
    paint(serval_face, FACE_ROWS, jeep_keys, FACE_X, 1);
    out = pack(canvas, CANVAS_W, 0, 0, JEEP_W, JEEP_H, out);
    canvas_clear();
    paint(side_art, JEEP_H, jeep_keys, 0, 0);
    out = pack(canvas, CANVAS_W, 0, 0, JEEP_W, JEEP_H, out);
    sprite(SPR_JEEP, JEEP_W, JEEP_H, JEEP_FRAMES, PAL_JEEP, first);
    sprites[SPR_JEEP].origin_x = JEEP_W / 2;
    sprites[SPR_JEEP].origin_y = JEEP_H - 1;
    return out;
}

// Half of a checkpoint's post: a pole banded red and white, two bands of
// each, so two of them one on the other (objects.c) make the whole post,
// banded all the way. (A post 64 tall in one sprite would need a 32x64 box:
// the hardware has no 16x64 sprite.)
static void draw_post(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 5; x <= 10; x++)
            put(x, y, x == 5 || x == 10 ? 1 : (y / 8) & 1 ? 15 : 14); // K; D or W bands
}

// A third of a checkpoint's banner: a chequered cloth with red hems.
static void draw_banner(void) {
    for (int y = 5; y <= 30; y++) {
        for (int x = 0; x < 64; x++) {
            int c;
            if (y == 5 || y == 30)
                c = 1; // K
            else if (y <= 8 || y >= 27)
                c = 15; // D
            else
                c = ((x / 6) + ((y - 9) / 6)) & 1 ? 1 : 14; // K or W
            put(x, y, c);
        }
    }
}

// The commonest colour of the n x n block of the canvas at (x, y), or 0
// (transparent) if under a quarter of it is coloured; ties go to the later
// colour.
static u8 block_colour(int x, int y, int n) {
    u8 count[16] = {0};
    int coloured = 0;
    for (int dy = 0; dy < n; dy++) {
        for (int dx = 0; dx < n; dx++) {
            u8 c = canvas[(y + dy) * CANVAS_W + x + dx];
            if (c) {
                count[c]++;
                coloured++;
            }
        }
    }
    if (coloured * 4 < n * n)
        return 0;
    u8 best = 0;
    for (int c = 1; c < 16; c++)
        if (count[c] >= count[best] && count[c] > 0)
            best = (u8)c;
    return best;
}

// An object from the w x h picture on the canvas: one sprite per size class,
// the picture reduced by 2^class standing at the bottom of a box at least
// 8x8, centred.
static u32* build_object(int object, int w, int h, int palette, u32* out) {
    for (int c = 0; c < ROAD_SIZE_CLASSES; c++) {
        int n = 1 << c;
        int aw = w >> c, ah = h >> c;
        int bw = aw < 8 ? 8 : aw, bh = ah < 8 ? 8 : ah;
        int ox = (bw - aw) / 2, oy = bh - ah;
        for (int i = 0; i < bw * bh; i++)
            reduced[i] = 0;
        for (int y = 0; y < ah; y++)
            for (int x = 0; x < aw; x++)
                reduced[(oy + y) * bw + ox + x] = block_colour(x * n, y * n, n);
        object_box[object][c] = (ObjectBox){(u8)bw, (u8)bh};
        sprite(OBJ_SPRITE(object, c), bw, bh, 1, palette, out);
        out = pack(reduced, bw, 0, 0, bw, bh, out);
    }
    return out;
}

static u32* build_objects(u32* out) {
    canvas_clear();
    paint(acacia_art, 64, scenery_keys, 0, 0);
    out = build_object(OBJ_ACACIA, ACACIA_W, ACACIA_H, PAL_SCENERY, out);
    canvas_clear();
    paint(mound_art, 64, scenery_keys, 0, 0);
    out = build_object(OBJ_MOUND, MOUND_W, MOUND_H, PAL_SCENERY, out);
    canvas_clear();
    paint(rock_art, 32, scenery_keys, 0, 0);
    out = build_object(OBJ_ROCK, ROCK_W, ROCK_H, PAL_SCENERY, out);
    canvas_clear();
    paint(sign_art, 32, scenery_keys, 0, 0);
    out = build_object(OBJ_SIGN, SIGN_W, SIGN_H, PAL_SCENERY, out);
    canvas_clear();
    paint(giraffe_art, 64, scenery_keys, 0, 0);
    out = build_object(OBJ_GIRAFFE, GIRAFFE_W, GIRAFFE_H, PAL_SCENERY, out);
    canvas_clear();
    draw_post();
    out = build_object(OBJ_POST, POST_W, POST_H, PAL_SCENERY, out);
    canvas_clear();
    draw_banner();
    out = build_object(OBJ_BANNER, BANNER_W, BANNER_H, PAL_SCENERY, out);
    canvas_clear();
    paint_mirrored(jeep_half, JEEP_W, JEEP_H, jeep_keys); // no serval at this wheel
    out = build_object(OBJ_JEEP, JEEP_W, JEEP_H, PAL_TRAFFIC, out);
    canvas_clear();
    paint_mirrored(truck_half, TRUCK_W, TRUCK_H, jeep_keys);
    return build_object(OBJ_TRUCK, TRUCK_W, TRUCK_H, PAL_TRAFFIC, out);
}

// The dust: three overlapping balls, lit from the top left. Thick, solid
// pale earth; thin, every other pixel, a little bigger.
static void draw_dust(int frame) {
    static const s8 balls[3][2] = {{5, 10}, {10, 9}, {8, 6}};
    int r = frame == DUST_THICK ? 4 : 5;
    for (int y = 0; y < DUST_SIDE; y++) {
        for (int x = 0; x < DUST_SIDE; x++) {
            int in = 0;
            for (int b = 0; b < 3; b++) {
                int dx = x - balls[b][0], dy = y - balls[b][1];
                in |= dx * dx + dy * dy <= r * r;
            }
            if (!in)
                continue;
            if (frame == DUST_THICK) // C cream, H and E: pale earth, light to dark
                put(x, y, x + y < 11 ? 13 : x + y < 18 ? 9 : 8);
            else if ((x + y) & 1)
                put(x, y, x + y < 16 ? 13 : 9);
        }
    }
}

static u32* build_dust(u32* out) {
    const u32* first = out;
    for (int f = DUST_THICK; f < DUST_FRAMES; f++) {
        canvas_clear();
        draw_dust(f);
        out = pack(canvas, CANVAS_W, 0, 0, DUST_SIDE, DUST_SIDE, out);
    }
    sprite(SPR_DUST, DUST_SIDE, DUST_SIDE, DUST_FRAMES, PAL_SCENERY, first);
    return out;
}

// Text in the big letters onto the canvas, its top-left at (x0, y0): each
// letter yellow at the top to red at its foot.
// A space is half a letter wide.
static void letters(const char* s, int x0, int y0) {
    for (; *s; s++, x0 += FONT_ADVANCE) {
        if (*s == ' ') {
            x0 -= FONT_ADVANCE / 2;
            continue;
        }
        int g = 0;
        while (font_chars[g] != *s)
            g++;
        for (int y = 0; y < 7 * FONT_SCALE; y++)
            for (int x = 0; x < 5 * FONT_SCALE; x++)
                if (font[g][y / FONT_SCALE][x / FONT_SCALE] == '#')
                    put(x0 + x, y0 + y, 2 + y * LOGO_GRADIENT / (7 * FONT_SCALE));
    }
}

// Around every letter on the canvas: a glint along its top edges and a
// shade along its feet, a dark outline, and a shadow two pixels down and
// right.
static void letters_outline(void) {
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            u8* p = &canvas[y * CANVAS_W + x];
            if (!*p)
                continue;
            if (y == 0 || !p[-CANVAS_W])
                *p = LOGO_GLINT;
            else if (y == CANVAS_H - 1 || !p[CANVAS_W])
                *p = LOGO_SHADE;
        }
    }
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            if (canvas[y * CANVAS_W + x])
                continue;
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; dy++)
                for (int dx = -1; dx <= 1 && !edge; dx++) {
                    int u = x + dx, v = y + dy;
                    edge = u >= 0 && v >= 0 && u < CANVAS_W && v < CANVAS_H &&
                           canvas[v * CANVAS_W + u] >= 2;
                }
            if (edge || (x >= 2 && y >= 2 && canvas[(y - 2) * CANVAS_W + x - 2] >= 2))
                canvas[y * CANVAS_W + x] = 1;
        }
    }
}

// The width of a word in the big letters, outline and shadow included.
#define WORD_W(n) ((n) * FONT_ADVANCE - FONT_SCALE + 3)

static u32* build_letters(u32* out) {
    // The logo: SAVANNA over three pieces, RUN over two.
    canvas_clear();
    letters("SAVANNA", (192 - WORD_W(7)) / 2 + 1, 1);
    letters("RUN", (128 - WORD_W(3)) / 2 + 1, 33);
    letters_outline();
    const u32* logo = out;
    for (int f = LOGO_SA; f <= LOGO_NA; f++)
        out = pack(canvas, CANVAS_W, f * 64, 0, 64, 32, out);
    for (int f = 0; f < 2; f++)
        out = pack(canvas, CANVAS_W, f * 64, 32, 64, 32, out);
    sprite(SPR_LOGO, 64, 32, LOGO_FRAMES, PAL_LOGO, logo);
    // The countdown: 1, 2, 3 in 32x32, side by side; GO in 64x32 below.
    canvas_clear();
    letters("1", 6, 1);
    letters("2", 32 + 6, 1);
    letters("3", 64 + 6, 1);
    letters("GO", (64 - WORD_W(2)) / 2 + 1, 33);
    letters_outline();
    const u32* digits = out;
    for (int f = 0; f < 3; f++)
        out = pack(canvas, CANVAS_W, f * 32, 0, 32, 32, out);
    sprite(SPR_DIGIT, 32, 32, 3, PAL_LOGO, digits);
    sprite(SPR_GO, 64, 32, 1, PAL_LOGO, out);
    out = pack(canvas, CANVAS_W, 0, 32, 64, 32, out);
    // The race's last words: FINISH over three pieces, TIME UP (its space
    // half a letter wide) over three more.
    canvas_clear();
    letters("FINISH", (192 - WORD_W(6)) / 2 + 1, 1);
    letters("TIME UP", (192 - WORD_W(6) - FONT_ADVANCE / 2) / 2 + 1, 33);
    letters_outline();
    const u32* words = out;
    for (int w = 0; w < WORDS; w++)
        for (int p = 0; p < WORD_PIECES; p++)
            out = pack(canvas, CANVAS_W, p * 64, w * 32, 64, 32, out);
    sprite(SPR_WORDS, 64, 32, WORDS * WORD_PIECES, PAL_LOGO, words);
    return out;
}

static void build_sprites(void) {
    u32* out = obj_tiles;
    out = build_jeep(out);
    out = build_letters(out);
    out = build_dust(out);
    out = build_objects(out);
    obj_tiles_used = (u16)((out - obj_tiles) / 8);
    for (int i = 0; i < SPRITE_COUNT; i++)
        sprite_table[i] = &sprites[i];
}

void art_build(void) {
    build_times();
    u32 blank[8] = {0};
    bg_tiles_used = 0;
    add_tile(blank); // tile 0
    build_road();
    build_sky();
    road_tileset.tile_count = bg_tiles_used;
    build_palettes();
    build_sprites();
}
