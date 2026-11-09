// The title, the song select and the results.
//
// The title is the play screen's stage without its panel: the serval
// dances to the title loop, BEAT PAWS hops a letter a beat, and a paw
// print falls onto a pad on every beat, which flashes as it lands, all
// timed by music_ticks() as a song is. The song select and the results
// show the stage dimmed behind their text (art_dim()), so the text reads
// clearly; the select previews the song chosen, looping, and the serval
// dances to it.

#include "screens.h"

#include "art.h"
#include "records.h"
#include "songs.h"
#include "sound.h"

#define BEAT ((FIXED)(BEAT_TICKS << 8))
#define DIM 176 // the stage behind a menu: 176/256 of the way to the backdrop

#define BLINK 48 // PRESS START: on for 32 frames of 48
#define BLINK_ON 32

static u32 frames; // since the screen showed

// The beat the ticks are in and how many ticks into it; beat -1 before row 0.
static int beat_of(FIXED ticks, int* into) {
    if (ticks < 0) {
        *into = 0;
        return -1;
    }
    int beat = (int)(ticks / BEAT);
    *into = (int)((ticks - beat * BEAT) >> 8);
    return beat;
}

void serval_dance(FIXED ticks, int x, int y) {
    int into, beat = beat_of(ticks, &into);
    u8 frame = DANCE_IDLE;
    u16 flags = 0;
    if (beat >= 0) {
        if (into < BEAT_TICKS / 4) {
            frame = DANCE_PAW;
            flags = beat & 1 ? SPRITE_FLIP_H : 0;
        } else if (into >= BEAT_TICKS / 2 && into < BEAT_TICKS * 3 / 4) {
            frame = DANCE_BOB;
        }
    }
    sprite_draw(SPR_SERVAL, frame, x, y, flags);
}

// --- The title ------------------------------------------------------------------------------

