// button_repeat() timing (src/core/input.c), fed button states directly as
// frame_begin() would.

#include "../src/core/input_internal.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "test.h"

// Feeds `frames` frames of `buttons` held and returns the frames (counted
// from 1) on which button_repeat(watch) was true, as bits.
static u32 repeat_frames(u32 buttons, u16 watch, u32 frames) {
    u32 fired = 0;
    for (u32 f = 1; f <= frames; f++) {
        serval_repeat_frame(buttons);
        if (button_repeat(watch))
            fired |= 1u << f;
    }
    return fired;
}

static void fires_on_press_then_after_delay_and_interval(void) {
    serval_repeat_reset();
    // Default: the press (frame 1), then 20 frames later, then every 4.
    u32 fired = repeat_frames(BUTTON_UP, BUTTON_UP, 31);
    CHECK(fired == (1u << 1 | 1u << 21 | 1u << 25 | 1u << 29));
    serval_repeat_frame(0);
    CHECK(!button_repeat(BUTTON_UP));
    serval_repeat_reset();
}

static void nothing_without_buttons(void) {
    serval_repeat_reset();
    CHECK(repeat_frames(0, BUTTON_ANY, 30) == 0);
    CHECK(repeat_frames(BUTTON_A, BUTTON_B, 30) == 0); // another button
}

static void release_restarts_the_delay(void) {
    serval_repeat_reset();
    button_repeat_set(3, 2);
    CHECK(repeat_frames(BUTTON_A, BUTTON_A, 4) == (1u << 1 | 1u << 4));
    serval_repeat_frame(0);
    CHECK(repeat_frames(BUTTON_A, BUTTON_A, 4) == (1u << 1 | 1u << 4)); // a new press
    serval_repeat_reset();
}

static void buttons_count_separately(void) {
    serval_repeat_reset();
    button_repeat_set(3, 2);
    serval_repeat_frame(BUTTON_UP);               // UP pressed on frame 1
    serval_repeat_frame(BUTTON_UP | BUTTON_DOWN); // DOWN on frame 2
    CHECK(button_repeat(BUTTON_DOWN) && !button_repeat(BUTTON_UP));
    CHECK(button_repeat(BUTTON_UP | BUTTON_DOWN));
    serval_repeat_frame(BUTTON_UP | BUTTON_DOWN); // frame 3: neither due
    CHECK(!button_repeat(BUTTON_UP | BUTTON_DOWN));
    serval_repeat_frame(BUTTON_UP | BUTTON_DOWN); // frame 4: UP's first repeat
    CHECK(button_repeat(BUTTON_UP) && !button_repeat(BUTTON_DOWN));
    serval_repeat_frame(BUTTON_UP | BUTTON_DOWN); // frame 5: DOWN's
    CHECK(!button_repeat(BUTTON_UP) && button_repeat(BUTTON_DOWN));
    serval_repeat_reset();
}

static void interval_of_one_fires_every_frame(void) {
    serval_repeat_reset();
    button_repeat_set(1, 1);
    CHECK(repeat_frames(BUTTON_L, BUTTON_L, 5) == 0x3E); // frames 1-5
    serval_repeat_reset();
}

static void bad_settings_are_ignored(void) {
    serval_repeat_reset();
    u32 warnings = debug_warning_count();
    button_repeat_set(0, 4);
    button_repeat_set(10, 70000);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1); // reported once
#else
    CHECK(debug_warning_count() == warnings);
#endif
    CHECK(repeat_frames(BUTTON_R, BUTTON_R, 21) == (1u << 1 | 1u << 21)); // still the default
    serval_repeat_reset();
}

// button_repeat_reset(), as a screen opens: a button held at the call fires
// nothing (not even this frame's press) until it is released; pressed again,
// it works as usual.
static void reset_silences_held_buttons_until_released(void) {
    serval_repeat_reset();
    button_repeat_set(3, 2);
    serval_repeat_frame(BUTTON_A); // pressed: fires
    CHECK(button_repeat(BUTTON_A));
    button_repeat_reset(); // in the same frame
    CHECK(!button_repeat(BUTTON_A));
    // Held on: without the reset it would repeat on frames 3, 5, 7 and 9 of these.
    CHECK(repeat_frames(BUTTON_A, BUTTON_A, 10) == 0);
    serval_repeat_frame(0); // released
    // A new press works as usual.
    CHECK(repeat_frames(BUTTON_A, BUTTON_A, 6) == (1u << 1 | 1u << 4 | 1u << 6));
    serval_repeat_reset();
}

// Buttons not held at the call are unaffected, also while a silenced one is
// still held.
static void reset_leaves_buttons_pressed_later_alone(void) {
    serval_repeat_reset();
    button_repeat_set(3, 2);
    repeat_frames(BUTTON_UP, BUTTON_UP, 5); // UP held from the last screen
    button_repeat_reset();
    serval_repeat_frame(BUTTON_UP); // frame 6: UP's repeat would be due
    CHECK(!button_repeat(BUTTON_UP));
    // Frames 7-12: DOWN goes down and repeats as usual. Frames 13-16: UP,
    // whose repeats would be due on 14 and 16, is still silent.
    CHECK(repeat_frames(BUTTON_UP | BUTTON_DOWN, BUTTON_DOWN, 6) == (1u << 1 | 1u << 4 | 1u << 6));
    CHECK(repeat_frames(BUTTON_UP | BUTTON_DOWN, BUTTON_UP, 4) == 0);
    serval_repeat_reset();
}

// The reset keeps the delay and interval (button_repeat_set); serval_init()'s
// serval_repeat_reset() forgets everything, silenced buttons included.
static void reset_keeps_the_timing(void) {
    serval_repeat_reset();
    button_repeat_set(3, 2);
    serval_repeat_frame(BUTTON_A);
    button_repeat_reset();
    serval_repeat_frame(0);
    CHECK(repeat_frames(BUTTON_B, BUTTON_B, 6) == (1u << 1 | 1u << 4 | 1u << 6)); // not 20, 4
    serval_repeat_frame(BUTTON_A);
    button_repeat_reset();
    serval_repeat_reset(); // A still held: serval_init() forgets the silence
    CHECK(repeat_frames(BUTTON_A, BUTTON_A, 1) == 1u << 1);
    serval_repeat_reset();
}

TEST_SUITE(input_tests, "input",
           {"fires_on_press_then_after_delay_and_interval",
            fires_on_press_then_after_delay_and_interval},
           {"nothing_without_buttons", nothing_without_buttons},
           {"release_restarts_the_delay", release_restarts_the_delay},
           {"buttons_count_separately", buttons_count_separately},
           {"interval_of_one_fires_every_frame", interval_of_one_fires_every_frame},
           {"bad_settings_are_ignored", bad_settings_are_ignored},
           {"reset_silences_held_buttons_until_released",
            reset_silences_held_buttons_until_released},
           {"reset_leaves_buttons_pressed_later_alone", reset_leaves_buttons_pressed_later_alone},
           {"reset_keeps_the_timing", reset_keeps_the_timing});
