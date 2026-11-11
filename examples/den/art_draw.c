// Serval Den's pictures, drawn by rule on the host (art_draw(), run by
// art_gen, which writes what it draws into art_data.c: see art.c's opening
// comment for how to change the art). No image files; nothing here is in
// the ROM. (Divisions are kept out of the pixel loops: they once ran at
// boot on the GBA.)
//
// The den is one 240x160 picture: a rock shelter of boulders (each lit from
// the den's mouth, cracks between them, a rim of light round the mouth),
// roots hanging from its roof, one holding the firefly jar, and a sandy floor
// with pebbles, loose grass and a woven grass nest. Through the arched mouth:
// the savanna, far hills, a flat-topped acacia and tall grass, in a palette
// bank of its own (the lights don't dim the world outside). The mouth's lower
// sides are straight along tile edges so that no tile mixes the two banks.
// Behind it, on its own background, the sky: a banded gradient, clouds and
// stars. Stars are drawn in colours that match the band behind them by day,
// so one picture serves every hour: a palette per time of day shows them.
//
// The serval is drawn by rule from a skeleton (hip, shoulder, head, paws) and
// a pose per frame: ellipses and capsules filled with shaded coat colours,
// then spots, the head's face, and a dark outline round the silhouette. The
// three sizes share the poses; each has its own proportions (the kitten all
// head and ears, the adult long in the leg). Spots are three sets drawn in
// three colours; a coat's pattern shows some sets in black and hides the
// others in the coat colour, so every coat uses the same tiles.

#include "art_internal.h"

// --- Small helpers ----------------------------------------------------------------------------

static u32 hash(u32 a, u32 b) {
    u32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0xC2B2AE3Du;
    h ^= h >> 13;
    return h;
}

// A hash scaled to 0..n-1 by a multiply (Thumb code has no fast division,
// not even by a constant: picture loops must not divide).
static int pick(u32 h, int n) {
    return (int)(((h >> 16) * (u32)n) >> 16);
}

// Ordered dithering: 0-15 for each pixel of a 4x4 tile.
static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static int dither(int x, int y) {
    return bayer[y & 3][x & 3];
}

// --- Background tiles -------------------------------------------------------------------------

#define BG_TILE_MAX 1024

static u32 bg_tiles[BG_TILE_MAX * 8];
static u16 bg_tiles_used;
static u16 tile_head[256]; // hash -> first tile (0xFFFF none)
static u16 tile_next[BG_TILE_MAX];

static u32 tile_hash(const u32* w) {
    u32 h = 0;
    for (int i = 0; i < 8; i++)
        h = (h ^ w[i]) * 0x9E3779B1u;
    return h >> 24;
}

// The tile with these 8 words: an existing one if any is the same, else a
// new one (tile 0 is the blank one, made first).
static u16 add_tile(const u32* words) {
    u32 h = tile_hash(words);
    for (u16 t = tile_head[h]; t != 0xFFFF; t = tile_next[t]) {
        const u32* old = bg_tiles + t * 8;
        int i = 0;
        while (i < 8 && old[i] == words[i])
            i++;
        if (i == 8)
            return t;
    }
    if (bg_tiles_used == BG_TILE_MAX)
        return 0; // never with these pictures (far fewer tiles)
    u16 t = bg_tiles_used++;
    u32* out = bg_tiles + t * 8;
    for (int i = 0; i < 8; i++)
        out[i] = words[i];
    tile_next[t] = tile_head[h];
    tile_head[h] = t;
    return t;
}

// A picture: one byte a pixel, bank * 16 + colour (0: transparent).
#define PIC_W 256
#define PIC_H 160
static u8 pic[PIC_H][PIC_W];

static void pic_clear(void) {
    for (int y = 0; y < PIC_H; y++)
        for (int x = 0; x < PIC_W; x++)
            pic[y][x] = 0;
}

// One 8x8 tile of the picture as a screen entry: its bank is the bank of
// its pixels (the pictures never mix two in a tile).
static u16 pic_entry(int x0, int y0) {
    u32 words[8];
    int bank = 0;
    for (int y = 0; y < 8; y++) {
        u32 word = 0;
        for (int x = 0; x < 8; x++) {
            int v = pic[y0 + y][x0 + x];
            if (v)
                bank = v >> 4;
            word |= (u32)(v & 15) << (4 * x);
        }
        words[y] = word;
    }
    return MAP_SE(add_tile(words), bank, 0);
}

static Metatile den_metatiles[DEN_W * MAP_H];
static u16 den_cells[DEN_W * MAP_H];
static Metatile sky_metatiles[SKY_W * MAP_H];
static u16 sky_cells[SKY_W * MAP_H];

static void cut_map(Metatile* metatiles, u16* cells, int w) {
    for (int cy = 0; cy < MAP_H; cy++)
        for (int cx = 0; cx < w; cx++) {
            Metatile m = {.collision = MAP_EMPTY};
            for (int q = 0; q < 4; q++)
                m.se[q] = pic_entry(cx * 16 + (q & 1) * 8, cy * 16 + (q >> 1) * 8);
            metatiles[cy * w + cx] = m;
            cells[cy * w + cx] = (u16)(cy * w + cx);
        }
}

// --- Background colours -------------------------------------------------------------------------

// --- The sky's picture --------------------------------------------------------------------------

#define SKY_Y0 16 // the sky's bands, from SKY_Y0 to MOUTH_Y1
#define BANDS 5

// The band (0-4) at each row of the sky and each of 4 dither columns, made
// once: the picture loops call no division (debug builds are unoptimized).
static u8 band_at[MOUTH_Y1][4];

static void make_bands(void) {
    for (int y = 0; y < MOUTH_Y1; y++)
        for (int x = 0; x < 4; x++) {
            int v = (y - SKY_Y0) * BANDS * 16 / (MOUTH_Y1 - SKY_Y0); // sixteenths of a band
            if (v < 0)
                v = 0;
            int band = v >> 4, frac = v & 15;
            // Each band blends into the next over its last half, by dithering.
            if (frac >= 8 && dither(x, y) < (frac - 8) * 2)
                band++;
            band_at[y][x] = (u8)(band < BANDS ? band : BANDS - 1);
        }
}

static int sky_band(int x, int y) {
    if (y < 0)
        y = 0;
    if (y >= MOUTH_Y1)
        y = MOUTH_Y1 - 1;
    return band_at[y][x & 3];
}

// A cloud: puffs (circles) on a flat base, lit from above.
typedef struct {
    s16 x, y; // the base's left end
    u8 w;
} Cloud;

static const Cloud clouds[] = {
    {8, 40, 34}, {70, 30, 22}, {120, 46, 40}, {190, 36, 28}, {232, 52, 20}};

static void put_sky(int x, int y, int c) {
    if (y >= SKY_Y0 && y < MOUTH_Y1)
        pic[y][x & (PIC_W - 1)] = (u8)(BANK_SKY * 16 + c);
}

static void paint_clouds(void) {
    for (unsigned i = 0; i < sizeof clouds / sizeof clouds[0]; i++) {
        const Cloud* c = &clouds[i];
        // the flat base, shaded
        for (int dx = 2; dx < c->w - 1; dx++) {
            put_sky(c->x + dx, c->y, S_CLOUD_SHADE);
            put_sky(c->x + dx, c->y - 1, S_CLOUD_SHADE);
        }
        // puffs along the base, biggest in the middle; a lighter shade
        // replaces a darker one where puffs overlap
        int mid = c->w / 2;
        for (int p = 0; p < c->w; p += 5) {
            int r = 3 + (mid - (p > mid ? p - mid : mid - p)) / 3 + (int)(hash(i, (u32)p) % 3);
            int px = c->x + p + 2, py = c->y - r / 2;
            for (int ey = -r; ey <= r; ey++)
                for (int ex = -r; ex <= r; ex++) {
                    if (ex * ex + ey * ey > r * r || py + ey > c->y)
                        continue;
                    int shade = ey * 3 > r ? 2 : (ey * 3 < -r ? 0 : 1);
                    int x = (px + ex) & (PIC_W - 1), y = py + ey;
                    int old = pic[y][x] & 15;
                    if (old >= S_CLOUD_LIGHT && old <= S_CLOUD_SHADE &&
                        old - S_CLOUD_LIGHT <= shade)
                        continue;
                    put_sky(x, y, S_CLOUD_LIGHT + shade);
                }
        }
    }
}

static void paint_sky(void) {
    make_bands();
    for (int y = SKY_Y0; y < MOUTH_Y1; y++)
        for (int x = 0; x < PIC_W; x++) {
            int band = sky_band(x, y);
            int c = S_BAND1 + band;
            if (band < 2 && sky_band(x, y + 1) == band && sky_band(x, y - 1) == band) {
                u32 h = hash((u32)x * 7 + 1, (u32)y * 13 + 5) >> 22; // 0-1023
                if (h < 6)
                    c = band == 0 ? S_STAR1 : S_STAR2;
                else if (h < 22)
                    c = band == 0 ? S_DIM1 : S_DIM2;
            }
            pic[y][x] = (u8)(BANK_SKY * 16 + c);
        }
    paint_clouds();
}

// --- The den's picture --------------------------------------------------------------------------

#define DEN_PIC_W 240
#define BACK_FLOOR 96 // where the back wall meets the floor

// The den's shapes, made once into tables (the picture loops call no
// division): the mouth, the pillars of rock at the sides, the back wall's
// foot, and the rock's beds.
static u8 mouth_bits[MOUTH_Y1][DEN_PIC_W / 8];
static u8 pil_left[PIC_H];
static u8 pil_right[PIC_H];
static u8 foot[DEN_PIC_W];
static s8 bend[DEN_PIC_W]; // how far the beds dip at each column
#define BED_H 13
#define BEDS 16
static u8 bed_lip[BEDS];
#define BED_ROWS (PIC_H + 16)
static u8 bed_of[BED_ROWS]; // the bed and the row in it of each row (with its bend)
static u8 bed_pos[BED_ROWS];
static u8 joint[BEDS][DEN_PIC_W]; // 0: a joint, 1: its lit edge, 2: its shadowed
                                  // edge, 3: none

