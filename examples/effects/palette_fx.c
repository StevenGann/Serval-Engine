// The palettes scene: palette writes, sprite_set_colors() and
// tileset_set_colors() (docs/sprites.md#palettes,
// docs/tilemaps.md#palette-writes). The scene's contract (what main.c resets
// between scenes, what leave() must undo) is in effects.h.
//
// A beach looking out to sea, painted at boot (paint(), art.c): a sky with
// the sun, two clouds and an island, the sea, and the sand, with two gems
// lying on it.
//   - The sea's four colors (palette 1, and four lighter ones for the sun's
//     glitter on it) move along by one every 6 frames: a palette cycle, so
//     the waves roll in toward the beach while no tile changes.
//   - The sky, the sun, the clouds, the island and the sand (palette 0) and
//     the backdrop, which is the sky's top band (color 0 of palette 0), fade
//     from day to dusk and back, every color mixed from its day and dusk
//     colors with color_mix() each frame. The sea's cycled colors are mixed
//     the same way, so it darkens with the sky.
//   - The left gem's colors fade toward white and back (a pulse). The right
//     gem turns white for 6 frames when A is pressed, then gets its colors
//     back from its ROM palette, written again. Both gems' groups load the
//     same ROM palette, each into a bank of its own, so each write changes
//     one gem only.

#include "effects.h"

// --- The gems --------------------------------------------------------------------

enum { GEM_PULSE, GEM_FLASH, SPRITE_COUNT };
#define GEM_COLORS 5 // colors 1-5 of the gems' palette

static const char* const gem_picture[] = {
    "....kkkkkkkk....", //
    "...kwwpRRRRrk...", //
    "..kwpRRRRRRrrk..", //
    ".kppRRRRRRRRrrk.", //
    "kkkkkkkkkkkkkkkk", //
    "kwppRRRRRRRRrrrk", //
    ".kppRRRRRRRRrrk.", //
    "..kpRRRRRRRRrk..", //
    "...kpRRRRRRrk...", //
    "....kpRRRRrk....", //
    ".....kRRRRk.....", //
    "......kRrk......", //
    ".......kk.......", //
};
#define GEM_ROWS ((int)(sizeof(gem_picture) / sizeof(gem_picture[0])))
static const char gem_keys[] = ".krRpw"; // colors 0-5

static const u16 gem_palette[16] = {
    0,
    COLOR_RGB(40, 16, 56),    // k: outline
    COLOR_RGB(120, 16, 64),   // r: shade
    COLOR_RGB(208, 40, 96),   // R: body
    COLOR_RGB(248, 128, 168), // p: light
    COLOR_RGB(255, 240, 248), // w: shine
};

static u32 gem_tiles[16 * 8] SERVAL_EWRAM_BSS; // one 32x32 frame

static const SpriteAsset gem = {
    .size = SPRITE_32x32, .tiles = gem_tiles, .origin_x = 16, .origin_y = 16};
static const SpriteAsset* const sprites[SPRITE_COUNT] = {&gem, &gem};
static const u16 pulse_ids[] = {GEM_PULSE};
static const u16 flash_ids[] = {GEM_FLASH};
static const SpriteGroup pulse_group = {
    .sprite_ids = pulse_ids, .palettes = gem_palette, .sprite_count = 1, .palette_count = 1};
static const SpriteGroup flash_group = {
    .sprite_ids = flash_ids, .palettes = gem_palette, .sprite_count = 1, .palette_count = 1};

// --- The beach ---------------------------------------------------------------------

// Palette 0, the land and the sky: color 0 is the backdrop, the sky's top
// band. Palette 1, the sea.
enum { LAND, SEA, BG_PALETTES };
enum {
    SKY_1 = 1, // the sky's bands below the backdrop, down to the horizon
    SKY_5 = 5,
    SUN,
    SUN_RIM,
    CLOUD,
    CLOUD_SHADE,
    ISLAND,
    SAND_LIGHT,
    SAND,
    SAND_DARK,
    FOAM,
    LAND_COLORS // 15: the backdrop and colors 1-14
};
#define WAVES 4 // sea colors 1-4, and the glitter's 5-8

