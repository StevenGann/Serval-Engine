// The game's flow: the title, the song select, a song, its results, the
// calibration, and the fades between them.
//
//   title --START--> select --A--> song --end--> results --A--> select
//                    select --B--> title
//                    select --SELECT--> calibration --A or B--> select
//                    song --pause, QUIT--> select
//
// Every change of screen fades to black, sets the next screen up under the
// black and fades it in. The screens keep drawing through the fades (input
// is ignored until they end). The records are read at boot and taken from
// each song that ends (records_take(), which never takes the autoplay's).

#include "game.h"

#include "art.h"
#include "calibrate.h"
#include "play.h"
#include "records.h"
#include "screens.h"
#include "songs.h"
#include "sound.h"

#define FADE_STEP 2 // brightness per frame: 8 frames to or from black

enum { TITLE, SELECT, PLAYING, RESULTS, CALIBRATE };

static int state;
static SelectChoice played; // the song being played, for its results

static int brightness = SCREEN_BRIGHTNESS_MIN;
static int fade = 1; // 1 fading in, -1 fading out, 0 neither
static void (*after_fade)(void);

// Fades to black, then calls next() to set up the next screen under the
// black, and fades that in.
static void fade_to(void (*next)(void)) {
    after_fade = next;
    fade = -1;
}

static void run_fade(void) {
    if (fade > 0) {
        brightness = int_min(brightness + FADE_STEP, 0);
        fade = brightness < 0;
    } else if (fade < 0) {
        brightness -= FADE_STEP;
        if (brightness <= SCREEN_BRIGHTNESS_MIN) {
            brightness = SCREEN_BRIGHTNESS_MIN;
            fade = 1;
            after_fade();
        }
    }
    screen_set_brightness(brightness);
}

// --- The screens' set-ups, each called under the black ------------------------------------

static void to_title(void) {
    state = TITLE;
    title_show();
}

static bool calibration_failed; // the calibration's save just failed: say so on the select

static void to_select(void) {
    state = SELECT;
    select_show(calibration_failed);
    calibration_failed = false;
}

static void to_song(void) {
    state = PLAYING;
    played = *select_choice();
    play_begin(played.song, played.difficulty, played.autoplay, records_get()->calibration_ms);
}

static void to_results(void) {
    state = RESULTS;
    const Score* s = play_result();
    u32 fell = records_take(played.song, played.difficulty, s, played.autoplay);
    results_show(&played, s, fell);
}

static void to_calibration(void) {
    state = CALIBRATE;
    calibrate_show();
}

// --- The frame ----------------------------------------------------------------------------

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    art_build();
    art_load();
    text_set_shadow(true); // text reads over the bright panel and the stage
    // Each text style builds its glyphs the first time it is used (tens of
    // thousands of cycles): use them now, under the black, not in a song.
    text_clear();
    text_set_style(2); // SAVE FAILED's
    text_set_style(TEXT_HIGHLIGHT);
    text_set_style(TEXT_NORMAL);
    sound_init();
    records_load();
    audio_bank_set(songs_bank);
    to_title();
}

void game_frame(void) {
    run_fade();
    bool input = fade == 0;
    switch (state) {
    case TITLE:
        if (title_frame(input))
            fade_to(to_select);
        break;
    case SELECT:
        switch (select_frame(input)) {
        case SELECT_PLAY:
            fade_to(to_song);
            break;
        case SELECT_BACK:
            fade_to(to_title);
            break;
        case SELECT_CALIBRATE:
            fade_to(to_calibration);
            break;
        default:
            break;
        }
        break;
    case PLAYING:
        if (fade < 0) {
            play_draw(); // the picture held through the fade out
            break;
        }
        switch (play_frame()) {
        case PLAY_FINISHED:
            fade_to(to_results);
            break;
        case PLAY_QUIT:
            fade_to(to_select);
            break;
        default:
            break;
        }
        break;
    case RESULTS:
        if (results_frame(input))
            fade_to(to_select);
        break;
    default:
        switch (calibrate_frame(input)) {
        case CALIBRATE_SAVED:
            calibration_failed = records_save_failed();
            fade_to(to_select);
            break;
        case CALIBRATE_CANCELLED:
            fade_to(to_select);
            break;
        default:
            break;
        }
        break;
    }
}