// Grit: a 32x32 tile of noise, -8 to 7, made once (cheaper than a hash a
// pixel); and the squares of each column's and row's distance from the
// light at the mouth.
static s8 grit[32][32];
static s32 light_dx2[DEN_PIC_W];
static s32 light_dy2[PIC_H];

static bool in_mouth(int x, int y) {
    if (y < 0 || y >= MOUTH_Y1 || x < MOUTH_X0 || x >= MOUTH_X1)
        return false;
    return (mouth_bits[y][x >> 3] >> (x & 7)) & 1;
}

// Smooth noise along x: hash values at knots `step` apart, joined straight.
static int smooth(int x, int step, u32 seed, int amp) {
    int k = x / step, f = x % step;
    int a = (int)(hash((u32)k, seed) % (u32)(2 * amp + 1)) - amp;
    int b = (int)(hash((u32)k + 1, seed) % (u32)(2 * amp + 1)) - amp;
    return a + (b - a) * f / step;
}

// Every table is made with as few divisions as can be (each a library call
// in Thumb code).
static void make_shapes(void) {
    for (int x = MOUTH_X0; x < MOUTH_X1; x++) {
        // The arch: an ellipse over the opening, its edge broken by the rock.
        int dx = x - (MOUTH_X0 + MOUTH_X1) / 2;
        const int rx = (MOUTH_X1 - MOUTH_X0) / 2, ry = 56 - MOUTH_TOP;
        int r = rx * ry - (pick(hash((u32)(x / 3), 77), 5) - 2) * 12;
        for (int y = MOUTH_TOP - 2; y < MOUTH_Y1; y++) {
            int dy = y < 56 ? y - 56 : 0;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= r * r || y >= 56)
                mouth_bits[y][x >> 3] |= (u8)(1 << (x & 7));
        }
    }
    for (int y = 0; y < PIC_H; y++) {
        int in = y > 100 ? (y - 100) / 12 : 0;
        pil_left[y] = (u8)(10 + in + (int)(hash((u32)(y / 5), 9) % 3));
        pil_right[y] = (u8)(228 - in - (int)(hash((u32)(y / 5), 11) % 3));
    }
    for (int x = 0; x < DEN_PIC_W; x++) {
        foot[x] = (u8)(BACK_FLOOR + (int)(hash((u32)(x / 6), 3) % 3));
        bend[x] = (s8)(smooth(x, 24, 7, 4) + smooth(x, 9, 8, 1));
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            grit[y][x] = (s8)((int)(hash((u32)x * 5, (u32)y * 3) >> 28) - 8);
    for (int x = 0; x < DEN_PIC_W; x++)
        light_dx2[x] = (x - 160) * (x - 160);
    for (int y = 0; y < PIC_H; y++)
        light_dy2[y] = (y - 64) * (y - 64) * 4;
    for (int y = 0, b = 0, pos = 0; y < BED_ROWS; y++) {
        bed_of[y] = (u8)b;
        bed_pos[y] = (u8)pos;
        if (++pos == BED_H) {
            pos = 0;
            b++;
        }
    }
    for (int b = 0; b < BEDS; b++) {
        // each bed sticks out a little more or less: its lip is deeper
        bed_lip[b] = (u8)(BED_H - 3 - (int)(hash((u32)b, 21) % 3));
        // joints: a vertical crack every 30-69 pixels, staggered by bed
        int off = (int)(hash((u32)b, 31) % 40), span = 30 + (int)(hash((u32)b, 33) % 40);
        int j = off % span;
        for (int x = 0; x < DEN_PIC_W; x++) {
            joint[b][x] = (u8)(j == 0 ? 0 : j == 1 ? 1 : j == span - 1 ? 2 : 3);
            if (++j == span)
                j = 0;
        }
    }
}

static bool is_rock(int x, int y) {
    if (in_mouth(x, y))
        return false;
    if (x < pil_left[y] || x >= pil_right[y])
        return true;
    return y < (x >= MOUTH_X0 && x < MOUTH_X1 ? MOUTH_Y1 : foot[x]);
}

// How much light reaches (x, y): from the mouth, falling off with distance.
static int light_at(int x, int y) {
    int l = 70 - (((light_dx2[x] + light_dy2[y]) * 252) >> 16); // squared distance / 260
    return l < -10 ? -10 : l;
}

// The rock: layered sandstone. Beds of uneven thickness run across, each
// lit on its upper face and shadowed under its lip, split by joints.
static int rock_pixel(int x, int y, int bed, int pos) {
    int lip = bed_lip[bed], j = joint[bed][x];
    if (pos == 0 || (j == 0 && pos < lip))
        return D_CRACK;
    int v = light_at(x, y) - 22;
    if (pos == 1)
        v += 34; // the bed's top edge catches the light
    else if (pos == 2)
        v += 16;
    else if (pos >= lip)
        v -= 40; // under the lip
    else
        v += 6 - pos * 2;
    if (j == 1)
        v += 12; // a joint's lit edge
    else if (j == 2)
        v -= 12;
    // the near pillars and the roof are darker than the back wall
    if (x < pil_left[y] + 4 || x >= pil_right[y] - 4 || y < 14)
        v -= 18;
    v += grit[y & 31][x & 31] * 3 / 4; // grit
    v += dither(x, y) * 2 - 15;
    if (v < -34)
        return D_CRACK;
    if (v < -8)
        return D_ROCK_DARK;
    if (v < 18)
        return D_ROCK;
    if (v < 42)
        return D_ROCK_LIGHT;
    return D_ROCK_GLINT;
}

static void put_den(int x, int y, int c) {
    if (x >= 0 && x < DEN_PIC_W && y >= 0 && y < PIC_H)
        pic[y][x] = (u8)(BANK_DEN * 16 + c);
}

static int floor_pixel(int x, int y) {
    // Light from the mouth across the floor, darker toward the front and the walls.
    int dx = x - 160, dy = (y - BACK_FLOOR) * 3;
    int l = 72 - (((dx * dx + dy * dy) * 298) >> 16) + dither(x, y) * 2 - 15; // d / 220
    if (y < BACK_FLOOR + 4)
        l -= 30 - (y - BACK_FLOOR) * 6; // the wall's shadow at its foot
    if (x < pil_left[y] + 6 || x >= pil_right[y] - 6)
        l -= 26;
    // a little grain
    if (grit[(y + 7) & 31][(x * 3) & 31] == -8 && ((x ^ y) & 1))
        l -= 30;
    if (l < -20)
        return D_EARTH_DARK;
    if (l < 22)
        return D_EARTH;
    if (l < 52)
        return D_EARTH_LIGHT;
    return D_SAND;
}

// A short straight stroke of one colour.
static void stroke(int x0, int y0, int x1, int y1, int c) {
    int dx = x1 - x0, dy = y1 - y0;
    int n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (n == 0) {
        put_den(x0, y0, c);
        return;
    }
    for (int i = 0; i <= n; i++)
        put_den(x0 + (dx * i + (dx > 0 ? n / 2 : -n / 2)) / n,
                y0 + (dy * i + (dy > 0 ? n / 2 : -n / 2)) / n, c);
}

static void pebble(int cx, int cy, int rx, int ry) {
    for (int y = cy - ry; y <= cy + ry; y++)
        for (int x = cx - rx; x <= cx + rx; x++) {
            int ex = (x - cx) * ry, ey = (y - cy) * rx;
            if (ex * ex + ey * ey > rx * rx * ry * ry)
                continue;
            int c = (y - cy) < 0 && (x - cx) <= 0 ? D_ROCK_GLINT
                    : (y - cy) >= ry - 1          ? D_ROCK_DARK
                                                  : D_ROCK_LIGHT;
            put_den(x, y, c);
        }
    // its shadow on the ground
    for (int x = cx - rx + 1; x <= cx + rx + 1; x++)
        put_den(x, cy + ry + 1, D_EARTH_DARK);
}

// The nest: a ring of woven grass round a dark hollow.
#define NEST_RX 30
#define NEST_RY 11

