#ifndef DEN_CLOCK_H
#define DEN_CLOCK_H

// Serval Den's use of the cartridge clock (rtc.h; docs/rtc.md): the checks at
// boot, a read at most once a second, the hour of a time, the rebase and the
// 30-day rule, and (debug builds only) a time warp for playtesting.
//
// The den's time is the game clock: the cartridge clock's seconds plus an
// offset kept in the save (0 until the clock is reset).
//
// Rulings:
//  - A clock gone back (a daylight-saving step, a changed clock) is no time
//    passed: pet_rebase() moves the pet's stored times by the difference,
//    so its age and neglect stay as they were, and the offset is left alone,
//    so the hour of day stays the real one.
//  - A reset clock (rtc_was_reset(), or a reading earlier than 2000-01-01
//    plus the pet's age, which a rebase couldn't move the pet's birth back
//    to) moves the offset instead: the game clock resumes at the time the den
//    was last seen and the pet is unchanged. The hour of day then follows the
//    shifted time, not the real one; after a reset the real hour is lost
//    anyway (until the player sets the clock, which this game doesn't do).
//  - A jump past 30 days is the player's to judge (scenes.c asks, at boot
//    and, for a web page's clock, during play): YES lives it, NO rebases.

#include "pet.h"

typedef enum { CLOCK_OK, CLOCK_MISSING, CLOCK_WAS_RESET, CLOCK_WENT_BACK, CLOCK_JUMPED } ClockCheck;

#define CLOCK_JUMP_LIMIT (30u * 86400u) // seconds: further is a changed clock

// The checks, without the hardware (host-tested): `present`, `was_reset` and
// `raw` are what the clock says, `last_seen` the game time the den was saved
// at, `age` the pet's age then (seconds), `offset` the saved offset. Writes
// the game time now and the offset to keep. CLOCK_WAS_RESET: the offset moved
// so that now == last_seen; CLOCK_WENT_BACK: now < last_seen; CLOCK_JUMPED:
// now - last_seen > 30 days; CLOCK_MISSING: now and offset 0.
ClockCheck clock_resolve(bool present, bool was_reset, u32 raw, u32 last_seen, u32 age, u32 offset,
                         u32* now, u32* new_offset);

// Reads the clock once at boot and checks it (clock_resolve()) against the
// saved den; keeps the resulting offset for clock_now() and clock_offset().
ClockCheck clock_boot(u32 last_seen, u32 age, u32 offset, u32* now);

// A new den: reads the clock with no offset; returns the game time now.
u32 clock_start(void);

// The game time: the clock's seconds, read at most once a second (the
// reading is kept in between), plus the offset.
u32 clock_now(void);

// The offset to save beside the time last seen.
u32 clock_offset(void);

// The hour (0-23) of a time, through rtc_time_from_seconds().
int clock_hour(u32 seconds);

// Debug builds: holding L + R and pressing SELECT adds an hour to the offset
// (it is saved, so the warp lasts); returns true on that frame. Release
// builds: always false.
bool clock_debug_warp(void);

#endif // DEN_CLOCK_H
