// Graphics: every sprite and background tile, drawn at boot (art_build) from
// ASCII pictures into EWRAM buffers that sprite_group_load and tileset_load
// copy to VRAM, as blackjack does. One character per pixel: '.' is
// transparent, letters pick palette colors (the keys are by each palette).
// Round things (the fireflies' halos, the sparks of a catch) are drawn by
// distance from their middle instead, and the backdrop's colors, one per
// screen line, are blended between a few given ones. Also the fireflies'
// flight paths, which the scripts start by index.

#include "game.h"

// --- Palettes ----------------------------------------------------------------

enum { PAL_SERVAL, PAL_GLOW, PALETTE_COUNT };

static const u16 palettes[PALETTE_COUNT][16] = {
    // K outline, O fur, L light fur, S shade, D spots, W white, E eye, N nose,
    // M moonlight on the fur's top edge (added by serval_frame)
    [PAL_SERVAL] = {0, COLOR_RGB(36, 20, 30), COLOR_RGB(214, 146, 66), COLOR_RGB(248, 210, 132),
                    COLOR_RGB(150, 84, 52), COLOR_RGB(66, 34, 34), COLOR_RGB(244, 236, 216),
                    COLOR_RGB(200, 232, 90), COLOR_RGB(224, 120, 130), COLOR_RGB(236, 214, 200)},
    // Fireflies: 1-4 glow, dim to white-hot; 5 the dark body. Sparkles: W
    // white, Y pale yellow, G gold. a b c: a firefly's halo, added to the
    // meadow behind it (SPRITE_BLEND), so dark green brightens it.
    [PAL_GLOW] = {0, COLOR_RGB(70, 120, 50), COLOR_RGB(160, 220, 60), COLOR_RGB(230, 255, 120),
                  COLOR_RGB(255, 255, 230), COLOR_RGB(40, 30, 40), COLOR_RGB(255, 255, 255),
                  COLOR_RGB(255, 240, 140), COLOR_RGB(240, 170, 50), COLOR_RGB(16, 32, 8),
                  COLOR_RGB(32, 64, 16), COLOR_RGB(64, 104, 24)},
};

// Picture characters to colors, per palette.
static const char serval_keys[] = ".KOLSDWENM";
static const char glow_keys[] = ".12345WYGabc";

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

// The moon is high: in each column, the first fur pixel under the outline
// (light fur or a spot included) catches its light.
static void moonlight(void) {
    int m = key(serval_keys, 'M'), k = key(serval_keys, 'K'), w = key(serval_keys, 'W');
    for (int x = 0; x < canvas_w; x++) {
        for (int y = 0; y + 1 < canvas_h; y++) {
            int c = canvas[y * canvas_w + x];
            if (!c)
                continue;
            u8* below = &canvas[(y + 1) * canvas_w + x];
            if (c == k && *below != k && *below != w && *below)
                *below = (u8)m;
            break;
        }
    }
}

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
    moonlight();
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

// The light a firefly casts on the meadow: a 16x16 halo around its 8x8 core,
// drawn with SPRITE_BLEND so that, with main.c's screen_set_blend(0, ...,
// 16, 16), its colors are added to the meadow behind it. Rings of a, b and c
// (dark greens, faintest outside), radius in pixels; halo_frame draws them.
enum { HALO_NONE, HALO_SMALL, HALO_MID, HALO_LARGE, HALO_COUNT };
static const u8 halo_radius[HALO_COUNT] = {0, 4, 6, 8};

// A firefly's frame: its core (a glow_art frame) and the halo behind it,
// centered on the pivot, the middle of its 8x8 body.
#define CORE_PIECE(core) {.sprite = SPR_GLOW_CORE, .frame = (core)}
#define HALO_PIECE(halo) {.sprite = SPR_GLOW_HALO, .frame = (halo), .flags = SPRITE_BLEND}
#define GLOW(core, halo) CORE_PIECE(core), HALO_PIECE(halo)

