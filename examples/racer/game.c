// The game's flow and HUD. For now: boot straight into a race of the
// savanna circuit (the countdown, then three laps against the clock),
// driven with A, B and the D-pad; START races again once it is over.

#include "game.h"

#include "art.h"
#include "road.h"
#include "track.h"
#include "view.h"

#define FADE_STEP 2   // brightness per frame: 8 frames from black
#define GO_FRAMES 60  // "GO!" stays up this long after the countdown
#define MESSAGE_ROW 5 // the countdown's and the end's messages
#define RACE_SEED 1

static Race race;
static RoadFrame road SERVAL_EWRAM_BSS;
static int brightness = SCREEN_BRIGHTNESS_MIN;
static u32 since_go; // frames since GO, for taking "GO!" down

// What the HUD shows, so values are printed only when they change.
static int shown_lap, shown_time, shown_speed, shown_count;
static u32 shown_race_time;

// The driving buttons held this frame, as race_step() takes them.
static u16 held_buttons(void) {
    static const u16 driving[] = {BUTTON_A, BUTTON_B, BUTTON_LEFT, BUTTON_RIGHT};
    u16 held = 0;
    for (u32 i = 0; i < sizeof driving / sizeof driving[0]; i++)
        if (button_down(driving[i]))
            held |= driving[i];
    return held;
}

static void hud_reset(void) {
    text_clear();
    text_print_centered_in(11, 8, 0, "TIME");
    shown_lap = shown_time = shown_speed = shown_count = -1;
    shown_race_time = (u32)-1;
}

static void start_race(void) {
    race_start(&race, &savanna, RACE_SEED);
    view_reset();
    since_go = 0;
    hud_reset();
}

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    road_init();
    art_build();
    view_init();
    text_set_shadow(true); // the HUD reads over the bright sky
    start_race();
}

// The top two rows: LAP and the race's time on the left, the time left in
// the middle, the speed on the right.
static void draw_hud(void) {
    int lap = int_min(race.lap + 1, RACE_LAPS);
    if (lap != shown_lap)
        text_print(1, 0, text_format("LAP %d/%d", shown_lap = lap, RACE_LAPS));
    u32 t = race.race_time;
    if (t != shown_race_time) {
        shown_race_time = t;
        text_print(1, 1, text_format("%u'%02u\"%02u", t / 3600, t / 60 % 60, t % 60 * 100 / 60));
    }
    u32 left = race.countdown ? race.track->start_time : race.time_left;
    int seconds = (int)((left + 59) / 60);
    if (seconds != shown_time)
        text_print_centered_in(11, 8, 1, text_format("%d", shown_time = seconds));
    int kmh = race_speed_kmh(&race);
    if (kmh != shown_speed)
        text_print(21, 0, text_format("%3d KM/H", shown_speed = kmh));
}

// The middle of the screen: 3, 2, 1, GO!, then FINISH or TIME UP.
static void draw_message(void) {
    int count; // what to show: 3-1 counting down, 0 GO!, -2 nothing, -3/-4 the end
    if (race.over)
        count = race.finished ? -3 : -4;
    else if (race.countdown)
        count = (race.countdown + 59) / 60;
    else
        count = since_go < GO_FRAMES ? 0 : -2;
    if (count == shown_count)
        return;
    shown_count = count;
    text_clear_area(0, MESSAGE_ROW, TEXT_COLS, 3);
    if (count > 0)
        text_print_centered(MESSAGE_ROW, text_format("%d", count));
    else if (count == 0)
        text_print_centered(MESSAGE_ROW, "GO!");
    else if (count < -2) {
        text_print_centered(MESSAGE_ROW, count == -3 ? "FINISH" : "TIME UP");
        text_print_centered(MESSAGE_ROW + 2, "START: RACE AGAIN");
    }
}

void game_frame(void) {
    if (brightness < 0) {
        brightness = int_min(brightness + FADE_STEP, 0);
        screen_set_brightness(brightness);
    }
    if (race.over && button_pressed(BUTTON_START))
        start_race();
    race_step(&race, held_buttons());
    if (!race.countdown)
        since_go++;
    road_frame(&road, &savanna, race.distance, race.lateral);
    view_frame(&race, &road);
    draw_hud();
    draw_message();
}
