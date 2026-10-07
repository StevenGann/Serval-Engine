// Tests for PSG sound effects (src/gba/psg.c), checked against the sound
// registers they program.

#include "../test.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/debug.h"

#include <tonc.h>

#include "../../src/core/psg_sequencer.h"
#include "../../src/gba/internal.h"

#define VOLUME(cnt) ((cnt) >> 12)

enum { SND_BEEP, SND_MELODY, SND_SLIDE, SND_NOISE, SND_DEFAULTS, SOUND_COUNT };

static const u16 melody_notes[] = {262, 0, 392};
static const PsgSound beep = {.channel = PSG_SQUARE2,
                              .frequency = 440,
                              .frames = 3,
                              .volume = 12,
                              .fade = -2,
                              .duty = PSG_DUTY_25};
static const PsgSound melody = {
    .channel = PSG_SQUARE2, .frames = 2, .notes = melody_notes, .note_count = 3};
static const PsgSound slide = {
    .channel = PSG_SQUARE1, .frequency = 880, .frames = 10, .slide = -3, .slide_size = 2};
static const PsgSound noise = {.channel = PSG_NOISE, .frequency = 4096, .fade = -1};
static const PsgSound defaults = {.frequency = 1000, .frames = 1};

static const PsgSound* const sounds[SOUND_COUNT] = {[SND_BEEP] = &beep,
                                                    [SND_MELODY] = &melody,
                                                    [SND_SLIDE] = &slide,
                                                    [SND_NOISE] = &noise,
                                                    [SND_DEFAULTS] = &defaults};

static void frames(int n) {
    for (int i = 0; i < n; i++) {
        frame_begin();
        frame_end();
    }
}

static void init_turns_sound_on(void) {
    CHECK(REG_SNDSTAT & SSTAT_ENABLE);
    CHECK((REG_SNDDMGCNT & (SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE) << 8) ==
          (SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE) << 8);
}

static void play_programs_frequency_volume_and_envelope(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_BEEP);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 440);
    CHECK(VOLUME(REG_SND2CNT) == 12);
    CHECK(((REG_SND2CNT >> 8) & 7) == 2 && !(REG_SND2CNT & (1 << 11))); // fade out, step 2
    CHECK(((REG_SND2CNT >> 6) & 3) == 1);                               // 25%
}

static void sounds_stop_after_their_frames(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_BEEP); // 3 frames
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 12);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
}

static void melodies_step_through_notes_and_rests(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_MELODY); // 262 Hz, rest, 392 Hz; 2 frames each
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 262);
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 0); // rest
    frames(2);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 392);
    CHECK(VOLUME(REG_SND2CNT) == 15); // default volume
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 0); // finished
}

static void slide_uses_the_sweep_unit(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_SLIDE);
    CHECK(REG_SND1SWEEP == (3 << 4 | 0x0008 | 2)); // time 3, down, shift 2
    psg_play(SND_DEFAULTS);                        // square 1 without a slide
    CHECK(REG_SND1SWEEP & 0x0008);
    CHECK(((REG_SND1SWEEP >> 4) & 7) == 0);
    CHECK(((REG_SND1CNT >> 6) & 3) == 2); // default duty: 50%
}

static void noise_picks_a_close_rate(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_NOISE);
    u32 r = REG_SND4FREQ & 7, s = (REG_SND4FREQ >> 4) & 15; // readable on the noise channel
    CHECK(serval_psg_rate(PSG_NOISE) == (REG_SND4FREQ & 0xFF));
    u32 hz = r ? (524288u / r) >> (s + 1) : 1048576u >> (s + 1);
    CHECK(hz == 4096);
    CHECK(VOLUME(REG_SND4CNT) == 15);
}

