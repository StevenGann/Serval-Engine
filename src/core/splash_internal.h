#ifndef SERVAL_CORE_SPLASH_INTERNAL_H
#define SERVAL_CORE_SPLASH_INTERNAL_H

// Engine-internal: the timing and button logic of serval_splash(), kept apart
// from the hardware (src/gba/splash.c) so it can be tested on the host, where
// the test ROM can't press buttons. Not part of the public API.
//
// The splash is a fade-in, a hold with the jingle, and a fade-out; its logo
// is one of SERVAL_SPLASH_STYLES variations of the logo, which L and R cycle
// while the owner picks the final one (docs/open-questions.md). Frames at ~59.73 fps.

#include "serval/platform.h"

#define SERVAL_SPLASH_FADE_FRAMES 30        // 500 ms in, and again out
#define SERVAL_SPLASH_HOLD_BEFORE_JINGLE 30 // 500 ms
#define SERVAL_SPLASH_HOLD_AFTER_JINGLE 90  // 1500 ms
#define SERVAL_SPLASH_JINGLE_FIRST_NOTE 4   // frames of the short first note
#define SERVAL_SPLASH_STYLES 4

// The hold: the frames between the fade-in and the fade-out when nothing is
// pressed, and again after the latest style switch.
#define SERVAL_SPLASH_HOLD_FRAMES                                                                  \
    (SERVAL_SPLASH_HOLD_BEFORE_JINGLE + SERVAL_SPLASH_HOLD_AFTER_JINGLE)

typedef struct {
    u32 frame;       // frames stepped so far: the index of the next one
    u32 fade_out_at; // the frame the fade-out starts on (pushed back by switches)
    u32 style;       // the style shown, 0 to SERVAL_SPLASH_STYLES - 1
    bool over;       // the last frame has been stepped
} ServalSplashState;

// What the renderer does for one frame.
typedef struct {
    u32 darkness;       // BLDY level for the frame: 0 fully visible, 16 black
    u32 style;          // the logo style to show
    bool jingle_first;  // start the jingle's first note before this frame
    bool jingle_second; // start its second note before this frame
    bool last;          // this is the splash's last frame: stop after it
} ServalSplashFrame;

// Starts the splash: the fade-in from black, style 0.
void serval_splash_logic_begin(ServalSplashState* state);

// Steps one frame. `pressed` is the buttons that went down this frame
// (BUTTON_* bits, as button_pressed() reports them). Fills in `frame`:
//
//   - 30 frames of fade-in, the hold, then 30 frames of fade-out, with the
//     jingle's notes at frames 60 and 64 (never replayed, whatever is pressed);
//   - any button but L and R pressed after the fade-in ends the splash with
//     this frame (`last`); during the fade-in it does nothing;
//   - R and L, from the first frame until the fade-out begins, show the next
//     and previous style (wrapping) and restart the hold: the fade-out then
//     starts SERVAL_SPLASH_HOLD_FRAMES frames after the switch. During the
//     fade-out they do nothing. L and R never skip.
//
// After the last frame every further step reports `last` again, in black.
void serval_splash_logic_step(ServalSplashState* state, u32 pressed, ServalSplashFrame* frame);

#endif // SERVAL_CORE_SPLASH_INTERNAL_H
