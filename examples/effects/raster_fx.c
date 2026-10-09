// The raster scene: raster effects, raster_scroll(), raster_backdrop() and
// raster_clear() (docs/runtime-systems.md#raster-effects): a value per
// scanline, which the engine copies into a register as the hardware draws
// each line. The scene's contract (what main.c resets between scenes, what
// leave() must undo) is in effects.h.
//
// A city at night on a lake: one map layer on background 2, the buildings
// above the horizon (line 96) and their reflections below it. A switches
// between two effects, one at a time, as the engine has them:
//   - Ripple: raster_scroll() moves each line of the layer sideways by its
//     own offset, a sine wave on the lake's lines, a pixel near the horizon
//     to four at the bottom, moved along every frame. The reflections wobble;
//     the city above stays still.
//   - Sunset: raster_backdrop() gives each line its own backdrop color, a sky
//     from deep blue at the top to orange at the horizon behind the city,
//     which darkens to night and back over 8 seconds: the engine reads the
//     table at every frame_end(), so changing it animates the sky.
// Leaving the sunset brings back the plain night sky set with
// screen_set_backdrop().

#include "effects.h"

// --- Art -------------------------------------------------------------------------

// Tiles, 4 bits per pixel: row words with pixel 0 in the low nibble. Colors:
// 1 wall, 2 lit window, 3 roof, 4 water, 5 a glint on the water, 6 dark
// window.
enum { T_BLANK, T_WINDOWS_A, T_WINDOWS_B, T_ROOF, T_WATER, TILE_COUNT };

static const u32 tiles[TILE_COUNT * 8] = {
    [T_WINDOWS_A * 8] = 0x11111111,
    0x12211221,
    0x12211221,
    0x11111111, //
    0x11111111,
    0x16611221,
    0x16611221,
    0x11111111, //
    [T_WINDOWS_B * 8] = 0x11111111,
    0x16611661,
    0x16611661,
    0x11111111, //
    0x11111111,
    0x12211661,
    0x12211661,
    0x11111111, //
    [T_ROOF * 8] = 0x33333333,
    0x11111111,
    0x12211221,
    0x12211221, //
    0x11111111,
    0x11111111,
    0x16611221,
    0x11111111, //
    [T_WATER * 8] = 0x44444444,
    0x44444444,
    0x44455444,
    0x44444444, //
    0x44444444,
    0x44444444,
    0x54444445,
    0x44444444,
};

// Palette 0, the city and the water; palette 1, the reflections: the same
// colors, darker and bluer.
static const u16 palettes[2 * 16] = {
    [1] = COLOR_RGB(48, 40, 72),       [2] = COLOR_RGB(255, 216, 112),
    [3] = COLOR_RGB(104, 88, 136),     [4] = COLOR_RGB(16, 40, 88),
    [5] = COLOR_RGB(72, 120, 184),     [6] = COLOR_RGB(32, 26, 52),
    [16 + 1] = COLOR_RGB(24, 32, 80),  [16 + 2] = COLOR_RGB(144, 136, 120),
    [16 + 3] = COLOR_RGB(56, 64, 112), [16 + 6] = COLOR_RGB(18, 24, 64),
};

static const Tileset tileset = {
    .tiles = tiles, .tile_count = TILE_COUNT, .palettes = palettes, .palette_count = 2};

// Metatiles: sky (the backdrop shows), a building's top and body, water, and
// the building's two upside down in the reflections' colors.
enum { M_SKY, M_ROOF, M_WALL, M_WATER, M_ROOF_REFLECTED, M_WALL_REFLECTED, METATILE_COUNT };
#define CITY(t) MAP_SE(t, 0, 0)
#define REFLECTED(t) MAP_SE(t, 1, MAP_SE_FLIP_V)

static const Metatile metatiles[METATILE_COUNT] = {
    [M_ROOF] = {.se = {CITY(T_ROOF), CITY(T_ROOF), CITY(T_WINDOWS_A), CITY(T_WINDOWS_B)}},
    [M_WALL] = {.se = {CITY(T_WINDOWS_A), CITY(T_WINDOWS_B), CITY(T_WINDOWS_B), CITY(T_WINDOWS_A)}},
    [M_WATER] = {.se = {CITY(T_WATER), CITY(T_WATER), CITY(T_WATER), CITY(T_WATER)}},
    [M_ROOF_REFLECTED] = {.se = {REFLECTED(T_WINDOWS_A), REFLECTED(T_WINDOWS_B), REFLECTED(T_ROOF),
                                 REFLECTED(T_ROOF)}},
    [M_WALL_REFLECTED] = {.se = {REFLECTED(T_WINDOWS_B), REFLECTED(T_WINDOWS_A),
                                 REFLECTED(T_WINDOWS_B), REFLECTED(T_WINDOWS_A)}},
};

