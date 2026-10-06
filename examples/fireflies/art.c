// Graphics: every sprite and background tile, drawn at boot (art_build) from
// ASCII pictures into EWRAM buffers that sprite_group_load and tileset_load
// copy to VRAM, as blackjack does. One character per pixel: '.' is
// transparent, letters pick palette colors (the keys are by each palette).
// Also the fireflies' flight paths, which the scripts start by index.

#include "game.h"

// --- Palettes ----------------------------------------------------------------

enum { PAL_SERVAL, PAL_GLOW, PALETTE_COUNT };

static const u16 palettes[PALETTE_COUNT][16] = {
    // K outline, O fur, L light fur, S shade, D spots, W white, E eye, N nose
    [PAL_SERVAL] = {0, COLOR_RGB(44, 26, 22), COLOR_RGB(214, 150, 70), COLOR_RGB(246, 204, 120),
                    COLOR_RGB(160, 96, 46), COLOR_RGB(60, 34, 30), COLOR_RGB(244, 236, 216),
                    COLOR_RGB(200, 232, 90), COLOR_RGB(224, 120, 130)},
    // Fireflies: 1-4 glow, dim to white-hot; 5 the dark body. Sparkles: W
    // white, Y pale yellow, G gold.
    [PAL_GLOW] = {0, COLOR_RGB(70, 120, 50), COLOR_RGB(160, 220, 60), COLOR_RGB(230, 255, 120),
                  COLOR_RGB(255, 255, 230), COLOR_RGB(40, 30, 40), COLOR_RGB(255, 255, 255),
                  COLOR_RGB(255, 240, 140), COLOR_RGB(240, 170, 50)},
};

// Picture characters to colors, per palette.
static const char serval_keys[] = ".KOLSDWEN";
static const char glow_keys[] = ".12345WYG";

// --- A canvas to draw on, then cut into tiles --------------------------------

static u8 canvas[32 * 32] SERVAL_EWRAM_BSS;
static int canvas_w, canvas_h;

static void canvas_begin(int w, int h) {
    canvas_w = w;
    canvas_h = h;
    for (int i = 0; i < w * h; i++)
        canvas[i] = 0;
}

// The color index of picture character c in `keys` (0 if it isn't there).
static int key(const char* keys, char c) {
    for (int k = 0; keys[k]; k++)
        if (keys[k] == c)
            return k;
    return 0;
}

// Draws `rows` rows of a picture at (x, y); with `blink`, eyes are drawn
// closed (the outline color).
static void draw(const char* const* rows, int count, int x, int y, const char* keys, bool blink) {
    for (int py = 0; py < count; py++) {
        for (int px = 0; rows[py][px]; px++) {
            int c = key(keys, blink && rows[py][px] == 'E' ? 'K' : rows[py][px]);
            int cx = x + px, cy = y + py;
            if (c && cx < canvas_w && cy < canvas_h)
                canvas[cy * canvas_w + cx] = (u8)c;
        }
    }
}

// Cuts the canvas into 4bpp tiles at `out`, row by row (1D mapping, as sprite
// frames and a metatile's four tiles are laid out). Returns the end.
static u32* canvas_pack(u32* out) {
    for (int ty = 0; ty < canvas_h / 8; ty++) {
        for (int tx = 0; tx < canvas_w / 8; tx++) {
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)canvas[(ty * 8 + y) * canvas_w + tx * 8 + x] << (4 * x);
                *out++ = word;
            }
        }
    }
    return out;
}

// --- The serval ----------------------------------------------------------------
//
// Side view, facing right (SPRITE_FLIP_H faces it left): a golden, spotted
// coat, big ears, long legs, a short ringed tail. A frame is 32x32 with the
// picture in its bottom 24 rows; the body (head to rump, SERVAL_BODY_W x
// SERVAL_BODY_H) starts 4 pixels in and 12 down, the sprite's origin.

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

