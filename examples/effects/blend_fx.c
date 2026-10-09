// The blending scene: alpha blending, screen_set_blend() and SPRITE_BLEND
// (docs/runtime-systems.md#alpha-blending, docs/sprites.md#alpha-blending).
//
// A garden at night: a starry sky with the moon and hills (background 3,
// LAYER_BACKGROUND), a brick wall and the grass (the playfield, background 2),
// and in front of them a waterfall pouring into a pool (the foreground,
// background 1). A serval walks along the grass with a shadow at its feet, a
// street lamp shines (a metasprite whose halo is a piece of its own), and
// three wisps of light drift in front of everything. The shadow, the halo
// and the wisps are drawn with SPRITE_BLEND.
//
// A switches between three settings of screen_set_blend():
//   MIX   (LAYER_FOREGROUND, LAYER_ALL, 8, 8): the foreground is a top layer,
//         so the water is see-through (half water, half what is behind it,
//         the serval included); the SPRITE_BLEND sprites mix half and half
//         with what is behind them: the shadow darkens the grass by half,
//         the halo and the wisps (which fly in front of the water: LAYER_ALL
//         has the foreground as a bottom layer too) are half see-through.
//   GLOW  (0, LAYER_ALL, 16, 16): no top layer, so only the SPRITE_BLEND
//         sprites blend, and weights summing past 16 add their colors to
//         what is behind them: the halo and the wisps glow. The shadow is
//         black, which adds nothing, so it disappears; the water is opaque.
//   OFF   (0, 0, 0, 0): nothing blends; everything is opaque, the shadow a
//         black blot and the halo and wisps solid rings of color.
// Holding B dims the screen with screen_set_brightness(-6): blending pauses
// while the brightness is not 0 (the hardware has one color effect), so
// everything is drawn opaque, then dimmed; it resumes when B is released.
// The scene's own fades in and out (main.c) pause it the same way.

#include "effects.h"

// --- Sprites -------------------------------------------------------------------

enum { SPR_SERVAL, SPR_SHADOW, SPR_WISP, SPR_POST, SPR_HALO, SPR_LAMP, SPRITE_COUNT };
enum { PAL_SERVAL, PAL_SHADOW, PAL_WISP, PAL_LAMP, PAL_HALO, PALETTE_COUNT };

static const u16 sprite_palettes[PALETTE_COUNT][16] = {
    // K outline, O fur, L light fur, S shade, D spots, W white, E eye, N nose
    [PAL_SERVAL] = {0, COLOR_RGB(44, 26, 22), COLOR_RGB(214, 150, 70), COLOR_RGB(246, 204, 120),
                    COLOR_RGB(160, 96, 46), COLOR_RGB(60, 34, 30), COLOR_RGB(244, 236, 216),
                    COLOR_RGB(200, 232, 90), COLOR_RGB(224, 120, 130)},
    // Black: mixed half and half, it halves what is behind it.
    [PAL_SHADOW] = {0, COLOR_RGB(0, 0, 0)},
    // Rings from the dim edge to the white-hot middle.
    [PAL_WISP] = {0, COLOR_RGB(40, 100, 90), COLOR_RGB(80, 180, 150), COLOR_RGB(160, 240, 200),
                  COLOR_RGB(240, 255, 240)},
    // K outline, I iron, H iron highlight, G glass, W the flame
    [PAL_LAMP] = {0, COLOR_RGB(16, 14, 24), COLOR_RGB(60, 62, 84), COLOR_RGB(120, 124, 150),
                  COLOR_RGB(255, 200, 90), COLOR_RGB(255, 250, 220)},
    [PAL_HALO] = {0, COLOR_RGB(60, 36, 8), COLOR_RGB(110, 70, 20), COLOR_RGB(170, 120, 40),
                  COLOR_RGB(240, 190, 90)},
};

static const char serval_keys[] = ".KOLSDWEN";
static const char lamp_keys[] = ".KIHGW";

