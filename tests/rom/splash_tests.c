// Tests for serval_splash (src/gba/splash.c): it runs for its full length
// without input, plays its jingle, and puts back everything it borrows.

#include "../test.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/random.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

static void splash_runs_and_restores_state(void) {
    pal_bg_mem[0] = RGB15(3, 6, 9); // a game's backdrop
    REG_BLDCNT = 0;
    REG_BLDY = 0;
    bool text_before = serval_text_active();

    u32 start = random_entropy(); // the CPU cycle counter
    serval_splash();
    u32 frames = (random_entropy() - start) / 280896;

    // 30 fade in + 30 hold + 90 hold + 30 fade out = 180 frames (~3 s).
    CHECK(frames >= 178 && frames <= 182);
    CHECK(serval_psg_rate(PSG_SQUARE1) == 2048 - 131072 / 1319); // the jingle's last note
    CHECK((REG_SND1CNT >> 12) == 0);                             // and silenced after
    CHECK(pal_bg_mem[0] == RGB15(3, 6, 9));
    CHECK(REG_BLDCNT == 0); // (BLDY is write-only: can't be checked)
    CHECK(serval_text_active() == text_before);
    CHECK(se_mem[31][8 * 32 + 10] == 0); // the splash text is gone
}

TEST_SUITE(splash_tests, "splash",
           {"splash_runs_and_restores_state", splash_runs_and_restores_state});
