// The first-person view: a ray per column of the picture, the wall it hits
// drawn as one textured column, and the things in front of that wall drawn
// over it (game.h). All of it is the game's own: the engine gives the pixels
// (surface.h) and the hardware stretches them.
//
// How the picture reaches the screen. The surface is 240 x 160 bytes, row
// after row, but a raycaster draws columns, and a column of a row-major
// picture is 160 bytes 240 apart. So the game turns the surface on its side
// (game.c: surface_set_transform(0, 0, 0, FX(1), FX(1) / 2, 0)): screen
// column sx, row sy shows surface pixel (sy, sx / 2). A column of the view is
// then the first 160 bytes of a surface row, consecutive in memory, and each
// is shown two screen pixels wide: 120 columns fill the 240-pixel screen at
// half the work, for free.
//
// A column is drawn in a 160-byte buffer in fast RAM (IWRAM), then copied to
// video memory a word at a time: video memory can't store single bytes
// (surface.h), and reading and writing the buffer is quicker anyway.
//
// The arithmetic is 16.16 fixed point with no division while drawing: the
// two things a raycaster divides by, a ray's direction and the distance to a
// wall, go through a table of reciprocals (recip()).
//
// view_draw() runs in IWRAM as ARM code (SERVAL_IWRAM_CODE): it is the
// game's inner loop, and there it runs about three times as fast as from ROM.
// Its own inner loops, the ones that run once per pixel, are written for the
// worst case, a wall or a wisp right in front of the serval's nose, which
// fills the screen: there a texel covers several pixels, so pixels are drawn
// in pairs, one texel fetched and one 16-bit store for both.

#include "game.h"

// --- Reciprocals ----------------------------------------------------------------

// recip_table[i] is 1 / (1 + i / 512) with 30 fraction bits, for i = 0 to
// 512: 1/x for x from 1 to 2.
#define RECIP_STEPS 512
static u32 recip_table[RECIP_STEPS + 1] SERVAL_EWRAM_BSS;

// The largest reciprocal recip() returns: 1,024 cells, for a ray that runs
// along a grid line and never crosses the next one.
#define RECIP_MAX (1u << 26)

// 1 / d for a positive 16.16 number, as 16.16. d is scaled into 1..2 (the
// loops: a shift or two for the distances and directions of a view), looked
// up, and the result scaled back; between two table entries it follows the
// straight line. Within 0.001%.
static inline __attribute__((always_inline)) u32 recip(u32 d) {
    if (d < 64)
        return RECIP_MAX;
    int shift = 14;
    while (d >= 2 * FIX_ONE) {
        d >>= 1;
        shift++;
    }
    while (d < FIX_ONE) {
        d <<= 1;
        shift--;
    }
    u32 i = (d >> 7) - RECIP_STEPS;
    u32 a = recip_table[i];
    return (a - (((a - recip_table[i + 1]) * (d & 127)) >> 7)) >> shift;
}

// --- The camera -----------------------------------------------------------------

// The field of view: the picture's left and right edges are at 0.75 to the
// sides for 1 ahead (about 74 degrees), which with a 3:2 screen makes a wall
// one cell away exactly as tall as the screen, and cells square.
#define PLANE FIX(0.75)
// So a thing 1 to the side for 1 ahead is 80 columns (160 screen pixels) from
// the middle, and a wall at distance d is 160 / d pixels tall.
#define COLS_PER_UNIT (VIEW_COLS * 2 / 3)
#define HORIZON (VIEW_ROWS / 2)

// Nothing is drawn closer than this, however near the serval stands.
#define NEAREST (FIX_ONE / 16)

// Texels per pixel of a wall or thing at distance d, with the fraction bits
// that leave the texel's number in the top 6 (a 64-texel wall) or 5 (a
// 32-texel picture) bits of 32, so the drawing loops get it with one shift:
// d * 64 / 160, shifted left 26 - 16 = d * 409.6.
#define TEXEL_STEP(d) ((u32)(d) * 409 + (((u32)(d) * 154) >> 8))

// A wall or thing taller than this many pixels is drawn two pixels at a
// time: from there every texel covers at least two (64 texels over 128).
#define PAIRS_FROM 128

