#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "serval/screen.h"
#include "test.h"

// A 10x10 body that bounces (keeps 7/8) and slides (loses 1/8) on floors.
static u32 make_body(FIXED x, FIXED y, FIXED vx, FIXED vy) {
    Entity e = entity_create(C_POS | C_VEL | C_BODY);
    u32 i = entity_index(e);
    pos_x[i] = x;
    pos_y[i] = y;
    vel_x[i] = vx;
    vel_y[i] = vy;
    body_w[i] = body_h[i] = 10;
    body_bounce[i] = 224;
    body_friction[i] = 32;
    return i;
}

static void step(int frames) {
    for (int f = 0; f < frames; f++) {
        sys_movement();
        sys_physics();
    }
}

static void reset(void) {
    ecs_reset();
    physics_set_bounds(0, 0, 100, 100);
    physics_set_gravity(0, 0);
    physics_set_open_edges(0);
    physics_set_wrap(false, false);
    physics_set_contacts(false);
}

static void gravity_accelerates_bodies(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(FX(50), FX(10), 0, 0);
    step(4);
    CHECK(vel_y[i] == FX(1));
    CHECK(pos_y[i] > FX(10));
}

static void bounds_include_the_body_size(void) {
    reset();
    u32 i = make_body(FX(85), FX(50), FX(10), 0); // 10 wide: right edge at 90
    step(1);
    CHECK(pos_x[i] <= FX(90));  // stays fully inside
    CHECK(vel_x[i] == -FX(10)); // bounced back
}

static void walls_bounce_perfectly_without_gravity(void) {
    reset();
    u32 i = make_body(FX(40), FX(40), FX(3), FX(2));
    step(1000);
    CHECK(vel_x[i] == FX(3) || vel_x[i] == -FX(3));
    CHECK(vel_y[i] == FX(2) || vel_y[i] == -FX(2));
    CHECK(pos_x[i] >= 0 && pos_x[i] <= FX(90) && pos_y[i] >= 0 && pos_y[i] <= FX(90));
}

static void bodies_come_to_rest_on_the_floor(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(FX(20), FX(0), FX(2), 0);
    // Each bounce peaks no higher than the one before (y grows downward), so
    // the physics creates no energy. An apex is where the velocity turns from
    // upward to downward. Stepping once per frame can't land a bounce exactly
    // on the floor, so tiny bounces may wobble by part of a pixel: allow 1.
    FIXED last_apex = 0;
    u32 apexes = 0;
    FIXED previous_vel = vel_y[i];
    for (int f = 0; f < 1200; f++) {
        step(1);
        if (previous_vel < 0 && vel_y[i] >= 0) {
            CHECK(pos_y[i] >= last_apex - FX(1));
            last_apex = pos_y[i];
            apexes++;
        }
        previous_vel = vel_y[i];
    }
    CHECK(apexes >= 10);                   // it really bounced
    CHECK(pos_y[i] == FX(90));             // on the floor
    CHECK(vel_y[i] == 0 && vel_x[i] == 0); // and stopped sliding
}

// A body dropped from rest at height y onto the floor at 90 (no ceiling in
// reach), with the given body_bounce, for `frames` frames, with contacts on
// (sys_physics' general loop) or off (its fast loop).
typedef struct {
    FIXED highest, lowest; // the highest and lowest peaks (least and most y) after a bounce
    u32 bounces;
    bool rested; // ever lay still on the floor
    FIXED y, vy; // where it ended
} Drop;

static Drop drop(FIXED y, u32 bounce, bool contacts, int frames) {
    reset();
    physics_set_bounds(0, -1000, 100, 100);
    physics_set_gravity(0, FX_ONE / 4);
    physics_set_contacts(contacts);
    u32 i = make_body(FX(20), y, 0, 0);
    body_bounce[i] = (u8)bounce;
    Drop d = {.highest = FX(1000), .lowest = -FX(1000)};
    for (int f = 0; f < frames; f++) {
        FIXED previous_vel = vel_y[i];
        step(1);
        if (previous_vel > 0 && vel_y[i] <= 0)
            d.bounces++;
        if (d.bounces && previous_vel < 0 && vel_y[i] >= 0) { // a peak
            d.highest = pos_y[i] < d.highest ? pos_y[i] : d.highest;
            d.lowest = pos_y[i] > d.lowest ? pos_y[i] : d.lowest;
        }
        d.rested = d.rested || (pos_y[i] == FX(90) && vel_y[i] == 0);
    }
    d.y = pos_y[i];
    d.vy = vel_y[i];
    reset();
    return d;
}

// body_bounce 255 is a perfect bounce: the body comes back up to the height
// it fell from, bounce after bounce, and never comes to rest (a u8 can't hold
// 256, and 255/256 would lose a little height every time). From 75 pixels up
// the body lands exactly on the floor, at a whole frame: keeping the speed
// instead would lose a quarter pixel per frame of it on every bounce until it
// rested. Both of sys_physics' loops bounce it alike.
static void perfect_bounce_keeps_its_height(void) {
    static const int heights[] = {15, 32, 50};
    for (u32 h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
        const FIXED y = FX(heights[h]);
        Drop fast = drop(y, 255, false, 6000);
        Drop general = drop(y, 255, true, 6000);
        CHECK(!fast.rested && fast.bounces >= 80);
        CHECK(fast.highest >= y - FX(1) && fast.lowest <= y + FX(1));
        CHECK(general.rested == fast.rested && general.bounces == fast.bounces);
        CHECK(general.highest == fast.highest && general.lowest == fast.lowest);
        CHECK(general.y == fast.y && general.vy == fast.vy);
    }
    // 254 isn't special: it loses height.
    Drop lossy = drop(FX(15), 254, false, 3000);
    CHECK(lossy.lowest > FX(15) + FX(20));
    Drop lossy_general = drop(FX(15), 254, true, 3000);
    CHECK(lossy_general.lowest == lossy.lowest && lossy_general.y == lossy.y);
}

