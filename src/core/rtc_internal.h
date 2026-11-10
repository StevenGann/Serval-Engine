#ifndef SERVAL_CORE_RTC_INTERNAL_H
#define SERVAL_CORE_RTC_INTERNAL_H

// Engine-internal: the clock chip behind rtc.h. Not part of the public API.
//
// src/core/rtc.c finds the clock, validates what it reads and converts times;
// each platform supplies the chip: the GBA's GPIO port (src/gba/rtc.c), the
// browser's clock (src/web/rtc.c), none on the host (src/host/platform.c).
// Tests swap in their own (tests/rtc_tests.c).

#include "serval/platform.h"
#include "serval/rtc.h"

typedef struct RtcDevice {
    u8 (*read_status)(void); // the status byte; an absent chip reads 0
    void (*write_status)(u8 status);
    void (*reset)(void);
    void (*read_datetime)(u8 raw[7]); // year, month, day, weekday, hour, minute, second (BCD)
    const char* id; // "SIIRTC_V001" on the GBA, NULL elsewhere; referencing it keeps it linked
} RtcDevice;

#define RTC_STATUS_24H 0x40
#define RTC_STATUS_POWER 0x80

extern const RtcDevice serval_platform_rtc_device;
extern const RtcDevice* serval_rtc_device; // initially the platform's; tests point it at their own

// The conversions (src/core/rtc_convert.c, an object of its own so that a game
// that only converts links no device); rtc.c validates and reads with them.
bool serval_rtc_fields_valid(const RtcTime* t);
u32 serval_rtc_to_seconds(const RtcTime* t); // fields already valid
void serval_rtc_convert_forget(void);        // the conversions' warnings, for serval_rtc_forget

// Tests: forget the probe's result (and the warnings given), so the next call
// probes again.
void serval_rtc_forget(void);

#endif // SERVAL_CORE_RTC_INTERNAL_H
