// The raster scene: raster effects, raster_scroll(), raster_backdrop() and
// raster_clear() (docs/runtime-systems.md#raster-effects): a value per
// scanline, which the engine copies into a register as the hardware draws
// each line. The scene's contract (what main.c resets between scenes, what
// leave() must undo) is in effects.h.
//
// A city at night on a lake: one map layer on background 2, two rows of
// towers above the horizon (line 96), the far ones darker, and below it
// their reflections, upside down. The sky and the lake are the backdrop
// (their tiles are see-through), with stars and a moon above and glints on
// the water. A switches between two effects, one at a time, as the engine
// has them:
//   - Ripple: raster_scroll() moves each line of the layer sideways by its
//     own offset, a sine wave on the lake's lines, a pixel near the horizon
//     to four at the bottom, moved along every frame. The reflections and
//     the glints wobble; the city above stays still.
//   - Sunset: raster_backdrop() gives each line its own backdrop color: a
//     sky from deep blue at the top through rose to orange at the horizon,
//     and below it the same colors upside down and darker, the lake
//     mirroring the sky. It darkens to night and back over 8 seconds: the
//     engine reads the table at every frame_end(), so changing it animates
//     the sky.
// Leaving the sunset brings back the plain night sky set with
// screen_set_backdrop().

#include "effects.h"

// --- Art -------------------------------------------------------------------------

// Colors of every palette: the towers' (palettes 0 and 2, near and far) and
// their reflections' (1 and 3, darker and bluer); the water, the stars and the
// moon use palette 0's.
enum { WALL = 1, LIT, DARK, LEDGE, BEACON, GLINT, GLINT_DIM, STAR, MOON, MOON_SHADE };
enum { NEAR, NEAR_REFLECTED, FAR, FAR_REFLECTED, PALETTE_COUNT };

static const u16 palettes[PALETTE_COUNT * 16] = {
    [NEAR * 16 + WALL] = COLOR_RGB(44, 36, 72),
    [NEAR * 16 + LIT] = COLOR_RGB(255, 214, 112),
    [NEAR * 16 + DARK] = COLOR_RGB(24, 20, 44),
    [NEAR * 16 + LEDGE] = COLOR_RGB(96, 80, 132),
    [NEAR * 16 + BEACON] = COLOR_RGB(255, 72, 72),
    [NEAR * 16 + GLINT] = COLOR_RGB(150, 184, 240),
    [NEAR * 16 + GLINT_DIM] = COLOR_RGB(64, 96, 160),
    [NEAR * 16 + STAR] = COLOR_RGB(232, 236, 255),
    [NEAR * 16 + MOON] = COLOR_RGB(255, 246, 208),
    [NEAR * 16 + MOON_SHADE] = COLOR_RGB(208, 196, 156),
    [NEAR_REFLECTED * 16 + WALL] = COLOR_RGB(20, 24, 60),
    [NEAR_REFLECTED * 16 + LIT] = COLOR_RGB(176, 136, 96),
    [NEAR_REFLECTED * 16 + DARK] = COLOR_RGB(14, 16, 44),
    [NEAR_REFLECTED * 16 + LEDGE] = COLOR_RGB(44, 52, 100),
    [NEAR_REFLECTED * 16 + BEACON] = COLOR_RGB(168, 56, 88),
    [FAR * 16 + WALL] = COLOR_RGB(30, 30, 62),
    [FAR * 16 + LIT] = COLOR_RGB(208, 168, 112),
    [FAR * 16 + DARK] = COLOR_RGB(22, 22, 48),
    [FAR * 16 + LEDGE] = COLOR_RGB(56, 58, 100),
    [FAR * 16 + BEACON] = COLOR_RGB(255, 72, 72),
    [FAR_REFLECTED * 16 + WALL] = COLOR_RGB(18, 24, 64),
    [FAR_REFLECTED * 16 + LIT] = COLOR_RGB(128, 112, 108),
    [FAR_REFLECTED * 16 + DARK] = COLOR_RGB(14, 18, 52),
    [FAR_REFLECTED * 16 + LEDGE] = COLOR_RGB(32, 40, 86),
    [FAR_REFLECTED * 16 + BEACON] = COLOR_RGB(168, 56, 88),
};

