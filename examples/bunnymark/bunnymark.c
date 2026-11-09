// bunnymark's game: the bunny sprites, their physics and the HUD, and the
// interactive demo's scenery. See main.c for what the example demonstrates
// and what to expect when it runs.

#include "bunnymark.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_BUNNY_WHITE, SPR_BUNNY_GOLD, SPR_BUNNY_BLUE, SPR_BUNNY_GREEN, SPRITE_COUNT };

#define BUNNY_SIZE 16

// A 16x16 bunny seen from the side, facing right, 4 bits per pixel, as four
// 8x8 tiles (top-left, top-right, bottom-left, bottom-right). Generated from
// this art:
//
//   ........KK.KK...    . transparent (color 0)
//   .......KSKKHPK..    K outline     (color 1)
//   .......KSKKHPK..    H fur, lit    (color 2)
//   .......KSKKHPK..    F fur         (color 3)
//   ........KSKHPK..    S fur, shaded (color 4)
//   ........KKHHK...    P inner ear   (color 5)
//   .......KHHHHHK..    E eye         (color 6)
//   ....KKKHHFFWEHK.    W tail, glint (color 7)
//   ..KKHHHHFFFEEFK.    N nose        (color 8)
//   .KWKHHFFFFFFFFNK
//   KWWWKFFFFFFFFFK.    Light comes from the top: the back and the head are
//   KWWWFFFSSSFFFSK.    lit, the belly and the far ear are in shade, and a
//   .KWWFFSFFFSFSSK.    shaded curve marks the haunch.
//   ..KSFFSFFFSSSK..
//   ..KSSKKSSSKSK...
//   ...KK..KKK.K....
static const u32 bunny_tiles[32] = {
    0x00000000, 0x10000000, 0x10000000, 0x10000000, 0x00000000, 0x00000000, 0x10000000, 0x21110000,
    0x00011011, 0x00152114, 0x00152114, 0x00152114, 0x00152141, 0x00012211, 0x00122222, 0x01267332,
    0x22221100, 0x33221710, 0x33317771, 0x43337771, 0x34337710, 0x34334100, 0x41144100, 0x10011000,
    0x01366333, 0x18333333, 0x01333333, 0x01433344, 0x01443433, 0x00144433, 0x00014144, 0x00001011,
};

// One palette per bunny color: the three fur shades differ, the outline, ears,
// eye, tail and nose are the same.
#define BUNNY_PALETTE(lit, fur, shade)                                                             \
    {0,                                                                                            \
     COLOR_RGB(34, 22, 48),                                                                        \
     lit,                                                                                          \
     fur,                                                                                          \
     shade,                                                                                        \
     COLOR_RGB(250, 150, 176),                                                                     \
     COLOR_RGB(34, 22, 48),                                                                        \
     COLOR_RGB(255, 250, 240),                                                                     \
     COLOR_RGB(240, 110, 150)}

static const u16 bunny_palettes[SPRITE_COUNT][16] = {
    [SPR_BUNNY_WHITE] =
        BUNNY_PALETTE(COLOR_RGB(255, 255, 255), COLOR_RGB(228, 222, 240), COLOR_RGB(168, 160, 196)),
    [SPR_BUNNY_GOLD] =
        BUNNY_PALETTE(COLOR_RGB(255, 236, 150), COLOR_RGB(244, 186, 72), COLOR_RGB(190, 122, 40)),
    [SPR_BUNNY_BLUE] =
        BUNNY_PALETTE(COLOR_RGB(196, 230, 255), COLOR_RGB(120, 176, 240), COLOR_RGB(70, 112, 196)),
    [SPR_BUNNY_GREEN] =
        BUNNY_PALETTE(COLOR_RGB(200, 250, 196), COLOR_RGB(120, 214, 140), COLOR_RGB(60, 150, 100)),
};

#define BUNNY_SPRITE(color) {.size = SPRITE_16x16, .tiles = bunny_tiles, .palette_slot = (color)}