// A perfect bounce still comes to rest when it is too small to make, as any
// floor bounce does: when the body hits the floor, or would leave it, slower
// than twice one frame's gravity. A body resting on the floor stays put
// rather than hopping. Both loops alike.
static void perfect_bounce_rests_when_too_slow(void) {
    for (int general = 0; general < 2; general++) {
        reset();
        physics_set_gravity(0, FX_ONE / 4);
        physics_set_contacts(general == 1);
        u32 resting = make_body(FX(20), FX(90), 0, 0);
        u32 slow = make_body(FX(40), FX(90) - FX_ONE / 8, 0, 0); // will hit at a quarter pixel
        // Hits at 0.625 pixels per frame (2.5 times gravity) and ends 150/256
        // of a pixel past the floor: mirrored back inside that far, it could
        // keep its height only by leaving slower than a quarter pixel per
        // frame (gravity) after this frame's gravity, so it rests.
        u32 grazing = make_body(FX(60), FX(90) - 10, 0, 160);
        body_bounce[resting] = body_bounce[slow] = body_bounce[grazing] = 255;
        step(1);
        CHECK(pos_y[resting] == FX(90) && vel_y[resting] == 0);
        CHECK(pos_y[slow] < FX(90) && vel_y[slow] == FX_ONE / 4);
        CHECK(pos_y[grazing] == FX(90) && vel_y[grazing] == 0); // would leave too slowly
        step(1);
        CHECK(pos_y[slow] == FX(90) && vel_y[slow] == 0); // hit too slowly
        step(100);
        CHECK(pos_y[resting] == FX(90) && vel_y[resting] == 0);
        CHECK(pos_y[slow] == FX(90) && vel_y[slow] == 0);
        CHECK(pos_y[grazing] == FX(90) && vel_y[grazing] == 0);
    }
    reset();
}

static void resting_bodies_stay_put_without_gravity(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(FX(20), FX(90), 0, 0);
    step(10);
    CHECK(pos_y[i] == FX(90) && vel_y[i] == 0);
    physics_set_gravity(0, 0);
    step(100);
    CHECK(pos_y[i] == FX(90) && vel_y[i] == 0);
}

static void gravity_in_any_direction(void) {
    reset();
    physics_set_gravity(-FX_ONE / 4, -FX_ONE / 4); // up-left
    u32 i = make_body(FX(50), FX(50), FX(1), FX(1));
    step(1200);
    CHECK(pos_x[i] == 0 && pos_y[i] == 0); // resting in the top-left corner
    CHECK(vel_x[i] == 0 && vel_y[i] == 0);
}

static void non_bodies_are_untouched(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    Entity e = entity_create(C_POS | C_VEL); // no C_BODY
    u32 i = entity_index(e);
    pos_x[i] = FX(200); // outside the bounds
    step(10);
    CHECK(pos_x[i] == FX(200) && vel_y[i] == 0);
    physics_set_bounds(0, 0, SCREEN_W, SCREEN_H);
}

static void open_edges_let_bodies_out(void) {
    reset();
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT);
    u32 i = make_body(FX(50), FX(50), -FX(5), FX(3));
    step(30);
    CHECK(pos_x[i] < 0 && vel_x[i] == -FX(5));  // left through the left edge
    CHECK(pos_y[i] >= 0 && pos_y[i] <= FX(90)); // still bouncing top and bottom
    CHECK(vel_y[i] == FX(3) || vel_y[i] == -FX(3));
    physics_set_open_edges(0);
}

static void overlap_tests_rectangles(void) {
    reset();
    u32 a = make_body(FX(10), FX(10), 0, 0); // 10x10 at (10, 10)
    u32 b = make_body(FX(15), FX(15), 0, 0);
    CHECK(body_overlap(a, b) && body_overlap(b, a));
    pos_x[b] = FX(20); // touching edges: not overlapping
    CHECK(!body_overlap(a, b));
    pos_x[b] = FX(20) - 1; // overlapping by 1/256 of a pixel
    CHECK(body_overlap(a, b));
    pos_y[b] = FX(30);
    CHECK(!body_overlap(a, b));
}

static void wrapping_brings_bodies_back_on_the_other_side(void) {
    reset();
    physics_set_wrap(true, true);
    u32 i = make_body(FX(95), FX(50), FX(4), 0); // 10 wide, bounds 0-100
    step(1);
    CHECK(pos_x[i] == FX(99) && vel_x[i] == FX(4)); // still partly visible: no wrap yet
    step(1);
    CHECK(pos_x[i] == FX(103) - FX(110)); // fully past: re-enters from just left of 0
    step(3);
    CHECK(pos_x[i] == FX(5)); // sliding back in, same speed
    pos_y[i] = FX(2);
    vel_y[i] = -FX(5);
    step(3); // y: 2 -> -3 -> -8 -> -13, which is fully above 0 (10 tall): wraps
    CHECK(pos_y[i] == -FX(13) + FX(110));
    CHECK(vel_y[i] == -FX(5));
    physics_set_wrap(false, false);
}

// Friction below 16/256 used to round a positive speed's loss down to zero, so
// a body sliding right never stopped.
static void low_friction_stops_both_ways(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    u32 right = make_body(FX(30), FX(90), FX_ONE / 2, 0);
    u32 left = make_body(FX(60), FX(90), -FX_ONE / 2, 0);
    body_friction[right] = body_friction[left] = 10;
    step(200); // both stop long before reaching a wall
    CHECK(vel_x[right] == 0 && vel_x[left] == 0);
    CHECK(pos_x[right] - FX(30) == FX(60) - pos_x[left]); // same distance each way
}

