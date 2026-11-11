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
//   - Art drawn by rule in C and run ahead of time (art_draw.c, by the host
//     program art_gen into art_data.c, const data in ROM, as servle's word
//     tables are generated): a sandstone den with a grass nest and roots
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
//   - Saves (den_save.c, save.h): the pet, the album, the games' best scores
//     (the den's records, kept from one generation to the next), the stage
//     last shown growing up and the time last seen, written every 5 minutes
//     of play, after every care action, at every change of stage or mood and
//     after every life event
//   - A mini-game inside the den (yarn.c, rules in yarn_rules.c, host-tested):
//     YARN BAT, the kitten's. The den's own pictures are the court: its rock
//     at both sides are the walls, the roof the ceiling, and the pet runs the
//     floor as the paw, batting the yarn ball up (breakout-style); the ball and
//     its wavering thread are sprites
//   - A second game with a scrolling world (climb.c, rules in climb_rules.c,
//     host-tested): ACACIA CLIMB, the adolescent's. The trunk is a wrapping
//     background layer and the sky another, both scrolled by the camera with
//     map_set_scroll(); the boughs and nests are sprites that sway, crack and
//     fall; the jump's arc is one function that the rules play and that the
//     test of the tree's reachability measures
//   - A third game played by ear (pounce.c, rules in pounce_rules.c,
//     host-tested): HIGH POUNCE, the adult's. Grass tufts as sprites that
//     shake where a mouse or a bird hides, a rustle in PSG noise whose volume
//     rises with the serval's nearness (the engine's sound is mono: an arrow
//     over the serval's head points to the rustle it last heard), a leap whose
//     arc and reach are constants of the rules
//   - PSG sounds (sound.c): a purr, crunches, the serval's chirp, birds by
//     day and crickets at night; short songs played once (PsgSong,
//     psg_music_play()) for growing up, the litter and the tombstone
//   - Life events (events.c, host-tested; scenes.c on screen): the time away
//     lived minute by minute at boot and summed up, growing up shown once per
//     stage (the stage last shown is saved), the litter, the tombstone, the
//     album; a clock that jumps more than 30 days (at boot, or on a web page
//     while it runs) is asked about
//   - Several coats on the screen at once: the serval's pictures drawn with
//     five more sprite palettes (SPRITE_PALETTE()), one per coat, for the
//     visitor and up to four kittens beside the pet; enlarged sprites
//     (sprite_draw_ex()) for the growing serval, the stone and the album's
//     portraits
//
// What to expect when booting the ROM:
//   - The Serval Engine splash. Without a cartridge clock (mGBA without its
//     override, a cartridge without the chip) the game stops on a dark
//     screen saying SERVAL DEN NEEDS A CARTRIDGE CLOCK and, under it, IN MGBA:
//     TOOLS > GAME OVERRIDES > REALTIME CLOCK, and nothing else, for good.
//   - With a clock, SERVAL DEN in gold on black for a second (longer when
//     the time away is long: it is lived then). Then, on the dark screen, a
//     card if there is news (PRESS A goes on): THE CARTRIDGE CLOCK WAS RESET.
//     TIME AWAY WAS NOT COUNTED. if the clock started over; THE CLOCK JUMPED
//     AHEAD BY N DAYS. COUNT IT? if it is more than 30 days on (LEFT/RIGHT
//     choose YES or NO, NO first; A answers; YES lives the days, which a pet
//     doesn't survive, NO counts none); after an hour or more away, the
//     summary: AWAY 9H 30M in gold, then what happened (SPOTS GREW UP!,
//     SPOTS SLEPT 8H, A VISITOR IS AT THE DEN, SPOTS DIED) and how it is now
//     (SPOTS IS HUNGRY, IS SAD, IS SICK, IS FINE...; SPOTS DIED for a pet
//     gone, then its stone). A clock gone back is silent. The same happens
//     during play when the clock moves on by more than an hour (a web page's
//     clock can): the screen goes dark, the time is lived, the summary card,
//     then the den; up to an hour (the debug warp) the den just catches up.
//     Then the den fades in:
//     warm sandstone walls in layers, roots hanging from the roof (one holds
//     a jar of glowing fireflies at the left), a sandy floor with pebbles
//     and loose grass, a woven grass nest at the left, and through the
//     arched mouth at the right the savanna:
//     far hills, a flat-topped acacia, tall grass and the sky, its clouds
//     drifting. At the top, the serval's name and the clock's time. At the
//     foot, the care bar.
//   - The sky follows the clock: dawn 5-7 (violet to peach, the sun rising
//     orange at the left), day 8-17 (blue, the sun crossing high), dusk
//     18-20 (blue to orange, the sun setting at the right behind the hills),
//     night 21-4 (dark blue, stars, the moon); the den warms at dawn and dusk
//     and cools at night.
//   - A new den has a kitten (16x16, big ears, big eyes) with a coat of its
//     own. It wanders the floor, stops, blinks, flicks its tail. It sleeps in
//     the nest at night (21-7), curled, with rising Zs. It sits with its ears
//     down when sad (12 h without care), lies shivering and tinted blue when
//     sick (18 h), more blue when sicker (24 h). Droppings appear on the floor every few hours
//     awake, with a whiff above them. It grows to a lanky adolescent at 24 h and a long-legged
//     adult at 72 h (both 32x32).
//   - The care bar at the foot of the screen: eight 16x16 buttons (a bowl,
//     a ball of yarn, a paw, a broom, a medicine bottle, a lamp, a chart, a
//     book) with a gold frame on the chosen one and its name above. LEFT and
//     RIGHT choose, A acts, B backs out. FEED asks MEAL or TREAT (a bowl that
//     empties as it eats, crunching); PLAY lists YARN BAT, ACACIA CLIMB and
//     HIGH POUNCE in a box (the ones the pet's stage doesn't allow yet are
//     grey, and the ones it has grown into stay: the adult still plays all
//     three; the PLAY button is
//     grey while the pet is asleep, tired or gone); PET gives hearts and a
//     purr; CLEAN a sweep and sparkles; MEDICINE a sparkle and a chime (and, for a sick pet, health
//     lifted to at least a fifth of the bar); LIGHTS puts the lights out or on (a click; the den
//     goes dark but the world outside the mouth doesn't; START does the same from the bar); STATUS
//     is a page of bars (FULL, FUN, ENERGY, HEALTH), age, time to growing up or to the litter, time
//     since last care, mood and a warning once it is sad (12 h without care) or sick; ALBUM is the
//     serval raised, newest first, one a page (LEFT and RIGHT turn them; ALBUM 1 OF N): a portrait
//     in its coat, twice its size, in a frame, with a stone beside it (AT REST) or a heart (RAISED
//     A LITTER), its name, generation and age, and the den's records when it left (BAT, CLIMB,
//     POUNCE; NO GAMES PLAYED for none); NO ONE YET while empty. Caring for a sleeping serval wakes
//     it for an hour. A refusal sounds a low "no" and says why above the bar in red: NOT HUNGRY,
//     NOT SICK, ALREADY CLEAN, SHH, ASLEEP, TOO TIRED, NOT YET, GONE. A button held when a screen
//     opens does nothing until it is released.
//   - The first time the game starts (no save) a NAME YOUR KITTEN page: a
//     letter grid (A-Z, DEL, END) with a random suggestion filled in; D-pad
//     moves, A picks a letter, B deletes one, END or START accepts (up to 8
//     letters, at least 1).
//   - PLAY > YARN BAT (kitten and up): the den fades out and in again with the
//     care bar gone; BATS and BEST in the roof. After a moment the yarn ball
//     comes down from the roof toward the pet, which stands on the den floor;
//     it bounces off the roof and the rock walls at both sides, and trails a
//     red thread that waves and grows with each bat; the ball shrinks a little
//     as the yarn unravels. LEFT and RIGHT make the pet walk along the floor
//     (its stage's walk frames, facing the way it goes; its size sets its
//     reach): it swipes an arm up and shuts its eyes when it bats the ball
//     back up. The ball leaves the paw at an angle by where it met it (the
//     ends slant it to that side, the middle sends it straight up) and speeds
//     up a little every 4 bats. Thump on a bat, tap on a wall, a falling note
//     on a miss. A ball that falls past the paw to the floor ends the round. START pauses (RESUME
//     or QUIT; QUIT also ends and counts the round). A ROUND OVER card shows BATS, BEST (NEW! for a
//     record, saved) and FUN +N (10 plus the bats, at most 40): A returns to the den, whose FUN bar
//     has risen. Buttons held as the game starts or ends do nothing until released.
//   - PLAY > ACACIA CLIMB (adolescent and up): the den fades out and in as an
//     acacia's trunk seen from the foot, the sky behind it (its colours follow
//     the hour, clouds drifting), the serval on a ledge. HEIGHT and BEST in
//     metres at the top. LEFT and RIGHT walk and steer in the air, A jumps (hold
//     it to go higher: a full jump clears a bough a row up, a tap is a hop).
//     Boughs are one-way (leap up through them, land coming down), flat leafy
//     crowns on a limb, alternating left and right of the trunk; some sway
//     16 pixels either way, dry yellow ones crack when landed on and break 30
//     frames later (the pieces fall), a weaver bird's nest hanging from a bough
//     knocks the serval down. The camera follows upward only; falling below
//     the screen ends the round. START pauses (RESUME or QUIT); a ROUND OVER
//     card shows HEIGHT, BEST (NEW! for a record, saved) and FUN +N (10 plus
//     half the metres, at most 40). Jump, snap and knock have a sound each.
//   - PLAY > HIGH POUNCE (adult only): the den fades out and in as the savanna
//     seen from the grass, golden, with far hills, two flat acacias and the
//     sky behind (its colours follow the hour, clouds drifting). A row of
//     tall tufts sways across the foot of the screen, the serval (32x32,
//     adult) among them. SCORE, TIME (60 counting down) and BEST at the top.
//     Now and then a tuft shakes hard for a moment with a hissing rustle,
//     louder the nearer the serval is (soft far off, sharp close by), and an
//     arrow over the serval's head points left or right to the one it heard,
//     blinking out after a second. LEFT and RIGHT creep; B held crouches
//     (low, head forward: slower, birds aren't startled, and the rustles sound a step
//     louder); A is a
//     high leap 48 pixels ahead in the direction it faces (a rising arc, a
//     chirp). Landing within 12 pixels of a mouse's rustle catches it: a
//     mouse squirms under its paw (+1, a chirp). Some rustles hide a bird,
//     indistinguishable; walking (not crouched) within 24 pixels startles it
//     (a flutter): it flies up and away, and A while it is within 48 pixels
//     swats it in a puff of feathers (+3); the bird is a small brown one that
//     flaps. A leap that finds nothing scares the rustles near the landing
//     silent for 2 seconds (the grass hops, scared rustles whip theirs, seed
//     heads fly, a falling tone). An A pressed up to 8 frames early leaps
//     when the serval can. At 0 the TIME UP! card shows SCORE, BEST (NEW! for a
//     record, saved), MICE and BIRDS and FUN +N (10 plus the score, at most
//     40). START pauses (RESUME or QUIT).
//   - Growing up (adolescent at 24 h, adult at 72 h; during play or after
//     the boot's summary, once per stage): the care bar goes, sparkles circle
//     the serval, it grows, flashing white, to a rising fanfare, and stands
//     at its new size, happy; a box says SPOTS GREW UP!, NOW AN ADOLESCENT (or
//     AN ADULT) and NEW GAME: ACACIA CLIMB (or HIGH POUNCE); PRESS A.
//   - The litter (an adult 144 h old, well): A VISITOR! A serval in a coat of
//     its own walks in from the right and the pet goes to meet it; SPOTS
//     MEETS ZURI, both purr with hearts rising between them. The screen fades
//     to N KITTENS!: the parents at the back and 2-4 kittens in a row in front,
//     each in its own coat (from its parents'), its name under it; LEFT and
//     RIGHT choose (the chosen hops under a heart, its name in gold), A takes
//     it. The parents and the other kittens walk off to the right (SPOTS AND
//     ZURI LEAVE FOR THE SAVANNA); NAME YOUR KITTEN with the chosen kitten's
//     name filled in; the den with the new kitten, a generation on. Both
//     parents go into the album. A bright tune when the kittens appear.
//   - Death (36 h without care): the den fades to a grey stone, twice the
//     size, with flowers round its foot and fireflies drifting up, a box with
//     the name, LIVED N DAYS and GENERATION N, a slow quiet tune; PRESS A:
//     A NEW KITTEN ARRIVES on the dark screen, a kitten in a new coat
//     trotting in; PRESS A: NAME YOUR KITTEN; the den with the new kitten, a
//     generation on. The album keeps the one lost.
//   - Debug builds only: hold L, then R, and press SELECT: an hour passes (TIME
//     WARP +1H at the top). Release builds have no warp.
//   - To see the clock at work: in mGBA, a day later means setting the clock
//     (or leaving the game off for a day); on the web page the clock is the
//     browser's. A test page or a capture run can set it, with `rtc=` and
//     `rtcstep=` (docs/rtc.md).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into the portable core (pet.c, litter.c, album.c, den_save.c, events.c),
// clock.c (the game's use of the clock), art.c (the colours, and the
// pictures' loading; art_draw.c draws them, on the host), den.c (the den on
// screen), menu.c (the care bar and the album), status.c and naming.c (their
// pages), scenes.c (the life events) and sound.c.
// main.c boots and runs the den a frame at a time.

