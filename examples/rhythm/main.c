// rhythm (BEAT PAWS): a placeholder while the game is built.
//
// Plays the game's music, made with its charts by
// tools/make-example-audio.py's rhythm set (rhythm_bank.h, rhythm_charts.h).
//
// What to expect when booting the ROM:
//   - After the splash, "BEAT PAWS" and the title loop playing (a cheerful
//     tune in C major at 112 BPM, 16 bars, looped).
//   - A plays the next module: the three songs once each (CALM, STRIDE,
//     RUSH, with their charts' note counts and first ticks), the
//     calibration clicks, then the title again. A song shows ENDED when it
//     has played.
//
// Uses only Serval Engine's API; no third-party headers.

#include <serval/serval.h>

#include "rhythm_bank.h"
#include "rhythm_charts.h"

typedef struct {
    const char* name;
    u32 module;
    bool loop;
    const RhythmNote *easy, *hard; // its charts (NULL: none)
    u16 easy_count, hard_count;
} Track;

static const Track tracks[] = {
    {"TITLE", MOD_RHYTHM_TITLE, true, NULL, NULL, 0, 0},
    {"CALM", MOD_RHYTHM_CALM, false, rhythm_calm_easy, rhythm_calm_hard, RHYTHM_CALM_EASY_COUNT,
     RHYTHM_CALM_HARD_COUNT},
    {"STRIDE", MOD_RHYTHM_STRIDE, false, rhythm_stride_easy, rhythm_stride_hard,
     RHYTHM_STRIDE_EASY_COUNT, RHYTHM_STRIDE_HARD_COUNT},
    {"RUSH", MOD_RHYTHM_RUSH, false, rhythm_rush_easy, rhythm_rush_hard, RHYTHM_RUSH_EASY_COUNT,
     RHYTHM_RUSH_HARD_COUNT},
    {"CALIBRATE", MOD_RHYTHM_CALIBRATE, true, NULL, NULL, 0, 0},
};
#define TRACK_COUNT (int)(sizeof tracks / sizeof tracks[0])

static void show(int t) {
    const Track* k = &tracks[t];
    text_print_centered(9, k->name);
    if (k->hard) {
        text_print_centered(
            11, text_format("EASY %d NOTES FROM TICK %d", k->easy_count, (int)k->easy[0].tick));
        text_print_centered(
            12, text_format("HARD %d NOTES FROM TICK %d", k->hard_count, (int)k->hard[0].tick));
    } else {
        text_print_centered(11, "");
        text_print_centered(12, "");
    }
}

int main(void) {
    serval_init();
    serval_splash();
    audio_bank_set(rhythm_bank);
    text_print_centered(3, "BEAT PAWS");
    text_print_centered(17, "A NEXT MODULE");

    int t = 0;
    music_play(tracks[t].module, tracks[t].loop);
    show(t);
    bool ended = false;
    for (;;) {
        frame_begin();
        if (button_pressed(BUTTON_A)) {
            t = (t + 1) % TRACK_COUNT;
            music_play(tracks[t].module, tracks[t].loop);
            show(t);
            ended = false;
            text_print_centered(14, "");
        } else if (!ended && !music_playing()) {
            ended = true;
            text_print_centered(14, "ENDED");
        }
        frame_end();
    }
}
