// The game's flow: the title, a song, its results, and the fades between
// them.
//
// For now the title is a placeholder: the name, the song, difficulty and
// autoplay to play (CALM, HARD and AUTO at boot), and PRESS START. UP and
// DOWN choose the song, LEFT and RIGHT the difficulty, R turns the
// autoplay on and off. The results are the counts as text. The title and
// song select screens, the real results and the calibration come later
// (screens.c); the states here are theirs to grow into.

#include "game.h"

#include "art.h"
#include "play.h"
#include "songs.h"
#include "sound.h"

#define FADE_STEP 2 // brightness per frame: 8 frames to or from black

enum { TITLE, PLAYING, RESULTS };

static int state;
static u8 song = SONG_CALM, difficulty = DIFF_HARD;
static bool autoplay = true;
static u32 screen_frames;

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

// --- The title ----------------------------------------------------------------------------

static void title_choice(void) {
    text_print_line(0, 9, "");
    text_print_centered(9, text_format("%s  %s%s", songs[song].name, difficulty_names[difficulty],
                                       autoplay ? "  AUTO" : ""));
}

static void show_title(void) {
    state = TITLE;
    screen_frames = 0;
    art_hide_play();
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(4, "BEAT PAWS");
    text_set_style(TEXT_NORMAL);
    title_choice();
    text_print_centered(15, "UP DOWN: SONG");
    text_print_centered(16, "LEFT RIGHT: LEVEL");
    text_print_centered(17, "R: AUTO");
    music_play(module_title, true);
}

static void start_song(void) {
    state = PLAYING;
    play_begin(song, difficulty, autoplay, 0);
}

static void title_frame(void) {
    if (!fade && (button_pressed(BUTTON_START) || button_pressed(BUTTON_A))) {
        sound_play(SND_SELECT);
        fade_to(start_song);
    } else if (!fade) {
        int s = song, d = difficulty;
        if (button_pressed(BUTTON_UP))
            s = (s + SONG_COUNT - 1) % SONG_COUNT;
        if (button_pressed(BUTTON_DOWN))
            s = (s + 1) % SONG_COUNT;
        if (button_pressed(BUTTON_LEFT) || button_pressed(BUTTON_RIGHT))
            d = (d + 1) % DIFF_COUNT;
        bool a = autoplay;
        if (button_pressed(BUTTON_R) && !button_down(BUTTON_L))
            a = !a;
        if (s != song || d != difficulty || a != autoplay) {
            song = (u8)s;
            difficulty = (u8)d;
            autoplay = a;
            sound_play(SND_MOVE);
            title_choice();
        }
    }
    // PRESS START blinks.
    if (screen_frames % 32 == 0)
        text_print_centered(12, "PRESS START");
    else if (screen_frames % 32 == 20)
        text_print_line(0, 12, "");
    screen_frames++;
}

// --- The results --------------------------------------------------------------------------

static void show_results(void) {
    state = RESULTS;
    art_hide_play();
    text_clear();
    const Score* s = play_result();
    u16 acc = score_accuracy_permille(s);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(2, text_format("%s  %s%s", songs[song].name, difficulty_names[difficulty],
                                       autoplay ? "  AUTO" : ""));
    text_set_style(TEXT_NORMAL);
    text_print(8, 5, text_format("PERFECT %5u", s->perfect));
    text_print(8, 6, text_format("GOOD    %5u", s->good));
    text_print(8, 7, text_format("MISS    %5u", s->miss));
    text_print(8, 9, text_format("MAX COMBO %3u", s->max_combo));
    text_print(8, 10, text_format("ACCURACY %u.%u%%", acc / 10, acc % 10));
    text_print(8, 11, text_format("SCORE  %6u", s->score));
    text_print(8, 13, text_format("GRADE %c", grade_of(acc)));
    if (s->miss == 0 && s->perfect + s->good > 0)
        text_print_centered(15, "FULL COMBO");
    text_print_centered(18, "PRESS START");
}

static void results_frame(void) {
    if (!fade && (button_pressed(BUTTON_START) || button_pressed(BUTTON_A))) {
        sound_play(SND_SELECT);
        fade_to(show_title);
    }
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
    text_set_style(TEXT_HIGHLIGHT);
    text_set_style(TEXT_NORMAL);
    sound_init();
    audio_bank_set(songs_bank);
    show_title();
}

void game_frame(void) {
    run_fade();
    switch (state) {
    case TITLE:
        title_frame();
        break;
    case PLAYING:
        if (fade < 0) {
            play_draw(); // the picture held through the fade out
            break;
        }
        switch (play_frame()) {
        case PLAY_FINISHED:
            fade_to(show_results);
            break;
        case PLAY_QUIT:
            fade_to(show_title);
            break;
        default:
            break;
        }
        break;
    default:
        results_frame();
        break;
    }
}