// The serval, from the fireflies example: side view, facing right
// (SPRITE_FLIP_H faces it left). A frame is 32x32, the picture in its
// bottom 24 rows, the body in 16 rows and the legs below.
static const char* const serval_body[16] = {
    "...................KK...KK......", "..................KDDK.KDDK.....",
    "..................KDOK.KDOK.....", "..................KOOKKKOOK.....",
    ".................KOOOOOOOOK.....", ".................KOODOOOOOOK....",
    ".................KOOOOOOEKOOK...", ".................KOOOOOOOOOWWK..",
    "..................KOODOOOOWWNK..", "..................KOOOOOOWWWK...",
    "........KKKKKKKKKKKOOOOLLKKK....", ".......KOODOOOODOOOODOOOLLK.....",
    "......KOOOOOOOOOOOOOOOOOLLK.....", "......KOODOOODOOOODOOODOLLK.....",
    ".....KSOOOOOOOOOOOOOOOOLLK......", "....KDSSSSLLLLLLLLLLLSSOOK......",
};

// Standing, then the two strides of the walk: frames 0, 1 and 2.
#define SERVAL_FRAMES 3
static const char* const serval_legs[SERVAL_FRAMES][8] = {
    {"....KOKSSKKKKKKKKKKKKKKSSK......", "....KDKKSOKSSK.....KSOKSSK......",
     "....KOKKSOKSSK.....KSOKSSK......", "....KDKKSOKSSK.....KSOKSSK......",
     "....KDK.KSOKSK.....KSOKSK.......", ".....K..KSOKSK.....KSOKSK.......",
     "........KDSKDK.....KDSKDK.......", "........KKKKKK.....KKKKKK......."},
    {"....KOKSSKKKKKKKKKKKKKKSSK......", "....KDKKSOKKSK.....KSKKSOK......",
     "....KOKKSOK.KSK...KSSK.KSOK.....", "....KDK.KSOK.KSK..KSK...KSOK....",
     "....KDK.KSOK.KSK..KSK...KSOK....", ".....K.KSOK...KSKKSK.....KSOK...",
     ".......KDSK...KDKKDK.....KDSK...", ".......KKKK...KKKKKK.....KKKK..."},
    {"....KOKSSKKKKKKKKKKKKKKSSK......", "....KDKKSKKSOK.....KSOKKSK......",
     "....KOKKSK.KSOK...KSOK.KSK......", "....KDKKSK..KSOK..KSOK..KSK.....",
     "....KDKKSK..KSOK..KSOK..KSK.....", ".....KKSK....KSOKKSOK....KSK....",
     "......KDK....KDSKKDSK....KDK....", "......KKK....KKKKKKKK....KKK...."},
};

// The lamp's post and lantern, 16x32; the flame (W) is 6-7 rows below the
// top, 9 pixels above the post's center.
static const char* const lamp_post[32] = {
    "................", ".....KKKKKK.....", "....KIHHHHIK....", "...KKKKKKKKKK...",
    "....KGGGGGGK....", "....KGGWWGGK....", "....KGWWWWGK....", "....KGWWWWGK....",
    "....KGGWWGGK....", "....KGGGGGGK....", "...KKKKKKKKKK...", "....KIIIIIIK....",
    ".....KKIHKK.....", "......KIHK......", "......KIHK......", "......KIHK......",
    "......KIHK......", "......KIHK......", "......KIHK......", "......KIHK......",
    "......KIHK......", "......KIHK......", "......KIHK......", "......KIHK......",
    "......KIHK......", "......KIHK......", "......KIHK......", ".....KKIHKK.....",
    ".....KIIIHHK....", "....KIIIIIHHK...", "...KIIIIIIIHHK..", "...KKKKKKKKKKK..",
};
#define FLAME_ABOVE_CENTER 9