// body_friction 0 takes nothing from a body's speed along its floor, but
// sys_physics() still stops a speed there under a sixteenth of a pixel per
// frame, as with friction (map bodies without friction keep even a crawl:
// map_tests.c). Kept because every way of skipping it tried changed bunnymark
// (physics.c, update_body()). On a floor below and on one to the right, in
// the fast loop and in the general one (contacts on); in the air, nothing.
static void slow_slides_stop_without_friction(void) {
    for (int general = 0; general < 2; general++) {
        reset();
        physics_set_contacts(general == 1);
        physics_set_gravity(0, FX_ONE / 4);
        u32 right = make_body(FX(30), FX(90), FX_ONE / 32, 0); // resting on the floor
        u32 left = make_body(FX(60), FX(90), -FX_ONE / 32, 0);
        u32 kept = make_body(FX(10), FX(90), FX_ONE / 16, 0); // the slowest speed kept
        u32 air = make_body(FX(40), FX(20), FX_ONE / 32, 0);  // falling
        body_friction[right] = body_friction[left] = body_friction[kept] = body_friction[air] = 0;
        step(1); // moved by sys_movement(), then stopped by sys_physics()
        CHECK(vel_x[right] == 0 && pos_x[right] == FX(30) + FX_ONE / 32);
        CHECK(vel_x[left] == 0 && pos_x[left] == FX(60) - FX_ONE / 32);
        CHECK(vel_x[air] == FX_ONE / 32);
        step(63);
        CHECK(vel_x[kept] == FX_ONE / 16 && pos_x[kept] == FX(14));
        CHECK(pos_x[right] == FX(30) + FX_ONE / 32 && pos_y[right] == FX(90));

        reset();
        physics_set_contacts(general == 1);
        physics_set_gravity(FX_ONE / 4, 0);
        u32 down = make_body(FX(90), FX(30), 0, FX_ONE / 32); // resting on the right wall
        body_friction[down] = 0;
        step(1);
        CHECK(vel_y[down] == 0 && pos_y[down] == FX(30) + FX_ONE / 32 && pos_x[down] == FX(90));
    }
    reset();
}

// Falling onto the floor and "falling" up onto the ceiling are mirror images.
static u32 fall(FIXED gravity, FIXED y, FIXED vy, int frames) {
    reset();
    physics_set_gravity(0, gravity);
    u32 i = make_body(FX(20), y, 0, vy);
    step(frames);
    return i;
}

static void floor_bounces_are_symmetric(void) {
    static const int checkpoints[] = {5, 9, 17, 33, 65, 129};
    bool mirrored = true;
    for (u32 c = 0; c < sizeof(checkpoints) / sizeof(checkpoints[0]); c++) {
        u32 i = fall(FX_ONE / 4, FX(80) + 37, FX(3) + 101, checkpoints[c]);
        FIXED down_y = pos_y[i], down_vel = vel_y[i];
        i = fall(-FX_ONE / 4, FX(10) - 37, -(FX(3) + 101), checkpoints[c]);
        mirrored &= down_y + pos_y[i] == FX(90) && down_vel == -vel_y[i];
    }
    CHECK(mirrored);
}

static void stationary_body_at_wrap_edge_stays_put(void) {
    reset();
    physics_set_wrap(true, false);
    u32 i = make_body(FX(100), FX(50), 0, 0); // exactly at the right bound
    step(1);
    FIXED x = pos_x[i];
    step(1);
    CHECK(pos_x[i] == x);
    step(1);
    CHECK(pos_x[i] == x);
    physics_set_wrap(false, false);
}

static void inverted_bounds_are_ignored(void) {
    reset();
    u32 warnings = debug_warning_count();
    physics_set_bounds(100, 0, 0, 100);
    physics_set_bounds(0, 100, 100, 0);
    physics_set_bounds(100, 0, 0, 100);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    u32 i = make_body(FX(85), FX(50), FX(10), 0); // still bounces off x = 90
    step(1);
    CHECK(pos_x[i] == FX(85) && vel_x[i] == -FX(10));
}

static void body_bigger_than_bounds_is_pinned(void) {
    reset();
    u32 warnings = debug_warning_count();
    u32 i = make_body(FX(50), FX(50), 0, 0);
    body_w[i] = 200;
    step(3);
    CHECK(pos_x[i] == 0 && vel_x[i] == 0);
    step(1);
    CHECK(pos_x[i] == 0 && vel_x[i] == 0);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
}

// body_max_fall limits the speed gravity builds up, in whichever direction it
// pulls, and only on the axes it acts on.
static void max_fall_limits_falling(void) {
    reset();
    physics_set_bounds(-100000, -100000, 100000, 100000); // far walls
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(FX(10), 0, FX(1), 0);
    u32 free = make_body(FX(40), 0, 0, 0); // no limit
    body_max_fall[i] = FX(3);
    FIXED fastest = 0;
    for (int f = 0; f < 60; f++) {
        step(1);
        fastest = vel_y[i] > fastest ? vel_y[i] : fastest;
    }
    CHECK(fastest == FX(3));
    CHECK(vel_y[i] == FX(3));
    CHECK(vel_y[free] == FX(15)); // 60 frames of a quarter pixel
    CHECK(vel_x[i] == FX(1));     // no gravity sideways: untouched
    // A jump against gravity isn't limited.
    vel_y[i] = -FX(6);
    step(1);
    CHECK(vel_y[i] == -FX(6) + FX_ONE / 4);
    // Upward and sideways gravity.
    physics_set_gravity(-FX_ONE / 2, -FX_ONE / 4);
    vel_x[i] = vel_y[i] = 0;
    step(60);
    CHECK(vel_x[i] == -FX(3) && vel_y[i] == -FX(3));
    reset();
}

// The limit also applies to wrapping bodies.
static void max_fall_limits_wrapping_bodies(void) {
    reset();
    physics_set_wrap(false, true);
    physics_set_gravity(0, FX_ONE);
    u32 i = make_body(FX(10), 0, 0, 0);
    body_max_fall[i] = FX(2);
    step(30);
    CHECK(vel_y[i] == FX(2));
    reset();
}

static u32 make_collider(int x, int y, u8 w, u8 h, FIXED vx, FIXED vy) {
    u32 i = entity_index(entity_create(C_POS | C_BODY | ((vx || vy) ? C_VEL : 0)));
    pos_x[i] = FX(x);
    pos_y[i] = FX(y);
    vel_x[i] = vx;
    vel_y[i] = vy;
    body_w[i] = w;
    body_h[i] = h;
    return i;
}

