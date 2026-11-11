// Life's colors and sprites, all made at boot: the four themes (the cells'
// 64 colors and the backdrop's gradient, mixed with color_mix()), the
// cursor's rings, the glass of the menu's band and the HUD's strip, and the
// title's letters, built of cells.

#include "life.h"

// --- Themes ---------------------------------------------------------------------
//
// A cell's color is its age: a newborn is the brightest, and the colors cool
// along the stops as it lives on, so what is still or oscillating settles
// into the last stop's quiet color and what is growing glows. A trail is the
// theme's ghost color, fading into the backdrop. The backdrop is a gradient
// from top to bottom, a color per scanline (raster_backdrop()).

typedef struct {
    u8 age; // 0-31: LIFE_BORN + age
    Color color;
} Stop;

typedef struct {
    Stop stops[7];
    Color ghost;               // a trail just after dying, at full strength
    Color top, middle, bottom; // the backdrop
    Color night;               // what the world dims toward behind the title and the menu
    Color accent;              // the HUD's numbers, the menu's values
    bool scanlines;            // every other line of the backdrop darker
} Theme;

const char* const theme_names[LIFE_THEMES] = {"EMBER", "OCEAN", "NEON", "PHOSPHOR"};

static const Theme themes[LIFE_THEMES] = {
    // EMBER: white-hot newborns cooling through amber and rose to violet.
    {.stops = {{0, COLOR_RGB(255, 255, 236)},
               {1, COLOR_RGB(255, 236, 128)},
               {3, COLOR_RGB(255, 184, 56)},
               {6, COLOR_RGB(250, 112, 48)},
               {11, COLOR_RGB(226, 58, 92)},
               {19, COLOR_RGB(172, 48, 140)},
               {31, COLOR_RGB(118, 64, 190)}},
     .ghost = COLOR_RGB(150, 46, 96),
     .top = COLOR_RGB(10, 6, 26),
     .middle = COLOR_RGB(22, 8, 36),
     .bottom = COLOR_RGB(40, 12, 34),
     .night = COLOR_RGB(4, 2, 10),
     .accent = COLOR_RGB(255, 190, 72)},
    // OCEAN: foam to turquoise to the deep blue of still water.
    {.stops = {{0, COLOR_RGB(240, 255, 255)},
               {1, COLOR_RGB(168, 250, 255)},
               {3, COLOR_RGB(80, 228, 236)},
               {6, COLOR_RGB(36, 190, 220)},
               {11, COLOR_RGB(36, 138, 214)},
               {19, COLOR_RGB(52, 96, 204)},
               {31, COLOR_RGB(76, 82, 210)}},
     .ghost = COLOR_RGB(24, 96, 132),
     .top = COLOR_RGB(2, 12, 28),
     .middle = COLOR_RGB(2, 22, 42),
     .bottom = COLOR_RGB(4, 40, 58),
     .night = COLOR_RGB(0, 4, 12),
     .accent = COLOR_RGB(96, 232, 255)},
    // NEON: hot pink fronts, cooling to violet and electric teal.
    {.stops = {{0, COLOR_RGB(255, 246, 255)},
               {1, COLOR_RGB(255, 140, 236)},
               {3, COLOR_RGB(255, 56, 178)},
               {6, COLOR_RGB(204, 52, 255)},
               {11, COLOR_RGB(104, 88, 255)},
               {19, COLOR_RGB(32, 160, 255)},
               {31, COLOR_RGB(0, 206, 196)}},
     .ghost = COLOR_RGB(96, 24, 132),
     .top = COLOR_RGB(4, 2, 12),
     .middle = COLOR_RGB(8, 4, 20),
     .bottom = COLOR_RGB(18, 4, 30),
     .night = COLOR_RGB(0, 0, 4),
     .accent = COLOR_RGB(255, 96, 208)},
    // PHOSPHOR: one green, as an old monitor showed it, with scanlines.
    {.stops = {{0, COLOR_RGB(232, 255, 232)},
               {1, COLOR_RGB(160, 255, 160)},
               {3, COLOR_RGB(88, 244, 104)},
               {6, COLOR_RGB(52, 210, 80)},
               {11, COLOR_RGB(40, 176, 68)},
               {19, COLOR_RGB(32, 146, 58)},
               {31, COLOR_RGB(28, 124, 52)}},
     .ghost = COLOR_RGB(20, 92, 40),
     .top = COLOR_RGB(2, 12, 6),
     .middle = COLOR_RGB(4, 18, 8),
     .bottom = COLOR_RGB(6, 24, 10),
     .night = COLOR_RGB(0, 4, 2),
     .accent = COLOR_RGB(140, 255, 150),
     .scanlines = true},
};