// Tiles drawn at boot (draw_sprites): 3 serval frames of 16 tiles, the
// shadow (32x8), a wisp (16x16), the post (16x32) and the halo (32x32).
static u32 serval_tiles[SERVAL_FRAMES * 16 * 8] SERVAL_EWRAM_BSS;
static u32 shadow_tiles[4 * 8] SERVAL_EWRAM_BSS;
static u32 wisp_tiles[4 * 8] SERVAL_EWRAM_BSS;
static u32 post_tiles[8 * 8] SERVAL_EWRAM_BSS;
static u32 halo_tiles[16 * 8] SERVAL_EWRAM_BSS;

// The lamp: the post in front, the halo behind it, centered on the flame.
// The halo blends by its own flag, so the lamp is drawn without one. (The
// post is the front piece: a sprite never blends with a sprite behind it, so
// a halo in front would hide the lantern where it covers it.)
static const SpritePiece lamp_pieces[] = {
    {.sprite = SPR_POST},
    {.y = -FLAME_ABOVE_CENTER, .sprite = SPR_HALO, .flags = SPRITE_BLEND},
};

static const SpriteAsset sprites[SPRITE_COUNT] = {
    // Drawn at its feet's middle.
    [SPR_SERVAL] = {.size = SPRITE_32x32,
                    .tiles = serval_tiles,
                    .frame_count = SERVAL_FRAMES,
                    .palette_slot = PAL_SERVAL,
                    .origin_x = 16,
                    .origin_y = 31},
    [SPR_SHADOW] = {.size = SPRITE_32x8,
                    .tiles = shadow_tiles,
                    .palette_slot = PAL_SHADOW,
                    .origin_x = 16,
                    .origin_y = 4},
    [SPR_WISP] = {.size = SPRITE_16x16,
                  .tiles = wisp_tiles,
                  .palette_slot = PAL_WISP,
                  .origin_x = 8,
                  .origin_y = 8},
    [SPR_POST] = {.size = SPRITE_16x32, .tiles = post_tiles, .palette_slot = PAL_LAMP},
    [SPR_HALO] = {.size = SPRITE_32x32, .tiles = halo_tiles, .palette_slot = PAL_HALO},
    // Drawn at the post's center.
    [SPR_LAMP] = {.flags = SPRITE_ASSET_METASPRITE, .pieces = lamp_pieces, .piece_count = 2},
};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    &sprites[SPR_SERVAL], &sprites[SPR_SHADOW], &sprites[SPR_WISP],
    &sprites[SPR_POST],   &sprites[SPR_HALO],   &sprites[SPR_LAMP],
};

static const SpriteGroup sprite_group = {
    .palettes = &sprite_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

// Color c where (2x - 2cx)^2 + (2y - 2cy)^2 (the distance from the center,
// doubled, squared) is below each of the four limits, from the middle out:
// rings of colors 4, 3, 2, 1.
static void rings(int w, int h, const int limits[4]) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = 2 * x - (w - 1), dy = 2 * y - (h - 1), d = dx * dx + dy * dy;
            for (u32 k = 0; k < 4; k++) {
                if (d < limits[k]) {
                    canvas_plot(x, y, 4 - k);
                    break;
                }
            }
        }
    }
}

static void draw_sprites(void) {
    u32* out = serval_tiles;
    for (u32 f = 0; f < SERVAL_FRAMES; f++) {
        canvas_begin(32, 32);
        canvas_draw(serval_body, 16, 0, 8, serval_keys);
        canvas_draw(serval_legs[f], 8, 0, 24, serval_keys);
        out = canvas_pack(out);
    }

    // An ellipse 28 pixels wide and 6 high.
    canvas_begin(32, 8);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 32; x++) {
            int dx = 2 * x - 31, dy = 2 * y - 7;
            if (dx * dx * 36 + dy * dy * 784 <= 784 * 36)
                canvas_plot(x, y, 1);
        }
    }
    canvas_pack(shadow_tiles);

    static const int wisp_rings[4] = {5 * 5, 8 * 8, 11 * 11, 15 * 15};
    canvas_begin(16, 16);
    rings(16, 16, wisp_rings);
    canvas_pack(wisp_tiles);

    canvas_begin(16, 32);
    canvas_draw(lamp_post, 32, 0, 0, lamp_keys);
    canvas_pack(post_tiles);

    static const int halo_rings[4] = {8 * 8, 14 * 14, 21 * 21, 30 * 30};
    canvas_begin(32, 32);
    rings(32, 32, halo_rings);
    canvas_pack(halo_tiles);
}