// Legs (and the tail's tip) under the body: standing, and the two strides
// of the walk (near legs in fur, far ones in shade).
enum { LEGS_STAND, LEGS_STRIDE1, LEGS_STRIDE2, LEG_POSES };
static const char* const serval_legs[LEG_POSES][8] = {
    [LEGS_STAND] = {"....KOKSSKKKKKKKKKKKKKKSSK......", "....KDKKSOKSSK.....KSOKSSK......",
                    "....KOKKSOKSSK.....KSOKSSK......", "....KDKKSOKSSK.....KSOKSSK......",
                    "....KDK.KSOKSK.....KSOKSK.......", ".....K..KSOKSK.....KSOKSK.......",
                    "........KDSKDK.....KDSKDK.......", "........KKKKKK.....KKKKKK......."},
    [LEGS_STRIDE1] = {"....KOKSSKKKKKKKKKKKKKKSSK......", "....KDKKSOKKSK.....KSKKSOK......",
                      "....KOKKSOK.KSK...KSSK.KSOK.....", "....KDK.KSOK.KSK..KSK...KSOK....",
                      "....KDK.KSOK.KSK..KSK...KSOK....", ".....K.KSOK...KSKKSK.....KSOK...",
                      ".......KDSK...KDKKDK.....KDSK...", ".......KKKK...KKKKKK.....KKKK..."},
    [LEGS_STRIDE2] = {"....KOKSSKKKKKKKKKKKKKKSSK......", "....KDKKSKKSOK.....KSOKKSK......",
                      "....KOKKSK.KSOK...KSOK.KSK......", "....KDKKSK..KSOK..KSOK..KSK.....",
                      "....KDKKSK..KSOK..KSOK..KSK.....", ".....KKSK....KSOKKSOK....KSK....",
                      "......KDK....KDSKKDSK....KDK....", "......KKK....KKKKKKKK....KKK...."},
};

// Sitting (time is up): the head as standing, the body sat down, the tail
// curled on the ground.
static const char* const serval_sitting[14] = {
    "...............KKKKOOOOLLKKK....", ".............KKOODOOOOOLLK......",
    "............KOOOOOOOODOOLLK.....", "...........KOODOOOOOOOOOLLK.....",
    "..........KOOOOOODOOOOOLLK......", ".........KOOOODOOOOOOOLLLK......",
    "........KOOOOOOOOOKSOKLLK.......", "........KOODOOOOOKKSOKLLK.......",
    "........KOOOOOOOOK.KSOKSK.......", "........KSOOODOOK..KSOKSK.......",
    ".KK.....KSSOOOOOK..KSOKSK.......", "KDOK....KSSSOOOK...KSOKSK.......",
    "KDODKKKKKSSSSSKK..KKDSKDK.......", ".KKDODODKKKKKKK...KKKKKKK.......",
};

enum { POSE_STANDING, POSE_SITTING };

// One 32x32 frame of the serval at `out`.
static u32* serval_frame(u32* out, int pose, int legs, bool blink) {
    canvas_begin(32, 32);
    if (pose == POSE_SITTING) {
        draw(serval_body, 10, 0, 8, serval_keys, blink);
        draw(serval_sitting, 14, 0, 18, serval_keys, blink);
    } else {
        draw(serval_body, 16, 0, 8, serval_keys, blink);
        draw(serval_legs[legs], 8, 0, 24, serval_keys, blink);
    }
    return canvas_pack(out);
}

// --- Fireflies and sparkles ------------------------------------------------------

enum {
    GLOW_DARK,
    GLOW_DIM,
    GLOW_MID,
    GLOW_BRIGHT,
    GLOW_RING,
    GLOW_DOTS,
    GLOW_SPECK,
    GLOW_NONE,
    GLOW_COUNT
};
static const char* const glow_art[GLOW_COUNT][8] = {
    [GLOW_DARK] = {"........", "........", "........", "...22...", "...22...", "........",
                   "........", "........"},
    [GLOW_DIM] = {"........", "........", "...11...", "..1331..", "..1331..", "...11...",
                  "........", "........"},
    [GLOW_MID] = {"........", "...11...", "..1221..", ".123321.", ".123321.", "..1221..",
                  "...11...", "........"},
    [GLOW_BRIGHT] = {"...11...", ".112211.", ".123321.", "12344321", "12344321", ".123321.",
                     ".112211.", "...11..."},
    [GLOW_RING] = {"..1221..", ".2....2.", "1......1", "2..33..2", "2..33..2", "1......1",
                   ".2....2.", "..1221.."},
    [GLOW_DOTS] = {".1....1.", "........", "1......1", "...22...", "...22...", "1......1",
                   "........", ".1....1."},
    [GLOW_SPECK] = {"........", "........", "....1...", "........", "........", "...1....",
                    "........", "........"},
    [GLOW_NONE] = {"........", "........", "........", "........", "........", "........",
                   "........", "........"},
};

