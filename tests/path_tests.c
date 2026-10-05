#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/math.h"
#include "serval/path.h"
#include "test.h"

#include <stddef.h>

static Entity make_mover(void) {
    ecs_reset();
    return entity_create(C_POS | C_VEL);
}

static void run(u32 frames) {
    for (u32 k = 0; k < frames; k++) {
        sys_path();
        sys_movement();
    }
}

static void straight_line(void) {
    static const PathStep steps[] = {{.frames = 10, .speed = FX(2)}};
    static const Path right = {PATH_STEPS(steps)};
    static const Path down = {PATH_STEPS(steps), .heading = ANGLE_DEG(90)};
    CHECK(right.step_count == 1 && right.steps == steps);

    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &right, 0);
    CHECK(ent_has(i, C_PATH) && path_active(e));
    run(5);
    CHECK(pos_x[i] == FX(10) && pos_y[i] == 0);
    CHECK(vel_x[i] == FX(2) && vel_y[i] == 0);
    CHECK(path_step[i] == 0 && path_time[i] == 5);

    path_start(e, &down, 0); // replaces the running path
    pos_x[i] = pos_y[i] = 0;
    run(4);
    CHECK(pos_x[i] == 0 && pos_y[i] == FX(8));
    CHECK(path_time[i] == 4);

    // The velocity follows path_heading and path_speed when the game changes
    // them, and a restart sets it even if they didn't change.
    path_heading[i] = ANGLE_DEG(180);
    run(1);
    CHECK(vel_x[i] == -FX(2) && vel_y[i] == 0);
    path_speed[i] = FX(3);
    run(1);
    CHECK(vel_x[i] == -FX(3));
    path_start(e, &down, 0);
    run(1);
    vel_x[i] = vel_y[i] = 0;
    path_start(e, &down, 0);
    run(1);
    CHECK(vel_y[i] == FX(2));
}

static void turns_add_up(void) {
    // A U-turn: 90 frames of 2 degrees after 10 frames straight down.
    static const PathStep steps[] = {
        {.frames = 10, .speed = FX(1)},
        {.frames = 90, .speed = FX(1), .turn = ANGLE_DEG(2)},
        {.frames = 10, .speed = FX(1)},
    };
    static const Path swoop = {PATH_STEPS(steps), .heading = ANGLE_DEG(90)};
    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &swoop, 0);
    run(10);
    CHECK(path_heading[i] == ANGLE_DEG(90) && pos_y[i] == FX(10));
    run(1); // the turn applies from the step's first frame
    CHECK(path_heading[i] == (u16)(ANGLE_DEG(90) + ANGLE_DEG(2)));
    run(89);
    CHECK(path_heading[i] == (u16)(ANGLE_DEG(90) + 90 * ANGLE_DEG(2)));
    CHECK(path_step[i] == 2 && path_time[i] == 0);
    // Clockwise from down is toward the left: the swoop ends left of its start,
    // flying up.
    CHECK(pos_x[i] < -FX(50));
    CHECK(vel_x[i] == fx_mul(fx_cos(path_heading[i]), FX(1)));
    CHECK(vel_y[i] < 0);
    // A negative turn, or ANGLE_DEG of a negative angle, turns the other way.
    static const PathStep left[] = {{.frames = 3, .turn = ANGLE_DEG(-10)},
                                    {.frames = 3, .turn = -ANGLE_DEG(10)}};
    static const Path wiggle = {PATH_STEPS(left)};
    path_start(e, &wiggle, 0);
    run(6);
    CHECK(path_heading[i] == (u16)(6 * -ANGLE_DEG(10)));
}

static void step_boundaries_and_acceleration(void) {
    static const PathStep steps[] = {
        {.frames = 3, .speed = FX(1)},
        {.frames = 4, .speed = FX(1), .accel = FX(1) / 4},
        {.frames = 2, .speed = -FX(1)},
    };
    static const Path path = {PATH_STEPS(steps)};
    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &path, 0);
    static const FIXED expected[] = {FX(1),     FX(1), FX(1),  FX(5) / 4, FX(3) / 2,
                                     FX(7) / 4, FX(2), -FX(1), -FX(1)};
    static const u8 step_after[] = {0, 0, 1, 1, 1, 1, 2, 2};
    for (u32 k = 0; k < sizeof(expected) / sizeof(expected[0]); k++) {
        sys_path();
        CHECK(vel_x[i] == expected[k]);
        if (k < sizeof(step_after))
            CHECK(path_step[i] == step_after[k] && ent_has(i, C_PATH));
    }
    // The last step is done: the path ended, with step 1's speed restarting at
    // its .speed and ending at speed + frames * accel.
    CHECK(!ent_has(i, C_PATH) && !path_active(e));
}