// --- Backgrounds ---------------------------------------------------------------

// Palette banks: the sky, the wall, the grass, the water. Each bank's keys
// name its colors for the pictures.
enum { BANK_SKY, BANK_WALL, BANK_GRASS, BANK_WATER, BANK_COUNT };
static const u16 bg_palettes[BANK_COUNT][16] = {
    // W star, D dim star, M moon, S moon shade, C crater, H hills, R hilltops
    [BANK_SKY] = {0, COLOR_RGB(250, 250, 220), COLOR_RGB(120, 130, 190), COLOR_RGB(240, 236, 200),
                  COLOR_RGB(196, 188, 150), COLOR_RGB(170, 160, 128), COLOR_RGB(28, 26, 64),
                  COLOR_RGB(52, 48, 104)},
    // B brick, L lit brick, M mortar, X dark brick, T capstone, U lit capstone
    [BANK_WALL] = {0, COLOR_RGB(156, 62, 50), COLOR_RGB(196, 96, 72), COLOR_RGB(64, 40, 52),
                   COLOR_RGB(112, 40, 40), COLOR_RGB(116, 116, 140), COLOR_RGB(168, 168, 190)},
    // G grass, L lit grass, S soil, D dark soil, P pebble
    [BANK_GRASS] = {0, COLOR_RGB(52, 140, 60), COLOR_RGB(110, 200, 80), COLOR_RGB(100, 68, 40),
                    COLOR_RGB(68, 44, 30), COLOR_RGB(150, 128, 96)},
    // D deep, M water, L light, F foam
    [BANK_WATER] = {0, COLOR_RGB(20, 56, 150), COLOR_RGB(40, 110, 210), COLOR_RGB(110, 180, 245),
                    COLOR_RGB(225, 245, 255)},
};
static const char sky_keys[] = ".WDMSCHR";
static const char wall_keys[] = ".BLMXTU";
static const char grass_keys[] = ".GLSDP";
static const char water_keys[] = ".DMLF";

// The metatiles. Each is drawn on a 16x16 canvas into four tiles, in this
// order from tile 1 (tile 0 stays blank).
enum {
    MT_EMPTY,
    MT_STARS_A,
    MT_STARS_B,
    MT_MOON_TL,
    MT_MOON_TR,
    MT_MOON_BL,
    MT_MOON_BR,
    MT_HILLTOP,
    MT_HILL,
    MT_WALL_TOP,
    MT_WALL,
    MT_GRASS,
    MT_SOIL,
    MT_FALL,
    MT_POOL_TOP,
    MT_SPLASH,
    MT_POOL,
    METATILE_COUNT
};
#define TILE_OF(mt) (1 + ((mt) - 1) * 4)
#define BG_TILES TILE_OF(METATILE_COUNT)
#define FALL_FRAMES 4

static u32 bg_tiles[BG_TILES * 8] SERVAL_EWRAM_BSS;
static u32 fall_frames[FALL_FRAMES][4 * 8] SERVAL_EWRAM_BSS;

