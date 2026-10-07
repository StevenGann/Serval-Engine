// serval_splash()'s timing and buttons (src/core/splash_logic.c), fed presses
// directly as the splash's frame loop would: the 180-frame run, skipping, and
// the L/R cycling through the candidate logo styles.

#include "../src/core/splash_internal.h"
#include "serval/core.h"
#include "test.h"

#define FADE SERVAL_SPLASH_FADE_FRAMES
#define HOLD SERVAL_SPLASH_HOLD_FRAMES
#define STYLES SERVAL_SPLASH_STYLES
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

// No scenario here runs past frame 350; a loop that gets to FRAME_CAP is a
// splash that never ends, which the checks then report instead of hanging.
#define FRAME_CAP 1000

// Steps frames with nothing pressed until frame `index` is the next one.
static void run_to(u32 index) {
    for (u32 n = 0; state.frame < index && n < FRAME_CAP; n++)
        step(0);
    CHECK(state.frame == index);
}

// Steps frames with nothing pressed until the splash reports its last frame;
// returns that frame's index (FRAME_CAP if it never ends, which no check
// expects).
static u32 run_to_end(void) {
    for (u32 n = 0; n < FRAME_CAP; n++) {
        u32 f = step(0);
        if (frame.last)
            return f;
    }
    CHECK(!"the splash never ended");
    return FRAME_CAP;
}

static void runs_180_frames_without_input(void) {
    begin();
    u32 last_darkness = 16;
    for (u32 f = 0; f < FULL_RUN; f++) {
        CHECK(step(0) == f);
        CHECK(frame.style == 0);
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

static void a_skips_after_the_fade_in_not_before(void) {
    begin();
    run_to(10);
    step(BUTTON_A); // during the fade-in: nothing
    CHECK(!frame.last);
    CHECK(frame.darkness == 16 - 11 * 16 / FADE);
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

    // Every other button skips too, B through DOWN.
    static const u16 others[] = {BUTTON_B,    BUTTON_SELECT, BUTTON_START, BUTTON_RIGHT,
                                 BUTTON_LEFT, BUTTON_UP,     BUTTON_DOWN};
    for (u32 i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        begin();
        run_to(FADE);
        step(others[i]);
        CHECK(frame.last);
    }
}

static void l_and_r_never_skip(void) {
    begin();
    run_to(40);
    step(BUTTON_R);
    CHECK(!frame.last);
    run_to(50);
    step(BUTTON_L);
    CHECK(!frame.last);
    run_to(100);
    step(BUTTON_L | BUTTON_R);
    CHECK(!frame.last);
    // The splash still ends by itself: a hold after the latest switch, then
    // the fade-out.
    CHECK(run_to_end() == 100 + HOLD + FADE - 1);
}

static void r_and_l_cycle_and_wrap(void) {
    begin();
    CHECK(step(0) == 0);
    CHECK(frame.style == 0);
    for (u32 n = 1; n <= STYLES; n++) {
        step(BUTTON_R); // the switch shows on the frame of the press
        CHECK(frame.style == n % STYLES);
    }
    CHECK(frame.style == 0); // wrapped around
    step(BUTTON_L);
    CHECK(frame.style == STYLES - 1);
    step(BUTTON_L);
    CHECK(frame.style == STYLES - 2);
    step(0);
    CHECK(frame.style == STYLES - 2); // and stays
    // Both at once: R wins.
    step(BUTTON_L | BUTTON_R);
    CHECK(frame.style == STYLES - 1);
}

static void a_switch_restarts_the_hold(void) {
    begin();
    run_to(100);
    step(BUTTON_R);
    // The fade-out now starts a full hold after the switch, at frame 220
    // instead of 150: visible until then, then 30 frames to black.
    for (u32 f = 101; f < 100 + HOLD; f++) {
        step(0);
        CHECK(frame.darkness == 0);
        CHECK(!frame.last);
    }
    CHECK(state.frame == 100 + HOLD);
    for (u32 n = 1; n <= FADE; n++) {
        step(0);
        CHECK(frame.darkness == n * 16 / FADE);
        CHECK(frame.last == (n == FADE));
    }
    CHECK(frame.darkness == 16);

    // A switch during the fade-in is early enough not to delay anything.
    begin();
    run_to(10);
    step(BUTTON_R);
    CHECK(run_to_end() == FULL_RUN - 1);

    // One on the hold's last frame pushes the fade-out by a full hold.
    begin();
    run_to(FADE + HOLD - 1);
    step(BUTTON_L);
    CHECK(frame.darkness == 0);
    CHECK(run_to_end() == FADE + HOLD - 1 + HOLD + FADE - 1);
}

static void jingle_plays_once_despite_switches(void) {
    begin();
    u32 firsts = 0, seconds = 0, first_at = 0, second_at = 0;
    for (u32 n = 0; n < FRAME_CAP; n++) {
        u32 f = state.frame;
        u32 pressed = (f == 20 || f == 45 || f == 61 || f == 100 || f == 200) ? BUTTON_R
                      : (f == 63 || f == 150)                                 ? BUTTON_L
                                                                              : 0;
        step(pressed);
        if (frame.jingle_first) {
            firsts++;
            first_at = f;
        }
        if (frame.jingle_second) {
            seconds++;
            second_at = f;
        }
        if (frame.last)
            break;
    }
    CHECK(frame.last); // and not the cap
    CHECK(firsts == 1);
    CHECK(seconds == 1);
    CHECK(first_at == FADE + SERVAL_SPLASH_HOLD_BEFORE_JINGLE);
    CHECK(second_at == first_at + SERVAL_SPLASH_JINGLE_FIRST_NOTE);
    CHECK(state.frame == 200 + HOLD + FADE); // the last switch set the end
}

static void fade_out_ignores_l_and_r_while_a_skips(void) {
    begin();
    run_to(FADE + HOLD + 10); // 10 frames into the fade-out
    step(BUTTON_R);
    CHECK(frame.style == 0);
    CHECK(!frame.last);
    CHECK(frame.darkness == 11 * 16 / FADE);
    step(BUTTON_L);
    CHECK(frame.style == 0);
    CHECK(!frame.last);
    CHECK(frame.darkness == 12 * 16 / FADE);
    // The end is where it was: the switches didn't restart the hold.
    CHECK(run_to_end() == FULL_RUN - 1);

    begin();
    run_to(FADE + HOLD + 15);
    step(BUTTON_A);
    CHECK(frame.last);
    CHECK(frame.darkness == 16 * 16 / FADE); // this frame is still drawn as it was
}

TEST_SUITE(splash_logic_tests, "splash_logic",
           {"runs_180_frames_without_input", runs_180_frames_without_input},
           {"jingle_plays_at_its_time", jingle_plays_at_its_time},
           {"a_skips_after_the_fade_in_not_before", a_skips_after_the_fade_in_not_before},
           {"l_and_r_never_skip", l_and_r_never_skip},
           {"r_and_l_cycle_and_wrap", r_and_l_cycle_and_wrap},
           {"a_switch_restarts_the_hold", a_switch_restarts_the_hold},
           {"jingle_plays_once_despite_switches", jingle_plays_once_despite_switches},
           {"fade_out_ignores_l_and_r_while_a_skips", fade_out_ignores_l_and_r_while_a_skips});