// How dark things are at a distance: one level per 1.33 cells.
static inline __attribute__((always_inline)) u32 shade_at(u32 distance) {
    u32 level = (distance * 3) >> 18;
    return level < SHADES ? level : SHADES - 1;
}

// What view_begin() notes for the columns drawn after it.
static struct {
    Fix x, y;           // where the serval stands
    Fix dir_x, dir_y;   // where it looks: a vector of length 1
    Fix step_x, step_y; // what a column adds to the ray's direction, with 7
                        // more fraction bits (per half column)
} camera;

// A thing in view: the columns and rows it covers and how to texture them.
typedef struct {
    const u8* texels;        // its picture
    const u8* spans;         // and each of its columns' opaque texels (pic_spans)
    Fix depth;               // how far ahead it is
    Fix left;                // its left edge, in columns (16.16)
    Fix top;                 // its top edge, in rows (16.16; above the screen when near)
    Fix across;              // texels per column (16.16)
    Fix grow;                // rows per texel (16.16)
    u32 down;                // texels per row (5.27)
    s16 first, last;         // columns first to last - 1
    s16 top_row, bottom_row; // rows on screen: top_row to bottom_row - 1
    u8 shade;                // SHADE(level)
} Slice;

// The things in view, in no order, and the order to draw them in: farthest
// first, so that nearer ones are drawn over them. (Sorting the numbers moves
// bytes; sorting the slices would copy structs.)
#define MAX_SLICES 24
static Slice slices[MAX_SLICES];
static u8 slice_order[MAX_SLICES];
static int slice_count;

// The column being drawn, and the floor and ceiling every column starts
// from. Words, because they are copied as words; drawn into as bytes and, two
// pixels at a time, as halfwords (through types that may alias the others).
typedef u16 __attribute__((may_alias)) Pair;
typedef u32 __attribute__((may_alias)) Quad;
static u32 line[VIEW_ROWS / 4];
static u32 backdrop[VIEW_ROWS / 4];

void view_init(void) {
    for (u32 i = 0; i <= RECIP_STEPS; i++)
        recip_table[i] = (0x80000000u / (RECIP_STEPS + i)) << 8;

    // The floor and ceiling have no texture: each row is the floor's color
    // as dark as the floor is far there. Row y of the lower half shows the
    // floor HORIZON / (y + 0.5 - HORIZON) cells away (where a wall's foot
    // would be), and the ceiling mirrors it.
    u8* rows = (u8*)backdrop;
    for (int y = 0; y < HORIZON; y++) {
        u32 distance = (u32)(HORIZON * 2 * FIX_ONE) / (u32)(2 * y + 1);
        u32 shade = SHADE(shade_at(distance));
        rows[HORIZON + y] = (u8)(shade | BASE_FLOOR);
        rows[HORIZON - 1 - y] = (u8)(shade | BASE_CEILING);
    }
}

