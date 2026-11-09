// The pictures, all drawn at boot (art_build) into EWRAM buffers that
// tileset_load, map_load and sprite_group_load copy to VRAM, as rhythm and
// racer do. No image files.
//
// The cabinet (background 2) is one picture of the whole screen, painted
// with boxes, discs and the 5x7 font into a screen-sized canvas, then cut
// into tiles: a marquee across the top with a row of bulbs, the turntable's
// deck on the left, the control panel on the right (the music's display,
// the meters' lamps, the speed slider's groove, the beat lamps' sockets),
// and the four effect pads at the foot, each a button cap with its letter
// and the effect's name. What moves is its palettes: the trim and the bulbs
// pulse with the beat, each pad has its own bank, which flashes when it
// plays, and each meter its own, a colour a lamp, so lighting a meter is a
// palette write. Where the cabinet is empty (colour 0) the backdrop shows:
// a gradient, one colour a line (raster_backdrop()), which pulses too.
//
// The sprites are drawn the same way, into a canvas cut into tiles: the
// title's letters (a 5x7 font, doubled), the record (its grooves and its
// label, a paw print, so its turning shows), the light on it, the tonearm,
// and the small things show.c moves.
//
// Background tiles are kept once: a tile already made (the empty one, a
// stretch of plain panel) is used again rather than stored twice.

#include "art.h"

#define BLACK COLOR_RGB(0, 0, 0)
#define WHITE COLOR_RGB(255, 255, 255)
#define DUSK COLOR_RGB(16, 8, 34) // the pads darken toward the cabinet's violet

// --- The 5x7 font ---------------------------------------------------------------------------

// The letters the title and the pads use, '#' set.
static const char font_chars[] = "ABCEHIJKLMNOPRSUVX3";
static const char* const font[][7] = {
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // A
    {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}, // B
    {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}, // C
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}, // E
    {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // H
    {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}, // I
    {"..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."}, // J
    {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}, // K
    {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}, // L
    {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}, // M
    {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}, // N
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // O
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}, // P
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}, // R
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."}, // S
    {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // U
    {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}, // V
    {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}, // X
    {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}, // 3
};

// Whether pixel (x, y) of character c's 5x7 cell is set (false outside it,
// and for a character the font lacks).
static bool glyph_pixel(char c, int x, int y) {
    if (x < 0 || x >= 5 || y < 0 || y >= 7)
        return false;
    for (int i = 0; font_chars[i]; i++)
        if (font_chars[i] == c)
            return font[i][y][x] == '#';
    return false;
}

// --- Colours --------------------------------------------------------------------------------

// The cabinet's colours (bank 0).
enum {
    C_OUTLINE = 1,
    C_FACE,
    C_LIGHT,  // the panels' lit top and left edges
    C_SHADOW, // their bottom and right edges
    C_TRIM,   // gold: pulses with the beat
    C_SLOT,   // the display's window, the slider's groove, the lamps' sockets
    C_SLOT_EDGE,
    C_PLATTER,
    C_PLATTER_RIM,
    C_BULB, // 10-13: the marquee's bulbs, every fourth one alike
    C_MARQUEE = C_BULB + 4,
    C_LED_SLOT, // a meter's unlit lamp
};

// A meter's colours (its own bank): the cabinet's 1-4, then its lamps.
#define M_LAMP 5

static const Color cabinet_colors[16] = {
    0,
    COLOR_RGB(10, 6, 22),   // outline
    COLOR_RGB(42, 30, 74),  // face
    COLOR_RGB(74, 56, 124), // light
    COLOR_RGB(24, 16, 46),  // shadow
    COLOR_RGB(192, 138, 48),
    COLOR_RGB(14, 10, 28),  // slot
    COLOR_RGB(84, 66, 130), // slot edge
    COLOR_RGB(22, 18, 34),  // platter
    COLOR_RGB(70, 60, 96),  // its rim
    0,
    0,
    0,
    0,                     // the bulbs: art_bulbs()
    COLOR_RGB(84, 18, 52), // the marquee
    COLOR_RGB(34, 26, 58), // an unlit lamp
};

#define TRIM_REST COLOR_RGB(176, 124, 44)
#define TRIM_PEAK COLOR_RGB(255, 228, 140)
#define BULB_OFF COLOR_RGB(110, 40, 40)
#define BULB_ON COLOR_RGB(255, 214, 120)

// A pad's colours (its own bank): the same roles 1-4 as the cabinet's.
enum { P_CAP = C_TRIM, P_CAP_LIGHT, P_CAP_DARK, P_LETTER, P_NAME, P_LAST = P_NAME };
#define PAD_COLORS P_LAST // colours 1 to P_LAST

static const Color pad_hue[PADS] = {
    COLOR_RGB(242, 194, 48), // coin: gold
    COLOR_RGB(48, 200, 240), // laser: cyan
    COLOR_RGB(255, 106, 48), // boom: orange
    COLOR_RGB(80, 224, 112), // hum: green
};

// The backdrop: top to bottom, at rest; the pulse lifts it toward PEAK.
#define SKY_TOP COLOR_RGB(30, 14, 52)
#define SKY_MID COLOR_RGB(46, 14, 58)
#define SKY_FOOT COLOR_RGB(8, 20, 40)
#define SKY_PEAK COLOR_RGB(110, 40, 120)