// The blink: mostly a dim ember, then a flash that lights the grass around
// it (blink_times below).
static const SpritePiece blink_pieces[FIREFLY_FRAMES * 2] = {
    GLOW(GLOW_DARK, HALO_NONE),    GLOW(GLOW_DIM, HALO_SMALL), GLOW(GLOW_MID, HALO_MID),
    GLOW(GLOW_BRIGHT, HALO_LARGE), GLOW(GLOW_MID, HALO_MID),   GLOW(GLOW_DIM, HALO_SMALL),
};
static const u8 blink_times[FIREFLY_FRAMES] = {24, 3, 3, 18, 4, 4};
// The fade: a last flash whose glow spreads into a ring and scatters as its
// light goes out.
#define FADE_FRAMES 5
static const SpritePiece fade_pieces[FADE_FRAMES * 2] = {
    GLOW(GLOW_BRIGHT, HALO_LARGE), GLOW(GLOW_RING, HALO_MID),  GLOW(GLOW_DOTS, HALO_SMALL),
    GLOW(GLOW_SPECK, HALO_NONE),   GLOW(GLOW_NONE, HALO_NONE),
};
static const u8 fade_times[FADE_FRAMES] = {6, 6, 7, 8, 1};

// One halo frame on the 16x16 canvas: rings by distance from its middle,
// measured in half pixels so the middle falls between the four center pixels,
// the outer ring dithered so that the light thins out.
static void halo_frame(int radius) {
    canvas_begin(16, 16);
    int r2 = 4 * radius * radius; // the radius squared, in half pixels
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x - 15, dy = 2 * y - 15, d2 = dx * dx + dy * dy;
            char c = '.';
            if (d2 * 5 < r2) // within 0.45 of the radius
                c = 'c';
            else if (d2 * 2 < r2) // 0.7
                c = 'b';
            else if (d2 * 4 < r2 * 3 || (d2 < r2 && (x + y) % 2 == 0)) // 0.87, then dithered
                c = 'a';
            canvas[y * 16 + x] = (u8)key(glow_keys, c);
        }
    }
}

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

// Around each burst, on a 32x32 frame, eight sparks flying out on a ring and
// thinning: the ring's radius in pixels (0: none) and the sparks' color,
// dithered once they fade.
static const struct {
    u8 radius;
    char color;
    bool dithered;
} sparkle_rings[SPARKLE_FRAMES] = {
    {0, '.', false},  {6, 'W', false},  {9, 'Y', false},
    {11, 'Y', false}, {13, 'G', false}, {14, 'G', true},
};

// Draws frame f's sparks on the 32x32 canvas: the pixels of a ring around its
// middle, one pixel wide, that lie on the eight lines out from the middle
// (across, up and down, and the diagonals). Distances in half pixels, as in
// halo_frame.
static void sparkle_sparks(int f) {
    int inner = 2 * sparkle_rings[f].radius - 1, outer = inner + 2;
    int c = key(glow_keys, sparkle_rings[f].color);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int dx = 2 * x - 31, dy = 2 * y - 31, d2 = dx * dx + dy * dy;
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            int diagonal = ax > ay ? ax - ay : ay - ax;
            bool on_ring = d2 >= inner * inner && d2 < outer * outer;
            bool on_line = ax < 5 || ay < 5 || diagonal < 5;
            if (on_ring && on_line && !(sparkle_rings[f].dithered && (x + y) % 2))
                canvas[y * 32 + x] = (u8)c;
        }
    }
}

// --- Sprites -------------------------------------------------------------------------

#define SERVAL_TILES 16                          // per 32x32 frame
#define T_IDLE 0                                 // 2 frames: standing, blinking
#define T_WALK (T_IDLE + 2 * SERVAL_TILES)       // 4 frames: stride, stand, stride, stand
#define T_SIT (T_WALK + 4 * SERVAL_TILES)        // 2 frames: sitting, blinking
#define T_SPARKLE (T_SIT + 2 * SERVAL_TILES)     // SPARKLE_FRAMES of 16
#define T_CORE (T_SPARKLE + SPARKLE_FRAMES * 16) // GLOW_COUNT of 1
#define T_HALO (T_CORE + GLOW_COUNT)             // HALO_COUNT of 4
#define OBJ_TILES (T_HALO + HALO_COUNT * 4)

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

