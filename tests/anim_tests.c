// Tests for sys_animate (src/ecs/animate.c): frame timing, looping, playing
// once, and restarting.

#include "serval/ecs.h"
#include "serval/sprites.h"
#include "test.h"

#include "../src/core/sprite_internal.h"

static const u32 tiles[8 * 4];
static const u8 times[] = {2, 1, 3};
static const SpriteAsset timed = {
    .size = SPRITE_8x8, .tiles = tiles, .frame_count = 3, .frame_times = times};
static const SpriteAsset untimed = {.size = SPRITE_8x8, .tiles = tiles, .frame_count = 2};
static const SpriteAsset once = {.size = SPRITE_8x8,
                                 .tiles = tiles,
                                 .frame_count = 3,
                                 .frame_times = times,
                                 .flags = SPRITE_ASSET_ANIM_ONCE};
static const u8 hold_times[] = {1, 0};
static const SpriteAsset held = {
    .size = SPRITE_8x8, .tiles = tiles, .frame_count = 2, .frame_times = hold_times};
static const SpriteAsset* const table[] = {&timed, &untimed, &once, &held};

// Installs the test sprite table (sys_animate only reads it) and an animated
// entity showing sprite `id`; returns its slot.
static u32 setup(u16 id) {
    serval_sprite_table = table;
    serval_sprite_count = 4;
    ecs_reset();
    Entity e = entity_create(C_SPR | C_ANIM);
    u32 i = entity_index(e);
    spr_id[i] = id;
    return i;
}

// Runs sys_animate n times and returns the frame shown after each, plus 1,
// as decimal digits: 123 means frames 0, 1, 2.
static u32 frames_after(u32 i, u32 n) {
    u32 digits = 0;
    for (u32 k = 0; k < n; k++) {
        sys_animate();
        digits = digits * 10 + spr_frame[i] + 1;
    }
    return digits;
}

static void follows_frame_times_and_loops(void) {
    u32 i = setup(0);
    // Frame 0 shows for 2 frames, frame 1 for 1, frame 2 for 3, then frame 0
    // again. The entity starts on frame 0, which the first call counts.
    CHECK(frames_after(i, 8) == 12333112u);
}

static void without_frame_times_advances_every_frame(void) {
    u32 i = setup(1);
    CHECK(frames_after(i, 5) == 21212u);
}

static void anim_once_stops_on_last_frame(void) {
    u32 i = setup(2);
    CHECK(frames_after(i, 9) == 123333333u);
    CHECK(spr_anim_time[i] == 0);
}

static void zero_time_holds_the_frame(void) {
    u32 i = setup(3);
    CHECK(frames_after(i, 4) == 2222u);
}

static void restart_and_bad_frames(void) {
    u32 i = setup(0);
    frames_after(i, 4); // on frame 2, part way through
    spr_id[i] = 1;      // a different sprite, properly restarted
    spr_frame[i] = 0;
    spr_anim_time[i] = 0;
    CHECK(frames_after(i, 2) == 21u);
    spr_id[i] = 1;
    spr_frame[i] = 7; // a frame sprite 1 doesn't have: restarts at 0
    spr_anim_time[i] = 5;
    sys_animate();
    CHECK(spr_frame[i] == 0 && spr_anim_time[i] == 0);
}

static void only_animates_entities_with_c_anim(void) {
    u32 i = setup(1);
    ent_mask[i] &= ~C_ANIM;
    sys_animate();
    CHECK(spr_frame[i] == 0);
    spr_id[i] = 99; // not in the table: skipped (a warning in debug builds)
    ent_mask[i] |= C_ANIM;
    sys_animate();
    CHECK(spr_frame[i] == 0);
    CHECK(entity_create(C_ANIM) != ENTITY_NONE); // spr_anim_time is zeroed on create
    CHECK(spr_anim_time[i + 1] == 0);
}

TEST_SUITE(anim_tests, "anim", {"follows_frame_times_and_loops", follows_frame_times_and_loops},
           {"without_frame_times_advances_every_frame", without_frame_times_advances_every_frame},
           {"anim_once_stops_on_last_frame", anim_once_stops_on_last_frame},
           {"zero_time_holds_the_frame", zero_time_holds_the_frame},
           {"restart_and_bad_frames", restart_and_bad_frames},
           {"only_animates_entities_with_c_anim", only_animates_entities_with_c_anim});