// a (8x8) arrives at b (16x16 at 50, 50, still) from each side.
static void hit_side_from_each_side(void) {
    reset();
    u32 b = make_collider(50, 50, 16, 16, 0, 0);
    u32 a = make_collider(54, 44, 8, 8, 0, FX(3)); // was at y 41, bottom 49: came down
    CHECK(body_hit_side(a, b) == BODY_SIDE_BOTTOM);
    CHECK(body_hit_side(b, a) == BODY_SIDE_TOP); // b sees a land on its top
    pos_y[a] = FX(64);
    vel_y[a] = -FX(3); // was at 67, top at b's bottom edge (66) or below
    CHECK(body_hit_side(a, b) == BODY_SIDE_TOP);
    CHECK(body_hit_side(b, a) == BODY_SIDE_BOTTOM);
    pos_x[a] = FX(44);
    pos_y[a] = FX(54);
    vel_x[a] = FX(2);
    vel_y[a] = 0;
    CHECK(body_hit_side(a, b) == BODY_SIDE_RIGHT); // a ran into b's left side
    CHECK(body_hit_side(b, a) == BODY_SIDE_LEFT);
    pos_x[a] = FX(64);
    vel_x[a] = -FX(2);
    CHECK(body_hit_side(a, b) == BODY_SIDE_LEFT);
    CHECK(body_hit_side(b, a) == BODY_SIDE_RIGHT);
    reset();
}

// No overlap, no side; touching edges don't overlap (as in body_overlap).
static void hit_side_needs_overlap(void) {
    reset();
    u32 b = make_collider(50, 50, 16, 16, 0, 0);
    u32 a = make_collider(54, 42, 8, 8, 0, FX(3)); // bottom edge at 50: touching
    CHECK(!body_overlap(a, b));
    CHECK(body_hit_side(a, b) == 0);
    pos_x[a] = FX(66); // right of b, touching, overlapping in y
    pos_y[a] = FX(54);
    CHECK(body_hit_side(a, b) == 0);
    pos_x[a] = FX(100);
    CHECK(body_hit_side(a, b) == 0);
    CHECK(body_hit_side(b, b) == 0);
    // One subpixel in: now it counts.
    pos_x[a] = FX(66) - 1;
    vel_x[a] = -FX(1);
    vel_y[a] = 0;
    CHECK(body_hit_side(a, b) == BODY_SIDE_LEFT);
    // Touching before the frame counts as clear: the side is still known.
    pos_x[a] = FX(65);
    CHECK(body_hit_side(a, b) == BODY_SIDE_LEFT);
    // Even when a went in deeper than it overlaps vertically: it was
    // touching b's left side (x 42-50) and moved 6 pixels right.
    pos_x[a] = FX(48);
    pos_y[a] = FX(64); // 2 pixels into b's bottom
    vel_x[a] = FX(6);
    CHECK(body_hit_side(a, b) == BODY_SIDE_RIGHT);
    reset();
}

// A fast body that ends up past the middle of a thin one still hit the side
// it came from, judged by the motion, not by where it ended up.
static void hit_side_of_fast_bodies(void) {
    reset();
    u32 b = make_collider(50, 50, 32, 4, 0, 0);     // a thin platform
    u32 a = make_collider(60, 51, 8, 8, 0, FX(12)); // was at y 39, now mostly below it
    CHECK(pos_y[a] + FX(4) > pos_y[b] + FX(2));     // its center is below b's
    CHECK(body_hit_side(a, b) == BODY_SIDE_BOTTOM);
    // Sideways through a thin wall.
    u32 wall = make_collider(100, 0, 2, 100, 0, 0);
    u32 c = make_collider(101, 40, 8, 8, FX(10), 0);
    CHECK(body_hit_side(c, wall) == BODY_SIDE_RIGHT);
    reset();
}

// Only relative motion matters: a body falling onto one that falls faster
// can't have stomped it; a fast riser hits a slow faller from below.
static void hit_side_uses_relative_motion(void) {
    reset();
    // a falls slowly, b rises into it from below: a's bottom met b's top.
    u32 a = make_collider(50, 50, 8, 8, 0, FX(1));
    u32 b = make_collider(50, 57, 8, 8, 0, -FX(4)); // was at 61: below a (a was at 49..57)
    CHECK(body_hit_side(a, b) == BODY_SIDE_BOTTOM);
    // Both falling, b faster from above: b's bottom met a's top, though both
    // moved down.
    vel_y[b] = FX(6);
    pos_y[b] = FX(43); // was at 37, a was at 49: b above a
    CHECK(body_hit_side(b, a) == BODY_SIDE_BOTTOM);
    CHECK(body_hit_side(a, b) == BODY_SIDE_TOP);
    // b drops onto a, which moves sideways: by a's own motion alone they
    // overlapped already (and least on a's right side), but relative to a, b
    // came from above.
    vel_x[a] = FX(1);
    vel_y[a] = 0;
    pos_x[b] = FX(55);
    pos_y[b] = FX(47); // was at 41, a's top at 50
    vel_x[b] = 0;
    vel_y[b] = FX(6);
    CHECK(body_hit_side(a, b) == BODY_SIDE_TOP);
    CHECK(body_hit_side(b, a) == BODY_SIDE_BOTTOM);
    // Moving together: no relative motion, they overlapped before already, so
    // no side is guessed (by least overlap it would be a's right side).
    pos_x[b] = FX(56);
    pos_y[b] = FX(50);
    vel_x[a] = vel_x[b] = FX(5);
    vel_y[a] = vel_y[b] = FX(5);
    CHECK(body_hit_side(a, b) == BODY_SIDE_INSIDE);
    CHECK(body_hit_side(b, a) == BODY_SIDE_INSIDE);
    reset();
}

// Clear on both axes before the frame: the axis crossed last decides; an
// exact corner goes to top/bottom.
static void hit_side_at_corners(void) {
    reset();
    u32 b = make_collider(50, 50, 16, 16, 0, 0);
    // Exactly diagonal: was at (40, 40), corner to corner with b at (50, 50).
    u32 a = make_collider(44, 44, 10, 10, FX(4), FX(4));
    CHECK(body_hit_side(a, b) == BODY_SIDE_BOTTOM);
    CHECK(body_hit_side(b, a) == BODY_SIDE_TOP);
    // Mostly sideways: x closed its gap (4) after y did (1).
    pos_x[a] = FX(44);
    pos_y[a] = FX(43);
    vel_x[a] = FX(8); // was at 36: x gap 4, entered at 1/2
    vel_y[a] = FX(4); // was at 39: y gap 1, entered at 1/4
    CHECK(body_hit_side(a, b) == BODY_SIDE_RIGHT);
    // Both gaps 7 pixels: x at 8 px/frame entered at 7/8 of the frame, y at
    // 16 px/frame at 7/16, so x was crossed last.
    pos_x[a] = FX(41);
    pos_y[a] = FX(49);
    vel_x[a] = FX(8);
    vel_y[a] = FX(16);
    CHECK(body_hit_side(a, b) == BODY_SIDE_RIGHT);
    // Swapped speeds: y crossed last.
    pos_x[a] = FX(49);
    pos_y[a] = FX(41);
    vel_x[a] = FX(16);
    vel_y[a] = FX(8);
    CHECK(body_hit_side(a, b) == BODY_SIDE_BOTTOM);
    reset();
}

