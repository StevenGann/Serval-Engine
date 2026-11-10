// The web page's clock, for src/core/rtc.c: the browser's local time, as a
// chip that is always there, in 24-hour mode, and never lost its power. The
// page (shell.html) supplies the seconds since 2000-01-01 00:00:00 (clamped to
// the clock's range; the browser's local time, or a fake one in test mode), or
// -1 for no clock (the page opened with rtc=none), which reads as an absent
// chip. Setting the clock and resetting it do nothing.

#include "../core/rtc_internal.h"

#include <emscripten.h>

#include <stddef.h>

// shell.html: seconds since 2000-01-01 00:00:00, or -1 for no clock.
EM_JS(double, web_rtc_seconds, (void), { return Module.servalRtcSeconds(); });

static u8 web_read_status(void) {
    return web_rtc_seconds() < 0 ? 0 : RTC_STATUS_24H;
}

static void web_write_status(u8 status) {
    (void)status;
}

static void web_reset(void) {}

static u8 to_bcd(u32 v) {
    return (u8)(((v / 10) << 4) | (v % 10));
}

static void web_read_datetime(u8 raw[7]) {
    double seconds = web_rtc_seconds();
    RtcTime t = {0};
    if (seconds >= 0) {
        rtc_time_from_seconds((u32)seconds, &t);
    }
    raw[0] = to_bcd((u32)(t.year >= 2000 ? t.year - 2000 : 0));
    raw[1] = to_bcd(t.month);
    raw[2] = to_bcd(t.day);
    raw[3] = 0; // the weekday: not kept
    raw[4] = to_bcd(t.hour);
    raw[5] = to_bcd(t.minute);
    raw[6] = to_bcd(t.second);
}

const RtcDevice serval_platform_rtc_device = {.read_status = web_read_status,
                                              .write_status = web_write_status,
                                              .reset = web_reset,
                                              .read_datetime = web_read_datetime,
                                              .id = NULL};