static void paint_nest(void) {
    int cx = NEST_X, cy = NEST_Y + 2;
    // the hollow and a bed of grass inside it
    for (int y = cy - NEST_RY; y <= cy + NEST_RY; y++)
        for (int x = cx - NEST_RX; x <= cx + NEST_RX; x++) {
            int ex = (x - cx) * NEST_RY, ey = (y - cy) * NEST_RX;
            if (ex * ex + ey * ey <= NEST_RX * NEST_RX * NEST_RY * NEST_RY)
                put_den(x, y, D_HOLLOW);
        }
    // loose grass lining the hollow
    for (int i = 0; i < 40; i++) {
        u32 h = hash((u32)i, 505);
        int x = cx - NEST_RX + 8 + (int)(h % (NEST_RX * 2 - 16));
        int y = cy - NEST_RY / 2 + (int)((h >> 8) % NEST_RY);
        stroke(x, y, x + 2 + (int)((h >> 16) % 3), y + (int)((h >> 20) % 3) - 1,
               (h >> 24) % 3 ? D_STRAW_DARK : D_STRAW);
    }
    // the woven rim: strokes round an ellipse, each its own slant and colour
    for (int i = 0; i < 160; i++) {
        u32 h = hash((u32)i, 404);
        int a = (int)(h % 256);
        // a point on the ring (a 64-step circle from a quarter-wave table)
        static const s8 sine[17] = {0, 6, 12, 18, 23, 27, 30, 32, 32, 32, 30, 27, 23, 18, 12, 6, 0};
        int q = a & 63, s = q < 32 ? sine[q / 2] : -sine[(q - 32) / 2];
        int c2 =
            ((a + 16) & 63) < 32 ? sine[((a + 16) & 63) / 2] : -sine[(((a + 16) & 63) - 32) / 2];
        int ring = 26 + (int)((h >> 8) % 6);
        int x = cx + c2 * ring / 32, y = cy + s * (ring * NEST_RY / NEST_RX) / 32 + 1;
        int len = 3 + (int)((h >> 12) % 4), slant = (int)((h >> 16) % 5) - 2;
        // the front of the rim is lit, the back in shadow
        int col = s > 8    ? ((h >> 20) % 3 == 0 ? D_STRAW_LIGHT : D_STRAW)
                  : s < -8 ? ((h >> 20) % 2 ? D_STRAW_DARK : D_STRAW)
                           : ((h >> 20) % 3 == 0 ? D_STRAW_DARK : D_STRAW);
        stroke(x - len / 2, y + slant / 2, x + len / 2, y - slant / 2, col);
    }
    // shadow under the front of the nest
    for (int x = cx - NEST_RX + 6; x <= cx + NEST_RX - 6; x++)
        put_den(x, cy + NEST_RY + 2, D_EARTH_DARK);
}

// A root hanging from the roof: a wavy line, thicker at the top.
static void root(int x0, int y0, int len, int phase) {
    static const s8 wave[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int i = 0; i < len; i++) {
        int x = x0 + wave[(i / 3 + phase) & 7];
        put_den(x, y0 + i, D_ROOT);
        if (i < len / 2)
            put_den(x + 1, y0 + i, D_ROOT_DARK);
    }
    put_den(x0 + wave[((len - 1) / 3 + phase) & 7], y0 + len, D_ROOT_DARK);
}

// The savanna seen through the mouth (BANK_LAND).
static void put_land(int x, int y, int c) {
    if (x >= MOUTH_X0 && x < MOUTH_X1 && y >= 0 && y < MOUTH_Y1)
        pic[y][x] = (u8)(BANK_LAND * 16 + c);
}

#define HORIZON 82

static void paint_savanna(void) {
    // Far hills: two soft rises.
    for (int x = MOUTH_X0; x < MOUTH_X1; x++) {
        int a = x - 146, b = x - 186;
        int h1 = 10 - a * a / 60, h2 = 7 - b * b / 50;
        int top = HORIZON - (h1 > h2 ? h1 : h2);
        if (top > HORIZON - 2)
            top = HORIZON - 2 + (int)(hash((u32)x, 5) % 2);
        for (int y = top; y < HORIZON + 2; y++)
            put_land(x, y, (y - top) < 2 ? L_HILL_FAR : L_HILL);
    }
    // The plain, banded toward the den.
    for (int y = HORIZON + 2; y < MOUTH_Y1; y++)
        for (int x = MOUTH_X0; x < MOUTH_X1; x++) {
            int v = (y - HORIZON) * 16 / (MOUTH_Y1 - HORIZON) + (dither(x, y) - 8) / 3;
            put_land(x, y, v < 5 ? L_PLAIN_LIGHT : v < 11 ? L_PLAIN : L_PLAIN_DARK);
        }
    // The acacia: a flat crown on a crooked trunk.
    int tx = 176;
    for (int y = 66; y <= HORIZON + 4; y++) {
        int x = tx + (y < 74 ? (74 - y) / 3 : 0);
        put_land(x, y, L_TRUNK);
        if (y > 76)
            put_land(x + 1, y, L_TRUNK);
    }
    // branches up to the crown
    for (int i = 0; i < 6; i++) {
        put_land(tx - 2 - i, 70 - i / 2, L_TRUNK);
        put_land(tx + 3 + i, 69 - i / 2, L_TRUNK);
    }
    for (int y = 58; y <= 67; y++)
        for (int x = 160; x < 196; x++) {
            int dx = x - 178, dy = y - 63;
            // a flat-topped, wide umbrella with a ragged edge
            int r =
                17 - (dy < 0 ? -dy : dy) * (dy < 0 ? 3 : 2) / 2 + (int)(hash((u32)x, (u32)y) % 3);
            if ((dx < 0 ? -dx : dx) > r)
                continue;
            put_land(x, y, dy <= -2 ? L_CANOPY_LIGHT : L_CANOPY);
        }
    // Tall grass in front, its blades swaying a little.
    for (int i = 0; i < 70; i++) {
        u32 h = hash((u32)i, 911);
        int x = MOUTH_X0 + (int)(h % (MOUTH_X1 - MOUTH_X0));
        int len = 5 + (int)((h >> 8) % 9);
        int lean = (int)((h >> 16) % 3) - 1;
        for (int k = 0; k < len; k++)
            put_land(x + lean * k / 4, MOUTH_Y1 - 1 - k,
                     k > len - 3 ? L_GRASS_LIGHT : (k < 3 ? L_GRASS_DARK : L_GRASS));
    }
}

static void paint_den(void) {
    make_shapes();
    for (int y = 0; y < PIC_H; y++)
        for (int x = 0; x < DEN_PIC_W; x++) {
            if (in_mouth(x, y))
                continue;
            if (is_rock(x, y)) {
                int yy = y + bend[x] + 8; // 8: keeps it positive
                put_den(x, y, rock_pixel(x, y, bed_of[yy], bed_pos[yy]));
            } else {
                put_den(x, y, floor_pixel(x, y));
            }
        }
    // A rim of light round the mouth, and its shadowed lower edge.
    for (int y = 1; y < MOUTH_Y1; y++)
        for (int x = MOUTH_X0 - 4; x < MOUTH_X1 + 4; x++) {
            if (in_mouth(x, y))
                continue;
            bool beside = in_mouth(x + 1, y) || in_mouth(x - 1, y) || in_mouth(x, y + 1);
            if (beside)
                put_den(x, y, in_mouth(x, y + 1) ? D_ROCK_GLINT : D_ROCK_LIGHT);
            else if (in_mouth(x, y - 1))
                put_den(x, y, D_CRACK);
        }
    // The threshold: the floor under the mouth catches the light.
    for (int x = MOUTH_X0; x < MOUTH_X1; x++)
        for (int y = MOUTH_Y1; y < MOUTH_Y1 + 3; y++)
            put_den(x, y, y == MOUTH_Y1 ? D_EARTH_LIGHT : D_SAND);
    // Pebbles and loose grass on the floor.
    for (int i = 0; i < 26; i++) {
        u32 h = hash((u32)i, 1234);
        int x = 18 + (int)(h % 204), y = 102 + (int)((h >> 8) % 54);
        int dx = x - NEST_X, dy = (y - NEST_Y) * 3;
        if (dx * dx + dy * dy < 40 * 40)
            continue; // not in the nest
        if (i % 3 == 0)
            pebble(x, y, 1 + (int)((h >> 16) % 3), 1 + (int)((h >> 20) % 2));
        else
            stroke(x, y, x + 2 + (int)((h >> 16) % 3), y - 1 + (int)((h >> 20) % 3),
                   (h >> 24) & 1 ? D_STRAW : D_STRAW_DARK);
    }
    paint_nest();
    // Roots from the roof; the second holds the firefly jar.
    root(70, 14, 14, 0);
    root(LAMP_X + 7, 10, LAMP_Y - 10 + 2, 3);
    root(98, 12, 22, 5);
    root(214, 16, 10, 2);
    root(110, 10, 9, 6);
}

// --- Sprite drawing -----------------------------------------------------------------------------

#define OBJ_TILE_MAX 720
static u32 obj_tiles[OBJ_TILE_MAX * 8];
static SpriteAsset sprites[SPRITE_COUNT];

// A 32x32 scratch canvas for one frame, and which part drew each pixel.
#define CW 32
static u8 canvas[CW][CW];
static u8 part_of[CW][CW];

static void canvas_clear(void) {
    for (int y = 0; y < CW; y++)
        for (int x = 0; x < CW; x++)
            canvas[y][x] = part_of[y][x] = 0;
}

static void put(int x, int y, int c) {
    if (x >= 0 && x < CW && y >= 0 && y < CW)
        canvas[y][x] = (u8)c;
}

static int get(int x, int y) {
    return x >= 0 && x < CW && y >= 0 && y < CW ? canvas[y][x] : 0;
}

// Cuts the w x h block at the canvas's top-left into 4bpp tiles, row by row
// (a sprite frame's layout). Returns the next free word.
static u32* pack(int w, int h, u32* out) {
    for (int ty = 0; ty < h / 8; ty++)
        for (int tx = 0; tx < w / 8; tx++)
            for (int y = 0; y < 8; y++) {
                u32 word = 0;
                for (int x = 0; x < 8; x++)
                    word |= (u32)canvas[ty * 8 + y][tx * 8 + x] << (4 * x);
                *out++ = word;
            }
    return out;
}

static void sprite(int id, u8 size, int frames, int palette, const u32* tiles) {
    sprites[id] = (SpriteAsset){
        .tiles = tiles,
        .size = size,
        .frame_count = (u8)frames,
        .palette_slot = (u8)palette,
    };
}

// Outlines what is drawn in the w x h block with colour `c`: every empty
// pixel beside (not diagonally) a drawn one.
static void outline(int w, int h, int c) {
    static u8 drawn[CW][CW];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            drawn[y][x] = canvas[y][x] != 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (drawn[y][x])
                continue;
            if ((x > 0 && drawn[y][x - 1]) || (x < w - 1 && drawn[y][x + 1]) ||
                (y > 0 && drawn[y - 1][x]) || (y < h - 1 && drawn[y + 1][x]))
                canvas[y][x] = (u8)c;
        }
}

