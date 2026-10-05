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

TEST_SUITE(physics_tests, "physics", {"gravity_accelerates_bodies", gravity_accelerates_bodies},
           {"bounds_include_the_body_size", bounds_include_the_body_size},
           {"walls_bounce_perfectly_without_gravity", walls_bounce_perfectly_without_gravity},
           {"bodies_come_to_rest_on_the_floor", bodies_come_to_rest_on_the_floor},
           {"resting_bodies_stay_put_without_gravity", resting_bodies_stay_put_without_gravity},
           {"gravity_in_any_direction", gravity_in_any_direction},
           {"non_bodies_are_untouched", non_bodies_are_untouched});