#include "album.h"
#include "art.h"
#include "climb.h"
#include "clock.h"
#include "den.h"
#include "den_save.h"
#include "events.h"
#include "menu.h"
#include "naming.h"
#include "pounce.h"
#include "scenes.h"
#include "sound.h"
#include "status.h"
#include "yarn.h"

#include <serval/serval.h>

#define AUTOSAVE_FRAMES (5 * 60 * 60) // every 5 minutes of play
#define TITLE_FRAMES 60               // the title's least time on screen
#define STYLE_ALERT 2                 // text.h's third style: light red

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
        text_set_style(STYLE_ALERT);
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

// Whole minutes of the pet's life not yet stepped (0 for a clock gone back).
static u32 gap_minutes(const Pet* p, u32 now) {
    return now > p->last_tick ? (now - p->last_tick) / 60 : 0;
}

// The most the den catches up by itself, 15 minutes a frame (den_update());
// more is lived at once behind a black screen, as at boot (events.h).
#define CATCH_UP_IN_DEN 60

// The clock moved on while the game was on (a web page's can): past 30 days,
// the question; past an hour, lived at once with a summary. The scene
// returns with the den behind a black screen: stored, then faded in.
static bool time_jumped(Pet* pet, u32 now) {
    if (gap_minutes(pet, now) > CLOCK_JUMP_LIMIT / 60)
        scenes_jump_in_play(&save, now);
    else if (gap_minutes(pet, now) > CATCH_UP_IN_DEN)
        scenes_catch_up_in_play(&save, now);
    else
        return false;
    store(clock_now());
    menu_fade_in(pet, true);
    return true;
}

