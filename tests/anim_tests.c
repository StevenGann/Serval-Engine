// Tests for sys_animate (src/ecs/animate.c): frame timing, looping, playing
// once, restarting, and frame_order sequences with their flips; and for
// anim_finished.

#include "serval/debug.h"
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
// frame_order sequences (frame_times are per step).
static const u8 order[] = {2, 0, 1, 0};
static const u8 order_times[] = {1, 2, 1, 3};
static const SpriteAsset sequence = {.size = SPRITE_8x8,
                                     .tiles = tiles,
                                     .frame_count = 3,
                                     .frame_times = order_times,
                                     .frame_order = order,
                                     .order_length = 4};
static const u8 once_order[] = {1, 0, 1};
static const SpriteAsset sequence_once = {.size = SPRITE_8x8,
                                          .tiles = tiles,
                                          .frame_count = 2,
                                          .flags = SPRITE_ASSET_ANIM_ONCE,
                                          .frame_order = once_order,
                                          .order_length = 3};
static const u8 held_order[] = {1, 0};
static const SpriteAsset sequence_held = {.size = SPRITE_8x8,
                                          .tiles = tiles,
                                          .frame_count = 2,
                                          .frame_times = hold_times,
                                          .frame_order = held_order,
                                          .order_length = 2};
static const u8 flip_order[] = {0, 0 | SPRITE_FRAME_FLIP_H, 1 | SPRITE_FRAME_FLIP_V,
                                0 | SPRITE_FRAME_FLIP_H | SPRITE_FRAME_FLIP_V};
static const SpriteAsset flipping = {.size = SPRITE_8x8,
                                     .tiles = tiles,
                                     .frame_count = 2,
                                     .frame_order = flip_order,
                                     .order_length = 4};
static const u8 bad_order[] = {1, 5};
static const SpriteAsset bad_frame_order = {.size = SPRITE_8x8,
                                            .tiles = tiles,
                                            .frame_count = 2,
                                            .frame_order = bad_order,
                                            .order_length = 2};
static const SpriteAsset no_length = {
    .size = SPRITE_8x8, .tiles = tiles, .frame_count = 2, .frame_order = bad_order};
static const SpriteAsset no_order = {
    .size = SPRITE_8x8, .tiles = tiles, .frame_count = 2, .order_length = 2};
// One frame (frame_count 0 means 1), played once.
static const SpriteAsset single_once = {
    .size = SPRITE_8x8, .tiles = tiles, .flags = SPRITE_ASSET_ANIM_ONCE};
enum {
    TIMED,
    UNTIMED,
    ONCE,
    HELD,
    SEQUENCE,
    SEQUENCE_ONCE,
    SEQUENCE_HELD,
    FLIPPING,
    BAD_FRAME_ORDER,
    NO_LENGTH,
    NO_ORDER,
    SINGLE_ONCE,
    TABLE_SIZE
};
static const SpriteAsset* const table[TABLE_SIZE] = {
    [TIMED] = &timed,
    [UNTIMED] = &untimed,
    [ONCE] = &once,
    [HELD] = &held,
    [SEQUENCE] = &sequence,
    [SEQUENCE_ONCE] = &sequence_once,
    [SEQUENCE_HELD] = &sequence_held,
    [FLIPPING] = &flipping,
    [BAD_FRAME_ORDER] = &bad_frame_order,
    [NO_LENGTH] = &no_length,
    [NO_ORDER] = &no_order,
    [SINGLE_ONCE] = &single_once,
};

