// Serval Den's colours, and the pictures' loading. The pictures themselves
// are drawn ahead of time: art_draw.c draws them (the den, the sky, the
// serval at three sizes, the small sprites; see its opening comment) and
// art_gen, a host program, writes the tiles, maps and sprites it drew into
// art_data.c as const data, which the ROM copies to VRAM (art_load()). The
// ROM has none of the drawing code.
//
// To change the art: edit art_draw.c (or the colour indices in
// art_internal.h), then run
//
//     cmake --build --preset host --target den_art_data_refresh
//
// which builds art_gen and rewrites art_data.c and art_data.h, and commit
// all of them. CTest den_art_data fails while the committed data is not what
// art_draw.c draws.
//
// What stays at run time, here: the colours. The pictures are drawn in
// colour indices; which colour each index is depends on the hour (four
// skies and savannas, the den's rock tinted for dawn, dusk and night,
// dimmed with the lights off) and on the serval's coat (its shade, spot
// pattern, eyes, sickness), so they are palette writes, made when either
// changes, from the tables below.

#include "art_data.h"
#include "art_internal.h"

static const MapLayer den_layer = {
    .width = DEN_W,
    .height = MAP_H,
    .cells = art_den_cells,
    .metatiles = art_den_metatiles,
    .metatile_count = DEN_W * MAP_H,
    .bg = DEN_BG,
    .flags = MAP_LAYER_FIXED,
};

static const MapLayer sky_layer = {
    .width = SKY_W,
    .height = MAP_H,
    .cells = art_sky_cells,
    .metatiles = art_sky_metatiles,
    .metatile_count = SKY_W * MAP_H,
    .bg = SKY_BG,
    .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP,
};

// --- Background colours -------------------------------------------------------------------------

static const Color den_colors[15] = {
    COLOR_RGB(34, 22, 20),   COLOR_RGB(78, 56, 48),    COLOR_RGB(112, 82, 66),
    COLOR_RGB(146, 110, 86), COLOR_RGB(184, 148, 116), COLOR_RGB(120, 84, 56),
    COLOR_RGB(156, 116, 78), COLOR_RGB(190, 150, 104), COLOR_RGB(132, 98, 44),
    COLOR_RGB(186, 148, 70), COLOR_RGB(226, 196, 112), COLOR_RGB(58, 38, 28),
    COLOR_RGB(98, 70, 46),   COLOR_RGB(56, 38, 32),    COLOR_RGB(214, 178, 128),
};

static const Color land_colors[TOD_COUNT][LAND_COLORS] = {
    [TOD_DAWN] = {COLOR_RGB(150, 120, 150), COLOR_RGB(118, 92, 120), COLOR_RGB(214, 160, 120),
                  COLOR_RGB(176, 124, 96), COLOR_RGB(126, 88, 78), COLOR_RGB(222, 168, 110),
                  COLOR_RGB(166, 116, 80), COLOR_RGB(102, 70, 60), COLOR_RGB(70, 48, 52),
                  COLOR_RGB(84, 66, 70), COLOR_RGB(120, 92, 90)},
    [TOD_DAY] = {COLOR_RGB(150, 178, 186), COLOR_RGB(118, 146, 140), COLOR_RGB(232, 206, 128),
                 COLOR_RGB(206, 172, 92), COLOR_RGB(160, 128, 66), COLOR_RGB(240, 214, 120),
                 COLOR_RGB(196, 160, 76), COLOR_RGB(136, 106, 52), COLOR_RGB(86, 62, 40),
                 COLOR_RGB(70, 104, 54), COLOR_RGB(108, 146, 70)},
    [TOD_DUSK] = {COLOR_RGB(132, 86, 120), COLOR_RGB(96, 62, 92), COLOR_RGB(220, 140, 84),
                  COLOR_RGB(176, 104, 66), COLOR_RGB(118, 66, 52), COLOR_RGB(232, 150, 80),
                  COLOR_RGB(168, 96, 58), COLOR_RGB(96, 54, 46), COLOR_RGB(52, 32, 38),
                  COLOR_RGB(62, 40, 50), COLOR_RGB(96, 58, 64)},
    [TOD_NIGHT] = {COLOR_RGB(40, 50, 84), COLOR_RGB(30, 38, 66), COLOR_RGB(62, 72, 96),
                   COLOR_RGB(48, 56, 82), COLOR_RGB(34, 40, 64), COLOR_RGB(70, 80, 104),
                   COLOR_RGB(46, 54, 80), COLOR_RGB(26, 30, 52), COLOR_RGB(20, 22, 40),
                   COLOR_RGB(22, 28, 48), COLOR_RGB(34, 42, 66)},
};