// ASCII pictures: one character a pixel, '.' transparent, the others keys
// into a palette string.
static void paint(const char* const* rows, int h, const char* keys, int x0, int y0) {
    for (int y = 0; y < h; y++)
        for (int x = 0; rows[y][x]; x++) {
            char ch = rows[y][x];
            if (ch == '.')
                continue;
            for (int k = 1; keys[k]; k++)
                if (keys[k] == ch)
                    put(x0 + x, y0 + y, k);
        }
}

// --- The serval ---------------------------------------------------------------------------------

// Coordinates in sixteenths of a pixel (Q4).
#define Q(x) ((int)((x) * 16))

// The parts of the serval, for its spots: each part's spots are placed from
// its own anchor, so they move with it from frame to frame.
enum { PART_NONE, PART_BODY, PART_NECK, PART_HEAD, PART_LEG, PART_TAIL, PART_EAR, PARTS };
static int anchor_x[PARTS], anchor_y[PARTS];

// How a filled shape colours its pixels.
enum {
    K_COAT,  // shaded by height in the shape: light on top, shade below
    K_BELLY, // as K_COAT, cream underneath
    K_FAR,   // far side, in shadow
    K_CREAM,
    K_BLACK,
    K_EAR,      // the near ear: black back with a white spot, pale inside
    K_EAR_FAR,  // the far ear: its black back and white spot
    K_EAR_DOWN, // an ear laid back: black, pointing behind
    K_EAR_RING, // a dark line round the near ear where it covers the far one
    K_LEG,      // a near leg: shade at the back edge
    K_TAIL,     // coat with black rings and tip
    K_MUZZLE,   // coat on top, cream below
};

static int shade_by(int ny, int kind) {
    // ny: -256 (top) to 256 (bottom) within the shape
    switch (kind) {
    case K_FAR:
        return P_FAR;
    case K_CREAM:
        return P_CREAM;
    case K_BLACK:
        return P_BLACK;
    case K_BELLY:
        if (ny > 120)
            return P_CREAM;
        // fall through
    case K_COAT:
        return ny < -130 ? P_HI : ny > 110 ? P_SHADE : P_MID;
    case K_MUZZLE:
        return ny > -20 ? P_CREAM : P_MID;
    default:
        return P_MID;
    }
}

static void plot(int x, int y, int c, int part) {
    if (x >= 0 && x < CW && y >= 0 && y < CW) {
        canvas[y][x] = (u8)c;
        part_of[y][x] = (u8)part;
    }
}

// A filled ellipse at (cx, cy) with radii rx, ry (Q4).
static void ellipse(int cx, int cy, int rx, int ry, int kind, int part) {
    if (rx < 4 || ry < 4)
        return;
    int irx = (256 << 12) / rx, iry = (256 << 12) / ry;
    for (int y = (cy - ry) >> 4; y <= (cy + ry) >> 4; y++)
        for (int x = (cx - rx) >> 4; x <= (cx + rx) >> 4; x++) {
            int nx = ((x * 16 + 8 - cx) * irx) >> 12, ny = ((y * 16 + 8 - cy) * iry) >> 12;
            if (nx * nx + ny * ny > 65536)
                continue;
            // Ears taper to a rounded point: upward, or backward laid down.
            if ((kind == K_EAR || kind == K_EAR_FAR || kind == K_EAR_RING) &&
                (nx < 0 ? -nx : nx) > 90 + (ny + 256) * 166 / 512)
                continue;
            if (kind == K_EAR_DOWN && (ny < 0 ? -ny : ny) > 90 + (nx + 256) * 166 / 512)
                continue;
            int c;
            if (kind == K_EAR_RING) {
                if (x >= 0 && x < CW && y >= 0 && y < CW && part_of[y][x] == PART_EAR)
                    plot(x, y, P_OUTLINE, PART_HEAD);
                continue;
            }
            if (kind == K_EAR_DOWN) {
                plot(x, y, nx < -60 ? P_BLACK : P_MID, part);
                continue;
            }
            if (kind == K_EAR || kind == K_EAR_FAR) {
                // the back (left) of the ear black, a white spot in it
                if (kind == K_EAR_FAR) {
                    bool spot = nx > -110 && nx < 110 && ny > -60 && ny < 110;
                    c = spot ? P_CREAM : P_BLACK;
                } else if (nx < -70) {
                    c = P_BLACK; // the back's edge, seen past the rim
                } else if (nx > 110) {
                    c = ny < -150 ? P_BLACK : P_MID; // the rim, its tip dark
                } else {
                    c = ny > 150 ? P_MID : P_EAR;
                }
            } else {
                c = shade_by(ny, kind);
            }
            plot(x, y, c, part);
        }
}

// A capsule: the points within r of the segment (x0, y0)-(x1, y1) (Q4).
static void capsule(int x0, int y0, int x1, int y1, int r, int kind, int part) {
    int dx = x1 - x0, dy = y1 - y0;
    int len2 = dx * dx + dy * dy;
    s32 inv = len2 ? (256 << 16) / len2 : 0; // one division, not one a pixel
    int r3 = r / 3, r2 = r / 2;
    int minx = (x0 < x1 ? x0 : x1) - r, maxx = (x0 > x1 ? x0 : x1) + r;
    int miny = (y0 < y1 ? y0 : y1) - r, maxy = (y0 > y1 ? y0 : y1) + r;
    for (int y = miny >> 4; y <= maxy >> 4; y++)
        for (int x = minx >> 4; x <= maxx >> 4; x++) {
            int px = x * 16 + 8 - x0, py = y * 16 + 8 - y0;
            // 0-256 along; clamped first, so dot * inv < 2^24 (no 64-bit
            // multiply, a library call in Thumb code)
            int dot = px * dx + py * dy;
            int t = dot <= 0 ? 0 : dot >= len2 ? 256 : (dot * inv) >> 16;
            int ex = px - ((dx * t) >> 8), ey = py - ((dy * t) >> 8);
            if (ex * ex + ey * ey > r * r)
                continue;
            int c;
            if (kind == K_TAIL) {
                bool ring = r > Q(1) && ((t > 110 && t < 140) || (t > 160 && t < 182));
                c = (t > (r > Q(1) ? 200 : 170) || ring) ? P_BLACK : ey < -r3 ? P_HI : P_MID;
            } else if (kind == K_LEG) {
                c = ex < -r3 ? P_SHADE : P_MID; // the back edge in shade
            } else if (kind == K_COAT) {
                c = ey < -r2 ? P_HI : ey > r2 ? P_SHADE : P_MID;
            } else {
                c = shade_by(0, kind);
            }
            plot(x, y, c, part);
        }
}

// The spots: three sets, made once into a 64x64 field (bit 0 big spots on
// a staggered grid, bit 1 the neck's stripes, bit 2 freckles) that each part
// reads at its own offset, so the drawing loops call no division.
#define FIELD 64
static u8 spot_field[FIELD][FIELD];

static void make_spot_field(void) {
    enum { CELL_W = 5, CELL_H = 4 };
    // freckles, one pixel in seven
    for (int y = 0; y < FIELD; y++)
        for (int x = 0; x < FIELD; x++)
            spot_field[y][x] = (hash((u32)x * 3, (u32)y * 5) >> 16) < 9363 ? 4 : 0;
    // big spots and stripes, cell by cell on a staggered grid
    for (int gy = 0; gy * CELL_H < FIELD; gy++)
        for (int gx = 0; gx * CELL_W < FIELD + CELL_W; gx++) {
            u32 h = hash((u32)gx, (u32)gy);
            int x0 = gx * CELL_W - (gy & 1) * (CELL_W / 2), y0 = gy * CELL_H;
            bool stripes = pick(h << 12, 3) != 0;
            for (int y = y0; y < y0 + CELL_H; y++) {
                int third = 0, k = y; // y / 3 without dividing
                while (k >= 3) {
                    k -= 3;
                    third++;
                }
                for (int x = x0; x < x0 + CELL_W; x++) {
                    if (x < 0 || x >= FIELD || !stripes)
                        continue;
                    int m = x + third;
                    while (m >= 3)
                        m -= 3;
                    if (m == 0)
                        spot_field[y][x] |= 2;
                }
            }
            int sx = x0 + 1 + pick(h, CELL_W - 2), sy = y0 + pick(h << 8, CELL_H - 1);
            bool big = ((h >> 16) & 3) != 0;
            for (int j = 0; j < 4; j++) {
                int x = sx + (j & 1), y = sy + (j >> 1);
                if ((j == 0 || big) && x >= 0 && x < FIELD && y < FIELD)
                    spot_field[y][x] |= 1;
            }
        }
}

// Spots on the coat pixels of the body, neck and legs.
static void spots(void) {
    for (int y = 0; y < CW; y++)
        for (int x = 0; x < CW; x++) {
            int c = canvas[y][x], part = part_of[y][x];
            if ((c != P_HI && c != P_MID && c != P_SHADE && c != P_FAR) || part == PART_NONE ||
                part == PART_TAIL || part == PART_HEAD || part == PART_EAR)
                continue;
            int lx = x - (anchor_x[part] >> 4), ly = y - (anchor_y[part] >> 4);
            int v = spot_field[(ly + part * 13) & (FIELD - 1)][(lx + part * 29) & (FIELD - 1)];
            if (v & 1)
                plot(x, y, P_SPOT_A, part);
            else if ((v & 2) && (part == PART_NECK || (part == PART_BODY && lx >= 2)))
                plot(x, y, P_SPOT_B, part); // stripes down the neck and shoulders
            else if (v & 4)
                plot(x, y, P_SPOT_C, part);
        }
}

