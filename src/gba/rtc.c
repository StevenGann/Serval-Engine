// The GBA's clock: for now none (Task 3 of the clock plan replaces this with
// the cartridge's GPIO driver).

#include "../core/rtc_internal.h"

#include <stddef.h>

static u8 absent_status(void) {
    return 0;
}
static void absent_write_status(u8 status) {
    (void)status;
}
static void absent_reset(void) {}
static void absent_datetime(u8 raw[7]) {
    for (int i = 0; i < 7; i++) {
        raw[i] = 0;
    }
}

const RtcDevice serval_platform_rtc_device = {.read_status = absent_status,
                                              .write_status = absent_write_status,
                                              .reset = absent_reset,
                                              .read_datetime = absent_datetime,
                                              .id = NULL};
