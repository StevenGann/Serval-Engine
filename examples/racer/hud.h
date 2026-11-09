#ifndef RACER_HUD_H
#define RACER_HUD_H

// What the race shows over the road: the clock, the lap and the speed at
// the top, messages in the middle, the countdown's big numbers, PAUSED, and
// the SELECT readout at the bottom.

#include "track.h"

// A time in frames as m'ss"hh (minutes, seconds, hundredths), in one of
// text_format()'s rotating buffers.
const char* hud_time(u32 frames);

// Clears the screen's text and puts up the HUD's labels, for a new race.
void hud_reset(void);

// Shows a message in the middle of the screen for a while: one or two
// lines (`second` may be NULL).
void hud_message(const char* first, const char* second);

// A message that stays until the next hud_reset() (FINISH, TIME UP).
void hud_message_hold(const char* first);

// A frame of the race on: the message's time runs down. Not called while
// paused, so a paused message keeps its time.
void hud_step(void);

// The top rows from the race (values printed only when they change), the
// countdown's numbers and GO (`since_go`: frames since GO, counted only while
// racing), and PAUSED while `paused`.
void hud_draw(const Race* r, u32 since_go, bool paused);

// The SELECT readout: turns it on or off, and while on, shows the last
// frame's CPU load and sprite counts on the bottom rows (and has the engine
// count the per-line sprite time, which costs a little).
void hud_readout_toggle(void);
void hud_readout_off(void);
void hud_readout_draw(void);

#endif // RACER_HUD_H
