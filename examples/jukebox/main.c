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
//   - Pictures in time with the music: music_position() (show.c) gives the
//     module's order, row and tick at the VBlank that shows the frame, so
//     the lights flare on the beat, the beat lamps step through the bar and
//     the record turns a turn a bar, at any SPEED, and stops when paused
//   - Sampled effects: sfx_play(), and sfx_play_ex() with a volume, a pan,
//     a pitch and a priority; a looped sample held with its handle and
//     stopped with sfx_stop(); sfx_playing() counting the effects playing;
//     sfx_set_volume() for all of them
//   - A script playing them: the effect pads and the pause button are
//     pads.lua, a Lua-subset script compiled at build time
//     (serval_add_script()), which names the bank's samples from
//     jukebox_bank.h and calls the same functions through the VM's SYS
//     calls; the menu is C, and reads the script's count of effects
//     playing and which way its laser went (vm_global)
//   - The PSG playing beside the mixer, at its full volume: a short square
//     wave blip for each move of the cursor
//   - Art drawn at boot (art.c): a cabinet on one fixed map layer, palette
//     animation (tileset_set_colors) for the pulse and the pads' flashes, a
//     backdrop gradient (raster_backdrop), a rotated sprite (the record),
//     per-draw palettes, screen_set_brightness() for the fade-in and the
//     boom's flash
//   - frame_cpu_permille(), which counts the mixer's time
//
// What to expect when booting the ROM:
//   - After the splash, the jukebox fades in on a purple-to-navy gradient:
//     a marquee across the top with a row of bulbs and "SERVAL JUKEBOX" in
//     gold letters; a turntable on the left, a black record with a red label
//     (a paw print) under a silver tonearm; the control panel on the right,
//     four rows, MUSIC (the tune's name in a dark display between < and >),
//     VOLUME and EFFECTS (meters of ten lamps, green to amber to red, and
//     the number), SPEED (a slider with a knob, ticks at 50/100/150/200%),
//     a gold quaver for the cursor on MUSIC; under them the music's state
//     (PLAYING in gold, PAUSED in pink, STOPPED in grey), four beat lamps
//     and the module's ORDER and ROW; four pads at the foot, gold A COIN,
//     cyan B LASER, orange L BOOM, green R HUM; the buttons and the
//     bottom line under them.
//   - The music starts at once: THEME, an upbeat chiptune in A minor at 132
//     BPM (drums, a bass, a lead and chord arpeggios), which loops after
//     about 51 seconds (past its intro). The record turns with it, a turn a
//     bar; on every beat the trim, the bulbs and the sky flare and a ripple
//     runs along the title, the beat lamp moves on (the bar's first in
//     orange, the others green) and the cursor nods; the bulbs chase a step
//     a row.
//   - Up and down move the cursor, which glides to its row (a short PSG
//     blip each time); left and right change the row's value (held, they
//     repeat):
//     - MUSIC: THEME, CALM (a slow, mellow piece in D major at 90 BPM: a
//       sine bass, a triangle melody, a bell arpeggio and a soft pad; its
//       record's label is teal) or OFF (the tonearm swings off the record,
//       which stops, its label grey, and the lights rest). A new module
//       starts from its beginning, at 100% speed.
//     - VOLUME: the music's volume, 0-255 in steps of 15.
//     - SPEED: the music's tempo, 50-200% in steps of 10, the pitch
//       unchanged; the record, the lights and the beat lamps follow it.
//     - EFFECTS: the effects' volume, 0-255, playing effects too.
//   - START pauses the music (PAUSED, the music falls silent mid-note, the
//     record stops and the bar's lamp blinks) and resumes it exactly where
//     it stopped.
//   - A: a coin (two rising tones); a coin flips up out of its pad. B: a
//     laser zap, falling, panned to the left and right speakers in turn,
//     its pitch a little different each time; a beam shoots to that side.
//     L: an explosion, priority 2 (it takes a channel from the others when
//     all are busy); sparks burst from its pad, the screen flashes white and
//     shakes. R, held: an engine's hum, looped until R is released; its pad
//     throbs and ripples rise from it. Each pad lights up as it plays.
//   - The bottom line shows how many effects are playing (the script counts
//     them) and the CPU time a frame takes, the mixer's and the script's
//     included: about 10% with nothing playing (the mixer about 3, the VM
//     running the script about 4, the pictures about 3), 25-30% with the
//     music and a few effects.
//   - The web build looks and sounds the same: the music, the effects and
//     the PSG blips (the browser starts the sound at the first click or key
//     press, so the page waits for one). Its CPU figure is the browser's
//     time, not the GBA's.
//   (In mGBA's default keyboard mapping, the D-pad is the arrow keys, A is X,
//   B is Z, L is A, R is S, START is Enter.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

