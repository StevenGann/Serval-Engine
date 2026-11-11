// den: "SERVAL DEN", a virtual serval that lives in real time by the
// cartridge's clock: it grows from a kitten to an adult over days, gets
// hungry, bored, tired and dirty while the game is off, sleeps at night, and
// needs looking after.
//
// Demonstrates:
//   - The cartridge clock (rtc.h; clock.c): rtc_present() checked at boot,
//     the clock read once at boot and then at most once a second, the time
//     since the last save credited to the pet (the core steps it minute by
//     minute, so a pet left for a day is the pet that lived that day), a
//     clock gone backwards treated as no time passing (a rebase), a reset
//     clock answered by a game-clock offset kept in the save, and the hour
//     of day from rtc_time_from_seconds()
//   - A virtual pet's life as portable C, host-tested (pet.c, litter.c,
//     album.c, den_save.c; tests/den_tests.c): needs, sleep by the hour,
//     droppings, health, neglect (sad at 12 h, sick at 18 h, sicker at 24 h,
//     dead at 36 h without care), growing up (adolescent at 24 h, adult at
//     72 h)
//   - Art drawn at boot (art.c): a sandstone den with a grass nest and roots
//     on background 2, the savanna through its arched mouth in a palette bank
//     of its own, the sky on background 3 drifting by map_set_scroll(); the
//     serval at three sizes drawn by rule from a skeleton and a pose per
//     frame (the kitten by hand), its coat (shade, spot pattern, eye colour,
//     the rare black coat) a palette: the spots are three sets of colours
//     that a pattern shows or hides
//   - Time of day and light by palette writes: four skies, the den's rock
//     warmed at dawn and dusk and cooled at night, dimmed when the lights go
//     off; the sun and the moon as sprites behind the den
//     (SPRITE_BEHIND_PLAYFIELD) following the hour across the mouth, setting
//     behind the far hills
//   - Saves (den_save.c, save.h): the pet, the album and the time last seen,
//     written every 5 minutes of play, after every care action and at every
//     change of stage or mood
//   - PSG sounds (sound.c): a purr, crunches, the serval's chirp, birds by
//     day and crickets at night
//
// What to expect when booting the ROM:
//   - The Serval Engine splash. Without a cartridge clock (mGBA without its
//     override, a cartridge without the chip) the game stops on a dark
//     screen saying SERVAL DEN NEEDS A CARTRIDGE CLOCK and, under it, IN MGBA:
//     TOOLS > GAME OVERRIDES > REALTIME CLOCK, and nothing else, for good.
//   - With a clock, SERVAL DEN in gold on black for two seconds or so (the
//     pictures being drawn, and the time away lived), then the den fades in:
//     warm sandstone walls in layers, roots hanging from the roof (one holds
//     a jar of glowing fireflies at the left), a sandy floor with pebbles
//     and loose grass, a woven grass nest at the left, and through the
//     arched mouth at the right the savanna:
//     far hills, a flat-topped acacia, tall grass and the sky, its clouds
//     drifting. At the top, the serval's name (SPOTS) and the clock's time.
//     At the foot, the keys, two lines by turns.
//   - The sky follows the clock: dawn 5-7 (violet to peach, the sun rising
//     orange at the left), day 8-17 (blue, the sun crossing high), dusk
//     18-20 (blue to orange, the sun setting at the right behind the hills),
//     night 21-4 (dark blue, stars, the moon); the den warms at dawn and dusk
//     and cools at night.
//   - A new den has a kitten (16x16, big ears, big eyes) with a coat of its
//     own. It wanders the floor, stops, blinks, flicks its tail. It sleeps in
//     the nest at night (21-7), curled, with rising Zs. It sits with its ears
//     down when sad (12 h without care), lies shivering and tinted blue when
//     sick (18 h), more blue when sicker (24 h); a dead pet is a little stone
//     with flowers. Droppings appear on the floor every few hours awake, with
//     a whiff above them. It grows to a lanky adolescent at 24 h and a
//     long-legged adult at 72 h (both 32x32).
//   - Keys (until the care menu): A pets it (hearts, a purr); B feeds it a
//     meal (a bowl that empties as it eats, crunching; refused with a low
//     "no" when it's full); R gives medicine (a sparkle and a chime; "no"
//     when it isn't sick); SELECT cleans the droppings (a sweep and
//     sparkles); START puts the lights out or on (a click; the den goes dark
//     but the world outside the mouth doesn't, the jar goes dark). Caring
//     for a sleeping serval wakes it for an hour.
//   - Debug builds only: hold L, then R, and press SELECT: an hour passes (TIME
//     WARP +1H at the top). Release builds have no warp.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into the portable core (pet.c, litter.c, album.c, den_save.c), clock.c
// (the game's use of the clock), art.c (the pictures, drawn at boot), den.c
// (the den on screen) and sound.c. main.c boots and runs the den a frame at
// a time.

#include "album.h"
#include "art.h"
#include "clock.h"
#include "den.h"
#include "den_save.h"
#include "sound.h"

#include <serval/serval.h>

#define AUTOSAVE_FRAMES (5 * 60 * 60) // every 5 minutes of play

static DenSave save SERVAL_EWRAM_BSS;
static u32 frames_since_save;
static int save_failed_timer;