// The firefly metasprites: two pieces a frame, their pivot 4 pixels right of
// and below the entity's position, the middle of its 8x8 body.
#define FIREFLY_SPRITE(pieces_, frames, times, flags_)                                             \
    {.pieces = (pieces_),                                                                          \
     .piece_count = 2,                                                                             \
     .frame_count = (frames),                                                                      \
     .frame_times = (times),                                                                       \
     .origin_x = -4,                                                                               \
     .origin_y = -4,                                                                               \
     .flags = SPRITE_ASSET_METASPRITE | (flags_)}

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_SERVAL_IDLE] = SERVAL_SPRITE(T_IDLE, 2, idle_times),
    [SPR_SERVAL_WALK] = SERVAL_SPRITE(T_WALK, 4, walk_times),
    [SPR_SERVAL_SIT] = SERVAL_SPRITE(T_SIT, 2, idle_times),
    [SPR_FIREFLY] = FIREFLY_SPRITE(blink_pieces, FIREFLY_FRAMES, blink_times, 0),
    [SPR_FIREFLY_FADE] =
        FIREFLY_SPRITE(fade_pieces, FADE_FRAMES, fade_times, SPRITE_ASSET_ANIM_ONCE),
    // 32x32, its middle where a 16x16 sprite's would be (the script centers
    // a 16x16 burst on the firefly).
    [SPR_SPARKLE] = {.size = SPRITE_32x32,
                     .tiles = obj_tiles + T_SPARKLE * 8,
                     .origin_x = 8,
                     .origin_y = 8,
                     .frame_count = SPARKLE_FRAMES,
                     .frame_times = sparkle_times,
                     .palette_slot = PAL_GLOW,
                     .flags = SPRITE_ASSET_ANIM_ONCE},
    [SPR_GLOW_CORE] = {.size = SPRITE_8x8,
                       .tiles = obj_tiles + T_CORE * 8,
                       .frame_count = GLOW_COUNT,
                       .palette_slot = PAL_GLOW},
    [SPR_GLOW_HALO] = {.size = SPRITE_16x16,
                       .tiles = obj_tiles + T_HALO * 8,
                       .frame_count = HALO_COUNT,
                       .palette_slot = PAL_GLOW},
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_SERVAL_IDLE] = &sprites[SPR_SERVAL_IDLE],   [SPR_SERVAL_WALK] = &sprites[SPR_SERVAL_WALK],
    [SPR_SERVAL_SIT] = &sprites[SPR_SERVAL_SIT],     [SPR_FIREFLY] = &sprites[SPR_FIREFLY],
    [SPR_FIREFLY_FADE] = &sprites[SPR_FIREFLY_FADE], [SPR_SPARKLE] = &sprites[SPR_SPARKLE],
    [SPR_GLOW_CORE] = &sprites[SPR_GLOW_CORE],       [SPR_GLOW_HALO] = &sprites[SPR_GLOW_HALO],
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
    // T_SPARKLE
    for (int f = 0; f < SPARKLE_FRAMES; f++) {
        canvas_begin(32, 32);
        if (sparkle_rings[f].radius)
            sparkle_sparks(f);
        draw(sparkle_art[f], 16, 8, 8, glow_keys, false);
        out = canvas_pack(out);
    }
    // T_CORE, T_HALO: the pieces of the firefly metasprites
    for (int g = 0; g < GLOW_COUNT; g++) {
        canvas_begin(8, 8);
        draw(glow_art[g], 8, 0, 0, glow_keys, false);
        out = canvas_pack(out);
    }
    for (int h = 0; h < HALO_COUNT; h++) {
        halo_frame(halo_radius[h]);
        out = canvas_pack(out);
    }
}

// --- The meadow ----------------------------------------------------------------------
//
// Two map layers of 16x16 metatiles, one screen each (the camera never
// moves), over a backdrop that changes color line by line (dusk_lines, which
// main.c gives raster_backdrop): night blue at the top of the sky, a violet
// and rose glow on the horizon, and the meadow, hazy where it meets the hills
// and darker towards the front. Background 3: stars, a crescent moon, far
// hills lit by the glow, a dark treeline with an acacia standing out of it,
// and on the meadow tufts of grass, flowers and stones, small and pale far
// off, bigger near. Background 1, in front of the sprites: tall grass along
// the bottom, moonlit at the tips, which the serval's legs disappear into.

