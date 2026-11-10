// The web page's clock (src/web/rtc.c, shell.html): in the web test page,
// which CI opens with rtc=762525296 (2024-02-29 12:34:56), the clock is there,
// starts at that time, and is not reported as reset. The suite runs before the
// page's first frame, so it sees the start time; opened without rtc=, it fails.

#include "../test.h"
#include "serval/rtc.h"

static void the_page_has_its_clock(void) {
    CHECK(rtc_present());
    CHECK(!rtc_was_reset());
}

static void the_clock_starts_at_the_pages_time(void) {
    CHECK(rtc_seconds() == 762525296u);
    RtcTime t;
    CHECK(rtc_read(&t));
    CHECK(t.year == 2024 && t.month == 2 && t.day == 29);
    CHECK(t.hour == 12 && t.minute == 34 && t.second == 56);
}

TEST_SUITE(web_rtc_tests, "web clock", {"the_page_has_its_clock", the_page_has_its_clock},
           {"the_clock_starts_at_the_pages_time", the_clock_starts_at_the_pages_time});