// 16 x 10 metatiles (256 x 160 pixels): the city in rows 0-5, the lake in
// rows 6-9. It wraps, and a map 16 metatiles wide fits VRAM whole, so the
// rippled lines never show an edge.
#define CITY_W 16
#define CITY_H 10
#define HORIZON_ROW 6
#define HORIZON (HORIZON_ROW * 16) // the first line of the lake

static const u8 heights[CITY_W] = {2, 4, 1, 3, 1, 2, 4, 3, 1, 2, 1, 3, 4, 1, 2, 3};
static u16 cells[CITY_W * CITY_H] SERVAL_EWRAM_BSS;

static const MapLayer city = {.width = CITY_W,
                              .height = CITY_H,
                              .cells = cells,
                              .metatiles = metatiles,
                              .metatile_count = METATILE_COUNT,
                              .bg = 2,
                              .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP};

// A building `heights[x]` metatiles tall in each column, and below the
// horizon its mirror image (row HORIZON_ROW + k mirrors row HORIZON_ROW - 1 - k).
static void build_city(void) {
    for (int x = 0; x < CITY_W; x++) {
        int top = HORIZON_ROW - heights[x];
        for (int y = 0; y < HORIZON_ROW; y++) {
            u16 cell = y < top ? M_SKY : y == top ? M_ROOF : M_WALL;
            cells[y * CITY_W + x] = cell;
            int mirror = 2 * HORIZON_ROW - 1 - y;
            if (mirror < CITY_H)
                cells[mirror * CITY_W + x] = cell == M_SKY    ? M_WATER
                                             : cell == M_ROOF ? M_ROOF_REFLECTED
                                                              : M_WALL_REFLECTED;
        }
    }
}

// --- The effects -----------------------------------------------------------------

// The tables the engine reads at every frame_end(): a value per line.
static s16 ripple[SCREEN_H] SERVAL_EWRAM_BSS;
static Color sky[SCREEN_H] SERVAL_EWRAM_BSS;

static const Color night = COLOR_RGB(12, 16, 40);

static bool sunset;
static u32 tick;

// The lake's lines move sideways on a sine wave, more toward the bottom
// (nearer the viewer), the wave moving along with `phase`. Offsets stay
// within -4 to 4, which the layer's 9-pixel limit allows anywhere (and this
// layer wraps whole in VRAM anyway).
static void make_ripple(u32 phase) {
    for (int y = 0; y < SCREEN_H; y++) {
        if (y < HORIZON) {
            ripple[y] = 0;
            continue;
        }
        int depth = y - HORIZON; // 0-63
        FIXED amplitude = FX(1) + depth * FX(3) / 63;
        u16 angle = (u16)(phase + (u32)y * 0x0700u);
        ripple[y] = (s16)fx_to_int(fx_mul(fx_sin(angle), amplitude));
    }
}

// The sky from its top color to the horizon's, then the horizon color on
// down (the lake covers those lines). `dusk` 0 is the sunset, 256 night.
static void make_sky(u32 dusk) {
    Color top = color_mix(COLOR_RGB(40, 64, 168), COLOR_RGB(8, 8, 32), dusk);
    Color horizon = color_mix(COLOR_RGB(255, 152, 72), COLOR_RGB(64, 32, 88), dusk);
    for (u32 y = 0; y < SCREEN_H; y++)
        sky[y] = y < HORIZON ? color_mix(top, horizon, y * 256 / (HORIZON - 1)) : horizon;
}

static void show_caption(void) {
    text_print_centered(1, sunset ? "SUNSET: raster_backdrop()" : "RIPPLE: raster_scroll()");
    text_print_centered(19, sunset ? "A: RIPPLE" : "A: SUNSET");
}

static void enter(void) {
    build_city();
    tileset_load(&tileset);
    map_load(&city);
    screen_set_backdrop(night);
    sunset = false;
    tick = 0;
    show_caption();
    make_ripple(0);
    raster_scroll(2, false, ripple);
}

static void update(void) {
    if (button_pressed(BUTTON_A)) {
        sunset = !sunset;
        show_caption();
        // One effect at a time: each call replaces the other at the next
        // frame_end(), and leaving the sunset brings back `night`.
        if (sunset)
            raster_backdrop(sky);
        else
            raster_scroll(2, false, ripple);
    }
    // The table changes in place: the effect follows at frame_end().
    if (sunset) {
        u32 cycle = tick % 480; // 8 seconds: sunset, to night, and back
        make_sky(cycle < 240 ? cycle * 256 / 240 : (480 - cycle) * 256 / 240);
    } else {
        make_ripple(tick * 0x0500u);
    }
    tick++;
}

// The effect ends at the next frame_end(), while the screen is black.
static void leave(void) {
    raster_clear();
}

const Scene raster_scene = {.name = "RASTER", .enter = enter, .update = update, .leave = leave};
