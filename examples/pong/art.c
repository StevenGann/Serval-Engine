// Graphics: every sprite and the court, drawn at boot (art_build) into EWRAM
// buffers that sprite_group_load and tileset_load copy to VRAM. The paddle and
// the ball are ASCII pictures, one character per pixel; the letters are a 5x7
// font blown up to chunky shaded glyphs; the rest is drawn by a few rules.

#include "pong.h"

// --- A canvas to draw on, then cut into tiles --------------------------------

// The court is drawn in EWRAM, which is big; the sprites, at most 32x32, in a
// buffer on the stack, which is in IWRAM and about three times faster, so the
// 50 glyphs don't hold up the boot. Both are boot-time scratch.
static u8 court_canvas[SCREEN_W * SCREEN_H] SERVAL_EWRAM_BSS;
static u8* canvas;
static int canvas_w, canvas_h;

static void canvas_begin(int w, int h) {
    canvas_w = w;
    canvas_h = h;
    for (int i = 0; i < w * h; i++)
        canvas[i] = 0;
}

static u8 get(int x, int y) {
    if (x < 0 || y < 0 || x >= canvas_w || y >= canvas_h)
        return 0;
    return canvas[y * canvas_w + x];
}

static void put(int x, int y, int c) {
    if (x >= 0 && y >= 0 && x < canvas_w && y < canvas_h)
        canvas[y * canvas_w + x] = (u8)c;
}

// The 8 words of the 4bpp tile at tile column tx, row ty of the canvas.
static void cut_tile(int tx, int ty, u32* out) {
    for (int y = 0; y < 8; y++) {
        const u8* row = &canvas[(ty * 8 + y) * canvas_w + tx * 8];
        u32 word = 0;
        for (int x = 0; x < 8; x++)
            word |= (u32)row[x] << (4 * x);
        out[y] = word;
    }
}

// Cuts the canvas into tiles at `out`, row by row (1D mapping, as sprite
// frames are laid out). Returns the end.
static u32* canvas_pack(u32* out) {
    for (int ty = 0; ty < canvas_h / 8; ty++) {
        for (int tx = 0; tx < canvas_w / 8; tx++) {
            cut_tile(tx, ty, out);
            out += 8;
        }
    }
    return out;
}

// --- Sprite colors ---------------------------------------------------------------

// The roles of a palette's colors (pong.h), and the characters that stand for
// them in the ASCII pictures.
enum { C_BASE = 1, C_HIGHLIGHT, C_SHADOW, C_OUTLINE, C_GLOW, C_SHINE };
static const char keys[] = ".BHSKGW";

static const u16 palettes[PALETTE_COUNT][16] = {
    [PAL_PLAYER] = {0, COLOR_RGB(40, 170, 255), COLOR_RGB(150, 225, 255), COLOR_RGB(16, 96, 190),
                    COLOR_RGB(6, 28, 72), COLOR_RGB(40, 140, 255), COLOR_RGB(235, 250, 255)},
    [PAL_CPU] = {0, COLOR_RGB(255, 128, 40), COLOR_RGB(255, 206, 130), COLOR_RGB(190, 66, 16),
                 COLOR_RGB(72, 22, 6), COLOR_RGB(255, 120, 40), COLOR_RGB(255, 248, 225)},
    [PAL_FLASH] = {0, COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(200, 225, 255),
                   COLOR_RGB(110, 130, 160), COLOR_RGB(120, 120, 140), COLOR_RGB(255, 255, 255)},
    [PAL_BALL] = {0, COLOR_RGB(230, 232, 240), COLOR_RGB(255, 255, 255), COLOR_RGB(140, 145, 175),
                  COLOR_RGB(50, 52, 80), COLOR_RGB(90, 90, 120), COLOR_RGB(255, 255, 255)},
    [PAL_GOLD] = {0, COLOR_RGB(255, 205, 70), COLOR_RGB(255, 240, 170), COLOR_RGB(205, 130, 20),
                  COLOR_RGB(70, 36, 8), COLOR_RGB(150, 100, 20), COLOR_RGB(255, 255, 255)},
};

// Draws `count` rows of an ASCII picture at (x, y): each character found in
// `keys` is the color of its place there; '.' draws nothing.
static void draw(const char* const* rows, int count, int x, int y) {
    for (int py = 0; py < count; py++)
        for (int px = 0; rows[py][px]; px++)
            for (int k = 1; keys[k]; k++)
                if (rows[py][px] == keys[k])
                    put(x + px, y + py, k);
}