static void noise_rate_is_the_closest_of_all(void) {
    // Against an exhaustive search of every divider r and shift s.
    static const u16 wanted[] = {4, 100, 800, 1500, 2000, 4000, 9000, 30000, 65535};
    for (u32 k = 0; k < sizeof wanted / sizeof wanted[0]; k++) {
        PsgSound s = {.channel = PSG_NOISE, .frequency = wanted[k], .frames = 2};
        serval_psg_play_sound(&s);
        u32 rate = serval_psg_rate(PSG_NOISE);
        u32 best_error = 0xFFFFFFFF;
        for (u32 sh = 0; sh < 14; sh++) {
            for (u32 r = 0; r < 8; r++) {
                u32 f = r ? (524288u / r) >> (sh + 1) : 1048576u >> (sh + 1);
                u32 e = f > wanted[k] ? f - wanted[k] : wanted[k] - f;
                if (e < best_error)
                    best_error = e;
            }
        }
        u32 r = rate & 7, sh = rate >> 4;
        u32 f = r ? (524288u / r) >> (sh + 1) : 1048576u >> (sh + 1);
        CHECK((f > wanted[k] ? f - wanted[k] : wanted[k] - f) == best_error);
    }
    psg_stop_all();
}

static void every_duty_is_selectable(void) {
    // The register's duty field is 0-3: 12.5%, 25%, 50%, 75%.
    static const u8 duties[] = {PSG_DUTY_12, PSG_DUTY_25, PSG_DUTY_50, PSG_DUTY_75};
    for (u32 k = 0; k < 4; k++) {
        PsgSound s = {.channel = PSG_SQUARE2, .frequency = 440, .frames = 2, .duty = duties[k]};
        serval_psg_play_sound(&s);
        CHECK(((REG_SND2CNT >> 6) & 3) == k);
        psg_stop_all();
    }
    PsgSound s = {.channel = PSG_SQUARE2, .frequency = 440, .frames = 2};
    serval_psg_play_sound(&s);
    CHECK(((REG_SND2CNT >> 6) & 3) == 2); // left out: 50%
    psg_stop_all();
}

static void fade_in_without_volume_starts_silent(void) {
    PsgSound s = {.channel = PSG_SQUARE2, .frequency = 440, .frames = 2, .fade = 3};
    serval_psg_play_sound(&s);
    CHECK(VOLUME(REG_SND2CNT) == 0 && (REG_SND2CNT & (1 << 11)) && ((REG_SND2CNT >> 8) & 7) == 3);
    s.volume = 5; // an explicit start volume is kept
    serval_psg_play_sound(&s);
    CHECK(VOLUME(REG_SND2CNT) == 5);
    psg_stop_all();
}

// Sounds with mistakes, each reported once (debug builds) and played safely.
static const u16 two_notes[] = {440, 880};
static const PsgSound bad_duty = {.channel = PSG_SQUARE2, .frequency = 440, .frames = 2, .duty = 9};
static const PsgSound big_fade = {
    .channel = PSG_SQUARE2, .frequency = 440, .frames = 2, .fade = -20};
static const PsgSound noise_slide = {
    .channel = PSG_NOISE, .frequency = 440, .frames = 2, .slide = 2};
static const PsgSound big_slide = {
    .channel = PSG_SQUARE1, .frequency = 440, .frames = 2, .slide = 9, .slide_size = 12};
static const PsgSound endless_melody = {
    .channel = PSG_SQUARE2, .notes = two_notes, .note_count = 2}; // no .frames
static const PsgSound no_notes = {.channel = PSG_SQUARE2, .frames = 2, .note_count = 2};
static const u16 low_notes[] = {440, 0, 50};
static const PsgSound low_square = {.channel = PSG_SQUARE2, .frequency = 40, .frames = 2};
static const PsgSound low_note = {
    .channel = PSG_SQUARE1, .frames = 2, .notes = low_notes, .note_count = 3};
static const PsgSound low_noise = {.channel = PSG_NOISE, .frequency = 40, .frames = 2};
enum {
    BAD_DUTY,
    BIG_FADE,
    NOISE_SLIDE,
    BIG_SLIDE,
    ENDLESS_MELODY,
    NO_NOTES,
    NULL_SOUND,
    LOW_SQUARE,
    LOW_NOTE,
    LOW_NOISE,
    BAD_COUNT
};
static const PsgSound* const bad_sounds[BAD_COUNT] = {
    &bad_duty, &big_fade, &noise_slide, &big_slide, &endless_melody,
    &no_notes, NULL,      &low_square,  &low_note,  &low_noise};

// Plays a sound twice; returns how many warnings that reported.
static u32 play_twice(u16 id) {
    u32 before = debug_warning_count();
    psg_play(id);
    psg_play(id);
    return debug_warning_count() - before;
}

