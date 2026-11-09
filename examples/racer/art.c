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
// Tiles are kept once: a tile already made (the plain grass, the tarmac,
// the sky's bands) is used again rather than stored twice.
//
// The jeep is an ASCII picture, one character per pixel ('.' transparent):
// its left half, mirrored, and the serval's head over the driver's seat.

#include "art.h"

// --- The tiles --------------------------------------------------------------------

// Room for the pictures' tiles (they take 144, the blank one included).
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
#define ROAD_MAP_H 10 // 160 pixels: the sky's 64 lines blank, then the road's 96
#define ROAD_TILE_ROW0 (HORIZON / 8)

// Picture pixel (x, y), x in the left half (0-127): the material and group
// of the road or grass there, as a colour index of the line's bank.
static int road_pixel(int x, int y) {
    if (y < HORIZON)
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

// The screen entry of tile (tx, ty) of the picture: the left half's tile,
// or for the right half the mirror column's, flipped; bank = road row.
static u16 road_se(int tx, int ty) {
    if (ty < ROAD_TILE_ROW0)
        return MAP_SE(0, 0, 0);
    int bank = ty - ROAD_TILE_ROW0;
    if (tx < ROAD_MAP_W)
        return MAP_SE(make_tile(road_pixel, tx * 8, ty * 8), bank, 0);
    return MAP_SE(make_tile(road_pixel, (2 * ROAD_MAP_W - 1 - tx) * 8, ty * 8), bank,
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

// --- The road's colours ---------------------------------------------------------------

// Late afternoon: golden savanna grass, red and white rumble strips, warm grey
// tarmac with white dashes (the dark segments' dash is the tarmac's colour,
// so the dashes come and go with the stripes).
static const Color afternoon_light[MATERIALS] = {
    [MAT_GRASS] = COLOR_RGB(184, 172, 80),
    [MAT_RUMBLE] = COLOR_RGB(240, 232, 216),
    [MAT_SURFACE] = COLOR_RGB(128, 118, 114),
    [MAT_DASH] = COLOR_RGB(240, 236, 220),
};
static const Color afternoon_dark[MATERIALS] = {
    [MAT_GRASS] = COLOR_RGB(150, 140, 60),
    [MAT_RUMBLE] = COLOR_RGB(200, 52, 40),
    [MAT_SURFACE] = COLOR_RGB(110, 100, 98),
    [MAT_DASH] = COLOR_RGB(110, 100, 98),
};
#define EDGE_LINE COLOR_RGB(224, 216, 192)

RoadColors road_colors[TIMES_OF_DAY] SERVAL_EWRAM_BSS;

static void build_road_colors(RoadColors* c, const Color* light, const Color* dark) {
    for (int m = 0; m < MATERIALS; m++) {
        c->shade[SHADE_LIGHT][m] = light[m];
        c->shade[SHADE_DARK][m] = dark[m];
        c->shade[SHADE_HAZE][m] = color_mix(light[m], dark[m], 128);
    }
}

// --- The sky ---------------------------------------------------------------------------

#define SKY_MAP_W 32 // metatiles: 512 pixels
#define SKY_MAP_H 4  // 64 pixels: the lines above the horizon
#define SKY_W (SKY_MAP_W * 16)
#define SKY_H (SKY_MAP_H * 16)

enum {
    SKY_BANDS = 9,           // colours 1-9, top to bottom
    SKY_SUN = 1 + SKY_BANDS, // the sun's disc
    SKY_SUN_RIM,             // its edge
    SKY_HILLS,               // the far hills, hazy
    SKY_TREES,               // the acacias and the bush in front, dark
};
// The bands run from blue-violet at the top through mauve and rose to orange
// and a pale gold at the horizon.
static const Color sky_colors[16] = {
    0,
    COLOR_RGB(64, 56, 132),
    COLOR_RGB(92, 66, 144),
    COLOR_RGB(128, 78, 148),
    COLOR_RGB(166, 90, 140),
    COLOR_RGB(202, 106, 116),
    COLOR_RGB(228, 128, 92),
    COLOR_RGB(242, 154, 80),
    COLOR_RGB(250, 182, 92),
    COLOR_RGB(252, 208, 124),
    [SKY_SUN] = COLOR_RGB(255, 246, 206),
    [SKY_SUN_RIM] = COLOR_RGB(255, 224, 148),
    [SKY_HILLS] = COLOR_RGB(156, 92, 116),
    [SKY_TREES] = COLOR_RGB(70, 38, 54),
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

// --- The tileset -------------------------------------------------------------------------

#define BG_BANKS (SKY_BANK + 1)
static u16 bg_palettes[BG_BANKS * 16] SERVAL_EWRAM_BSS;

Tileset road_tileset = {
    .tiles = bg_tiles, // .tile_count: set by art_build, once the tiles are made
    .palettes = bg_palettes,
    .palette_count = BG_BANKS,
};

// The banks as loaded: the road's rows light (view.c restripes them on the
// first frame) with the edge line, and the sky's.
static void build_palettes(void) {
    for (int bank = 0; bank < ROAD_ROWS; bank++) {
        u16* p = bg_palettes + bank * 16;
        for (int m = 0; m < MATERIALS; m++)
            for (int g = 0; g < ROAD_GROUPS; g++)
                p[1 + m * ROAD_GROUPS + g] = road_colors[TIME_AFTERNOON].shade[SHADE_LIGHT][m];
        p[ROAD_EDGE_COLOR] = EDGE_LINE;
    }
    for (int i = 1; i < 16; i++)
        bg_palettes[SKY_BANK * 16 + i] = sky_colors[i];
}

// --- Sprites ---------------------------------------------------------------------------

// Jeep keys: K outline, T tyre, t tread and underside, G metal, H hub, B body
// (khaki), D body in shade, L body's lit rim, R tail light, r its highlight,
// S seat, s seat's seam and the serval's spots, P number plate, O the
// serval's fur, W the white spots on the backs of its ears.
static const char jeep_keys[] = ".KTtGHBDLRrSsPOW";

#define JEEP_COLORS                                                                                \
    0, COLOR_RGB(24, 16, 16), COLOR_RGB(40, 36, 36), COLOR_RGB(76, 70, 68),                        \
        COLOR_RGB(168, 164, 156), COLOR_RGB(216, 212, 200), COLOR_RGB(196, 168, 104),              \
        COLOR_RGB(150, 124, 72), COLOR_RGB(228, 208, 150)
#define JEEP_COLORS_REST                                                                           \
    COLOR_RGB(120, 70, 40), COLOR_RGB(70, 40, 24), COLOR_RGB(236, 228, 196),                       \
        COLOR_RGB(226, 162, 58), COLOR_RGB(240, 240, 230)

static const u16 obj_palettes[OBJ_PALETTE_COUNT][16] = {
    [PAL_JEEP] = {JEEP_COLORS, COLOR_RGB(120, 24, 24), COLOR_RGB(164, 48, 40), JEEP_COLORS_REST},
    [PAL_JEEP_BRAKING] = {JEEP_COLORS, COLOR_RGB(255, 40, 32), COLOR_RGB(255, 184, 160),
                          JEEP_COLORS_REST},
};

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

static u8 canvas[JEEP_W * JEEP_H] SERVAL_EWRAM_BSS;
static u8 straight[JEEP_W * JEEP_H] SERVAL_EWRAM_BSS;

static int key_of(const char* keys, char c) {
    for (int k = 1; keys[k]; k++)
        if (keys[k] == c)
            return k;
    return 0;
}

// The straight jeep into `straight`.
static void draw_jeep(void) {
    for (int y = 0; y < JEEP_H; y++) {
        for (int x = 0; x < JEEP_W / 2; x++) {
            u8 c = (u8)key_of(jeep_keys, jeep_half[y][x]);
            straight[y * JEEP_W + x] = c;
            straight[y * JEEP_W + JEEP_W - 1 - x] = c;
        }
    }
    for (int y = 0; y < SERVAL_ROWS; y++)
        for (int x = 0; serval_head[y][x]; x++)
            if (serval_head[y][x] != '.')
                straight[y * JEEP_W + SERVAL_X + x] = (u8)key_of(jeep_keys, serval_head[y][x]);
}

// A frame into the canvas: the straight jeep, or steering (dir -1 left, 1
// right): the body leans into the turn over its wheels, the cab and the
// serval two pixels, the tub one, so the jeep seems to swing its nose in.
static void jeep_frame(int dir) {
    for (int i = 0; i < JEEP_W * JEEP_H; i++)
        canvas[i] = 0;
    for (int y = 0; y < JEEP_H; y++) {
        int shift = dir * (y < 13 ? 2 : y < 24 ? 1 : 0);
        for (int x = 0; x < JEEP_W; x++) {
            int to = x + shift;
            u8 c = straight[y * JEEP_W + x];
            if (c && to >= 0 && to < JEEP_W)
                canvas[y * JEEP_W + to] = c;
        }
    }
}

// Cuts the canvas into 4bpp tiles, row by row (a sprite frame's layout).
static u32* canvas_pack(u32* out) {
    for (int ty = 0; ty < JEEP_H / 8; ty++) {
        for (int tx = 0; tx < JEEP_W / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)canvas[(ty * 8 + y) * JEEP_W + tx * 8 + x] << (4 * x);
                *out++ = word;
            }
        }
    }
    return out;
}

#define JEEP_TILES (JEEP_W * JEEP_H / 64)
#define OBJ_TILES (JEEP_FRAMES * JEEP_TILES)

// Sprite VRAM: the jeep's 3 frames of 32 tiles, 96 of 1,024.
static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_JEEP] = {.size = SPRITE_64x32,
                  .tiles = obj_tiles,
                  .frame_count = JEEP_FRAMES,
                  .palette_slot = PAL_JEEP,
                  .origin_x = JEEP_W / 2,
                  .origin_y = JEEP_H - 1},
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_JEEP] = &sprites[SPR_JEEP],
};

const SpriteGroup sprite_group = {
    .palettes = &obj_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = OBJ_PALETTE_COUNT,
};

static void build_sprites(void) {
    draw_jeep();
    u32* out = obj_tiles;
    static const s8 steer[JEEP_FRAMES] = {[JEEP_STRAIGHT] = 0, [JEEP_LEFT] = -1, [JEEP_RIGHT] = 1};
    for (int f = 0; f < JEEP_FRAMES; f++) {
        jeep_frame(steer[f]);
        out = canvas_pack(out);
    }
}

void art_build(void) {
    build_road_colors(&road_colors[TIME_AFTERNOON], afternoon_light, afternoon_dark);
    u32 blank[8] = {0};
    bg_tiles_used = 0;
    add_tile(blank); // tile 0
    build_road();
    build_sky();
    road_tileset.tile_count = bg_tiles_used;
    build_palettes();
    build_sprites();
}