// Proportions of each size (Q4), and its scale for the poses (adult 16).
typedef struct {
    u8 size;    // 16 or 32
    u8 scale;   // pose offsets are pixels at the adult's size, times scale / 16
    s16 ground; // the paws' lower edge
    s16 hip_x, hip_y, sh_x, sh_y;
    s16 haunch_r, chest_rx, chest_ry, belly_rx, belly_ry;
    s16 leg_r, paw_r;
    s16 head_dx, head_dy; // the head's middle from the shoulder
    s16 head_rx, head_ry, muzzle_dx, muzzle_dy, muzzle_rx, muzzle_ry;
    s16 ear_rx, ear_ry, ear_dx, ear_dy, far_ear_dx; // near ear's middle from the head's
    s16 tail_len, tail_r;
} Build;

static const Build builds[3] = {
    // kitten: a big head and ears on a small round body, short legs
    {16,     7,      Q(15),  Q(4.6), Q(9.4), Q(8.2),  Q(9.6),  Q(2.2), Q(2.3), Q(2.4),
     Q(3.0), Q(1.8), Q(0.8), Q(0.9), Q(2.6), Q(-3.6), Q(3.0),  Q(2.8), Q(1.9), Q(1.0),
     Q(1.3), Q(1.0), Q(1.4), Q(2.2), Q(0.6), Q(-3.4), Q(-1.8), Q(2.8), Q(0.7)},
    // adolescent: lanky, the head still large
    {32,     13,     Q(31),  Q(10.5), Q(19.5), Q(19.5), Q(20),   Q(3.6), Q(3.7), Q(4.0),
     Q(5.4), Q(3.0), Q(1.1), Q(1.25), Q(4.8),  Q(-7.0), Q(3.5),  Q(3.1), Q(2.9), Q(1.2),
     Q(1.8), Q(1.4), Q(1.5), Q(3.6),  Q(0.9),  Q(-4.8), Q(-2.4), Q(5.5), Q(1.1)},
    // adult: long legs, a long neck, a small head under very large ears
    {32,     16,     Q(31),   Q(9.5), Q(16),  Q(20),   Q(16.5), Q(4.3), Q(4.2), Q(4.6),
     Q(6.8), Q(3.4), Q(1.25), Q(1.4), Q(5.4), Q(-6.6), Q(3.5),  Q(3.0), Q(3.1), Q(1.2),
     Q(2.0), Q(1.5), Q(1.6),  Q(3.6), Q(0.9), Q(-4.9), Q(-2.6), Q(7.0), Q(1.2)},
};

enum { EARS_UP, EARS_DOWN };
enum { EYES_OPEN, EYES_SHUT, EYES_HAPPY, EYES_SLEEP };
enum { POSE_STAND, POSE_SIT, POSE_LIE, POSE_CURL };

typedef struct {
    u8 kind;
    s8 fx[4];   // paws' offsets forward: front near, front far, hind near, hind far
    s8 lift[4]; // paws' lift off the ground
    s8 body_dy; // the body up (-) or down
    s8 head_dx, head_dy;
    s8 tail_dx, tail_dy; // the tail's tip from its rest
    u8 ears, eyes;
    u8 mouth; // 1: open (eating)
} Pose;

static const Pose poses[PET_FRAMES] = {
    [PF_STAND] = {POSE_STAND, {0, -1, 0, 1}, {0}, 0, 0, 0, 0, 0, EARS_UP, EYES_OPEN, 0},
    [PF_STAND_TAIL] = {POSE_STAND, {0, -1, 0, 1}, {0}, 0, 0, 0, -2, -3, EARS_UP, EYES_OPEN, 0},
    [PF_BLINK] = {POSE_STAND, {0, -1, 0, 1}, {0}, 0, 0, 0, 0, 0, EARS_UP, EYES_SHUT, 0},
    [PF_WALK0] = {POSE_STAND, {3, -2, -2, 3}, {0}, 0, 1, 0, 0, 0, EARS_UP, EYES_OPEN, 0},
    [PF_WALK1] = {POSE_STAND, {0, 1, 1, 0}, {0, 2, 2, 0}, -1, 1, -1, 1, -1, EARS_UP, EYES_OPEN, 0},
    [PF_WALK2] = {POSE_STAND, {-2, 3, 3, -2}, {0}, 0, 1, 0, 0, 0, EARS_UP, EYES_OPEN, 0},
    [PF_WALK3] = {POSE_STAND, {1, 0, 0, 1}, {2, 0, 0, 2}, -1, 1, -1, 1, -1, EARS_UP, EYES_OPEN, 0},
    [PF_HAPPY] = {POSE_STAND, {0, -1, 0, 1}, {0}, 0, 0, -1, -2, -4, EARS_UP, EYES_HAPPY, 0},
    [PF_EAT0] = {POSE_STAND, {1, 0, 0, 1}, {0}, 0, 2, 9, 0, 0, EARS_UP, EYES_SHUT, 0},
    [PF_EAT1] = {POSE_STAND, {1, 0, 0, 1}, {0}, 0, 2, 8, 0, 0, EARS_UP, EYES_SHUT, 1},
    [PF_SIT] = {POSE_SIT, {0}, {0}, 0, 0, 2, 0, 0, EARS_DOWN, EYES_OPEN, 0},
    [PF_SIT1] = {POSE_SIT, {0}, {0}, 0, 0, 2, 2, 0, EARS_DOWN, EYES_OPEN, 0},
    [PF_LIE] = {POSE_LIE, {0}, {0}, 0, 0, 0, 0, 0, EARS_DOWN, EYES_OPEN, 0},
    [PF_LIE1] = {POSE_LIE, {0}, {0}, 0, 0, 0, 0, 0, EARS_DOWN, EYES_SHUT, 0},
    [PF_SLEEP0] = {POSE_CURL, {0}, {0}, 0, 0, 0, 0, 0, EARS_UP, EYES_SLEEP, 0},
    [PF_SLEEP1] = {POSE_CURL, {0}, {0}, -1, 0, 0, 0, 0, EARS_UP, EYES_SLEEP, 0},
};

// Pose pixels (adult) to Q4 at this size.
static int pz(const Build* b, int v) {
    return v * b->scale;
}

static void set_anchor(int part, int x, int y) {
    anchor_x[part] = x;
    anchor_y[part] = y;
}

// The head, its middle at (hx, hy) (Q4): ears, skull, muzzle, then the face.
static void draw_head(const Build* b, const Pose* p, int hx, int hy) {
    set_anchor(PART_HEAD, hx, hy);
    int erx = b->ear_rx, ery = b->ear_ry;
    if (p->ears == EARS_UP) {
        ellipse(hx + b->far_ear_dx, hy + b->ear_dy + Q(0.3), erx, ery, K_EAR_FAR, PART_EAR);
    } else {
        // laid back and down: long ears pointing behind the head
        ellipse(hx - b->head_rx / 2 - ery / 2, hy - b->head_ry / 3, ery, erx, K_EAR_DOWN, PART_EAR);
    }
    ellipse(hx, hy, b->head_rx, b->head_ry, K_COAT, PART_HEAD);
    ellipse(hx + b->muzzle_dx, hy + b->muzzle_dy, b->muzzle_rx, b->muzzle_ry, K_MUZZLE, PART_HEAD);
    // cheek and chin
    ellipse(hx + b->muzzle_dx / 3, hy + b->head_ry * 2 / 3, b->head_rx * 2 / 3, b->head_ry / 3,
            K_CREAM, PART_HEAD);
    if (p->ears == EARS_UP) {
        ellipse(hx + b->ear_dx, hy + b->ear_dy, erx + Q(1), ery + Q(1), K_EAR_RING, PART_HEAD);
        ellipse(hx + b->ear_dx, hy + b->ear_dy, erx, ery, K_EAR, PART_HEAD);
    } else {
        ellipse(hx - b->head_rx / 3 - ery / 3, hy - b->head_ry * 3 / 4, ery, erx, K_EAR_DOWN,
                PART_HEAD);
    }

    // The face, in pixels from the head's middle.
    int x = hx >> 4, y = hy >> 4;
    bool big = b->size == 32;
    int ex = x + 1, ey = y - 1;
    if (b->scale == 7) {
        ex = x;
        ey = y - 1;
    }
    // the brow, a dark line over the eye and markings on the crown
    plot(ex - 2, y - (b->head_ry >> 4) + 1, P_BLACK, PART_HEAD);
    if (big) {
        plot(ex - 3, y - (b->head_ry >> 4) + 2, P_BLACK, PART_HEAD);
        plot(ex - 1, y - (b->head_ry >> 4) + 1, P_BLACK, PART_HEAD);
    }
    switch (p->eyes) {
    case EYES_OPEN:
        if (b->scale == 7) { // kitten: a big round eye
            plot(ex, ey - 1, P_OUTLINE, PART_HEAD);
            plot(ex + 1, ey - 1, P_OUTLINE, PART_HEAD);
            plot(ex, ey, P_IRIS, PART_HEAD);
            plot(ex + 1, ey, P_PUPIL, PART_HEAD);
            plot(ex, ey + 1, P_GLINT, PART_HEAD);
            plot(ex + 1, ey + 1, P_IRIS, PART_HEAD);
        } else {
            plot(ex - 1, ey - 1, P_OUTLINE, PART_HEAD);
            plot(ex, ey - 1, P_OUTLINE, PART_HEAD);
            plot(ex + 1, ey - 1, P_OUTLINE, PART_HEAD);
            plot(ex - 1, ey, P_IRIS, PART_HEAD);
            plot(ex, ey, P_IRIS, PART_HEAD);
            plot(ex + 1, ey, P_PUPIL, PART_HEAD);
            plot(ex, ey + 1, P_IRIS, PART_HEAD);
            plot(ex - 1, ey + 1, P_GLINT, PART_HEAD);
            if (p->ears == EARS_DOWN) // a sad brow
                plot(ex + 1, ey - 2, P_OUTLINE, PART_HEAD);
        }
        break;
    case EYES_SHUT:
    case EYES_SLEEP:
        plot(ex - 1, ey, P_OUTLINE, PART_HEAD);
        plot(ex, ey + 1, P_OUTLINE, PART_HEAD);
        plot(ex + 1, ey + 1, P_OUTLINE, PART_HEAD);
        break;
    case EYES_HAPPY: // a smiling arch
        plot(ex - 1, ey + 1, P_OUTLINE, PART_HEAD);
        plot(ex, ey, P_OUTLINE, PART_HEAD);
        plot(ex + 1, ey + 1, P_OUTLINE, PART_HEAD);
        break;
    }
    // the black tear line from the eye toward the mouth
    if (b->scale != 7) {
        plot(ex + 1, ey + 2, P_BLACK, PART_HEAD);
        plot(ex + 2, ey + 3, P_BLACK, PART_HEAD);
    }
    // nose and mouth at the muzzle's tip
    int nx = (hx + b->muzzle_dx + b->muzzle_rx) >> 4,
        ny = (hy + b->muzzle_dy - b->muzzle_ry / 2) >> 4;
    plot(nx, ny, P_PINK, PART_HEAD);
    plot(nx - 1, ny, P_PINK, PART_HEAD);
    plot(nx, ny + 1, P_OUTLINE, PART_HEAD);
    if (p->mouth) {
        plot(nx, ny + 2, P_PINK, PART_HEAD);
        plot(nx - 1, ny + 2, P_OUTLINE, PART_HEAD);
    }
}

