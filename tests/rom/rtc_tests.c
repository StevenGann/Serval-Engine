// The cartridge clock's GPIO driver (src/gba/rtc.c) in mGBA, which gives a
// ROM with this game code no clock: the probe finds none, warns about nothing,
// leaves the port closed (its registers read as the ROM's zeros again) and
// costs little; the ID string emulators look for is in the ROM. The present
// path is checked against mGBA's clock with the capture tool (docs/rtc.md).

#include "../test.h"
#include "serval/debug.h"
#include "serval/rtc.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/core/rtc_internal.h"

// "SIIRTC_V001", each character plus one, so this test doesn't put a second
// copy in the ROM.
static const char shifted_id[] = "TJJSUD`W112";

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

static void use_the_cartridge(void) {
    serval_rtc_device = &serval_platform_rtc_device;
    serval_rtc_forget();
}

static void rtc_gba_absent_leaves_port_closed(void) {
    use_the_cartridge();
    u32 before = debug_warning_count();
    CHECK(!rtc_present());
    CHECK(rtc_seconds() == 0);
    CHECK(!rtc_was_reset());
    CHECK(debug_warning_count() == before);
    // Control back at 0: reads there are the ROM's own bytes, zeros.
    CHECK(*(volatile u16*)0x080000C8 == 0);
    CHECK(*(volatile u16*)0x080000C4 == 0);
    CHECK(*(volatile u16*)0x080000C6 == 0);
}

static void rtc_gba_id_in_rom(void) {
    const char* id = serval_platform_rtc_device.id;
    CHECK(id != NULL);
    if (!id)
        return;
    CHECK(((u32)id & 3) == 0);
    CHECK((u32)id >= 0x08000000 && (u32)id < 0x0A000000);
    for (u32 i = 0; i < sizeof shifted_id - 1; i++)
        CHECK(id[i] == shifted_id[i] - 1);
}

static void rtc_gba_probe_cost(void) {
    use_the_cartridge();
    u32 t0 = cycles();
    (void)rtc_present();
    u32 cost = cycles() - t0;
    debug_log(text_format("rtc probe: %u cycles", cost));
    CHECK(cost < 2000000);
}

TEST_SUITE(gba_rtc_tests, "gba_rtc",
           {"absent_leaves_port_closed", rtc_gba_absent_leaves_port_closed},
           {"id_in_rom", rtc_gba_id_in_rom}, {"probe_cost", rtc_gba_probe_cost});