// By day and at dusk: the backdrop, then palette 0's colors 1-14.
static const Color day[LAND_COLORS] = {
    COLOR_RGB(56, 104, 216),  COLOR_RGB(80, 128, 232),  COLOR_RGB(108, 156, 240),
    COLOR_RGB(140, 184, 248), COLOR_RGB(176, 212, 252), COLOR_RGB(212, 232, 255),
    COLOR_RGB(255, 252, 224), COLOR_RGB(255, 236, 160), COLOR_RGB(255, 255, 255),
    COLOR_RGB(196, 210, 236), COLOR_RGB(88, 128, 152),  COLOR_RGB(252, 232, 176),
    COLOR_RGB(236, 204, 140), COLOR_RGB(200, 164, 108), COLOR_RGB(255, 255, 255),
};
static const Color dusk[LAND_COLORS] = {
    COLOR_RGB(32, 20, 80),    COLOR_RGB(64, 32, 112),  COLOR_RGB(120, 48, 128),
    COLOR_RGB(192, 72, 112),  COLOR_RGB(240, 120, 96), COLOR_RGB(255, 176, 96),
    COLOR_RGB(255, 232, 160), COLOR_RGB(255, 128, 64), COLOR_RGB(255, 168, 144),
    COLOR_RGB(144, 64, 112),  COLOR_RGB(64, 28, 80),   COLOR_RGB(208, 136, 120),
    COLOR_RGB(168, 100, 104), COLOR_RGB(120, 68, 88),  COLOR_RGB(255, 208, 192),
};
// The sea's colors as cycled, deep to light, then the glitter's.
static const Color sea_day[2 * WAVES] = {
    COLOR_RGB(16, 48, 120),   COLOR_RGB(24, 80, 168),   COLOR_RGB(40, 120, 208),
    COLOR_RGB(104, 176, 236), COLOR_RGB(120, 184, 244), COLOR_RGB(160, 208, 250),
    COLOR_RGB(208, 232, 255), COLOR_RGB(248, 252, 255),
};
static const Color sea_dusk[2 * WAVES] = {
    COLOR_RGB(28, 16, 68),    COLOR_RGB(52, 28, 100),   COLOR_RGB(96, 48, 120),
    COLOR_RGB(168, 88, 120),  COLOR_RGB(208, 104, 96),  COLOR_RGB(240, 144, 96),
    COLOR_RGB(255, 192, 120), COLOR_RGB(255, 236, 184),
};

// The tileset's palettes as loaded: the day colors (draw_art copies them).
static u16 bg_palettes[BG_PALETTES * 16] SERVAL_EWRAM_BSS;

static Painting beach SERVAL_EWRAM_BSS;

static Tileset tileset = {
    .tiles = beach.tiles, .palettes = bg_palettes, .palette_count = BG_PALETTES};

#define HORIZON 80 // the sky above, the sea below
#define SHORE 128  // the sand from here down
#define SUN_X 172  // the sun's center, and its glitter's
#define SUN_Y 58
#define SUN_R 15

// A 4x4 ordered dither: blends one band of the sky into the next.
static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// A cloud: discs over a flat base, lit from the top left.
typedef struct {
    s16 x, y, r;
} Disc;
static const Disc clouds[] = {
    {34, 36, 8},  {46, 30, 11}, {60, 33, 9},  {72, 37, 6}, // the big one
    {112, 46, 5}, {121, 42, 7}, {131, 45, 5},              // the small one
};
#define CLOUD_BASE_1 42
#define CLOUD_BASE_2 50

static bool in_disc(const Disc* d, int x, int y) {
    int dx = x - d->x, dy = y - d->y;
    return dx * dx + dy * dy < d->r * d->r;
}

static u32 cloud_pixel(int x, int y) {
    if (y < 18 || y > CLOUD_BASE_2 || x < 25 || x > 136)
        return 0; // no cloud out here
    for (u32 k = 0; k < sizeof(clouds) / sizeof(clouds[0]); k++) {
        const Disc* d = &clouds[k];
        if (y > (k < 4 ? CLOUD_BASE_1 : CLOUD_BASE_2) || !in_disc(d, x, y))
            continue;
        // Lit where the same disc moved down and right would still cover it.
        Disc lit = {(s16)(d->x - 1), (s16)(d->y - 2), d->r};
        bool shade = y >= (k < 4 ? CLOUD_BASE_1 : CLOUD_BASE_2) - 1;
        return !shade && in_disc(&lit, x, y) ? CLOUD : CLOUD_SHADE;
    }
    return 0;
}