// The neck from the shoulder to the head, with its black bands.
static void draw_neck(const Build* b, int sx, int sy, int hx, int hy) {
    set_anchor(PART_NECK, sx, sy);
    int r = b->chest_ry * 5 / 9;
    capsule(sx, sy, hx - b->head_rx / 3, hy + b->head_ry / 3, r, K_COAT, PART_NECK);
    // cream throat
    capsule(sx + r / 2, sy + r / 2, hx + b->head_rx / 4, hy + b->head_ry * 2 / 3, r / 2, K_CREAM,
            PART_NECK);
}

static void neck_bands(const Build* b, int sx, int sy, int hx, int hy) {
    // two or three dark bands across the back of the neck
    int n = b->scale >= 13 ? 3 : 2;
    for (int i = 1; i <= n; i++) {
        int t = i * 256 / (n + 1);
        int x = (sx + (hx - sx) * t / 256 - b->chest_ry / 3) >> 4;
        int y = (sy + (hy - sy) * t / 256 - b->chest_ry / 3) >> 4;
        for (int k = 0; k < (b->scale >= 13 ? 3 : 2); k++)
            if (get(x - 1 + k, y - 1 + k / 2) >= P_HI && get(x - 1 + k, y - 1 + k / 2) <= P_SHADE)
                plot(x - 1 + k, y - 1 + k / 2, P_BLACK, PART_NECK);
    }
}

// A standing leg: from the joint down to the paw, a hock bent back on the
// hind legs.
static void leg(const Build* b, int jx, int jy, int fx, int lift, bool hind, bool far) {
    int px = jx + fx, py = b->ground - b->paw_r - lift;
    int kind = far ? K_FAR : K_LEG;
    if (hind) {
        int hx = (jx + px) / 2 - b->leg_r * 2, hy = jy + (py - jy) * 5 / 8;
        capsule(jx, jy, hx, hy, b->leg_r + Q(0.3), kind, PART_LEG);
        capsule(hx, hy, px, py, b->leg_r, kind, PART_LEG);
    } else if (lift > 0) {
        int kx = (jx + px) / 2 + lift * 3, ky = (jy + py) / 2;
        capsule(jx, jy, kx, ky, b->leg_r, kind, PART_LEG);
        capsule(kx, ky, px, py, b->leg_r, kind, PART_LEG);
    } else {
        capsule(jx, jy, px, py, b->leg_r, kind, PART_LEG);
    }
    ellipse(px + b->paw_r / 3, py, b->paw_r + Q(0.2), b->paw_r, far ? K_FAR : K_COAT, PART_LEG);
}

static void tail(const Build* b, int bx, int by, int tx, int ty) {
    set_anchor(PART_TAIL, bx, by);
    capsule(bx, by, tx, ty, b->tail_r, K_TAIL, PART_TAIL);
}

static void draw_stand(const Build* b, const Pose* p) {
    int dy = pz(b, p->body_dy);
    int hip_x = b->hip_x, hip_y = b->hip_y + dy, sh_x = b->sh_x, sh_y = b->sh_y + dy;
    int leg_y = (hip_y + sh_y) / 2 + b->belly_ry / 2;
    set_anchor(PART_BODY, (hip_x + sh_x) / 2, hip_y);
    set_anchor(PART_LEG, hip_x, hip_y);
    // far legs first
    leg(b, sh_x - Q(1), leg_y, pz(b, p->fx[1]), pz(b, p->lift[1]), false, true);
    leg(b, hip_x + Q(1), leg_y - Q(1), pz(b, p->fx[3]), pz(b, p->lift[3]), true, true);
    // the tail, low and curved, from the rump
    int tbx = hip_x - b->haunch_r + Q(1), tby = hip_y - b->haunch_r / 2;
    tail(b, tbx, tby, tbx - b->tail_len * 2 / 3 + pz(b, p->tail_dx),
         tby + b->tail_len * 3 / 4 + pz(b, p->tail_dy));
    // the body: haunch, belly, chest
    ellipse(hip_x, hip_y, b->haunch_r, b->haunch_r, K_COAT, PART_BODY);
    ellipse((hip_x + sh_x) / 2, (hip_y + sh_y) / 2 + Q(0.5), b->belly_rx, b->belly_ry, K_BELLY,
            PART_BODY);
    ellipse(sh_x, sh_y, b->chest_rx, b->chest_ry, K_BELLY, PART_BODY);
    // the neck and head
    int hx = sh_x + b->head_dx + pz(b, p->head_dx), hy = sh_y + b->head_dy + pz(b, p->head_dy);
    draw_neck(b, sh_x + Q(0.5), sh_y - Q(1), hx, hy);
    // near legs
    leg(b, hip_x - Q(0.5), leg_y, pz(b, p->fx[2]), pz(b, p->lift[2]), true, false);
    leg(b, sh_x + Q(0.5), leg_y, pz(b, p->fx[0]), pz(b, p->lift[0]), false, false);
    spots();
    neck_bands(b, sh_x + Q(0.5), sh_y - Q(1), hx, hy);
    draw_head(b, p, hx, hy);
}

// Sitting: haunches on the ground, front legs straight, head low (sad).
static void draw_sit(const Build* b, const Pose* p) {
    int hr = b->haunch_r + b->haunch_r / 4;
    int hip_x = b->hip_x + Q(1) * b->scale / 16, hip_y = b->ground - hr;
    int sh_x = b->sh_x - Q(2) * b->scale / 16, sh_y = b->sh_y - Q(1) * b->scale / 16;
    set_anchor(PART_BODY, hip_x, hip_y);
    set_anchor(PART_LEG, sh_x, sh_y);
    // the tail along the ground, curling forward
    tail(b, hip_x - hr + Q(1), b->ground - b->tail_r - Q(0.5),
         hip_x - hr - b->tail_len / 2 + pz(b, p->tail_dx), b->ground - b->tail_r - Q(0.5));
    // far front leg
    capsule(sh_x - Q(1), sh_y, sh_x - Q(1), b->ground - b->paw_r, b->leg_r, K_FAR, PART_LEG);
    ellipse(hip_x, hip_y, hr, hr, K_COAT, PART_BODY);
    capsule(hip_x, hip_y, sh_x, sh_y, b->chest_rx, K_COAT, PART_BODY);
    ellipse(sh_x, sh_y, b->chest_rx, b->chest_ry, K_BELLY, PART_BODY);
    // a hind paw in front of the haunch
    ellipse(hip_x + hr - Q(1), b->ground - b->paw_r, b->paw_r * 3 / 2, b->paw_r, K_COAT, PART_LEG);
    int hx = sh_x + b->head_dx * 3 / 4 + pz(b, p->head_dx);
    int hy = sh_y + b->head_dy * 3 / 4 + pz(b, p->head_dy);
    draw_neck(b, sh_x, sh_y - Q(1), hx, hy);
    // near front leg
    capsule(sh_x + Q(1), sh_y + Q(1), sh_x + Q(1.5), b->ground - b->paw_r, b->leg_r, K_LEG,
            PART_LEG);
    ellipse(sh_x + Q(1.5) + b->paw_r / 3, b->ground - b->paw_r, b->paw_r + Q(0.2), b->paw_r, K_COAT,
            PART_LEG);
    spots();
    neck_bands(b, sh_x, sh_y - Q(1), hx, hy);
    draw_head(b, p, hx, hy);
}