static u16 bg_palettes[BG_BANKS * 16] SERVAL_EWRAM_BSS;
static u16 obj_palettes[OBJ_PALETTES * 16] SERVAL_EWRAM_BSS;
static Color pad_flash[PADS][FLASH_LEVELS][PAD_COLORS] SERVAL_EWRAM_BSS;
static Color trim_pulse[PULSE_LEVELS];
static Color backdrop[PULSE_LEVELS][SCREEN_H] SERVAL_EWRAM_BSS;

// From `from` to `to` by `level` of `levels - 1` steps.
static Color stage_mix(Color from, Color to, int level, int levels) {
    return color_mix(from, to, (u32)((level * 256 + (levels - 1) / 2) / (levels - 1)));
}

// A pad's colours at rest and lit, by role (index 0 is colour 1).
static void pad_colors(Color h, Color* rest, Color* peak) {
    rest[C_OUTLINE - 1] = peak[C_OUTLINE - 1] = color_mix(h, DUSK, 236);
    rest[C_FACE - 1] = color_mix(h, DUSK, 176);
    peak[C_FACE - 1] = color_mix(h, DUSK, 90);
    rest[C_LIGHT - 1] = color_mix(h, DUSK, 120);
    peak[C_LIGHT - 1] = color_mix(h, WHITE, 110);
    rest[C_SHADOW - 1] = color_mix(h, DUSK, 210);
    peak[C_SHADOW - 1] = color_mix(h, DUSK, 150);
    rest[P_CAP - 1] = color_mix(h, DUSK, 70);
    peak[P_CAP - 1] = color_mix(h, WHITE, 120);
    rest[P_CAP_LIGHT - 1] = color_mix(h, WHITE, 70);
    peak[P_CAP_LIGHT - 1] = WHITE;
    rest[P_CAP_DARK - 1] = color_mix(h, DUSK, 140);
    peak[P_CAP_DARK - 1] = h;
    rest[P_LETTER - 1] = peak[P_LETTER - 1] = color_mix(h, DUSK, 210);
    rest[P_NAME - 1] = color_mix(h, WHITE, 90);
    peak[P_NAME - 1] = WHITE;
}

// A sprite palette from its colours 1 on.
static void obj_palette(int pal, const Color* colors, int count) {
    for (int c = 0; c < count; c++)
        obj_palettes[pal * 16 + 1 + c] = colors[c];
}

static void build_palettes(void) {
    for (int c = 0; c < 16; c++)
        bg_palettes[BANK_CABINET * 16 + c] = cabinet_colors[c];
    for (int l = 0; l < PULSE_LEVELS; l++) {
        trim_pulse[l] = stage_mix(TRIM_REST, TRIM_PEAK, l, PULSE_LEVELS);
        for (int y = 0; y < SCREEN_H; y++) {
            Color c = y < 80 ? color_mix(SKY_TOP, SKY_MID, (u32)(y * 256 / 80))
                             : color_mix(SKY_MID, SKY_FOOT, (u32)((y - 80) * 256 / 80));
            // The pulse lights the top of the sky most, fading down the screen.
            u32 lift = (u32)(l * (SCREEN_H - y) * 96 / ((PULSE_LEVELS - 1) * SCREEN_H));
            backdrop[l][y] = color_mix(c, SKY_PEAK, lift);
        }
    }
    bg_palettes[C_TRIM] = trim_pulse[0];
    for (int b = 0; b < 4; b++)
        bg_palettes[C_BULB + b] = BULB_OFF;
    for (int p = 0; p < PADS; p++) {
        Color rest[PAD_COLORS], peak[PAD_COLORS];
        pad_colors(pad_hue[p], rest, peak);
        for (int l = 0; l < FLASH_LEVELS; l++)
            for (int c = 0; c < PAD_COLORS; c++)
                pad_flash[p][l][c] = stage_mix(rest[c], peak[c], l, FLASH_LEVELS);
        for (int c = 0; c < PAD_COLORS; c++)
            bg_palettes[(BANK_PAD + p) * 16 + 1 + c] = rest[c];
    }

    static const Color title[] = {
        COLOR_RGB(42, 12, 16),   COLOR_RGB(255, 248, 220), COLOR_RGB(255, 228, 138),
        COLOR_RGB(255, 188, 72), COLOR_RGB(240, 138, 40),  COLOR_RGB(192, 90, 24),
    };
    obj_palette(PAL_TITLE, title, 6);
    // The record: its vinyl (1-5), then its label (6-9), a colour a tune.
    static const Color vinyl[] = {
        COLOR_RGB(6, 6, 10),   COLOR_RGB(20, 20, 28), COLOR_RGB(34, 34, 44),
        COLOR_RGB(54, 52, 66), COLOR_RGB(72, 70, 88),
    };
    static const Color label[TUNE_COUNT][4] = {
        {COLOR_RGB(228, 64, 80), COLOR_RGB(120, 24, 40), COLOR_RGB(255, 240, 208),
         COLOR_RGB(176, 40, 56)},
        {COLOR_RGB(56, 184, 168), COLOR_RGB(20, 90, 84), COLOR_RGB(232, 255, 240),
         COLOR_RGB(36, 136, 124)},
        {COLOR_RGB(120, 118, 144), COLOR_RGB(56, 54, 72), COLOR_RGB(200, 200, 214),
         COLOR_RGB(90, 88, 110)},
    };
    for (int t = 0; t < TUNE_COUNT; t++) {
        obj_palette(PAL_RECORD + t, vinyl, 5);
        for (int c = 0; c < 4; c++)
            obj_palettes[(PAL_RECORD + t) * 16 + 6 + c] = label[t][c];
    }
    static const Color metal[] = {
        COLOR_RGB(12, 10, 20),  COLOR_RGB(232, 232, 244), COLOR_RGB(160, 160, 184),
        COLOR_RGB(84, 82, 104), COLOR_RGB(230, 70, 80),   COLOR_RGB(112, 110, 136),
        COLOR_RGB(66, 64, 84),  COLOR_RGB(255, 214, 96),  COLOR_RGB(180, 120, 30),
    };
    obj_palette(PAL_METAL, metal, 9);
    for (int m = 0; m < 2; m++)
        for (int c = 1; c < M_LAMP; c++)
            bg_palettes[(BANK_METER + m) * 16 + c] = cabinet_colors[c];
    static const Color lamp[] = {
        COLOR_RGB(8, 30, 14),   COLOR_RGB(80, 240, 128), COLOR_RGB(220, 255, 228),
        COLOR_RGB(32, 110, 60), COLOR_RGB(60, 150, 90),
    };
    static const Color lamp_down[] = {
        COLOR_RGB(40, 10, 8),   COLOR_RGB(255, 112, 72), COLOR_RGB(255, 230, 200),
        COLOR_RGB(130, 44, 30), COLOR_RGB(180, 74, 50),
    };
    obj_palette(PAL_LAMP, lamp, 5);
    obj_palette(PAL_LAMP_DOWN, lamp_down, 5);
    // The particles: an outline, the body, its light, its dark, white.
    static const Color fx[PADS][5] = {
        {COLOR_RGB(60, 28, 0), COLOR_RGB(242, 194, 48), COLOR_RGB(255, 244, 160),
         COLOR_RGB(176, 112, 16), WHITE},
        {COLOR_RGB(0, 48, 60), COLOR_RGB(48, 208, 240), COLOR_RGB(184, 246, 255),
         COLOR_RGB(20, 120, 160), WHITE},
        {COLOR_RGB(64, 16, 0), COLOR_RGB(255, 112, 48), COLOR_RGB(255, 208, 96),
         COLOR_RGB(176, 52, 16), WHITE},
        {COLOR_RGB(0, 48, 20), COLOR_RGB(80, 224, 112), COLOR_RGB(192, 255, 208),
         COLOR_RGB(30, 130, 60), WHITE},
    };
    for (int p = 0; p < PADS; p++)
        obj_palette(PAL_COIN + p, fx[p], 5);
}