#include "jukebox.h"
#include "jukebox_bank.h"
#include "pads_script.h"
#include "show.h"

static const char* const tune_names[TUNE_COUNT] = {"THEME", "CALM", "OFF"};
static const u16 tune_modules[] = {MOD_THEME, MOD_CALM};

// The PSG blip for the cursor.
static const PsgSound blip = {
    .channel = PSG_SQUARE1, .frequency = 1760, .frames = 3, .volume = 9, .duty = PSG_DUTY_25};
static const PsgSound* const psg_sounds[] = {&blip};

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

// Redraws the menu's text when it changed (show.c draws its meters and its
// cursor), and the bottom line four times a second, so the text costs
// little of the CPU time shown.
static void draw(const Jukebox* j, bool all) {
    static const char* const names[ROW_COUNT] = {"MUSIC", "VOLUME", "SPEED", "EFFECTS"};
    if (all) {
        for (int r = 0; r < ROW_COUNT; r++) {
            int y = MENU_ROW(r);
            text_set_style(r == j->row ? TEXT_HIGHLIGHT : TEXT_NORMAL);
            text_print(11, y, names[r]);
            text_set_style(TEXT_NORMAL);
            switch (r) {
            case ROW_MUSIC:
                text_print(17, y, "<");
                text_print_centered_in(18, 7, y, tune_names[j->tune]);
                text_print(25, y, ">");
                break;
            case ROW_VOLUME:
                text_print_line(25, y, text_format("%3d", j->volume));
                break;
            case ROW_SPEED:
                text_print_line(25, y, text_format("%3d%%", j->speed));
                break;
            case ROW_EFFECTS:
                text_print_line(25, y, text_format("%3d", j->effects));
                break;
            }
        }
    }
    if (all || frame_count() % 15 == 0) {
        u32 permille = frame_cpu_permille();
        text_set_style(STYLE_DIM);
        text_print_line(14, 19,
                        text_format("SFX %d  CPU %2d.%d%%", (int)vm_global(G_PLAYING),
                                    (int)(permille / 10), (int)(permille % 10)));
        text_set_style(TEXT_NORMAL);
    }
}

int main(void) {
    serval_init();
    serval_splash();
    audio_bank_set(jukebox_bank);
    psg_table_set(psg_sounds, 1);
    random_seed(random_entropy());

    show_start(); // the cabinet, faded in over the first frames
    text_set_style(STYLE_DIM);
    text_print_centered(18, "UP/DOWN PICK  LEFT/RIGHT SET");
    text_print(1, 19, "START PAUSE");
    text_set_style(TEXT_NORMAL);

    // The pads and the pause button: pads.lua's thread.
    ecs_reset();
    vm_load(pads_script, pads_script_size);
    vm_start(OBJ_PADS, VM_EV_ROOM_START);

    Jukebox j = {.row = ROW_MUSIC, .tune = TUNE_THEME, .volume = 255, .speed = 100, .effects = 255};
    play_tune(&j);
    draw(&j, true);

    for (;;) {
        frame_begin();
        vm_step(); // the pads: effects, and START pausing or resuming the music
        vm_events();
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
        draw(&j, changed);
        show_frame(&j); // the record, the lights, the meters, the pads
        frame_end();
    }
}
