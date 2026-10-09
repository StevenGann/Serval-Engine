// The palettes scene: palette writes, sprite_set_colors() and
// tileset_set_colors() (docs/sprites.md#palettes,
// docs/tilemaps.md#palette-writes). The scene's contract (what main.c resets
// between scenes, what leave() must undo) is in effects.h.
//
// A sky over water, two gems on the shore between them, all drawn at boot:
//   - The water's four colors (palette 1) move along by one every 6 frames:
//     a palette cycle, so its stripes flow while no tile changes.
//   - The sky's four bands (palette 2) and the backdrop behind the gems
//     (color 0 of palette 0) fade from day to dusk and back, every color
//     mixed from its day and dusk colors with color_mix() each frame.
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

// --- The background ----------------------------------------------------------------

#define WATER 1 // its palette
#define SKY 2   // its palette
#define BANDS 4 // sky bands, and water colors

// The water's colors, as cycled: deep to light.
#define WATER_1 COLOR_RGB(16, 40, 112)
#define WATER_2 COLOR_RGB(24, 72, 160)
#define WATER_3 COLOR_RGB(40, 112, 200)
#define WATER_4 COLOR_RGB(104, 168, 232)
// The sky's bands, top to bottom, by day.
#define SKY_1 COLOR_RGB(64, 120, 232)
#define SKY_2 COLOR_RGB(96, 152, 240)
#define SKY_3 COLOR_RGB(136, 184, 248)
#define SKY_4 COLOR_RGB(184, 216, 255)

static const Color water_colors[BANDS] = {WATER_1, WATER_2, WATER_3, WATER_4};
// The sky's bands, then the backdrop: by day and at dusk.
static const Color day[BANDS + 1] = {SKY_1, SKY_2, SKY_3, SKY_4, COLOR_RGB(24, 64, 72)};
static const Color dusk[BANDS + 1] = {COLOR_RGB(40, 24, 96), COLOR_RGB(112, 48, 128),
                                      COLOR_RGB(208, 88, 104), COLOR_RGB(248, 160, 88),
                                      COLOR_RGB(56, 24, 64)};

static const u16 bg_palettes[3 * 16] = {
    [WATER * 16 + 1] = WATER_1, [WATER * 16 + 2] = WATER_2, [WATER * 16 + 3] = WATER_3,
    [WATER * 16 + 4] = WATER_4, [SKY * 16 + 1] = SKY_1,     [SKY * 16 + 2] = SKY_2,
    [SKY * 16 + 3] = SKY_3,     [SKY * 16 + 4] = SKY_4,
};

// Tile 0 blank, tiles 1-4 the water metatile's, then a solid tile of each
// sky color.
#define SKY_TILE 5
static u32 bg_tiles[(SKY_TILE + BANDS) * 8] SERVAL_EWRAM_BSS;
static const Tileset tileset = {
    .tiles = bg_tiles, .tile_count = SKY_TILE + BANDS, .palettes = bg_palettes, .palette_count = 3};

#define SOLID(tile, palette)                                                                       \
    {                                                                                              \
        .se = {                                                                                    \
            MAP_SE(tile, palette, 0),                                                              \
            MAP_SE(tile, palette, 0),                                                              \
            MAP_SE(tile, palette, 0),                                                              \
            MAP_SE(tile, palette, 0)                                                               \
        }                                                                                          \
    }
// Metatiles: 0 empty (the backdrop shows), 1 water, 2-5 the sky's bands.
static const Metatile metatiles[2 + BANDS] = {
    {.se = {0}},
    {.se = {MAP_SE(1, WATER, 0), MAP_SE(2, WATER, 0), MAP_SE(3, WATER, 0), MAP_SE(4, WATER, 0)}},
    SOLID(SKY_TILE, SKY),
    SOLID(SKY_TILE + 1, SKY),
    SOLID(SKY_TILE + 2, SKY),
    SOLID(SKY_TILE + 3, SKY),
};

