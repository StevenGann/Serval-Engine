#ifndef RACER_HUD_H
#define RACER_HUD_H

// What the race shows over the road: the clock, the lap and the speed at
// the top, messages in the middle, the countdown's big numbers, PAUSED,
// FINISH or TIME UP, and the SELECT readout at the bottom.

#include "track.h"

// A time in frames as m'ss"hh (minutes, seconds, hundredths), in one of
// text_format()'s rotating buffers.
const char* hud_time(u32 frames);

// Clears the screen's text and puts up the HUD's labels, for a new race.
void hud_reset(void);

// Shows a message in the middle of the screen for a while: one or two
// lines (`second` may be NULL).
void hud_message(const char* first, const char* second);

// Takes the message down (the race is over: FINISH or TIME UP comes down
// where it is).
void hud_message_clear(void);

// A frame of the race on: the message's time runs down. Not called while
// paused, so a paused message keeps its time.
void hud_step(void);

// The top rows from the race (values printed only when they change), the
// countdown's numbers and GO (`since_go`: frames since GO, counted only while
// racing), and PAUSED while `paused`.
void hud_draw(const Race* r, u32 since_go, bool paused);

// FINISH (or, `finished` false, TIME UP) in the big letters, centred across
// the screen, its top at y. hud_word_drop() is its y `frames` after it
// came up: dropped in from above the screen onto the sky, with a bounce.
void hud_word_draw(bool finished, int y);
int hud_word_drop(u32 frames);

// The SELECT readout: turns it on or off, and while on, shows the last
// frame's CPU load and sprite counts on the bottom rows (and has the engine
// count the per-line sprite time, which costs a little).
void hud_readout_toggle(void);
void hud_readout_off(void);
void hud_readout_draw(void);

#endif // RACER_HUD_H
