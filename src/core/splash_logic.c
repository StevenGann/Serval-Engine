// serval_splash()'s timing and buttons (splash_internal.h). Portable, so the
// fade, hold, jingle and skip rules and the L/R style cycling are unit tested
// natively; src/gba/splash.c feeds it the frame's presses and draws what it
// says.

#include "serval/core.h"
#include "splash_internal.h"

#define FADE_FRAMES SERVAL_SPLASH_FADE_FRAMES
#define JINGLE_AT (SERVAL_SPLASH_FADE_FRAMES + SERVAL_SPLASH_HOLD_BEFORE_JINGLE)
#define STYLE_BUTTONS (BUTTON_L | BUTTON_R)

void serval_splash_logic_begin(ServalSplashState* state) {
    state->frame = 0;
    state->fade_out_at = FADE_FRAMES + SERVAL_SPLASH_HOLD_FRAMES;
    state->style = 0;
    state->over = false;
}

void serval_splash_logic_step(ServalSplashState* state, u32 pressed, ServalSplashFrame* frame) {
    u32 f = state->frame;
    frame->jingle_first = false;
    frame->jingle_second = false;
    if (state->over) {
        frame->darkness = 16;
        frame->style = state->style;
        frame->last = true;
        state->frame = f + 1;
        return;
    }
    bool fading_in = f < FADE_FRAMES;
    bool fading_out = f >= state->fade_out_at;

    // R and L: the next or previous style, and the hold starts over, so the
    // fade-out comes a full hold after the latest switch.
    if (!fading_out && (pressed & STYLE_BUTTONS)) {
        u32 delta = (pressed & BUTTON_R) ? 1 : SERVAL_SPLASH_STYLES - 1;
        state->style = (state->style + delta) % SERVAL_SPLASH_STYLES;
        if (state->fade_out_at < f + SERVAL_SPLASH_HOLD_FRAMES)
            state->fade_out_at = f + SERVAL_SPLASH_HOLD_FRAMES;
    }

    if (fading_in) {
        frame->darkness = 16 - (f + 1) * 16 / FADE_FRAMES;
    } else if (fading_out) {
        u32 step = f - state->fade_out_at + 1; // 1 to FADE_FRAMES
        frame->darkness = step * 16 / FADE_FRAMES;
        if (step == FADE_FRAMES)
            state->over = true;
    } else {
        frame->darkness = 0;
    }
    // The jingle keeps its place after the fade-in, whatever the switches.
    frame->jingle_first = f == JINGLE_AT;
    frame->jingle_second = f == JINGLE_AT + SERVAL_SPLASH_JINGLE_FIRST_NOTE;
    // Any other button, once faded in, skips the rest.
    if (!fading_in && (pressed & ~STYLE_BUTTONS))
        state->over = true;

    frame->style = state->style;
    frame->last = state->over;
    state->frame = f + 1;
}