static Color age_color(const Theme* t, u32 age) {
    int i = 0;
    while (i < 6 && t->stops[i + 1].age <= age)
        i++;
    if (i == 6)
        return t->stops[6].color;
    const Stop* a = &t->stops[i];
    const Stop* b = &t->stops[i + 1];
    return color_mix(a->color, b->color, (age - a->age) * 256 / (u32)(b->age - a->age));
}

#define CELL_COLORS 32 // what the surface's colors 0-31 hold besides the cells' 32-63
static Color theme_colors[LIFE_THEMES][LIFE_STATES] SERVAL_EWRAM_BSS;
static Color theme_backdrop[LIFE_THEMES][SCREEN_H] SERVAL_EWRAM_BSS;
static Color shown_colors[LIFE_STATES] SERVAL_EWRAM_BSS;
static Color shown_backdrop[SCREEN_H] SERVAL_EWRAM_BSS;

// A color between a and b, `amount` 256ths of the way, dithered by the line:
// the screen has 32 levels a channel, too few for a dark gradient over 160
// lines, so each line rounds up or down by a 4-line pattern and the steps
// between levels blend.
static Color gradient(Color a, Color b, u32 amount, int y) {
    static const u8 threshold[4] = {32, 160, 96, 224};
    Color out = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        u32 ca = (u32)(a >> shift & 31), cb = (u32)(b >> shift & 31);
        u32 v = ca * (256 - amount) + cb * amount + threshold[y & 3];
        out |= (Color)((v >> 8) << shift);
    }
    return out;
}

// The stamp's cells in the preview: pale, the accent's tint, `strength`
// 256ths of the way from the backdrop.
static Color preview_color(u32 theme, u32 strength) {
    const Theme* t = &themes[theme];
    Color pale = color_mix(t->accent, COLOR_RGB(255, 255, 255), 150);
    return color_mix(t->middle, pale, strength * 220 / 256);
}

static void build_theme(u32 n) {
    const Theme* t = &themes[n];
    Color* c = theme_colors[n];
    for (int y = 0; y < SCREEN_H; y++) {
        Color g = y < SCREEN_H / 2 ? gradient(t->top, t->middle, (u32)y * 256 / (SCREEN_H / 2), y)
                                   : gradient(t->middle, t->bottom,
                                              (u32)(y - SCREEN_H / 2) * 256 / (SCREEN_H / 2), y);
        if (t->scanlines && (y & 1))
            g = color_mix(g, COLOR_RGB(0, 0, 0), 80);
        theme_backdrop[n][y] = g;
    }
    for (int i = 0; i < LIFE_STATES; i++)
        c[i] = 0;
    c[0] = t->middle;
    // Trails: the ghost color, fading into the backdrop's middle.
    for (int v = 1; v <= LIFE_TRAIL; v++)
        c[v] = color_mix(t->middle, t->ghost, (u32)(v * 150 / LIFE_TRAIL));
    for (u32 age = 0; age <= LIFE_OLDEST - LIFE_BORN; age++)
        c[LIFE_BORN + age] = age_color(t, age);
    // The stamp's preview: a see-through newborn, a bright one over a live
    // cell, a dark red where it would clear one, a faint box.
    c[COLOR_PREVIEW] = preview_color(n, 256);
    c[COLOR_PREVIEW_LIVE] = COLOR_RGB(255, 255, 255);
    c[COLOR_PREVIEW_KILL] = color_mix(t->middle, COLOR_RGB(255, 40, 40), 120);
    c[COLOR_PREVIEW_BOX] = color_mix(t->middle, COLOR_RGB(255, 255, 255), 28);
}