// --- Painting the cabinet -------------------------------------------------------------------

// The cabinet is painted into a picture of the whole screen, a colour number
// a pixel (in its tile's bank), then cut into tiles.
static u8 screen[SCREEN_H][SCREEN_W] SERVAL_EWRAM_BSS;

static void put_screen(int x, int y, int c) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
        screen[y][x] = (u8)c;
}

// Fills the box from (x0, y0) to (x1, y1), both included.
static void fill(int x0, int y0, int x1, int y1, int c) {
    for (int y = int_max(y0, 0); y <= int_min(y1, SCREEN_H - 1); y++)
        for (int x = int_max(x0, 0); x <= int_min(x1, SCREEN_W - 1); x++)
            screen[y][x] = (u8)c;
}

// A bevelled panel from (x0, y0) to (x1, y1): an outline, lit along its
// top and left, shaded along its bottom and right, its corners cut.
static void paint_panel(int x0, int y0, int x1, int y1) {
    fill(x0, y0, x1, y1, C_OUTLINE);
    fill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, C_LIGHT);
    fill(x0 + 2, y0 + 2, x1 - 1, y1 - 1, C_SHADOW);
    fill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, C_FACE);
    for (int corner = 0; corner < 4; corner++) {
        int sx = corner & 1 ? -1 : 1, sy = corner & 2 ? -1 : 1;
        int cx = corner & 1 ? x1 : x0, cy = corner & 2 ? y1 : y0;
        put_screen(cx, cy, 0);
        put_screen(cx + sx, cy, 0);
        put_screen(cx, cy + sy, 0);
        put_screen(cx + sx, cy + sy, C_OUTLINE);
    }
}

// A disc of colour c: the pixels whose middles lie within sqrt(r2) half
// pixels of (cx2, cy2), all in half pixels (so a middle can lie between
// pixels).
static void paint_disc(int cx2, int cy2, int r2, int c) {
    int r = 0;
    while (r * r < r2)
        r++;
    for (int y = (cy2 - r) / 2 - 1; y <= (cy2 + r) / 2 + 1; y++)
        for (int x = (cx2 - r) / 2 - 1; x <= (cx2 + r) / 2 + 1; x++) {
            int dx = 2 * x + 1 - cx2, dy = 2 * y + 1 - cy2;
            if (dx * dx + dy * dy <= r2)
                put_screen(x, y, c);
        }
}