// Where the backdrop's colors are given (8 bits a channel); the lines between
// are blended from the two around them.
static const struct {
    u8 y, r, g, b;
} dusk_stops[] = {
    {0, 8, 6, 28},      {8, 18, 14, 52},     {16, 54, 30, 96},
    {24, 140, 70, 128}, {31, 196, 104, 140}, // the sky, down to the horizon behind the treeline
    {32, 92, 64, 124},  {48, 66, 48, 112},   {88, 46, 38, 100},
    {128, 32, 28, 82},  {159, 20, 18, 56}, // the meadow
};

Color dusk_lines[SCREEN_H] SERVAL_EWRAM_BSS;

// Each line's color, blended between the stops above and below it.
#define DUSK_STOPS (int)(sizeof dusk_stops / sizeof dusk_stops[0])

static void build_dusk(void) {
    int s = 0; // the stop at or above line y; s + 1 is below it
    for (int y = 0; y < SCREEN_H; y++) {
        while (s + 2 < DUSK_STOPS && dusk_stops[s + 1].y <= y)
            s++;
        int t = y - dusk_stops[s].y, span = dusk_stops[s + 1].y - dusk_stops[s].y;
#define BLEND(c) (dusk_stops[s].c + (dusk_stops[s + 1].c - dusk_stops[s].c) * t / span)
        dusk_lines[y] = COLOR_RGB(BLEND(r), BLEND(g), BLEND(b));
#undef BLEND
    }
}

// Background colors: s S stars (dim, bright), m n moon (lit, shade), o its
// halo, w c far hills (rim lit by the glow, shade), k treeline, t u meadow
// plants (shade, moonlit), f p flowers (white, pink), g h i tall grass (dark
// to moonlit). Where nothing is drawn, the backdrop shows.
static const char meadow_keys[] = ".sSmnowckutfpghi";
static const u16 meadow_colors[16] = {
    0,
    COLOR_RGB(120, 116, 178),
    COLOR_RGB(255, 250, 224),
    COLOR_RGB(250, 242, 204),
    COLOR_RGB(178, 162, 176),
    COLOR_RGB(56, 46, 108),
    COLOR_RGB(200, 116, 152),
    COLOR_RGB(62, 38, 100),
    COLOR_RGB(14, 10, 30),
    COLOR_RGB(102, 92, 170),
    COLOR_RGB(30, 24, 66),
    COLOR_RGB(228, 218, 248),
    COLOR_RGB(220, 132, 180),
    COLOR_RGB(10, 12, 32),
    COLOR_RGB(36, 56, 92),
    COLOR_RGB(104, 144, 172),
};

enum {
    MT_EMPTY,
    MT_STARS1,
    MT_STARS2,
    MT_MOON,
    MT_TREES1,
    MT_TREES2,
    MT_TREES3,
    MT_ACACIA_TL, // the acacia, 2x2 metatiles: its crown
    MT_ACACIA_TR,
    MT_ACACIA_BL, // its trunk, in the treeline
    MT_ACACIA_BR,
    MT_TUFT_FAR,
    MT_TUFT_MID,
    MT_TUFT_NEAR,
    MT_FLOWERS,
    MT_FLOWERS_PINK,
    MT_STONE,
    MT_GRASS1,
    MT_GRASS2,
    MT_GRASS3,
    MT_BLADES,
    MT_SEEDS,
    MT_COUNT
};

#define BLANK "................"
#define BLANK4 BLANK, BLANK, BLANK, BLANK
#define SOLID_TREES "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk", "kkkkkkkkkkkkkkkk"
#define GRASS_BASE "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg"

