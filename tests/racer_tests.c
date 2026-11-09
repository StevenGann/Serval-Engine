// racer: the road's and the race's arithmetic (examples/racer/road.c and
// track.c), natively. Nothing of it is on screen here, so these tests are
// what shows the road keeps its wrapped copy off screen, the stripes run
// toward the player and never all flip at once, and the race keeps its rules
// and its timing targets.

#include "test.h"

#include "../examples/racer/road.h"

// --- Helpers ----------------------------------------------------------------

// Synthetic circuits: one long segment of each curve, and S-bends.
#define SYNTHETIC(name, ...)                                                                       \
    static const TrackSegment name##_segments[] = {__VA_ARGS__};                                   \
    static const Track name = {                                                                    \
        .segments = name##_segments,                                                               \
        .segment_count = sizeof(name##_segments) / sizeof(name##_segments[0]),                     \
        .start_time = 60 * 60 * 10,                                                                \
    }

SYNTHETIC(straight, {250, 0}, {250, 0});
SYNTHETIC(sharp_right, {250, CURVE_SHARP}, {250, CURVE_SHARP});
SYNTHETIC(sharp_left, {250, -CURVE_SHARP}, {250, -CURVE_SHARP});
SYNTHETIC(s_bends, {10, CURVE_SHARP}, {10, -CURVE_SHARP}, {30, CURVE_SHARP}, {30, -CURVE_SHARP},
          {60, CURVE_SHARP}, {60, -CURVE_SHARP});
// Beyond anything the circuit has: twice the sharpest curve, so that the
// road's far end reaches the clamp.
SYNTHETIC(over_right, {250, 2 * CURVE_SHARP}, {250, 2 * CURVE_SHARP});
SYNTHETIC(over_left, {250, -2 * CURVE_SHARP}, {250, -2 * CURVE_SHARP});

// Where the road starts on screen on line y of frame f (road.h's formula).
static int road_left(const RoadFrame* f, int y) {
    return ROAD_PICTURE_WIDTH / 2 - road_width(y) / 2 - f->offsets[y];
}

// The road lines of f that break the wrap invariant.
static int wrap_breaks(const RoadFrame* f) {
    int breaks = 0;
    for (int y = HORIZON; y < SCREEN_H; y++) {
        int a = road_left(f, y);
        if (a < -WRAP_MARGIN || a + road_width(y) > ROAD_PICTURE_WIDTH)
            breaks++;
    }
    return breaks;
}

// The frames of t from distance 0 to `until`, every `step`, at every
// lateral position from -CAR_MAX_X to CAR_MAX_X by eighths, that break the
// invariant or put the jeep off screen.
static int sweep_breaks(const Track* t, FIXED step, FIXED until) {
    static RoadFrame f;
    int breaks = 0;
    for (FIXED d = 0; d < until; d += step) {
        for (FIXED lat = -CAR_MAX_X; lat <= CAR_MAX_X; lat += FX_ONE / 8) {
            road_frame(&f, t, d, lat);
            breaks += wrap_breaks(&f);
            if (f.car_x < 32 || f.car_x > SCREEN_W - 32)
                breaks++; // the jeep, 64 pixels wide, half off screen
        }
    }
    return breaks;
}

// Distance from a to b going forward around the lap of t, in
// [-length / 2, length / 2).
static FIXED gap(const Track* t, FIXED a, FIXED b) {
    FIXED len = track_length(t), d = b - a;
    while (d >= len / 2)
        d -= len;
    while (d < -len / 2)
        d += len;
    return d;
}

// Runs `frames` frames holding `buttons`; returns the events, or-ed.
static u32 hold(Race* r, u16 buttons, int frames) {
    u32 ev = 0;
    for (int i = 0; i < frames; i++)
        ev |= race_step(r, buttons);
    return ev;
}

// Starts a race on t and runs the countdown.
static void start_racing(Race* r, const Track* t) {
    race_start(r, t, 1);
    hold(r, 0, RACE_COUNTDOWN);
}

// A good driver: holds A; passes the nearest vehicle close ahead on the
// side away from it, and keeps to the middle otherwise; steers toward that,
// and so against the curves' push.
static u16 good_driver(const Race* r) {
    FIXED aim = 0, nearest = FX(250);
    for (int i = 0; i < r->traffic_count; i++) {
        const Vehicle* v = &r->traffic[i];
        FIXED ahead = gap(r->track, r->distance, v->distance);
        if (ahead >= 0 && ahead < nearest) {
            nearest = ahead;
            aim = v->lateral > 0 ? -FX_ONE * 3 / 10 : FX_ONE * 3 / 10;
        }
    }
    u16 b = BUTTON_A;
    if (r->lateral < aim - FX_ONE / 16)
        b |= BUTTON_RIGHT;
    else if (r->lateral > aim + FX_ONE / 16)
        b |= BUTTON_LEFT;
    return b;
}

// --- The road ---------------------------------------------------------------

static void wrap_invariant(void) {
    road_init();
    // The circuit: every segment, in steps finer than a segment.
    CHECK(sweep_breaks(&savanna, SEGMENT_LENGTH / 2 - 1, track_length(&savanna)) == 0);
    // Synthetic extremes: the sharpest curve both ways (the same all along,
    // so a stretch of it is enough), S-bends (all of them), and twice the
    // sharpest (which only the clamp keeps in).
    const FIXED stretch = 40 * SEGMENT_LENGTH;
    CHECK(sweep_breaks(&sharp_right, SEGMENT_LENGTH / 3, stretch) == 0);
    CHECK(sweep_breaks(&sharp_left, SEGMENT_LENGTH / 3, stretch) == 0);
    CHECK(sweep_breaks(&s_bends, SEGMENT_LENGTH / 3, track_length(&s_bends)) == 0);
    CHECK(sweep_breaks(&over_right, SEGMENT_LENGTH / 3, stretch) == 0);
    CHECK(sweep_breaks(&over_left, SEGMENT_LENGTH / 3, stretch) == 0);
    // Out of the car's range too (the caller's mistake) the road stays in.
    static RoadFrame f;
    road_frame(&f, &sharp_right, 0, FX(10));
    CHECK(wrap_breaks(&f) == 0);
    road_frame(&f, &sharp_left, 0, -FX(10));
    CHECK(wrap_breaks(&f) == 0);
}

static void straight_centred(void) {
    road_init();
    static RoadFrame f;
    for (FIXED d = 0; d < FX(200); d += FX(7)) {
        road_frame(&f, &straight, d, 0);
        int unequal = 0;
        for (int y = HORIZON; y < SCREEN_H; y++)
            unequal += f.offsets[y] != f.offsets[SCREEN_H - 1];
        CHECK(unequal == 0);
        // The road's middle at the screen's.
        CHECK(road_left(&f, SCREEN_H - 1) + ROAD_MAX_WIDTH / 2 == SCREEN_W / 2);
        CHECK(f.car_x == SCREEN_W / 2);
        CHECK(f.curve == 0);
    }
    // Steering right moves the road left and leaves the jeep in the middle,
    // until the clamp hands the rest to the jeep.
    road_frame(&f, &straight, 0, FX_ONE / 4);
    CHECK(road_left(&f, SCREEN_H - 1) + ROAD_MAX_WIDTH / 2 == SCREEN_W / 2 - ROAD_MAX_WIDTH / 8);
    CHECK(f.car_x == SCREEN_W / 2);
    road_frame(&f, &straight, 0, CAR_MAX_X);
    CHECK(f.car_x > SCREEN_W / 2 + 40);
    road_frame(&f, &straight, 0, -CAR_MAX_X);
    CHECK(f.car_x < SCREEN_W / 2 - 40);
    // At every lateral position the road keeps its shape: every line shifts
    // in proportion to its width (the road pivots about the horizon), so the
    // clamp is taken from the whole road, not line by line; and the jeep is
    // at its lateral position on the bottom line of the road drawn.
    int bent = 0, misplaced = 0;
    const int bottom = SCREEN_H - 1, bottom_half = ROAD_MAX_WIDTH / 2;
    for (FIXED lat = -CAR_MAX_X; lat <= CAR_MAX_X; lat += FX_ONE / 16) {
        road_frame(&f, &straight, 0, lat);
        int shift = road_left(&f, bottom) + bottom_half - SCREEN_W / 2;
        for (int y = HORIZON; y < SCREEN_H; y++) {
            int half = road_width(y) / 2;
            int line_shift = road_left(&f, y) + half - SCREEN_W / 2;
            bent += int_abs(line_shift * bottom_half - shift * half) > bottom_half;
        }
        int on_road = road_left(&f, bottom) + bottom_half + (lat * bottom_half) / FX_ONE;
        misplaced += int_abs(f.car_x - on_road) > 1;
    }
    CHECK(bent == 0);
    CHECK(misplaced == 0);
}

static void width_and_depth(void) {
    road_init();
    CHECK(road_width(HORIZON) > 0);
    CHECK(road_width(SCREEN_H - 1) == ROAD_MAX_WIDTH);
    CHECK(road_width(HORIZON - 1) == 0);
    CHECK(road_depth(SCREEN_H - 1) == CAR_DEPTH);
    for (int y = HORIZON; y < SCREEN_H; y++) {
        CHECK(road_width(y) % 2 == 0);
        CHECK(road_width(y) <= ROAD_MAX_WIDTH);
        CHECK(road_depth(y) > 0);
        if (y > HORIZON) {
            CHECK(road_width(y) >= road_width(y - 1));
            CHECK(road_depth(y) < road_depth(y - 1));
        }
    }
    CHECK(road_width(HORIZON + 48) > road_width(HORIZON));
    // Perspective: twice as far is half as wide (the road's half width is
    // 1 + 79 p / 96 pixels for depth CAR_DEPTH * 96 / p), within a pixel.
    CHECK(road_depth(HORIZON + 47) == 2 * CAR_DEPTH);
    CHECK(int_abs(road_width(HORIZON + 47) - ROAD_MAX_WIDTH / 2) <= 2);
    // Groups: lines 0-2, 3-5, 6-7 of each row.
    static const int groups[8] = {0, 0, 0, 1, 1, 1, 2, 2};
    for (int y = HORIZON; y < SCREEN_H; y++)
        CHECK(road_group(y) == groups[(y - HORIZON) % 8]);
}

// The shade group g of row r shows on frame f.
static int shade(const RoadFrame* f, int k) {
    return f->shade[k / ROAD_GROUPS][k % ROAD_GROUPS];
}

static void stripes_move_toward_player(void) {
    road_init();
    static RoadFrame a, b;
    // Moving forward by the depth between two groups brings the stripe the
    // farther group showed to the nearer one: the stripes come toward the
    // player. Groups are numbered from the top, k = row * 3 + group.
    int compared = 0, wrong = 0;
    for (int k = 0; k + 1 < ROAD_ROWS * ROAD_GROUPS; k++) {
        int row = k / ROAD_GROUPS, next = (k + 1) / ROAD_GROUPS;
        if (road_row_haze(row) || road_row_haze(next))
            continue;
        FIXED step =
            road_group_depth(row, k % ROAD_GROUPS) - road_group_depth(next, (k + 1) % ROAD_GROUPS);
        CHECK(step > 0);
        for (FIXED d = FX(100); d < FX(100) + 2 * SEGMENT_LENGTH; d += FX(3)) {
            road_frame(&a, &straight, d, 0);
            road_frame(&b, &straight, d + step, 0);
            wrong += shade(&a, k) != shade(&b, k + 1);
            compared++;
        }
    }
    CHECK(compared > 100);
    CHECK(wrong == 0);

    // And it shows: a boundary between a light and a dark group moves down
    // the screen, never up, as the car advances a little at a time.
    road_frame(&a, &straight, FX(300), 0);
    int boundary = -1;
    for (int k = ROAD_ROWS * ROAD_GROUPS - 2; k >= 0 && boundary < 0; k--)
        if (shade(&a, k) != SHADE_HAZE && shade(&a, k) != shade(&a, k + 1))
            boundary = k; // the nearest: between group k and k + 1
    CHECK(boundary >= 0);
    int moved_down = 0;
    for (FIXED d = FX(300) + 1; d < FX(300) + SEGMENT_LENGTH; d++) {
        road_frame(&b, &straight, d, 0);
        if (shade(&b, boundary) == shade(&b, boundary + 1)) {
            // Gone from there: it is now below, and the colours above it
            // are those it had above.
            moved_down = shade(&b, boundary + 1) == shade(&a, boundary);
            break;
        }
    }
    CHECK(moved_down);

    // No wagon wheel: at top speed a frame moves the stripes less than half
    // a period (one segment), the same at every row and so at the nearest.
    Race r;
    start_racing(&r, &straight);
    FIXED fastest = 0;
    for (int i = 0; i < 600; i++) {
        FIXED before = r.distance;
        race_step(&r, BUTTON_A);
        FIXED moved = gap(&straight, before, r.distance);
        if (moved > fastest)
            fastest = moved;
    }
    CHECK(fastest == CAR_TOP_SPEED);
    CHECK(fastest < SEGMENT_LENGTH); // a period is two segments

    // Haze: a row where a stripe period is shorter than two groups is
    // SHADE_HAZE; in fact every row where one segment spans fewer than four
    // lines, about one and a half groups (road_row_haze), and only those:
    // rows 0-4. From line 104 down there are stripes.
    for (int row = 0; row < ROAD_ROWS; row++) {
        int top = HORIZON + row * 8;
        FIXED two_groups = road_depth(top) - road_depth(top + 6);
        if (2 * SEGMENT_LENGTH < two_groups)
            CHECK(road_row_haze(row));
        CHECK(road_row_haze(row) == (SEGMENT_LENGTH < road_depth(top) - road_depth(top + 4)));
        CHECK(road_row_haze(row) == (row <= 4));
        for (int g = 0; g < ROAD_GROUPS; g++)
            CHECK((a.shade[row][g] == SHADE_HAZE) == road_row_haze(row));
    }
}

static void stripes_continuous_across_lap(void) {
    road_init();
    FIXED len = track_length(&savanna);
    CHECK(len % (2 * SEGMENT_LENGTH) == 0); // a whole number of periods
    // The last frame of a lap and the first of the next, at several speeds
    // and phases, against the same two frames in the middle of the lap: the
    // stripes are those of a continuous road.
    static RoadFrame a, b, ma, mb;
    FIXED mid = (len / 2) & ~(2 * SEGMENT_LENGTH - 1);
    int wrong = 0, all_flipped = 0;
    for (FIXED speed = FX(1); speed <= CAR_TOP_SPEED; speed += FX(1) / 2) {
        for (FIXED x = 1; x <= speed; x += 37) {
            road_frame(&a, &savanna, len - x, 0);
            road_frame(&b, &savanna, speed - x, 0);
            road_frame(&ma, &savanna, mid - x, 0);
            road_frame(&mb, &savanna, mid + speed - x, 0);
            int flips = 0, groups = 0;
            for (int k = 0; k < ROAD_ROWS * ROAD_GROUPS; k++) {
                wrong += shade(&a, k) != shade(&ma, k);
                wrong += shade(&b, k) != shade(&mb, k);
                if (shade(&a, k) != SHADE_HAZE) {
                    groups++;
                    flips += shade(&a, k) != shade(&b, k);
                }
            }
            all_flipped += flips == groups;
        }
    }
    CHECK(wrong == 0);
    CHECK(all_flipped == 0);
}

static void projection(void) {
    road_init();
    static RoadFrame f;
    road_frame(&f, &straight, FX(100), 0);
    // At the car: the bottom line, full size, the road's edges.
    Projection p = road_project(&f, 0, 0);
    CHECK(p.visible && p.y == SCREEN_H - 1 && p.x == SCREEN_W / 2);
    CHECK(p.scale == FX_ONE && p.size_class == 0);
    p = road_project(&f, 0, FX_ONE);
    CHECK(p.x == SCREEN_W / 2 + ROAD_MAX_WIDTH / 2);
    p = road_project(&f, 0, -FX_ONE);
    CHECK(p.x == SCREEN_W / 2 - ROAD_MAX_WIDTH / 2);
    // Behind the car and beyond the horizon: not drawn.
    CHECK(!road_project(&f, -1, 0).visible);
    CHECK(!road_project(&f, -FX(50), 0).visible);
    CHECK(!road_project(&f, road_depth(HORIZON), 0).visible);
    CHECK(!road_project(&f, FX(100000), 0).visible);

    // Farther is higher and smaller; the scale stays within its class's band.
    int last_y = SCREEN_H, visible = 0, bad = 0;
    FIXED last_size = FX_ONE + 1;
    for (FIXED ahead = 0; ahead < road_depth(HORIZON); ahead += FX(1)) {
        p = road_project(&f, ahead, FX(2));
        if (!p.visible)
            continue;
        visible++;
        FIXED size = p.scale >> p.size_class; // of the full-size art
        bad += p.y > last_y || size > last_size;
        bad += p.y < HORIZON || p.y >= SCREEN_H;
        bad += p.scale > FX_ONE || p.scale < FX_ONE / 2 || p.scale % (FX_ONE / 16) != 0;
        bad += p.size_class >= ROAD_SIZE_CLASSES;
        last_y = p.y;
        last_size = size;
    }
    CHECK(bad == 0);
    CHECK(visible > 400);
    CHECK(last_y < HORIZON + 12);
    CHECK(road_project(&f, FX(400), 0).y < road_project(&f, FX(40), 0).y);
    CHECK(road_project(&f, FX(400), 0).scale >> road_project(&f, FX(400), 0).size_class <
          FX_ONE / 4);

    // On a curve the edges of the road drawn and of the projection agree.
    road_frame(&f, &sharp_right, FX(100), FX_ONE / 2);
    for (FIXED ahead = 0; ahead < FX(600); ahead += FX(5)) {
        Projection l = road_project(&f, ahead, -FX_ONE), r = road_project(&f, ahead, FX_ONE);
        if (!l.visible)
            continue;
        CHECK(l.y == r.y);
        CHECK(int_abs(l.x - road_left(&f, l.y)) <= 1);
        CHECK(int_abs(r.x - (road_left(&f, r.y) + road_width(r.y))) <= 1);
    }
}

// --- The race ---------------------------------------------------------------

static void car_physics(void) {
    Race r;
    // Holding A from a standstill reaches top speed within five seconds, and
    // never passes it.
    start_racing(&r, &straight);
    int frames = 0;
    while (r.speed < CAR_TOP_SPEED && frames < 1000) {
        race_step(&r, BUTTON_A);
        frames++;
    }
    CHECK(frames <= 5 * 60);
    CHECK(frames >= 2 * 60); // not instant either
    hold(&r, BUTTON_A, 300);
    CHECK(r.speed == CAR_TOP_SPEED);
    CHECK(race_speed_kmh(&r) == 290);

    // B brakes faster than coasting slows.
    Race coast = r;
    hold(&r, BUTTON_B, 30);
    hold(&coast, 0, 30);
    CHECK(r.speed < coast.speed);
    CHECK(coast.speed < CAR_TOP_SPEED);
    CHECK(r.braking && !coast.braking);
    hold(&r, BUTTON_B, 300);
    CHECK(r.speed == 0);
    CHECK(race_speed_kmh(&r) == 0);
    hold(&coast, 0, 60 * 60);
    CHECK(coast.speed == 0); // coasting stops in the end

    // No steering at a standstill.
    FIXED lat = r.lateral;
    hold(&r, BUTTON_LEFT, 60);
    hold(&r, BUTTON_RIGHT, 30);
    CHECK(r.lateral == lat);

    // Off the road the top speed falls to about a third.
    start_racing(&r, &straight);
    hold(&r, BUTTON_A, 400);
    u32 ev = hold(&r, BUTTON_A | BUTTON_RIGHT, 120);
    CHECK(r.lateral > FX_ONE);
    CHECK(ev & RACE_EV_OFFROAD);
    CHECK(r.car_state == CAR_OFFROAD);
    hold(&r, BUTTON_A, 600);
    CHECK(r.speed > CAR_TOP_SPEED * 3 / 10 && r.speed < CAR_TOP_SPEED * 37 / 100);
    CHECK(race_step(&r, BUTTON_A) & RACE_EV_OFFROAD); // held
    // And back on it, full speed again.
    hold(&r, BUTTON_A | BUTTON_LEFT, 75);
    CHECK(r.car_state == CAR_NORMAL);
    CHECK(!(race_step(&r, BUTTON_A) & RACE_EV_OFFROAD));
    hold(&r, BUTTON_A, 400);
    CHECK(r.speed == CAR_TOP_SPEED);

    // The car never passes CAR_MAX_X, on either side, pushed by curves or not.
    const Track* tracks[] = {&straight, &sharp_left, &sharp_right};
    for (int t = 0; t < 3; t++) {
        for (int side = 0; side < 2; side++) {
            u16 steer = side ? BUTTON_RIGHT : BUTTON_LEFT;
            start_racing(&r, tracks[t]);
            int beyond = 0, at_limit = 0;
            for (int i = 0; i < 900; i++) {
                race_step(&r, BUTTON_A | steer);
                beyond += r.lateral > CAR_MAX_X || r.lateral < -CAR_MAX_X;
                at_limit += r.lateral == (side ? CAR_MAX_X : -CAR_MAX_X);
            }
            CHECK(beyond == 0);
            CHECK(at_limit > 0);
        }
    }

    // A curve pushes the car outward: a right curve to the left.
    start_racing(&r, &sharp_right);
    hold(&r, BUTTON_A, 120);
    CHECK(r.lateral < -FX_ONE / 8);
    start_racing(&r, &sharp_left);
    hold(&r, BUTTON_A, 120);
    CHECK(r.lateral > FX_ONE / 8);
    // The harder the faster: at top speed on the sharpest curve, letting go
    // leaves the road within two seconds; full steering into the curve only
    // just wins against it, and keeps the car on the road.
    start_racing(&r, &straight);
    hold(&r, BUTTON_A, 400);
    r.track = &sharp_right;
    Race curve = r;
    race_step(&curve, BUTTON_A);
    FIXED push = r.lateral - curve.lateral; // to the left, outward
    curve = r;
    race_step(&curve, BUTTON_A | BUTTON_RIGHT);
    FIXED net = curve.lateral - r.lateral; // steering's gain against it
    CHECK(push > 0 && net > 0);
    CHECK(net < push / 2);
    curve = r;
    hold(&curve, BUTTON_A, 120);
    CHECK(curve.lateral < -FX_ONE);
    curve = r;
    int off = 0;
    for (int i = 0; i < 600; i++) {
        race_step(&curve, BUTTON_A | (curve.lateral < 0 ? BUTTON_RIGHT : 0));
        off += curve.car_state != CAR_NORMAL;
    }
    CHECK(off == 0);
    CHECK(curve.speed == CAR_TOP_SPEED);
}

static void skids(void) {
    Race r;
    start_racing(&r, &straight);
    hold(&r, BUTTON_A, 400);
    r.track = &sharp_right;
    Race top = r;
    // Hard steering at speed in a sharp curve: a skid from its
    // RACE_SKID_START-th frame, for as long as it lasts, and RACE_SKID_TAIL
    // frames more.
    int frame = 0, first = 0;
    for (; frame < 40; frame++)
        if ((race_step(&r, BUTTON_A | BUTTON_RIGHT) & RACE_EV_SKID) && !first)
            first = frame + 1;
    CHECK(first == RACE_SKID_START);
    CHECK(race_step(&r, BUTTON_A | BUTTON_RIGHT) & RACE_EV_SKID);
    int tail = 0;
    while ((race_step(&r, BUTTON_A) & RACE_EV_SKID) && tail < 100)
        tail++;
    CHECK(tail == RACE_SKID_TAIL);
    // Taps shorter than that never skid.
    r = top;
    u32 ev = 0;
    for (int i = 0; i < 48; i++)
        ev |= race_step(&r, (u16)(BUTTON_A | ((i & 7) < RACE_SKID_START - 2 ? BUTTON_RIGHT : 0)));
    CHECK(!(ev & RACE_EV_SKID));
    // Nor does steering on a straight, or slowly.
    start_racing(&r, &straight);
    hold(&r, BUTTON_A, 400);
    CHECK(!(hold(&r, BUTTON_A | BUTTON_LEFT, 30) & RACE_EV_SKID));
    r = top;
    r.speed = CAR_TOP_SPEED / 2;
    CHECK(!(hold(&r, BUTTON_RIGHT, 30) & RACE_EV_SKID));
}

// A straight circuit with a rock just beyond the right edge, 60 segments in.
static const TrackSegment rocky_segments[] = {{200, 0}};
static const Scenery rocky_scenery[] = {{60 * SEGMENT_LENGTH, FX_ONE * 13 / 10, SCENERY_ROCK}};
static const Track rocky = {
    .segments = rocky_segments,
    .segment_count = 1,
    .scenery = rocky_scenery,
    .scenery_count = 1,
    .start_time = 60 * 60 * 10,
};

static void crash_and_recovery(void) {
    Race r;
    // On the road, past the rock: nothing.
    start_racing(&r, &rocky);
    u32 ev = hold(&r, BUTTON_A, 400);
    CHECK(r.distance > rocky_scenery[0].distance);
    CHECK(!(ev & RACE_EV_CRASH));

    // Off the road into it: a crash.
    start_racing(&r, &rocky);
    ev = 0;
    int frames = 0;
    while (!(ev & RACE_EV_CRASH) && frames < 1000) {
        ev = race_step(&r, BUTTON_A | BUTTON_RIGHT);
        frames++;
    }
    CHECK(ev & RACE_EV_CRASH);
    CHECK(r.speed == 0);
    CHECK(r.car_state == CAR_CRASHED);
    // Input does nothing while it lasts, about a second and a half.
    FIXED d = r.distance, lat = r.lateral;
    int crashed = 0;
    while (r.car_state == CAR_CRASHED && crashed < 1000) {
        ev = race_step(&r, (u16)(BUTTON_A | ((crashed & 8) ? BUTTON_LEFT : BUTTON_RIGHT)));
        CHECK(!(ev & RACE_EV_CRASH)); // once
        if (r.car_state == CAR_CRASHED) {
            CHECK(r.distance == d && r.lateral == lat && r.speed == 0);
        }
        crashed++;
    }
    CHECK(crashed == CAR_CRASH_FRAMES);
    CHECK(crashed >= 80 && crashed <= 100);
    // Then back at the road's middle, at rest, driving normally.
    CHECK(r.lateral == 0 && r.speed == 0 && r.car_state == CAR_NORMAL);
    hold(&r, BUTTON_A, 60);
    CHECK(r.speed > 0);
    // The clock ran on through it.
    CHECK(r.race_time == (u32)(frames + crashed + 60));
}

// A straight circuit with a jeep in the right lane, 40 segments ahead.
static const TrafficSpawn lone_spawn[] = {
    {40 * SEGMENT_LENGTH, TRAFFIC_LANE_RIGHT, TRAFFIC_JEEP},
};
static const Track lone = {
    .segments = rocky_segments,
    .segment_count = 1,
    .traffic = lone_spawn,
    .traffic_count = 1,
    .start_time = 60 * 60 * 10,
};

static void bump(void) {
    Race r;
    start_racing(&r, &lone);
    CHECK(r.traffic_count == 1);
    CHECK(r.traffic[0].speed == TRAFFIC_SPEED);
    // Behind it in its lane: a bump.
    r.lateral = TRAFFIC_LANE_RIGHT;
    u32 ev = 0;
    int frames = 0;
    while (!(ev & RACE_EV_BUMP) && frames < 2000) {
        ev = race_step(&r, BUTTON_A);
        frames++;
    }
    CHECK(ev & RACE_EV_BUMP);
    CHECK(!(ev & RACE_EV_CRASH));
    CHECK(r.speed < r.traffic[0].speed);
    CHECK(r.car_state == CAR_NORMAL);
    CHECK(r.bump_timer > 0);
    FIXED behind = gap(&lone, r.distance, r.traffic[0].distance);
    CHECK(behind >= 0); // still behind it, not through it
    // It falls back, then catches up and bumps again unless it steers.
    ev = hold(&r, 0, 30);
    CHECK(!(ev & RACE_EV_BUMP));
    CHECK(r.bump_timer == 0);

    // In the other lane it passes.
    start_racing(&r, &lone);
    r.lateral = TRAFFIC_LANE_LEFT;
    ev = 0;
    bool passed = false;
    for (int i = 0; i < 900 && !passed; i++) {
        ev |= race_step(&r, BUTTON_A);
        passed = gap(&lone, r.distance, r.traffic[0].distance) < 0;
    }
    CHECK(passed);
    CHECK(!(ev & (RACE_EV_BUMP | RACE_EV_CRASH)));
}

// A straight circuit with a jeep in the right lane 20 units behind the
// start line (close enough not to drive on from ahead).
static const TrafficSpawn behind_spawn[] = {
    {200 * SEGMENT_LENGTH - FX(20), TRAFFIC_LANE_RIGHT, TRAFFIC_JEEP},
};
static const Track behind = {
    .segments = rocky_segments,
    .segment_count = 1,
    .traffic = behind_spawn,
    .traffic_count = 1,
    .start_time = 60 * 60 * 10,
};

// Runs `frames` frames of r holding `buttons`, and counts the frames where
// the vehicle behind is not at least BUMP_GAP behind the car, and the bumps.
static void wait_behind(Race* r, u16 buttons, int frames, int* close, int* bumps) {
    for (int i = 0; i < frames; i++) {
        FIXED speed = r->speed;
        u32 ev = race_step(r, buttons);
        *bumps += (ev & RACE_EV_BUMP) != 0;
        *close += gap(r->track, r->distance, r->traffic[0].distance) > -BUMP_GAP;
        // Its speed is the car's own doing: never raised by traffic.
        *bumps += r->speed > speed && !(buttons & BUTTON_A);
    }
}

static void traffic_waits_behind(void) {
    Race r;
    int close = 0, bumps = 0;
    // A car at rest in a lane: the vehicle catching it waits behind it.
    start_racing(&r, &behind);
    r.lateral = TRAFFIC_LANE_RIGHT;
    wait_behind(&r, 0, 300, &close, &bumps);
    CHECK(close == 0 && bumps == 0);
    CHECK(r.speed == 0 && r.distance == 0);
    // Off the road (but within its reach), slower than it, accelerating.
    start_racing(&r, &behind);
    r.lateral = FX_ONE * 11 / 10;
    r.speed = FX(1);
    wait_behind(&r, BUTTON_A, 300, &close, &bumps);
    CHECK(close == 0 && bumps == 0);
    CHECK(r.speed == CAR_OFFROAD_SPEED);
    // Out of its lane, the car lets it by.
    start_racing(&r, &behind);
    r.lateral = TRAFFIC_LANE_LEFT;
    u32 ev = hold(&r, 0, 60);
    CHECK(!(ev & RACE_EV_BUMP));
    CHECK(gap(&behind, r.distance, r.traffic[0].distance) > FX(100));
    // Pulling away, the car leaves it behind (where it soon drives on from
    // out of sight ahead) and never bumps it.
    start_racing(&r, &behind);
    r.lateral = TRAFFIC_LANE_RIGHT;
    hold(&r, 0, 60);
    ev = 0;
    bool away = false;
    for (int i = 0; i < 300; i++) {
        ev |= race_step(&r, BUTTON_A);
        away |= gap(&behind, r.distance, r.traffic[0].distance) > FX(960);
    }
    CHECK(!(ev & RACE_EV_BUMP));
    CHECK(away);

    // Slower than a vehicle just ahead (steered in alongside it): no bump,
    // since the car isn't closing on it; it draws away.
    start_racing(&r, &lone);
    r.lateral = TRAFFIC_LANE_RIGHT;
    r.speed = FX(1);
    r.traffic[0].distance = r.distance + FX(5);
    ev = 0;
    for (int i = 0; i < 30; i++) {
        FIXED speed = r.speed;
        ev |= race_step(&r, 0);
        CHECK(r.speed <= speed);
    }
    CHECK(!(ev & RACE_EV_BUMP));
    CHECK(gap(&lone, r.distance, r.traffic[0].distance) > FX(40));

    // After a crash: the car is put back in the middle, in reach of both
    // lanes, at rest, with a vehicle coming; it waits behind.
    start_racing(&r, &behind);
    r.lateral = CAR_MAX_X;
    r.car_state = CAR_CRASHED;
    r.crash_timer = 2;
    close = bumps = 0;
    wait_behind(&r, 0, 240, &close, &bumps);
    CHECK(r.car_state == CAR_NORMAL && r.lateral == 0);
    CHECK(close == 0 && bumps == 0);
    CHECK(r.speed == 0);
    ev = hold(&r, BUTTON_A, 300);
    CHECK(!(ev & RACE_EV_BUMP) && r.speed == CAR_TOP_SPEED);
}

// Two jeeps in the right lane close behind the start line, the second 10
// units behind the first.
static const TrafficSpawn queue_spawn[] = {
    {200 * SEGMENT_LENGTH - FX(20), TRAFFIC_LANE_RIGHT, TRAFFIC_JEEP},
    {200 * SEGMENT_LENGTH - FX(30), TRAFFIC_LANE_RIGHT, TRAFFIC_JEEP},
};
static const Track queue = {
    .segments = rocky_segments,
    .segment_count = 1,
    .traffic = queue_spawn,
    .traffic_count = 2,
    .start_time = 60 * 60 * 10,
};

// Vehicles queueing behind a car at rest never stack: each waits BUMP_GAP
// behind the car or the vehicle ahead of it in its lane, so their sprites
// never lie one on the other.
static void traffic_queues(void) {
    Race r;
    start_racing(&r, &queue);
    r.lateral = TRAFFIC_LANE_RIGHT;
    int stacked = 0;
    for (int i = 0; i < 300; i++) {
        race_step(&r, 0);
        FIXED first = gap(&queue, r.distance, r.traffic[0].distance);
        FIXED second = gap(&queue, r.distance, r.traffic[1].distance);
        stacked += first > -BUMP_GAP || second - first > -BUMP_GAP;
    }
    CHECK(stacked == 0);
    // Both still waiting, the first BUMP_GAP behind the car, the second
    // BUMP_GAP behind it.
    CHECK(gap(&queue, r.distance, r.traffic[0].distance) == -BUMP_GAP);
    CHECK(gap(&queue, r.traffic[1].distance, r.traffic[0].distance) == BUMP_GAP);
    // The car drives off: the queue follows it, still apart.
    for (int i = 0; i < 120; i++) {
        race_step(&r, BUTTON_A);
        FIXED apart = gap(&queue, r.traffic[1].distance, r.traffic[0].distance);
        stacked += apart < BUMP_GAP;
    }
    CHECK(stacked == 0);
}

// A short straight circuit with two checkpoints.
static const TrackSegment loop_segments[] = {{20, 0}, {20, 0}};
static const Checkpoint loop_checkpoints[] = {{0, 600}, {20 * SEGMENT_LENGTH, 900}};
static const Track loop = {
    .segments = loop_segments,
    .segment_count = 2,
    .checkpoints = loop_checkpoints,
    .checkpoint_count = 2,
    .start_time = 60 * 30,
};

static void timing(void) {
    Race r;
    race_start(&r, &loop, 7);
    CHECK(r.countdown == RACE_COUNTDOWN && !r.over && r.lap == 0);
    CHECK(r.time_left == loop.start_time && r.distance == 0);
    // The countdown: driving input ignored, GO on its last frame, once.
    u32 ev = 0;
    for (int i = 1; i < RACE_COUNTDOWN; i++)
        ev |= race_step(&r, BUTTON_A | BUTTON_LEFT);
    CHECK(!(ev & RACE_EV_GO));
    CHECK(r.speed == 0 && r.lateral == 0 && r.distance == 0);
    CHECK(r.time_left == loop.start_time && r.race_time == 0);
    CHECK(race_step(&r, BUTTON_A) & RACE_EV_GO);
    CHECK(r.countdown == 0 && r.speed == 0);
    ev = race_step(&r, BUTTON_A);
    CHECK(!(ev & RACE_EV_GO));
    CHECK(r.time_left == loop.start_time - 1 && r.race_time == 1);

    // Checkpoints, laps, the finish.
    int checkpoints = 0, laps = 0, finishes = 0, gos = 0, frames = 1;
    while (!r.over && frames < 60 * 60) {
        u32 before = r.time_left;
        ev = race_step(&r, BUTTON_A);
        frames++;
        gos += (ev & RACE_EV_GO) != 0;
        if (ev & RACE_EV_CHECKPOINT) {
            checkpoints++;
            u32 bonus = loop_checkpoints[checkpoints % 2].bonus;
            CHECK(r.last_bonus == bonus);
            CHECK(r.time_left == before + bonus - 1);
        }
        if (ev & RACE_EV_LAP) {
            laps++;
            CHECK(r.lap == laps);
            CHECK(r.lap_times[laps - 1] > 0);
            CHECK(r.lap_start == r.race_time);
            u32 sum = 0;
            for (int i = 0; i < laps; i++)
                sum += r.lap_times[i];
            CHECK(sum == r.race_time);
        }
        if (ev & RACE_EV_FINISH) {
            finishes++;
            CHECK(laps == RACE_LAPS);
        }
    }
    CHECK(gos == 0);
    CHECK(laps == RACE_LAPS && finishes == 1);
    // Two a lap but none at the finish line.
    CHECK(checkpoints == 2 * RACE_LAPS - 1);
    CHECK(r.over && r.finished && r.race_time == (u32)frames);
    // The race is over: nothing changes any more.
    Race after = r;
    CHECK(hold(&r, BUTTON_A | BUTTON_LEFT, 120) == 0);
    CHECK(r.distance == after.distance && r.time_left == after.time_left &&
          r.race_time == after.race_time && r.speed == after.speed);
    // The second and third laps start at speed, so they are faster.
    CHECK(r.lap_times[1] < r.lap_times[0]);

    // Time up: with nothing held the clock runs out, once.
    race_start(&r, &loop, 7);
    hold(&r, 0, RACE_COUNTDOWN);
    ev = hold(&r, 0, (int)loop.start_time - 1);
    CHECK(!(ev & RACE_EV_TIME_UP) && r.time_left == 1);
    CHECK(race_step(&r, 0) == RACE_EV_TIME_UP);
    CHECK(r.over && !r.finished && r.time_left == 0);
    CHECK(hold(&r, BUTTON_A, 300) == 0);
    CHECK(r.distance == 0 && r.time_left == 0);
}

// Drives a whole race of t with driver, until it is over; counts events.
typedef struct {
    int crashes, bumps, checkpoints;
    int crowded;  // frames with two vehicles nearer each other than 250
    int popped;   // vehicles that drove on from ahead within sight
    int skidding; // frames of RACE_EV_SKID
    int skids;    // skids (runs of RACE_EV_SKID)
} Tally;

static Tally drive(Race* r, u16 (*driver)(const Race*)) {
    Tally n = {0, 0, 0, 0, 0, 0, 0};
    bool was_skidding = false;
    static RoadFrame f;
    hold(r, 0, RACE_COUNTDOWN);
    for (int frames = 0; !r->over && frames < 60 * 60 * 10; frames++) {
        FIXED before[RACE_TRAFFIC_MAX];
        for (int i = 0; i < r->traffic_count; i++)
            before[i] = gap(r->track, r->distance, r->traffic[i].distance);
        u32 ev = race_step(r, driver(r));
        road_frame(&f, r->track, r->distance, r->lateral);
        for (int i = 0; i < r->traffic_count; i++) {
            FIXED ahead = gap(r->track, r->distance, r->traffic[i].distance);
            if (ahead > before[i] + FX(100)) // recycled: it must come from out of sight
                n.popped += road_project(&f, ahead, r->traffic[i].lateral).visible;
        }
        bool skidding = (ev & RACE_EV_SKID) != 0;
        n.skidding += skidding;
        n.skids += skidding && !was_skidding;
        was_skidding = skidding;
        n.crashes += (ev & RACE_EV_CRASH) != 0;
        n.bumps += (ev & RACE_EV_BUMP) != 0;
        n.checkpoints += (ev & RACE_EV_CHECKPOINT) != 0;
        bool crowded = false;
        for (int i = 0; i < r->traffic_count; i++)
            for (int j = i + 1; j < r->traffic_count; j++)
                crowded |= int_abs(gap(r->track, r->traffic[i].distance, r->traffic[j].distance)) <
                           FX(250);
        n.crowded += crowded;
    }
    return n;
}

// A careless driver: as the good one, but three times a race it steers off
// the right edge until it hits something.
static int careless_crashes;
static u16 careless_driver(const Race* r) {
    FIXED lap = track_length(r->track);
    FIXED at = r->lap * lap + r->distance; // into the race
    bool wander = careless_crashes < 3 && at > (careless_crashes + 1) * (lap * RACE_LAPS / 4);
    if (r->car_state == CAR_CRASHED) {
        careless_crashes += r->crash_timer == CAR_CRASH_FRAMES; // its first frame
        return 0;
    }
    return wander ? BUTTON_A | BUTTON_RIGHT : good_driver(r);
}

static void scripted_race(void) {
    Race r;
    race_start(&r, &savanna, 1234);
    Tally n = drive(&r, good_driver);
    // A good driver finishes in about two and a half minutes, with about ten
    // seconds to spare.
    CHECK(r.finished);
    CHECK(n.crashes == 0 && n.bumps == 0);
    CHECK(n.crowded == 0); // traffic never side by side: always a way through
    CHECK(n.popped == 0);  // nor does it appear from nowhere
    // Skids are sustained, a few a lap: in the sharp curves, not every frame
    // the driver touches the stick (measured: 17 skids, 524 frames).
    CHECK(n.skids >= 6 && n.skids <= 30);
    CHECK(n.skidding < 15 * 60 && n.skidding > n.skids * RACE_SKID_TAIL);
    CHECK(n.checkpoints == 2 * RACE_LAPS - 1);
    CHECK(r.race_time >= 140 * 60 && r.race_time <= 160 * 60);
    CHECK(r.time_left >= 5 * 60 && r.time_left <= 15 * 60);
    // Laps of about fifty seconds; the first is longer by the start.
    for (int i = 0; i < RACE_LAPS; i++)
        CHECK(r.lap_times[i] >= 45 * 60 && r.lap_times[i] <= 55 * 60);
    CHECK(r.lap_times[0] > r.lap_times[1]);

    // Three crashes cost the race.
    race_start(&r, &savanna, 1234);
    careless_crashes = 0;
    n = drive(&r, careless_driver);
    CHECK(n.crashes == 3);
    CHECK(r.over && !r.finished);
}

// The fields of a race that race_start() sets, compared.
static bool same_race(const Race* a, const Race* b) {
    bool same = a->track == b->track && a->countdown == b->countdown && a->over == b->over &&
                a->finished == b->finished && a->distance == b->distance &&
                a->lateral == b->lateral && a->speed == b->speed && a->car_state == b->car_state &&
                a->crash_timer == b->crash_timer && a->bump_timer == b->bump_timer &&
                a->steer == b->steer && a->braking == b->braking && a->skid_hold == b->skid_hold &&
                a->skid_tail == b->skid_tail && a->travelled == b->travelled &&
                a->traffic_count == b->traffic_count && a->time_left == b->time_left &&
                a->race_time == b->race_time && a->last_bonus == b->last_bonus &&
                a->lap == b->lap && a->lap_start == b->lap_start &&
                a->next_checkpoint == b->next_checkpoint && a->rng == b->rng;
    for (int i = 0; i < RACE_LAPS; i++)
        same = same && a->lap_times[i] == b->lap_times[i];
    for (int i = 0; i < RACE_TRAFFIC_MAX; i++) {
        const Vehicle *u = &a->traffic[i], *v = &b->traffic[i];
        same = same && u->distance == v->distance && u->lateral == v->lateral &&
               u->speed == v->speed && u->kind == v->kind;
    }
    return same;
}

static void race_start_resets(void) {
    static Race fresh, r;
    race_start(&fresh, &savanna, 99);
    CHECK(fresh.traffic_count == savanna.traffic_count);
    CHECK(fresh.time_left == savanna.start_time);
    CHECK(fresh.next_checkpoint == 1 && fresh.lap == 0);
    // After a finished race.
    race_start(&r, &savanna, 5);
    drive(&r, good_driver);
    CHECK(r.finished);
    race_start(&r, &savanna, 99);
    CHECK(same_race(&r, &fresh));
    // After a timed-out one, crashed and off the road.
    race_start(&r, &savanna, 5);
    careless_crashes = 0;
    drive(&r, careless_driver);
    CHECK(r.over && !r.finished);
    race_start(&r, &savanna, 99);
    CHECK(same_race(&r, &fresh));
    // Mid-race, mid-crash.
    race_start(&r, &rocky, 5);
    hold(&r, 0, RACE_COUNTDOWN);
    u32 ev = 0;
    for (int i = 0; i < 1000 && !(ev & RACE_EV_CRASH); i++)
        ev = race_step(&r, BUTTON_A | BUTTON_RIGHT);
    CHECK(r.car_state == CAR_CRASHED);
    race_start(&r, &savanna, 99);
    CHECK(same_race(&r, &fresh));
    // The same seed and input give the same race.
    static Race x, y;
    race_start(&x, &savanna, 42);
    race_start(&y, &savanna, 42);
    drive(&x, good_driver);
    drive(&y, good_driver);
    CHECK(same_race(&x, &y));
}

static void records(void) {
    Records rec = {0, 0};
    Race r = {0};
    r.over = r.finished = true;
    r.race_time = 9000;
    r.lap = 3;
    r.lap_times[0] = 3100;
    r.lap_times[1] = 2900;
    r.lap_times[2] = 3000;
    CHECK(records_update(&rec, &r) == (RECORD_RACE | RECORD_LAP));
    CHECK(rec.best_race == 9000 && rec.best_lap == 2900);
    // Slower: nothing falls.
    r.race_time = 9100;
    r.lap_times[1] = 2950;
    CHECK(records_update(&rec, &r) == 0);
    CHECK(rec.best_race == 9000 && rec.best_lap == 2900);
    // Faster race, no faster lap.
    r.race_time = 8950;
    CHECK(records_update(&rec, &r) == RECORD_RACE);
    CHECK(rec.best_race == 8950 && rec.best_lap == 2900);
    // A timed-out race never sets the race record; its completed laps may
    // set the lap record, its unfinished lap never.
    Race t = {0};
    t.over = true;
    t.race_time = 7000;
    t.lap = 1;
    t.lap_times[0] = 2800;
    t.lap_times[1] = 100; // not completed: lap is 1
    CHECK(records_update(&rec, &t) == RECORD_LAP);
    CHECK(rec.best_race == 8950 && rec.best_lap == 2800);
    t.lap = 0;
    t.lap_times[0] = 100;
    CHECK(records_update(&rec, &t) == 0);
    CHECK(rec.best_lap == 2800);
    // A race still running sets nothing.
    Race running = r;
    running.over = running.finished = false;
    running.race_time = 10;
    running.lap_times[0] = 10;
    CHECK(records_update(&rec, &running) == 0);

    // A real race that runs out of time: no race record, and the best lap
    // is the fastest of the laps it completed, not the one cut short.
    static Race up;
    race_start(&up, &savanna, 5);
    careless_crashes = 0;
    drive(&up, careless_driver);
    CHECK(up.over && !up.finished && up.lap >= 1 && up.lap < RACE_LAPS);
    Records none = {0, 0};
    CHECK(records_update(&none, &up) == RECORD_LAP);
    CHECK(none.best_race == 0);
    u32 fastest = up.lap_times[0];
    for (int i = 1; i < up.lap; i++)
        fastest = up.lap_times[i] < fastest ? up.lap_times[i] : fastest;
    CHECK(none.best_lap == fastest && fastest > 0);
    // Taken again: nothing more falls.
    CHECK(records_update(&none, &up) == 0);
}

// The circuit's own data is well formed.
static void savanna_data(void) {
    FIXED len = track_length(&savanna);
    CHECK(len % (2 * SEGMENT_LENGTH) == 0);
    int bad = 0;
    for (int i = 0; i < savanna.segment_count; i++)
        bad += savanna.segments[i].length == 0 || savanna.segments[i].curve > CURVE_SHARP ||
               savanna.segments[i].curve < -CURVE_SHARP;
    for (int i = 0; i < savanna.scenery_count; i++) {
        const Scenery* s = &savanna.scenery[i];
        bad += s->distance < 0 || s->distance >= len;
        bad += i > 0 && s->distance < savanna.scenery[i - 1].distance;
        bad += int_abs(s->lateral) <= FX_ONE * 6 / 5; // clear of the road
        bad += s->kind >= SCENERY_CHECKPOINT;
    }
    CHECK(bad == 0);
    CHECK(savanna.scenery_count >= 40);
    CHECK(savanna.checkpoint_count == 2);
    CHECK(savanna.checkpoints[0].distance == 0);
    CHECK(savanna.checkpoints[1].distance > len / 3 &&
          savanna.checkpoints[1].distance < len * 2 / 3);
    CHECK(savanna.traffic_count > 0 && savanna.traffic_count <= RACE_TRAFFIC_MAX);
    // Curves of every strength both ways, and track_curve_at() follows them.
    int seen[2 * CURVE_SHARP + 1] = {0};
    FIXED at = 0;
    for (int i = 0; i < savanna.segment_count; i++) {
        const TrackSegment* s = &savanna.segments[i];
        seen[s->curve + CURVE_SHARP] = 1;
        FIXED end;
        CHECK(track_curve_at(&savanna, at) == s->curve * FX_ONE / CURVE_SHARP);
        CHECK(track_curve_at(&savanna, at + len) == s->curve * FX_ONE / CURVE_SHARP);
        CHECK(track_segment_at(&savanna, at + s->length * SEGMENT_LENGTH - 1, &end) == i);
        CHECK(end == at + s->length * SEGMENT_LENGTH);
        at += s->length * SEGMENT_LENGTH;
    }
    CHECK(at == len);
    for (int c = 0; c <= 2 * CURVE_SHARP; c++)
        CHECK(seen[c]);
    CHECK(track_curve_at(&savanna, -1) == track_curve_at(&savanna, len - 1));
}

TEST_SUITE(racer_tests, "racer", {"wrap_invariant", wrap_invariant},
           {"straight_centred", straight_centred}, {"width_and_depth", width_and_depth},
           {"stripes_move_toward_player", stripes_move_toward_player},
           {"stripes_continuous_across_lap", stripes_continuous_across_lap},
           {"projection", projection}, {"car_physics", car_physics},
           {"crash_and_recovery", crash_and_recovery}, {"bump", bump},
           {"traffic_waits_behind", traffic_waits_behind}, {"traffic_queues", traffic_queues},
           {"skids", skids}, {"timing", timing}, {"scripted_race", scripted_race},
           {"race_start_resets", race_start_resets}, {"records", records},
           {"savanna_data", savanna_data});