// Writes s in the 5x7 font centred on column cx from line y0.
static void paint_text(const char* s, int cx, int y0, int c) {
    int n = 0;
    while (s[n])
        n++;
    int x0 = cx - (n * 6 - 1) / 2;
    for (int i = 0; i < n; i++)
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (glyph_pixel(s[i], x, y))
                    put_screen(x0 + i * 6 + x, y0 + y, c);
}

// The integer square root of v (v >= 0), from a guess r: the last pixel's
// root, which is never far from the next one's.
static int isqrt_from(int v, int r) {
    while (r > 0 && r * r > v)
        r--;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}

#define MARQUEE_FOOT 19 // its outline; the trim is the line above

// The marquee: a band across the top with a row of bulbs, every fourth one
// alike, so that lighting one colour at a time makes them chase.
static void paint_marquee(void) {
    fill(0, 0, SCREEN_W - 1, MARQUEE_FOOT, C_MARQUEE);
    fill(0, 0, SCREEN_W - 1, 0, C_OUTLINE);
    fill(0, MARQUEE_FOOT, SCREEN_W - 1, MARQUEE_FOOT, C_OUTLINE);
    fill(0, MARQUEE_FOOT - 1, SCREEN_W - 1, MARQUEE_FOOT - 1, C_TRIM);
    for (int k = 0; k < SCREEN_W / 8; k++)
        fill(3 + k * 8, 1, 5 + k * 8, 3, C_BULB + k % 4);
}

// The deck: the platter under the record, and its speed.
static void paint_deck(void) {
    paint_panel(DECK_LEFT, PANEL_TOP, DECK_RIGHT, PANEL_FOOT);
    // The record's middle is between pixels, as a 64x64 sprite's is.
    int cx2 = 2 * RECORD_X - 1, cy2 = 2 * RECORD_Y - 1;
    paint_disc(cx2, cy2, 4 * 33 * 33, C_PLATTER_RIM);
    paint_disc(cx2, cy2, 4 * 32 * 32, C_PLATTER);
    paint_text("33 RPM", 22, 100, C_TRIM);
}

// The control panel: the music's display, the meters' unlit lamps, the
// speed slider's groove, the divider, the beat lamps' sockets.
static void paint_menu(void) {
    paint_panel(MENU_LEFT, PANEL_TOP, MENU_RIGHT, PANEL_FOOT);
    fill(MENU_LEFT + 4, DIVIDER_Y, MENU_RIGHT - 4, DIVIDER_Y, C_SHADOW);
    fill(MENU_LEFT + 4, DIVIDER_Y + 1, MENU_RIGHT - 4, DIVIDER_Y + 1, C_LIGHT);

    int top = MENU_ROW(ROW_MUSIC) * 8 - 1; // the display, between the text's arrows
    fill(143, top, 200, top + 9, C_SLOT_EDGE);
    fill(143, top, 199, top + 8, C_SLOT);

    for (int r = ROW_VOLUME; r <= ROW_EFFECTS; r += ROW_EFFECTS - ROW_VOLUME) {
        int y0 = MENU_ROW(r) * 8;
        for (int i = 0; i < LEDS; i++) // in the meter's bank: a colour a lamp
            fill(METER_X + i * LED_STEP, y0 + 1, METER_X + i * LED_STEP + 3, y0 + 6, M_LAMP + i);
    }

    int y0 = MENU_ROW(ROW_SPEED) * 8; // a groove, ticks at 50, 100, 150 and 200%
    fill(METER_X, y0 + 3, SPEED_X(200), y0 + 4, C_SLOT);
    fill(METER_X, y0 + 5, SPEED_X(200), y0 + 5, C_SLOT_EDGE);
    for (int p = 50; p <= 200; p += 50) {
        put_screen(SPEED_X(p), y0 + 1, C_SLOT_EDGE);
        put_screen(SPEED_X(p), y0 + 7, C_SLOT_EDGE);
    }

    for (int i = 0; i < LAMPS; i++) { // a lamp's middle is between pixels too
        int cx2 = 2 * (LAMP_X + i * LAMP_STEP) - 1, cy2 = 2 * (NOW_ROW * 8 + 4) - 1;
        paint_disc(cx2, cy2, 81, C_SLOT);
        paint_disc(cx2, cy2, 49, C_LED_SLOT);
    }
}

static const char pad_letter[PADS] = {'A', 'B', 'L', 'R'};
static const char* const pad_name[PADS] = {"COIN", "LASER", "BOOM", "HUM"};

// Pad p, in its own bank: a panel, a round button cap lit from the top left
// with the button's letter, the effect's name under it.
static void paint_pad(int p) {
    paint_panel(PAD_X(p), PAD_TOP, PAD_X(p) + PAD_W - 1, PAD_FOOT);
    int cx = PAD_CAP_X(p), cy = PAD_CAP_Y;
    for (int dy = -8; dy <= 8; dy++)
        for (int dx = -8; dx <= 8; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > 8 * 8 + 3)
                continue;
            int c = d2 > 7 * 7 + 3   ? C_OUTLINE
                    : d2 > 6 * 6 + 3 ? (dx + dy < 0 ? P_CAP_LIGHT : P_CAP_DARK)
                                     : P_CAP;
            if (glyph_pixel(pad_letter[p], dx + 2, dy + 3))
                c = P_LETTER;
            put_screen(cx + dx, cy + dy, c);
        }
    paint_text(pad_name[p], cx, PAD_FOOT - 8, P_NAME);
}

