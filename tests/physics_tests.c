#include "serval/debug.h"
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
    body_max_fall[i] = 3;
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
    body_max_fall[i] = 2;
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
    // Moving together: no relative motion, they overlapped before already;
    // falls back to the side of least overlap (a's right side, 2 pixels in).
    pos_x[b] = FX(56);
    pos_y[b] = FX(50);
    vel_x[a] = vel_x[b] = FX(5);
    vel_y[a] = vel_y[b] = FX(5);
    CHECK(body_hit_side(a, b) == BODY_SIDE_RIGHT);
    CHECK(body_hit_side(b, a) == BODY_SIDE_LEFT);
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

TEST_SUITE(physics_tests, "physics", {"gravity_accelerates_bodies", gravity_accelerates_bodies},
           {"bounds_include_the_body_size", bounds_include_the_body_size},
           {"walls_bounce_perfectly_without_gravity", walls_bounce_perfectly_without_gravity},
           {"bodies_come_to_rest_on_the_floor", bodies_come_to_rest_on_the_floor},
           {"resting_bodies_stay_put_without_gravity", resting_bodies_stay_put_without_gravity},
           {"gravity_in_any_direction", gravity_in_any_direction},
           {"non_bodies_are_untouched", non_bodies_are_untouched},
           {"open_edges_let_bodies_out", open_edges_let_bodies_out},
           {"overlap_tests_rectangles", overlap_tests_rectangles},
           {"wrapping_brings_bodies_back_on_the_other_side",
            wrapping_brings_bodies_back_on_the_other_side},
           {"low_friction_stops_both_ways", low_friction_stops_both_ways},
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
           {"hit_side_with_static_colliders", hit_side_with_static_colliders});
