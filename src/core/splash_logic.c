// serval_splash()'s timing and buttons (splash_internal.h). Portable, so the
// fade, hold, jingle and skip rules are unit tested natively; src/gba/splash.c
// feeds it the frame's presses and draws what it says.

#include "serval/core.h"
#include "splash_internal.h"

#define FADE_FRAMES SERVAL_SPLASH_FADE_FRAMES
#define FADE_OUT_AT (SERVAL_SPLASH_FADE_FRAMES + SERVAL_SPLASH_HOLD_FRAMES)
#define JINGLE_AT (SERVAL_SPLASH_FADE_FRAMES + SERVAL_SPLASH_HOLD_BEFORE_JINGLE)

void serval_splash_logic_begin(ServalSplashState* state) {
    state->frame = 0;
    state->over = false;
}

void serval_splash_logic_step(ServalSplashState* state, u32 pressed, ServalSplashFrame* frame) {
    u32 f = state->frame;
    frame->jingle_first = false;
    frame->jingle_second = false;
    if (state->over) {
        frame->darkness = 16;
        frame->last = true;
        state->frame = f + 1;
        return;
    }
    bool fading_in = f < FADE_FRAMES;

    if (fading_in) {
        frame->darkness = 16 - (f + 1) * 16 / FADE_FRAMES;
    } else if (f >= FADE_OUT_AT) {
        u32 step = f - FADE_OUT_AT + 1; // 1 to FADE_FRAMES
        frame->darkness = step * 16 / FADE_FRAMES;
        if (step == FADE_FRAMES)
            state->over = true;
    } else {
        frame->darkness = 0;
    }
    frame->jingle_first = f == JINGLE_AT;
    frame->jingle_second = f == JINGLE_AT + SERVAL_SPLASH_JINGLE_FIRST_NOTE;
    // Any button, once faded in, skips the rest.
    if (!fading_in && pressed)
        state->over = true;

    frame->last = state->over;
    state->frame = f + 1;
}