// The pad whose box holds column x, or -1.
static int pad_at(int x) {
    for (int p = 0; p < PADS; p++)
        if (x >= PAD_X(p) && x < PAD_X(p) + PAD_W)
            return p;
    return -1;
}

// --- Background tiles -----------------------------------------------------------------------

// Room for the cabinet's tiles, the empty one included (it takes about
// 160); art_build() logs it if they ever outgrow this.
#define BG_TILE_MAX 512

// Tiles are found again by a key, their words folded into one, whose low
// bits pick one of BG_BUCKETS lists: a list's latest tile (plus 1, 0 for
// none), and each tile's next in its list (likewise).
#define BG_BUCKETS 64
static u32 bg_tiles[BG_TILE_MAX * 8] SERVAL_EWRAM_BSS;
static u32 bg_keys[BG_TILE_MAX] SERVAL_EWRAM_BSS;
static u16 bg_next[BG_TILE_MAX] SERVAL_EWRAM_BSS;
static u16 bg_first[BG_BUCKETS] SERVAL_EWRAM_BSS;
static u16 bg_tiles_used;
static bool bg_tiles_full; // a tile found no room: shown as the empty one

// The tile with these 8 words: an existing one if any is the same, else a
// new one (tile 0, made first, is the empty one).
static u16 add_tile(const u32* words) {
    u32 key = 0;
    for (int i = 0; i < 8; i++)
        key = ((key << 5) | (key >> 27)) ^ words[i];
    u16* first = &bg_first[key % BG_BUCKETS];
    for (int t = *first - 1; t >= 0; t = bg_next[t] - 1) {
        if (bg_keys[t] != key)
            continue;
        const u32* old = bg_tiles + t * 8;
        int i = 0;
        while (i < 8 && old[i] == words[i])
            i++;
        if (i == 8)
            return (u16)t;
    }
    if (bg_tiles_used == BG_TILE_MAX) {
        bg_tiles_full = true;
        return 0;
    }
    u32* out = bg_tiles + bg_tiles_used * 8;
    for (int i = 0; i < 8; i++)
        out[i] = words[i];
    bg_keys[bg_tiles_used] = key;
    bg_next[bg_tiles_used] = *first;
    *first = (u16)(bg_tiles_used + 1);
    return bg_tiles_used++;
}

// The screen entry of the cabinet's 8x8 tile (tx, ty), cut from the
// picture: the pads' tiles in their pad's bank (no tile holds two pads, or
// a pad and the rest of the cabinet).
static u16 cabinet_se(int tx, int ty) {
    int x0 = tx * 8, y0 = ty * 8;
    int p = -1;
    if (y0 + 7 >= PAD_TOP)
        p = pad_at(x0) >= 0 ? pad_at(x0) : pad_at(x0 + 7);
    int bank = p >= 0 ? BANK_PAD + p : BANK_CABINET;
    if (x0 + 7 >= METER_X && x0 < METER_X + LEDS * LED_STEP) {
        if (ty == MENU_ROW(ROW_VOLUME))
            bank = BANK_METER;
        if (ty == MENU_ROW(ROW_EFFECTS))
            bank = BANK_METER + 1;
    }
    u32 words[8];
    for (int row = 0; row < 8; row++) {
        const u8* line = &screen[y0 + row][x0];
        u32 word = 0;
        for (int col = 7; col >= 0; col--)
            word = (word << 4) | line[col];
        words[row] = word;
    }
    return MAP_SE(add_tile(words), bank, 0);
}

#define MAP_W (SCREEN_W / 16)
#define MAP_H (SCREEN_H / 16)
static Metatile metatiles[MAP_W * MAP_H] SERVAL_EWRAM_BSS;
static u16 cells[MAP_W * MAP_H] SERVAL_EWRAM_BSS;

static void build_cabinet(void) {
    fill(0, 0, SCREEN_W - 1, SCREEN_H - 1, 0);
    paint_marquee();
    paint_deck();
    paint_menu();
    for (int p = 0; p < PADS; p++)
        paint_pad(p);

    u32 empty[8] = {0};
    bg_tiles_used = 0;
    for (int b = 0; b < BG_BUCKETS; b++)
        bg_first[b] = 0;
    add_tile(empty);
    for (int my = 0; my < MAP_H; my++)
        for (int mx = 0; mx < MAP_W; mx++) {
            Metatile* m = &metatiles[my * MAP_W + mx];
            for (int i = 0; i < 4; i++)
                m->se[i] = cabinet_se(2 * mx + (i & 1), 2 * my + (i >> 1));
            m->collision = MAP_EMPTY;
            cells[my * MAP_W + mx] = (u16)(my * MAP_W + mx);
        }
}

static const MapLayer cabinet_layer = {
    .width = MAP_W,
    .height = MAP_H,
    .cells = cells,
    .metatiles = metatiles,
    .metatile_count = MAP_W * MAP_H,
    .bg = CABINET_BG,
    .flags = MAP_LAYER_FIXED,
};

// --- Sprites --------------------------------------------------------------------------------

// The canvas sprites are painted in, a colour number a pixel, then cut into
// tiles (pack). As big as the largest frame.
#define CANVAS 64
static u32 canvas_words[CANVAS * CANVAS / 4] SERVAL_EWRAM_BSS;
static u8* const canvas = (u8*)canvas_words;

