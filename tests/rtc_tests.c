// The real-time clock (rtc.h): the probe, the validation and the conversions,
// against a fake chip that behaves like the real one (a status byte whose
// bits 0-6 a write sets and whose power bit clears when read, seven BCD
// bytes). Runs natively and in the test ROM; the GBA driver's own tests, on
// mGBA's emulated chip, are tests/rom/rtc_tests.c.

#include "serval/debug.h"
#include "serval/rtc.h"
#include "test.h"

#include "../src/core/rtc_internal.h"

#include <string.h>

typedef struct {
    u8 status;
    u8 raw[7];
    bool open_bus; // reads 0xFF, ignores writes
    u32 status_reads;
    u32 status_writes;
    u32 last_status_write;
    u32 resets;
    u32 datetime_reads;
} FakeChip;

static FakeChip fake;

static u8 fake_read_status(void) {
    fake.status_reads++;
    if (fake.open_bus) {
        return 0xFF;
    }
    u8 s = fake.status;
    fake.status &= (u8)~RTC_STATUS_POWER; // the power flag clears on read
    return s;
}

static void fake_write_status(u8 status) {
    fake.status_writes++;
    fake.last_status_write = status;
    if (!fake.open_bus) {
        fake.status = (u8)((fake.status & 0x80) | (status & 0x7F));
    }
}

static void fake_reset(void) {
    fake.resets++;
    if (!fake.open_bus) {
        fake.status &= 0x80;
    }
}

static void fake_read_datetime(u8 raw[7]) {
    fake.datetime_reads++;
    memcpy(raw, fake.raw, 7);
}

static const RtcDevice fake_device = {.read_status = fake_read_status,
                                      .write_status = fake_write_status,
                                      .reset = fake_reset,
                                      .read_datetime = fake_read_datetime,
                                      .id = NULL};

static const RtcDevice* saved_device;

static void begin(u8 status, const u8 raw[7]) {
    saved_device = serval_rtc_device;
    memset(&fake, 0, sizeof fake);
    fake.status = status;
    if (raw) {
        memcpy(fake.raw, raw, 7);
    }
    serval_rtc_device = &fake_device;
    serval_rtc_forget();
}

static void end(void) {
    serval_rtc_device = saved_device;
    serval_rtc_forget();
}

// 2024-02-29 12:34:56
static const u8 leap_day[7] = {0x24, 0x02, 0x29, 0x04, 0x12, 0x34, 0x56};

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

// The number of warnings given since `before`.
static u32 warned_since(u32 before) {
    return debug_warning_count() - before;
}

static void check_time(RtcTime t, u32 seconds) {
    RtcTime back;
    CHECK(rtc_seconds_from_time(&t) == seconds);
    rtc_time_from_seconds(seconds, &back);
    CHECK(back.year == t.year && back.month == t.month && back.day == t.day &&
          back.hour == t.hour && back.minute == t.minute && back.second == t.second);
}

static void rtc_conversions_round_trip(void) {
    begin(0, NULL);
    check_time((RtcTime){2024, 2, 29, 12, 34, 56}, 762525296u);
    check_time((RtcTime){2000, 3, 1, 0, 0, 0}, 5184000u);
    check_time((RtcTime){2001, 1, 1, 0, 0, 0}, 31622400u);
    check_time((RtcTime){2000, 1, 1, 0, 0, 0}, 0u);
    check_time((RtcTime){2099, 12, 31, 23, 59, 59}, 3155759999u);

    // Every day: the offset time of day moves with it.
    RtcTime t = {2000, 1, 1, 0, 0, 0};
    u32 bad = 0;
    for (u32 day = 0; day < 36525; day++) {
        u32 seconds = day * 86400u + (day * 3601u) % 86400u;
        RtcTime r;
        rtc_time_from_seconds(seconds, &r);
        if (rtc_seconds_from_time(&r) != seconds) {
            bad++;
        }
        // Dates advance one day at a time.
        RtcTime d;
        rtc_time_from_seconds(day * 86400u, &d);
        if (d.year != t.year || d.month != t.month || d.day != t.day) {
            bad++;
        }
        u32 length = (t.month == 2) ? ((t.year % 4 == 0) ? 29u : 28u)
                     : (t.month == 4 || t.month == 6 || t.month == 9 || t.month == 11) ? 30u
                                                                                       : 31u;
        if (++t.day > length) {
            t.day = 1;
            if (++t.month > 12) {
                t.month = 1;
                t.year++;
            }
        }
    }
    CHECK(bad == 0);
    CHECK(t.year == 2100 && t.month == 1 && t.day == 1);
    end();
}

