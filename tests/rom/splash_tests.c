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
    // A game using background 0 itself, without the text layer.
    serval_text_deactivate();
    const u16 game_bg0cnt = BG_CBB(2) | BG_SBB(20) | BG_PRIO(3);
    REG_BG0CNT = game_bg0cnt;
    REG_DISPCNT |= DCNT_BG0;
    bool text_before = serval_text_active();

    u32 start = frame_count();
    serval_splash();
    u32 frames = frame_count() - start;

    // 30 fade in + 30 hold + 90 hold + 30 fade out = 180 frames (~3 s).
    CHECK(frames >= 178 && frames <= 182);
    CHECK(serval_psg_rate(PSG_SQUARE1) == 2048 - 131072 / 1319); // the jingle's last note
    CHECK((REG_SND1CNT >> 12) == 0);                             // and silenced after
    CHECK(pal_bg_mem[0] == RGB15(3, 6, 9));
    CHECK(REG_BLDCNT == 0); // (BLDY is write-only: can't be checked)
    CHECK(serval_text_active() == text_before);
    CHECK(se_mem[31][8 * 32 + 10] == 0); // the splash text is gone
    CHECK(REG_BG0CNT == game_bg0cnt);
    CHECK(REG_DISPCNT & DCNT_BG0);
    REG_DISPCNT &= ~DCNT_BG0;
}

TEST_SUITE(splash_tests, "splash",
           {"splash_runs_and_restores_state", splash_runs_and_restores_state});
