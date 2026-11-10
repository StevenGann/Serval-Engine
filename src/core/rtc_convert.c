// The real-time clock's conversions (rtc.h): dates and times to and from
// seconds since 2000-01-01. An object of its own, so that a game that only
// converts links no clock device (rtc.c has the probe and the device).

#include "serval/rtc.h"

#include "rtc_internal.h"
#include "warn.h"

#define RTC_MAX_SECONDS 3155759999u

SERVAL_EWRAM_BSS static bool warned_fields;
SERVAL_EWRAM_BSS static bool warned_limit;
SERVAL_EWRAM_BSS static bool warned_null_from;
SERVAL_EWRAM_BSS static bool warned_null_to;

void serval_rtc_convert_forget(void) {
    warned_fields = warned_limit = warned_null_from = warned_null_to = false;
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

bool serval_rtc_fields_valid(const RtcTime* t) {
    return t->year >= 2000 && t->year <= 2099 && t->month >= 1 && t->month <= 12 && t->day >= 1 &&
           t->day <= days_in_month(t->year, t->month) && t->hour <= 23 && t->minute <= 59 &&
           t->second <= 59;
}

u32 serval_rtc_to_seconds(const RtcTime* t) {
    u32 y = (u32)t->year - 2000;
    u32 days = y * 365 + (y + 3) / 4; // the leap days of the years before
    days += month_start[t->month - 1] + (t->month > 2 && is_leap(t->year) ? 1u : 0u);
    days += (u32)t->day - 1;
    return days * 86400u + t->hour * 3600u + t->minute * 60u + t->second;
}

u32 rtc_seconds_from_time(const RtcTime* t) {
    if (!t) {
        if (!warned_null_from) {
            warned_null_from = true;
            SERVAL_WARN("rtc_seconds_from_time: t is NULL; returning 0");
        }
        return 0;
    }
    if (!serval_rtc_fields_valid(t)) {
        if (!warned_fields) {
            warned_fields = true;
            SERVAL_WARN("rtc_seconds_from_time: %d-%d-%d %d:%d:%d is not a date and time from "
                        "2000-01-01 to 2099-12-31; returning 0",
                        (int)t->year, (int)t->month, (int)t->day, (int)t->hour, (int)t->minute,
                        (int)t->second);
        }
        return 0;
    }
    return serval_rtc_to_seconds(t);
}

void rtc_time_from_seconds(u32 s, RtcTime* out) {
    if (!out) {
        if (!warned_null_to) {
            warned_null_to = true;
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
