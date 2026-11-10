#ifndef SERVAL_RTC_H
#define SERVAL_RTC_H

// The cartridge's real-time clock, for games that want to know how much time
// has passed between two runs (a creature that gets hungry, a crop that grows).
// See docs/rtc.md.
//
// GBA: the clock chip some cartridges carry (Seiko S-3511A, found by the ID
// string "SIIRTC_V001" that linking this module puts in the ROM). Web: the
// browser's clock. Elsewhere, and on a cartridge or in an emulator without a
// clock, there is none: rtc_present() is false and the other calls return 0.
//
// The clock tells the player's local time as the chip keeps it. Use it for
// elapsed time (rtc_seconds() now minus rtc_seconds() at the last save), not
// as proof of the date: the player can set the clock, and daylight saving time
// moves it. Games must accept a clock that went backwards as no time passing.
// There is no way to set the clock, and no weekday or time zone.

#include "serval/platform.h"

typedef struct RtcTime {
    u16 year; // 2000 to 2099
    u8 month; // 1 to 12
    u8 day;   // 1 to 31
    u8 hour;  // 0 to 23
    u8 minute;
    u8 second;
} RtcTime;

// Whether a working clock was found. The first call (of any rtc_ function but
// the two conversions) looks for it; the result is kept.
bool rtc_present(void);

// Reads the date and time into *out. Without a clock, zeroes *out and returns
// false. If the chip returns nonsense twice, *out is the last good time and the
// result is false (with a warning in debug builds).
bool rtc_read(RtcTime* out);

// The time as seconds since 2000-01-01 00:00:00, 0 to 3155759999 (2099-12-31
// 23:59:59); 0 without a clock. If the chip returns nonsense, the last good
// time's seconds. If it is lower than the previous call's, the
// clock was changed or went back an hour for daylight saving time.
u32 rtc_seconds(void);

// True when the clock had lost its power before this run (a dead battery or a
// new cartridge): its time was reset and is not the real date. Always false
// without a clock.
bool rtc_was_reset(void);

// Conversions between a date and time and seconds since 2000-01-01 00:00:00.
// A field out of range gives 0 (with a warning); seconds above 3155759999 give
// 2099-12-31 23:59:59.
u32 rtc_seconds_from_time(const RtcTime* t);
void rtc_time_from_seconds(u32 s, RtcTime* out);

#endif // SERVAL_RTC_H