static int shown_theme = -1, shown_dim = -1;

void art_preview_pulse(void) {
    if (shown_theme < 0)
        return;
    u32 wave = (u32)(fx_sin((u16)(frame_count() * 1600)) + FX_ONE) * 128 / FX_ONE; // 0-256
    Color c = preview_color((u32)shown_theme, 128 + wave / 2);
    surface_set_colors(COLOR_PREVIEW, &c, 1);
}

void art_show(u32 theme, u32 dim) {
    if ((int)theme == shown_theme && (int)dim == shown_dim)
        return;
    const Theme* t = &themes[theme];
    for (int i = 1; i < LIFE_STATES; i++)
        shown_colors[i] =
            dim ? color_mix(theme_colors[theme][i], t->night, dim) : theme_colors[theme][i];
    for (int y = 0; y < SCREEN_H; y++)
        shown_backdrop[y] =
            dim ? color_mix(theme_backdrop[theme][y], t->night, dim) : theme_backdrop[theme][y];
    surface_set_colors(1, shown_colors + 1, LIFE_STATES - 1);
    raster_backdrop(shown_backdrop);
    if ((int)theme != shown_theme) {
        // The sprites' colors that follow the theme.
        Color accent = t->accent;
        sprite_set_colors(SPR_TEXT, PAL_TEXT * 16 + 3, &accent, 1);
        sprite_set_colors(SPR_TEXT, PAL_PICKED * 16 + 3, &accent, 1);
        Color soft = color_mix(accent, COLOR_RGB(40, 40, 60), 110);
        sprite_set_colors(SPR_TEXT, PAL_SOFT * 16 + 3, &soft, 1);
        Color rule = color_mix(accent, COLOR_RGB(255, 255, 255), 64);
        sprite_set_colors(SPR_TEXT, PAL_GLASS * 16 + 2, &rule, 1);
        Color glass = color_mix(t->night, t->bottom, 96);
        sprite_set_colors(SPR_TEXT, PAL_GLASS * 16 + 1, &glass, 1);
    }
    shown_theme = (int)theme;
    shown_dim = (int)dim;
}

// The colors the logo's cells cycle through: the theme's young ages.
static u32 logo_theme;

// --- Tiles ----------------------------------------------------------------------

#define LOGO_FRAMES LOGO_LETTERS
static u32 cursor_tiles[2 * 8] SERVAL_EWRAM_BSS;
static u32 glass_tiles[2 * 16 * 8] SERVAL_EWRAM_BSS;
static u32 strip_tiles[4 * 8] SERVAL_EWRAM_BSS;
static u32 logo_tiles[LOGO_FRAMES * 32 * 8] SERVAL_EWRAM_BSS;

// Sets pixel (x, y) of a frame `w` pixels wide in `tiles` (4 bits a pixel,
// 8 x 8 tiles row by row) to color `c`.
static void pixel(u32* tiles, int w, int x, int y, u32 c) {
    u32* word = tiles + ((y >> 3) * (w >> 3) + (x >> 3)) * 8 + (y & 7);
    u32 shift = (u32)(x & 7) * 4;
    *word = (*word & ~(15u << shift)) | c << shift;
}

static void ring(u32* tiles, int size) {
    for (int i = 0; i < size; i++) {
        pixel(tiles, 8, i, 0, 1);
        pixel(tiles, 8, i, size - 1, 1);
        pixel(tiles, 8, 0, i, 1);
        pixel(tiles, 8, size - 1, i, 1);
    }
}