#define E16 "................"
static const char* const stars_a[16] = {
    E16,
    E16,
    E16,
    ".....D..........",
    "....DWD.........",
    ".....D..........",
    E16,
    E16,
    E16,
    E16,
    "...........W....",
    E16,
    E16,
    E16,
    E16,
    E16,
};
static const char* const stars_b[16] = {
    E16,
    "..W.............",
    E16,
    E16,
    E16,
    E16,
    E16,
    "..........D.....",
    ".........DWD....",
    "..........D.....",
    E16,
    E16,
    E16,
    ".....D..........",
    E16,
    E16,
};
static const char* const wall_top[8] = {
    "UUUU....UUUU....", "TTTT....TTTT....", "TTTT....TTTT....", "UUUUUUUUUUUUUUUU",
    "TTTTTTTTTTTTTTTT", "TTTTTTTTTTTTTTTT", "MMMMMMMMMMMMMMMM", "MMMMMMMMMMMMMMMM",
};
static const char* const grass_top[6] = {
    "...L.......L....", "..LGL..L..LG..L.", ".LGGGLLGL.GGLLG.",
    "GGGGGGGGGGGGGGGG", "GGLGGGGGGLGGGGGG", "SGGSGGSGGGSGGSGG",
};
static const char* const pool_top[5] = {
    "................", "FFF.......FFFF..", "LFFFF...FFFLLFFF",
    "LLLLLFFFLLLLLLLL", "MLLMMMLLLMMMMLLM",
};
static const char* const splash_top[5] = {
    "..F...F..F...F..", ".FFF.FFFFFF.FFF.", "FFFFFFFFFFFFFFFF",
    "LFFLFFFLFFLFFFLL", "MLLFLLMLLFLLMLLM",
};

// The moon: a disc of radius 13 in a 32x32 picture, shaded on its right,
// with three craters; (x, y) in the picture.
static u32 moon_pixel(int x, int y) {
    int dx = 2 * x - 31, dy = 2 * y - 31;
    if (dx * dx + dy * dy >= 26 * 26)
        return 0;
    static const s8 craters[3][3] = {{-8, -6, 4}, {6, 8, 3}, {-2, 12, 2}}; // x, y, radius
    for (u32 k = 0; k < 3; k++) {
        int cx = dx - 2 * craters[k][0], cy = dy - 2 * craters[k][1], r = 2 * craters[k][2];
        if (cx * cx + cy * cy < r * r)
            return 5;
    }
    int lx = dx + 10, ly = dy + 6; // the lit part: a disc offset to the upper left
    return lx * lx + ly * ly < 30 * 30 ? 3 : 4;
}

// The waterfall: streaks of water, light and foam falling `shift` pixels.
static void draw_fall(int shift) {
    static const u8 streaks[16] = {2, 2, 3, 2, 1, 2, 3, 3, 2, 2, 1, 2, 3, 2, 2, 3};
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int v = (y - shift + x * 7) & 15;
            u32 c = v < 2 ? 4 : v < 5 ? 3 : streaks[x];
            canvas_plot(x, y, c);
        }
    }
}

static void draw_backgrounds(void) {
    u32* out = bg_tiles + 8; // tile 0 stays blank
    for (u32 mt = 1; mt < METATILE_COUNT; mt++) {
        canvas_begin(16, 16);
        switch (mt) {
        case MT_STARS_A:
            canvas_draw(stars_a, 16, 0, 0, sky_keys);
            break;
        case MT_STARS_B:
            canvas_draw(stars_b, 16, 0, 0, sky_keys);
            break;
        case MT_MOON_TL:
        case MT_MOON_TR:
        case MT_MOON_BL:
        case MT_MOON_BR: {
            int qx = (int)((mt - MT_MOON_TL) & 1) * 16, qy = (int)((mt - MT_MOON_TL) >> 1) * 16;
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    canvas_plot(x, y, moon_pixel(qx + x, qy + y));
            break;
        }
        case MT_HILLTOP: {
            static const u8 top[16] = {9, 8, 7, 7, 6, 6, 6, 7, 7, 8, 8, 9, 9, 8, 8, 9};
            for (int x = 0; x < 16; x++)
                for (int y = top[x]; y < 16; y++)
                    canvas_plot(x, y, y == top[x] ? 7 : 6);
            break;
        }
        case MT_HILL:
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    canvas_plot(x, y, 6);
            break;
        case MT_WALL_TOP:
        case MT_WALL:
            // Bricks 8x4 with a mortar line, every other row shifted by 4,
            // lit along their top edge.
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    int bx = (x + ((y >> 2) & 1) * 4) & 7, by = y & 3;
                    canvas_plot(x, y, by == 3 || bx == 7 ? 3 : by == 0 ? 2 : (x + y) % 5 ? 1 : 4);
                }
            }
            if (mt == MT_WALL_TOP) {
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 16; x++)
                        canvas_plot(x, y, 0);
                canvas_draw(wall_top, 8, 0, 0, wall_keys);
            }
            break;
        case MT_GRASS:
        case MT_SOIL:
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    canvas_plot(x, y, (x * 3 + y * 5) % 7 ? 3 : (x + y) % 3 ? 4 : 5);
            if (mt == MT_GRASS) {
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 16; x++)
                        canvas_plot(x, y, 0);
                canvas_draw(grass_top, 6, 0, 2, grass_keys);
            }
            break;
        case MT_FALL:
            draw_fall(0);
            break;
        case MT_POOL_TOP:
        case MT_SPLASH:
        case MT_POOL:
            // Deep water with ripples of lighter water.
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    canvas_plot(x, y, ((x + y * 3) & 7) == 0 && (y & 1) ? 2 : 1);
            if (mt != MT_POOL) {
                for (int y = 0; y < 5; y++)
                    for (int x = 0; x < 16; x++)
                        canvas_plot(x, y, 0);
                canvas_draw(mt == MT_SPLASH ? splash_top : pool_top, 5, 0, 0, water_keys);
            }
            break;
        }
        out = canvas_pack(out);
    }
    for (int f = 0; f < FALL_FRAMES; f++) {
        canvas_begin(16, 16);
        draw_fall(f * 4);
        canvas_pack(fall_frames[f]);
    }
}

