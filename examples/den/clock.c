// Serval Den's clock: see clock.h.

#include "clock.h"

#include <serval/serval.h>

static u32 reading, warp;
static u32 read_frame;
static bool have_reading;

ClockCheck clock_boot(u32 last_seen, u32* now) {
    *now = 0;
    if (!rtc_present())
        return CLOCK_MISSING;
    reading = rtc_seconds();
    read_frame = frame_count();
    have_reading = true;
    *now = reading + warp;
    if (rtc_was_reset())
        return CLOCK_WAS_RESET;
    if (*now < last_seen)
        return CLOCK_WENT_BACK;
    if (*now - last_seen > CLOCK_JUMP_LIMIT)
        return CLOCK_JUMPED;
    return CLOCK_OK;
}

u32 clock_now(void) {
    // A read costs about 3,600 cycles with interrupts off: once a second.
    if (!have_reading || frame_count() - read_frame >= 60) {
        reading = rtc_seconds();
        read_frame = frame_count();
        have_reading = true;
    }
    return reading + warp;
}

int clock_hour(u32 seconds) {
    RtcTime t;
    rtc_time_from_seconds(seconds, &t);
    return t.hour;
}

// t moved by now - from, never below 0.
static u32 moved(u32 t, u32 from, u32 now) {
    if (now >= from)
        return t + (now - from);
    u32 d = from - now;
    return t > d ? t - d : 0;
}

void clock_rebase(Pet* p, u32 now) {
    u32 from = p->last_tick;
    p->born = moved(p->born, from, now);
    p->last_interaction = moved(p->last_interaction, from, now);
    if (p->awake_until)
        p->awake_until = moved(p->awake_until, from, now);
    if (p->died_at)
        p->died_at = moved(p->died_at, from, now);
    p->last_tick = now;
}

bool clock_debug_warp(void) {
#ifdef SERVAL_DEBUG
    if (button_down(BUTTON_L) && button_down(BUTTON_R) && button_pressed(BUTTON_SELECT)) {
        warp += 3600;
        return true;
    }
#endif
    return false;
}
