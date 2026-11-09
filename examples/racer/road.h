#ifndef RACER_ROAD_H
#define RACER_ROAD_H

// The road on screen: per-line tables (depth, width, row group), a frame's
// horizontal offsets for raster_scroll() with the wrap clamp, the stripes'
// shades per row group, and the projection of a point beside the road to the
// screen. Portable arithmetic only (no hardware), so tests/racer_tests.c runs
// it on the host. Units as in track.h; screen positions in pixels.
//
// The road picture (art.c, Task 2) is a layer 256 pixels wide that wraps
// (MAP_LAYER_FIXED | MAP_LAYER_WRAP, on BG2, scrolled to 0): each line below
// the horizon holds the road for that line, road_width(y) pixels wide (rumble
// included), centred at picture pixel ROAD_PICTURE_WIDTH / 2 = 128, grass
// everywhere else. raster_scroll(2, false, f->offsets) then shows at screen x
// of line y the picture pixel (x + offsets[y]) mod 256, so on line y the road
// covers the screen pixels [a, a + road_width(y)) and their copies 256 apart,
// where
//
//     a = ROAD_PICTURE_WIDTH / 2 - road_width(y) / 2 - offsets[y]
//
// and its middle is at screen x  ROAD_PICTURE_WIDTH / 2 - offsets[y]. The
// wrap invariant: road_frame() keeps, on every road line,
//
//     -WRAP_MARGIN <= a   and   a + road_width(y) <= ROAD_PICTURE_WIDTH
//
// so the road's copies 256 pixels to either side stay in the 16 pixels of
// each line the screen never shows.

#include "track.h"

#include <serval/serval.h>

#define HORIZON 64         // the first road line; lines above are sky
#define ROAD_ROWS 12       // tile rows of road: lines HORIZON .. SCREEN_H - 1
#define ROAD_GROUPS 3      // line groups per tile row: lines 0-2, 3-5, 6-7
#define ROAD_MAX_WIDTH 160 // the road's width on the bottom line, pixels
#define ROAD_PICTURE_WIDTH 256
#define WRAP_MARGIN 16      // = ROAD_PICTURE_WIDTH - SCREEN_W
#define ROAD_SIZE_CLASSES 4 // art sizes for road_project(): 1, 1/2, 1/4, 1/8

// Shades of a row group: which colours its bank's group takes.
#define SHADE_DARK 0
#define SHADE_LIGHT 1
#define SHADE_HAZE 2 // too far for stripes: the mix of light and dark

// Builds the per-line tables (one division per line). Call once at boot,
// before anything else here.
void road_init(void);

// The road's width on line y (HORIZON <= y < SCREEN_H), rumble included,
// pixels: even (the picture is symmetric), growing from 4 at the horizon to
// ROAD_MAX_WIDTH at the bottom. 0 above the horizon.
int road_width(int y);

// How far ahead of the camera the ground on line y is, track units
// (HORIZON <= y < SCREEN_H): CAR_DEPTH on the bottom line, 96 times that on
// the horizon's. 0 above the horizon.
FIXED road_depth(int y);

// The group (0-2) of line y within its tile row: lines 0-2, 3-5 and 6-7.
int road_group(int y);

// The depth whose stripe a row group shows (road_depth() of its middle line).
FIXED road_group_depth(int row, int group);

// Whether a tile row is too far for stripes (its shades are all SHADE_HAZE):
// a segment there spans fewer than four lines (about one and a half groups),
// so the stripes would alias. Fixed: rows 0-4 (lines 64-103) are haze.
bool road_row_haze(int row);

// One frame of the road.
typedef struct {
    // raster_scroll(2, false, offsets): line y shows the road picture shifted
    // by offsets[y] (see the top). Lines above the horizon: 0.
    s16 offsets[SCREEN_H];
    // The jeep's centre on screen, x: where the car's lateral position lies
    // on the bottom line of the road drawn, so it takes whatever sideways
    // shift the clamp refused the road.
    int car_x;
    // Each row group's shade, SHADE_*: row 0 is lines HORIZON .. HORIZON + 7.
    u8 shade[ROAD_ROWS][ROAD_GROUPS];
    // The curve at the car (track_curve_at()), for the sky's drift.
    FIXED curve;
} RoadFrame;

// Works out a frame of the road for the car at `distance` along track t
// (Race.distance) and `lateral` across it (Race.lateral). No division.
void road_frame(RoadFrame* f, const Track* t, FIXED distance, FIXED lateral);

// Where a point on the ground appears.
typedef struct {
    int x, y; // the point on screen, pixels: y is the line it stands on
    // Size: draw the art of class size_class (class c is the full-size art
    // reduced to 1/2^c: class 0 full, 3 an eighth) at this scale, between
    // FX_ONE / 2 and FX_ONE, in sixteenths. The full-size art is drawn at
    // FX_ONE on the bottom line.
    FIXED scale;
    u8 size_class;
    bool visible; // false behind the car or too far to draw (near the horizon)
} Projection;

// Projects the point `ahead` track units in front of the car (an object's
// distance minus Race.distance, taken around the lap by the caller) and
// `lateral` across the road, on frame f. No division.
Projection road_project(const RoadFrame* f, FIXED ahead, FIXED lateral);

#endif // RACER_ROAD_H