// Every metatile's four screen entries, from its tiles and its bank.
#define MT(mt, bank)                                                                               \
    {                                                                                              \
        .se = {                                                                                    \
            MAP_SE(TILE_OF(mt), bank, 0),                                                          \
            MAP_SE(TILE_OF(mt) + 1, bank, 0),                                                      \
            MAP_SE(TILE_OF(mt) + 2, bank, 0),                                                      \
            MAP_SE(TILE_OF(mt) + 3, bank, 0)                                                       \
        }                                                                                          \
    }
static const Metatile metatiles[METATILE_COUNT] = {
    [MT_STARS_A] = MT(MT_STARS_A, BANK_SKY),    [MT_STARS_B] = MT(MT_STARS_B, BANK_SKY),
    [MT_MOON_TL] = MT(MT_MOON_TL, BANK_SKY),    [MT_MOON_TR] = MT(MT_MOON_TR, BANK_SKY),
    [MT_MOON_BL] = MT(MT_MOON_BL, BANK_SKY),    [MT_MOON_BR] = MT(MT_MOON_BR, BANK_SKY),
    [MT_HILLTOP] = MT(MT_HILLTOP, BANK_SKY),    [MT_HILL] = MT(MT_HILL, BANK_SKY),
    [MT_WALL_TOP] = MT(MT_WALL_TOP, BANK_WALL), [MT_WALL] = MT(MT_WALL, BANK_WALL),
    [MT_GRASS] = MT(MT_GRASS, BANK_GRASS),      [MT_SOIL] = MT(MT_SOIL, BANK_GRASS),
    [MT_FALL] = MT(MT_FALL, BANK_WATER),        [MT_POOL_TOP] = MT(MT_POOL_TOP, BANK_WATER),
    [MT_SPLASH] = MT(MT_SPLASH, BANK_WATER),    [MT_POOL] = MT(MT_POOL, BANK_WATER),
};

static const Tileset tileset = {
    .tiles = bg_tiles,
    .tile_count = BG_TILES,
    .palettes = &bg_palettes[0][0],
    .palette_count = BANK_COUNT,
};

