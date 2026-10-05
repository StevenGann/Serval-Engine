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
            wrapping_brings_bodies_back_on_the_other_side});