static const Color sky_bands[TOD_COUNT][8] = {
    // five bands, then the clouds' light, middle and shade
    [TOD_DAWN] = {COLOR_RGB(64, 72, 128), COLOR_RGB(112, 98, 150), COLOR_RGB(176, 124, 160),
                  COLOR_RGB(236, 156, 138), COLOR_RGB(252, 196, 128), COLOR_RGB(255, 214, 196),
                  COLOR_RGB(234, 164, 160), COLOR_RGB(168, 112, 140)},
    [TOD_DAY] = {COLOR_RGB(70, 132, 214), COLOR_RGB(96, 158, 226), COLOR_RGB(128, 184, 236),
                 COLOR_RGB(164, 208, 240), COLOR_RGB(204, 228, 240), COLOR_RGB(255, 255, 255),
                 COLOR_RGB(226, 236, 246), COLOR_RGB(178, 196, 222)},
    [TOD_DUSK] = {COLOR_RGB(40, 44, 100), COLOR_RGB(88, 62, 128), COLOR_RGB(160, 76, 120),
                  COLOR_RGB(226, 110, 84), COLOR_RGB(248, 166, 72), COLOR_RGB(255, 196, 140),
                  COLOR_RGB(226, 120, 104), COLOR_RGB(132, 70, 100)},
    [TOD_NIGHT] = {COLOR_RGB(8, 12, 36), COLOR_RGB(12, 20, 50), COLOR_RGB(18, 30, 66),
                   COLOR_RGB(26, 42, 82), COLOR_RGB(40, 56, 96), COLOR_RGB(66, 76, 108),
                   COLOR_RGB(48, 58, 88), COLOR_RGB(30, 38, 66)},
};

// The stars by night (bright, dim) and at dawn and dusk (faint, in band 1).
#define STAR_BRIGHT COLOR_RGB(250, 246, 214)
#define STAR_DIM COLOR_RGB(150, 160, 196)

static u16 bg_palettes[BG_BANKS * 16] SERVAL_EWRAM_BSS;

static const Tileset den_tileset = {
    .tiles = art_bg_tiles,
    .tile_count = ART_BG_TILES,
    .palettes = bg_palettes,
    .palette_count = BG_BANKS,
};

int art_time_of_day(int hour) {
    if (hour >= 5 && hour <= 7)
        return TOD_DAWN;
    if (hour >= 8 && hour <= 17)
        return TOD_DAY;
    if (hour >= 18 && hour <= 20)
        return TOD_DUSK;
    return TOD_NIGHT;
}

static void sky_palette(int tod, Color* out) {
    const Color* b = sky_bands[tod];
    for (int i = 0; i < 8; i++)
        out[S_BAND1 - 1 + i] = b[i];
    // Stars: by day the band behind them; at night bright and dim; at dawn
    // and dusk only the bright ones, faint, in the top band.
    out[S_STAR1 - 1] = b[0];
    out[S_STAR2 - 1] = b[1];
    out[S_DIM1 - 1] = b[0];
    out[S_DIM2 - 1] = b[1];
    if (tod == TOD_NIGHT) {
        out[S_STAR1 - 1] = STAR_BRIGHT;
        out[S_STAR2 - 1] = STAR_BRIGHT;
        out[S_DIM1 - 1] = STAR_DIM;
        out[S_DIM2 - 1] = color_mix(STAR_DIM, b[1], 96);
    } else if (tod != TOD_DAY) {
        out[S_STAR1 - 1] = color_mix(STAR_BRIGHT, b[0], 140);
    }
}

// The den's light: by day as drawn, at dawn and dusk warmed, at night cool
// and dim; with the lights off much darker.
static const Color den_tint[TOD_COUNT] = {COLOR_RGB(200, 120, 120), 0, COLOR_RGB(200, 96, 60),
                                          COLOR_RGB(20, 28, 70)};
static const u8 den_tint_amount[TOD_COUNT] = {40, 0, 56, 110};
#define LIGHTS_OFF_TINT COLOR_RGB(6, 8, 26)
#define LIGHTS_OFF_AMOUNT 160

// A colour as the den's light shows it. Sprites (`sprite`) are dimmed a
// little less than the rock, so that the serval stays visible in the dark.
static Color den_light(Color c, int tod, bool lights_off, bool sprite) {
    c = color_mix(c, den_tint[tod], den_tint_amount[tod] * (sprite ? 3u : 4u) / 4);
    if (lights_off)
        c = color_mix(c, LIGHTS_OFF_TINT, sprite ? LIGHTS_OFF_AMOUNT - 56 : LIGHTS_OFF_AMOUNT);
    return c;
}