// The three layers, a screen each (15 x 10 metatiles). Shorter names for
// the maps.
enum {
    NO = MT_EMPTY,
    S1 = MT_STARS_A,
    S2 = MT_STARS_B,
    M1 = MT_MOON_TL,
    M2 = MT_MOON_TR,
    M3 = MT_MOON_BL,
    M4 = MT_MOON_BR,
    HT = MT_HILLTOP,
    HH = MT_HILL,
    WT = MT_WALL_TOP,
    WW = MT_WALL,
    GG = MT_GRASS,
    SS = MT_SOIL,
    FF = MT_FALL,
    PT = MT_POOL_TOP,
    SP = MT_SPLASH,
    PP = MT_POOL,
};
#define LAYER_W 15
#define LAYER_H 10

// Background 3: the sky, the moon and the hills.
static const u16 sky_cells[LAYER_W * LAYER_H] = {
    S1, NO, S2, NO, NO, S1, NO, NO, S2, NO, NO, S1, NO, S2, NO, //
    NO, S2, NO, NO, NO, NO, S2, NO, NO, S1, NO, NO, NO, NO, S1, //
    NO, NO, NO, M1, M2, NO, NO, S1, NO, NO, NO, S2, NO, NO, NO, //
    S2, NO, NO, M3, M4, NO, S2, NO, NO, S1, NO, NO, S1, NO, NO, //
    NO, S1, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, //
    HT, HT, HT, HT, HT, HT, HT, HT, HT, HT, HT, HT, HT, HT, HT, //
    HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, //
    HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, //
    HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, //
    HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, HH, //
};

// Background 2, the playfield: the wall and the grass.
static const u16 wall_cells[LAYER_W * LAYER_H] = {
    NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, WT, WT, WT, WT, WT, WT, WT, WT, WT, //
    NO, NO, NO, NO, NO, NO, WW, WW, WW, WW, WW, WW, WW, WW, WW, //
    NO, NO, NO, NO, NO, NO, WW, WW, WW, WW, WW, WW, WW, WW, WW, //
    NO, NO, NO, NO, NO, NO, WW, WW, WW, WW, WW, WW, WW, WW, WW, //
    NO, NO, NO, NO, NO, NO, WW, WW, WW, WW, WW, WW, WW, WW, WW, //
    GG, GG, GG, GG, GG, GG, GG, GG, GG, GG, GG, GG, GG, GG, GG, //
    SS, SS, SS, SS, SS, SS, SS, SS, SS, SS, SS, SS, SS, SS, SS, //
};

// Background 1, the foreground: the waterfall and its pool.
static const u16 water_cells[LAYER_W * LAYER_H] = {
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, NO, NO, FF, FF, FF, NO, NO, NO, //
    NO, NO, NO, NO, NO, NO, NO, PT, PT, SP, SP, SP, PT, PT, PT, //
    NO, NO, NO, NO, NO, NO, NO, PP, PP, PP, PP, PP, PP, PP, PP, //
};

#define LAYER(cells_, bg_)                                                                         \
    {.width = LAYER_W,                                                                             \
     .height = LAYER_H,                                                                            \
     .cells = (cells_),                                                                            \
     .metatiles = metatiles,                                                                       \
     .metatile_count = METATILE_COUNT,                                                             \
     .bg = (bg_)}
static const MapLayer layers[3] = {LAYER(sky_cells, 3), LAYER(wall_cells, 2),
                                   LAYER(water_cells, 1)};

// --- The scene -----------------------------------------------------------------

// What A switches between.
typedef struct {
    u32 top, bottom, top_weight, bottom_weight;
    const char* call;  // the call, for the HUD
    const char* shows; // what to look for
} Setting;

static const Setting settings[] = {
    {LAYER_FOREGROUND, LAYER_ALL, 8, 8, "blend(FOREGROUND, ALL, 8, 8)", "MIX: SEE-THROUGH WATER"},
    {0, LAYER_ALL, 16, 16, "blend(0, ALL, 16, 16)", "GLOW: SPRITES ADD LIGHT"},
    {0, 0, 0, 0, "blend(0, 0, 0, 0)", "OFF: ALL OPAQUE"},
};
#define SETTING_COUNT (sizeof(settings) / sizeof(settings[0]))

