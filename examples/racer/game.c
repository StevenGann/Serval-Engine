// The game's flow: the title, the race (its countdown, pausing and its
// end), the results, and the fades between them.
//
// The title drives the road by itself, at an easy pace down the middle of
// the circuit, under the logo; START or A fades to a race. A race is the
// Race of track.c, one race_step() a frame with the buttons held, and what
// it reports as events comes to one place, events(), with the game's own
// (the countdown's numbers, pausing, a record): the messages, the sounds
// (sound.c) and the end of the race are made there. Everything else on
// screen follows the race's state by itself: the jeep's spin after a crash,
// the shake after a bump or a crash, the dust off the road, the time of day
// by the lap. START pauses: nothing steps, the same picture is drawn again
// every frame, and the music and the drone stop until it goes on. Each
// screen starts its own sound: the title its jingle once; a race the
// engine idling and the countdown's beeps, then the race tune from GO.
// When the race is over, FINISH or TIME UP drops in over the stopped
// picture with its fanfare or phrase, then rises to the top while the
// picture dims and the results come up under it; the records are taken
// (and saved, if one fell) as they do. A or START goes back to the title.
//
// Everything is loaded once at boot, under a black screen, so changing
// screens is a fade of the brightness and some text. Two more effects of
// the whole picture: a white flash (the brightness again) for a crash, GO
// and a new lap; and a dimming of the road and the sky behind the results
// and PAUSED (alpha blending with no colour from behind, so a layer shows
// at a fraction of its brightness; the text layer isn't blended, so the
// text stays bright).

#include "game.h"

#include "art.h"
#include "hud.h"
#include "objects.h"
#include "records.h"
#include "road.h"
#include "screens.h"
#include "sound.h"
#include "track.h"
#include "view.h"

#define FADE_STEP 2         // brightness per frame: 8 frames to or from black
#define ENDING_FRAMES 150   // FINISH or TIME UP over the stopped race, then the results
#define TITLE_SPEED FX(2)   // the title's cruise, track units a frame: 110 km/h
#define TITLE_START FX(160) // where it starts: ten segments past the start line
#define FLASH_STEP 2        // a flash's brightness fades this much a frame
#define FLASH_CRASH 12      // a crash's flash
#define FLASH_GO 8          // GO's, and a new lap's
#define DIM_MAX 9           // dimmed by 9/16 (to 7/16), a sixteenth a frame
#define WORD_RISE 2         // FINISH or TIME UP rises to the results' top, pixels a frame

// The game's own events, beside race_step()'s RACE_EV_* bits (the low ones).
#define EV_COUNT (1u << 16)  // a number of the countdown came up (3, 2, 1)
#define EV_PAUSE (1u << 17)  // the race paused
#define EV_RESUME (1u << 18) // ... and went on
#define EV_RECORD (1u << 19) // the results came up with a record fallen
#define EV_ERASED (1u << 20) // the records were erased on the title

enum { TITLE, RACE, PAUSED, ENDING, RESULTS };

static int state;
static Race race;
static RoadFrame road SERVAL_EWRAM_BSS;
static FIXED cruise;      // the title's distance along the circuit
static u32 since_go;      // frames raced since GO (not counting pauses)
static u32 screen_frames; // frames since the screen came up
static int shown_count;   // the countdown's number last shown (0 none)
static u32 races;         // races started since boot

static int brightness = SCREEN_BRIGHTNESS_MIN;
static int fade = 1; // 1 fading in, -1 fading out, 0 neither
static void (*after_fade)(void);
static int flash;       // a white flash's brightness, on top of the fade's
static int dim;         // the picture behind the results or PAUSED: 0 to DIM_MAX
static u32 fell;        // the records that fell, for the results
static bool results_up; // ... and the results text is up

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
    screen_set_brightness(brightness + flash);
    flash = int_max(flash - FLASH_STEP, 0);
}

// Dims the road and the sky (and, while paused, the sprites; behind the
// results only FINISH or TIME UP is a sprite, and it stays bright) toward
// DIM_MAX behind the results and PAUSED, and undims at once otherwise: each
// layer blends with what is behind it at a weight of 0, so it shows at
// (16 - dim) / 16 of its brightness. Blending waits while the brightness
// isn't 0 (a fade, a flash).
static void run_dim(void) {
    bool dimmed = state == PAUSED || state == RESULTS;
    dim = dimmed ? int_min(dim + 1, DIM_MAX) : 0;
    u32 top = LAYER_PLAYFIELD | LAYER_BACKGROUND | (state == PAUSED ? LAYER_SPRITES : 0);
    u32 bottom = LAYER_PLAYFIELD | LAYER_BACKGROUND | LAYER_BACKDROP;
    if (dim)
        screen_set_blend(top, bottom, (u32)(16 - dim), 0);
    else
        screen_set_blend(0, 0, 0, 0);
}