static void end_of_path_keeps_velocity(void) {
    static const PathStep steps[] = {{.frames = 4, .speed = FX(3), .turn = ANGLE_DEG(45) / 2}};
    static const Path path = {PATH_STEPS(steps)};
    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &path, 0);
    run(4);
    CHECK(!path_active(e) && ent_has(i, C_POS | C_VEL));
    CHECK(path_heading[i] == ANGLE_DEG(90));
    FIXED vx = vel_x[i], vy = vel_y[i];
    CHECK(vx == 0 && vy == FX(3));
    FIXED y = pos_y[i];
    run(5); // flies on straight without the path
    CHECK(vel_x[i] == vx && vel_y[i] == vy && pos_y[i] == y + 5 * FX(3));
    CHECK(path_heading[i] == ANGLE_DEG(90));
}

static void loops_return_to_the_loop_step(void) {
    static const PathStep steps[] = {
        {.frames = 2, .speed = FX(1)},
        {.frames = 1, .speed = FX(2)},
        {.frames = 1, .speed = FX(3)},
    };
    static const Path path = {PATH_STEPS(steps), .loop = true, .loop_step = 1};
    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &path, 0);
    static const FIXED expected[] = {FX(1), FX(1), FX(2), FX(3), FX(2), FX(3), FX(2), FX(3)};
    for (u32 k = 0; k < sizeof(expected) / sizeof(expected[0]); k++) {
        sys_path();
        CHECK(vel_x[i] == expected[k]);
    }
    run(1000);
    CHECK(path_active(e));

    // A loop to step 0 replays the whole path.
    static const Path whole = {PATH_STEPS(steps), .loop = true};
    path_start(e, &whole, 0);
    static const FIXED expected_whole[] = {FX(1), FX(1), FX(2), FX(3), FX(1), FX(1), FX(2)};
    for (u32 k = 0; k < sizeof(expected_whole) / sizeof(expected_whole[0]); k++) {
        sys_path();
        CHECK(vel_x[i] == expected_whole[k]);
    }
}

static void endless_step_circles(void) {
    // frames 0: the step never ends. 64 frames of 1/64 turn make a circle.
    static const PathStep steps[] = {{.speed = FX(2), .turn = 0x10000 / 64}};
    static const Path circle = {PATH_STEPS(steps)};
    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &circle, 0);
    run(64);
    CHECK(path_heading[i] == 0 && path_step[i] == 0 && path_time[i] == 0);
    CHECK(int_abs(pos_x[i]) <= 8 && int_abs(pos_y[i]) <= 8); // back at the start
    run(64 * 100);
    CHECK(path_active(e) && int_abs(pos_x[i]) <= 8 * 101 && int_abs(pos_y[i]) <= 8 * 101);
    // An endless step that accelerates is capped, not overflowing.
    static const PathStep faster[] = {{.speed = FX(100), .accel = FX(100)}};
    static const Path rocket = {PATH_STEPS(faster)};
    path_start(e, &rocket, 0);
    run(100);
    CHECK(path_speed[i] == FX(4096) && vel_x[i] == FX(4096));
    path_speed[i] = -FX(100000); // set too fast by the game: the velocity is capped
    run(1);
    CHECK(vel_x[i] == -FX(4096));
}