// What beach_pixel() needs of each column and line, worked out once before
// painting: the GBA has no divide instruction, and dividing for each of the
// 38,400 pixels would take over a second.
static s8 island[SCREEN_W] SERVAL_EWRAM_BSS;  // the island's height in each column
static u8 sky_band[HORIZON] SERVAL_EWRAM_BSS; // each sky line's place among the bands, in 16ths
static u8 sea_band[SHORE - HORIZON] SERVAL_EWRAM_BSS; // each sea line's wave band, before the waves
static u8 sea_reach[SHORE - HORIZON] SERVAL_EWRAM_BSS; // half the glitter's width

static void plan_beach(void) {
    for (int x = 0; x < SCREEN_W; x++) {
        // Two low hills on the horizon.
        int a = 11 - (x - 42) * (x - 42) / 60, b = 7 - (x - 78) * (x - 78) / 50;
        island[x] = (s8)int_clamp(int_max(a, b), -1, 20);
    }
    for (int y = 0; y < HORIZON; y++)
        sky_band[y] = (u8)(y * 6 * 16 / HORIZON);
    for (int d = 0; d < SHORE - HORIZON; d++) {
        sea_band[d] = (u8)(d * (120 - d) / 40); // thinner toward the horizon
        sea_reach[d] = (u8)(4 + d / 3);         // wider toward the viewer
    }
}

// Every pixel of the scene: palette * 16 + color.
static u32 beach_pixel(int x, int y) {
    if (y < HORIZON) {
        int dx = x - SUN_X, dy = y - SUN_Y, d = dx * dx + dy * dy;
        if (d < SUN_R * SUN_R)
            return d < (SUN_R - 2) * (SUN_R - 2) ? SUN : SUN_RIM;
        u32 c = cloud_pixel(x, y);
        if (c)
            return c;
        if (HORIZON - y <= island[x])
            return ISLAND;
        // Six bands, the first the backdrop (color 0), each solid for its
        // first half, then dithered into the next.
        int band = sky_band[y] / 16, into = sky_band[y] % 16;
        if (into > 8 && bayer[y & 3][x & 3] < (into - 8) * 2)
            band++;
        return (u32)int_min(band, SKY_5);
    }
    if (y < SHORE) {
        // Waves: bands that thin out toward the horizon, wavier near the
        // beach; under the sun, a glitter of the lighter four colors.
        int d = y - HORIZON; // 0-47
        int wave = (int)(fx_sin((u16)(x * 0x0800 + d * 0x0400)) * (1 + d / 8) / FX_ONE);
        int band = (sea_band[d] + wave + 64) / 2 % WAVES;
        bool glitter = int_abs(x - SUN_X) < sea_reach[d] && ((x + d) & 3) != 0;
        return SEA * 16 + 1 + (u32)band + (glitter ? WAVES : 0);
    }
    // The sand: foam along its wavy edge, then speckles.
    int d = y - SHORE;
    int edge = 3 + fx_sin((u16)(x * 0x0500)) * 2 / FX_ONE;
    if (d < edge)
        return FOAM;
    if (d == edge)
        return SAND_DARK;
    u32 h = (u32)(x * 7 + y * 13) % 16;
    return h == 0 ? SAND_DARK : h == 5 ? SAND_LIGHT : SAND;
}

static const MapLayer layer = {.width = PAINT_W,
                               .height = PAINT_H,
                               .cells = beach.cells,
                               .metatiles = beach.metatiles,
                               .metatile_count = PAINT_W * PAINT_H,
                               .bg = 3,
                               .flags = MAP_LAYER_FIXED};

static bool art_drawn;