static void rtc_conversion_limits(void) {
    begin(0, NULL);
    RtcTime bad[] = {
        {2024, 2, 30, 0, 0, 0}, {2023, 2, 29, 0, 0, 0}, {1999, 1, 1, 0, 0, 0},
        {2100, 1, 1, 0, 0, 0},  {2024, 0, 1, 0, 0, 0},  {2024, 13, 1, 0, 0, 0},
        {2024, 1, 0, 0, 0, 0},  {2024, 1, 1, 24, 0, 0}, {2024, 1, 1, 0, 60, 0},
        {2024, 1, 1, 0, 0, 60},
    };
    for (u32 i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        serval_rtc_forget(); // the warning is given once; this call site's own
        u32 before = debug_warning_count();
        CHECK(rtc_seconds_from_time(&bad[i]) == 0);
        CHECK(warned_since(before) == WARNINGS(1));
        CHECK(rtc_seconds_from_time(&bad[i]) == 0);
        CHECK(warned_since(before) == WARNINGS(1)); // once
    }

    u32 before = debug_warning_count();
    RtcTime t;
    rtc_time_from_seconds(3155760000u, &t);
    CHECK(t.year == 2099 && t.month == 12 && t.day == 31 && t.hour == 23 && t.minute == 59 &&
          t.second == 59);
    CHECK(warned_since(before) == WARNINGS(1));
    rtc_time_from_seconds(4000000000u, &t);
    CHECK(t.year == 2099 && warned_since(before) == WARNINGS(1));

    before = debug_warning_count();
    CHECK(rtc_seconds_from_time(NULL) == 0);
    rtc_time_from_seconds(0, NULL);
    CHECK(warned_since(before) == WARNINGS(2)); // one per function
    end();
}

static void rtc_absent_host(void) {
    saved_device = serval_rtc_device;
    serval_rtc_device = &serval_platform_rtc_device;
    serval_rtc_forget();
#if !defined(SERVAL_GBA) && !defined(__EMSCRIPTEN__)
    RtcTime t = {1, 2, 3, 4, 5, 6};
    CHECK(!rtc_present());
    CHECK(rtc_seconds() == 0);
    CHECK(!rtc_read(&t));
    CHECK(t.year == 0 && t.month == 0 && t.day == 0 && t.hour == 0 && t.minute == 0 &&
          t.second == 0);
    CHECK(!rtc_was_reset());
#endif
    end();
}

static void rtc_present_reads_time(void) {
    begin(RTC_STATUS_24H, leap_day);
    u32 before = debug_warning_count();
    RtcTime t;
    CHECK(rtc_present());
    CHECK(rtc_read(&t));
    CHECK(t.year == 2024 && t.month == 2 && t.day == 29 && t.hour == 12 && t.minute == 34 &&
          t.second == 56);
    CHECK(rtc_seconds() == 762525296u);
    CHECK(!rtc_was_reset());
    CHECK(fake.resets == 0);
    CHECK(warned_since(before) == WARNINGS(0));
    end();
}

static void rtc_pm_flag_masked(void) {
    u8 raw[7] = {0x24, 0x02, 0x29, 0x04, 0x92, 0x34, 0x56};
    begin(RTC_STATUS_24H, raw);
    RtcTime t;
    CHECK(rtc_read(&t) && t.hour == 12);
    end();
    raw[4] = 0x95;
    begin(RTC_STATUS_24H, raw);
    CHECK(rtc_read(&t) && t.hour == 15);
    end();
}

static void rtc_power_lost_resets_and_sets_24h(void) {
    begin(RTC_STATUS_POWER | RTC_STATUS_24H, leap_day);
    CHECK(rtc_present());
    CHECK(fake.resets == 1);
    CHECK(fake.status_writes == 1 && fake.last_status_write == RTC_STATUS_24H);
    CHECK(rtc_was_reset());
    end();
}

static void rtc_12h_mode_is_switched(void) {
    begin(0x00, leap_day);
    CHECK(rtc_present());
    CHECK(fake.status_writes == 1 && fake.last_status_write == RTC_STATUS_24H);
    CHECK(fake.resets == 0);
    CHECK(!rtc_was_reset());
    end();
}

