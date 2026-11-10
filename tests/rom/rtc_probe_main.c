// The cartridge clock's driver against mGBA's clock: run by CTest
// rtc_probe_capture (tests/rom/rtc_probe_capture.py) in the capture tool,
// which gives this ROM's game code (SVRT) a clock with rtc=SECONDS. Logs the
// clock at frame 1 and at frame 180:
//
//   rtc: present=P reset=R YYYY-MM-DD hh:mm:ss seconds=S
//
// and, at frame 1, what an rtc_read() costs with the clock there and whether
// it left the GPIO port closed (control 0, so reads at 0x080000C8 are the
// ROM's zero again; mGBA writes the port's state into the ROM image when it
// is open):
//
//   rtc: read=N cycles control=C

#include "serval/core.h"
#include "serval/debug.h"
#include "serval/rtc.h"
#include "serval/text.h"

#include <tonc.h>

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

static void log_clock(void) {
    RtcTime t;
    bool present = rtc_present();
    (void)rtc_read(&t);
    u32 seconds = rtc_seconds();
    debug_log(text_format("rtc: present=%u reset=%u %04u-%02u-%02u %02u:%02u:%02u seconds=%u",
                          present ? 1u : 0u, rtc_was_reset() ? 1u : 0u, t.year, t.month, t.day,
                          t.hour, t.minute, t.second, seconds));
}

static void log_read_cost(void) {
    RtcTime t;
    u32 t0 = cycles();
    (void)rtc_read(&t);
    u32 cost = cycles() - t0;
    debug_log(text_format("rtc: read=%u cycles control=%u", cost, *(volatile u16*)0x080000C8));
}

int main(void) {
    serval_init();
    for (;;) {
        frame_begin();
        if (frame_count() == 1) {
            log_clock();
            log_read_cost();
        } else if (frame_count() == 180) {
            log_clock();
        }
        frame_end();
    }
}