// The blink: mostly a dim ember, then a flash (frame_times below).
static const u8 blink_frames[FIREFLY_FRAMES] = {GLOW_DARK,   GLOW_DIM, GLOW_MID,
                                                GLOW_BRIGHT, GLOW_MID, GLOW_DIM};
static const u8 blink_times[FIREFLY_FRAMES] = {24, 3, 3, 18, 4, 4};
// The fade: a last flash whose glow spreads into a ring and scatters.
#define FADE_FRAMES 5
static const u8 fade_frames[FADE_FRAMES] = {GLOW_BRIGHT, GLOW_RING, GLOW_DOTS, GLOW_SPECK,
                                            GLOW_NONE};
static const u8 fade_times[FADE_FRAMES] = {6, 6, 7, 8, 1};

#define SPARKLE_FRAMES 6
#define E16 "................"
static const char* const sparkle_art[SPARKLE_FRAMES][16] = {
    {E16, E16, E16, E16, E16, E16, ".......YY.......", "......YWWY......", "......YWWY......",
     ".......YY.......", E16, E16, E16, E16, E16, E16},
    {E16, E16, E16, ".......YY.......", ".......WW.......", ".......WW.......", "......YWWY......",
     "...YWWWWWWWWY...", "...YWWWWWWWWY...", "......YWWY......", ".......WW.......",
     ".......WW.......", ".......YY.......", E16, E16, E16},
    {".......YY.......", ".......WW.......", "..G....WW....G..", "...W...YY...W...",
     "....W......W....", ".....Y....Y.....", E16, "YWWY........YWWY", "YWWY........YWWY", E16,
     ".....Y....Y.....", "....W......W....", "...W...YY...W...", "..G....WW....G..",
     ".......WW.......", ".......YY......."},
    {".......WW.......", E16, "..YY........YY..", "..YY........YY..", E16, E16, E16,
     "W..............W", "W..............W", E16, E16, E16, "..YY........YY..", "..YY........YY..",
     E16, ".......WW......."},
    {".......G........", E16, "..G..........G..", E16, E16, E16, E16, "G..............G", E16, E16,
     E16, E16, E16, "..G..........G..", E16, "........G......."},
    {E16, E16, E16, ".............G..", E16, E16, E16, E16, E16, E16, E16, E16, "..G.............",
     E16, E16, E16},
};
static const u8 sparkle_times[SPARKLE_FRAMES] = {3, 3, 4, 5, 5, 4};

// --- Sprites -------------------------------------------------------------------------

#define SERVAL_TILES 16                      // per 32x32 frame
#define T_IDLE 0                             // 2 frames: standing, blinking
#define T_WALK (T_IDLE + 2 * SERVAL_TILES)   // 4 frames: stride, stand, stride, stand
#define T_SIT (T_WALK + 4 * SERVAL_TILES)    // 2 frames: sitting, blinking
#define T_FIREFLY (T_SIT + 2 * SERVAL_TILES) // FIREFLY_FRAMES of 1
#define T_FADE (T_FIREFLY + FIREFLY_FRAMES)  // FADE_FRAMES of 1
#define T_SPARKLE (T_FADE + FADE_FRAMES)     // SPARKLE_FRAMES of 4
#define OBJ_TILES (T_SPARKLE + SPARKLE_FRAMES * 4)

static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

static const u8 idle_times[2] = {150, 6};
static const u8 walk_times[4] = {7, 7, 7, 7};

