// rhythm: "BEAT PAWS", a four-lane rhythm game. Notes fall down two pairs of
// lanes, one pair for each thumb, onto paw-print pads, in time with tracker
// music; press each as it lands. A serval dances on the stage between them.
//
// Demonstrates:
//   - Keeping time with tracker music (play.c): every frame starts from
//     music_position(), the ticks heard since row 0, and the charts are in
//     the module's own ticks, made with the music from one source
//     (tools/make-example-audio.py's rhythm set), so the notes cannot drift
//     from it over a song or after a pause. The input's and the picture's
//     latencies are fixed times added to it (docs/audio.md#position),
//     turned into ticks with MusicPosition.tick_samples
//   - Judging, scoring and grading as portable C tested on the host
//     (judge.c, tests/rhythm_tests.c)
//   - Palette effects: the field's colours pulse on every beat and the
//     pads flash, each a few tileset_set_colors() calls, from tables mixed
//     at boot with color_mix
//   - Art drawn at boot from pixel functions and ASCII pictures (art.c):
//     the field and its panel as background tiles kept once, the sprites
//     (paw-print notes in four colours by SPRITE_PALETTE, the serval's dance
//     frames, the words and digits)
//   - A background in front of the sprites (the panel the notes come out
//     from under) and a sprite in front of it (SPRITE_ABOVE_FOREGROUND)
//   - PSG blips for presses and menus over Maxmod's tracker music
//   - music_pause()/music_resume() for a pause menu
//   - A calibration measured by tapping along to steady clicks (the median
//     of the taps' errors, calibrate.c) and saved with the best scores in
//     one versioned save slot (records.c; its rules tested on the host)
//   - The frame's CPU time, the sprites and the busiest scanline on screen
//     (frame_cpu_permille, sprite_stats, sprite_stats_scanlines): SELECT
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then the title: the play screen's stage
//     under its night sky, BEAT PAWS in big gold and mint letters that hop
//     one a beat, the serval dancing, a paw print falling onto a pad on
//     every beat (the pad flashes and sparks as it lands), PRESS START
//     blinking, and the title music, a cheerful loop at 112 BPM. START or
//     A goes on, with a two-note chime (as every choice made in the menus;
//     a move between choices ticks).
//   - The song select, over the stage dimmed: CALM (a relaxed groove,
//     100 BPM, 1:12), STRIDE (bouncy and funky, 124 BPM, 1:06) and RUSH (a
//     driving rock beat, 148 BPM, 1:21), a paw beside the one chosen; the
//     difficulty (EASY or HARD); the best score and grade for it (dashes
//     before any), and FULL COMBO once played without a miss. The chosen
//     song plays, looping, and the serval dances to it. UP and DOWN choose
//     the song, LEFT and RIGHT the difficulty, R turns AUTO on and off
//     ("AUTO: NO RECORDS"), A plays, B goes back to the title, SELECT
//     opens the calibration. Holding L and R and pressing SELECT erases the
//     records (RECORDS ERASED; the calibration stays).
//   - The calibration: steady clicks at 120 BPM, a timing line across the
//     top (EARLY to LATE, -200 to +200 ms; a white diamond at 0). Tap A,
//     LEFT or RIGHT on every click: each tap, silent so that only the
//     clicks are heard, puts a rose diamond on the line and its error under
//     TAPS n / 16 (LAST +12 MS), and the serval lifts a paw. After 16 the
//     offset is their median (MEASURED), the amber diamond, held there until
//     every button has been let go for half a second (LET GO OF THE
//     BUTTONS). B goes back at any time while tapping (the saved offset
//     stays). LEFT and RIGHT adjust it (1 ms a press, 4 a repeat held), A
//     saves it, B leaves the saved one, START taps again (and stops the
//     tapping early: from 4 taps the offset is their median, with fewer
//     TOO FEW TAPS in red and the saved offset stays). A positive offset
//     means the presses come late, as with a TV's lag or wireless audio:
//     the game moves them back, and draws what the player hears by it too,
//     so a note crosses its pad as a press there is PERFECT. It covers
//     latencies up to 200 ms.
//   - The play screen: a dark panel across the top with the song, its
//     difficulty (and AUTO) and the score (000000), and a groove with a
//     white diamond that travels along it as the song goes. Two pairs of
//     lanes, rose and amber on the left, mint and blue on the right, with
//     an oval pad at the foot of each: a paw print, and under it the button
//     that plays it (the D-pad's LEFT and RIGHT as triangles, B, A). Between
//     the pairs a stage under a starry purple sky: a spotlight, black grass
//     and acacias on the horizon, and the serval, golden with black spots
//     and huge ears, dancing: a paw raised on every beat, left then right,
//     a bob between. The sky, the rails and the spotlight pulse brighter on
//     each beat.
//   - The music starts at once. The first bar is a count-in: four
//     woodblock clicks, and big 1, 2, 3, 4 over the stage on them. Then
//     paw-print notes in the lanes' colours slide out from under the panel
//     and fall onto the pads, landing as the music plays them. Pressing a
//     lane's button blips (one pitch a lane, a C major chord LEFT to A) and
//     lights its pad dimly; a press on time hits the note: the pad flashes
//     bright, a ring of sparks opens from it, the note vanishes and PERFECT
//     (gold, within 35 ms) or GOOD (green, within 80 ms) pops up over the
//     stage, with the combo under it from 2 hits ("12" over "COMBO"). A
//     note let past is a MISS (grey), and the combo goes. The score grows
//     with each hit, faster with the combo (up to twice at 50). A press with
//     no note near does nothing but the blip.
//   - With AUTO, the game presses every note itself, in the first frame
//     that reaches it, judged as a press in that frame would be: the blips
//     play the chart, every pad flashes on its note and every judgement is
//     PERFECT, through the last note (a GOOD or MISS there is a timing bug).
//   - START pauses: a blip, the music stops, the picture holds, and PAUSED
//     over CONTINUE, RETRY and QUIT, chosen with UP and DOWN and A (START
//     or B continue). CONTINUE goes on exactly where it was; RETRY starts
//     the song again from its count-in, the score at 0; QUIT fades back to
//     the song select.
//   - When the music ends, after the last note, the screen fades to the
//     results (the music stopped; A chimes), over the stage dimmed: the song, the PERFECT,
//     GOOD and MISS counts, the longest combo, the accuracy, the score, and
//     the grade (S, A, B, C, D) as a big letter that drops in; NEW BEST when
//     the score beats the record, else NEW BEST GRADE for a better grade
//     (never with AUTO), FULL COMBO when nothing was missed (FIRST FULL COMBO
//     the first time) (the serval dances), SAVE FAILED if the save memory
//     refused.
//     A or START goes back to the song select. The records and the
//     calibration survive a power-off.
//   - SELECT, while playing, shows a readout on the bottom two rows: the
//     CPU time (the busiest frame of the last 16, and the highest), the
//     hardware sprites used (and the highest), the counts so far (P, G,
//     M), the busiest scanline's sprite time (L, cycles of 1,210), sprites
//     cut short (C) and lost (X), both 0, and with AUTO, how late after its
//     note's tick the latest autoplay press came (A, milliseconds: under
//     a frame, 17, while the music plays smoothly; PERFECT allows 35).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (the flow and the fades), screens.c (the title, the song
// select, the results), calibrate.c, play.c (a song played), judge.c
// (judging, scoring, the calibration's arithmetic), records.c (the saved
// bests), songs.c (the song table), art.c (the pictures, drawn at boot) and
// sound.c.

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init(); // draws the art, then the title
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