SERVAL_IWRAM_CODE void view_begin(void) {
    camera.x = player.x;
    camera.y = player.y;
    camera.dir_x = fix_from_fx(fx_cos(player.angle));
    camera.dir_y = fix_from_fx(fx_sin(player.angle));
    // The picture's plane runs to the serval's right, (-dir_y, dir_x), PLANE
    // long each way: a column is 1/60 of that, a half column 1/120.
    Fix right_x = -camera.dir_y, right_y = camera.dir_x;
    camera.step_x = fix_mul(right_x, PLANE) * 128 / VIEW_COLS;
    camera.step_y = fix_mul(right_y, PLANE) * 128 / VIEW_COLS;

    // The things ahead.
    slice_count = 0;
    for (int i = 0; i < MAX_THINGS; i++) {
        const Thing* t = &things[i];
        if (t->kind == THING_NONE)
            continue;
        Fix dx = t->x - camera.x, dy = t->y - camera.y;
        Fix depth = fix_mul(dx, camera.dir_x) + fix_mul(dy, camera.dir_y);
        if (depth < FIX(0.2) || depth > FIX(24))
            continue;
        Fix side = fix_mul(dx, right_x) + fix_mul(dy, right_y);
        // Outside the picture for certain: more than its half width past
        // the edge, which is 0.75 to the side for 1 ahead.
        Fix edge = fix_mul(depth, PLANE) + FIX_ONE / 2;
        if (side > edge || side < -edge)
            continue;
        Fix scale = (Fix)recip((u32)depth) * (COLS_PER_UNIT / 2); // half its width, in columns
        Fix center = VIEW_COLS / 2 * FIX_ONE + fix_mul(side, scale) * 2;
        Fix left = center - scale;
        // A column or row is drawn where its middle lies inside the picture.
        int first = (left + FIX_ONE / 2 - 1) >> 16;
        int last = (center + scale + FIX_ONE / 2 - 1) >> 16;
        if (first < 0)
            first = 0;
        if (last > VIEW_COLS)
            last = VIEW_COLS;
        if (first >= last)
            continue;
        // As tall as a wall there: 160 / depth pixels, 80 each side of the horizon.
        Fix top = HORIZON * FIX_ONE - scale * 2;
        int top_row = top < FIX_ONE / 2 ? 0 : (top + FIX_ONE / 2 - 1) >> 16;
        int bottom_row = (HORIZON * FIX_ONE + scale * 2 + FIX_ONE / 2 - 1) >> 16;
        if (bottom_row > VIEW_ROWS)
            bottom_row = VIEW_ROWS;

        // Its place in the order: after every farther one. When the list is
        // full it takes the farthest one's slot, unless it is farther still.
        // (Loops that stop at a comparison: a plain shift of the array the
        // compiler would hand to memmove(), in ROM.)
        int slot, at;
        if (slice_count < MAX_SLICES) {
            slot = at = slice_count++;
            while (at > 0 && slices[slice_order[at - 1]].depth < depth) {
                slice_order[at] = slice_order[at - 1];
                at--;
            }
        } else {
            slot = slice_order[0];
            if (slices[slot].depth <= depth)
                continue;
            at = 0;
            while (at + 1 < MAX_SLICES && slices[slice_order[at + 1]].depth > depth) {
                slice_order[at] = slice_order[at + 1];
                at++;
            }
        }
        slice_order[at] = (u8)slot;
        Slice* s = &slices[slot];
        s->texels = pic_texels[t->pic];
        s->spans = pic_spans[t->pic];
        s->depth = depth;
        s->left = left;
        s->top = top;
        s->across = fix_mul(depth, FIX(0.4)); // 32 texels over 80 / depth columns
        s->grow = scale / 8;                  // 160 / depth rows over 32 texels
        s->down = TEXEL_STEP(depth);
        s->first = (s16)first;
        s->last = (s16)last;
        s->top_row = (s16)top_row;
        s->bottom_row = (s16)bottom_row;
        s->shade = (u8)SHADE(t->glows ? 0 : shade_at((u32)depth));
    }
}