// The screen, in metatiles: the title bar's row, four sky bands, two rows
// of shore (the backdrop) for the gems, three of water.
#define MAP_W 15
#define MAP_H 10
static u16 cells[MAP_W * MAP_H] SERVAL_EWRAM_BSS;
static const MapLayer layer = {.width = MAP_W,
                               .height = MAP_H,
                               .cells = cells,
                               .metatiles = metatiles,
                               .metatile_count = 2 + BANDS,
                               .bg = 3,
                               .flags = MAP_LAYER_FIXED};

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

    // The water: wavy bands of colors 1-4, 2 pixels tall, repeating every 8
    // pixels down and every 16 across, so metatiles side by side and stacked
    // line up.
    static const u8 wave[16] = {0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 2, 2, 1, 1, 0, 0};
    canvas_begin(16, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            canvas_plot(x, y, 1 + (u32)((y + wave[x]) / 2 % BANDS));
    canvas_pack(bg_tiles + 8);
    for (u32 k = 0; k < 8; k++)
        bg_tiles[k] = 0;
    for (u32 band = 0; band < BANDS; band++)
        for (u32 k = 0; k < 8; k++)
            bg_tiles[(SKY_TILE + band) * 8 + k] = 0x11111111u * (band + 1);

    for (int y = 0; y < MAP_H; y++) {
        u16 cell = (u16)(y >= 1 && y <= BANDS ? 1 + y : y >= 7 ? 1 : 0);
        for (int x = 0; x < MAP_W; x++)
            cells[y * MAP_W + x] = cell;
    }
}

// --- The scene ---------------------------------------------------------------------

static u32 tick;
static int flash_left; // frames the right gem stays white

// 0 up to 256 and back down over `period` frames.
static u32 triangle(u32 t, u32 period) {
    u32 phase = t % period, half = period / 2;
    return (phase < half ? phase : period - phase) * 256 / half;
}

static void enter(void) {
    draw_art();
    sprite_table_set(sprites, SPRITE_COUNT);
    sprite_group_load(&pulse_group);
    sprite_group_load(&flash_group);
    tileset_load(&tileset);
    map_load(&layer);
    text_print_centered(1, "SKY: A FADE TO DUSK");
    text_print(8, 11, "< FADE");
    text_print(12, 13, "A: FLASH >");
    text_print_centered(17, "WATER: A PALETTE CYCLE");
    tick = 0;
    flash_left = 0;
}

static void update(void) {
    // The cycle: every 6 frames each water color moves on by one band, so
    // the waves roll up the screen, toward the shore.
    if (tick % 6 == 0) {
        Color cycled[BANDS];
        for (u32 k = 0; k < BANDS; k++)
            cycled[k] = water_colors[(k + tick / 6) % BANDS];
        tileset_set_colors(WATER * 16 + 1, cycled, BANDS);
    }

    // The fades, each color mixed from its own two: the sky and the backdrop
    // toward dusk and back (about 8.5 seconds), the left gem toward white
    // and three quarters of the way (about 2 seconds).
    u32 dusk_amount = triangle(tick, 512);
    Color sky[BANDS + 1];
    for (u32 k = 0; k <= BANDS; k++)
        sky[k] = color_mix(day[k], dusk[k], dusk_amount);
    tileset_set_colors(SKY * 16 + 1, sky, BANDS);
    tileset_set_colors(0, &sky[BANDS], 1); // color 0 of palette 0: the backdrop
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

    sprite_draw(GEM_PULSE, 0, 40, 96, 0);
    sprite_draw(GEM_FLASH, 0, 200, 96, 0);
    tick++;
}

static void leave(void) {
    // The tileset's colors back where they were written (main.c sets the
    // backdrop again; the gems' groups are unloaded, their banks reused).
    tileset_set_colors(16, bg_palettes + 16, 32);
}

const Scene palette_scene = {.name = "PALETTES", .enter = enter, .update = update, .leave = leave};