static void draw_art(void) {
    // The gem, each pixel of the picture 2x2.
    canvas_begin(32, 32);
    for (int y = 0; y < GEM_ROWS; y++) {
        for (int x = 0; gem_picture[y][x]; x++) {
            for (u32 c = 1; gem_keys[c]; c++) {
                if (gem_keys[c] != gem_picture[y][x])
                    continue;
                for (int k = 0; k < 4; k++)
                    canvas_plot(2 * x + k % 2, 3 + 2 * y + k / 2, c);
            }
        }
    }
    canvas_pack(gem_tiles);
    plan_beach();
    paint(&beach, beach_pixel);
    tileset.tile_count = beach.tile_count;
    for (u32 k = 1; k < LAND_COLORS; k++)
        bg_palettes[LAND * 16 + k] = day[k];
    for (u32 k = 0; k < 2 * WAVES; k++)
        bg_palettes[SEA * 16 + 1 + k] = sea_day[k];
}

// --- The scene ---------------------------------------------------------------------

static u32 tick;
static int flash_left; // frames the right gem stays white

// 0 up to 256 and back down over `period` frames.
static u32 triangle(u32 t, u32 period) {
    u32 phase = t % period, half = period / 2;
    return (phase < half ? phase : period - phase) * 256 / half;
}

#define GEM_Y 142

static void enter(void) {
    if (!art_drawn) {
        draw_art();
        art_drawn = true;
    }
    tileset_load(&tileset);
    map_load(&layer);
    sprite_table_set(sprites, SPRITE_COUNT);
    sprite_group_load(&pulse_group);
    sprite_group_load(&flash_group);
    text_print_centered(1, "SKY: A FADE TO DUSK");
    text_print(6, 17, "< FADE");
    text_print(14, 17, "A: FLASH >");
    text_print_centered(14, "SEA: A PALETTE CYCLE");
    tick = 0;
    flash_left = 0;
}

static void update(void) {
    // The fades, each color mixed from its own two: the land, the sky and
    // the backdrop toward dusk and back (about 8.5 seconds), the left gem
    // toward white and three quarters of the way (about 2 seconds).
    u32 dusk_amount = triangle(tick, 512);
    Color land[LAND_COLORS];
    for (u32 k = 0; k < LAND_COLORS; k++)
        land[k] = color_mix(day[k], dusk[k], dusk_amount);
    tileset_set_colors(0, land, LAND_COLORS); // color 0 of palette 0: the backdrop

    // The cycle: every 6 frames each sea color moves on by one band, so the
    // waves roll down the screen, toward the beach. Then mixed toward dusk.
    u32 step = tick / 6;
    Color sea[2 * WAVES];
    for (u32 k = 0; k < 2 * WAVES; k++) {
        u32 from = (k / WAVES) * WAVES + (k + WAVES - step % WAVES) % WAVES;
        sea[k] = color_mix(sea_day[from], sea_dusk[from], dusk_amount);
    }
    tileset_set_colors(SEA * 16 + 1, sea, 2 * WAVES);

    u32 shine = triangle(tick, 128) * 3 / 4;
    Color pulse[GEM_COLORS];
    for (u32 k = 0; k < GEM_COLORS; k++)
        pulse[k] = color_mix(gem_palette[1 + k], COLOR_RGB(255, 255, 255), shine);
    sprite_set_colors(GEM_PULSE, 1, pulse, GEM_COLORS);

    // The flash: white, then the ROM colors written back.
    if (button_pressed(BUTTON_A)) {
        static const Color white[GEM_COLORS] = {0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF};
        sprite_set_colors(GEM_FLASH, 1, white, GEM_COLORS);
        flash_left = 6;
    } else if (flash_left > 0 && --flash_left == 0) {
        sprite_set_colors(GEM_FLASH, 1, gem_palette + 1, GEM_COLORS);
    }

    sprite_draw(GEM_PULSE, 0, 32, GEM_Y, 0);
    sprite_draw(GEM_FLASH, 0, 208, GEM_Y, 0);
    tick++;
}

static void leave(void) {
    // The tileset's colors back where they were loaded (main.c sets the
    // backdrop again; the gems' groups are unloaded, their banks reused).
    tileset_set_colors(1, bg_palettes + 1, BG_PALETTES * 16 - 1);
}

const Scene palette_scene = {.name = "PALETTES", .enter = enter, .update = update, .leave = leave};
