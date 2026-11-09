// The road on screen, worked out a line at a time.
//
// The picture. The road layer holds one picture of a straight road seen from
// above the car: line y of it is the road at depth road_depth(y), centred,
// road_width(y) pixels wide. Nothing in it ever changes. Everything that
// moves is done to whole lines: raster_scroll() shifts each screen line of
// the picture sideways by its own amount (curves and steering), and palette
// writes recolour each band of lines light or dark (the stripes).
//
// Depth and width. Line y, p = y - HORIZON + 1 lines below the horizon,
// shows the ground at depth CAR_DEPTH * 96 / p track units ahead of the
// camera: the bottom line (p = 96) at the car, a line halfway up twice as
// far, the horizon's line 96 times as far. The road's width on screen falls
// as 1 / depth, so it grows in a straight line from the horizon down: about
// p * 160 / 96 pixels. Both are tables built once by road_init() (one
// division per line); a frame divides nothing.
//
// Curves: the per-line curve sum. A curve bends the road a little more on
// every line up from the car: going up the screen, each line's sideways step
// (dx) grows by the curve under that line (ddx), and each line's shift (x)
// is the sum of the steps below it. A constant curve so makes a parabola,
// straight at the car and bending away toward the horizon; a curve that
// starts far ahead bends only the top lines, and sweeps down the screen as
// the car reaches it. The curve under a line is the track's at that line's
// depth (track_curve_at()); since depth only grows going up, road_frame()
// walks the track's segments once along the lines instead of looking each
// line up.
//
// Steering. Moving the car right moves the road left, by the car's lateral
// position times each line's half width: the road pivots about the
// horizon's point, as it does seen from a car.
//
// The wrap clamp, and why the jeep takes the rest. The road layer is 256
// pixels wide and wraps, and the screen shows 240 of them: a line's road
// pushed more than 16 pixels off one edge comes back in at the other. So
// every line's road must stay within [-16, 256) on screen (road.h). The
// bottom line's road is 160 pixels wide, which leaves it 56 pixels of room
// each way, about 0.7 of the car's 1.5 half-road widths of travel. So the
// road follows the car only up to 0.7 (the same on every line, so the road
// keeps its shape), and the jeep sprite moves for the rest: car_x is where
// the car's lateral position falls on the road actually drawn. Curves leave
// the bottom line alone, and near the horizon, where they shift the road
// most, it is narrow and has nearly the whole line to move in. A last clamp
// on every line keeps the invariant whatever the curves (twice the sharpest
// the circuit has would reach it at the top).
//
// Stripes: phases by row group. The road is light and dark by turns, one
// segment each. Each tile row of the picture has its own palette bank and,
// within it, three groups of lines (0-2, 3-5, 6-7) with their own colours,
// so the stripes can change colour at a third of a row. A group is light or
// dark by the segment the ground at its middle line lies in:
//
//     shade = ((car's distance - CAR_DEPTH + depth) >> SEGMENT_SHIFT) & 1
//
// As the car advances, the boundary between two segments comes nearer, so
// it moves down the screen. A car moving under a segment a frame never
// moves the stripes more than half a period (light and dark) between frames,
// so the eye never sees them crawling backwards. Far up, a segment spans
// fewer lines than about one and a half groups (four lines), so groups
// would show stripes at random: rows that far (lines 64-103) are
// SHADE_HAZE, the mix of the two.

#include "road.h"

#define ROAD_LINES (SCREEN_H - HORIZON) // 96

// The curve's pull: how much a line's sideways step grows per line, per
// curve step (track.h's CURVE_SHARP steps make the sharpest), in 1/65536
// pixels. The sharpest curve bends the horizon's line about 100 pixels.
#define CURVE_PULL 360

// The bottom line's room to move each way, pixels, and the lateral position
// (half-road widths) that uses it up: the most the road follows the car.
#define BOTTOM_ROOM (SCREEN_W / 2 + WRAP_MARGIN - ROAD_MAX_WIDTH / 2)
#define ROAD_FOLLOW_MAX (BOTTOM_ROOM * FX_ONE / (ROAD_MAX_WIDTH / 2))

// The per-line tables, by line below the horizon (i = y - HORIZON).
static FIXED line_depth[ROAD_LINES] SERVAL_EWRAM_BSS;   // track units ahead of the camera
static u8 line_half_width[ROAD_LINES] SERVAL_EWRAM_BSS; // pixels
static u8 line_class[ROAD_LINES] SERVAL_EWRAM_BSS;      // size class for road_project()
static u8 line_scale[ROAD_LINES] SERVAL_EWRAM_BSS;      // its scale, sixteenths; 0 too far
static bool row_haze[ROAD_ROWS] SERVAL_EWRAM_BSS;
static int far_line; // the first line with a scale: the farthest objects stand on

// The middle line of each group within a row (the first of two for 6-7).
static const u8 group_middle[ROAD_GROUPS] = {1, 4, 6};