// Lying: the body low along the ground, front paws out ahead, head low.
static void draw_lie(const Build* b, const Pose* p) {
    int ry = b->belly_ry + Q(0.5);
    int cy = b->ground - ry;
    int hip_x = b->hip_x, sh_x = b->sh_x;
    set_anchor(PART_BODY, hip_x, cy);
    set_anchor(PART_LEG, sh_x, cy);
    tail(b, hip_x - b->haunch_r + Q(1), b->ground - b->tail_r - Q(0.5),
         hip_x - b->haunch_r - b->tail_len * 3 / 4, b->ground - b->tail_r - Q(0.5));
    // far front leg, stretched forward
    capsule(sh_x, b->ground - b->paw_r - Q(1), sh_x + pz(b, 5), b->ground - b->paw_r - Q(0.5),
            b->leg_r, K_FAR, PART_LEG);
    ellipse(hip_x, b->ground - b->haunch_r, b->haunch_r, b->haunch_r, K_COAT, PART_BODY);
    ellipse((hip_x + sh_x) / 2, cy, (sh_x - hip_x) / 2 + b->chest_rx / 2, ry, K_BELLY, PART_BODY);
    ellipse(sh_x, b->ground - b->chest_ry + Q(0.5), b->chest_rx, b->chest_ry - Q(0.5), K_BELLY,
            PART_BODY);
    // near front leg forward, and the hind paw
    capsule(sh_x + Q(1), b->ground - b->paw_r, sh_x + pz(b, 6), b->ground - b->paw_r, b->leg_r,
            K_LEG, PART_LEG);
    ellipse(hip_x + b->haunch_r / 2, b->ground - b->paw_r, b->paw_r * 3 / 2, b->paw_r, K_COAT,
            PART_LEG);
    int hx = sh_x + b->head_dx * 3 / 4 + pz(b, p->head_dx);
    int hy = b->ground - b->head_ry - b->paw_r - Q(0.5) + pz(b, p->head_dy);
    draw_neck(b, sh_x, b->ground - b->chest_ry, hx, hy);
    spots();
    draw_head(b, p, hx, hy);
}

// Curled asleep: a round body, the tail wrapped round the front, the head
// resting on it.
static void draw_curl(const Build* b, const Pose* p) {
    int rx = (b->sh_x - b->hip_x) / 2 + b->haunch_r;
    int ry = b->haunch_r + Q(0.8) - pz(b, p->body_dy) / 2;
    int cx = (b->hip_x + b->sh_x) / 2 - Q(1), cy = b->ground - ry;
    set_anchor(PART_BODY, cx, b->ground);
    set_anchor(PART_LEG, cx, b->ground);
    ellipse(cx, cy, rx, ry, K_COAT, PART_BODY);
    // the haunch's curve
    ellipse(cx - rx / 3, cy - ry / 6, rx / 2, ry * 3 / 4, K_COAT, PART_BODY);
    spots();
    // the tail round the front, along the ground
    tail(b, cx - rx + Q(1), b->ground - b->tail_r - Q(0.3), cx + rx / 2,
         b->ground - b->tail_r - Q(0.3));
    // front paws tucked under the chin
    ellipse(cx + rx - Q(1), b->ground - b->paw_r, b->paw_r * 2, b->paw_r, K_COAT, PART_LEG);
    int hx = cx + rx - b->head_rx / 2, hy = b->ground - b->head_ry - b->paw_r;
    draw_head(b, p, hx, hy);
}

// The kitten is drawn by hand, a head on a body: at 16x16 a rule can't place
// pixels as well. Keys as the coat's colours (P_OUTLINE ...).
static const char kit_keys[] = " Ohcswkabfepnidg";

enum { KH_OPEN, KH_SHUT, KH_HAPPY, KH_SAD, KH_SAD_SHUT, KH_COUNT };
static const char* const kit_heads[KH_COUNT][7] = {
    [KH_OPEN] = {"..k...k..", ".kwk.kic.", ".kwk.kic.", ".hkhhhcc.", "hhccOOcc.", "hcccepccn",
                 "scccgwwO."},
    [KH_SHUT] = {"..k...k..", ".kwk.kic.", ".kwk.kic.", ".hkhhhcc.", "hhcccccc.", "hcccOOccn",
                 "sccccwwO."},
    [KH_HAPPY] = {"..k...k..", ".kwk.kic.", ".kwk.kic.", ".hkhhhcc.", "hhcccOcc.", "hcccOcOcn",
                  "sccccwwO."},
    [KH_SAD] = {".........", ".........", "kkkkk....", "kwkkhhcc.", "hhcOOOcc.", "hcccepccn",
                "scccgwwO."},
    [KH_SAD_SHUT] = {".........", ".........", "kkkkk....", "kwkkhhcc.", "hhcccccc.", "hcccOOccn",
                     "sccccwwO."},
};

enum {
    KB_STAND,
    KB_TAIL,
    KB_WALK0,
    KB_WALK1,
    KB_WALK2,
    KB_WALK3,
    KB_SIT,
    KB_LIE,
    KB_CURL0,
    KB_CURL1,
    KB_COUNT
};
// Bodies: frame rows 7-14.
static const char* const kit_bodies[KB_COUNT][8] = {
    [KB_STAND] = {"................", "..kchhbhbcwww...", "...hccacccsw....", "...cfcccaccs....",
                  "...sccwwwwcs....", "...ccd...ccd....", "...ccd...ccd....", "...cccd..cccd..."},
    [KB_TAIL] = {".k..............", "..cchhbhbcwww...", "...hccacccsw....", "...cfcccaccs....",
                 "...sccwwwwcs....", "...ccd...ccd....", "...ccd...ccd....", "...cccd..cccd..."},
    [KB_WALK0] = {"................", "..kchhbhbcwww...", "...hccacccsw....", "...cfcccaccs....",
                  "...sccwwwwcs....", "...ccd...ccd....", "..ccd.....ccd...", ".cccd.....cccd.."},
    [KB_WALK1] = {"................", "..kchhbhbcwww...", "...hccacccsw....", "...cfcccaccs....",
                  "...sccwwwwcs....", "...ccd...ccd....", "...ccdd..ccdd...", "...ccc...ccc...."},
    [KB_WALK2] = {"................", "..kchhbhbcwww...", "...hccacccsw....", "...cfcccaccs....",
                  "...sccwwwwcs....", "...dcc...dcc....", "..dcc.....dcc...", ".ddcc....ddccc.."},
    [KB_WALK3] = {"................", "..kchhbhbcwww...", "...hccacccsw....", "...cfcccaccs....",
                  "...sccwwwwcs....", "...dcc...dcc....", "...dccc..dccc...", "...ddd....ddd..."},
    [KB_SIT] = {"................", "......hhbcw.....", "....hhcacsw.....", "...hcccfcsw.....",
                "..hcacccccw.....", "..ccccccscw.....", ".kcccsswccw.....", "kkccccsccccc...."},
    [KB_LIE] = {"................", "................", "................", "...hhhhhb.......",
                "..hcacccfcc.....", ".hccfcaccccw....", "kcswwwwwwwccccc.", "kkcccccccccccccc"},
    [KB_CURL0] = {"................", "................", "...hhhhh........", "..hcacchh.......",
                  ".hccccfcch......", ".hcfccccacc.....", ".scccacccccc....", "kcccccccccccck.."},
    [KB_CURL1] = {"................", "....hhh.........", "..hhhhhhh.......", "..hcacchh.......",
                  ".hccccfcch......", ".hcfccccacc.....", ".scccacccccc....", "kcccccccccccck.."},
};

// Each frame: a body, a head and where the head goes.
static const u8 kit_frames[PET_FRAMES][4] = {
    [PF_STAND] = {KB_STAND, KH_OPEN, 6, 1},  [PF_STAND_TAIL] = {KB_TAIL, KH_OPEN, 6, 1},
    [PF_BLINK] = {KB_STAND, KH_SHUT, 6, 1},  [PF_WALK0] = {KB_WALK0, KH_OPEN, 6, 1},
    [PF_WALK1] = {KB_WALK1, KH_OPEN, 6, 0},  [PF_WALK2] = {KB_WALK2, KH_OPEN, 6, 1},
    [PF_WALK3] = {KB_WALK3, KH_OPEN, 6, 0},  [PF_HAPPY] = {KB_TAIL, KH_HAPPY, 6, 1},
    [PF_EAT0] = {KB_STAND, KH_SHUT, 6, 6},   [PF_EAT1] = {KB_STAND, KH_SHUT, 6, 7},
    [PF_SIT] = {KB_SIT, KH_SAD, 5, 3},       [PF_SIT1] = {KB_SIT, KH_SAD, 5, 4},
    [PF_LIE] = {KB_LIE, KH_SAD, 6, 7},       [PF_LIE1] = {KB_LIE, KH_SAD_SHUT, 6, 7},
    [PF_SLEEP0] = {KB_CURL0, KH_SHUT, 6, 7}, [PF_SLEEP1] = {KB_CURL1, KH_SHUT, 6, 7},
};

static void draw_kitten(int frame) {
    const u8* f = kit_frames[frame];
    canvas_clear();
    paint(kit_bodies[f[0]], 8, kit_keys, 0, 7);
    paint(kit_heads[f[1]], 7, kit_keys, f[2], f[3]);
    outline(16, 16, P_OUTLINE);
}

static void draw_pet(int stage, int frame) {
    const Build* b = &builds[stage];
    const Pose* p = &poses[frame];
    canvas_clear();
    switch (p->kind) {
    case POSE_STAND:
        draw_stand(b, p);
        break;
    case POSE_SIT:
        draw_sit(b, p);
        break;
    case POSE_LIE:
        draw_lie(b, p);
        break;
    default:
        draw_curl(b, p);
        break;
    }
    outline(b->size, b->size, P_OUTLINE);
}

// --- Small sprites ------------------------------------------------------------------------------

// PAL_ITEMS's colours.
static const char item_keys[] = " Kdbhcmpwrqgsty";
static const char* const drop_art[DROP_FRAMES][8] = {
    {"........", "........", "........", "...KK...", "..KhbK..", ".KhbbdK.", "KbbdbddK",
     ".KKKKKK."},
    {"........", "........", "........", "........", "..KK....", ".KhbKKK.", "KbbdhbdK",
     ".KKKKKK."},
    {"..g.....", ".g..g...", "..g..g..", ".g..g...", "..g.....", "........", "........",
     "........"},
};