// --- The paddle and the ball ---------------------------------------------------
//
// Glossy capsules: a shine down the left, a shadow down the right, and three
// grip lines across the paddle's middle.

static const char* const paddle_art[32] = {
    "..KKKK..", ".KWWHHK.", "KHWHBBSK", "KHWBBBSK", "KHWBBBSK", "KHHBBBSK", "KHBBBBSK", "KHBBBBSK",
    "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KSSSSSSK", "KHBBBBSK", "KSSSSSSK",
    "KHBBBBSK", "KSSSSSSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK",
    "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBBSK", "KHBBBSSK", "KBBBSSSK", ".KSSSSK.", "..KKKK..",
};

static const char* const ball_art[8] = {
    "..KKKK..", ".KWHBBK.", "KWHBBBSK", "KHBBBBSK", "KBBBBBSK", "KBBBBSSK", ".KSSSSK.", "..KKKK..",
};

// A paddle's halo, 16x32 with the paddle's 8x32 in the middle: the glow color
// up to 2 pixels from the paddle's sides, the shadow color 2 pixels further.
static void draw_glow(void) {
    canvas_begin(16, 32);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 16; x++) {
            int d = x < 4 ? 4 - x : x - 11;                // pixels from the paddle's side
            int end = y < 3 ? 3 - y : y > 28 ? y - 28 : 0; // and from its straight part
            int d2 = d * d + end * end;
            if (d > 0 && d2 <= 5)
                put(x, y, C_GLOW);
            else if (d > 0 && d2 <= 17)
                put(x, y, C_SHADOW);
        }
    }
}

// A disc of radius r (in pixels) or, with `ring`, a one-pixel circle, in the
// middle of a size x size canvas.
static void draw_circle(int size, int r, bool ring, int color) {
    canvas_begin(size, size);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int dx = 2 * x + 1 - size, dy = 2 * y + 1 - size; // in half pixels
            int d2 = dx * dx + dy * dy;
            if (ring ? (2 * r - 1) * (2 * r - 1) <= d2 && d2 <= (2 * r + 1) * (2 * r + 1)
                     : d2 <= 4 * r * r)
                put(x, y, color);
        }
    }
}

// --- Letters and digits ------------------------------------------------------------
//
// A 5x7 font, drawn at 4 pixels per dot for SPR_BIG (the title and the score)
// and at 2 for SPR_SMALL (messages): blocks with rounded outer corners, a
// highlight along their tops, a shadow along their bottoms, a shine on their
// top-left corners, a dark outline and, on the big ones, a drop shadow.

static const char glyph_chars[] = "0123456789PONGYUWICSAED!";
#define GLYPHS (int)(sizeof glyph_chars - 1)
#define FONT_W 5
#define FONT_H 7

static const char* const font[GLYPHS][FONT_H] = {
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // 0
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}, // 1
    {".###.", "#...#", "....#", "..##.", ".#...", "#....", "#####"}, // 2
    {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}, // 3
    {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}, // 4
    {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}, // 5
    {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}, // 6
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}, // 7
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}, // 8
    {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}, // 9
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}, // P
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // O
    {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}, // N
    {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}, // G
    {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}, // Y
    {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}, // U
    {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}, // W
    {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}, // I
    {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}, // C
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."}, // S
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}, // A
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}, // E
    {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}, // D
    {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}, // !
};

int glyph(char c) {
    for (int i = 0; i < GLYPHS; i++)
        if (glyph_chars[i] == c)
            return i;
    return -1;
}

static bool dot(int g, int cx, int cy) {
    return cx >= 0 && cy >= 0 && cx < FONT_W && cy < FONT_H && font[g][cy][cx] == '#';
}