// Installs the test sprite table (sys_animate only reads it) and an animated
// entity showing sprite `id`; returns its slot.
static u32 setup(u16 id) {
    serval_sprite_table = table;
    serval_sprite_count = TABLE_SIZE;
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

// Like frames_after, for spr_anim_step.
static u32 steps_after(u32 i, u32 n) {
    u32 digits = 0;
    for (u32 k = 0; k < n; k++) {
        sys_animate();
        digits = digits * 10 + spr_anim_step[i] + 1;
    }
    return digits;
}

static void follows_frame_times_and_loops(void) {
    u32 i = setup(TIMED);
    // Frame 0 shows for 2 frames, frame 1 for 1, frame 2 for 3, then frame 0
    // again. The entity starts on frame 0, which the first call counts.
    CHECK(frames_after(i, 8) == 12333112u);
}

static void without_frame_times_advances_every_frame(void) {
    u32 i = setup(UNTIMED);
    CHECK(frames_after(i, 5) == 21212u);
}

static void anim_once_stops_on_last_frame(void) {
    u32 i = setup(ONCE);
    CHECK(frames_after(i, 9) == 123333333u);
    CHECK(spr_anim_time[i] == 0);
}

static void zero_time_holds_the_frame(void) {
    u32 i = setup(HELD);
    CHECK(frames_after(i, 4) == 2222u);
}

static void restart_and_bad_frames(void) {
    u32 i = setup(TIMED);
    frames_after(i, 4);  // on frame 2, part way through
    spr_id[i] = UNTIMED; // a different sprite, properly restarted
    spr_frame[i] = 0;
    spr_anim_time[i] = 0;
    CHECK(frames_after(i, 2) == 21u);
    spr_id[i] = UNTIMED;
    spr_frame[i] = 7; // a frame sprite 1 doesn't have: restarts at 0
    spr_anim_time[i] = 5;
    sys_animate();
    CHECK(spr_frame[i] == 0 && spr_anim_time[i] == 0);
}

static void only_animates_entities_with_c_anim(void) {
    u32 i = setup(UNTIMED);
    ent_mask[i] &= ~C_ANIM;
    sys_animate();
    CHECK(spr_frame[i] == 0);
    spr_id[i] = 99; // not in the table: skipped (a warning in debug builds)
    ent_mask[i] |= C_ANIM;
    sys_animate();
    CHECK(spr_frame[i] == 0);
    CHECK(entity_create(C_ANIM) != ENTITY_NONE); // spr_anim_time is zeroed on create
    CHECK(spr_anim_time[i + 1] == 0 && spr_anim_step[i + 1] == 0);
}

static void sequence_steps_through_frame_order(void) {
    u32 i = setup(SEQUENCE);
    // Steps 0-3 show frames 2, 0, 1, 0 for 1, 2, 1 and 3 frames each.
    CHECK(steps_after(i, 8) == 22344412u);
    i = setup(SEQUENCE);
    CHECK(frames_after(i, 8) == 11211131u);
    CHECK(spr_anim_time[i] == 0);
    // A game's spr_anim_step = 0 shows step 0's frame on the next call.
    spr_anim_step[i] = 0;
    sys_animate();
    CHECK(spr_anim_step[i] == 1 && spr_frame[i] == 0);
    i = setup(SEQUENCE);
    spr_anim_step[i] = 2; // starting part way: step 2 (frame 1) shows for 1 frame
    CHECK(frames_after(i, 2) == 11u && spr_anim_step[i] == 3);
}

static void sequence_anim_once_stops_on_last_step(void) {
    u32 i = setup(SEQUENCE_ONCE);
    CHECK(steps_after(i, 5) == 23333u);
    CHECK(spr_frame[i] == 1 && spr_anim_time[i] == 0);
    i = setup(SEQUENCE_HELD); // a 0 frame_time holds its step
    CHECK(steps_after(i, 4) == 2222u);
    CHECK(spr_frame[i] == 0);
}

static void sequence_flips_combine_with_game_flips(void) {
    u32 i = setup(FLIPPING);
    const u32 flips = SPRITE_FLIP_H | SPRITE_FLIP_V;
    spr_flags[i] = SPRITE_FLIP_H | SPRITE_ABOVE_HUD; // the game's: facing left
    sys_animate();                                   // step 1: frame 0, flipped H
    CHECK(spr_frame[i] == 0 && (spr_flags[i] & flips) == 0);
    CHECK(spr_flags[i] == (SPRITE_ABOVE_HUD | SPRITE_ANIM_FLIP_H));
    sys_animate(); // step 2: frame 1, flipped V
    CHECK(spr_frame[i] == 1 && (spr_flags[i] & flips) == flips);
    spr_flags[i] |= SPRITE_HIDDEN; // other flags are left alone
    sys_animate();                 // step 3: flipped both ways
    CHECK((spr_flags[i] & flips) == SPRITE_FLIP_V && (spr_flags[i] & SPRITE_HIDDEN));
    sys_animate(); // step 0: not flipped
    CHECK(spr_flags[i] == (SPRITE_FLIP_H | SPRITE_ABOVE_HUD | SPRITE_HIDDEN));
    sys_animate();    // step 1
    spr_flags[i] = 0; // assigned whole: now facing right
    sys_animate();    // step 2
    CHECK((spr_flags[i] & flips) == SPRITE_FLIP_V);
    spr_flags[i] ^= SPRITE_FLIP_H; // toggled: facing left
    sys_animate();                 // step 3
    CHECK((spr_flags[i] & flips) == SPRITE_FLIP_V);
    sys_animate(); // step 0
    CHECK(spr_flags[i] == SPRITE_FLIP_H);
}

static void without_frame_order_leaves_step_and_flips(void) {
    u32 i = setup(UNTIMED);
    spr_anim_step[i] = 5;
    spr_flags[i] = SPRITE_FLIP_V | SPRITE_ANIM_FLIP_H;
    CHECK(frames_after(i, 3) == 212u);
    CHECK(spr_anim_step[i] == 5 && spr_flags[i] == (SPRITE_FLIP_V | SPRITE_ANIM_FLIP_H));
}

static void bad_sequences_warn_once(void) {
#ifdef SERVAL_DEBUG
    const u32 warnings = 4;
#else
    const u32 warnings = 0;
#endif
    u32 before = debug_warning_count();
    for (u32 k = 0; k < 2; k++) {
        u32 i = setup(SEQUENCE);
        spr_anim_step[i] = 4; // a step it doesn't have: restarts on step 0
        spr_anim_time[i] = 7;
        sys_animate();
        CHECK(spr_anim_step[i] == 0 && spr_anim_time[i] == 0 && spr_frame[i] == 2);
        i = setup(BAD_FRAME_ORDER); // step 1 is frame 5: shows frame 0
        CHECK(frames_after(i, 2) == 12u);
        i = setup(NO_LENGTH); // frame_order unused: frames play in order
        CHECK(frames_after(i, 2) == 21u);
        i = setup(NO_ORDER); // nothing to play: left alone
        spr_frame[i] = 1;
        sys_animate();
        CHECK(spr_frame[i] == 1 && spr_anim_step[i] == 0 && spr_anim_time[i] == 0);
    }
    CHECK(debug_warning_count() == before + warnings);
}

static void anim_finished_on_the_last_frame(void) {
    u32 i = setup(ONCE); // frames 0, 1, 2 for 2, 1 and 3 frames, then stays on 2
    Entity e = entity_at(i);
    CHECK(!anim_finished(e));
    u32 finished = 0;
    for (u32 k = 0; k < 6; k++) {
        sys_animate();
        finished = finished * 10 + anim_finished(e);
    }
    CHECK(finished == 1111u); // frames 0, 1, 2, 2, 2, 2
    CHECK(spr_frame[i] == 2);
    i = setup(SINGLE_ONCE); // frame_count 0: one frame, so finished at once
    CHECK(anim_finished(entity_at(i)));
}

static void anim_finished_on_the_last_step(void) {
    u32 i = setup(SEQUENCE_ONCE); // steps 0, 1, 2 show frames 1, 0, 1
    Entity e = entity_at(i);
    spr_frame[i] = 1; // the last frame, but step 0: not finished
    CHECK(!anim_finished(e));
    sys_animate(); // step 1
    CHECK(!anim_finished(e));
    sys_animate(); // step 2, the last
    CHECK(spr_anim_step[i] == 2 && anim_finished(e));
    sys_animate();
    CHECK(anim_finished(e));
    spr_anim_step[i] = 5; // a step it doesn't have (sys_animate restarts it)
    CHECK(!anim_finished(e));
}

static void anim_finished_never_for_looping_sprites(void) {
    u32 i = setup(TIMED);
    spr_frame[i] = 2; // the last frame of a looping sprite
    CHECK(!anim_finished(entity_at(i)));
    i = setup(SEQUENCE);
    spr_anim_step[i] = 3; // the last step of a looping sequence
    CHECK(!anim_finished(entity_at(i)));
}

static void anim_finished_needs_a_live_animated_entity(void) {
    u32 i = setup(ONCE);
    Entity e = entity_at(i);
    spr_frame[i] = 2;
    CHECK(anim_finished(e));
    ent_mask[i] &= ~C_ANIM;
    CHECK(!anim_finished(e));
    ent_mask[i] = (ent_mask[i] | C_ANIM) & ~C_SPR;
    CHECK(!anim_finished(e));
    ent_mask[i] |= C_SPR;
    spr_id[i] = 99; // not in the table
    CHECK(!anim_finished(e));
    spr_id[i] = ONCE;
    CHECK(anim_finished(e));
    entity_destroy(e);
    CHECK(!anim_finished(e));
    CHECK(!anim_finished(ENTITY_NONE));
}

TEST_SUITE(anim_tests, "anim", {"follows_frame_times_and_loops", follows_frame_times_and_loops},
           {"without_frame_times_advances_every_frame", without_frame_times_advances_every_frame},
           {"anim_once_stops_on_last_frame", anim_once_stops_on_last_frame},
           {"zero_time_holds_the_frame", zero_time_holds_the_frame},
           {"restart_and_bad_frames", restart_and_bad_frames},
           {"only_animates_entities_with_c_anim", only_animates_entities_with_c_anim},
           {"sequence_steps_through_frame_order", sequence_steps_through_frame_order},
           {"sequence_anim_once_stops_on_last_step", sequence_anim_once_stops_on_last_step},
           {"sequence_flips_combine_with_game_flips", sequence_flips_combine_with_game_flips},
           {"without_frame_order_leaves_step_and_flips", without_frame_order_leaves_step_and_flips},
           {"bad_sequences_warn_once", bad_sequences_warn_once},
           {"anim_finished_on_the_last_frame", anim_finished_on_the_last_frame},
           {"anim_finished_on_the_last_step", anim_finished_on_the_last_step},
           {"anim_finished_never_for_looping_sprites", anim_finished_never_for_looping_sprites},
           {"anim_finished_needs_a_live_animated_entity",
            anim_finished_needs_a_live_animated_entity});
