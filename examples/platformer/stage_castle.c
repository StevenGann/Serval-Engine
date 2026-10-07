// Stage 1-4, the castle: the last stage, and after it the ending.
//
// A placeholder: a short level on the underground's art (stage_castle.h),
// with spikes where the lava will be, woodlice, and an exit; the boss's slot
// (boss.c) is empty, and the ending is a short message.

#include "stage_castle.h"
#include "stage_underground.h" // (the placeholder's art: the underground's metatiles)

// Legend: level.c's, plus
//   =  stone ceiling        A  spikes (solid; they hurt)
// Row 11 is the floor's surface; the serval stands on it in row 10.
static const char level_text[][16 + 1] = {
    // Screen 0: columns 0-15
    "================",
    "================",
    "================",
    "================",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..s.........e...",
    "################",
    "################",
    // Screen 1: columns 16-31
    "================",
    "================",
    "================",
    "================",
    "................",
    "................",
    "................",
    ".BPB............",
    "................",
    "................",
    "................",
    "######...#######",
    "######AAA#######",
    // Screen 2: columns 32-47
    "================",
    "================",
    "================",
    "================",
    "................",
    "................",
    "................",
    "................",
    "....--..--......",
    "................",
    "................",
    "##..........####",
    "##AAAAAAAAAA####",
    // Screen 3: columns 48-63
    "================",
    "================",
    "================",
    "================",
    "........DDDD====",
    "........DDDD====",
    "........DDDD====",
    "......##########",
    ".....X##########",
    "....XX##########",
    "...XXX##########",
    "################",
    "################",
};

// The stage's own characters (StageDef.cell).
static u16 cell(char c, int mx, int my, u16* front) {
    (void)front;
    switch (c) {
    case '=': {
        char below = level_text_at(mx, my + 1);
        return below == '=' || below == '#' || below == 'D' ? MT_CEILING : MT_CEILING_EDGE;
    }
    case 'A':
        return MT_SPIKES;
    default:
        return MT_EMPTY;
    }
}

static void start(void) {
    boss_start();
}

static void update(void) {
    boss_update();
}

static void after_move(void) {
    boss_after_move();
}

// --- The ending (placeholder) --------------------------------------------------

#define ENDING_FRAMES 600

static int ending_timer;

static void ending_start(void) {
    ending_timer = 0;
    text_print_centered(6, "THE END");
    text_print_centered(9, "THE SERVAL IS HOME AGAIN");
    text_print_centered(12, "THANK YOU FOR PLAYING");
    psg_music_play(&goal_song);
}

static bool ending_update(void) {
    return ++ending_timer >= ENDING_FRAMES || (ending_timer > 60 && button_pressed(BUTTON_START));
}

const StageDef stage_castle = {
    .name = "1-4",
    .text = level_text,
    .screens = sizeof level_text / sizeof level_text[0] / 13,
    .height = 13,
    .time = 300,
    // Borrowed from the underground until the castle has art of its own.
    .tileset = &underground_tileset,
    .metatiles = underground_metatiles,
    .metatile_count = UG_MT_COUNT,
    .bonus_tile = UG_BONUS_TILE,
    .far_layer = &underground_far_layer,
    .backdrop = COLOR_RGB(24, 8, 8), // a red-black dark
    .sprites = &underground_group,
    .walker_sprite = SPR_LOUSE,
    .walker_flat_sprite = SPR_LOUSE_BALL,
    .hopper_sprite = SPR_LOUSE,
    .song = &castle_song,
    .hurry_tempo = 140, // from 112
    .load = underground_load_palettes,
    .cell = cell,
    .start = start,
    .update = update,
    .after_move = after_move,
    .ending_start = ending_start,
    .ending_update = ending_update,
};