// A static collider (no C_VEL) counts as still even if vel holds leftovers.
static void hit_side_with_static_colliders(void) {
    reset();
    u32 paddle = make_collider(10, 40, 8, 32, 0, 0);
    vel_x[paddle] = -FX(20); // ignored: no C_VEL
    vel_y[paddle] = -FX(20);
    u32 ball = make_collider(15, 50, 8, 8, -FX(4), FX(1)); // was at x 19: in front
    CHECK(body_hit_side(ball, paddle) == BODY_SIDE_LEFT);
    CHECK(body_hit_side(paddle, ball) == BODY_SIDE_RIGHT);
    // From behind the paddle's face: was at x 9, overlapping in x already,
    // above the paddle (y was 31): the paddle's top end.
    pos_x[ball] = FX(12);
    pos_y[ball] = FX(34);
    vel_x[ball] = FX(3);
    vel_y[ball] = FX(3);
    CHECK(body_hit_side(ball, paddle) == BODY_SIDE_BOTTOM);
    reset();
}

// Bodies that overlapped before this frame's movement get BODY_SIDE_INSIDE,
// never a guessed side: not the side of least overlap, which can be a face
// the other body has already passed.
static void hit_side_inside_when_overlapping_before(void) {
    reset();
    u32 b = make_collider(50, 50, 16, 16, 0, 0);
    // Spawned inside, still: 1 pixel from b's left side, which least overlap
    // would report.
    u32 a = make_collider(43, 54, 8, 8, 0, 0);
    CHECK(body_hit_side(a, b) == BODY_SIDE_INSIDE);
    CHECK(body_hit_side(b, a) == BODY_SIDE_INSIDE);
    // Moving out through b's top: overlapped before (y was 58), overlaps now.
    ent_mask[a] |= C_VEL;
    pos_x[a] = FX(54);
    vel_x[a] = 0;
    vel_y[a] = -FX(4);
    CHECK(body_hit_side(a, b) == BODY_SIDE_INSIDE);
    // Deep inside, moving fast: still no side.
    pos_y[a] = FX(52);
    vel_y[a] = FX(3);
    CHECK(body_hit_side(a, b) == BODY_SIDE_INSIDE);
    // Overlapping on one axis only before the frame: the other axis was
    // crossed, so that side is known.
    pos_x[a] = FX(44);
    pos_y[a] = FX(54);
    vel_x[a] = FX(2); // was at x 42: touching b's left side
    vel_y[a] = FX(1);
    CHECK(body_hit_side(a, b) == BODY_SIDE_RIGHT);
    // The result is non-zero whenever they overlap, and 0 once they don't.
    pos_x[a] = FX(100);
    CHECK(body_hit_side(a, b) == 0);
    reset();
}

// Pong's left paddle (x 8-16) moved up 3 pixels a frame by the game, without
// C_VEL, and a ball moving (-3, +1.5) coming down onto its top end. The ball
// is never in front of the face while they overlap, so the face (the ball's
// left side) must never be reported.
static void hit_side_paddle_moved_onto_ball(void) {
    reset();
    u32 paddle = make_collider(8, 26, 8, 32, 0, 0);
    u32 ball = make_collider(12, 21, 8, 8, -FX(3), FX(3) / 2);
    pos_x[ball] = FX(12) + FX_ONE / 2; // x 12.5-20.5, was at 15.5: already over x 8-16
    for (int frame = 14; frame <= 16; frame++) {
        // Least overlap said BOTTOM, then LEFT (the face, 6.5 pixels behind
        // it: Pong would catch the ball with the paddle's end), then RIGHT.
        CHECK(body_overlap(ball, paddle));
        CHECK(body_hit_side(ball, paddle) == BODY_SIDE_INSIDE);
        CHECK(body_hit_side(paddle, ball) == BODY_SIDE_INSIDE);
        pos_y[paddle] -= FX(3);
        pos_x[ball] += vel_x[ball];
        pos_y[ball] += vel_y[ball];
    }
    // A paddle that has C_VEL (and is moved by sys_movement) has its motion
    // counted: at the first frame they overlap, the paddle was 3 pixels lower
    // (y 29), clear of the ball (bottom at 27.5), so the ball landed on it.
    ent_mask[paddle] |= C_VEL;
    pos_y[paddle] = FX(26);
    vel_y[paddle] = -FX(3);
    vel_x[paddle] = 0;
    pos_x[ball] = FX(12) + FX_ONE / 2;
    pos_y[ball] = FX(21);
    CHECK(body_hit_side(ball, paddle) == BODY_SIDE_BOTTOM);
    CHECK(body_hit_side(paddle, ball) == BODY_SIDE_TOP);
    reset();
}

