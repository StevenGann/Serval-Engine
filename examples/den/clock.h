#ifndef DEN_CLOCK_H
#define DEN_CLOCK_H

// Serval Den's use of the cartridge clock (rtc.h; docs/rtc.md): the checks at
// boot, a read at most once a second, the hour of a time, the rebase and the
// 30-day rule, and (debug builds only) a time warp for playtesting.
//
// Rulings:
//  - The den's time is the clock's seconds as they are. A clock gone back, or
//    reset, is no time passed: clock_rebase() moves the pet's stored times by
//    the difference, so its age and neglect stay as they were. A reset clock
//    reads near 2000-01-01, earlier than a pet's birth can be moved back to
//    (times are unsigned): such a pet comes back younger, by what it can't
//    be moved. A real battery reset is rare and mGBA never reports one.
//  - Until the game asks the player about a jump past 30 days (the life
//    events' task), a jump is rebased: no time passes.

#include "pet.h"

typedef enum { CLOCK_OK, CLOCK_MISSING, CLOCK_WAS_RESET, CLOCK_WENT_BACK, CLOCK_JUMPED } ClockCheck;

#define CLOCK_JUMP_LIMIT (30u * 86400u) // seconds: further is a changed clock

// Reads the clock once at boot into *now and checks it against the time the
// den was last seen. CLOCK_JUMPED: now - last_seen > 30 days.
ClockCheck clock_boot(u32 last_seen, u32* now);

// The clock's seconds, read at most once a second (the reading is kept in
// between), plus the debug time warp.
u32 clock_now(void);

// The hour (0-23) of a time, through rtc_time_from_seconds().
int clock_hour(u32 seconds);

// No time passes: moves every time the pet keeps so that it was last stepped
// at `now` (either way).
void clock_rebase(Pet* p, u32 now);

// Debug builds: holding L + R and pressing SELECT adds an hour to
// clock_now(); returns true on that frame. Release builds: always false.
bool clock_debug_warp(void);

#endif // DEN_CLOCK_H