#ifdef SERVAL_DEBUG
#define REPORTED_ONCE(n) CHECK((n) == 1)
#else
#define REPORTED_ONCE(n) CHECK((n) == 0)
#endif

static void misused_fields_are_reported_and_clamped(void) {
    psg_table_set(bad_sounds, BAD_COUNT);
    REPORTED_ONCE(play_twice(BAD_DUTY));
    CHECK(((REG_SND2CNT >> 6) & 3) == ((9 - 1) & 3)); // masked
    REPORTED_ONCE(play_twice(BIG_FADE));
    CHECK(((REG_SND2CNT >> 8) & 7) == 7 && !(REG_SND2CNT & (1 << 11))); // slowest fade out
    REPORTED_ONCE(play_twice(NOISE_SLIDE));
    CHECK(VOLUME(REG_SND4CNT) == 15); // still plays, without the slide
    REPORTED_ONCE(play_twice(BIG_SLIDE));
    CHECK(REG_SND1SWEEP == (7 << 4 | 7)); // time 7, up, shift 7
    // Square channels play 64 Hz and up: lower tones play at 64 Hz.
    REPORTED_ONCE(play_twice(LOW_SQUARE));
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 64);
    psg_table_set(bad_sounds, BAD_COUNT); // reportable again, for a melody's note
    REPORTED_ONCE(play_twice(LOW_NOTE));
    CHECK(play_twice(LOW_NOISE) == 0); // the noise channel goes lower
    psg_stop_all();
}

static void unplayable_sounds_are_reported_and_skipped(void) {
    psg_table_set(bad_sounds, BAD_COUNT);
    REPORTED_ONCE(play_twice(ENDLESS_MELODY)); // would hold its first note forever
    CHECK(VOLUME(REG_SND2CNT) == 0);
    REPORTED_ONCE(play_twice(NO_NOTES));
    CHECK(VOLUME(REG_SND2CNT) == 0);
    REPORTED_ONCE(play_twice(NULL_SOUND));
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
}

static void bad_ids_are_ignored_and_reported(void) {
    psg_table_set(sounds, SOUND_COUNT);
    u32 warnings = debug_warning_count();
    psg_play(SOUND_COUNT);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    psg_stop_all();
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
}

// --- Priorities ------------------------------------------------------------

enum { PRI_LOW, PRI_HIGH, PRI_HIGH_FADE, PRI_SAME, PRI_COUNT };
static const PsgSound low = {.channel = PSG_SQUARE2, .frequency = 300, .frames = 20};
static const PsgSound high = {.channel = PSG_SQUARE2, .frequency = 600, .frames = 4, .priority = 2};
static const PsgSound high_fade = { // no .frames: holds its channel until silent
    .channel = PSG_SQUARE2,
    .frequency = 700,
    .volume = 2,
    .fade = -1,
    .priority = 2};
static const PsgSound same = {.channel = PSG_SQUARE2, .frequency = 900, .frames = 4, .priority = 2};
static const PsgSound* const priority_sounds[PRI_COUNT] = {&low, &high, &high_fade, &same};

static void higher_priority_sounds_keep_their_channel(void) {
    psg_table_set(priority_sounds, PRI_COUNT);
    psg_play(PRI_HIGH);
    psg_play(PRI_LOW); // lower: ignored
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 600);
    psg_play(PRI_SAME); // equal: replaces
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 900);
    frames(4); // ended
    CHECK(VOLUME(REG_SND2CNT) == 0);
    psg_play(PRI_LOW); // nothing in the way now
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 300);
    psg_play(PRI_HIGH); // higher: replaces
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 600);
    psg_stop_all();
}

static void fading_sounds_hold_their_channel_until_silent(void) {
    psg_table_set(priority_sounds, PRI_COUNT);
    psg_play(PRI_HIGH_FADE); // volume 2, a step each 1/64 s: silent after ~2 frames
    psg_play(PRI_LOW);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 700);
    frames(4);
    psg_play(PRI_LOW);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 300);
    psg_stop_all();
}

// --- Pitch slides past the top -----------------------------------------------

enum { SLIDE_CUT, SLIDE_SAFE, SLIDE_DOWN, SLIDE_CUT_AT_ONCE, SLIDE_COUNT };
// 330 Hz (rate 1651) in steps of 1/16 every 2/128 s: past 2047 after 3 steps,
// 6/128 s (~2.8 frames), long before its 10 frames end.
static const PsgSound slide_cut = {
    .frequency = 330, .slide = 2, .slide_size = 4, .frames = 10, .fade = 0};