// body_gravity scales gravity per body; the zero entity_create() leaves is
// normal gravity.
static void gravity_scale_per_body(void) {
    reset();
    physics_set_bounds(-100000, -100000, 100000, 100000); // far walls
    physics_set_gravity(FX_ONE / 8, FX_ONE / 4);
    u32 normal = make_body(0, 0, 0, 0);
    u32 half = make_body(0, 0, 0, 0);
    u32 none = make_body(0, 0, 0, 0);
    u32 reversed = make_body(0, 0, 0, 0);
    u32 twice = make_body(0, 0, 0, 0);
    CHECK(body_gravity[normal] == BODY_GRAVITY(16));
    body_gravity[half] = BODY_GRAVITY(8);
    body_gravity[none] = BODY_GRAVITY(0);
    body_gravity[reversed] = BODY_GRAVITY(-16);
    body_gravity[twice] = BODY_GRAVITY(32);
    step(4);
    CHECK(vel_x[normal] == FX_ONE / 2 && vel_y[normal] == FX(1));
    CHECK(vel_x[half] == FX_ONE / 4 && vel_y[half] == FX_ONE / 2);
    CHECK(vel_x[none] == 0 && vel_y[none] == 0 && pos_y[none] == 0);
    CHECK(vel_x[reversed] == -FX_ONE / 2 && vel_y[reversed] == -FX(1));
    CHECK(vel_x[twice] == FX(1) && vel_y[twice] == FX(2));
    // The same in a wrapping world, which takes the other loop.
    physics_set_wrap(true, true);
    step(4);
    CHECK(vel_y[normal] == FX(2) && vel_y[half] == FX(1) && vel_y[none] == 0);
    CHECK(vel_y[reversed] == -FX(2) && vel_y[twice] == FX(4));
    // A scale only matters while there is gravity.
    physics_set_gravity(0, 0);
    step(4);
    CHECK(vel_y[normal] == FX(2) && vel_y[twice] == FX(4));
    reset();
}

// Scaled gravity rounds its magnitude down, so reversing a scale only flips
// the sign.
static void gravity_scale_rounds_symmetrically(void) {
    reset();
    physics_set_bounds(-100000, -100000, 100000, 100000);
    physics_set_gravity(0, 5); // 5/256 pixel: half of it is 2.5
    u32 down = make_body(0, 0, 0, 0), up = make_body(0, 0, 0, 0);
    body_gravity[down] = BODY_GRAVITY(8);
    body_gravity[up] = BODY_GRAVITY(-8);
    step(1);
    CHECK(vel_y[down] == 2 && vel_y[up] == -2);
    physics_set_gravity(0, -5);
    step(1);
    CHECK(vel_y[down] == 0 && vel_y[up] == 0);
    reset();
}

// Floors follow each body's own gravity.
static void floors_follow_the_body_gravity(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    // Without gravity, the bottom wall isn't a floor: a bounce keeps all its
    // speed, though body_bounce is 0, and there is no friction.
    u32 ball = make_body(FX(40), FX(80), FX(1), FX(3));
    body_bounce[ball] = 0;
    body_gravity[ball] = BODY_GRAVITY(0);
    step(4);
    CHECK(vel_y[ball] == -FX(3) && vel_x[ball] == FX(1));
    // With reversed gravity, a body comes to rest on the ceiling.
    u32 balloon = make_body(FX(20), FX(50), 0, 0);
    body_gravity[balloon] = BODY_GRAVITY(-16);
    step(1200);
    CHECK(pos_y[balloon] == 0 && vel_y[balloon] == 0);
    // Normal gravity next to them: on the floor.
    u32 rock = make_body(FX(60), FX(50), 0, 0);
    step(1200);
    CHECK(pos_y[rock] == FX(90) && vel_y[rock] == 0);
    CHECK(pos_y[balloon] == 0);
    reset();
}

// body_max_fall limits a body's fall under its own gravity.
static void max_fall_with_gravity_scale(void) {
    reset();
    physics_set_bounds(-100000, -100000, 100000, 100000);
    physics_set_gravity(0, FX_ONE / 4);
    u32 up = make_body(0, 0, 0, 0);
    body_gravity[up] = BODY_GRAVITY(-16);
    body_max_fall[up] = FX(2);
    step(30);
    CHECK(vel_y[up] == -FX(2));
    reset();
}

// Contacts: one wall on each side of the body.
static void contacts_report_each_wall(void) {
    reset();
    physics_set_contacts(true);
    // Bounds 0-100, bodies 10x10: the far walls are at 90.
    u32 left = make_body(FX(1), FX(50), -FX(2), 0);
    u32 right = make_body(FX(89), FX(30), FX(2), 0);
    u32 top = make_body(FX(30), FX(1), 0, -FX(2));
    u32 bottom = make_body(FX(50), FX(89), 0, FX(2));
    u32 corner = make_body(FX(89), FX(89), FX(2), FX(2));
    u32 middle = make_body(FX(40), FX(40), FX(1), FX(1));
    step(1);
    CHECK(body_contact[left] == BODY_SIDE_LEFT);
    CHECK(body_contact[right] == BODY_SIDE_RIGHT);
    CHECK(body_contact[top] == BODY_SIDE_TOP);
    CHECK(body_contact[bottom] == BODY_SIDE_BOTTOM);
    CHECK(body_contact[corner] == (BODY_SIDE_RIGHT | BODY_SIDE_BOTTOM));
    CHECK(body_contact[middle] == 0);
    // They bounced: next frame they are moving away, and touch nothing.
    step(1);
    CHECK(body_contact[left] == 0 && body_contact[corner] == 0);
    CHECK(vel_x[left] == FX(2));
    // The same bits as map bodies'.
    CHECK(BODY_SIDE_BOTTOM == MAP_CONTACT_FLOOR && BODY_SIDE_TOP == MAP_CONTACT_CEILING &&
          BODY_SIDE_LEFT == MAP_CONTACT_LEFT && BODY_SIDE_RIGHT == MAP_CONTACT_RIGHT);
    reset();
}

static void contacts_while_resting_and_off(void) {
    reset();
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(FX(20), FX(90), 0, 0); // on the floor
    step(2);
    CHECK(body_contact[i] == 0); // off by default
    physics_set_contacts(true);
    for (int f = 0; f < 5; f++) {
        step(1);
        CHECK(body_contact[i] == BODY_SIDE_BOTTOM); // every frame it rests there
    }
    physics_set_contacts(false); // clears them
    CHECK(body_contact[i] == 0);
    step(1);
    CHECK(body_contact[i] == 0);
    reset();
}

