// life: Conway's Game of Life, to watch and to play with: a world of 120 x 80
// cells on a torus, drawn by the pixel surface, which paint and stamps change
// while it runs or stands still.
//
// Demonstrates:
//   - The pixel surface (surface.h) as the world itself: a cell is a byte of
//     the page, its color number and its state at once (0 empty, 1-12 a
//     fading trail, 32-63 alive, the value its age), so the simulation reads
//     and writes the colors directly (life_rules.c). The step writes each new
//     row into RAM and into the page being drawn in the same pass, in words,
//     then surface_flip() shows it
//   - The surface's transform (surface_set_transform) doing the drawing work:
//     the 120 x 80 grid is stretched 2x to fill the screen, or 4x around the
//     cursor, with a smooth eased zoom between them and a window that
//     follows the cursor, all at no CPU cost
//   - The step (life_rules.c): four cells a word, the live neighbours of four
//     cells counted at once with no carries between their bytes, and a
//     640-byte table (in IWRAM) that gives each cell's next value from its
//     value and its count, so a rule, the ages and the trails are one
//     lookup; rows with nothing in or beside them skipped. Portable C,
//     host-tested, run on the GBA from IWRAM as ARM code
//     (SERVAL_IWRAM_CODE): 160,000 cycles for the opening world, 330,000
//     (more than a frame) for a fresh random soup, so a generation is
//     stepped in two halves over two frames unless it is sure to fit in one
//     (game.c); every frame stays under 90% of the CPU but the few that
//     save (save_write() to SRAM takes about a frame)
//   - Six rules as birth and survival masks (CONWAY B3/S23, HIGHLIFE, DAY &
//     NIGHT, SEEDS, MAZE, NO DEATH); still lifes and oscillators recognized
//     by a hash of the world, kept for the last 30 generations
//   - Colors by age, in four themes made at boot with color_mix(): newborns
//     white-hot, cooling as they live, so busy fronts glow and settled
//     patterns rest in a calm color; the backdrop a gradient by
//     raster_backdrop(); the whole picture dimmed behind the title and the
//     menu by mixing the palette toward the theme's night
//   - Text without a text layer (label.c): a font of the game's own,
//     composed into 32 x 8 sprite frames, a line per few sprites, and sent
//     to video memory with sprite_set_tiles() only for the frames that
//     changed, at most 8 a frame; the HUD and the menu over strips of dark
//     glass (SPRITE_BLEND, screen_set_blend)
//   - The PSG as a generative instrument: the population, mapped onto a
//     pentatonic scale, plays a soft pluck on the wave channel (a waveform
//     of the game's own, psg_waves_set) as the world changes; clicks,
//     thumps and sweeps for the edits
//   - Save data (save.h): the settings and the world, a bit per cell, saved
//     on pausing and as the menu closes, restored at boot; the loader refuses
//     values it doesn't know (life_save.c)
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then the title fades in: LIFE in big letters
//     made of square cells over the world, which runs behind them, dimmed
//     (a glider gun firing gliders into an R-pentomino's and an acorn's
//     chaos, a pulsar, a spaceship), or the world saved last time. The
//     letters bob gently, their cells shimmering through the theme's warm
//     colors; "CONWAY'S GAME OF LIFE" under them, "PRESS START" pulsing, and
//     a line of hints that changes every few seconds. A soft plucked tune
//     plays, its notes following the population.
//   - Any button: a rising four-note chime, the letters fly up one after
//     another and the world brightens: the whole world, 2 screen pixels a
//     cell, paused, a pulsing white ring around the cell in the middle. The
//     HUD along the bottom on dark glass: "| PAUSE GEN 123 POP 456"; at the
//     right "STILL" when nothing changes any more, "PERIOD 2" (or another
//     number) when the world repeats, "EMPTY".
//   - Cells are white when born and cool with age (EMBER: yellow, orange,
//     rose, then violet), and leave a fading ghost for 12 generations when
//     they die. Still lifes and oscillators settle into violet; the gliders
//     and the fronts of growth glow.
//   - D-pad: moves the ring a cell; held, it repeats faster and faster. It
//     wraps around the edges, as the world does.
//   - A: toggles the cell (a high click on, a lower one off). Holding A and
//     moving paints: every cell the ring enters gets what the first toggle
//     made, alive or empty. Works while the world runs too.
//   - B (hold): the stamp (a glider at first) shows at the ring's place as a
//     see-through, breathing preview instead of the ring: its cells pale, a
//     live cell under one of them bright white, a live cell in its box that
//     it would clear dark red, the rest of its box faintly lit. L and R turn
//     it a quarter turn (a tick). Releasing B puts it down (a thump), its
//     middle on the cell the ring was on.
//   - L: zooms in (a rising slide) to 4 pixels a cell, about the ring, in a
//     smooth eased move; the view then follows the ring, and the ring grows
//     to fit the bigger cells. L again zooms out.
//   - START: runs (two rising notes) or pauses (two falling ones; the world
//     is saved). The HUD then shows "} RUN 15/S" with the speed.
//   - R: while paused, steps one generation (a tick; held, it repeats);
//     while running, held, runs as fast as it can ("TURBO"): 60 generations
//     a second while the world is sparse, 30 once it is busy.
//   - SELECT: the menu (two notes): the world dims and stops behind a band
//     of dark glass with bright edges; rows STAMP (GLIDER, SPACESHIP,
//     R-PENTOMINO, ACORN, DIEHARD, PULSAR, PENTADECATHLON, GLIDER GUN), RULE
//     (with its B/S notation), SPEED (1, 2, 4, 8, 15, 30 or 60 a second),
//     COLORS (EMBER, OCEAN, NEON, PHOSPHOR: the world behind changes at
//     once), SOUND (ON, OFF), RANDOM (A: a new random soup, about 30% alive,
//     with a rising sweep) and CLEAR (A: an empty world, a falling sweep).
//     UP and DOWN choose a row (the chosen one white, marked by a ring),
//     LEFT and RIGHT change it (a blip). SELECT or B closes it (two falling
//     notes) and saves; RANDOM and CLEAR close it too.
//   - Sound: while the world runs, a soft pluck about eight times a second
//     at most (every generation up to 8 a second), its pitch the population
//     on an A minor pentatonic scale, louder when many cells are born;
//     silent when the world stands still.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into life_rules.c (the world, the rules, the step and the stamps; portable
// and host-tested), life_save.c (the save's format, host-tested), game.c
// (the world on screen, the cursor, editing, the zoom, the HUD), title.c,
// menu.c, label.c (the font and text in sprites), art.c (the themes, the
// sprites) and sound.c.

#include "life.h"

enum { STATE_TITLE, STATE_PLAY, STATE_MENU };

int main(void) {
    serval_init();
    serval_splash();

    // Behind black: the surface, the art, the world, the labels.
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN);
    surface_begin();
    labels_build();
    art_build();
    sound_init();
    game_init();
    title_build();
    menu_build();
    art_load();
    // The glass: the world behind it at about 40%.
    screen_set_blend(0, LAYER_PLAYFIELD | LAYER_BACKDROP, 10, 6);
    title_enter();

    int state = STATE_TITLE;
    for (int fade = SCREEN_BRIGHTNESS_MIN;; fade += fade < 0) {
        frame_begin();
        screen_set_brightness(fade);
        switch (state) {
        case STATE_TITLE:
            if (!title_frame())
                state = STATE_PLAY;
            break;
        case STATE_PLAY:
            game_frame();
            if (game_wants_menu()) {
                menu_open();
                state = STATE_MENU;
            }
            break;
        default:
            if (!menu_frame())
                state = STATE_PLAY;
            break;
        }
        art_frame();
        labels_flush();
        frame_end();
    }
}