// The driving buttons held this frame, as race_step() takes them.
static u16 held_buttons(void) {
    static const u16 driving[] = {BUTTON_A, BUTTON_B, BUTTON_LEFT, BUTTON_RIGHT};
    u16 held = 0;
    for (u32 i = 0; i < sizeof driving / sizeof driving[0]; i++)
        if (button_down(driving[i]))
            held |= driving[i];
    return held;
}

// --- Events -----------------------------------------------------------------------------

static void show_ending(void) {
    state = ENDING;
    screen_frames = 0;
    hud_message_clear();
}

// Everything that happens, in one place: race_step()'s RACE_EV_* bits and
// the game's EV_* ones. The messages, the sounds and the end of the race
// are made here; the pictures follow the race by themselves (GO and the
// numbers from the countdown, the spin, the shake, the jolt, the dust,
// PAUSED, the results' marks). While racing, the drone follows the speed
// and the held events (a skid, off the road) sound while they last.
static void events(u32 ev) {
    if (ev & EV_COUNT)
        sound_play(SND_COUNT);
    if (ev & RACE_EV_GO) {
        flash = FLASH_GO;
        sound_play(SND_GO);
        sound_tune(TUNE_RACE);
    }
    if ((ev & RACE_EV_LAP) && !(ev & RACE_EV_FINISH)) {
        const char* lap =
            race.lap == RACE_LAPS - 1 ? "FINAL LAP" : text_format("LAP %d", race.lap + 1);
        hud_message(lap, text_format("+%u", race.last_bonus / 60));
        flash = FLASH_GO;
        sound_play(SND_LAP);
    } else if (ev & RACE_EV_CHECKPOINT) {
        hud_message(text_format("+%u", race.last_bonus / 60), NULL);
        sound_play(SND_CHECKPOINT);
    }
    if (ev & RACE_EV_BUMP)
        sound_play(SND_BUMP);
    if (ev & RACE_EV_CRASH) {
        flash = FLASH_CRASH;
        sound_play(SND_CRASH);
        sound_play(SND_CRASH_TONE);
    }
    if (ev & (EV_PAUSE | EV_RESUME))
        sound_pause(ev & EV_PAUSE);
    if (ev & EV_RECORD)
        sound_play(SND_RECORD);
    if (ev & EV_ERASED)
        sound_play(SND_ERASED);
    if (state == RACE)
        sound_drive(&race, ev);
    if (ev & (RACE_EV_FINISH | RACE_EV_TIME_UP)) {
        sound_drone_off();
        sound_tune(race.finished ? TUNE_FINISH : TUNE_TIME_UP);
        show_ending();
    }
}

// --- The title ------------------------------------------------------------------------

static void start_race(void);

static void show_title(void) {
    state = TITLE;
    screen_frames = 0;
    cruise = TITLE_START;
    view_reset(TIME_STAGE(TIME_SUNSET));
    hud_readout_off();
    title_show();
    sound_quiet();
    sound_tune(TUNE_TITLE);
}

static void title_frame(void) {
    u32 ev = 0;
    if (!fade && (button_pressed(BUTTON_START) || button_pressed(BUTTON_A))) {
        fade_to(start_race);
    } else if (!fade && button_down(BUTTON_L) && button_down(BUTTON_R) &&
               button_pressed(BUTTON_SELECT)) {
        // Hidden, for demos: it acts when SELECT is pressed while L and R
        // are held.
        records_erase();
        title_erased();
        ev |= EV_ERASED;
    }
    events(ev);
    // Down the middle of the road (lateral 0: the road's offsets are the
    // curves' alone, which road.c keeps clear of the screen's edges).
    cruise = track_around(&savanna, cruise + TITLE_SPEED);
    road_frame(&road, &savanna, cruise, 0);
    view_step(&road, TITLE_SPEED, TIME_STAGE(TIME_SUNSET));
    view_draw(&road, 0);
    title_draw(screen_frames++);
    jeep_draw_cruise(&road);
    objects_draw(&savanna, &road, cruise, NULL, 0, 0);
}

// --- The race ----------------------------------------------------------------------------