#define SERVAL_SPRITE(first, frames, times)                                                        \
    {.size = SPRITE_32x32,                                                                         \
     .tiles = obj_tiles + (first) * 8,                                                             \
     .frame_count = (frames),                                                                      \
     .frame_times = (times),                                                                       \
     .palette_slot = PAL_SERVAL,                                                                   \
     .origin_x = 4,                                                                                \
     .origin_y = 12}

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_SERVAL_IDLE] = SERVAL_SPRITE(T_IDLE, 2, idle_times),
    [SPR_SERVAL_WALK] = SERVAL_SPRITE(T_WALK, 4, walk_times),
    [SPR_SERVAL_SIT] = SERVAL_SPRITE(T_SIT, 2, idle_times),
    [SPR_FIREFLY] = {.size = SPRITE_8x8,
                     .tiles = obj_tiles + T_FIREFLY * 8,
                     .frame_count = FIREFLY_FRAMES,
                     .frame_times = blink_times,
                     .palette_slot = PAL_GLOW},
    [SPR_FIREFLY_FADE] = {.size = SPRITE_8x8,
                          .tiles = obj_tiles + T_FADE * 8,
                          .frame_count = FADE_FRAMES,
                          .frame_times = fade_times,
                          .palette_slot = PAL_GLOW,
                          .flags = SPRITE_ASSET_ANIM_ONCE},
    [SPR_SPARKLE] = {.size = SPRITE_16x16,
                     .tiles = obj_tiles + T_SPARKLE * 8,
                     .frame_count = SPARKLE_FRAMES,
                     .frame_times = sparkle_times,
                     .palette_slot = PAL_GLOW,
                     .flags = SPRITE_ASSET_ANIM_ONCE},
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_SERVAL_IDLE] = &sprites[SPR_SERVAL_IDLE],   [SPR_SERVAL_WALK] = &sprites[SPR_SERVAL_WALK],
    [SPR_SERVAL_SIT] = &sprites[SPR_SERVAL_SIT],     [SPR_FIREFLY] = &sprites[SPR_FIREFLY],
    [SPR_FIREFLY_FADE] = &sprites[SPR_FIREFLY_FADE], [SPR_SPARKLE] = &sprites[SPR_SPARKLE],
};

const SpriteGroup sprite_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

static void build_sprites(void) {
    u32* out = obj_tiles + T_IDLE * 8;
    out = serval_frame(out, POSE_STANDING, LEGS_STAND, false);
    out = serval_frame(out, POSE_STANDING, LEGS_STAND, true);
    // T_WALK
    out = serval_frame(out, POSE_STANDING, LEGS_STRIDE1, false);
    out = serval_frame(out, POSE_STANDING, LEGS_STAND, false);
    out = serval_frame(out, POSE_STANDING, LEGS_STRIDE2, false);
    out = serval_frame(out, POSE_STANDING, LEGS_STAND, false);
    // T_SIT
    out = serval_frame(out, POSE_SITTING, 0, false);
    out = serval_frame(out, POSE_SITTING, 0, true);
    // T_FIREFLY, T_FADE
    for (int f = 0; f < FIREFLY_FRAMES; f++) {
        canvas_begin(8, 8);
        draw(glow_art[blink_frames[f]], 8, 0, 0, glow_keys, false);
        out = canvas_pack(out);
    }
    for (int f = 0; f < FADE_FRAMES; f++) {
        canvas_begin(8, 8);
        draw(glow_art[fade_frames[f]], 8, 0, 0, glow_keys, false);
        out = canvas_pack(out);
    }
    // T_SPARKLE
    for (int f = 0; f < SPARKLE_FRAMES; f++) {
        canvas_begin(16, 16);
        draw(sparkle_art[f], 16, 0, 0, glow_keys, false);
        out = canvas_pack(out);
    }
}

// --- The meadow ----------------------------------------------------------------------
//
// Two map layers of 16x16 metatiles, one screen each (the camera never
// moves). Background 3: a strip of night sky with stars and a crescent moon,
// a glow along a dark treeline, and below it the meadow: the deep blue-violet
// backdrop (screen_set_backdrop, main.c) with tufts of grass and a few
// flowers. Background 1, in front of the sprites: tall grass along the bottom,
// which the serval's legs disappear into.

// Background colors: a b sky (dark, mid), c d glow (violet, pink), k
// treeline, t u tufts (dark, moonlit), s S stars (dim, bright), m n moon
// (lit, shade), g h i tall grass (dark to light), f flowers.
static const char meadow_keys[] = ".abcdktusSmnghif";
static const u16 meadow_colors[16] = {
    0,
    COLOR_RGB(16, 14, 46),
    COLOR_RGB(30, 22, 72),
    COLOR_RGB(84, 44, 112),
    COLOR_RGB(150, 76, 128),
    COLOR_RGB(12, 10, 30),
    COLOR_RGB(34, 26, 74),
    COLOR_RGB(84, 72, 146),
    COLOR_RGB(140, 140, 190),
    COLOR_RGB(255, 250, 220),
    COLOR_RGB(250, 244, 206),
    COLOR_RGB(200, 190, 150),
    COLOR_RGB(12, 16, 38),
    COLOR_RGB(24, 34, 66),
    COLOR_RGB(48, 66, 104),
    COLOR_RGB(220, 200, 240),
};