static void sky_obj_colors(int tod, Color* out);
static const Color item_colors[15];

void art_set_scene(int tod, bool lights_off) {
    Color c[16];
    for (int i = 0; i < 15; i++)
        c[i] = den_light(den_colors[i], tod, lights_off, false);
    tileset_set_colors(BANK_DEN * 16 + 1, c, 15);
    tileset_set_colors(BANK_LAND * 16 + 1, land_colors[tod], LAND_COLORS);
    sky_palette(tod, c);
    tileset_set_colors(BANK_SKY * 16 + 1, c, SKY_COLORS);
    for (int i = 0; i < 15; i++)
        c[1 + i] = den_light(item_colors[i], tod, lights_off, true);
    sprite_set_colors(SPR_DROP, PAL_ITEMS * 16 + 1, c + 1, 15);
    c[0] = 0;
    sky_obj_colors(tod, c);
    sprite_set_colors(SPR_SUN, PAL_SKY_OBJ * 16, c, 16);
}

#define SICK_TINT COLOR_RGB(70, 110, 220)

void art_set_pet(Coat coat, int sick, int tod, bool lights_off) {
    Color c[16];
    art_pet_colors(coat, SICK_TINT, sick == 0 ? 0 : sick == 1 ? 64 : 104, c);
    for (int i = 1; i < 16; i++)
        c[i] = den_light(c[i], tod, lights_off, true);
    sprite_set_colors(SPR_KITTEN, PAL_PET * 16, c, 16);
}

// --- Sprite colours -----------------------------------------------------------------------------

static const SpriteAsset* sprite_table[SPRITE_COUNT];
static Color obj_palettes[OBJ_PALETTES][16] SERVAL_EWRAM_BSS;

static const SpriteGroup sprite_group = {
    .palettes = &obj_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = OBJ_PALETTES,
};

// Shades: pale gold, gold, tawny, dark (light, middle, shade, far).
static const Color coat_shades[4][4] = {
    {COLOR_RGB(252, 232, 178), COLOR_RGB(236, 204, 132), COLOR_RGB(198, 156, 92),
     COLOR_RGB(164, 124, 70)},
    {COLOR_RGB(250, 212, 120), COLOR_RGB(226, 172, 74), COLOR_RGB(178, 122, 44),
     COLOR_RGB(142, 94, 34)},
    {COLOR_RGB(238, 186, 112), COLOR_RGB(204, 138, 70), COLOR_RGB(152, 94, 44),
     COLOR_RGB(116, 70, 34)},
    {COLOR_RGB(214, 158, 92), COLOR_RGB(172, 112, 56), COLOR_RGB(122, 76, 36),
     COLOR_RGB(92, 56, 30)},
};
// The rare melanistic coat (shade 3 with pattern 3): near black, its spots
// a ghost of themselves.
static const Color melanistic[4] = {COLOR_RGB(78, 70, 66), COLOR_RGB(52, 46, 44),
                                    COLOR_RGB(36, 32, 32), COLOR_RGB(28, 24, 24)};
static const Color eye_colors[3][2] = {
    {COLOR_RGB(232, 176, 40), COLOR_RGB(176, 120, 24)}, // amber
    {COLOR_RGB(176, 200, 72), COLOR_RGB(120, 150, 40)}, // green-gold
    {COLOR_RGB(196, 132, 64), COLOR_RGB(140, 88, 40)},  // honey brown
};
// The spot sets each pattern shows: big spots, the neck's stripes, freckles.
static const u8 pattern_sets[4] = {0x3, 0x6, 0x1, 0x7};

void art_pet_colors(Coat coat, Color tint, u32 amount, Color out[16]) {
    bool melan = coat.shade == 3 && coat.spots == 3;
    const Color* s = melan ? melanistic : coat_shades[coat.shade & 3];
    Color spot = melan ? COLOR_RGB(24, 20, 20) : COLOR_RGB(40, 26, 20);
    u8 sets = pattern_sets[coat.spots & 3];
    out[0] = 0;
    out[P_OUTLINE] = COLOR_RGB(30, 20, 16);
    out[P_HI] = s[0];
    out[P_MID] = s[1];
    out[P_SHADE] = s[2];
    out[P_FAR] = s[3];
    out[P_CREAM] = melan ? COLOR_RGB(96, 88, 84) : COLOR_RGB(250, 240, 216);
    out[P_BLACK] = melan ? COLOR_RGB(18, 14, 14) : COLOR_RGB(28, 20, 18);
    out[P_SPOT_A] = sets & 1 ? spot : s[1];
    out[P_SPOT_B] = sets & 2 ? spot : s[1];
    out[P_SPOT_C] = sets & 4 ? spot : s[1];
    out[P_IRIS] = eye_colors[coat.eyes % 3][0];
    out[P_PUPIL] = COLOR_RGB(24, 18, 16);
    out[P_PINK] = melan ? COLOR_RGB(110, 80, 84) : COLOR_RGB(222, 136, 130);
    out[P_EAR] = melan ? COLOR_RGB(120, 108, 104) : COLOR_RGB(246, 214, 190);
    out[P_GLINT] = COLOR_RGB(255, 255, 255);
    for (int i = 1; i < 16; i++)
        out[i] = color_mix(out[i], tint, amount);
}

