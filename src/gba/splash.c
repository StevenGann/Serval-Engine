#include "serval/audio.h"
#include "serval/core.h"
#include "serval/text.h"

#include <tonc.h>

#include "../core/splash_internal.h"
#include "internal.h"
#include "screen_internal.h"
#include "splash_art.h"

// The "made with Serval Engine" splash: the logo (splash_art.c) on
// background 0, faded in and out through BLDY; the timing and the buttons
// are the portable logic in src/core/splash_logic.c.

// "made with" is the text layer's font through palette bank 14 (grey). The
// logo's own tiles and colors are splash_art.c's (charblock 1, bank 13).
#define GREY_BANK 14

// A coin-pickup style jingle: a short B5, then a long E6 that fades away.
static const PsgSound jingle_first = {.channel = PSG_SQUARE1,
                                      .frequency = 988,
                                      .frames = SERVAL_SPLASH_JINGLE_FIRST_NOTE,
                                      .volume = 12};
static const PsgSound jingle_second = {
    .channel = PSG_SQUARE1, .frequency = 1319, .fade = -3, .volume = 12};

// Fades background 0 (the logo) toward black: 0 = fully visible, 16 = black.
static void set_darkness(u32 level) {
    REG_BLDY = (u16)level;
}

void serval_splash(void) {
    // Borrow the backdrop, a grey in bank 14 and the logo's bank 13, the
    // blend control (the game's brightness or blending), the text shadow and
    // background 0 (control register and display bit); put them back at the
    // end. Charblock 1 is not put back: games load their tilesets after.
    // palettes: palette writes still waiting land first, so the splash
    // borrows and puts back the colors the game gave
    if (serval_palette_hooks)
        serval_palette_hooks->flush();
    bool text_was_active = serval_text_active();
    u16 old_bg0cnt = REG_BG0CNT;
    u16 old_bg0_shown = REG_DISPCNT & DCNT_BG0;
    u16 old_backdrop = pal_bg_mem[0];
    u16 old_grey = pal_bg_bank[GREY_BANK][1];
    u16 old_logo_colors[15];
    for (u32 c = 0; c < 15; c++)
        old_logo_colors[c] = pal_bg_bank[SERVAL_SPLASH_ART_BANK][c + 1];
    bool old_shadow = serval_text_shadow();

    pal_bg_mem[0] = RGB15(0, 0, 0);
    text_set_shadow(false);
    text_clear(); // also sets the text layer up if the game hadn't
    pal_bg_bank[GREY_BANK][1] = RGB15(16, 16, 16);
    serval_blend_borrow(true); // blending: frame_end() leaves BLDCNT to the splash
    REG_BLDCNT = BLD_BG0 | BLD_BLACK;
    set_darkness(16);
    // Background 0 is black now, so drawing the logo into VRAM (a frame's
    // work) shows nothing, like the font's tiles above.
    serval_splash_art_draw(GREY_BANK);

    ServalSplashState state;
    ServalSplashFrame frame;
    serval_splash_logic_begin(&state);
    for (;;) {
        frame_begin();
        serval_splash_logic_step(&state, key_hit(BUTTON_ANY), &frame);
        set_darkness(frame.darkness);
        if (frame.jingle_first)
            serval_psg_play_sound(&jingle_first);
        if (frame.jingle_second)
            serval_psg_play_sound(&jingle_second);
        frame_end();
        if (frame.last)
            break;
    }

    // Clear the logo and put everything back.
    serval_psg_silence(PSG_SQUARE1);
    text_clear();
    if (!text_was_active)
        serval_text_deactivate();
    REG_BG0CNT = old_bg0cnt;
    REG_DISPCNT = (u16)((REG_DISPCNT & ~DCNT_BG0) | old_bg0_shown);
    pal_bg_bank[GREY_BANK][1] = old_grey;
    for (u32 c = 0; c < 15; c++)
        pal_bg_bank[SERVAL_SPLASH_ART_BANK][c + 1] = old_logo_colors[c];
    text_set_shadow(old_shadow);
    // blending: the game's effect back (BLDY is write-only, so screen.c
    // keeps its level): its brightness, or at level 0 its blending.
    serval_blend_borrow(false);
    serval_screen_apply_brightness();
    pal_bg_mem[0] = old_backdrop;
}