enum {
    MT_EMPTY,
    MT_SKY,
    MT_STARS1,
    MT_STARS2,
    MT_MOON,
    MT_TREES1,
    MT_TREES2,
    MT_TREES3,
    MT_TUFT1,
    MT_TUFT2,
    MT_FLOWERS,
    MT_GRASS1,
    MT_GRASS2,
    MT_COUNT
};

#define SKY "aaaaaaaaaaaaaaaa"
#define GLOW_ROWS                                                                                  \
    "abababababababab", "babababababababa", "bbbbbbbbbbbbbbbb", "bcbbbcbbbcbbbcbb",                \
        "cbcbcbcbcbcbcbcb", "cccccccccccccccc", "cdcccdcccdcccdcc", "dcdcdcdcdcdcdcdc"
#define BLANK "................"

static const char* const metatile_art[MT_COUNT][16] = {
    [MT_EMPTY] = {BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK,
                  BLANK, BLANK, BLANK, BLANK, BLANK},
    [MT_SKY] = {SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY, SKY},
    [MT_STARS1] = {SKY, SKY, "aaasaaaaaaaaaaaa", SKY, "aaaaaaaaaaaSaaaa", "aaaaaaaaaasSsaaa",
                   "aaaaaaaaaaaSaaaa", SKY, SKY, "aaaaasaaaaaaaaaa", SKY, SKY, "aaaaaaaaaaaaasaa",
                   SKY, SKY, SKY},
    [MT_STARS2] = {SKY, "aaaaaaaasaaaaaaa", SKY, SKY, SKY, "aaSaaaaaaaaaaaaa", SKY, SKY,
                   "aaaaaaaaaaaaSaaa", "aaaaaaaaaaasSsaa", "aaaaaaaaaaaaSaaa", SKY,
                   "aaaasaaaaaaaaaaa", SKY, SKY, SKY},
    [MT_MOON] = {SKY, "aaaaaaaaaaaaaaaa", "aaaaaammmmaaaaaa", "aaaammmmnaaaaaaa",
                 "aaammmmnaaaaaaaa", "aaammmnaaaaaaaaa", "aammmmnaaaaaaaaa", "aammmmnaaaaaaaaa",
                 "aammmmnaaaaaaaaa", "aaammmmnaaaaaaaa", "aaammmmmnaaaaaaa", "aaaammmmmnnaaaaa",
                 "aaaaaammmmmaaaaa", SKY, SKY, SKY},
    [MT_TREES1] = {GLOW_ROWS, "dddddddddddddddd", "dddddkkkdddddddd", "ddddkkkkkddddkdd",
                   "dkkkkkkkkkkdkkkd", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk",
                   "kkkkkkkkkkkkkkkk"},
    [MT_TREES2] = {GLOW_ROWS, "ddddddddddddkddd", "ddkdddddddkkkkdd", "dkkkddddddkkkkkd",
                   "kkkkkdddkkkkkkkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk",
                   "kkkkkkkkkkkkkkkk"},
    [MT_TREES3] = {GLOW_ROWS, "dddddddddddddddd", "dddddddddddddddd", "ddddddkkkddddddd",
                   "dkkddkkkkkkdddkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk",
                   "kkkkkkkkkkkkkkkk"},
    [MT_TUFT1] = {BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, ".....u..........", "....tu.u........",
                  "....tutu.t......", ".....ttutt......", BLANK, BLANK, BLANK, BLANK, BLANK, BLANK},
    [MT_TUFT2] = {BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK,
                  "..........u.....", ".........u.tu...", "........tuttu...", ".........tttt...",
                  BLANK, BLANK},
    [MT_FLOWERS] = {BLANK, BLANK, "...f............", "..fuf...........", "...t.....f......",
                    "...t....fuf.....", "..tt.....t......", ".........t......", "........tt......",
                    BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK},
    [MT_GRASS1] = {BLANK, "..i.............", "..h.......i.....", ".ih.......h.....",
                   ".hh..i...hh..i..", ".hg..h...hg.ih..", "hgg.hh..hgg.hg..", "hgg.hgh.hggihgh.",
                   "ggghggh.gggggggh", "gggggggghggggggg", "gggggggggggggggg", "gggggggggggggggg",
                   "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg"},
    [MT_GRASS2] = {".......i........", ".......h.....i..", "...i...hi....h..", "...h..hhh...ih..",
                   "..hh..hgh...hg..", "i.hg.hhgg..hgg.i", "h.hghhggg..hgg.h", "hhgghggggihggghh",
                   "gggggggggghggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg",
                   "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg"},
};

