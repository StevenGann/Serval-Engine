// The stage: a tall map on background 2 that the camera climbs from bottom to
// top, built at boot from segments written as text, and the turrets on its
// station hulls.
//
// The map is 11 metatiles wide (the 176-pixel field) and STAGE_ROWS tall. The
// camera starts at its bottom and moves up a pixel per frame, so the stage
// reads from the bottom of the list below to the top: about 7,000 pixels, a
// little under two minutes, before the open space where the boss waits. The
// top SCREEN_H pixels are empty: the camera stops there for the boss fight,
// and the stars scroll on by themselves (game.c).

#include "game.h"

#define STAGE_W 11 // metatiles: FIELD_W / 16
#define SEGMENT_H 16

// Legend (one character per 16x16 metatile):
//   .  open space           r  small asteroid      c  crystals
//   A B / C D  a big asteroid (2 x 2)
//   #  station hull (edges and corners follow its neighbors)
//   L  hull with a signal lamp    T  hull with a turret
static const char segment_open[SEGMENT_H][STAGE_W + 1] = {
    "...........", "...........", "...........", "...........", "...........", "...........",
    "...........", "...........", "...........", "...........", "...........", "...........",
    "...........", "...........", "...........", "...........",
};

static const char segment_rocks_a[SEGMENT_H][STAGE_W + 1] = {
    "...........", "....r......", "...........", ".AB......c.", ".CD........", "...........",
    ".......r...", "..c........", "...........", "........AB.", "r.......CD.", "...........",
    "...r.......", "...........", "......c....", "...........",
};

static const char segment_rocks_b[SEGMENT_H][STAGE_W + 1] = {
    "........r..", "..AB.......", "..CD...c...", "...........", "r..........", ".....AB....",
    ".....CD..r.", "...........", ".c.........", "...........", "........c..", "..r........",
    "...........", "AB.....AB..", "CD.....CD..", "...........",
};

static const char segment_hull_a[SEGMENT_H][STAGE_W + 1] = {
    "...........", "#####......", "#T#L#......", "#####......", "..#........", "..#........",
    "..#....####", "..#....#T##", "..######L##", "..#....####", "..#........", "#####......",
    "##T##......", "#####......", "...........", "...........",
};

static const char segment_hull_b[SEGMENT_H][STAGE_W + 1] = {
    "...........", "...#####...", "...#L#T#...", "...#####...", ".....#.....", "#....#....#",
    "###########", "#T#L###L#T#", "###########", "#....#....#", ".....#.....", "...#####...",
    "...#T#L#...", "...#####...", "...........", "...........",
};

typedef const char (*Segment)[STAGE_W + 1];

// The stage from its top (the end) to its bottom (the start). The boss's open
// space comes above all of it.
static const Segment stage_segments[] = {
    segment_open,    segment_open,    segment_rocks_a, segment_open,    segment_hull_b,
    segment_open,    segment_rocks_b, segment_open,    segment_hull_a,  segment_open,
    segment_open,    segment_rocks_a, segment_open,    segment_hull_b,  segment_open,
    segment_rocks_b, segment_open,    segment_rocks_a, segment_open,    segment_hull_a,
    segment_open,    segment_rocks_b, segment_open,    segment_rocks_a, segment_open,
    segment_open,
};
#define SEGMENT_COUNT ((int)(sizeof stage_segments / sizeof stage_segments[0]))
#define TOP_ROWS (SCREEN_H / 16) // the boss's open space
#define STAGE_ROWS (TOP_ROWS + SEGMENT_COUNT * SEGMENT_H)

int stage_start_y = STAGE_ROWS * 16 - SCREEN_H;

static u16 stage_cells[STAGE_W * STAGE_ROWS] SERVAL_EWRAM_BSS;
static char stage_text[STAGE_ROWS][STAGE_W] SERVAL_EWRAM_BSS; // the segments, stacked

static const MapLayer stage_layer = {
    .width = STAGE_W,
    .height = STAGE_ROWS,
    .cells = stage_cells,
    .metatiles = stage_metatiles,
    .metatile_count = MT_COUNT,
    .bg = 2,
};

// Turret positions (metatile coordinates), from the bottom of the stage up,
// in the order the camera reaches them.
#define MAX_TURRETS 32
static struct {
    u8 mx;
    u16 my;
} turrets[MAX_TURRETS];
static int turret_count, next_turret;

static bool is_hull(int mx, int my) {
    if (mx < 0 || mx >= STAGE_W || my < 0 || my >= STAGE_ROWS)
        return false;
    char c = stage_text[my][mx];
    return c == '#' || c == 'L' || c == 'T';
}

void stage_build(void) {
    for (int my = 0; my < STAGE_ROWS; my++) {
        int seg = (my - TOP_ROWS) / SEGMENT_H;
        for (int mx = 0; mx < STAGE_W; mx++)
            stage_text[my][mx] =
                my < TOP_ROWS ? '.' : stage_segments[seg][(my - TOP_ROWS) % SEGMENT_H][mx];
    }
    turret_count = 0;
    for (int my = STAGE_ROWS - 1; my >= 0; my--) {
        for (int mx = 0; mx < STAGE_W; mx++) {
            char c = stage_text[my][mx];
            u16 cell = MT_EMPTY;
            switch (c) {
            case 'r':
                cell = MT_ROCK;
                break;
            case 'A':
            case 'B':
            case 'C':
            case 'D':
                cell = (u16)(MT_BIG_ROCK + (c - 'A'));
                break;
            case 'c':
                cell = MT_CRYSTAL;
                break;
            case 'L':
                cell = MT_HULL_LIGHT;
                break;
            case 'T':
                cell = MT_PAD;
                if (turret_count < MAX_TURRETS) {
                    turrets[turret_count].mx = (u8)mx;
                    turrets[turret_count].my = (u16)my;
                    turret_count++;
                }
                break;
            case '#':
                cell = (u16)(MT_HULL + (is_hull(mx, my - 1) ? HULL_UP : 0) +
                             (is_hull(mx, my + 1) ? HULL_DOWN : 0) +
                             (is_hull(mx - 1, my) ? HULL_LEFT : 0) +
                             (is_hull(mx + 1, my) ? HULL_RIGHT : 0));
                break;
            default:
                break;
            }
            stage_cells[my * STAGE_W + mx] = cell;
        }
    }
}

void stage_show(void) {
    // Loading the playfield (background 2) undoes map_set_cell changes: the
    // craters of the last game are pads again.
    map_load(&stage_layer);
    map_load(&stars_layer);
    map_load(&panel_layer);
    next_turret = 0;
}

void stage_hide(void) {
    map_unload(1);
    map_unload(2);
}

// Turrets are created as their pad comes within a metatile of the screen's
// top, and enemies.c removes them once they have scrolled off the bottom.
void stage_spawn_ground(void) {
    while (next_turret < turret_count && turrets[next_turret].my * 16 + 16 >= cam_y - 16) {
        spawn_turret(turrets[next_turret].mx * 16 + 8, turrets[next_turret].my * 16 + 8);
        next_turret++;
    }
}

void stage_destroy_pad(int x, int y) {
    map_set_cell(x / 16, y / 16, MT_CRATER);
}

// The hull's lamps blink: one tile, swapped in VBlank, changes every lamp.
void stage_animate(void) {
    u32 t = (u32)frame_count() % 48;
    if (t == 0 || t == 8) {
        const u32* tiles;
        u16 first;
        art_light_frame(t == 0, &tiles, &first);
        tileset_set_tiles(first, tiles, 1);
    }
}