// The same in steps of 1/64: ~13 frames to the top, longer than the sound.
static const PsgSound slide_safe = {.frequency = 330, .slide = 2, .slide_size = 6, .frames = 10};
static const PsgSound slide_down = {.frequency = 2000, .slide = -1, .slide_size = 1, .frames = 30};
static const PsgSound slide_cut_at_once = {.frequency = 330, .slide = 2, .slide_size = 1};
static const PsgSound* const slide_sounds[SLIDE_COUNT] = {&slide_cut, &slide_safe, &slide_down,
                                                          &slide_cut_at_once};

#define SQUARE1_ON (REG_SNDSTAT & 1)

static void slides_past_the_top_are_reported(void) {
    psg_table_set(slide_sounds, SLIDE_COUNT);
    u32 before = debug_warning_count();
    psg_play(SLIDE_SAFE);
    psg_play(SLIDE_DOWN);
    CHECK(debug_warning_count() == before);
    REPORTED_ONCE(play_twice(SLIDE_CUT));
    psg_table_set(slide_sounds, SLIDE_COUNT);
    REPORTED_ONCE(play_twice(SLIDE_CUT_AT_ONCE)); // held until replaced: any cut-off counts
    psg_stop_all();
}

static void slides_cut_off_where_the_warning_says(void) {
    // The hardware (mGBA) agrees with the warning's model.
    psg_table_set(slide_sounds, SLIDE_COUNT);
    psg_play(SLIDE_CUT);
    frames(1);
    CHECK(SQUARE1_ON);
    frames(3);
    CHECK(!SQUARE1_ON); // silenced at ~2.8 frames
    psg_play(SLIDE_SAFE);
    frames(9);
    CHECK(SQUARE1_ON); // still sounding in its last frame
    psg_play(SLIDE_CUT_AT_ONCE);
    frames(1);
    CHECK(!SQUARE1_ON);
    psg_stop_all();
}

// --- Music -------------------------------------------------------------------

// 3583 ticks a minute: one tick per frame.
#define TICK_PER_FRAME .tempo = 3583, .ticks_per_beat = 1

static const PsgNote lead[] = {{PSG_C5, 2}, {PSG_REST, 1}, {PSG_E5, 3}};
static const PsgNote bass[] = {{PSG_C3, 6}};
static const PsgNote drums[] = {{PSG_C3, 2}, {PSG_C8, 2}, {PSG_C3, 2}};
static const PsgTrack song_tracks[] = {
    {.channel = PSG_SQUARE2, .notes = lead, .note_count = 3, .duty = PSG_DUTY_25, .volume = 10},
    {.channel = PSG_SQUARE1, .notes = bass, .note_count = 1},
    {.channel = PSG_NOISE, .notes = drums, .note_count = 3, .fade = -1},
};
static const PsgSong test_song = {TICK_PER_FRAME, .tracks = song_tracks, .track_count = 3};

#define SQUARE_RATE(note) serval_psg_square_rates[note]

static void music_plays_notes_on_their_frames(void) {
    psg_music_play(&test_song);
    CHECK(psg_music_playing());
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5));
    CHECK(VOLUME(REG_SND2CNT) == 10 && ((REG_SND2CNT >> 6) & 3) == 1); // volume 10, 25%
    CHECK(serval_psg_rate(PSG_SQUARE1) == SQUARE_RATE(PSG_C3));
    CHECK(REG_SND1SWEEP == 0x0008); // no slide
    CHECK((REG_SND4FREQ & 0xFF) == serval_psg_noise_rates[PSG_C3]);
    CHECK(VOLUME(REG_SND4CNT) == 15 && ((REG_SND4CNT >> 8) & 7) == 1); // fade -1
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5) && VOLUME(REG_SND2CNT) == 10);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);                                // the rest
    CHECK((REG_SND4FREQ & 0xFF) == serval_psg_noise_rates[PSG_C8]); // second drum
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5) && VOLUME(REG_SND2CNT) == 10);
    frames(2);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5));
    frames(1); // loops: all three tracks are 6 ticks long
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5));
    CHECK((REG_SND4FREQ & 0xFF) == serval_psg_noise_rates[PSG_C3]);
    psg_music_stop();
    CHECK(!psg_music_playing());
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
    frames(3); // stays stopped
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
}