int main(void) {
    serval_init();
    serval_splash();
    if (!rtc_present())
        no_clock();
    // The title, for a second at least, and while the time away is lived
    // (longer after a long absence).
    text_set_color(COLOR_RGB(232, 176, 72), COLOR_RGB(0, 0, 0));
    text_print_centered(9, "SERVAL DEN");
    frame_begin();
    frame_end();
    u32 title_shown = frame_count();
    sound_init();

    u32 now;
    bool first_boot = false;
    ClockCheck check = CLOCK_OK;
    Away away = {0};
    bool lived = false;
    if (den_save_load(&save)) {
        album_set(save.album, save.album_count);
        const Pet* p = &save.pet;
        u32 age = p->last_tick > p->born ? p->last_tick - p->born : 0;
        check = clock_boot(save.last_seen, age, save.offset, &now);
        if (check == CLOCK_OK || check == CLOCK_WAS_RESET) {
            events_live(&save.pet, now, clock_hour, &away); // the time away, minute by minute
            lived = true;
        } else if (check == CLOCK_WENT_BACK) {
            // Backwards: the time the save says passed is lived, the rest is
            // not (silently).
            pet_advance(&save.pet, save.last_seen, clock_hour);
            pet_rebase(&save.pet, now);
        }
        // CLOCK_JUMPED: the player is asked, once the title has been seen.
    } else {
        // A new den: the first kitten, to be named once the title is gone.
        first_boot = true;
        now = clock_start();
        random_seed(now ^ random_entropy());
        Coat coat = litter_visitor_coat();
        save = (DenSave){0};
        pet_new(&save.pet, "SPOTS", coat, 1, now);
        pet_advance(&save.pet, now, clock_hour);
    }
    random_seed(now ^ 0x5E5A1u ^ random_entropy()); // the naming page's suggestion varies
    while (frame_count() - title_shown < TITLE_FRAMES) {
        frame_begin();
        frame_end();
    }

    // The clock's news and the time away, as cards over the dark title.
    if (check == CLOCK_WAS_RESET) {
        static const char* const reset[2] = {"THE CARTRIDGE CLOCK WAS RESET.",
                                             "TIME AWAY WAS NOT COUNTED."};
        scenes_card(reset, 2);
    } else if (check == CLOCK_JUMPED) {
        if (scenes_ask_jump((now - save.last_seen) / 86400)) {
            events_live(&save.pet, now, clock_hour, &away);
            lived = true;
        } else {
            pet_advance(&save.pet, save.last_seen, clock_hour);
            now = clock_decline_jump(save.last_seen); // the clock was set: not time passed
            pet_rebase(&save.pet, now);
        }
    }
    if (lived)
        scenes_summary(&save.pet, &away);

    if (first_boot) {
        // Naming the first kitten, on a page of its own.
        for (int b = 0; b >= SCREEN_BRIGHTNESS_MIN; b -= 2) {
            frame_begin();
            screen_set_brightness(b);
            frame_end();
        }
        frame_begin();
        frame_end();
        text_set_color(COLOR_RGB(248, 236, 208), COLOR_RGB(30, 20, 16));
        text_set_shadow(true);
        screen_set_backdrop(COLOR_RGB(24, 16, 14));
        frame_begin();
        text_clear();
        frame_end();
        for (int b = SCREEN_BRIGHTNESS_MIN; b <= 0; b += 2) {
            frame_begin();
            screen_set_brightness(b);
            frame_end();
        }
        naming_run(save.pet.name, "NAME YOUR KITTEN", 0);
    }

    // Black first (the brightness takes effect at the frame's end), then
    // VRAM is written.
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN);
    frame_begin();
    frame_end();
    den_enter();
    store(now); // after den_enter(), whose text_clear() would hide SAVE FAILED
    Pet* pet = &save.pet;
    menu_init();
    yarn_attach(&save.best[0]);
    menu_set_game(GAME_YARN, yarn_play);
    climb_attach(&save.best[1]);
    menu_set_game(GAME_CLIMB, climb_play);
    pounce_attach(&save.best[2]);
    menu_set_game(GAME_POUNCE, pounce_play);
    for (int b = SCREEN_BRIGHTNESS_MIN; b <= 0; b++) {
        frame_begin();
        screen_set_brightness(b);
        den_update(pet, clock_now());
        menu_draw(pet, clock_now());
        den_draw(pet, clock_now());
        frame_end();
    }

