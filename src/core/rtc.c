// The real-time clock (rtc.h): finding it, validating what it returns and
// converting times. Portable; the chip itself is a RtcDevice (rtc_internal.h).

#include "serval/rtc.h"

#include "rtc_internal.h"
#include "warn.h"

#define RTC_MAX_SECONDS 3155759999u

const RtcDevice* serval_rtc_device = &serval_platform_rtc_device;

static bool probed;
static bool present;
static bool was_reset;
static bool have_good;
static RtcTime last_good;
static bool have_last;
static u32 last_seconds;

static bool warned_invalid;
static bool warned_backwards;
static bool warned_fields;
static bool warned_limit;
static bool warned_null;

void serval_rtc_forget(void) {
    probed = present = was_reset = have_good = have_last = false;
    last_seconds = 0;
    last_good = (RtcTime){0};
    warned_invalid = warned_backwards = warned_fields = warned_limit = warned_null = false;
}

static const u8 month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
// Days before each month in a common year.
static const u16 month_start[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

static bool is_leap(u32 year) {
    return year % 4 == 0; // 2000 to 2099: 2000 is a leap year, 2100 is out of range
}

static u32 days_in_month(u32 year, u32 month) {
    return month_days[month - 1] + (month == 2 && is_leap(year) ? 1u : 0u);
}

static bool fields_valid(const RtcTime* t) {
    return t->year >= 2000 && t->year <= 2099 && t->month >= 1 && t->month <= 12 && t->day >= 1 &&
           t->day <= days_in_month(t->year, t->month) && t->hour <= 23 && t->minute <= 59 &&
           t->second <= 59;
}

static u32 to_seconds(const RtcTime* t) {
    u32 y = (u32)t->year - 2000;
    u32 days = y * 365 + (y + 3) / 4; // the leap days of the years before
    days += month_start[t->month - 1] + (t->month > 2 && is_leap(t->year) ? 1u : 0u);
    days += (u32)t->day - 1;
    return days * 86400u + t->hour * 3600u + t->minute * 60u + t->second;
}

u32 rtc_seconds_from_time(const RtcTime* t) {
    if (!t) {
        if (!warned_null) {
            warned_null = true;
            SERVAL_WARN("rtc_seconds_from_time: t is NULL; returning 0");
        }
        return 0;
    }
    if (!fields_valid(t)) {
        if (!warned_fields) {
            warned_fields = true;
            SERVAL_WARN("rtc_seconds_from_time: %d-%d-%d %d:%d:%d is not a date and time from "
                        "2000-01-01 to 2099-12-31; returning 0",
                        (int)t->year, (int)t->month, (int)t->day, (int)t->hour, (int)t->minute,
                        (int)t->second);
        }
        return 0;
    }
    return to_seconds(t);
}

void rtc_time_from_seconds(u32 s, RtcTime* out) {
    if (!out) {
        if (!warned_null) {
            warned_null = true;
            SERVAL_WARN("rtc_time_from_seconds: out is NULL; nothing written");
        }
        return;
    }
    if (s > RTC_MAX_SECONDS) {
        if (!warned_limit) {
            warned_limit = true;
            SERVAL_WARN("rtc_time_from_seconds: %u is past 2099-12-31 23:59:59 (3155759999); "
                        "using that",
                        (unsigned)s);
        }
        s = RTC_MAX_SECONDS;
    }
    u32 days = s / 86400u;
    u32 rest = s % 86400u;
    u32 year = 2000;
    for (;;) {
        u32 length = is_leap(year) ? 366u : 365u;
        if (days < length) {
            break;
        }
        days -= length;
        year++;
    }
    u32 month = 1;
    while (days >= days_in_month(year, month)) {
        days -= days_in_month(year, month);
        month++;
    }
    out->year = (u16)year;
    out->month = (u8)month;
    out->day = (u8)(days + 1);
    out->hour = (u8)(rest / 3600u);
    out->minute = (u8)(rest % 3600u / 60u);
    out->second = (u8)(rest % 60u);
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
    return fields_valid(t);
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
            return;
        }
    }
    RtcTime t;
    if (read_chip(&t)) {
        present = true;
        last_good = t;
        have_good = true;
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
        if (!warned_null) {
            warned_null = true;
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
        have_good = true;
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
    u32 s = to_seconds(&t);
    if (have_last && s < last_seconds && !warned_backwards) {
        warned_backwards = true;
        SERVAL_WARN("the cartridge clock went backwards (changed, or daylight saving time): "
                    "games should treat that as no time passing");
    }
    last_seconds = s;
    have_last = true;
    return s;
}