static const SpriteAsset bunny_sprites[SPRITE_COUNT] = {
    [SPR_BUNNY_WHITE] = BUNNY_SPRITE(SPR_BUNNY_WHITE),
    [SPR_BUNNY_GOLD] = BUNNY_SPRITE(SPR_BUNNY_GOLD),
    [SPR_BUNNY_BLUE] = BUNNY_SPRITE(SPR_BUNNY_BLUE),
    [SPR_BUNNY_GREEN] = BUNNY_SPRITE(SPR_BUNNY_GREEN),
};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_BUNNY_WHITE] = &bunny_sprites[SPR_BUNNY_WHITE],
    [SPR_BUNNY_GOLD] = &bunny_sprites[SPR_BUNNY_GOLD],
    [SPR_BUNNY_BLUE] = &bunny_sprites[SPR_BUNNY_BLUE],
    [SPR_BUNNY_GREEN] = &bunny_sprites[SPR_BUNNY_GREEN],
};

// Every sprite in the table (no sprite_ids), one palette each.
static const SpriteGroup bunny_group = {
    .palettes = &bunny_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = SPRITE_COUNT,
};

// --- Scenery -----------------------------------------------------------------

// The backdrop is one picture the size of the screen on background 3, behind
// the bunnies: a night sky with a moon and stars over two rows of hills and a
// strip of grass along the bottom, under the HUD's panel. Every 8x8 cell of
// the screen has a tile of its own (600 tiles), drawn at boot into
// scene_tiles, and the map is a grid of metatiles that shows each one in its
// place. Only the interactive demo shows it (main.c): a map layer costs the
// engine a little time every frame, which the benchmark leaves out.

#define HUD_HEIGHT 24 // the panel: three text rows; bunnies stay below them

#define HORIZON 128 // where the sky's gradient ends, behind the hills
#define GROUND 150  // the top of the grass, give or take its tufts

// The CPU meter: a bar behind the HUD's second line (the CPU share), as long
// as the share of the frame the game used, the whole width being 100%.
#define METER_ROW 1 // in text rows (8 pixels)
#define METER_TOP 9
#define METER_BOTTOM 14

// Background palette 0: the scene. (Its color 0 is the backdrop, which no
// tile shows: every pixel of the picture is opaque.)
enum {
    C_SKY = 1, // five colors, darkest at the top: C_SKY to C_SKY + 4
    C_STAR_A = 6,
    C_STAR_B, // the two twinkle in turn
    C_MOON,
    C_MOON_SHADE,
    C_FAR_HILLS,
    C_NEAR_HILLS,
    C_GRASS_DARK,
    C_GRASS,
    C_GRASS_LIT,
    C_FLOWER,
};
// Background palette 1: the HUD's panel.
enum { C_PANEL = 1, C_PANEL_LIT, C_TRACK, C_TICK, C_EDGE, C_METER, C_METER_LIT };
#define PAL_SCENE 0
#define PAL_HUD 1

#define STAR_DIM COLOR_RGB(110, 100, 170)
#define STAR_BRIGHT COLOR_RGB(255, 244, 210)

static const u16 scene_palettes[2][16] = {
    [PAL_SCENE] = {0, COLOR_RGB(14, 12, 36), COLOR_RGB(22, 18, 52), COLOR_RGB(34, 26, 70),
                   COLOR_RGB(50, 34, 88), COLOR_RGB(72, 46, 106), STAR_BRIGHT, STAR_DIM,
                   COLOR_RGB(255, 244, 214), COLOR_RGB(214, 196, 168), COLOR_RGB(40, 30, 78),
                   COLOR_RGB(24, 24, 54), COLOR_RGB(18, 44, 46), COLOR_RGB(30, 74, 60),
                   COLOR_RGB(64, 124, 80), COLOR_RGB(236, 140, 176)},
    [PAL_HUD] = {0, COLOR_RGB(12, 10, 28), COLOR_RGB(22, 18, 46), COLOR_RGB(8, 6, 20),
                 COLOR_RGB(44, 38, 84), COLOR_RGB(120, 104, 200), COLOR_RGB(30, 96, 100),
                 COLOR_RGB(60, 150, 140)},
};

// The meter's colors, by how busy the frame is: calm, busy (past half the
// frame) and nearly out of time (past 85%): the fill, then its lit top row.
static const Color meter_colors[3][2] = {
    {COLOR_RGB(30, 96, 100), COLOR_RGB(60, 150, 140)},
    {COLOR_RGB(120, 84, 24), COLOR_RGB(200, 150, 50)},
    {COLOR_RGB(130, 30, 40), COLOR_RGB(220, 70, 70)},
};