#define GROUND_Y 140 // where the serval's feet and the lamp's foot are
#define LAMP_X 40
#define DIM_LEVEL (-6)

// Three wisps, each drifting around its home on two sine waves.
typedef struct {
    s16 x, y; // home
    u8 reach_x, reach_y;
    u16 speed_x, speed_y; // angle steps per frame
} Wisp;
static const Wisp wisps[3] = {
    {64, 44, 40, 10, 300, 410},
    {168, 34, 36, 14, 230, 520},
    {120, 78, 70, 8, 170, 350},
};

static bool art_drawn;
static u32 setting;
static u32 ticks;
static int serval_x;
static bool facing_left;
static bool dimmed; // B held: the screen dimmed, blending paused

static void show_setting(void) {
    const Setting* s = &settings[setting];
    screen_set_blend(s->top, s->bottom, s->top_weight, s->bottom_weight);
    text_print_centered(1, s->call);
    text_print_centered(18, s->shows);
}

static void enter(void) {
    if (!art_drawn) {
        draw_sprites();
        draw_backgrounds();
        art_drawn = true;
    }
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    tileset_load(&tileset);
    for (u32 k = 0; k < 3; k++)
        map_load(&layers[k]);
    screen_set_backdrop(COLOR_RGB(14, 16, 44));
    text_print_centered(19, "A:NEXT  B(HOLD):DIM  <>:WALK");
    setting = 0;
    ticks = 0;
    serval_x = 80;
    facing_left = false;
    dimmed = false;
    show_setting();
}

static void update(void) {
    ticks++;
    if (button_pressed(BUTTON_A)) {
        setting = (setting + 1) % SETTING_COUNT;
        show_setting();
    }
    // Only on a change, so the fades of main.c keep the brightness.
    if (button_pressed(BUTTON_B)) {
        dimmed = true;
        screen_set_brightness(DIM_LEVEL);
        text_print_centered(2, "DIMMED: BLENDING PAUSED");
    } else if (dimmed && !button_down(BUTTON_B)) {
        dimmed = false;
        screen_set_brightness(0);
        text_print_centered(2, "");
    }

    // The serval walks, a stride every 8 frames.
    int step = button_down(BUTTON_RIGHT) ? 1 : button_down(BUTTON_LEFT) ? -1 : 0;
    if (step)
        facing_left = step < 0;
    serval_x = int_clamp(serval_x + step, 16, SCREEN_W - 16);
    u32 frame = step ? 1 + ((ticks >> 3) & 1) : 0;

    // The waterfall falls 4 pixels every 4 frames.
    if ((ticks & 3) == 0)
        tileset_set_tiles(TILE_OF(MT_FALL), fall_frames[(ticks >> 2) % FALL_FRAMES], 4);

    // Drawn first, in front: the wisps, above the foreground.
    for (u32 k = 0; k < 3; k++) {
        const Wisp* w = &wisps[k];
        int x = w->x + (w->reach_x * fx_sin((u16)(ticks * w->speed_x + k * 20000))) / FX_ONE;
        int y = w->y + (w->reach_y * fx_sin((u16)(ticks * w->speed_y + k * 9000))) / FX_ONE;
        sprite_draw(SPR_WISP, 0, x, y, SPRITE_BLEND | SPRITE_ABOVE_FOREGROUND);
    }
    // The serval in front of its shadow, both behind the foreground.
    sprite_draw(SPR_SERVAL, (u8)frame, serval_x, GROUND_Y, facing_left ? SPRITE_FLIP_H : 0);
    sprite_draw(SPR_SHADOW, 0, serval_x, GROUND_Y - 1, SPRITE_BLEND);
    // The lamp: its halo blends by the piece's own SPRITE_BLEND.
    sprite_draw(SPR_LAMP, 0, LAMP_X, GROUND_Y - 16, 0);
}

static void leave(void) {
    screen_set_blend(0, 0, 0, 0);
    dimmed = false;
}

const Scene blend_scene = {.name = "BLENDING", .enter = enter, .update = update, .leave = leave};
