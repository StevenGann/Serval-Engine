// The real-time clock (rtc.h): finding it, validating what it returns and
// converting times. Portable; the chip itself is a RtcDevice (rtc_internal.h).

#include "serval/rtc.h"

#include "rtc_internal.h"
#include "warn.h"

const RtcDevice* serval_rtc_device = &serval_platform_rtc_device;

SERVAL_EWRAM_BSS static bool probed;
SERVAL_EWRAM_BSS static bool present;
SERVAL_EWRAM_BSS static bool was_reset;
SERVAL_EWRAM_BSS static RtcTime last_good;
SERVAL_EWRAM_BSS static bool have_last;
SERVAL_EWRAM_BSS static u32 last_seconds;

SERVAL_EWRAM_BSS static bool warned_invalid;
SERVAL_EWRAM_BSS static bool warned_backwards;
SERVAL_EWRAM_BSS static bool warned_null_read;

void serval_rtc_forget(void) {
    probed = present = was_reset = have_last = false;
    last_seconds = 0;
    last_good = (RtcTime){0};
    warned_invalid = warned_backwards = warned_null_read = false;
    serval_rtc_convert_forget();
}

// One BCD byte after masking; false when a nibble is not a digit.
static bool bcd(u8 raw, u8 mask, u32* value) {
    u8 v = raw & mask;
    if ((v & 0x0F) > 9 || (v >> 4) > 9) {
        return false;
    }
    *value = (u32)(v >> 4) * 10 + (v & 0x0F);
    return true;
}

// Decodes and validates the chip's seven bytes (the weekday is ignored).
static bool decode(const u8 raw[7], RtcTime* t) {
    u32 year, month, day, hour, minute, second;
    if (!bcd(raw[0], 0xFF, &year) || !bcd(raw[1], 0x1F, &month) || !bcd(raw[2], 0x3F, &day) ||
        !bcd(raw[4], 0x3F, &hour) || !bcd(raw[5], 0x7F, &minute) || !bcd(raw[6], 0x7F, &second)) {
        return false;
    }
    t->year = (u16)(2000 + year);
    t->month = (u8)month;
    t->day = (u8)day;
    t->hour = (u8)hour;
    t->minute = (u8)minute;
    t->second = (u8)second;
    return serval_rtc_fields_valid(t);
}

static bool read_chip(RtcTime* t) {
    u8 raw[7];
    serval_rtc_device->read_datetime(raw);
    return decode(raw, t);
}

static void probe(void) {
    if (probed) {
        return;
    }
    probed = true;
    const RtcDevice* d = serval_rtc_device;
    u8 status = d->read_status();
    if (status & RTC_STATUS_POWER) {
        was_reset = true;
        d->reset();
        d->write_status(RTC_STATUS_24H);
        status = d->read_status();
    }
    if (!(status & RTC_STATUS_24H)) {
        d->write_status(RTC_STATUS_24H);
        status = d->read_status();
        if (!(status & RTC_STATUS_24H)) {
            was_reset = false; // no clock, so nothing was reset
            return;
        }
    }
    RtcTime t;
    if (read_chip(&t)) {
        present = true;
        last_good = t;
    } else {
        was_reset = false; // no clock, so nothing was reset
    }
}

bool rtc_present(void) {
    probe();
    return present;
}

bool rtc_was_reset(void) {
    probe();
    return was_reset;
}

bool rtc_read(RtcTime* out) {
    if (!out) {
        if (!warned_null_read) {
            warned_null_read = true;
            SERVAL_WARN("rtc_read: out is NULL; nothing read");
        }
        return false;
    }
    probe();
    if (!present) {
        *out = (RtcTime){0};
        return false;
    }
    RtcTime t;
    if (read_chip(&t) || read_chip(&t)) {
        last_good = t;
        *out = t;
        return true;
    }
    if (!warned_invalid) {
        warned_invalid = true;
        SERVAL_WARN("the cartridge clock returned an invalid time; using the last good one");
    }
    *out = last_good;
    return false;
}

u32 rtc_seconds(void) {
    RtcTime t;
    probe();
    if (!present) {
        return 0;
    }
    (void)rtc_read(&t); // t is the last good time if the read failed
    u32 s = serval_rtc_to_seconds(&t);
    if (have_last && s < last_seconds && !warned_backwards) {
        warned_backwards = true;
        SERVAL_WARN("the cartridge clock went backwards (changed, or daylight saving time): "
                    "games should treat that as no time passing");
    }
    last_seconds = s;
    have_last = true;
    return s;
}