#define SCENE_COLS (SCREEN_W / 8)
#define SCENE_ROWS (SCREEN_H / 8)
#define SCENE_TILES (1 + SCENE_COLS * SCENE_ROWS) // tile 0 is blank, unused
#define SCENE_MAP_W (SCREEN_W / 16)
#define SCENE_MAP_H (SCREEN_H / 16)

static u32 scene_tiles[SCENE_TILES * 8] SERVAL_EWRAM_BSS;
static Metatile scene_metatiles[SCENE_MAP_W * SCENE_MAP_H] SERVAL_EWRAM_BSS;
static u16 scene_cells[SCENE_MAP_W * SCENE_MAP_H] SERVAL_EWRAM_BSS;

// The tile of cell (col, row), and a pixel's place in it: 4 bits per pixel,
// the leftmost in the low bits of each row's word.
static u32* scene_tile(int col, int row) {
    return &scene_tiles[(1 + row * SCENE_COLS + col) * 8];
}

static void plot(int x, int y, int color) {
    u32* bits = &scene_tile(x >> 3, y >> 3)[y & 7];
    u32 shift = (u32)(x & 7) * 4;
    *bits = (*bits & ~(15u << shift)) | (u32)color << shift;
}

// A 4x4 ordered-dither matrix: thresholds 0-15 that spread a mix of two
// colors evenly over the pixels.
static const u8 dither[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Scattered but repeatable noise, for the stars and the tufts of grass.
static u32 hash(u32 x, u32 y) {
    u32 h = x * 73856093u ^ y * 19349663u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return h ^ (h >> 15);
}

// The sky's gradient: five bands from the HUD down to the horizon, each
// dithered into the next over its lower half.
static int sky_color(int x, int y) {
    int t = int_min(y - HUD_HEIGHT, HORIZON - HUD_HEIGHT - 1) * 5 * 16 / (HORIZON - HUD_HEIGHT);
    int band = t / 16, mix = (t % 16 - 8) * 2;
    if (mix > dither[y & 3][x & 3])
        band++;
    return C_SKY + int_min(band, 4);
}

static void draw_sky(void) {
    // Each line of the gradient repeats every 4 pixels: one word fills the
    // line across all 30 tiles.
    for (int y = HUD_HEIGHT; y < SCREEN_H; y++) {
        u32 bits = 0;
        for (int x = 7; x >= 0; x--)
            bits = bits << 4 | (u32)sky_color(x, y);
        for (int col = 0; col < SCENE_COLS; col++)
            scene_tile(col, y >> 3)[y & 7] = bits;
    }
}

#define MOON_X 192
#define MOON_Y 56
#define MOON_R 11
#define GLOW_R (MOON_R + 9)

static void draw_moon(void) {
    static const s8 craters[3][3] = {{-4, 2, 3}, {3, 5, 2}, {1, -5, 1}}; // x, y, radius
    for (int dy = -GLOW_R; dy <= GLOW_R; dy++) {
        for (int dx = -GLOW_R; dx <= GLOW_R; dx++) {
            int x = MOON_X + dx, y = MOON_Y + dy;
            int d2 = dx * dx + dy * dy;
            if (d2 > GLOW_R * GLOW_R)
                continue;
            if (d2 > MOON_R * MOON_R) {
                // The glow: the next lighter sky color, thinner with distance.
                int glow = (GLOW_R * GLOW_R - d2) * 16 / (GLOW_R * GLOW_R - MOON_R * MOON_R);
                if (glow > dither[y & 3][x & 3])
                    plot(x, y, int_min(sky_color(x, y) + 1, C_SKY + 4));
                continue;
            }
            // The moon, with a shaded rim on its lower left, and craters.
            int lx = dx - 3, ly = dy + 3; // from the center of the lit part
            int color = lx * lx + ly * ly > MOON_R * MOON_R ? C_MOON_SHADE : C_MOON;
            for (int i = 0; i < 3; i++) {
                int cx = dx - craters[i][0], cy = dy - craters[i][1];
                if (cx * cx + cy * cy <= craters[i][2] * craters[i][2])
                    color = C_MOON_SHADE;
            }
            plot(x, y, color);
        }
    }
}

#define STAR_COUNT 90

// Bright stars, each with a small cross of light around it.
static const u8 big_stars[][2] = {{28, 44}, {92, 70}, {146, 36}, {226, 100}, {54, 98}};

static void draw_stars(void) {
    // Scattered over the sky, clear of the moon's glow. (Hills drawn later
    // cover the ones that fall behind them.)
    for (u32 i = 0; i < STAR_COUNT; i++) {
        int x = (int)(hash(i, 1) % SCREEN_W);
        int y = HUD_HEIGHT + 2 + (int)(hash(i, 2) % (HORIZON - HUD_HEIGHT - 2));
        int dx = x - MOON_X, dy = y - MOON_Y;
        if (dx * dx + dy * dy > (GLOW_R + 2) * (GLOW_R + 2))
            plot(x, y, i & 1 ? C_STAR_B : C_STAR_A);
    }
    for (u32 i = 0; i < sizeof big_stars / sizeof big_stars[0]; i++) {
        int x = big_stars[i][0], y = big_stars[i][1];
        plot(x, y, i & 1 ? C_STAR_B : C_STAR_A);
        static const s8 arms[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int a = 0; a < 4; a++) {
            int ax = x + arms[a][0], ay = y + arms[a][1];
            plot(ax, ay, int_min(sky_color(ax, ay) + 2, C_SKY + 4));
        }
    }
}

// Two rows of hills (sums of two sine waves), and the grass in front of them.
static void draw_land(void) {
    for (int x = 0; x < SCREEN_W; x++) {
        int far = 112 + (7 * fx_sin((u16)(x * 290 + 9000)) + 4 * fx_sin((u16)(x * 830))) / FX_ONE;
        int near = 132 + (6 * fx_sin((u16)(x * 410 + 30000)) + 3 * fx_sin((u16)(x * 1300 + 5000))) /
                             FX_ONE;
        int grass = GROUND - (int)(hash((u32)x, 3) % 3) - (hash((u32)x, 5) % 6 ? 0 : 2);
        for (int y = far; y < near; y++)
            plot(x, y, C_FAR_HILLS);
        for (int y = near; y < grass; y++)
            plot(x, y, C_NEAR_HILLS);
        // The tips of the grass, and a flower on some of the taller tufts.
        bool flower = grass < GROUND - 1 && hash((u32)x, 7) % 5 == 0;
        plot(x, grass, flower ? C_FLOWER : C_GRASS_LIT);
        for (int y = grass + 1; y < SCREEN_H; y++) {
            // The ground darkens toward the bottom of the screen.
            bool dark = y >= GROUND + 3 && (y - GROUND) * 2 > dither[y & 3][x & 3];
            plot(x, y, dark ? C_GRASS_DARK : C_GRASS);
        }
    }
}

// The color of the HUD's panel at (x, y), with the CPU meter empty.
static int hud_color(int x, int y) {
    if (y == HUD_HEIGHT - 1)
        return C_EDGE;
    if (y >= METER_TOP && y <= METER_BOTTOM) {
        // A tick at every quarter of the frame.
        bool tick = x == SCREEN_W / 4 - 1 || x == SCREEN_W / 2 - 1 || x == SCREEN_W * 3 / 4 - 1;
        return tick && y >= METER_BOTTOM - 1 ? C_TICK : C_TRACK;
    }
    return y == 0 ? C_PANEL_LIT : C_PANEL;
}

// The meter's tile in column col, its first `filled` pixels (0-8) filled:
// each line of the bar is the empty line (as drawn at boot, kept in
// meter_empty) with the filled pixels laid over it. Returns the tile.
static u32 meter_empty[SCENE_COLS][8] SERVAL_EWRAM_BSS;

static const u32* draw_meter_tile(int col, int filled) {
    u32* tile = scene_tile(col, METER_ROW);
    u32 mask = filled == 8 ? 0xFFFFFFFFu : (1u << (filled * 4)) - 1;
    for (int y = METER_TOP; y <= METER_BOTTOM; y++) {
        u32 fill = (y == METER_TOP ? C_METER_LIT : C_METER) * 0x11111111u;
        u32 empty = meter_empty[col][y & 7];
        tile[y & 7] = (fill & mask) | (empty & ~mask);
    }
    return tile;
}

void bunnymark_scenery_load(void) {
    for (int y = 0; y < HUD_HEIGHT; y++) {
        for (int col = 0; col < SCENE_COLS; col++) {
            u32 bits = 0;
            for (int x = 7; x >= 0; x--)
                bits = bits << 4 | (u32)hud_color(col * 8 + x, y);
            scene_tile(col, y >> 3)[y & 7] = bits;
            if (y >> 3 == METER_ROW)
                meter_empty[col][y & 7] = bits;
        }
    }
    draw_sky();
    draw_moon();
    draw_stars();
    draw_land();

    // Each metatile shows its four tiles, the HUD's in the HUD's palette.
    for (int my = 0; my < SCENE_MAP_H; my++) {
        for (int mx = 0; mx < SCENE_MAP_W; mx++) {
            Metatile* m = &scene_metatiles[my * SCENE_MAP_W + mx];
            for (int k = 0; k < 4; k++) {
                int col = mx * 2 + (k & 1), row = my * 2 + (k >> 1);
                int palette = row * 8 < HUD_HEIGHT ? PAL_HUD : PAL_SCENE;
                m->se[k] = MAP_SE(1 + row * SCENE_COLS + col, palette, 0);
            }
            scene_cells[my * SCENE_MAP_W + mx] = (u16)(my * SCENE_MAP_W + mx);
        }
    }

    static const Tileset tileset = {
        .tiles = scene_tiles,
        .tile_count = SCENE_TILES,
        .palettes = &scene_palettes[0][0],
        .palette_count = 2,
    };
    static const MapLayer layer = {
        .width = SCENE_MAP_W,
        .height = SCENE_MAP_H,
        .cells = scene_cells,
        .metatiles = scene_metatiles,
        .metatile_count = SCENE_MAP_W * SCENE_MAP_H,
        .bg = 3,
        .flags = MAP_LAYER_FIXED,
    };
    tileset_load(&tileset);
    map_load(&layer);
    screen_set_backdrop(scene_palettes[PAL_SCENE][C_SKY]);

    // Light text with a dark drop shadow, readable over the meter.
    text_set_color(COLOR_RGB(236, 232, 255), COLOR_RGB(6, 4, 16));
    text_set_shadow(true);
}

void bunnymark_scenery_update(void) {
    // The CPU meter. Its length (in pixels) eases toward the CPU share a
    // quarter of the way each frame, so it glides, and it stays still when
    // the share only wavers a little. Then the tiles from its old end to its
    // new one are redrawn: a step of at most 48 pixels touches at most 7
    // tiles, within the engine's MAP_MAX_TILE_UPDATES a frame.
    static int fill;
    static u32 scale; // meter pixels per CPU cycle, in 65536ths
    if (scale == 0)
        scale = ((u32)SCREEN_W << 16) / frame_budget_cycles(); // the one division
    int target = int_min((int)(frame_cpu_cycles() * scale >> 16), SCREEN_W);
    int step = int_clamp((target - fill) / 4, -48, 48);
    if (step != 0) {
        int from = int_min(fill, fill + step) / 8;
        int to = int_min(int_max(fill, fill + step) / 8, SCENE_COLS - 1);
        fill += step;
        for (int col = from; col <= to; col++)
            tileset_set_tiles((u16)(1 + METER_ROW * SCENE_COLS + col),
                              draw_meter_tile(col, int_clamp(fill - col * 8, 0, 8)), 1);
    }

    // Its color: calm, busy or nearly out of time.
    static int zone = 0;
    int now = target < SCREEN_W / 2 ? 0 : target < SCREEN_W * 85 / 100 ? 1 : 2;
    if (now != zone) {
        zone = now;
        tileset_set_colors(PAL_HUD * 16 + C_METER, meter_colors[zone], 2);
    }

    // The stars twinkle: the two star colors take turns to brighten.
    u32 t = frame_count();
    if (t % 4 == 0) {
        u32 phase = t / 4 % 32; // a triangle wave 0-16-0 over 128 frames
        u32 amount = (phase < 16 ? phase : 32 - phase) * 16;
        Color stars[2] = {color_mix(STAR_DIM, STAR_BRIGHT, 256 - amount),
                          color_mix(STAR_DIM, STAR_BRIGHT, amount)};
        tileset_set_colors(PAL_SCENE * 16 + C_STAR_A, stars, 2);
    }
}

// --- Game --------------------------------------------------------------------

// Game-defined component: marks entities the bunny_animate system handles.
#define C_BUNNY C_GAME(0)

// Like raylib's bunnymark: up to 250 pixels per second each way, at 60 fps.
#define MAX_SPEED (FX(250) / 60)

// Gravity: a quarter pixel per frame per frame, in the chosen direction.
#define GRAVITY (FX_ONE / 4)

// Physics for every bunny: floor bounces keep 7/8 of the speed, and sliding
// along a floor loses 1/8 of it each frame.
#define BUNNY_BOUNCE 224
#define BUNNY_FRICTION 32

static Entity bunnies[MAX_ENT];
static int count;

static int gravity_x = 0, gravity_y = 1; // direction, for the HUD

void bunnymark_init(void) {
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&bunny_group);
    screen_set_backdrop(COLOR_RGB(24, 24, 40));
    text_clear(); // sets up the text layer now, not during the first frame

    physics_set_bounds(0, HUD_HEIGHT, SCREEN_W, SCREEN_H);
    gravity_set(0, 1); // down
}

