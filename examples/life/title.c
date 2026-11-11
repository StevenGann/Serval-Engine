// The title: LIFE in letters made of cells over the world, which runs behind
// them, dimmed, playing its own tune. The letters bob, their cells cycling
// through the theme's young colors; PRESS START pulses; a line of hints
// changes every few seconds. Any button gives the player the world, paused,
// as it stands.

#include "life.h"

#define SUBTITLE_FRAME 9
#define SUBTITLE_BLOCKS 5
#define HINT_FRAME 14
#define HINT_BLOCKS 7
#define TITLE_DIM 120
#define HINT_FRAMES 200 // each hint shows this long
#define LEAVE_FRAMES 20

static Label subtitle, hint, press;
static int t, leaving;

static const char* const hints[] = {
    "~A~ TOGGLE A CELL, HOLD TO PAINT",
    "HOLD ~B~ FOR A STAMP, ~L~ ~R~ TURN IT",
    "~START~ RUN   ~R~ STEP   ~L~ ZOOM",
    "~SELECT~ STAMPS, RULES AND COLORS",
};
#define HINTS ((int)(sizeof hints / sizeof hints[0]))

void title_build(void) {
    label_init(&subtitle, SPR_TEXT, SUBTITLE_FRAME, SUBTITLE_BLOCKS, "CONWAY'S GAME OF LIFE");
    label_init(&hint, SPR_TEXT, HINT_FRAME, HINT_BLOCKS, hints[0]);
    label_init(&press, SPR_BIG, 0, BIG_FRAMES, "PRESS START");
}

void title_enter(void) {
    t = 0;
    leaving = 0;
}

static void logo_draw(void) {
    static const int gap = 8;
    int total = -gap;
    for (u32 l = 0; l < LOGO_LETTERS; l++)
        total += logo_width(l) + gap;
    int x = (SCREEN_W - total) / 2;
    for (u32 l = 0; l < LOGO_LETTERS; l++) {
        int y = 20 + fx_sin((u16)(t * 500 + l * 12000)) * 3 / FX_ONE;
        if (leaving) {
            int up = leaving - (int)l * 2;
            if (up > 0)
                y -= up * up / 2;
        }
        if (y > -64)
            sprite_draw(SPR_LOGO, (u8)l, x, y, 0);
        x += logo_width(l) + gap;
    }
}

bool title_frame(void) {
    const LifeSettings* s = game_settings();
    t++;
    if (!leaving && t > 30 && button_pressed(BUTTON_ANY)) {
        leaving = 1;
        sound_play(SFX_START);
    }
    if (leaving) {
        if (++leaving > LEAVE_FRAMES) {
            art_show(s->theme, 0);
            game_start();
            return false;
        }
        art_show(s->theme, (u32)(TITLE_DIM * (LEAVE_FRAMES - leaving) / LEAVE_FRAMES));
    } else {
        art_show(s->theme, TITLE_DIM);
    }
    game_title_frame();

    label_set(&hint, hints[t / HINT_FRAMES % HINTS]);
    logo_draw();
    if (!leaving) {
        label_draw_centered(&subtitle, 70, SPRITE_PALETTE(PAL_SOFT));
        // The hint fades in and out by blinking off between hints.
        if (t % HINT_FRAMES > 8)
            label_draw_centered(&hint, 140, 0);
        label_draw_centered(&press, 100, 0);
    }
    return true;
}