// A body that leaves through an open edge gets BODY_CONTACT_EXIT and that
// edge's side, on the frame it is entirely outside, and only then.
static void contacts_report_exits_through_open_edges(void) {
    reset();
    physics_set_contacts(true);
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_BOTTOM);
    u32 out_left = make_body(FX(3), FX(50), -FX(4), 0); // 10 wide: out at x <= -10
    u32 out_bottom = make_body(FX(50), FX(95), 0, FX(3));
    u32 bouncer = make_body(FX(89), FX(30), FX(2), 0); // the right edge is closed
    step(1);
    CHECK(body_contact[out_left] == 0 && body_contact[bouncer] == BODY_SIDE_RIGHT);
    CHECK(body_contact[out_bottom] == 0); // y 98: still partly inside
    step(1);
    CHECK(body_contact[out_left] == 0);                                        // x -5
    CHECK(body_contact[out_bottom] == (BODY_CONTACT_EXIT | BODY_SIDE_BOTTOM)); // y 101
    step(1);
    CHECK(body_contact[out_left] == 0);   // x -9: its right column is still inside
    CHECK(body_contact[out_bottom] == 0); // already out
    step(1);
    CHECK(body_contact[out_left] == (BODY_CONTACT_EXIT | BODY_SIDE_LEFT)); // x -13
    step(5);
    CHECK(body_contact[out_left] == 0 && pos_x[out_left] < -FX(20));
    // Exactly reaching the edge counts as out.
    u32 exact = make_body(FX(2), FX(20), -FX(12), 0);
    step(1);
    CHECK(pos_x[exact] == -FX(10));
    CHECK(body_contact[exact] == (BODY_CONTACT_EXIT | BODY_SIDE_LEFT));
    reset();
}

// The exact frame of BODY_CONTACT_EXIT, with bodies moving a pixel per frame:
// the one they become entirely outside on, pos_x + body_w <= left or
// pos_x >= right (likewise vertically; right and bottom are exclusive). At
// pos_x == left - body_w the body's last column is already outside, a frame
// before a game's own `pos_x < left - body_w` says so.
static void contacts_report_exits_on_the_exact_frame(void) {
    reset();
    physics_set_contacts(true);
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT | PHYSICS_EDGE_TOP |
                           PHYSICS_EDGE_BOTTOM);
    // 10x10, two pixels inside an edge each.
    u32 left = make_body(-FX(8), FX(20), -FX(1), 0);
    u32 right = make_body(FX(98), FX(60), FX(1), 0);
    u32 top = make_body(FX(20), -FX(8), 0, -FX(1));
    u32 bottom = make_body(FX(60), FX(98), 0, FX(1));
    step(1); // one pixel still inside each edge
    CHECK(pos_x[left] == -FX(9) && body_contact[left] == 0);
    CHECK(pos_x[right] == FX(99) && body_contact[right] == 0);
    CHECK(pos_y[top] == -FX(9) && body_contact[top] == 0);
    CHECK(pos_y[bottom] == FX(99) && body_contact[bottom] == 0);
    step(1); // entirely outside
    CHECK(pos_x[left] == -FX(10) && body_contact[left] == (BODY_CONTACT_EXIT | BODY_SIDE_LEFT));
    CHECK(pos_x[right] == FX(100) && body_contact[right] == (BODY_CONTACT_EXIT | BODY_SIDE_RIGHT));
    CHECK(pos_y[top] == -FX(10) && body_contact[top] == (BODY_CONTACT_EXIT | BODY_SIDE_TOP));
    CHECK(pos_y[bottom] == FX(100) &&
          body_contact[bottom] == (BODY_CONTACT_EXIT | BODY_SIDE_BOTTOM));
    CHECK(!(pos_x[left] < -FX(10))); // the game's own test: not yet
    step(1);
    CHECK(body_contact[left] == 0 && body_contact[right] == 0); // that frame only
    CHECK(body_contact[top] == 0 && body_contact[bottom] == 0);
    CHECK(pos_x[left] < -FX(10)); // the game's own test, a frame late
    reset();
}

// A wrapping axis has no walls: no contacts on it, while the other axis
// still reports its walls.
static void wrapping_axes_report_no_contacts(void) {
    reset();
    physics_set_contacts(true);
    physics_set_wrap(true, false);
    physics_set_open_edges(PHYSICS_EDGE_LEFT); // ignored on a wrapping axis
    u32 i = make_body(FX(2), FX(89), -FX(5), FX(2));
    bool seen_left = false;
    for (int f = 0; f < 40; f++) {
        step(1);
        seen_left = seen_left || (body_contact[i] & (BODY_SIDE_LEFT | BODY_SIDE_RIGHT));
        if (f == 0)
            CHECK(body_contact[i] == BODY_SIDE_BOTTOM);
    }
    CHECK(!seen_left);
    CHECK(pos_x[i] > 0); // it wrapped around
    reset();
}

// A screen-space entity (SPRITE_SCREEN) against a world-space one: compared
// in the world, adding the camera to the screen-space one's position.
static void screen_and_world_bodies_meet_in_the_world(void) {
    reset();
    u32 shot = make_body(FX(20), FX(30), 0, -FX(4)); // on the screen
    spr_flags[shot] = SPRITE_SCREEN;
    u32 turret = make_body(FX(20), FX(522), 0, 0); // in the world, its bottom at 532
    u32 other = make_body(FX(20), FX(30), 0, 0);   // in the world, where the shot is drawn
    camera_set(0, 500);
    CHECK(body_overlap(shot, turret) && body_overlap(turret, shot));
    CHECK(!body_overlap(shot, other) && !body_overlap(other, shot));
    // The shot flew up into the turret's bottom this frame.
    CHECK(body_hit_side(shot, turret) == BODY_SIDE_TOP);
    CHECK(body_hit_side(turret, shot) == BODY_SIDE_BOTTOM);
    CHECK(body_hit_side(shot, other) == 0);
    camera_set(0, 520); // out of reach again
    CHECK(!body_overlap(shot, turret) && body_hit_side(shot, turret) == 0);
    spr_flags[turret] = SPRITE_SCREEN; // both on the screen: as they are
    CHECK(body_overlap(shot, other) == false && body_overlap(shot, turret) == false);
    pos_y[turret] = FX(35);
    CHECK(body_overlap(shot, turret));
    camera_set(0, 0);
}