void bunny_add(void) {
    if (count == MAX_ENT)
        return;
    Entity e = entity_create(C_POS | C_VEL | C_SPR | C_BODY | C_BUNNY);
    if (e == ENTITY_NONE)
        return;
    u32 i = entity_index(e);
    pos_x[i] = FX((SCREEN_W - BUNNY_SIZE) / 2);
    pos_y[i] = FX(HUD_HEIGHT);
    vel_x[i] = random_range(-MAX_SPEED, MAX_SPEED);
    vel_y[i] = random_range(-MAX_SPEED, MAX_SPEED);
    spr_id[i] = (u16)random_range(0, SPRITE_COUNT - 1);
    body_w[i] = body_h[i] = BUNNY_SIZE;
    body_bounce[i] = BUNNY_BOUNCE;
    body_friction[i] = BUNNY_FRICTION;
    bunnies[count++] = e;
}

void bunny_remove(void) {
    if (count > 0)
        entity_destroy(bunnies[--count]);
}

int bunny_count(void) {
    return count;
}

// Bunnies that came to rest on the floor, or are sliding along it, would stay
// there once gravity is off: this pushes them off in a random direction, so
// every bunny floats.
static void kick_resting_bunnies(void) {
    ECS_FOR_EACH(i, C_BUNNY) {
        if (vel_x[i] == 0 || vel_y[i] == 0) {
            vel_x[i] = random_range(-MAX_SPEED, MAX_SPEED);
            vel_y[i] = random_range(-MAX_SPEED, MAX_SPEED);
        }
    }
}