#ifdef SERVAL_DEBUG
    int warp_timer = 0;
#endif
    for (;;) {
        frame_begin();
        now = clock_now();
        // Time to catch up comes first, before any input, so that no care
        // can land inside it (and rescue a pet that should not have lived).
        if (time_jumped(pet, now))
            now = clock_now();
        Life before = life_of(pet);
        bool cared = menu_update(pet, now);
        now = clock_now(); // a game or a page may have taken a while
#ifdef SERVAL_DEBUG
        if (clock_debug_warp()) {
            warp_timer = 90;
            text_print_centered(2, "TIME WARP +1H");
            now = clock_now();
        }
        if (warp_timer > 0 && --warp_timer == 0) {
            text_clear_area(0, 2, 30, 1);
            if (menu_covers_den())
                status_reset(); // the page's text shares the row
        }
#endif
        if (gap_minutes(pet, now) <= CATCH_UP_IN_DEN)
            den_update(pet, now); // a longer gap waits for the next frame's time_jumped()
        frames_since_save++;
        bool stored = false;
        if (cared || life_changed(before, life_of(pet)) || frames_since_save >= AUTOSAVE_FRAMES) {
            store(now);
            stored = true;
        }
        if (save_failed_timer > 0 && --save_failed_timer == 0) {
            text_clear_area(0, 17, 30, 1);
            if (menu_covers_den())
                status_reset(); // the page's warning shares the row
        }
        // Life events: growing up, the litter, the tombstone; not over a page,
        // and not in a frame that stored (the event lasts: it starts on the
        // next, so the save and the scene's setup don't share a frame).
        if (!stored && !menu_covers_den()) {
            LifeEvent ev = events_next(pet, save.stage_seen);
            bool changed = ev == EVENT_TOMBSTONE ? scenes_tombstone(&save)
                           : ev == EVENT_GROW_UP ? scenes_grow_up(&save)
                           : ev == EVENT_LITTER  ? scenes_litter(&save)
                                                 : false;
            if (changed) {
                // Written while the screen is still black, then the den.
                now = clock_now();
                store(now);
                menu_fade_in(pet, true);
            }
        }
        sound_ambient(art_time_of_day(clock_hour(now)));
        menu_draw(pet, now);
        if (!menu_covers_den())
            den_draw(pet, now);
        frame_end();
    }
}