static void canvas_clear(void) {
    for (int i = 0; i < CANVAS * CANVAS / 4; i++)
        canvas_words[i] = 0;
}

static void put(int x, int y, int c) {
    if (x >= 0 && x < CANVAS && y >= 0 && y < CANVAS)
        canvas[y * CANVAS + x] = (u8)c;
}

// Around everything painted with a colour from 2 up, within the w x h
// block at the canvas's top left: colour 1 on the empty pixels beside it.
static void outline(int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (canvas[y * CANVAS + x] < 2)
                continue;
            for (int ny = int_max(y - 1, 0); ny <= int_min(y + 1, h - 1); ny++)
                for (int nx = int_max(x - 1, 0); nx <= int_min(x + 1, w - 1); nx++)
                    if (!canvas[ny * CANVAS + nx])
                        canvas[ny * CANVAS + nx] = 1;
        }
}

// Cuts the w x h block at the canvas's top left into 4bpp tiles, row by
// row (a sprite frame's layout). Returns the next free word.
static u32* pack(int w, int h, u32* out) {
    for (int ty = 0; ty < h / 8; ty++)
        for (int tx = 0; tx < w / 8; tx++)
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)canvas[(ty * 8 + y) * CANVAS + tx * 8 + x] << (4 * x);
                *out++ = word;
            }
    return out;
}

const char title_letters[] = "SERVALJUKBOX";

// Every frame's tiles: the letters 48, the record 64, the glare 64, the arm
// 64, the cursor, the knob and the wave 1 each, the beat lamps 2, the coin 16, the beam 4, the
// sparks 2.
#define LETTERS ((int)sizeof title_letters - 1)
#define OBJ_TILES (LETTERS * 4 + 64 + 64 + 2 * 32 + 1 + 1 + 2 + COIN_FRAMES * 4 + 4 + 2 + 1)
static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

static SpriteAsset sprites[SPRITE_COUNT] SERVAL_EWRAM_BSS;
static const SpriteAsset* sprite_table[SPRITE_COUNT];

static const SpriteGroup sprite_group = {
    .palettes = obj_palettes,
    .sprite_count = SPRITE_COUNT,
    .palette_count = OBJ_PALETTES,
};

static void sprite(int id, u8 size, int frames, int palette, const u32* tiles, int ox, int oy) {
    sprites[id] = (SpriteAsset){
        .tiles = tiles,
        .size = size,
        .frame_count = (u8)frames,
        .palette_slot = (u8)palette,
        .origin_x = (s8)ox,
        .origin_y = (s8)oy,
    };
}

// The title's letters: the font doubled, gold shading from cream at the top
// to amber at the foot, outlined.
static u32* build_letters(u32* out) {
    static const u8 shade[7] = {2, 2, 3, 3, 4, 5, 6};
    sprite(SPR_LETTER, SPRITE_16x16, LETTERS, PAL_TITLE, out, 0, 0);
    for (int l = 0; l < LETTERS; l++) {
        canvas_clear();
        for (int y = 0; y < 14; y++)
            for (int x = 0; x < 10; x++)
                if (glyph_pixel(title_letters[l], x / 2, y / 2))
                    put(2 + x, 1 + y, shade[y / 2]);
        outline(16, 16);
        out = pack(16, 16, out);
    }
    return out;
}

// Inside the ellipse of middle (cx, cy) and radii (rx, ry).
static bool in_ellipse(int x, int y, int cx, int cy, int rx, int ry) {
    int dx = x - cx, dy = y - cy;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

// The record: black vinyl, fine grooves with two gaps between its tracks, a
// rim, and the label, a paw print over a line of print, so it shows turning.
// Distances in half pixels from its middle, between pixels 31 and 32.
static u32* build_record(u32* out) {
    int root = 0;
    canvas_clear();
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int dx = 2 * x + 1 - 64, dy = 2 * y + 1 - 64;
            root = isqrt_from(dx * dx + dy * dy, root);
            int r = root / 2; // pixels
            int c;
            if (r > 30)
                c = 0;
            else if (r == 30)
                c = 5; // the rim
            else if (r >= 12)
                c = r == 17 || r == 24 ? 4 : r % 2 ? 3 : 2; // grooves, track gaps
            else if (r <= 1)
                c = 1; // the spindle's hole
            else if (r == 11)
                c = 7; // the label's edge
            else if (in_ellipse(dx, dy, 0, 2, 8, 6) || in_ellipse(dx, dy, -8, -7, 3, 3) ||
                     in_ellipse(dx, dy, -3, -13, 3, 3) || in_ellipse(dx, dy, 3, -13, 3, 3) ||
                     in_ellipse(dx, dy, 8, -7, 3, 3))
                c = 8; // the paw
            else if (dy >= 13 && dy <= 15 && int_abs(dx) <= 9)
                c = 9; // the print
            else
                c = 6;
            put(x, y, c);
        }
    sprite(SPR_RECORD, SPRITE_64x64, 1, PAL_RECORD, out, 32, 32);
    return pack(64, 64, out);
}