void gravity_set(int x, int y) {
    gravity_x = x;
    gravity_y = y;
    physics_set_gravity(x * GRAVITY, y * GRAVITY);
    if (x == 0 && y == 0)
        kick_resting_bunnies();
}

// Game system: turns each bunny to face the way it's moving (the art faces
// right) and sorts bunnies lower on screen in front of those above them.
static void bunny_animate(void) {
    ECS_FOR_EACH(i, C_BUNNY) {
        if (vel_x[i] > 0)
            spr_flags[i] = 0;
        else if (vel_x[i] < 0)
            spr_flags[i] = SPRITE_FLIP_H;
        spr_depth[i] = (s16)fx_to_int(pos_y[i]);
    }
}

static const char* gravity_name(void) {
    static const char* const names[3][3] = {
        // [gravity_y + 1][gravity_x + 1]
        {"UP-LEFT", "UP", "UP-RIGHT"},
        {"LEFT", "OFF", "RIGHT"},
        {"DOWN-LEFT", "DOWN", "DOWN-RIGHT"},
    };
    return names[gravity_y + 1][gravity_x + 1];
}

static void draw_hud(void) {
    u32 permille = frame_cpu_permille();
    text_print_line(0, 0, text_format("BUNNIES %3d/%d  A:ADD B:DEL", count, MAX_ENT));
    text_print_line(
        0, 1,
        text_format("CPU %3u.%u%%  %7u CYCLES", permille / 10, permille % 10, frame_cpu_cycles()));
    text_print_line(0, 2, text_format("GRAVITY %-10s START:OFF", gravity_name()));
}

void bunnymark_update(void) {
    sys_movement();
    sys_physics();
    bunny_animate();
    sys_render_by_depth();
    draw_hud();
}
