// button_repeat() timing and the secret sequence (button_secret_set) in
// src/core/input.c, fed button states directly as frame_begin() would.

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

// --- The secret sequence (button_secret_set) ---------------------------------

static const u16 secret_code[] = {BUTTON_UP,    BUTTON_UP,   BUTTON_DOWN,  BUTTON_DOWN, BUTTON_LEFT,
                                  BUTTON_RIGHT, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_B,    BUTTON_A};
#define SECRET_LENGTH 10u

static u32 secret_calls, other_calls;
static bool secret_saw_a; // button_repeat(BUTTON_A) when the hook last ran

static void on_secret(void) {
    secret_calls++;
    secret_saw_a = button_repeat(BUTTON_A);
}

static void on_other(void) {
    other_calls++;
}

static void secret_start(void) {
    serval_repeat_reset();
    secret_calls = other_calls = 0;
    secret_saw_a = false;
    button_secret_set(on_secret);
}

// One press, as a player makes it: a frame down, a frame up. `held` stays
// held throughout.
static void press_holding(u32 buttons, u32 held) {
    serval_repeat_frame(buttons | held);
    serval_repeat_frame(held);
}

static void press(u32 buttons) {
    press_holding(buttons, 0);
}

// Presses the sequence's buttons from `from` up to (not including) `to`.
static void press_code(u32 from, u32 to) {
    for (u32 i = from; i < to; i++)
        press(secret_code[i]);
}

// The sequence calls the hook once, on the frame the final A goes down, with
// that frame's input already counted; holding A on, or idle frames, add
// nothing.
static void secret_fires_once_on_the_code(void) {
    secret_start();
    press_code(0, SECRET_LENGTH - 1);
    CHECK(secret_calls == 0);
    serval_repeat_frame(BUTTON_A);
    CHECK(secret_calls == 1);
    CHECK(secret_saw_a); // the hook runs after this frame's buttons are read
    for (u32 f = 0; f < 30; f++)
        serval_repeat_frame(BUTTON_A); // held: no new press
    for (u32 f = 0; f < 30; f++)
        serval_repeat_frame(0);
    CHECK(secret_calls == 1);
    serval_repeat_reset();
}

// Nothing before the final A: not after any shorter part of the sequence,
// however long it waits, and not for a wrong last button.
static void secret_needs_the_final_a(void) {
    for (u32 n = 0; n < SECRET_LENGTH; n++) {
        secret_start();
        press_code(0, n);
        for (u32 f = 0; f < 200; f++)
            serval_repeat_frame(0);
        CHECK(secret_calls == 0);
    }
    secret_start();
    press_code(0, SECRET_LENGTH - 1);
    press(BUTTON_B); // B, B rather than B, A
    CHECK(secret_calls == 0);
    serval_repeat_reset();
}

// A button counts on the frame it goes down, not for every frame it is held:
// one long Up is one Up. Buttons held while others go down don't matter
// (here R throughout, and B as A goes down).
static void secret_counts_presses_not_holding(void) {
    secret_start();
    for (u32 f = 0; f < 20; f++)
        serval_repeat_frame(BUTTON_UP); // one Up, held
    serval_repeat_frame(0);
    press_code(2, SECRET_LENGTH);
    CHECK(secret_calls == 0);

    secret_start();
    serval_repeat_frame(BUTTON_R); // R down before the sequence...
    for (u32 i = 0; i < SECRET_LENGTH - 2; i++)
        press_holding(secret_code[i], BUTTON_R); // ...and held throughout
    serval_repeat_frame(BUTTON_R | BUTTON_B);    // B goes down...
    CHECK(secret_calls == 0);
    serval_repeat_frame(BUTTON_R | BUTTON_B | BUTTON_A); // ...and is still held as A does
    CHECK(secret_calls == 1);
    serval_repeat_reset();
}

// Any other button breaks the sequence, START, SELECT, L and R included, and
// it starts over: the rest of it alone does nothing, the whole of it works.
// Tried with every button at every place but the first; an Up is the next
// test's.
static void secret_wrong_press_starts_over(void) {
    for (u32 at = 1; at < SECRET_LENGTH; at++) {
        for (u32 bit = 0; bit < 10; bit++) {
            u32 wrong = 1u << bit;
            if (wrong == BUTTON_UP || wrong == secret_code[at])
                continue;
            secret_start();
            press_code(0, at);
            press(wrong);
            press_code(at, SECRET_LENGTH);
            CHECK(secret_calls == 0);
            press_code(0, SECRET_LENGTH);
            CHECK(secret_calls == 1);
        }
    }
    serval_repeat_reset();
}