// The beat's pulse by the tick in the beat, as play.c's.
static const u8 pulse_curve[BEAT_TICKS] = {7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2,
                                           1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// A pad's flash as its paw lands, by the tick in the beat.
static const u8 land_flash[BEAT_TICKS] = {7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2,
                                          1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};

// A letter's hop, pixels up, by the tick in its beat.
static const u8 hop[BEAT_TICKS] = {3, 5, 6, 6, 5, 3, 1};

// The lane whose pad a beat's paw lands on: a little dance of its own.
static const u8 title_lanes[8] = {0, 3, 1, 2, 0, 2, 1, 3};

#define TITLE_SPEED 512 // pixels a tick, 8.8: HARD's fall
#define TITLE_AHEAD 3   // paws in the air: those of the next beats
#define WORD1_Y 26      // BEAT's and PAWS' middles
#define WORD2_Y 58
#define LETTER_STEP 22 // the four letters within the stage, clear of the rails
#define PRESS_ROW 19

static u8 shown_pulse, shown_flash[LANES];

static void stage_colors(int pulse, const u8* flash) {
    if (pulse != shown_pulse)
        art_pulse(shown_pulse = (u8)pulse);
    for (int l = 0; l < LANES; l++)
        if (flash[l] != shown_flash[l])
            art_pad_flash(l, shown_flash[l] = flash[l]);
}

void title_show(void) {
    frames = 0;
    text_clear();
    art_hide_play();
    art_show_field();
    shown_pulse = 0xFF;
    for (int l = 0; l < LANES; l++)
        shown_flash[l] = 0xFF;
    stage_colors(0, (const u8[LANES]){0});
    music_play(module_title, true);
}

static void title_word(const char* w, int y, int beat, int into, u16 palette) {
    for (int i = 0; w[i]; i++) {
        int up = beat >= 0 && (beat & 3) == i ? hop[into] : 0;
        sprite_draw(SPR_BIG, (u8)art_big_frame(w[i]),
                    STAGE_X - LETTER_STEP * 3 / 2 + i * LETTER_STEP, y - up,
                    SPRITE_PALETTE(palette));
    }
}

bool title_frame(bool input) {
    FIXED t = music_ticks();
    int into, beat = beat_of(t, &into);
    u8 flash[LANES] = {0};
    if (beat >= 0)
        flash[title_lanes[beat & 7]] = land_flash[into];
    stage_colors(beat >= 0 ? pulse_curve[into] : 0, flash);

    // Sprites, front to back.
    title_word("BEAT", WORD1_Y, beat, into, PAL_GOLD);
    title_word("PAWS", WORD2_Y, beat, into, PAL_MINT);
    if (beat >= 0 && into < 9) { // a burst where the paw landed
        int l = title_lanes[beat & 7];
        sprite_draw(SPR_BURST, (u8)(into / 3), lane_x[l], PAD_Y, SPRITE_PALETTE(PAL_LANE + l));
    }
    for (int b = beat + 1; b <= beat + TITLE_AHEAD; b++) {
        if (b < 0)
            continue;
        int l = title_lanes[b & 7];
        int y = PAD_Y - (int)(((b * BEAT - t) * TITLE_SPEED) >> 16);
        sprite_draw(SPR_NOTE, 0, lane_x[l], y, SPRITE_PALETTE(PAL_LANE + l));
    }
    serval_dance(t, STAGE_X, STAGE_FOOT);

    if (frames % BLINK == 0)
        text_print_centered(PRESS_ROW, "PRESS START");
    else if (frames % BLINK == BLINK_ON)
        text_print_line(0, PRESS_ROW, "");
    frames++;
    if (input && (button_pressed(BUTTON_START) || button_pressed(BUTTON_A))) {
        sound_play(SND_SELECT);
        return true;
    }
    return false;
}

// --- The song select ------------------------------------------------------------------------

#define HEAD_ROW 1
#define SONG_ROW 4 // the songs, two rows apart
#define SONG_COL 5
#define LEVEL_ROW 11
#define BEST_ROW 13
#define MESSAGE_ROW 15 // RECORDS ERASED, SAVE FAILED
#define AUTO_ROW 16
#define HELP_ROW 18
#define MESSAGE_FRAMES 150

#define SELECT_SERVAL_X 216
#define SELECT_SERVAL_FOOT 140

static SelectChoice choice = {SONG_CALM, DIFF_EASY, false};
static int message_frames;

// A song's length, m:ss: its ticks at Maxmod's tick for its tempo
// (docs/audio.md#position), to the nearest second.
static const char* song_length(const Song* s) {
    u32 tick_samples = (39420u / s->tempo) & ~1u;
    u32 secs = (s->length_ticks * tick_samples + MUSIC_RATE / 2) / MUSIC_RATE;
    return text_format("%u:%02u", secs / 60, secs % 60);
}

static void select_songs(void) {
    for (int i = 0; i < SONG_COUNT; i++) {
        const Song* s = &songs[i];
        text_set_style(i == choice.song ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_line(SONG_COL, SONG_ROW + 2 * i,
                        text_format("%-7s %3u BPM  %s", s->name, s->tempo, song_length(s)));
    }
    text_set_style(TEXT_NORMAL);
}

static void select_level(void) {
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(LEVEL_ROW, text_format("<  %s  >", difficulty_names[choice.difficulty]));
    text_set_style(TEXT_NORMAL);
    const Best* b = &records_get()->best[choice.song][choice.difficulty];
    if (b->grade)
        text_print_centered(BEST_ROW, text_format("BEST %06u  GRADE %c", b->score, b->grade));
    else
        text_print_centered(BEST_ROW, "BEST ------  GRADE -");
    text_print_centered(BEST_ROW + 1, b->full_combo ? "FULL COMBO" : "");
}

static void select_auto(void) {
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(AUTO_ROW, choice.autoplay ? "AUTO: NO RECORDS" : "");
    text_set_style(TEXT_NORMAL);
}

static void select_message(const char* s, bool warning) {
    text_set_style(warning ? TEXT_WARNING : TEXT_HIGHLIGHT);
    text_print_centered(MESSAGE_ROW, s);
    text_set_style(TEXT_NORMAL);
    message_frames = MESSAGE_FRAMES;
}

static void preview(void) {
    music_play(songs[choice.song].module, true);
}

void select_show(bool save_failed) {
    frames = 0;
    message_frames = 0;
    text_clear();
    art_hide_play();
    art_show_field();
    art_dim(DIM);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(HEAD_ROW, "CHOOSE A SONG");
    text_set_style(TEXT_NORMAL);
    select_songs();
    select_level();
    select_auto();
    text_print_centered(HELP_ROW, "A PLAY  B BACK  R AUTO");
    text_print_centered(HELP_ROW + 1, "SELECT CALIBRATE");
    if (save_failed)
        select_message("SAVE FAILED", true);
    button_repeat_reset();
    preview();
}

SelectAction select_frame(bool input) {
    FIXED t = music_ticks();
    // The chosen song's paw beside it, in its lane colour, tapping the beat.
    int into, beat = beat_of(t, &into);
    int bob = beat >= 0 && into < BEAT_TICKS / 4 ? 1 : 0;
    sprite_draw(SPR_NOTE, 0, SONG_COL * 8 - 12, (SONG_ROW + 2 * choice.song) * 8 + 3 - bob,
                SPRITE_PALETTE(PAL_LANE + choice.song));
    serval_dance(t, SELECT_SERVAL_X, SELECT_SERVAL_FOOT);
    if (message_frames > 0 && --message_frames == 0)
        text_print_line(0, MESSAGE_ROW, "");
    frames++;
    if (!input)
        return SELECT_STAY;

    if (button_pressed(BUTTON_SELECT)) {
        if (button_down(BUTTON_L) && button_down(BUTTON_R)) {
            records_erase();
            sound_play(SND_SELECT);
            select_level();
            if (records_save_failed())
                select_message("SAVE FAILED", true);
            else
                select_message("RECORDS ERASED", false);
            return SELECT_STAY;
        }
        sound_play(SND_SELECT);
        return SELECT_CALIBRATE;
    }
    if (button_pressed(BUTTON_A)) {
        sound_play(SND_SELECT);
        return SELECT_PLAY;
    }
    if (button_pressed(BUTTON_B)) {
        sound_play(SND_SELECT);
        return SELECT_BACK;
    }
    int song = choice.song, level = choice.difficulty;
    if (button_repeat(BUTTON_UP))
        song = (song + SONG_COUNT - 1) % SONG_COUNT;
    if (button_repeat(BUTTON_DOWN))
        song = (song + 1) % SONG_COUNT;
    if (button_pressed(BUTTON_LEFT) || button_pressed(BUTTON_RIGHT))
        level = (level + 1) % DIFF_COUNT;
    if (button_pressed(BUTTON_R) && !button_down(BUTTON_L)) {
        choice.autoplay = !choice.autoplay;
        sound_play(SND_MOVE);
        select_auto();
    }
    if (song != choice.song) {
        choice.song = (u8)song;
        sound_play(SND_MOVE);
        select_songs();
        select_level();
        preview();
    }
    if (level != choice.difficulty) {
        choice.difficulty = (u8)level;
        sound_play(SND_MOVE);
        select_level();
    }
    return SELECT_STAY;
}

const SelectChoice* select_choice(void) {
    return &choice;
}

// --- The results ----------------------------------------------------------------------------

#define RES_TITLE_ROW 1
#define RES_COL 3 // labels; their values end at RES_END
#define RES_END 20
#define GRADE_X 200 // the big grade's middle
#define GRADE_Y 44
#define RES_SERVAL_FOOT 148
#define BADGE_ROW 14 // NEW BEST, FULL COMBO under it
#define FAILED_ROW 17
#define BACK_ROW 19

static char grade;
static bool full_combo;

static void result_line(int row, const char* label, const char* value) {
    text_print(RES_COL, row, label);
    int n = 0;
    while (value[n])
        n++;
    text_print(RES_END - n, row, value);
}

void results_show(const SelectChoice* played, const Score* s, u32 fell) {
    frames = 0;
    text_clear();
    art_hide_play();
    art_show_field();
    art_dim(DIM);
    u16 acc = score_accuracy_permille(s);
    grade = grade_of(acc);
    full_combo = s->miss == 0 && s->perfect + s->good > 0;

    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(RES_TITLE_ROW, text_format("%s  %s%s", songs[played->song].name,
                                                   difficulty_names[played->difficulty],
                                                   played->autoplay ? "  AUTO" : ""));
    text_set_style(TEXT_NORMAL);
    result_line(4, "PERFECT", text_format("%u", s->perfect));
    result_line(5, "GOOD", text_format("%u", s->good));
    result_line(6, "MISS", text_format("%u", s->miss));
    result_line(8, "MAX COMBO", text_format("%u", s->max_combo));
    result_line(9, "ACCURACY", text_format("%u.%u%%", acc / 10, acc % 10));
    result_line(11, "SCORE", text_format("%06u", s->score));
    text_print(GRADE_X / 8 - 2, 2, "GRADE");

    text_set_style(TEXT_HIGHLIGHT);
    int row = BADGE_ROW;
    // The bests fall apart (records.h): a new score, else a better grade
    // (a first play's score of 0 and its D are no bests to speak of), and a
    // first full combo.
    if ((fell & RECORD_SCORE) && s->score > 0)
        text_print_centered_in(0, 23, row++, "NEW BEST");
    else if ((fell & RECORD_GRADE) && s->score > 0)
        text_print_centered_in(0, 23, row++, "NEW BEST GRADE");
    if (fell & RECORD_FULL_COMBO)
        text_print_centered_in(0, 23, row, "FIRST FULL COMBO");
    else if (full_combo)
        text_print_centered_in(0, 23, row, "FULL COMBO");
    if (fell && records_save_failed()) {
        text_set_style(TEXT_WARNING);
        text_print_centered_in(0, 23, FAILED_ROW, "SAVE FAILED");
    }
    text_set_style(TEXT_NORMAL);
    text_print_centered(BACK_ROW, "PRESS A");
    button_repeat_reset();
}

bool results_frame(bool input) {
    // The grade lands from above in its first frames; the serval dances,
    // counting its own beats (the music is over), on a full combo.
    int drop = frames < 12 ? (12 - (int)frames) * 4 : 0;
    sprite_draw(SPR_BIG, (u8)art_big_frame(grade), GRADE_X, GRADE_Y - drop,
                SPRITE_PALETTE(grade == 'S' || grade == 'A' ? PAL_GOLD : PAL_WHITE));
    FIXED beat_ticks = full_combo ? (FIXED)(frames * (BEAT_TICKS * 256 / 30)) : -1;
    serval_dance(beat_ticks, GRADE_X, RES_SERVAL_FOOT);
    frames++;
    if (input && (button_pressed(BUTTON_A) || button_pressed(BUTTON_START))) {
        sound_play(SND_SELECT);
        return true;
    }
    return false;
}