static void rtc_open_bus_is_absent(void) {
    const u8 ff[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    begin(0xFF, ff);
    fake.open_bus = true;
    RtcTime t = {1, 1, 1, 1, 1, 1};
    CHECK(!rtc_present());
    CHECK(rtc_seconds() == 0);
    CHECK(!rtc_read(&t) && t.year == 0);
    CHECK(!rtc_was_reset()); // no clock, nothing was reset
    end();
}

static void rtc_zeros_is_absent(void) {
    const u8 zeros[7] = {0};
    begin(0, zeros);
    CHECK(!rtc_present());
    end();
}

static void rtc_bad_bcd_is_absent(void) {
    u8 raw[7];
    memcpy(raw, leap_day, 7);
    raw[1] = 0x13; // month 13
    begin(RTC_STATUS_24H, raw);
    CHECK(!rtc_present());
    end();

    memcpy(raw, leap_day, 7);
    raw[1] = 0x04;
    raw[2] = 0x31; // April 31st
    begin(RTC_STATUS_24H, raw);
    CHECK(!rtc_present());
    end();

    memcpy(raw, leap_day, 7);
    raw[5] = 0x5A; // minute 5A
    begin(RTC_STATUS_24H, raw);
    CHECK(!rtc_present());
    end();
}

static void set_clock(u32 seconds) {
    RtcTime t;
    rtc_time_from_seconds(seconds, &t);
    fake.raw[0] = (u8)((t.year - 2000) / 10 << 4 | (t.year - 2000) % 10);
    fake.raw[1] = (u8)(t.month / 10 << 4 | t.month % 10);
    fake.raw[2] = (u8)(t.day / 10 << 4 | t.day % 10);
    fake.raw[4] = (u8)(t.hour / 10 << 4 | t.hour % 10);
    fake.raw[5] = (u8)(t.minute / 10 << 4 | t.minute % 10);
    fake.raw[6] = (u8)(t.second / 10 << 4 | t.second % 10);
}

static void rtc_backwards_warns_once(void) {
    begin(RTC_STATUS_24H, leap_day);
    u32 before = debug_warning_count();
    CHECK(rtc_seconds() == 762525296u);
    set_clock(762525296u - 3600u);
    CHECK(rtc_seconds() == 762521696u);
    CHECK(warned_since(before) == WARNINGS(1));
    set_clock(762521696u - 3600u);
    CHECK(rtc_seconds() == 762518096u);
    CHECK(warned_since(before) == WARNINGS(1));
    end();
}

static void rtc_invalid_after_probe_keeps_last_good(void) {
    begin(RTC_STATUS_24H, leap_day);
    RtcTime t;
    CHECK(rtc_read(&t));
    u32 before = debug_warning_count();
    fake.raw[1] = 0x1A; // not BCD
    CHECK(!rtc_read(&t));
    CHECK(t.year == 2024 && t.month == 2 && t.day == 29 && t.hour == 12 && t.minute == 34 &&
          t.second == 56);
    CHECK(rtc_seconds() == 762525296u);
    CHECK(warned_since(before) == WARNINGS(1));
    end();
}

static void rtc_invalid_read_is_retried(void) {
    begin(RTC_STATUS_24H, leap_day);
    RtcTime t;
    CHECK(rtc_present());
    u32 reads = fake.datetime_reads;
    fake.raw[6] = 0x6A;
    CHECK(!rtc_read(&t));
    CHECK(fake.datetime_reads == reads + 2); // once more
    end();
}

static void rtc_probe_once(void) {
    begin(RTC_STATUS_24H, leap_day);
    RtcTime t;
    CHECK(rtc_present());
    CHECK(rtc_read(&t));
    CHECK(rtc_seconds() != 0);
    CHECK(rtc_was_reset() == false);
    CHECK(fake.status_reads == 1);
    serval_rtc_forget();
    CHECK(rtc_present());
    CHECK(fake.status_reads == 2);
    end();
}

TEST_SUITE(rtc_tests, "rtc", {"conversions round trip", rtc_conversions_round_trip},
           {"conversion limits", rtc_conversion_limits}, {"absent on the host", rtc_absent_host},
           {"present clock reads the time", rtc_present_reads_time},
           {"PM flag is masked", rtc_pm_flag_masked},
           {"power lost resets and sets 24 hours", rtc_power_lost_resets_and_sets_24h},
           {"12 hour mode is switched", rtc_12h_mode_is_switched},
           {"open bus is absent", rtc_open_bus_is_absent},
           {"zeros are absent", rtc_zeros_is_absent}, {"bad BCD is absent", rtc_bad_bcd_is_absent},
           {"backwards time warns once", rtc_backwards_warns_once},
           {"invalid time keeps the last good one", rtc_invalid_after_probe_keeps_last_good},
           {"invalid read is retried", rtc_invalid_read_is_retried},
           {"probe happens once", rtc_probe_once}, );