static u32 bg_tiles[MT_COUNT * 4 * 8] SERVAL_EWRAM_BSS;
static Metatile metatiles[MT_COUNT] SERVAL_EWRAM_BSS;

// The two layers, one character per metatile.
#define MAP_W (SCREEN_W / 16)
#define MAP_H (SCREEN_H / 16)
static const char* const meadow_map[MAP_H] = {
    "..*.+...M.*.+..", "ABCABBCACBACBAB", "    t      v   ", " v       f    t", "      t        ",
    "  f         v  ", "          t    ", " t    v        ", "         f   t ", "    v      t   ",
};
static const char* const grass_map[MAP_H] = {
    "               ", "               ", "               ", "               ", "               ",
    "               ", "               ", "               ", "               ", "GHGGHGHGGHGHGHG",
};
static const char map_keys[MT_COUNT + 1] = " .*+MABCtvfGH";

static u16 meadow_cells[MAP_W * MAP_H] SERVAL_EWRAM_BSS;
static u16 grass_cells[MAP_W * MAP_H] SERVAL_EWRAM_BSS;

static void build_cells(u16* cells, const char* const* map) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            cells[y * MAP_W + x] = (u16)key(map_keys, map[y][x]);
}

static void build_meadow(void) {
    u32* out = bg_tiles;
    for (int m = 0; m < MT_COUNT; m++) {
        canvas_begin(16, 16);
        draw(metatile_art[m], 16, 0, 0, meadow_keys, false);
        out = canvas_pack(out);
        for (int k = 0; k < 4; k++)
            metatiles[m].se[k] = MAP_SE(m * 4 + k, 0, 0);
        metatiles[m].collision = MAP_EMPTY;
    }
    build_cells(meadow_cells, meadow_map);
    build_cells(grass_cells, grass_map);
}

const Tileset meadow_tileset = {
    .tiles = bg_tiles, .tile_count = MT_COUNT * 4, .palettes = meadow_colors, .palette_count = 1};

const MapLayer meadow_layer = {
    .width = MAP_W,
    .height = MAP_H,
    .cells = meadow_cells,
    .metatiles = metatiles,
    .metatile_count = MT_COUNT,
    .bg = 3,
    .flags = MAP_LAYER_FIXED,
};

const MapLayer grass_layer = {
    .width = MAP_W,
    .height = MAP_H,
    .cells = grass_cells,
    .metatiles = metatiles,
    .metatile_count = MT_COUNT,
    .bg = 1,
    .flags = MAP_LAYER_FIXED,
};

// --- Flight paths ----------------------------------------------------------------------
//
// Slow and wavering (5/8 of a pixel per frame), each ending in a glide to a
// stop, so a firefly hovers while its script waits between flights. The
// scripts pick one at random and mirror it at random (PATH_MIRROR_X/Y).

#define FLY (FX(5) / 8)
#define GLIDE {.frames = 20, .speed = FLY, .accel = -FLY / 20}

static const PathStep drift_steps[] = {
    {.frames = 48, .speed = FLY, .turn = ANGLE_DEG(1)},
    {.frames = 32, .speed = FLY, .turn = -ANGLE_DEG(2)},
    GLIDE,
};
static const PathStep loop_steps[] = {
    {.frames = 16, .speed = FLY},
    {.frames = 96, .speed = FLY, .turn = 65536 / 128}, // three quarters of a circle
    GLIDE,
};
static const PathStep zigzag_steps[] = {
    {.frames = 16, .speed = FLY, .turn = ANGLE_DEG(4)},
    {.frames = 32, .speed = FLY, .turn = -ANGLE_DEG(4)},
    {.frames = 32, .speed = FLY, .turn = ANGLE_DEG(4)},
    GLIDE,
};

static const Path drift = {PATH_STEPS(drift_steps)};
static const Path loop = {PATH_STEPS(loop_steps), .heading = ANGLE_DEG(270)};
static const Path zigzag = {PATH_STEPS(zigzag_steps), .heading = ANGLE_DEG(330)};

const Path* const paths[PATH_COUNT] = {
    [PATH_DRIFT] = &drift,
    [PATH_LOOP] = &loop,
    [PATH_ZIGZAG] = &zigzag,
};

void art_build(void) {
    build_sprites();
    build_meadow();
}