// Glyph g at `scale` pixels per dot, its top-left at (x0, y0) of a size x size
// canvas.
static void draw_glyph(int g, int scale, int size, int x0, int y0, bool drop_shadow) {
    canvas_begin(size, size);
    for (int cy = 0; cy < FONT_H; cy++) {
        for (int cx = 0; cx < FONT_W; cx++) {
            if (!dot(g, cx, cy))
                continue;
            for (int y = 0; y < scale; y++)
                for (int x = 0; x < scale; x++)
                    put(x0 + cx * scale + x, y0 + cy * scale + y, C_BASE);
            // The corners with no dot beside them nor above or below: round
            // them, or where a dot touches this one only at that corner
            // (the bowl of a P), fill in the joint so the stroke stays whole.
            int bevel = scale / 2; // pixels
            for (int sy = -1; sy <= 1; sy += 2) {
                for (int sx = -1; sx <= 1; sx += 2) {
                    if (dot(g, cx + sx, cy) || dot(g, cx, cy + sy))
                        continue;
                    // The corner's pixel.
                    int px = x0 + cx * scale + (sx < 0 ? 0 : scale - 1);
                    int py = y0 + cy * scale + (sy < 0 ? 0 : scale - 1);
                    bool joint = dot(g, cx + sx, cy + sy);
                    for (int i = 0; i < bevel; i++) {
                        for (int j = 0; i + j < bevel; j++) {
                            if (joint) { // into the two empty cells' corners
                                put(px + sx * (1 + i), py - sy * j, C_BASE);
                                put(px - sx * i, py + sy * (1 + j), C_BASE);
                            } else {
                                put(px - sx * i, py - sy * j, 0);
                            }
                        }
                    }
                }
            }
        }
    }
    // The rest works on rows of bits, a u32 per row (bit x: pixel x), where
    // an edge or an outline is a few shifts and masks for a whole row. Pixel
    // by pixel, the 48 glyphs held the boot up for half a second.
    u32 body[32], grown[32]; // the glyph; and with its outline
    for (int y = 0; y < size; y++) {
        body[y] = 0;
        for (int x = 0; x < size; x++)
            if (get(x, y))
                body[y] |= 1u << x;
    }
    for (int y = 0; y < size; y++) { // grown: the body and its 8 neighbors
        grown[y] = 0;
        for (int dy = -1; dy <= 1; dy++)
            if (y + dy >= 0 && y + dy < size)
                grown[y] |= body[y + dy] | body[y + dy] << 1 | body[y + dy] >> 1;
    }
    // Light from above: highlight on top edges, shine on top-left corners,
    // shadow on bottom edges. Then the dark outline and, on big glyphs, a
    // drop shadow: the outlined glyph again a pixel right and two down.
    for (int y = 0; y < size; y++) {
        u32 m = body[y], above = y > 0 ? body[y - 1] : 0, below = y + 1 < size ? body[y + 1] : 0;
        u32 top = m & ~above, bottom = m & ~below & ~top, shine = top & ~(m << 1);
        u32 dark = grown[y] & ~m;
        if (drop_shadow && y >= 2)
            dark |= (grown[y - 2] << 1) & ~grown[y];
        for (int x = 0; x < size; x++) {
            u32 bit = 1u << x;
            int c = shine & bit    ? C_SHINE
                    : top & bit    ? C_HIGHLIGHT
                    : bottom & bit ? C_SHADOW
                    : m & bit      ? C_BASE
                    : dark & bit   ? C_OUTLINE
                                   : 0;
            put(x, y, c);
        }
    }
}

// --- Sprites ---------------------------------------------------------------------

#define BIG_TILES 16  // 32x32
#define SMALL_TILES 4 // 16x16
#define T_BALL 0
#define T_PADDLE (T_BALL + 1)
#define T_GLOW (T_PADDLE + 4)
#define T_TRAIL (T_GLOW + 8)
#define T_SPARK (T_TRAIL + 3)
#define T_RING (T_SPARK + 3)
#define T_BIG (T_RING + 3 * 4)
#define T_SMALL (T_BIG + GLYPHS * BIG_TILES)
#define OBJ_TILES (T_SMALL + GLYPHS * SMALL_TILES)

static u32 obj_tiles[OBJ_TILES * 8] SERVAL_EWRAM_BSS;

#define PADDLE_SPRITE(palette)                                                                     \
    {.size = SPRITE_8x32, .tiles = obj_tiles + T_PADDLE * 8, .palette_slot = (palette)}

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_BALL] = {.size = SPRITE_8x8, .tiles = obj_tiles + T_BALL * 8, .palette_slot = PAL_BALL},
    [SPR_PADDLE_PLAYER] = PADDLE_SPRITE(PAL_PLAYER),
    [SPR_PADDLE_CPU] = PADDLE_SPRITE(PAL_CPU),
    [SPR_PADDLE_FLASH] = PADDLE_SPRITE(PAL_FLASH),
    [SPR_GLOW] = {.size = SPRITE_16x32, .tiles = obj_tiles + T_GLOW * 8},
    [SPR_TRAIL] = {.size = SPRITE_8x8, .tiles = obj_tiles + T_TRAIL * 8, .frame_count = 3},
    [SPR_SPARK] = {.size = SPRITE_8x8, .tiles = obj_tiles + T_SPARK * 8, .frame_count = 3},
    [SPR_RING] = {.size = SPRITE_16x16,
                  .tiles = obj_tiles + T_RING * 8,
                  .frame_count = 3,
                  .palette_slot = PAL_BALL},
    [SPR_BIG] = {.size = SPRITE_32x32, .tiles = obj_tiles + T_BIG * 8, .frame_count = GLYPHS},
    [SPR_SMALL] = {.size = SPRITE_16x16, .tiles = obj_tiles + T_SMALL * 8, .frame_count = GLYPHS},
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_BALL] = &sprites[SPR_BALL],
    [SPR_PADDLE_PLAYER] = &sprites[SPR_PADDLE_PLAYER],
    [SPR_PADDLE_CPU] = &sprites[SPR_PADDLE_CPU],
    [SPR_PADDLE_FLASH] = &sprites[SPR_PADDLE_FLASH],
    [SPR_GLOW] = &sprites[SPR_GLOW],
    [SPR_TRAIL] = &sprites[SPR_TRAIL],
    [SPR_SPARK] = &sprites[SPR_SPARK],
    [SPR_RING] = &sprites[SPR_RING],
    [SPR_BIG] = &sprites[SPR_BIG],
    [SPR_SMALL] = &sprites[SPR_SMALL],
};