enum { SFX_BLIP, SFX_LOW_PRIORITY, SFX_HIGH_PRIORITY, SFX_COUNT };
static const PsgSound blip = {.channel = PSG_SQUARE2, .frequency = 1000, .frames = 2};
static const PsgSound* const sfx_sounds[SFX_COUNT] = {&blip, &low, &high};

static void sounds_take_over_and_the_music_comes_back(void) {
    // Melody on square 2 with held notes: a sound effect plays over it, and
    // the note the music is on by then comes back as soon as it ends.
    psg_table_set(sfx_sounds, SFX_COUNT);
    psg_music_play(&test_song);
    frames(1);
    psg_play(SFX_BLIP);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 1000);
    frames(1); // the music moves to its rest under the sound
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 1000);
    CHECK(VOLUME(REG_SND2CNT) == 15);
    CHECK(serval_psg_rate(PSG_SQUARE1) == SQUARE_RATE(PSG_C3)); // other channels play on
    frames(1); // the sound ends as the music reaches E5
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5) && VOLUME(REG_SND2CNT) == 10);
    // Ending in the middle of a held note brings that note back at once.
    psg_play(SFX_BLIP);
    frames(2);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5) && VOLUME(REG_SND2CNT) == 10);
    psg_stop_all();
    CHECK(!psg_music_playing());
}

static void fading_tracks_come_back_with_their_next_note(void) {
    // The drums fade out: after a sound on the noise channel, they come back
    // with their next note, not in the middle of one.
    static const PsgSound hiss = {.channel = PSG_NOISE, .frequency = 30000, .frames = 1};
    psg_music_play(&test_song);
    serval_psg_play_sound(&hiss);
    frames(1); // hiss ends; the first drum (2 ticks) is half over
    CHECK(VOLUME(REG_SND4CNT) == 0);
    frames(1);
    CHECK((REG_SND4FREQ & 0xFF) == serval_psg_noise_rates[PSG_C8] && VOLUME(REG_SND4CNT) == 15);
    psg_stop_all();
}

static void music_priority_keeps_lower_sounds_out(void) {
    static const PsgSong important = {TICK_PER_FRAME, .priority = 1, .tracks = song_tracks,
                                      .track_count = 3};
    psg_table_set(sfx_sounds, SFX_COUNT);
    psg_music_play(&important);
    psg_play(SFX_LOW_PRIORITY); // priority 0 < 1: the music keeps square 2
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5));
    psg_play(SFX_HIGH_PRIORITY); // priority 2
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 600);
    psg_music_stop(); // the sound plays on
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 600 && VOLUME(REG_SND2CNT) == 15);
    psg_play(SFX_LOW_PRIORITY); // still below the sound playing
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 600);
    frames(4); // the sound ends; no music comes back
    CHECK(VOLUME(REG_SND2CNT) == 0);
    psg_stop_all();
}

static void songs_that_dont_loop_stop_playing(void) {
    static const PsgNote notes[] = {{PSG_G4, 1}, {PSG_C5, 2}};
    static const PsgTrack track = {.notes = notes, .note_count = 2, .loop = PSG_NO_LOOP};
    static const PsgSong jingle = {TICK_PER_FRAME, .tracks = &track, .track_count = 1};
    psg_music_play(&jingle);
    frames(2);
    CHECK(psg_music_playing() && serval_psg_rate(PSG_SQUARE1) == SQUARE_RATE(PSG_C5));
    frames(1);
    CHECK(!psg_music_playing() && VOLUME(REG_SND1CNT) == 0);
}

static void music_volume_scales_the_tracks(void) {
    psg_music_set_volume(5);
    psg_music_play(&test_song);
    CHECK(VOLUME(REG_SND2CNT) == 3); // 10 * 5 / 15
    CHECK(VOLUME(REG_SND1CNT) == 5); // 15 * 5 / 15
    psg_music_set_volume(0);         // from the next note
    frames(6);                       // loop
    CHECK(VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND1CNT) == 0);
    psg_music_set_volume(15);
    frames(6);
    CHECK(VOLUME(REG_SND2CNT) == 10);
    psg_stop_all();
}

