// serval_splash()'s timing and buttons (src/core/splash_logic.c), fed presses
// directly as the splash's frame loop would: the 180-frame run, the jingle
// and skipping.

#include "../src/core/splash_internal.h"
#include "serval/core.h"
#include "test.h"

#define FADE SERVAL_SPLASH_FADE_FRAMES
#define HOLD SERVAL_SPLASH_HOLD_FRAMES
#define FULL_RUN (2 * FADE + HOLD) // 180 frames without input

static ServalSplashState state;
static ServalSplashFrame frame;

static void begin(void) {
    serval_splash_logic_begin(&state);
}

// Steps the frame about to run with `pressed` and returns its index.
static u32 step(u32 pressed) {
    u32 f = state.frame;
    serval_splash_logic_step(&state, pressed, &frame);
    return f;
}

// No scenario here runs past frame 180; a loop that gets to FRAME_CAP is a
// splash that never ends, which the checks then report instead of hanging.
#define FRAME_CAP 1000

// Steps frames with nothing pressed until frame `index` is the next one.
static void run_to(u32 index) {
    for (u32 n = 0; state.frame < index && n < FRAME_CAP; n++)
        step(0);
    CHECK(state.frame == index);
}

static void runs_180_frames_without_input(void) {
    begin();
    u32 last_darkness = 16;
    for (u32 f = 0; f < FULL_RUN; f++) {
        CHECK(step(0) == f);
        CHECK(frame.last == (f == FULL_RUN - 1));
        // The curve: from black to visible over the fade-in, visible through
        // the hold, back to black over the fade-out; never a step backwards.
        if (f == 0)
            CHECK(frame.darkness == 16);
        if (f == FADE - 1)
            CHECK(frame.darkness == 0);
        if (f >= FADE && f < FADE + HOLD)
            CHECK(frame.darkness == 0);
        if (f < FADE)
            CHECK(frame.darkness <= last_darkness);
        if (f >= FADE + HOLD)
            CHECK(frame.darkness >= last_darkness);
        if (f == FULL_RUN - 1)
            CHECK(frame.darkness == 16);
        CHECK(frame.darkness <= 16);
        last_darkness = frame.darkness;
    }
    // Stepping on reports the end again, in black.
    step(0);
    CHECK(frame.last);
    CHECK(frame.darkness == 16);
}

static void jingle_plays_at_its_time(void) {
    begin();
    for (u32 f = 0; f < FULL_RUN; f++) {
        step(0);
        CHECK(frame.jingle_first == (f == FADE + SERVAL_SPLASH_HOLD_BEFORE_JINGLE));
        CHECK(frame.jingle_second ==
              (f == FADE + SERVAL_SPLASH_HOLD_BEFORE_JINGLE + SERVAL_SPLASH_JINGLE_FIRST_NOTE));
    }
}

static void any_button_skips_after_the_fade_in_not_before(void) {
    begin();
    run_to(10);
    step(BUTTON_A); // during the fade-in: nothing
    CHECK(!frame.last);
    CHECK(frame.darkness == 16 - 11 * 16 / FADE);
    step(BUTTON_L | BUTTON_R); // (L and R are buttons like any other)
    CHECK(!frame.last);
    CHECK(frame.darkness == 16 - 12 * 16 / FADE);
    run_to(FADE - 1);
    step(BUTTON_START); // the fade-in's last frame: still nothing
    CHECK(!frame.last);
    CHECK(frame.darkness == 0);
    run_to(40);
    step(BUTTON_A); // the hold: this frame is the last
    CHECK(frame.last);
    CHECK(frame.darkness == 0);
    CHECK(!frame.jingle_first); // never reached
    step(0);
    CHECK(frame.last);

    // Every other button skips too, B through DOWN, L and R.
    static const u16 others[] = {BUTTON_B,  BUTTON_SELECT, BUTTON_START, BUTTON_RIGHT, BUTTON_LEFT,
                                 BUTTON_UP, BUTTON_DOWN,   BUTTON_L,     BUTTON_R};
    for (u32 i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        begin();
        run_to(FADE);
        step(others[i]);
        CHECK(frame.last);
    }

    // So does one during the fade-out; the frame is still drawn as it was.
    begin();
    run_to(FADE + HOLD + 15);
    step(BUTTON_A);
    CHECK(frame.last);
    CHECK(frame.darkness == 16 * 16 / FADE);
}

TEST_SUITE(splash_logic_tests, "splash_logic",
           {"runs_180_frames_without_input", runs_180_frames_without_input},
           {"jingle_plays_at_its_time", jingle_plays_at_its_time},
           {"any_button_skips_after_the_fade_in_not_before",
            any_button_skips_after_the_fade_in_not_before});