void road_init(void) {
    for (int i = 0; i < ROAD_LINES; i++) {
        int p = i + 1;
        line_depth[i] = CAR_DEPTH * ROAD_LINES / p;
        line_half_width[i] = (u8)(1 + ((ROAD_MAX_WIDTH / 2 - 1) * p + ROAD_LINES / 2) / ROAD_LINES);
        // An object full size on the bottom line is p / 96 of it here: take
        // the art reduced by the largest power of two that is still no
        // smaller, and shrink that by the rest (at least a half).
        int c = 0;
        while (c + 1 < ROAD_SIZE_CLASSES && (p << (c + 1)) <= ROAD_LINES)
            c++;
        int scale = 16 * (p << c) / ROAD_LINES;
        line_class[i] = (u8)c;
        line_scale[i] = (u8)(scale >= 8 ? scale : 0);
    }
    far_line = 0;
    while (line_scale[far_line] == 0)
        far_line++;
    for (int row = 0; row < ROAD_ROWS; row++) {
        int top = row * 8;
        row_haze[row] = SEGMENT_LENGTH < line_depth[top] - line_depth[top + 4];
    }
}

int road_width(int y) {
    if (y < HORIZON || y >= SCREEN_H)
        return 0;
    return 2 * line_half_width[y - HORIZON];
}

FIXED road_depth(int y) {
    if (y < HORIZON || y >= SCREEN_H)
        return 0;
    return line_depth[y - HORIZON];
}

int road_group(int y) {
    int line = (y - HORIZON) & 7;
    return line < 3 ? 0 : line < 6 ? 1 : 2;
}

FIXED road_group_depth(int row, int group) {
    return line_depth[row * 8 + group_middle[group]];
}

bool road_row_haze(int row) {
    return row_haze[row];
}

// v * px rounded to the nearest pixel, the same both ways from 0 (v FIXED).
static int scale_px(FIXED v, int px) {
    return v >= 0 ? (v * px + FX_ONE / 2) >> FX_SHIFT : -((-v * px + FX_ONE / 2) >> FX_SHIFT);
}

// The offsets: the per-line curve sum, the lateral shift, the clamp; and
// where the jeep falls on the road drawn.
static void bend_lines(RoadFrame* f, const Track* t, FIXED at, FIXED lateral) {
    FIXED follow = int_clamp(lateral, -ROAD_FOLLOW_MAX, ROAD_FOLLOW_MAX);
    FIXED end;
    int seg = track_segment_at(t, at, &end);
    s32 x = 0, dx = 0; // the curve's shift and its step, 1/65536 pixels
    for (int i = ROAD_LINES - 1; i >= 0; i--) {
        // The segment under this line: walk on while the line is past it.
        FIXED ground = at + line_depth[i] - CAR_DEPTH;
        while (ground >= end) {
            seg = seg + 1 < t->segment_count ? seg + 1 : 0;
            end += t->segments[seg].length * SEGMENT_LENGTH;
        }
        int hw = line_half_width[i];
        int centre = SCREEN_W / 2 + ((x + 0x8000) >> 16) - scale_px(follow, hw);
        centre = int_clamp(centre, hw - WRAP_MARGIN, ROAD_PICTURE_WIDTH - hw);
        f->offsets[HORIZON + i] = (s16)(ROAD_PICTURE_WIDTH / 2 - centre);
        dx += t->segments[seg].curve * CURVE_PULL;
        x += dx;
    }
    int bottom_centre = ROAD_PICTURE_WIDTH / 2 - f->offsets[SCREEN_H - 1];
    f->car_x = bottom_centre + scale_px(lateral, line_half_width[ROAD_LINES - 1]);
}

// Each row group light or dark by the segment under its middle line.
static void shade_groups(RoadFrame* f, FIXED at) {
    for (int row = 0; row < ROAD_ROWS; row++) {
        for (int g = 0; g < ROAD_GROUPS; g++) {
            FIXED ground = at - CAR_DEPTH + road_group_depth(row, g);
            if (row_haze[row])
                f->shade[row][g] = SHADE_HAZE;
            else
                f->shade[row][g] = ((ground >> SEGMENT_SHIFT) & 1) ? SHADE_DARK : SHADE_LIGHT;
        }
    }
}

void road_frame(RoadFrame* f, const Track* t, FIXED distance, FIXED lateral) {
    FIXED at = track_around(t, distance);
    for (int y = 0; y < HORIZON; y++)
        f->offsets[y] = 0;
    bend_lines(f, t, at, lateral);
    shade_groups(f, at);
    f->curve = track_curve_at(t, at);
}

Projection road_project(const RoadFrame* f, FIXED ahead, FIXED lateral) {
    Projection p = {0, 0, 0, 0, false};
    FIXED depth = ahead + CAR_DEPTH;
    if (ahead < 0 || depth > line_depth[far_line])
        return p;
    // The nearest line whose ground is at least that far: binary search, as
    // depth falls line by line.
    int lo = far_line, hi = ROAD_LINES - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if (line_depth[mid] >= depth)
            lo = mid;
        else
            hi = mid - 1;
    }
    int centre = ROAD_PICTURE_WIDTH / 2 - f->offsets[HORIZON + lo];
    p.x = centre + scale_px(lateral, line_half_width[lo]);
    p.y = HORIZON + lo;
    p.scale = line_scale[lo] * (FX_ONE / 16);
    p.size_class = line_class[lo];
    p.visible = true;
    return p;
}