const SpriteGroup pong_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

static void build_sprites(void) {
    u8 sprite_canvas[32 * 32];
    canvas = sprite_canvas;
    u32* out = obj_tiles;
    canvas_begin(8, 8);
    draw(ball_art, 8, 0, 0);
    out = canvas_pack(out);
    canvas_begin(8, 32);
    draw(paddle_art, 32, 0, 0);
    out = canvas_pack(out);
    draw_glow();
    out = canvas_pack(out);
    for (int f = 0; f < 3; f++) { // trail: discs of radius 3, 2, 1
        draw_circle(8, 3 - f, false, C_BASE);
        out = canvas_pack(out);
    }
    for (int f = 0; f < 3; f++) { // spark: a disc with a shining heart, then 2x2, then 1x1
        draw_circle(8, f < 2 ? 2 - f : 0, false, C_BASE);
        if (f == 0)
            for (int i = 0; i < 4; i++)
                put(3 + i % 2, 3 + i / 2, C_SHINE);
        if (f == 2)
            put(3, 3, C_BASE);
        out = canvas_pack(out);
    }
    for (int f = 0; f < 3; f++) { // ring: radius 7, 5, 3
        draw_circle(16, 7 - 2 * f, true, C_HIGHLIGHT);
        out = canvas_pack(out);
    }
    for (int g = 0; g < GLYPHS; g++) {
        draw_glyph(g, 4, 32, 5, 1, true);
        out = canvas_pack(out);
    }
    for (int g = 0; g < GLYPHS; g++) {
        draw_glyph(g, 2, 16, 3, 1, false);
        out = canvas_pack(out);
    }
    canvas = court_canvas; // sprite_canvas ends here
}

// --- The court -----------------------------------------------------------------
//
// Drawn as one 240x160 picture, then cut into 8x8 tiles; tiles that repeat
// (most of the empty floor) are stored once. A metatile per 16x16 block.

enum { C_DOT = 1, C_LINE, C_RAIL, C_RAIL_LIGHT, C_HUD, C_GOAL_PLAYER, C_GOAL_CPU, C_NET };
static const u16 court_colors[16] = {
    0,
    COLOR_RGB(26, 30, 64),    // C_DOT: the floor's grid
    COLOR_RGB(40, 48, 100),   // C_LINE: the center circle
    COLOR_RGB(52, 62, 124),   // C_RAIL
    COLOR_RGB(120, 140, 210), // C_RAIL_LIGHT
    COLOR_RGB(6, 6, 16),      // C_HUD: the band behind the score line
    COLOR_RGB(20, 80, 160),   // C_GOAL_PLAYER: your goal line, left
    COLOR_RGB(160, 64, 20),   // C_GOAL_CPU: the computer's, right
    COLOR_RGB(80, 92, 160),   // C_NET
};

#define COURT_W (SCREEN_W / 16) // metatiles
#define COURT_H (SCREEN_H / 16)
#define COURT_CELLS_X (SCREEN_W / 8) // tiles
#define COURT_CELLS_Y (SCREEN_H / 8)
#define COURT_MAX_TILES (1 + COURT_CELLS_X * COURT_CELLS_Y)

static u32 court_tiles[COURT_MAX_TILES * 8] SERVAL_EWRAM_BSS;
static u32 tile_sums[COURT_MAX_TILES] SERVAL_EWRAM_BSS; // tile 0's: 0
static u16 tile_at[COURT_CELLS_X * COURT_CELLS_Y] SERVAL_EWRAM_BSS;
static Metatile court_metatiles[COURT_W * COURT_H] SERVAL_EWRAM_BSS;
static u16 court_cells[COURT_W * COURT_H] SERVAL_EWRAM_BSS;