// Each race gets its own random numbers (where traffic left behind turns up
// again ahead): the count of races since boot, so traffic differs from race
// to race, and the same input from boot replays the same races.
static void start_race(void) {
    race_start(&race, &savanna, ++races);
    state = RACE;
    since_go = 0;
    shown_count = 0;
    view_reset(TIME_STAGE(TIME_AFTERNOON)); // every race starts in the afternoon
    hud_reset();
    dust_reset();
    sound_quiet();
}

// The results: FINISH or TIME UP rises to the top as the picture dims,
// then the text comes up under it (results_frame()).
static void show_results(void) {
    state = RESULTS;
    screen_frames = 0;
    hud_readout_off();
    text_clear();
    results_up = false;
    fell = records_take(&race);
    events(fell ? EV_RECORD : 0);
}

// The race's picture: the road, the HUD, the jeep, then everything in front
// of it, nearest first. `running`: a frame of racing (not paused, not over),
// so the sky and the time of day move on. Behind the results only the road
// and the sky stay, dimmed: the text must read, and the jeep, the scenery,
// traffic and the checkpoints' white checks would be under it.
static void draw_race(bool running) {
    road_frame(&road, &savanna, race.distance, race.lateral);
    if (state != PAUSED) {
        int lap = int_min(race.lap, TIMES_OF_DAY - 1); // lap 1 afternoon, 2 sunset, 3 dusk
        view_step(&road, running ? race.speed : 0, TIME_STAGE(lap));
    }
    int shake = state == RACE || state == PAUSED ? view_shake(&race) : 0;
    view_draw(&road, shake);
    if (state != RESULTS) {
        hud_draw(&race, since_go, state == PAUSED);
        jeep_draw_race(&race, &road, shake);
        dust_draw(&road, shake);
        objects_draw(&savanna, &road, race.distance, race.traffic, race.traffic_count, shake);
    }
    hud_readout_draw();
}

static void race_frame(void) {
    u32 ev = 0;
    if (!fade && button_pressed(BUTTON_START)) {
        state = state == PAUSED ? RACE : PAUSED;
        ev |= state == PAUSED ? EV_PAUSE : EV_RESUME;
    }
    if (button_pressed(BUTTON_SELECT))
        hud_readout_toggle();
    if (state == RACE) {
        ev |= race_step(&race, held_buttons());
        dust_step(&race, ev);
        if (!race.countdown)
            since_go++;
        int count = race.countdown ? (race.countdown + 59) / 60 : 0;
        if (count != shown_count) {
            shown_count = count;
            if (count)
                ev |= EV_COUNT;
        }
        hud_step();
    }
    events(ev);
    draw_race(state == RACE);
}

static void ending_frame(void) {
    hud_word_draw(race.finished, hud_word_drop(screen_frames));
    if (++screen_frames == ENDING_FRAMES)
        show_results();
    draw_race(false);
}

static void results_frame(void) {
    // The word rises from where it landed to the top; then the text.
    int y = int_max(hud_word_drop(ENDING_FRAMES) - (int)screen_frames * WORD_RISE, 0);
    hud_word_draw(race.finished, y);
    if (y == 0 && !results_up) {
        results_up = true;
        results_show(&race, fell);
    }
    if (results_up) {
        if (!fade && (button_pressed(BUTTON_A) || button_pressed(BUTTON_START))) {
            // Off with the text, and on from the dimmed picture's
            // brightness to black, without a jump.
            text_clear();
            brightness = -dim;
            fade_to(show_title);
        }
        results_draw(screen_frames);
    }
    screen_frames++;
    draw_race(false);
}

// --- The frame --------------------------------------------------------------------------

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    road_init();
    art_build();
    view_init();
    text_set_shadow(true); // text reads over the bright sky
    text_set_style_color(STYLE_LABEL, COLOR_RGB(255, 186, 96), COLOR_RGB(0, 0, 0));
    // Each text style builds its glyphs the first time it is used (tens of
    // thousands of cycles): use them all now, under the black, not in the
    // middle of a race.
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_set_style(STYLE_WARNING);
    text_set_style(STYLE_LABEL);
    text_set_style(TEXT_NORMAL);
    records_load();
    sound_init();
    show_title();
}

void game_frame(void) {
    run_fade();
    switch (state) {
    case TITLE:
        title_frame();
        break;
    case RACE:
    case PAUSED:
        race_frame();
        break;
    case ENDING:
        ending_frame();
        break;
    default:
        results_frame();
        break;
    }
    run_dim();
}
