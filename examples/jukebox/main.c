// jukebox: tracker music and sampled sound effects.
//
// Demonstrates:
//   - A sound bank (audio_bank_set): two tracker modules and four WAV samples
//     built into one bank by mmutil at build time (serval_add_soundbank() in
//     examples/CMakeLists.txt), with the MOD_* and SFX_* numbers of the
//     header it generates, jukebox_bank.h. The modules and samples are
//     original, composed and synthesized by tools/make-example-audio.py.
//   - Tracker music: music_play() looped, music_stop(), music_pause() and
//     music_resume(), music_set_volume() and music_set_speed(), and
//     music_playing() and music_paused() for the display
//   - Sampled effects: sfx_play(), and sfx_play_ex() with a volume, a pan,
//     a pitch and a priority; a looped sample held with its handle and
//     stopped with sfx_stop(); sfx_playing() counting the effects playing;
//     sfx_set_volume() for all of them
//   - The PSG playing beside the mixer, at its full volume: a short square
//     wave blip for each move of the cursor
//   - frame_cpu_permille(), which counts the mixer's time
//
// What to expect when booting the ROM:
//   - Black screen, white text: "SERVAL JUKEBOX" on top, then four rows,
//     MUSIC, VOLUME, SPEED and EFFECTS, with a cursor (">") on MUSIC, and the
//     buttons below. The music starts at once: THEME, an upbeat chiptune in
//     A minor at 132 BPM (drums, a bass, a lead and chord arpeggios), which
//     loops after about 51 seconds (past its intro).
//   - Up and down move the cursor (a short PSG blip each time); left and
//     right change the row's value (held, they repeat):
//     - MUSIC: THEME, CALM (a slow, mellow piece in D major at 90 BPM: a
//       sine bass, a triangle melody, a bell arpeggio and a soft pad) or OFF.
//       A new module starts from its beginning, at 100% speed.
//     - VOLUME: the music's volume, 0-255 in steps of 15 (the bar shows it).
//     - SPEED: the music's tempo, 50-200% in steps of 10, the pitch
//       unchanged.
//     - EFFECTS: the effects' volume, 0-255, playing effects too.
//   - START pauses the music (the state line says PAUSED, the music falls
//     silent mid-note) and resumes it exactly where it stopped.
//   - A: a coin (two rising tones). B: a laser zap, falling, panned to the
//     left and right speakers in turn, its pitch a little different each
//     time. L: an explosion, priority 2 (it takes a channel from the others
//     when all are busy). R, held: an engine's hum, looped until R is
//     released.
//   - The bottom line shows how many effects are playing and the CPU time a
//     frame takes, the mixer's included: about 3% with nothing playing, 15-20%
//     with the music and a few effects.
//   - In the web build the music and effects are silent (the web has no
//     player for them yet); the PSG blips play.
//   (In mGBA's default keyboard mapping, the D-pad is the arrow keys, A is X,
//   B is Z, L is A, R is S, START is Enter.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

#include "jukebox_bank.h"

enum { ROW_MUSIC, ROW_VOLUME, ROW_SPEED, ROW_EFFECTS, ROW_COUNT };
enum { TUNE_THEME, TUNE_CALM, TUNE_OFF, TUNE_COUNT };

static const char* const tune_names[TUNE_COUNT] = {"THEME", "CALM", "OFF"};
static const u16 tune_modules[] = {MOD_THEME, MOD_CALM};

#define FIRST_ROW 3 // the text row of ROW_MUSIC
#define HANDLES 16  // effects remembered for the count shown

// The PSG blip for the cursor.
static const PsgSound blip = {
    .channel = PSG_SQUARE1, .frequency = 1760, .frames = 3, .volume = 9, .duty = PSG_DUTY_25};
static const PsgSound* const psg_sounds[] = {&blip};

typedef struct {
    int row;
    int tune;
    int volume; // 0-255
    int speed;  // percent
    int effects;
    bool laser_left;
    Sfx hum;
    Sfx handles[HANDLES]; // the latest effects, to count those still playing
    u32 next_handle;
} Jukebox;

static void remember(Jukebox* j, Sfx sfx) {
    j->handles[j->next_handle++ % HANDLES] = sfx;
}

static int effects_playing(const Jukebox* j) {
    int n = 0;
    for (int i = 0; i < HANDLES; i++)
        n += sfx_playing(j->handles[i]);
    return n;
}

static void play_tune(Jukebox* j) {
    if (j->tune == TUNE_OFF) {
        music_stop();
        return;
    }
    music_play(tune_modules[j->tune], true);
    j->speed = 100; // music_play() starts every module at its own tempo
}

static int step(int value, int by, int lo, int hi) {
    return int_clamp(value + by, lo, hi);
}