// A break keeps what still matches the sequence's start: a third Up after
// Up, Up still leaves Up, Up, however many Ups come, and an Up in the wrong
// place is the start of a new try.
static void secret_extra_up_still_counts(void) {
    for (u32 ups = 3; ups <= 6; ups++) {
        secret_start();
        for (u32 i = 0; i < ups; i++)
            press(BUTTON_UP);
        press_code(2, SECRET_LENGTH);
        CHECK(secret_calls == 1);
    }
    secret_start();
    press_code(0, 5);             // up to Left
    press_code(0, SECRET_LENGTH); // an Up where Right should be starts it again
    CHECK(secret_calls == 1);
    serval_repeat_reset();
}

// Two buttons going down on the same frame are a wrong press, even with the
// one expected among them, and nothing of the sequence remains.
static void secret_simultaneous_presses_break_it(void) {
    secret_start();
    press_code(0, SECRET_LENGTH - 2);
    press(BUTTON_B | BUTTON_A); // B and A on one frame
    CHECK(secret_calls == 0);
    press(BUTTON_A);
    CHECK(secret_calls == 0);

    secret_start();
    press(BUTTON_UP | BUTTON_LEFT); // a sloppy diagonal at the start
    press_code(1, SECRET_LENGTH);   // so this lacks an Up
    CHECK(secret_calls == 0);

    secret_start();
    press_code(0, 4);
    press(BUTTON_LEFT | BUTTON_START); // the expected Left, and START
    press_code(5, SECRET_LENGTH);
    CHECK(secret_calls == 0);
    press_code(0, SECRET_LENGTH);
    CHECK(secret_calls == 1);
    serval_repeat_reset();
}

// Each time the sequence is entered, the hook runs again, also straight
// after the last time.
static void secret_fires_each_time(void) {
    secret_start();
    press_code(0, SECRET_LENGTH);
    press_code(0, SECRET_LENGTH);
    CHECK(secret_calls == 2);
    press(BUTTON_START);
    press_code(0, SECRET_LENGTH);
    CHECK(secret_calls == 3);
    serval_repeat_reset();
}

// NULL turns detection off, and forgets a part already entered; set again,
// the hook needs the whole sequence. serval_init() (serval_repeat_reset)
// turns it off too.
static void secret_null_turns_it_off(void) {
    secret_start();
    button_secret_set(NULL);
    press_code(0, SECRET_LENGTH);
    CHECK(secret_calls == 0);

    secret_start();
    press_code(0, 5);
    button_secret_set(NULL);
    button_secret_set(on_secret);
    press_code(5, SECRET_LENGTH);
    CHECK(secret_calls == 0);
    press_code(0, SECRET_LENGTH);
    CHECK(secret_calls == 1);

    secret_start();
    press_code(0, 5);
    serval_repeat_reset(); // as serval_init() does
    press_code(5, SECRET_LENGTH);
    press_code(0, SECRET_LENGTH);
    CHECK(secret_calls == 0);
    serval_repeat_reset();
}

// One hook at a time: a new one replaces the old one and starts the
// sequence over; setting the one already set changes nothing.
static void secret_replacing_the_hook(void) {
    secret_start();
    button_secret_set(on_other);
    press_code(0, SECRET_LENGTH);
    CHECK(other_calls == 1 && secret_calls == 0);

    secret_start();
    press_code(0, 5);
    button_secret_set(on_other); // another hook: starts over
    press_code(5, SECRET_LENGTH);
    CHECK(other_calls == 0 && secret_calls == 0);

    secret_start();
    press_code(0, 5);
    button_secret_set(on_secret); // the same one: carries on
    press_code(5, SECRET_LENGTH);
    CHECK(secret_calls == 1);
    serval_repeat_reset();
}

// button_repeat_reset(), as a screen opens mid-sequence (a pause menu), with
// a button held, doesn't affect it.
static void secret_ignores_button_repeat_reset(void) {
    secret_start();
    press_code(0, 5);
    serval_repeat_frame(BUTTON_RIGHT); // Right down: a screen opens
    button_repeat_reset();
    serval_repeat_frame(0);
    press_code(6, SECRET_LENGTH);
    CHECK(secret_calls == 1);
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
           {"reset_keeps_the_timing", reset_keeps_the_timing},
           {"secret_fires_once_on_the_code", secret_fires_once_on_the_code},
           {"secret_needs_the_final_a", secret_needs_the_final_a},
           {"secret_counts_presses_not_holding", secret_counts_presses_not_holding},
           {"secret_wrong_press_starts_over", secret_wrong_press_starts_over},
           {"secret_extra_up_still_counts", secret_extra_up_still_counts},
           {"secret_simultaneous_presses_break_it", secret_simultaneous_presses_break_it},
           {"secret_fires_each_time", secret_fires_each_time},
           {"secret_null_turns_it_off", secret_null_turns_it_off},
           {"secret_replacing_the_hook", secret_replacing_the_hook},
           {"secret_ignores_button_repeat_reset", secret_ignores_button_repeat_reset});