static void mirroring(void) {
    static const PathStep steps[] = {
        {.frames = 8, .speed = FX(2)},
        {.frames = 30, .speed = FX(2), .turn = ANGLE_DEG(3)},
        {.frames = 8, .speed = FX(1), .accel = FX(1) / 8},
    };
    static const Path path = {PATH_STEPS(steps), .heading = ANGLE_DEG(60)};
    static const u32 flags[] = {PATH_MIRROR_X, PATH_MIRROR_Y, PATH_MIRROR_X | PATH_MIRROR_Y};
    // 180 - 60, -60 and 60 + 180 degrees.
    static const u16 start[] = {(u16)(ANGLE_DEG(180) - ANGLE_DEG(60)), (u16)-ANGLE_DEG(60),
                                (u16)(ANGLE_DEG(60) + ANGLE_DEG(180))};
    ecs_reset();
    Entity plain = entity_create(C_POS | C_VEL);
    Entity mirrored[3];
    for (int m = 0; m < 3; m++)
        mirrored[m] = entity_create(C_POS | C_VEL);
    path_start(plain, &path, 0);
    for (int m = 0; m < 3; m++) {
        path_start(mirrored[m], &path, flags[m]);
        CHECK(path_heading[entity_index(mirrored[m])] == start[m]);
    }
    u32 p = entity_index(plain);
    for (int k = 0; k < 50; k++) {
        sys_path();
        sys_movement();
        for (int m = 0; m < 3; m++) {
            u32 i = entity_index(mirrored[m]);
            FIXED sx = (flags[m] & PATH_MIRROR_X) ? -1 : 1;
            FIXED sy = (flags[m] & PATH_MIRROR_Y) ? -1 : 1;
            CHECK(int_abs(pos_x[i] - sx * pos_x[p]) <= k / 8 + 1);
            CHECK(int_abs(pos_y[i] - sy * pos_y[p]) <= k / 8 + 1);
            CHECK(path_speed[i] == path_speed[p]);
        }
    }
    CHECK(!path_active(plain));
}

static void path_stop_keeps_velocity(void) {
    static const PathStep steps[] = {{.frames = 50, .speed = FX(1), .turn = ANGLE_DEG(1)}};
    static const Path path = {PATH_STEPS(steps)};
    Entity e = make_mover();
    u32 i = entity_index(e);
    path_start(e, &path, 0);
    run(10);
    FIXED vx = vel_x[i], vy = vel_y[i];
    path_stop(e);
    CHECK(!path_active(e));
    run(5);
    CHECK(vel_x[i] == vx && vel_y[i] == vy && path_time[i] == 10);
    // Aiming: path_heading after path_start rotates the whole path.
    path_start(e, &path, 0);
    path_heading[i] = ANGLE_DEG(180);
    run(1);
    CHECK(path_heading[i] == (u16)(ANGLE_DEG(180) + ANGLE_DEG(1)));
}

static void misuse_is_reported_and_safe(void) {
    static const PathStep steps[] = {{.frames = 5, .speed = FX(1)}};
    static const Path good = {PATH_STEPS(steps)};
    static const Path empty = {.steps = steps};
    static const Path bad_loop = {PATH_STEPS(steps), .loop = true, .loop_step = 1};
    Entity e = make_mover();
    u32 i = entity_index(e);
    u32 warnings = debug_warning_count();

    entity_destroy(e);
    path_start(e, &good, 0); // dead entity
    path_stop(e);
    CHECK(!path_active(e));
    e = entity_create(C_POS | C_VEL);
    i = entity_index(e);
    path_start(e, NULL, 0);
    path_start(e, &empty, 0);
    path_start(e, &bad_loop, 0);
    CHECK(!ent_has(i, C_PATH));
    u32 expected = 1; // once for every refused start

    Entity still = entity_create(C_POS);
    path_start(still, &good, 0); // starts, but warns: no C_VEL
    CHECK(path_active(still));
    expected++;

    // C_PATH added by hand: debug builds remove it (with a warning) rather
    // than resume an old path (release builds don't check).
    path_start(e, &good, 0);
    run(1);
    entity_destroy(e);
    Entity reused = ENTITY_NONE;
    for (u32 k = 0; k < MAX_ENT && entity_index(reused) != i; k++)
        reused = entity_create(C_POS | C_VEL | C_PATH);
    CHECK(entity_index(reused) == i);
    run(1);
#ifdef SERVAL_DEBUG
    CHECK(!ent_has(i, C_PATH) && vel_x[i] == 0);
    expected++;
#endif

    // A path_step past the end: the path stops.
    path_start(reused, &good, 0);
    path_step[i] = 7;
    run(1);
    CHECK(!ent_has(i, C_PATH));
    expected++;

#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + expected);
#else
    (void)expected;
    CHECK(debug_warning_count() == warnings);
#endif
}

TEST_SUITE(path_tests, "path", {"straight_line", straight_line}, {"turns_add_up", turns_add_up},
           {"step_boundaries_and_acceleration", step_boundaries_and_acceleration},
           {"end_of_path_keeps_velocity", end_of_path_keeps_velocity},
           {"loops_return_to_the_loop_step", loops_return_to_the_loop_step},
           {"endless_step_circles", endless_step_circles}, {"mirroring", mirroring},
           {"path_stop_keeps_velocity", path_stop_keeps_velocity},
           {"misuse_is_reported_and_safe", misuse_is_reported_and_safe});