// The letters of LIFE in the font's shapes (label.c's, drawn again here as
// cells): a lit cell 5 x 5 pixels with a bright corner, a pixel of shadow
// right and below, one of six colors by a hash of where it is.
static const char* const logo_letters[LOGO_FRAMES][7] = {
    {"#....", "#....", "#....", "#....", "#....", "#....", "#####"},
    {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"},
    {"#####", "#....", "#....", "####.", "#....", "#....", "#...."},
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"},
};
static u8 logo_widths[LOGO_FRAMES];

static void build_logo(void) {
    for (int f = 0; f < LOGO_FRAMES; f++) {
        u32* tiles = logo_tiles + f * 32 * 8;
        int width = 0;
        for (int row = 0; row < 7; row++) {
            for (int col = 0; logo_letters[f][row][col]; col++) {
                if (logo_letters[f][row][col] != '#')
                    continue;
                if (col + 1 > width)
                    width = col + 1;
                u32 shade = 1 + (u32)((row * 7 + col * 3 + f * 5) * 2654435761u >> 29) % 6;
                int x0 = col * 6, y0 = row * 6;
                for (int y = 0; y < 5; y++)
                    for (int x = 0; x < 5; x++)
                        pixel(tiles, 32, x0 + x, y0 + y, (x + y < 2) ? 7 : shade);
                for (int i = 1; i <= 5; i++) {
                    pixel(tiles, 32, x0 + 5, y0 + i, 8);
                    pixel(tiles, 32, x0 + i, y0 + 5, 8);
                }
            }
        }
        logo_widths[f] = (u8)(width * 6);
    }
}

int logo_width(u32 letter) {
    return logo_widths[letter];
}

void art_build(void) {
    for (u32 t = 0; t < LIFE_THEMES; t++)
        build_theme(t);
    ring(cursor_tiles, 4);
    ring(cursor_tiles + 8, 6);
    for (int i = 0; i < 2 * 16 * 8; i++)
        glass_tiles[i] = 0x11111111;
    for (int tx = 0; tx < 4; tx++) {
        u32* edge = glass_tiles + (GLASS_EDGE * 16 + tx) * 8;
        edge[0] = 0x22222222;
        edge[2] = 0x33333333;
    }
    for (int i = 0; i < 4 * 8; i++)
        strip_tiles[i] = 0x11111111;
    for (int tx = 0; tx < 4; tx++)
        strip_tiles[tx * 8] = 0x33333333;
    build_logo();
}

// --- The group ---------------------------------------------------------------------

#define TEXT_COLORS(ink, shade, second) {0, ink, shade, second}

static const u16 palettes[PAL_COUNT][16] = {
    [PAL_TEXT] =
        TEXT_COLORS(COLOR_RGB(236, 238, 250), COLOR_RGB(6, 6, 16), COLOR_RGB(255, 190, 72)),
    [PAL_PICKED] =
        TEXT_COLORS(COLOR_RGB(255, 255, 255), COLOR_RGB(6, 6, 16), COLOR_RGB(255, 190, 72)),
    [PAL_SOFT] =
        TEXT_COLORS(COLOR_RGB(138, 146, 176), COLOR_RGB(6, 6, 16), COLOR_RGB(150, 120, 80)),
    [PAL_PULSE] =
        TEXT_COLORS(COLOR_RGB(255, 255, 255), COLOR_RGB(10, 6, 20), COLOR_RGB(255, 190, 72)),
    [PAL_CURSOR] = {0, COLOR_RGB(255, 255, 255), COLOR_RGB(0, 0, 0)},
    [PAL_GLASS] = {0, COLOR_RGB(6, 4, 16), COLOR_RGB(255, 210, 120), COLOR_RGB(60, 50, 90)},
    [PAL_LOGO] = {0, COLOR_RGB(255, 236, 128), COLOR_RGB(255, 184, 56), COLOR_RGB(250, 112, 48),
                  COLOR_RGB(226, 58, 92), COLOR_RGB(172, 48, 140), COLOR_RGB(118, 64, 190),
                  COLOR_RGB(255, 255, 255), COLOR_RGB(8, 4, 16)},
};

static const SpriteAsset sprite_cursor = {
    .size = SPRITE_8x8, .tiles = cursor_tiles, .frame_count = 2, .palette_slot = PAL_CURSOR};
static SpriteAsset sprite_text = {
    .size = SPRITE_32x8, .frame_count = TEXT_FRAMES, .palette_slot = PAL_TEXT};
static SpriteAsset sprite_big = {
    .size = SPRITE_32x16, .frame_count = BIG_FRAMES, .palette_slot = PAL_PULSE};
static const SpriteAsset sprite_logo = {.size = SPRITE_32x64,
                                        .tiles = logo_tiles,
                                        .frame_count = LOGO_FRAMES,
                                        .palette_slot = PAL_LOGO};
static const SpriteAsset sprite_glass = {
    .size = SPRITE_32x32, .tiles = glass_tiles, .frame_count = 2, .palette_slot = PAL_GLASS};
static const SpriteAsset sprite_strip = {
    .size = SPRITE_32x8, .tiles = strip_tiles, .palette_slot = PAL_GLASS};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_CURSOR] = &sprite_cursor, [SPR_TEXT] = &sprite_text,   [SPR_BIG] = &sprite_big,
    [SPR_LOGO] = &sprite_logo,     [SPR_GLASS] = &sprite_glass, [SPR_STRIP] = &sprite_strip,
};