static const char* const metatile_art[MT_COUNT][16] = {
    [MT_EMPTY] = {BLANK4, BLANK4, BLANK4, BLANK4},
    [MT_STARS1] = {BLANK, BLANK, "...s............", BLANK, "...........s....", "..........sSs...",
                   "...........s....", BLANK, BLANK, ".....s..........", BLANK, BLANK,
                   ".............s..", BLANK, BLANK, BLANK},
    [MT_STARS2] = {BLANK, "........s.......", BLANK, BLANK, "..s.............", ".sSs............",
                   "..s.........s...", BLANK, BLANK, "............S...", BLANK, "......s.........",
                   BLANK, BLANK, BLANK, BLANK},
    [MT_MOON] = {"................", "...........o....", "..........n.o...", "..........nm.o..",
                 "..........nmm.o.", "...........nmm..", "...........nmmo.", "..........nmmm.o",
                 "..........nmmmo.", ".........nmmmm..", "........nnmmmmo.", ".o.nnnnnmmmmmo..",
                 "..o.mmmmmmmmo...", "...o.mmmmmmo....", "....o.o.o.o.....", ".......o........"},
    // The treeline: hills lit from behind by the glow, bushes and trees in
    // front. Every one meets the next at the same heights, in any order.
    [MT_TREES1] = {"................", "...s............", ".............s..", "................",
                   ".......www......", "....wwwcccww....", "..wwccccccccww..", "wwccccccccccccww",
                   "cccccccccccckccc", "cccckkccccckkkcc", "cckkkkkccckkkkkc", "kkkkkkkkkkkkkkkk",
                   SOLID_TREES},
    [MT_TREES2] = {"................", "................", "................", ".........s......",
                   "................", "................", "................", "wwwwww.....wwwww",
                   "ccccccwwkkwccccc", "cccccckkkkkccccc", "ckkcckkkkkkkcccc", "kkkkkkkkkkkkkkkk",
                   SOLID_TREES},
    [MT_TREES3] = {"............S...", "................", ".s..............", "................",
                   "................", "..........ww....", "..wwwwwwwwccwww.", "wwcccccccccccccw",
                   "cccccccccccccccc", "cccccccckkcccccc", "ccckkcckkkkkckkc", "kkkkkkkkkkkkkkkk",
                   SOLID_TREES},
    [MT_ACACIA_TL] = {BLANK4, BLANK4, BLANK4, BLANK, ".s..............", "..........k..kkk",
                      "......kkkkkkkkkk"},
    [MT_ACACIA_TR] = {BLANK4, BLANK, "......s.........", BLANK, BLANK, BLANK4, BLANK, BLANK,
                      "k...k...........", "kkkkkkkk.kk....."},
    [MT_ACACIA_BL] = {"...kkkkkkkkkkkkk", "..kkkkkkkkkkkkkk", "....kkkkkkkkkkkk",
                      ".......kk.kkk..k", "...........k....", "............k...",
                      "....wwwwwwwwwkww", "wwwwcccccccccckc", "ccccccccccccccck",
                      "ccccccccccccccck", "cckkccckkkkkkkkk", "kkkkkkkkkkkkkkkk", SOLID_TREES},
    [MT_ACACIA_BR] = {"kkkkkkkkkkkkkk..", "kkkkkkkkkkkkkkk.", "kkkkkkkkkkkkk...",
                      "kkkk..kkkk.k....", "k.k...k.........", "k.k..k..........",
                      "kkwwkwwwwwww....", "kkckccccccccwwww", "kkkccccccccccccc",
                      "kkcccccccccccccc", "kkkkkkcckkcckccc", "kkkkkkkkkkkkkkkk", SOLID_TREES},
    // The meadow: far plants are small, near ones big.
    [MT_TUFT_FAR] = {BLANK4, BLANK, BLANK, "......u.u.......", ".....tutut......", BLANK, BLANK,
                     BLANK, BLANK, BLANK4},
    [MT_TUFT_MID] = {BLANK4, BLANK, "......u...u.....", ".....tu.ut.u....", ".....tutuu.t....",
                     "......ttuttt....", BLANK, BLANK, BLANK, BLANK4},
    [MT_TUFT_NEAR] = {BLANK, BLANK, BLANK, BLANK, BLANK, "......u.........", ".....ui...u.....",
                      "..u..ut..ui..u..", "..ut.uiu.ut.ui..", "...ututiuuttu...",
                      "...tutttututt...", "....tttttttt....", BLANK, BLANK, BLANK, BLANK},
    [MT_FLOWERS] = {BLANK, BLANK, "...f............", "..fuf...........", "...t.....f......",
                    "...t....fuf.....", "..tt.....t......", ".........t......", "........tt......",
                    BLANK, BLANK, BLANK, BLANK4},
    [MT_FLOWERS_PINK] = {BLANK, BLANK, BLANK, BLANK, ".........p......", "..p.....ppp.....",
                         ".ppp.....p......", "..p......t..p...", "..t.....tt.ppp..",
                         "..tt....t...p...", "...t...tt...t...", "...ttttt...tt...", BLANK4},
    [MT_STONE] = {BLANK4, BLANK, BLANK, BLANK, ".....uuu........", "...uuuuuuu......",
                  "..utuuuuttt.....", "..tttttttttt.u..", "...tttttttt.tut.", BLANK, BLANK, BLANK,
                  BLANK},
    // The tall grass in front: dark at the roots, moonlit at the tips.
    [MT_GRASS1] = {"..i.............", "..h.......i.....", ".ih.......h.....", ".hh..i...hh..i..",
                   ".hg..h...hg.ih..", "hgg.hh..hgg.hg..", "hgg.hgh.hggihgh.", "ggghggh.gggggggh",
                   "gggggggghggggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg",
                   GRASS_BASE},
    [MT_GRASS2] = {".......i........", ".......h.....i..", "...i...hi....h..", "...h..hhh...ih..",
                   "..hh..hgh...hg..", "i.hg.hhgg..hgg.i", "h.hghhggg..hgg.h", "hhgghggggihggghh",
                   "gggggggggghggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg",
                   GRASS_BASE},
    [MT_GRASS3] = {"....i......f....", "....h.....fif...", "i...hi.....h....", "h..hh.i....h.i..",
                   "hh.hg.h...hh.h..", "hg.hg.hh.ihg.hg.", "gg.gghhg.hgg.hgh", "gghggggghgggghgg",
                   "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg", "gggggggggggggggg",
                   GRASS_BASE},
    // More of the meadow: a few lone blades, and seed heads on tall stalks.
    [MT_BLADES] = {BLANK, BLANK, BLANK, "..u.............", "..t.......u.....", "..........t.....",
                   BLANK, BLANK, BLANK, ".....u..........", "....tu......u...", "............t...",
                   BLANK4},
    [MT_SEEDS] = {BLANK, BLANK, "....f.....f.....", "....u....fuf....", "...fu.....u.....",
                  "....u.....u..f..", "....u....u...u..", "...u.....u...u..", "...u....u...u...",
                  "..tut..tut.tut..", "..ttt..ttt.ttt..", BLANK, BLANK, BLANK, BLANK, BLANK},
};

static u32 bg_tiles[MT_COUNT * 4 * 8] SERVAL_EWRAM_BSS;
static Metatile metatiles[MT_COUNT] SERVAL_EWRAM_BSS;

// The two layers, one character per metatile.
#define MAP_W (SCREEN_W / 16)
#define MAP_H (SCREEN_H / 16)
static const char* const meadow_map[MAP_H] = {
    " * +  *M  +()  ", // the sky (the HUD's top line is over it)
    "ABCABCABCAB[]CA", // the treeline
    "  v  b v   v bv", // the far meadow: small plants
    " v  f  b  v  p ", //
    " b  t   b  f t ", //
    " t     p  b  r ", //
    "   b  s  t   b ", //
    "  T  f    b  T ", //
    "Tb    r s p   b", // the near meadow: big plants
    "               ", // behind the tall grass
};
static const char* const grass_map[MAP_H] = {
    "               ", "               ", "               ", "               ", "               ",
    "               ", "               ", "               ", "               ", "GHIGHGIHGHGIHGH",
};
static const char map_keys[MT_COUNT + 1] = " *+MABC()[]vtTfprGHIbs";

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
    build_dusk();
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
