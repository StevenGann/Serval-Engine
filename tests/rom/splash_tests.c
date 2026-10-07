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
    // A game's colors in the logo's palette bank (13) and the grey's (14).
    for (u32 bank = 13; bank <= 14; bank++)
        for (u32 c = 1; c < 16; c++)
            pal_bg_bank[bank][c] = RGB15(bank - 10, c, 7);

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
    // The logo and its text are gone from the map (the splash uses rows
    // 0-19 of screenblock 31)...
    bool map_clear = true;
    for (u32 i = 0; i < 20 * 32; i++)
        map_clear = map_clear && se_mem[31][i] == 0;
    CHECK(map_clear);
    // ...and the two palette banks hold the game's colors again.
    bool banks_restored = true;
    for (u32 bank = 13; bank <= 14; bank++)
        for (u32 c = 1; c < 16; c++)
            banks_restored = banks_restored && pal_bg_bank[bank][c] == RGB15(bank - 10, c, 7);
    CHECK(banks_restored);
    CHECK(REG_BG0CNT == game_bg0cnt);
    CHECK(REG_DISPCNT & DCNT_BG0);
    REG_DISPCNT &= ~DCNT_BG0;
}

TEST_SUITE(splash_tests, "splash",
           {"splash_runs_and_restores_state", splash_runs_and_restores_state});