// PAL_ITEMS's colours, by art_draw.c's item_keys.
static const Color item_colors[15] = {
    // PAL_ITEMS: see item_keys
    COLOR_RGB(36, 24, 20),    // K outline
    COLOR_RGB(84, 56, 30),    // d dropping dark
    COLOR_RGB(122, 84, 44),   // b dropping
    COLOR_RGB(156, 116, 70),  // h dropping light
    COLOR_RGB(150, 76, 48),   // c clay bowl
    COLOR_RGB(196, 108, 66),  // m clay light
    COLOR_RGB(200, 60, 56),   // p meat
    COLOR_RGB(255, 255, 255), // w white
    COLOR_RGB(230, 56, 80),   // r heart red
    COLOR_RGB(255, 150, 170), // q heart light
    COLOR_RGB(110, 108, 112), // g stone dark
    COLOR_RGB(156, 154, 158), // s stone
    COLOR_RGB(200, 198, 196), // t stone light
    COLOR_RGB(250, 214, 70),  // y gold, flowers
    COLOR_RGB(120, 170, 70),  // stem green (key ' ' is unused; index 15)
};

// PAL_LAMP's colours, by art_draw.c's lamp_keys.
static const Color lamp_colors[10] = {
    COLOR_RGB(30, 26, 30),    // K outline
    COLOR_RGB(70, 96, 104),   // g glass shade
    COLOR_RGB(120, 156, 160), // m glass
    COLOR_RGB(210, 236, 230), // h glass glint
    COLOR_RGB(132, 88, 52),   // c cork
    COLOR_RGB(180, 228, 90),  // a glow outer
    COLOR_RGB(236, 255, 140), // f glow
    COLOR_RGB(255, 255, 224), // e glow core
    COLOR_RGB(66, 72, 52),    // b a firefly, dark
    COLOR_RGB(96, 140, 60),   // d dim glow
};

static void build_palettes(void) {
    for (int i = 0; i < 15; i++)
        bg_palettes[BANK_DEN * 16 + 1 + i] = den_colors[i];
    for (int i = 0; i < LAND_COLORS; i++)
        bg_palettes[BANK_LAND * 16 + 1 + i] = land_colors[TOD_DAY][i];
    sky_palette(TOD_DAY, bg_palettes + BANK_SKY * 16 + 1);
    bg_palettes[0] = COLOR_RGB(8, 6, 6);
    Coat c = {1, 0, 0};
    art_pet_colors(c, 0, 0, obj_palettes[PAL_PET]);
    for (int i = 0; i < 15; i++)
        obj_palettes[PAL_ITEMS][1 + i] = item_colors[i];
    for (int i = 0; i < 10; i++)
        obj_palettes[PAL_LAMP][1 + i] = lamp_colors[i];
}

static const Color sun_day[4] = {COLOR_RGB(255, 252, 226), COLOR_RGB(255, 236, 150),
                                 COLOR_RGB(255, 214, 110), COLOR_RGB(255, 200, 90)};
static const Color sun_low[4] = {COLOR_RGB(255, 226, 150), COLOR_RGB(255, 170, 80),
                                 COLOR_RGB(240, 120, 60), COLOR_RGB(220, 90, 60)};
static const Color moon_colors[4] = {COLOR_RGB(240, 240, 220), COLOR_RGB(196, 200, 196),
                                     COLOR_RGB(160, 166, 176), COLOR_RGB(120, 128, 150)};

static void sky_obj_colors(int tod, Color* out) {
    const Color* s = tod == TOD_DAY ? sun_day : sun_low;
    for (int i = 0; i < 4; i++) {
        out[1 + i] = s[i];
        out[5 + i] = moon_colors[i];
    }
}

// --- Loading ------------------------------------------------------------------------------------

void art_load(void) {
    build_palettes();
    for (int i = 0; i < SPRITE_COUNT; i++)
        sprite_table[i] = &art_sprites[i];
    tileset_load(&den_tileset);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    camera_set(0, 0);
    map_load(&den_layer);
    map_load(&sky_layer);
}