// 510 of the 512 tiles: text 304, big text 40, the logo 128, the glass 32,
// the strip 4, the cursor 2.
static const SpriteGroup group = {
    .palettes = &palettes[0][0], .sprite_count = SPRITE_COUNT, .palette_count = PAL_COUNT};

void art_load(void) {
    sprite_text.tiles = label_tiles(SPR_TEXT);
    sprite_big.tiles = label_tiles(SPR_BIG);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&group);
    labels_loaded();
    shown_theme = -1; // the theme's sprite colors again
    shown_dim = -1;
}

// --- Every frame -----------------------------------------------------------------

void art_frame(void) {
    u32 t = frame_count();
    // The cursor's ring: white, breathing toward the theme's accent.
    const Theme* th = &themes[shown_theme < 0 ? 0 : shown_theme];
    u32 wave = (u32)(fx_sin((u16)(t * 1100)) + FX_ONE) * 100 / (2 * FX_ONE);
    Color ring_color = color_mix(COLOR_RGB(255, 255, 255), th->accent, wave);
    sprite_set_colors(SPR_CURSOR, PAL_CURSOR * 16 + 1, &ring_color, 1);
    // PRESS START: its ink pulsing.
    u32 pulse = (u32)(fx_sin((u16)(t * 900)) + FX_ONE) * 128 / (2 * FX_ONE);
    Color ink = color_mix(COLOR_RGB(255, 255, 255), th->accent, pulse + 40);
    sprite_set_colors(SPR_BIG, PAL_PULSE * 16 + 1, &ink, 1);
    // The logo's cells: the theme's young ages, drifting from cell to cell.
    if (logo_theme != (u32)shown_theme || (t & 7) == 0) {
        logo_theme = (u32)shown_theme;
        Color c[6];
        static const u8 ages[6] = {1, 3, 5, 8, 12, 17};
        for (u32 i = 0; i < 6; i++)
            c[i] = age_color(th, ages[(i + t / 8) % 6]);
        sprite_set_colors(SPR_LOGO, PAL_LOGO * 16 + 1, c, 6);
        Color shadow = color_mix(th->night, th->bottom, 64);
        sprite_set_colors(SPR_LOGO, PAL_LOGO * 16 + 8, &shadow, 1);
    }
}