// Tiles, drawn at boot (draw_tiles): four walls, by which of their two
// windows are lit, the same four with a roof ledge on top, an antenna, the
// quay, three
// kinds of glint on the water, two stars and the moon's four.
enum {
    T_BLANK,
    T_WALL,                 // + 0-3: windows lit (bit 0 left, bit 1 right)
    T_ROOF = T_WALL + 4,    // + 0-3: the same, under a ledge
    T_ANTENNA = T_ROOF + 4, // a mast with a red beacon, in the sky above a roof
    T_QUAY,                 // the waterfront the towers stand on, with lamps
    T_GLINT,                // + 0-2
    T_STAR = T_GLINT + 3,   // + 0-1
    T_MOON = T_STAR + 2,    // + 0-3: top-left, top-right, bottom-left, bottom-right
    TILE_COUNT = T_MOON + 4
};

static u32 tiles[TILE_COUNT * 8] SERVAL_EWRAM_BSS;

static const Tileset tileset = {
    .tiles = tiles, .tile_count = TILE_COUNT, .palettes = palettes, .palette_count = PALETTE_COUNT};

// A floor of wall: two windows 2 pixels wide and 4 tall, lit or dark.
static void draw_wall(u32 lit, bool roof) {
    canvas_begin(8, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            canvas_plot(x, y, roof && y == 0 ? LEDGE : WALL);
    for (int w = 0; w < 2; w++)
        for (int y = 2; y < 6; y++)
            for (int x = 1 + 4 * w; x < 3 + 4 * w; x++)
                canvas_plot(x, y, lit & (1u << w) ? LIT : DARK);
}

// The moon: a crescent, a disc of radius 7 less a disc to its upper right;
// (x, y) in its 16x16 picture.
static u32 moon_pixel(int x, int y) {
    int dx = 2 * x - 15, dy = 2 * y - 15;
    if (dx * dx + dy * dy >= 14 * 14)
        return 0;
    int bx = dx - 9, by = dy + 5; // the bite
    if (bx * bx + by * by < 12 * 12)
        return 0;
    return dx + dy > -6 ? MOON_SHADE : MOON;
}

static void draw_tiles(void) {
    u32* out = tiles + 8; // tile 0 stays blank
    for (u32 roof = 0; roof < 2; roof++) {
        for (u32 lit = 0; lit < 4; lit++) {
            draw_wall(lit, roof);
            out = canvas_pack(out);
        }
    }
    canvas_begin(8, 8);
    for (int y = 2; y < 8; y++)
        canvas_plot(3, y, LEDGE);
    canvas_plot(3, 1, BEACON);
    out = canvas_pack(out);
    canvas_begin(8, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            canvas_plot(x, y, y == 0 ? LEDGE : y == 2 && x == 3 ? LIT : DARK);
    out = canvas_pack(out);

    // Glints: short dashes of light on the water.
    static const s8 glints[3][4] = {{1, 2, 3, GLINT}, {4, 5, 2, GLINT_DIM}, {2, 6, 4, GLINT_DIM}};
    for (u32 g = 0; g < 3; g++) {
        canvas_begin(8, 8);
        for (int k = 0; k < glints[g][2]; k++)
            canvas_plot(glints[g][0] + k, glints[g][1], (u32)glints[g][3]);
        out = canvas_pack(out);
    }
    // Stars: a twinkle of five pixels, and a single one.
    canvas_begin(8, 8);
    canvas_plot(3, 2, GLINT);
    canvas_plot(2, 3, GLINT);
    canvas_plot(3, 3, STAR);
    canvas_plot(4, 3, GLINT);
    canvas_plot(3, 4, GLINT);
    out = canvas_pack(out);
    canvas_begin(8, 8);
    canvas_plot(5, 5, STAR);
    out = canvas_pack(out);

    for (int q = 0; q < 4; q++) {
        canvas_begin(8, 8);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                canvas_plot(x, y, moon_pixel(q % 2 * 8 + x, q / 2 * 8 + y));
        out = canvas_pack(out);
    }
}

// --- The city --------------------------------------------------------------------

// The layer is 32 x 20 tiles (256 x 160 pixels). It wraps, and a map 256
// pixels wide fits VRAM whole, so the rippled lines never show an edge.
#define CITY_W 16 // in metatiles
#define CITY_H 10
#define TILES_W (CITY_W * 2)
#define TILES_H (CITY_H * 2)
#define HORIZON_TILE 12            // the lake's first tile row
#define HORIZON (HORIZON_TILE * 8) // the lake's first line
#define LAKE_TILES (TILES_H - HORIZON_TILE)
#define QUAY_TILE (HORIZON_TILE - 1) // the towers stand on it

// The towers, in tiles: left column, width, height above the quay, and an
// antenna.
typedef struct {
    u8 x, w, h;
    bool antenna;
} Tower;

static const Tower far_towers[] = {
    {0, 3, 5, true},   {4, 2, 6, false},  {7, 4, 4, false}, {12, 3, 6, true},
    {16, 2, 4, false}, {19, 4, 5, false}, {24, 3, 6, true}, {28, 3, 4, false},
};
static const Tower near_towers[] = {
    {2, 2, 2, false},  {5, 3, 3, false},  {10, 2, 4, true},  {14, 4, 2, false},
    {18, 2, 3, false}, {22, 3, 4, false}, {27, 2, 2, false}, {30, 2, 3, false},
};

// The picture, a screen entry per tile, then cut into a metatile per cell:
// every cell of this city is its own metatile, so no two towers repeat.
static u16 picture[TILES_H][TILES_W] SERVAL_EWRAM_BSS;
static Metatile metatiles[CITY_W * CITY_H] SERVAL_EWRAM_BSS;
static u16 cells[CITY_W * CITY_H] SERVAL_EWRAM_BSS;

static const MapLayer city = {.width = CITY_W,
                              .height = CITY_H,
                              .cells = cells,
                              .metatiles = metatiles,
                              .metatile_count = CITY_W * CITY_H,
                              .bg = 2,
                              .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP};

// A little hash of a tile's place: which windows are lit, where the glints
// and the stars are.
static u32 scatter(int x, int y) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void place(const Tower* towers, int count, u32 palette) {
    for (int t = 0; t < count; t++) {
        const Tower* tw = &towers[t];
        int top = QUAY_TILE - tw->h;
        for (int x = tw->x; x < tw->x + tw->w; x++) {
            int col = x % TILES_W;
            for (int y = top; y < QUAY_TILE; y++) {
                u32 lit = scatter(x, y) & 3;
                if (scatter(x + 7, y) % 3 == 0)
                    lit = 0; // some floors dark
                picture[y][col] = MAP_SE((y == top ? T_ROOF : T_WALL) + lit, palette, 0);
            }
            if (tw->antenna && x == tw->x + tw->w / 2)
                picture[top - 1][col] = MAP_SE(T_ANTENNA, palette, 0);
        }
    }
}

static void build_city(void) {
    for (int y = 0; y < TILES_H; y++)
        for (int x = 0; x < TILES_W; x++)
            picture[y][x] = 0;
    // Stars in the upper sky, the moon, then the towers, far first.
    for (int y = 2; y < 7; y++)
        for (int x = 0; x < TILES_W; x++)
            if (scatter(x, y) % 9 == 0)
                picture[y][x] = MAP_SE(T_STAR + (scatter(y, x) & 1), NEAR, 0);
    for (int q = 0; q < 4; q++)
        picture[2 + q / 2][24 + q % 2] = MAP_SE(T_MOON + q, NEAR, 0);
    place(far_towers, sizeof(far_towers) / sizeof(far_towers[0]), FAR);
    place(near_towers, sizeof(near_towers) / sizeof(near_towers[0]), NEAR);
    for (int x = 0; x < TILES_W; x++)
        picture[QUAY_TILE][x] = MAP_SE(T_QUAY, NEAR, 0);

    // The lake: tile row HORIZON_TILE + k mirrors row HORIZON_TILE - 1 - k,
    // flipped and in the reflections' palette (one after the tower's); the
    // open water gets glints.
    for (int k = 0; k < LAKE_TILES; k++) {
        for (int x = 0; x < TILES_W; x++) {
            u16 above = picture[HORIZON_TILE - 1 - k][x];
            u16 se = 0;
            u32 tile = above & 0x3FF;
            if (tile >= T_WALL && tile <= T_QUAY)
                se = (u16)((above + (1u << 12)) | MAP_SE_FLIP_V);
            else if (scatter(x, k + 40) % 3 == 0)
                se = MAP_SE(T_GLINT + scatter(k, x) % 3, NEAR, 0);
            picture[HORIZON_TILE + k][x] = se;
        }
    }

    for (int my = 0; my < CITY_H; my++) {
        for (int mx = 0; mx < CITY_W; mx++) {
            Metatile* m = &metatiles[my * CITY_W + mx];
            for (int k = 0; k < 4; k++)
                m->se[k] = picture[2 * my + k / 2][2 * mx + k % 2];
            cells[my * CITY_W + mx] = (u16)(my * CITY_W + mx);
        }
    }
}

// --- The effects -----------------------------------------------------------------

// The tables the engine reads at every frame_end(): a value per line.
static s16 ripple[SCREEN_H] SERVAL_EWRAM_BSS;
static Color sky[SCREEN_H] SERVAL_EWRAM_BSS;

static const Color night = COLOR_RGB(12, 16, 40);

static bool art_drawn;
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

// The sky from deep blue at the top through rose to orange at the horizon,
// and the lake below mirroring it, darker. `dusk` 0 is the sunset, 256 night.
static void make_sky(u32 dusk) {
    Color top = color_mix(COLOR_RGB(32, 40, 128), COLOR_RGB(8, 8, 32), dusk);
    Color middle = color_mix(COLOR_RGB(200, 80, 128), COLOR_RGB(36, 24, 72), dusk);
    Color horizon = color_mix(COLOR_RGB(255, 176, 80), COLOR_RGB(72, 40, 96), dusk);
    Color deep = COLOR_RGB(8, 12, 40); // the lake's own color, mixed in below
    for (int y = 0; y < HORIZON; y++) {
        // Top to middle over the first 60% of the sky, then to the horizon.
        int split = HORIZON * 3 / 5;
        sky[y] = y < split
                     ? color_mix(top, middle, (u32)(y * 256 / split))
                     : color_mix(middle, horizon, (u32)((y - split) * 256 / (HORIZON - split)));
    }
    for (int y = HORIZON; y < SCREEN_H; y++)
        sky[y] = color_mix(sky[2 * HORIZON - 1 - y], deep, (u32)(64 + (y - HORIZON) * 2));
}

static void show_caption(void) {
    text_print_centered(1, sunset ? "SUNSET: raster_backdrop()" : "RIPPLE: raster_scroll()");
    text_print_centered(19, sunset ? "A: RIPPLE" : "A: SUNSET");
}

static void enter(void) {
    if (!art_drawn) {
        draw_tiles();
        build_city();
        art_drawn = true;
    }
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
        if (sunset) {
            tick = 0; // from the sunset on
            make_sky(0);
            raster_backdrop(sky);
        } else
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
