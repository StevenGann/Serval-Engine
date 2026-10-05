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

TEST_SUITE(input_tests, "input",
           {"fires_on_press_then_after_delay_and_interval",
            fires_on_press_then_after_delay_and_interval},
           {"nothing_without_buttons", nothing_without_buttons},
           {"release_restarts_the_delay", release_restarts_the_delay},
           {"buttons_count_separately", buttons_count_separately},
           {"interval_of_one_fires_every_frame", interval_of_one_fires_every_frame},
           {"bad_settings_are_ignored", bad_settings_are_ignored});