// The no-clock screen: the owner's rule is that the game is nothing without
// its clock, so it says so and how to give it one, and stops.
static void no_clock(void) {
    screen_set_backdrop(COLOR_RGB(20, 14, 12));
    text_set_color(COLOR_RGB(248, 236, 208), COLOR_RGB(0, 0, 0));
    text_clear();
    text_print_centered(7, "SERVAL DEN NEEDS");
    text_print_centered(8, "A CARTRIDGE CLOCK");
    text_print_centered(11, "IN MGBA:");
    text_print_centered(12, "TOOLS > GAME OVERRIDES >");
    text_print_centered(13, "REALTIME CLOCK");
    for (;;) {
        frame_begin();
        frame_end();
    }
}

static void store(u32 now) {
    save.last_seen = now;
    save.offset = clock_offset();
    save.album_count = album_export(save.album);
    if (!den_save_store(&save)) {
        save_failed_timer = 180;
        text_set_style(2);
        text_print_centered(17, "SAVE FAILED");
        text_set_style(TEXT_NORMAL);
    }
    frames_since_save = 0;
}

// What counts as a life event for saving: the stage, the mood, death and the
// litter coming due.
typedef struct {
    u8 stage, mood;
    bool dead, litter;
} Life;

static Life life_of(const Pet* p) {
    return (Life){(u8)pet_stage(p, p->last_tick), (u8)pet_mood(p, p->last_tick), p->dead,
                  p->litter_due};
}

static bool life_changed(Life a, Life b) {
    return a.stage != b.stage || a.mood != b.mood || a.dead != b.dead || a.litter != b.litter;
}

// The keys at the foot, two lines by turns (the care menu replaces them).
static void draw_keys(void) {
    static int shown = -1;
    int line = (int)(frame_count() / 240 % 2);
    if (line == shown)
        return;
    shown = line;
    text_print_centered(19, line ? "SELECT CLEAN   START LIGHTS" : "A PET   B FEED   R MEDICINE");
}

int main(void) {
    serval_init();
    serval_splash();
    if (!rtc_present())
        no_clock();
    // The title while the pictures are drawn and the time away is lived
    // (about two seconds in a release build, more after a long absence).
    text_set_color(COLOR_RGB(232, 176, 72), COLOR_RGB(0, 0, 0));
    text_print_centered(9, "SERVAL DEN");
    frame_begin();
    frame_end();
    art_build();
    sound_init();

    u32 now;
    if (den_save_load(&save)) {
        album_set(save.album, save.album_count);
        const Pet* p = &save.pet;
        u32 age = p->last_tick > p->born ? p->last_tick - p->born : 0;
        ClockCheck check = clock_boot(save.last_seen, age, save.offset, &now);
        if (check == CLOCK_OK || check == CLOCK_WAS_RESET)
            pet_advance(&save.pet, now, clock_hour); // the time away, minute by minute
        else
            clock_rebase(&save.pet, now); // backwards or a jump: no time passed
    } else {
        // A new den: the first kitten (naming comes with the care menu).
        now = clock_start();
        random_seed(now ^ random_entropy());
        Coat coat = litter_visitor_coat();
        save = (DenSave){0};
        pet_new(&save.pet, "SPOTS", coat, 1, now);
        pet_advance(&save.pet, now, clock_hour);
    }
    random_seed(now ^ 0x5E5A1u);
    store(now);

    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    den_enter();
    Pet* pet = &save.pet;
    for (int b = SCREEN_BRIGHTNESS_MIN; b <= 0; b++) {
        frame_begin();
        screen_set_brightness(b);
        den_update(pet, clock_now());
        den_draw(pet, clock_now());
        frame_end();
    }

#ifdef SERVAL_DEBUG
    int warp_timer = 0;
#endif
    for (;;) {
        frame_begin();
        now = clock_now();
        Life before = life_of(pet);
        bool cared = false;
        if (button_pressed(BUTTON_A))
            cared = den_care(pet, CARE_PET, now) == CARE_DONE;
        else if (button_pressed(BUTTON_B))
            cared = den_care(pet, CARE_MEAL, now) == CARE_DONE;
        else if (button_pressed(BUTTON_R) && !button_down(BUTTON_L))
            cared = den_care(pet, CARE_MEDICINE, now) == CARE_DONE;
        else if (button_pressed(BUTTON_SELECT) && !button_down(BUTTON_L | BUTTON_R))
            cared = den_care(pet, CARE_CLEAN, now) == CARE_DONE;
        else if (button_pressed(BUTTON_START))
            cared = den_care(pet, CARE_LIGHTS, now) == CARE_DONE;
#ifdef SERVAL_DEBUG
        if (clock_debug_warp()) {
            warp_timer = 90;
            text_print_centered(2, "TIME WARP +1H");
            now = clock_now();
        }
        if (warp_timer > 0 && --warp_timer == 0)
            text_clear_area(0, 2, 30, 1);
#endif
        den_update(pet, now);
        frames_since_save++;
        if (cared || life_changed(before, life_of(pet)) || frames_since_save >= AUTOSAVE_FRAMES)
            store(now);
        if (save_failed_timer > 0 && --save_failed_timer == 0)
            text_clear_area(0, 17, 30, 1);
        sound_ambient(art_time_of_day(clock_hour(now)));
        draw_keys();
        den_draw(pet, now);
        frame_end();
    }
}