static void changing_the_sound_table_keeps_the_music(void) {
    psg_music_play(&test_song);
    psg_table_set(sfx_sounds, SFX_COUNT);
    CHECK(psg_music_playing());
    psg_play(SFX_BLIP);
    psg_table_set(sounds, SOUND_COUNT); // stops the sound: the music comes back
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5) && VOLUME(REG_SND2CNT) == 10);
    psg_stop_all();
}

static void pause_holds_the_music_where_it_is(void) {
    psg_music_pause(); // no song: nothing happens
    CHECK(!psg_music_paused() && !psg_music_playing());
    psg_music_play(&test_song);
    frames(1); // C5 (2 ticks) half over, the bass held, the first drum fading
    psg_music_pause();
    CHECK(psg_music_paused() && psg_music_playing());
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
    frames(10); // time stands still, silently
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5));
    psg_music_pause(); // already paused: nothing happens
    psg_music_resume();
    CHECK(!psg_music_paused() && psg_music_playing());
    // Held notes come back at once; the fading drum with its next note.
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5) && VOLUME(REG_SND2CNT) == 10);
    CHECK(serval_psg_rate(PSG_SQUARE1) == SQUARE_RATE(PSG_C3) && VOLUME(REG_SND1CNT) == 15);
    CHECK(VOLUME(REG_SND4CNT) == 0);
    frames(1); // the rest of C5's second tick: on to the rest and the second drum
    CHECK(VOLUME(REG_SND2CNT) == 0);
    CHECK((REG_SND4FREQ & 0xFF) == serval_psg_noise_rates[PSG_C8] && VOLUME(REG_SND4CNT) == 15);
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5) && VOLUME(REG_SND2CNT) == 10);
    // Resuming music that isn't paused does nothing (no note restarts).
    serval_psg_quiet(PSG_SQUARE2);
    psg_music_resume();
    CHECK(VOLUME(REG_SND2CNT) == 0);
    psg_stop_all();
}

static void pause_keeps_the_phase_within_a_tick(void) {
    // Half a tick per frame: C5's 2 ticks take 4 frames. Paused after 1 for
    // 7 frames, the rest still comes after 3 more.
    static const PsgSong half_speed = {
        .tempo = 1791, .ticks_per_beat = 1, .tracks = song_tracks, .track_count = 3};
    psg_music_play(&half_speed);
    frames(1);
    psg_music_pause();
    frames(7);
    psg_music_resume();
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 10);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
    psg_stop_all();
}

static void sounds_play_while_the_music_is_paused(void) {
    // Even below the song's priority; the paused music doesn't come back when
    // they end, and comes back on resume.
    static const PsgSong important = {TICK_PER_FRAME, .priority = 1, .tracks = song_tracks,
                                      .track_count = 3};
    psg_table_set(sfx_sounds, SFX_COUNT);
    psg_music_play(&important);
    psg_music_pause();
    psg_play(SFX_BLIP); // priority 0
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 1000 && VOLUME(REG_SND2CNT) == 15);
    frames(2); // the sound ends
    CHECK(VOLUME(REG_SND2CNT) == 0);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 1000);
    psg_play(SFX_BLIP);
    psg_music_resume(); // the music comes back where the sound isn't
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 1000);
    CHECK(serval_psg_rate(PSG_SQUARE1) == SQUARE_RATE(PSG_C3) && VOLUME(REG_SND1CNT) == 15);
    frames(2); // the sound ends as the music, on from where it paused, reaches its rest
    CHECK(VOLUME(REG_SND2CNT) == 0);
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5) && VOLUME(REG_SND2CNT) == 10);
    psg_play(SFX_LOW_PRIORITY); // the song's priority holds again
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5));
    // Playing a song, stopping, and psg_stop_all end a pause.
    psg_music_pause();
    psg_music_play(&test_song);
    CHECK(!psg_music_paused() && VOLUME(REG_SND2CNT) == 10);
    psg_music_pause();
    psg_stop_all();
    CHECK(!psg_music_paused() && !psg_music_playing());
    psg_music_play(&test_song);
    psg_music_pause();
    psg_music_stop();
    CHECK(!psg_music_paused());
}