Tileset court_tileset = {.tiles = court_tiles, .palettes = court_colors, .palette_count = 1};

const MapLayer court_layer = {
    .width = COURT_W,
    .height = COURT_H,
    .cells = court_cells,
    .metatiles = court_metatiles,
    .metatile_count = COURT_W * COURT_H,
    .bg = 3,
};

static void draw_court(void) {
    canvas = court_canvas;
    canvas_begin(SCREEN_W, SCREEN_H);
    const int top = 8, mid_x = SCREEN_W / 2, mid_y = (top + SCREEN_H) / 2;
    for (int y = 0; y < top; y++) // the score line's band, then the rails, which the ball touches
        for (int x = 0; x < SCREEN_W; x++)
            put(x, y, C_HUD);
    for (int x = 0; x < SCREEN_W; x++) {
        put(x, top, C_RAIL_LIGHT);
        put(x, SCREEN_H - 2, C_RAIL_LIGHT);
        put(x, SCREEN_H - 1, C_RAIL);
    }
    for (int y = top + 8; y < SCREEN_H - 2; y += 16) // a dot every 16 pixels
        for (int x = 8; x < SCREEN_W; x += 16)
            if (x != mid_x - 8 && x != mid_x + 8)
                put(x, y, C_DOT);
    for (int y = top + 1; y < SCREEN_H - 2; y++) { // the goal lines, 2 pixels wide
        put(0, y, C_GOAL_PLAYER);
        put(1, y, C_GOAL_PLAYER);
        put(SCREEN_W - 2, y, C_GOAL_CPU);
        put(SCREEN_W - 1, y, C_GOAL_CPU);
    }
    // The center circle, radius 24, then the net: 2 pixels wide, dashed.
    for (int y = top; y < SCREEN_H - 2; y++) {
        for (int x = mid_x - 26; x < mid_x + 26; x++) {
            int dx = 2 * (x - mid_x) + 1, dy = 2 * (y - mid_y) + 1;
            int d2 = dx * dx + dy * dy;
            if (47 * 47 <= d2 && d2 <= 49 * 49)
                put(x, y, C_LINE);
        }
    }
    for (int y = top + 2; y < SCREEN_H - 2; y++) {
        int c = (y - top - 2) % 8 < 5 ? C_NET : 0;
        put(mid_x - 1, y, c);
        put(mid_x, y, c);
    }
}

// Cuts the court into tiles, each distinct one stored once (tile 0 blank),
// and builds the metatiles.
static void build_court(void) {
    draw_court();
    int count = 1; // court_tiles is zeroed: tile 0 is blank
    for (int ty = 0; ty < COURT_CELLS_Y; ty++) {
        for (int tx = 0; tx < COURT_CELLS_X; tx++) {
            u32* tile = court_tiles + count * 8;
            cut_tile(tx, ty, tile);
            // An equal tile stored already? Its words' sum must be the same:
            // a quick test that rules out most tiles before comparing words.
            u32 sum = 0;
            for (int w = 0; w < 8; w++)
                sum += tile[w] * (2 * w + 1);
            int t = 0;
            while (t < count) {
                int w = 0;
                if (tile_sums[t] == sum)
                    while (w < 8 && court_tiles[t * 8 + w] == tile[w])
                        w++;
                if (w == 8)
                    break;
                t++;
            }
            if (t == count)
                tile_sums[count++] = sum;
            tile_at[ty * COURT_CELLS_X + tx] = (u16)t;
        }
    }
    court_tileset.tile_count = (u16)count;
    for (int my = 0; my < COURT_H; my++) {
        for (int mx = 0; mx < COURT_W; mx++) {
            const u16* t = &tile_at[2 * my * COURT_CELLS_X + 2 * mx];
            Metatile* m = &court_metatiles[my * COURT_W + mx];
            m->se[0] = MAP_SE(t[0], 0, 0);
            m->se[1] = MAP_SE(t[1], 0, 0);
            m->se[2] = MAP_SE(t[COURT_CELLS_X], 0, 0);
            m->se[3] = MAP_SE(t[COURT_CELLS_X + 1], 0, 0);
            m->collision = MAP_EMPTY;
            court_cells[my * COURT_W + mx] = (u16)(my * COURT_W + mx);
        }
    }
}

void art_build(void) {
    build_sprites();
    build_court();
}