// The light on the record, which stays still while it turns: two arcs at
// the top left, a fainter pair at the bottom right.
static u32* build_glare(u32* out) {
    int root = 0;
    canvas_clear();
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int dx = 2 * x + 1 - 64, dy = 2 * y + 1 - 64;
            root = isqrt_from(dx * dx + dy * dy, root);
            int r = root / 2;
            int ax = int_abs(dx), ay = int_abs(dy);
            bool diagonal = ay * 5 > ax * 2 && ax * 5 > ay * 2;
            bool arc = r == 14 || r == 15 || (r >= 20 && r <= 22) || r == 26;
            if (!diagonal || !arc)
                continue;
            if (dx < 0 && dy < 0)
                put(x, y, 6);
            else if (dx > 0 && dy > 0 && r >= 20)
                put(x, y, 7);
        }
    sprite(SPR_GLARE, SPRITE_64x64, 1, PAL_METAL, out, 32, 32);
    return pack(64, 64, out);
}

// The squared distance, in 16ths of a pixel squared, from (px, py) to the
// segment (ax, ay)-(bx, by), all in pixels.
static int seg_dist2(int px, int py, int ax, int ay, int bx, int by) {
    int vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    if (px < int_min(ax, bx) - 3 || px > int_max(ax, bx) + 3 || py < int_min(ay, by) - 3 ||
        py > int_max(ay, by) + 3)
        return 16 * 16; // far from it
    int len2 = vx * vx + vy * vy;
    int t = int_clamp((wx * vx + wy * vy) * 256 / len2, 0, 256); // 0-256 along it
    int dx = wx * 16 - vx * t / 16, dy = wy * 16 - vy * t / 16;
    return (dx * dx + dy * dy) / 16;
}

// The tonearm, at rest beside the record or playing on it: a silver tube
// from its pivot, bent once, to the headshell; a counterweight behind the
// pivot. Points in the sprite's pixels: the pivot, the bend, the head.
static u32* build_arm(u32* out) {
    static const s8 path[2][3][2] = {
        {{22, 7}, {24, 42}, {22, 56}}, // ARM_REST
        {{22, 7}, {19, 40}, {9, 55}},  // ARM_PLAY
    };
    sprite(SPR_ARM, SPRITE_32x64, 2, PAL_METAL, out, 0, 0);
    for (int f = 0; f < 2; f++) {
        canvas_clear();
        const s8(*p)[2] = path[f];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 32; x++) {
                int d = int_min(seg_dist2(x, y, p[0][0], p[0][1], p[1][0], p[1][1]),
                                seg_dist2(x, y, p[1][0], p[1][1], p[2][0], p[2][1]));
                if (d <= 16 * 3 / 2)
                    put(x, y, d <= 16 / 2 ? 2 : 3);
                if (in_ellipse(x, y, p[2][0], p[2][1], 3, 3))
                    put(x, y, in_ellipse(x, y, p[2][0] - 1, p[2][1] - 1, 1, 1) ? 2 : 3);
                if (x == p[2][0] && y == p[2][1] + 2)
                    put(x, y, 5); // the stylus's red tip
                // The counterweight, then the pivot's base over the tube.
                if (x >= p[0][0] - 3 && x <= p[0][0] + 3 && y >= 1 && y <= 3)
                    put(x, y, y == 1 ? 3 : 4);
                int bx = x - p[0][0], by = y - p[0][1];
                if (bx * bx + by * by <= 20)
                    put(x, y, bx * bx + by * by <= 4 ? 2 : bx + by < 0 ? 3 : 4);
            }
        outline(32, 64);
        out = pack(32, 64, out);
    }
    return out;
}

// The little ones, from pictures: one character a pixel, '.' empty, a
// digit its colour.
static void picture(const char* const* rows, int h) {
    canvas_clear();
    for (int y = 0; y < h; y++)
        for (int x = 0; rows[y][x]; x++)
            if (rows[y][x] != '.')
                put(x, y, rows[y][x] - '0');
}

static u32* build_small(u32* out) {
    static const char* const cursor[8] = {
        "...88...", "...898..", "...8.98.", "...8..8.",
        "...8....", ".888....", "8898....", ".89.....",
    };
    picture(cursor, 8);
    sprite(SPR_CURSOR, SPRITE_8x8, 1, PAL_METAL, out, 0, 0);
    out = pack(8, 8, out);

    static const char* const knob[8] = {
        ".11111..", "1222221.", "1233321.", "1244421.",
        "1233321.", "1244421.", "1333331.", ".11111..",
    };
    picture(knob, 8);
    sprite(SPR_KNOB, SPRITE_8x8, 1, PAL_METAL, out, 3, 4);
    out = pack(8, 8, out);

    sprite(SPR_LAMP, SPRITE_8x8, 2, PAL_LAMP, out, 4, 4);
    for (int f = 0; f < 2; f++) {
        canvas_clear();
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int dx = 2 * x + 1 - 8, dy = 2 * y + 1 - 8, r2 = dx * dx + dy * dy;
                if (r2 > 44)
                    continue;
                bool shine = r2 <= 18 && dx + dy <= -2;
                put(x, y, r2 > 30 ? 1 : f == LAMP_BRIGHT ? (shine ? 3 : 2) : (shine ? 5 : 4));
            }
        out = pack(8, 8, out);
    }

    static const char* const wave[8] = {
        "........", "........", ".33...3.", "3..3.3..",
        "....3...", "........", "........", "........",
    };
    picture(wave, 8);
    sprite(SPR_WAVE, SPRITE_8x8, 1, PAL_COIN + PAD_HUM, out, 4, 4);
    return pack(8, 8, out);
}