// Left (-1) or right (+1) on the selected row.
static void change(Jukebox* j, int by) {
    switch (j->row) {
    case ROW_MUSIC:
        j->tune = (j->tune + by + TUNE_COUNT) % TUNE_COUNT;
        play_tune(j);
        break;
    case ROW_VOLUME:
        j->volume = step(j->volume, by * 15, 0, 255);
        music_set_volume((u8)j->volume);
        break;
    case ROW_SPEED:
        if (music_playing()) {
            j->speed = step(j->speed, by * 10, 50, 200);
            music_set_speed((u16)j->speed);
        }
        break;
    case ROW_EFFECTS:
        j->effects = step(j->effects, by * 15, 0, 255);
        sfx_set_volume((u8)j->effects);
        break;
    }
}

static void effects(Jukebox* j) {
    if (button_pressed(BUTTON_A))
        remember(j, sfx_play(SFX_COIN));
    if (button_pressed(BUTTON_B)) {
        // Panned left and right in turn, 0.8 to 1.25 times its pitch.
        FIXED pitch = random_range(FX_ONE * 4 / 5, FX_ONE * 5 / 4);
        remember(j, sfx_play_ex(SFX_LASER, 220, j->laser_left ? -96 : 96, pitch, 1));
        j->laser_left = !j->laser_left;
    }
    if (button_pressed(BUTTON_L)) {
        FIXED pitch = random_range(FX_ONE * 9 / 10, FX_ONE * 11 / 10);
        remember(j, sfx_play_ex(SFX_BOOM, 255, 0, pitch, 2));
    }
    if (button_pressed(BUTTON_R)) {
        j->hum = sfx_play_ex(SFX_ENGINE, 200, 0, FX_ONE, 1);
        remember(j, j->hum);
    }
    if (!button_down(BUTTON_R) && j->hum != SFX_NONE) {
        sfx_stop(j->hum);
        j->hum = SFX_NONE;
    }
}

// A bar of 10 cells for 0-255.
static const char* bar(int value) {
    static char cells[11];
    int full = (value * 10 + 127) / 255;
    for (int i = 0; i < 10; i++)
        cells[i] = i < full ? '#' : '.';
    cells[10] = '\0';
    return cells;
}

// Redraws the menu when it changed, and the bottom line four times a
// second, so the text costs little of the CPU time shown.
static void draw(const Jukebox* j, bool all) {
    if (all) {
        for (int r = 0; r < ROW_COUNT; r++) {
            char cursor = r == j->row ? '>' : ' ';
            const char* line = "";
            switch (r) {
            case ROW_MUSIC:
                line = text_format("%c MUSIC   < %s >", cursor, tune_names[j->tune]);
                break;
            case ROW_VOLUME:
                line = text_format("%c VOLUME  %s %d", cursor, bar(j->volume), j->volume);
                break;
            case ROW_SPEED:
                line = text_format("%c SPEED   %d%%", cursor, j->speed);
                break;
            case ROW_EFFECTS:
                line = text_format("%c EFFECTS %s %d", cursor, bar(j->effects), j->effects);
                break;
            }
            text_print_line(1, FIRST_ROW + r * 2, line);
        }
    }
    if (all || frame_count() % 15 == 0) {
        const char* state = !music_playing() ? "STOPPED" : music_paused() ? "PAUSED" : "PLAYING";
        text_print_line(3, FIRST_ROW + ROW_COUNT * 2, text_format("MUSIC %s", state));
        u32 permille = frame_cpu_permille();
        text_print_line(1, 19,
                        text_format("EFFECTS PLAYING %d   CPU %d.%d%%", effects_playing(j),
                                    (int)(permille / 10), (int)(permille % 10)));
    }
}

int main(void) {
    serval_init();
    serval_splash();
    audio_bank_set(jukebox_bank);
    psg_table_set(psg_sounds, 1);
    random_seed(random_entropy());

    text_print_centered(1, "SERVAL JUKEBOX");
    text_print_centered(13, "UP/DOWN PICK  LEFT/RIGHT SET");
    text_print_centered(14, "START PAUSE / RESUME");
    text_print_centered(16, "A COIN  B LASER  L BOOM");
    text_print_centered(17, "R (HOLD) ENGINE HUM");

    Jukebox j = {.row = ROW_MUSIC, .tune = TUNE_THEME, .volume = 255, .speed = 100, .effects = 255};
    play_tune(&j);
    draw(&j, true);

    for (;;) {
        frame_begin();
        bool changed = false;
        if (button_repeat(BUTTON_UP | BUTTON_DOWN)) {
            int by = button_down(BUTTON_UP) ? -1 : 1;
            j.row = (j.row + by + ROW_COUNT) % ROW_COUNT;
            psg_play(0);
            changed = true;
        }
        if (button_repeat(BUTTON_LEFT)) {
            change(&j, -1);
            changed = true;
        } else if (button_repeat(BUTTON_RIGHT)) {
            change(&j, 1);
            changed = true;
        }
        if (button_pressed(BUTTON_START)) {
            if (music_paused())
                music_resume();
            else
                music_pause();
            changed = true;
        }
        effects(&j);
        draw(&j, changed);
        frame_end();
    }
}