SERVAL_IWRAM_CODE void view_draw(int first, int last) {
    u8* page = surface_pixels();
    if (!page)
        return;
    u8* rows = (u8*)line;

    for (int col = first; col < last; col++) {
        // --- The ray: from the serval through the middle of this column,
        // stepped from grid line to grid line until it meets a wall (a DDA).
        // side_x and side_y are how far along the ray the next line it
        // crosses in x and in y is, measured straight ahead (so the walls
        // come out flat, not bulging), and delta_x and delta_y what one more
        // cell adds: 1 / the ray's direction.
        int half_cols = 2 * col - (VIEW_COLS - 1);
        Fix ray_x = camera.dir_x + ((camera.step_x * half_cols) >> 7);
        Fix ray_y = camera.dir_y + ((camera.step_y * half_cols) >> 7);
        u32 delta_x = recip((u32)(ray_x < 0 ? -ray_x : ray_x));
        u32 delta_y = recip((u32)(ray_y < 0 ? -ray_y : ray_y));
        int cell_x = camera.x >> 16, cell_y = camera.y >> 16;
        u32 in_x = (u32)camera.x & 0xFFFF, in_y = (u32)camera.y & 0xFFFF;
        int step_x = 1, step_y = 1;
        if (ray_x < 0)
            step_x = -1;
        else
            in_x = FIX_ONE - in_x;
        if (ray_y < 0)
            step_y = -1;
        else
            in_y = FIX_ONE - in_y;
        u32 side_x = (u32)(((uint64_t)in_x * delta_x) >> 16);
        u32 side_y = (u32)(((uint64_t)in_y * delta_y) >> 16);

        u32 distance; // to what the ray hit
        u32 texture;  // TEX_*
        u32 across;   // which of its 64 columns of texels
        bool faces_y; // it crossed a line in y: a north or south face, drawn darker
        for (;;) {
            if (side_x < side_y) {
                distance = side_x;
                side_x += delta_x;
                cell_x += step_x;
                faces_y = false;
            } else {
                distance = side_y;
                side_y += delta_y;
                cell_y += step_y;
                faces_y = true;
            }
            u32 cell = grid_cells[cell_y * GRID_W + cell_x];
            if (!cell)
                continue;
            if (cell & CELL_DOOR) {
                // A door stands in the middle of its cell, half a cell past
                // the face the ray came in by. The ray misses it if it leaves
                // the cell by a side first (the next cell is the wall beside
                // the door: the doorway has depth), or passes where the door
                // has slid away.
                const Door* door = &doors[cell & (CELL_DOOR - 1)];
                if (door->vertical == faces_y)
                    continue;
                u32 to_door = distance + ((faces_y ? delta_y : delta_x) >> 1);
                if (to_door > (faces_y ? side_x : side_y))
                    continue;
                Fix where = faces_y ? camera.x + fix_mul((Fix)to_door, ray_x)
                                    : camera.y + fix_mul((Fix)to_door, ray_y);
                int cell_at = faces_y ? cell_x : cell_y;
                u32 texel = ((u32)where >> 10) & (TEX_SIZE - 1);
                if ((where >> 16) != cell_at) // rounding took it just past an edge
                    texel = (where >> 16) < cell_at ? 0 : TEX_SIZE - 1;
                if (texel < door->open)
                    continue;
                distance = to_door;
                texture = door->locked ? TEX_LOCK : TEX_DOOR;
                across = texel - door->open;
                break;
            }
            texture = cell - 1;
            Fix where = faces_y ? camera.x + fix_mul((Fix)distance, ray_x)
                                : camera.y + fix_mul((Fix)distance, ray_y);
            across = ((u32)where >> 10) & (TEX_SIZE - 1);
            // Textures read left to right from whichever side they are seen.
            if (faces_y ? ray_y > 0 : ray_x < 0)
                across = TEX_SIZE - 1 - across;
            break;
        }
        if (distance < NEAREST)
            distance = NEAREST;

        // --- The wall: 160 / distance pixels tall, centered on the horizon.
        Fix half = (Fix)recip(distance) * HORIZON; // half its height, 16.16 pixels
        Fix top = HORIZON * FIX_ONE - half;
        int y0 = top < FIX_ONE / 2 ? 0 : (top + FIX_ONE / 2 - 1) >> 16;
        int y1 = (HORIZON * FIX_ONE + half + FIX_ONE / 2 - 1) >> 16;
        if (y1 > VIEW_ROWS)
            y1 = VIEW_ROWS;

        // The floor and ceiling first, unless the wall hides all of them.
        if (y0 > 0) {
            volatile u32* out = line; // volatile: or the compiler calls memcpy(), in ROM
            const u32* in = backdrop;
            for (int i = 0; i < VIEW_ROWS / 4; i += 8) {
                out[i] = in[i];
                out[i + 1] = in[i + 1];
                out[i + 2] = in[i + 2];
                out[i + 3] = in[i + 3];
                out[i + 4] = in[i + 4];
                out[i + 5] = in[i + 5];
                out[i + 6] = in[i + 6];
                out[i + 7] = in[i + 7];
            }
        }

        {
            u32 shade = shade_at(distance) + faces_y;
            if (shade >= SHADES)
                shade = SHADES - 1;
            shade = SHADE(shade);
            u32 step = TEXEL_STEP(distance);
            const u8* texels = wall_texels[texture] + across * TEX_SIZE;
            if (half > PAIRS_FROM * FIX_ONE / 2) {
                // A near wall: two pixels a texel. The pairs start at even
                // rows, so the wall may be a pixel shorter at each end.
                y0 = (y0 + 1) & ~1;
                y1 &= ~1;
                u32 v = (u32)(((int64_t)(y0 * FIX_ONE + FIX_ONE - top) * step) >> 16);
                Pair* out = (Pair*)(rows + y0);
                Pair* end = (Pair*)(rows + y1);
                shade |= shade << 8;
                step *= 2;
#define WALL_PAIR()                                                                                \
    do {                                                                                           \
        u32 texel = texels[v >> 26];                                                               \
        *out++ = (Pair)(texel | texel << 8 | shade);                                               \
        v += step;                                                                                 \
    } while (0)
                while (end - out >= 4) {
                    WALL_PAIR();
                    WALL_PAIR();
                    WALL_PAIR();
                    WALL_PAIR();
                }
                while (out < end)
                    WALL_PAIR();
            } else {
                u32 v = (u32)(((int64_t)(y0 * FIX_ONE + FIX_ONE / 2 - top) * step) >> 16);
                u8* out = rows + y0;
                u8* end = rows + y1;
#define WALL_PIXEL()                                                                               \
    do {                                                                                           \
        *out++ = (u8)(texels[v >> 26] | shade);                                                    \
        v += step;                                                                                 \
    } while (0)
                while (end - out >= 4) {
                    WALL_PIXEL();
                    WALL_PIXEL();
                    WALL_PIXEL();
                    WALL_PIXEL();
                }
                while (out < end)
                    WALL_PIXEL();
            }
        }

        // --- The things in front of the wall in this column, farthest first.
        for (int i = 0; i < slice_count; i++) {
            const Slice* s = &slices[slice_order[i]];
            if (col < s->first || col >= s->last || (u32)s->depth >= distance)
                continue;
            u32 u = (u32)fix_mul(col * FIX_ONE + FIX_ONE / 2 - s->left, s->across) >> 16;
            if (u > PIC_SIZE - 1)
                u = PIC_SIZE - 1;
            // Only the rows between this column's first and last opaque
            // texels: most pictures are mostly air.
            u32 opaque_from = s->spans[u * 2], opaque_to = s->spans[u * 2 + 1];
            if (opaque_from >= opaque_to)
                continue;
            int ya = (s->top + (Fix)opaque_from * s->grow) >> 16;
            int yb = ((s->top + (Fix)opaque_to * s->grow) >> 16) + 1;
            if (ya < s->top_row)
                ya = s->top_row;
            if (yb > s->bottom_row)
                yb = s->bottom_row;
            const u8* texels = s->texels + u * PIC_SIZE;
            u32 shade = s->shade;
            u32 step = s->down;
            if (s->grow > PAIRS_FROM * FIX_ONE / PIC_SIZE) {
                // A near thing: two pixels a texel, as for near walls.
                ya = (ya + 1) & ~1;
                yb &= ~1;
                u32 v = (u32)(((int64_t)(ya * FIX_ONE + FIX_ONE - s->top) * step) >> 16);
                Pair* out = (Pair*)(rows + ya);
                Pair* end = (Pair*)(rows + yb);
                shade |= shade << 8;
                step *= 2;
                while (out < end) {
                    u32 texel = texels[v >> 27];
                    if (texel)
                        *out = (Pair)(texel | texel << 8 | shade);
                    out++;
                    v += step;
                }
            } else {
                u32 v = (u32)(((int64_t)(ya * FIX_ONE + FIX_ONE / 2 - s->top) * step) >> 16);
                u8* out = rows + ya;
                u8* end = rows + yb;
                while (out < end) {
                    u32 texel = texels[v >> 27];
                    if (texel)
                        *out = (u8)(texel | shade);
                    out++;
                    v += step;
                }
            }
        }

        // --- To the page: this column is surface row `col`.
        {
            volatile Quad* out = (volatile Quad*)(page + col * SURFACE_W);
            const u32* in = line;
            for (int i = 0; i < VIEW_ROWS / 4; i += 8) {
                out[i] = in[i];
                out[i + 1] = in[i + 1];
                out[i + 2] = in[i + 2];
                out[i + 3] = in[i + 3];
                out[i + 4] = in[i + 4];
                out[i + 5] = in[i + 5];
                out[i + 6] = in[i + 6];
                out[i + 7] = in[i + 7];
            }
        }
    }
}