// A coin spinning: its face narrowing to its edge and back, outlined.
static u32* build_coin(u32* out) {
    static const u8 width[COIN_FRAMES] = {11, 7, 2, 7}; // half widths, in half pixels
    sprite(SPR_COIN, SPRITE_16x16, COIN_FRAMES, PAL_COIN, out, 8, 8);
    for (int f = 0; f < COIN_FRAMES; f++) {
        canvas_clear();
        int rx = width[f], ry = 11;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int dx = 2 * x + 1 - 16, dy = 2 * y + 1 - 16;
                if (!in_ellipse(dx, dy, 0, 0, rx, ry))
                    continue;
                int c = 2;
                if (rx > 4 && !in_ellipse(dx, dy, 0, 0, rx - 3, ry - 3))
                    c = 4; // the rim
                if (dx < -rx / 2 && c == 2)
                    c = 3; // the light on its left
                if (rx <= 4)
                    c = dx < 0 ? 3 : 4;
                put(x, y, c);
            }
        outline(16, 16);
        out = pack(16, 16, out);
    }
    return out;
}

// The laser's beam: a white core in a cyan glow, tapering at both ends.
static u32* build_beam(u32* out) {
    canvas_clear();
    for (int x = 0; x < 32; x++) {
        int t = int_min(x, 31 - x);
        for (int y = 0; y < 8; y++) {
            int d = y < 4 ? 3 - y : y - 4; // 0 at the middle lines
            if (d == 0 && t >= 1)
                put(x, y, 5);
            else if (d == 1 && t >= 3)
                put(x, y, 3);
            else if (d == 2 && t >= 6)
                put(x, y, 2);
        }
    }
    sprite(SPR_BEAM, SPRITE_32x8, 1, PAL_COIN + PAD_LASER, out, 16, 4);
    return pack(32, 8, out);
}

// A spark: a four-pointed star with a white heart, big and small.
static u32* build_spark(u32* out) {
    static const char* const spark[2][8] = {
        {"...2....", "...3....", "..333...", "2335332.", "..333...", "...3....", "...2....",
         "........"},
        {"........", "........", "...3....", "..353...", "...3....", "........", "........",
         "........"},
    };
    sprite(SPR_SPARK, SPRITE_8x8, 2, PAL_COIN, out, 3, 3);
    for (int f = 0; f < 2; f++) {
        picture(spark[f], 8);
        out = pack(8, 8, out);
    }
    return out;
}

static void build_sprites(void) {
    u32* out = obj_tiles;
    out = build_letters(out);
    out = build_record(out);
    out = build_glare(out);
    out = build_arm(out);
    out = build_small(out);
    out = build_coin(out);
    out = build_beam(out);
    out = build_spark(out);
    if (out != obj_tiles + OBJ_TILES * 8)
        debug_log("jukebox: OBJ_TILES doesn't match the sprites' tiles (art.c)");
    for (int i = 0; i < SPRITE_COUNT; i++)
        sprite_table[i] = &sprites[i];
}

// --- Building and loading -------------------------------------------------------------------

void art_build(void) {
    build_palettes();
    build_cabinet();
    if (bg_tiles_full)
        debug_log("jukebox: the cabinet needs more tiles than BG_TILE_MAX: raise it (art.c)");
    build_sprites();
}

void art_load(void) {
    Tileset t = {
        .tiles = bg_tiles,
        .tile_count = bg_tiles_used,
        .palettes = bg_palettes,
        .palette_count = BG_BANKS,
    };
    tileset_load(&t);
    map_load(&cabinet_layer);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
}

const Color* art_backdrop(int level) {
    return backdrop[level];
}

void art_pulse(int level) {
    tileset_set_colors(BANK_CABINET * 16 + C_TRIM, &trim_pulse[level], 1);
}

void art_bulbs(int lit, int level) {
    Color c[4];
    Color on = stage_mix(BULB_ON, WHITE, level, PULSE_LEVELS * 2);
    for (int b = 0; b < 4; b++)
        c[b] = b == lit ? on : (b + 2) % 4 == lit ? color_mix(BULB_OFF, BULB_ON, 80) : BULB_OFF;
    tileset_set_colors(BANK_CABINET * 16 + C_BULB, c, 4);
}

void art_meter(int meter, int lit) {
    Color c[LEDS];
    for (int i = 0; i < LEDS; i++) {
        // Green to amber to red along the meter; unlit, a dark trace of it.
        Color on =
            i < 5 ? color_mix(COLOR_RGB(64, 224, 112), COLOR_RGB(200, 230, 80), (u32)(i * 64))
            : i < 8
                ? color_mix(COLOR_RGB(240, 210, 64), COLOR_RGB(255, 150, 60), (u32)((i - 5) * 96))
                : COLOR_RGB(255, 72, 80);
        c[i] = i < lit ? on : color_mix(cabinet_colors[C_LED_SLOT], on, 40);
    }
    tileset_set_colors((BANK_METER + meter) * 16 + M_LAMP, c, LEDS);
}

void art_pad_flash(int pad, int level) {
    tileset_set_colors((BANK_PAD + pad) * 16 + 1, pad_flash[pad][level], PAD_COLORS);
}