static void tempo_changes_from_where_the_music_is(void) {
    // A tick per frame, then half that after a frame: C5's second tick takes
    // 2 frames, the rest 2, then E5.
    psg_music_play(&test_song);
    frames(1);
    psg_music_set_tempo(1791);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 10);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5));
    psg_music_set_tempo(0); // the song's own again: E5's 3 ticks take 3 frames
    frames(2);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5));
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_C5)); // looped
    // Playing a song starts it at its own tempo: C5 lasts 2 frames.
    psg_music_set_tempo(900);
    psg_music_play(&test_song);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 10);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
    // The tempo holds through a pause.
    psg_music_set_tempo(1791);
    psg_music_pause();
    frames(5);
    psg_music_resume();
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
    frames(1);
    CHECK(serval_psg_rate(PSG_SQUARE2) == SQUARE_RATE(PSG_E5));
    psg_stop_all();
    // No song: ignored, reported once.
    u32 before = debug_warning_count();
    psg_music_set_tempo(100);
    psg_music_set_tempo(100);
    REPORTED_ONCE(debug_warning_count() - before);
    CHECK(!psg_music_playing());
}

static void bad_songs_are_reported_and_nothing_plays(void) {
    psg_table_set(sounds, SOUND_COUNT); // makes song problems reportable again
    u32 before = debug_warning_count();
    psg_music_play(NULL);
    psg_music_play(NULL);
    REPORTED_ONCE(debug_warning_count() - before);
    CHECK(!psg_music_playing());
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
    before = debug_warning_count();
    psg_music_set_volume(40);
    psg_music_set_volume(40);
    REPORTED_ONCE(debug_warning_count() - before);
    psg_music_set_volume(15);
}

TEST_SUITE(
    audio_tests, "audio", {"init_turns_sound_on", init_turns_sound_on},
    {"play_programs_frequency_volume_and_envelope", play_programs_frequency_volume_and_envelope},
    {"sounds_stop_after_their_frames", sounds_stop_after_their_frames},
    {"melodies_step_through_notes_and_rests", melodies_step_through_notes_and_rests},
    {"slide_uses_the_sweep_unit", slide_uses_the_sweep_unit},
    {"noise_picks_a_close_rate", noise_picks_a_close_rate},
    {"noise_rate_is_the_closest_of_all", noise_rate_is_the_closest_of_all},
    {"every_duty_is_selectable", every_duty_is_selectable},
    {"fade_in_without_volume_starts_silent", fade_in_without_volume_starts_silent},
    {"misused_fields_are_reported_and_clamped", misused_fields_are_reported_and_clamped},
    {"unplayable_sounds_are_reported_and_skipped", unplayable_sounds_are_reported_and_skipped},
    {"bad_ids_are_ignored_and_reported", bad_ids_are_ignored_and_reported},
    {"higher_priority_sounds_keep_their_channel", higher_priority_sounds_keep_their_channel},
    {"fading_sounds_hold_their_channel_until_silent",
     fading_sounds_hold_their_channel_until_silent},
    {"slides_past_the_top_are_reported", slides_past_the_top_are_reported},
    {"slides_cut_off_where_the_warning_says", slides_cut_off_where_the_warning_says},
    {"music_plays_notes_on_their_frames", music_plays_notes_on_their_frames},
    {"sounds_take_over_and_the_music_comes_back", sounds_take_over_and_the_music_comes_back},
    {"fading_tracks_come_back_with_their_next_note", fading_tracks_come_back_with_their_next_note},
    {"music_priority_keeps_lower_sounds_out", music_priority_keeps_lower_sounds_out},
    {"songs_that_dont_loop_stop_playing", songs_that_dont_loop_stop_playing},
    {"music_volume_scales_the_tracks", music_volume_scales_the_tracks},
    {"changing_the_sound_table_keeps_the_music", changing_the_sound_table_keeps_the_music},
    {"pause_holds_the_music_where_it_is", pause_holds_the_music_where_it_is},
    {"pause_keeps_the_phase_within_a_tick", pause_keeps_the_phase_within_a_tick},
    {"sounds_play_while_the_music_is_paused", sounds_play_while_the_music_is_paused},
    {"tempo_changes_from_where_the_music_is", tempo_changes_from_where_the_music_is},
    {"bad_songs_are_reported_and_nothing_plays", bad_songs_are_reported_and_nothing_plays});