// body_max_fall is fixed point: a limit of 1.5 pixels per frame, on a body
// past the first sixteen slots (the pass skips sixteen bodies without limits
// at once).
static void max_fall_takes_fractions(void) {
    reset();
    physics_set_bounds(-100000, -100000, 100000, 100000);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = 0;
    for (int k = 0; k < 20; k++)
        i = make_body(FX(10), 0, 0, 0);
    CHECK(i >= 16);
    body_max_fall[i] = FX(3) / 2;
    step(30);
    CHECK(vel_y[i] == FX(3) / 2);
    CHECK(vel_y[0] == FX(30) / 4);
}

// physics.h: a kinematic body (C_KINEMATIC) moves only by its velocity.
// sys_movement() moves it; sys_physics() leaves it alone in each of its
// loops (the fast one; the general one, here with contacts and an open edge;
// with a scaled gravity; wrapping): no gravity, no bounds to bounce off or
// wrap around, no maximum fall, no contacts or exits, while a plain body
// beside it falls. body_overlap() and body_hit_side() test it as any body.
static void kinematic_bodies_move_only_by_velocity(void) {
    for (int loop = 0; loop < 4; loop++) {
        reset();
        physics_set_gravity(0, FX_ONE / 4);
        physics_set_contacts(loop == 1);
        physics_set_open_edges(loop == 1 ? PHYSICS_EDGE_RIGHT : 0);
        physics_set_wrap(loop == 3, false);
        Entity e = entity_create(C_POS | C_VEL | C_BODY | C_KINEMATIC);
        u32 k = entity_index(e);
        pos_x[k] = FX(50);
        pos_y[k] = FX(50);
        vel_x[k] = FX(3);
        vel_y[k] = FX(2);
        body_w[k] = body_h[k] = 8;
        body_bounce[k] = 224;
        body_friction[k] = 255;
        body_max_fall[k] = FX(1); // not applied
        u32 b = make_body(FX(10), FX(10), 0, 0);
        if (loop == 2)
            body_gravity[b] = BODY_GRAVITY(8); // sys_physics' scaled path
        u32 contacts = 0; // any frame's: an exit is reported for one frame only
        for (int f = 0; f < 20; f++) {
            step(1);
            contacts |= body_contact[k];
        }
        CHECK(pos_x[k] == FX(110) && pos_y[k] == FX(90)); // past the right bound: no bounce
        CHECK(vel_x[k] == FX(3) && vel_y[k] == FX(2));
        CHECK(contacts == 0);
        CHECK(vel_y[b] > 0 && pos_y[b] > FX(10)); // the plain body falls
        // A static collider just ahead: k ran into its left side this frame.
        Entity wall = entity_create(C_POS | C_BODY);
        u32 w = entity_index(wall);
        pos_x[w] = FX(117);
        pos_y[w] = FX(90);
        body_w[w] = body_h[w] = 8;
        CHECK(body_overlap(k, w));
        CHECK(body_hit_side(k, w) == BODY_SIDE_RIGHT);
    }
    reset();
}

TEST_SUITE(physics_tests, "physics", {"gravity_accelerates_bodies", gravity_accelerates_bodies},
           {"bounds_include_the_body_size", bounds_include_the_body_size},
           {"walls_bounce_perfectly_without_gravity", walls_bounce_perfectly_without_gravity},
           {"bodies_come_to_rest_on_the_floor", bodies_come_to_rest_on_the_floor},
           {"perfect_bounce_keeps_its_height", perfect_bounce_keeps_its_height},
           {"perfect_bounce_rests_when_too_slow", perfect_bounce_rests_when_too_slow},
           {"resting_bodies_stay_put_without_gravity", resting_bodies_stay_put_without_gravity},
           {"gravity_in_any_direction", gravity_in_any_direction},
           {"non_bodies_are_untouched", non_bodies_are_untouched},
           {"open_edges_let_bodies_out", open_edges_let_bodies_out},
           {"overlap_tests_rectangles", overlap_tests_rectangles},
           {"wrapping_brings_bodies_back_on_the_other_side",
            wrapping_brings_bodies_back_on_the_other_side},
           {"low_friction_stops_both_ways", low_friction_stops_both_ways},
           {"slow_slides_stop_without_friction", slow_slides_stop_without_friction},
           {"floor_bounces_are_symmetric", floor_bounces_are_symmetric},
           {"stationary_body_at_wrap_edge_stays_put", stationary_body_at_wrap_edge_stays_put},
           {"inverted_bounds_are_ignored", inverted_bounds_are_ignored},
           {"body_bigger_than_bounds_is_pinned", body_bigger_than_bounds_is_pinned},
           {"max_fall_limits_falling", max_fall_limits_falling},
           {"max_fall_limits_wrapping_bodies", max_fall_limits_wrapping_bodies},
           {"hit_side_from_each_side", hit_side_from_each_side},
           {"hit_side_needs_overlap", hit_side_needs_overlap},
           {"hit_side_of_fast_bodies", hit_side_of_fast_bodies},
           {"hit_side_uses_relative_motion", hit_side_uses_relative_motion},
           {"hit_side_at_corners", hit_side_at_corners},
           {"hit_side_with_static_colliders", hit_side_with_static_colliders},
           {"hit_side_inside_when_overlapping_before", hit_side_inside_when_overlapping_before},
           {"hit_side_paddle_moved_onto_ball", hit_side_paddle_moved_onto_ball},
           {"gravity_scale_per_body", gravity_scale_per_body},
           {"gravity_scale_rounds_symmetrically", gravity_scale_rounds_symmetrically},
           {"floors_follow_the_body_gravity", floors_follow_the_body_gravity},
           {"max_fall_with_gravity_scale", max_fall_with_gravity_scale},
           {"contacts_report_each_wall", contacts_report_each_wall},
           {"contacts_while_resting_and_off", contacts_while_resting_and_off},
           {"contacts_report_exits_through_open_edges", contacts_report_exits_through_open_edges},
           {"contacts_report_exits_on_the_exact_frame", contacts_report_exits_on_the_exact_frame},
           {"wrapping_axes_report_no_contacts", wrapping_axes_report_no_contacts},
           {"max_fall_takes_fractions", max_fall_takes_fractions},
           {"screen_and_world_bodies_meet_in_the_world", screen_and_world_bodies_meet_in_the_world},
           {"kinematic_bodies_move_only_by_velocity", kinematic_bodies_move_only_by_velocity});