static const char* const bowl_art[BOWL_FRAMES][8] = {
    {"..pppppp......", ".ppwppppp.....", "KKKKKKKKKKKK..", "KmmmmmmmmmmK..", ".KccccccccK...",
     "..KKKKKKKK....", "..............", ".............."},
    {"..............", "...pp.pp......", "KKKKKKKKKKKK..", "KmmmmmmmmmmK..", ".KccccccccK...",
     "..KKKKKKKK....", "..............", ".............."},
    {"..............", "..............", "KKKKKKKKKKKK..", "KmmmmmmmmmmK..", ".KccccccccK...",
     "..KKKKKKKK....", "..............", ".............."},
};

static const char* const zzz_art[2][8] = {
    {"KKKKKK..", "KwwwwK..", "KKKwKK..", "..KwK...", ".KwKKK..", "KwwwwK..", "KKKKKK..",
     "........"},
    {"........", "........", "KKKKK...", "KwwwK...", "KKwKK...", "KwwwK...", "KKKKK...",
     "........"},
};

static const char* const heart_art[2][8] = {
    {".KK.KK..", "KqrKrrK.", "KqrrrrK.", "KrrrrrK.", ".KrrrK..", "..KrK...", "...K....",
     "........"},
    {"........", "........", ".K.K....", "KrKrK...", "KrrrK...", ".KrK....", "..K.....",
     "........"},
};

static const char* const spark_art[SPARK_FRAMES][8] = {
    {"........", "........", "...y....", "..ywy...", "...y....", "........", "........",
     "........"},
    {"...y....", "...y....", "..ywy...", "yywwwyy.", "..ywy...", "...y....", "...y....",
     "........"},
    {"y.....y.", ".y...y..", "...w....", "..w.w...", "...w....", ".y...y..", "y.....y.",
     "........"},
};

static const char* const tomb_art[16] = {
    "................", "......KKKK......", "....KKttssKK....", "...KttsssssgK...",
    "...KtsKsKssgK...", "...KtsKsKssgK...", "...KtsssssggK...", "...KtsKKKsggK...",
    "...KtssssgggK...", "...KtsKKKgggK...", "...KtssssgggK...", "...KssggggggK...",
    ".y.KKKKKKKKKKy..", "yKy.y.....y.yKy.", ".y..Ky...yK..y..", "KKKKKKKKKKKKKKKK",
};

// The lamp: a jar of fireflies on a cord, lit (two flickers) or dark.
static const char lamp_keys[] = " Kgmhcafebd";
static const char* const lamp_art[LAMP_FRAMES][16] = {
    {"......KK........", ".....KccK.......", "....KKccKK......", "...KhmmmmgK.....",
     "..KhaaffaamK....", "..KhfeeffagK....", "..KmfeffeafK....", "..KmaffeeagK....",
     "..KmafeffaaK....", "..KmaaffaagK....", "...KmaaaagK.....", "....KKKKKK......",
     "................", "................", "................", "................"},
    {"......KK........", ".....KccK.......", "....KKccKK......", "...KhmmmmgK.....",
     "..KhafeaffmK....", "..KhffefeagK....", "..KmaefffefK....", "..KmfefeffgK....",
     "..KmaffefaaK....", "..KmaafaagaK....", "...KmaaaagK.....", "....KKKKKK......",
     "................", "................", "................", "................"},
    {"......KK........", ".....KccK.......", "....KKccKK......", "...KhmmmmgK.....",
     "..KhmmmmmmgK....", "..KhmbmmmmgK....", "..KmmmmdmmgK....", "..KmmmmmmmgK....",
     "..KmmmbmmmgK....", "..KmmmmmmggK....", "...KmmmmggK.....", "....KKKKKK......",
     "................", "................", "................", "................"},
};

// The sun and the moon (PAL_SKY_OBJ): the sun a disc with a soft rim, the
// moon a gibbous moon with its seas.
static const char sky_keys[] = " abcdlmns";

static void draw_disc(int r2_in, int r2_out, int inner, int outer) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x - 15, dy = 2 * y - 15, d = dx * dx + dy * dy;
            if (d <= r2_in)
                put(x, y, inner);
            else if (d <= r2_out)
                put(x, y, outer);
        }
}

static const char* const moon_art[16] = {
    "................", "................", "................", "......llll......",
    ".....llllmm.....", "....lmmllllm....", "....llllnlll....", "...llllllllls...",
    "...lmlllllmls...", "....llmlllls....", "....llllllss....", ".....llllss.....",
    "......lsss......", "................", "................", "................",
};

static u32* build_small(u32* out) {
    const u32* tiles;

    canvas_clear();
    draw_disc(60, 100, 1, 2);
    for (int y = 0; y < 16; y++) // a hotter middle
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x - 13, dy = 2 * y - 13;
            if (dx * dx + dy * dy <= 20)
                put(x, y, 1);
        }
    // the rim
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int dx = 2 * x - 15, dy = 2 * y - 15, d = dx * dx + dy * dy;
            if (d > 100 && d <= 140)
                put(x, y, 3);
        }
    tiles = out;
    out = pack(16, 16, out);
    sprite(SPR_SUN, SPRITE_16x16, 1, PAL_SKY_OBJ, tiles);

    canvas_clear();
    paint(moon_art, 16, sky_keys, 0, 0);
    tiles = out;
    out = pack(16, 16, out);
    sprite(SPR_MOON, SPRITE_16x16, 1, PAL_SKY_OBJ, tiles);

    tiles = out;
    for (int f = 0; f < LAMP_FRAMES; f++) {
        canvas_clear();
        paint(lamp_art[f], 16, lamp_keys, 0, 0);
        out = pack(16, 16, out);
    }
    sprite(SPR_LAMP, SPRITE_16x16, LAMP_FRAMES, PAL_LAMP, tiles);

    tiles = out;
    for (int f = 0; f < DROP_FRAMES; f++) {
        canvas_clear();
        paint(drop_art[f], 8, item_keys, 0, 0);
        out = pack(8, 8, out);
    }
    sprite(SPR_DROP, SPRITE_8x8, DROP_FRAMES, PAL_ITEMS, tiles);

    tiles = out;
    for (int f = 0; f < BOWL_FRAMES; f++) {
        canvas_clear();
        paint(bowl_art[f], 8, item_keys, 1, 0);
        out = pack(16, 8, out);
    }
    sprite(SPR_BOWL, SPRITE_16x8, BOWL_FRAMES, PAL_ITEMS, tiles);

    tiles = out;
    for (int f = 0; f < 2; f++) {
        canvas_clear();
        paint(zzz_art[f], 8, item_keys, 0, 0);
        out = pack(8, 8, out);
    }
    sprite(SPR_ZZZ, SPRITE_8x8, 2, PAL_ITEMS, tiles);

    tiles = out;
    for (int f = 0; f < 2; f++) {
        canvas_clear();
        paint(heart_art[f], 8, item_keys, 0, 0);
        out = pack(8, 8, out);
    }
    sprite(SPR_HEART, SPRITE_8x8, 2, PAL_ITEMS, tiles);

    tiles = out;
    for (int f = 0; f < SPARK_FRAMES; f++) {
        canvas_clear();
        paint(spark_art[f], 8, item_keys, 0, 0);
        out = pack(8, 8, out);
    }
    sprite(SPR_SPARK, SPRITE_8x8, SPARK_FRAMES, PAL_ITEMS, tiles);

    canvas_clear();
    paint(tomb_art, 16, item_keys, 0, 0);
    tiles = out;
    out = pack(16, 16, out);
    sprite(SPR_TOMB, SPRITE_16x16, 1, PAL_ITEMS, tiles);
    return out;
}

static u32* build_sprites(void) {
    u32* out = obj_tiles;
    make_spot_field();
    static const u8 pet_ids[3] = {SPR_KITTEN, SPR_YOUNG, SPR_ADULT};
    for (int stage = 0; stage < 3; stage++) {
        const u32* tiles = out;
        int size = builds[stage].size;
        for (int f = 0; f < PET_FRAMES; f++) {
            if (stage == 0)
                draw_kitten(f);
            else
                draw_pet(stage, f);
            out = pack(size, size, out);
        }
        sprite(pet_ids[stage], size == 16 ? SPRITE_16x16 : SPRITE_32x32, PET_FRAMES, PAL_PET,
               tiles);
    }
    return build_small(out);
}

_Static_assert(PET_FRAMES * (4 + 16 + 16) + 4 * 2 + 4 * LAMP_FRAMES + DROP_FRAMES +
                       2 * BOWL_FRAMES + 2 + 2 + SPARK_FRAMES + 4 <=
                   OBJ_TILE_MAX,
               "the sprites outgrow obj_tiles: raise OBJ_TILE_MAX");

// --- Building and loading -----------------------------------------------------------------------

void art_draw(ArtDrawing* out) {
    for (int i = 0; i < 256; i++)
        tile_head[i] = 0xFFFF;
    u32 blank[8] = {0};
    bg_tiles_used = 0;
    add_tile(blank); // tile 0

    pic_clear();
    paint_den();
    paint_savanna();
    cut_map(den_metatiles, den_cells, DEN_W);
    pic_clear();
    paint_sky();
    cut_map(sky_metatiles, sky_cells, SKY_W);

    u32* end = build_sprites();
    *out = (ArtDrawing){
        .bg_tiles = bg_tiles,
        .bg_tile_count = bg_tiles_used,
        .den_metatiles = den_metatiles,
        .sky_metatiles = sky_metatiles,
        .obj_tiles = obj_tiles,
        .obj_tile_count = (int)(end - obj_tiles) / 8,
        .sprites = sprites,
    };
}
