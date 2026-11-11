// Serval Den's clock: see clock.h.

#include "clock.h"

#include <serval/serval.h>

static u32 reading, game_offset;
static u32 read_frame;
static bool have_reading;

static void read_clock(void) {
    reading = rtc_seconds();
    read_frame = frame_count();
    have_reading = true;
}

ClockCheck clock_boot(u32 last_seen, u32 age, u32 saved_offset, u32* now) {
    bool present = rtc_present();
    if (present)
        read_clock();
    return clock_resolve(present, present && rtc_was_reset(), reading, last_seen, age, saved_offset,
                         now, &game_offset);
}

u32 clock_start(void) {
    read_clock();
    game_offset = 0;
    return reading;
}

u32 clock_now(void) {
    // A read costs about 3,600 cycles with interrupts off: once a second.
    if (!have_reading || frame_count() - read_frame >= 60)
        read_clock();
    return reading + game_offset;
}

u32 clock_offset(void) {
    return game_offset;
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
        game_offset += 3600;
        return true;
    }
#endif
    return false;
}
