#ifndef DEN_ART_H
#define DEN_ART_H

// Serval Den's pictures, drawn ahead of time (art_draw.c, by art_gen on the
// host, into art_data.c; no image files; art.c says how to change them): the
// den (a rock shelter with a sandy floor, a grass nest and an arched
// mouth onto the savanna) on background 2, the sky behind it on background 3,
// and the sprites: the serval at three sizes, the sun and the moon, the
// firefly lamp, the droppings, the bowl and the small effects.
//
// Time of day and the lights are palettes: the sky and the savanna have one
// for each of dawn, day, dusk and night, the den's rock is mixed toward the
// hour's light and dimmed when the lights are off. The serval's coat (its
// shade, spot pattern and eye colour) is a palette too: its spots are drawn
// in three colours, each a set of spots that the coat's pattern shows in black
// or hides in the coat's colour.

#include "pet.h"

#include <serval/serval.h>

#define DEN_BG 2 // the den: rock, floor, nest, and the savanna seen through its mouth
#define SKY_BG 3 // the sky: gradient, clouds and stars, drifting (it wraps)

// The den's floor: where the serval's paws can be (screen y of the paws'
// row), and the nest where it sleeps.
#define FLOOR_TOP 118
#define FLOOR_BOTTOM 146
#define NEST_X 52 // the nest's middle
#define NEST_Y 128
#define LAMP_X 30 // the firefly jar's top-left
#define LAMP_Y 26

// The den's mouth: the sky shows inside x MOUTH_X0-MOUTH_X1, above MOUTH_Y1.
#define MOUTH_X0 120
#define MOUTH_X1 200
#define MOUTH_TOP 24
#define MOUTH_Y1 96

// Times of day: what the sky shows (clock_hour(): dawn 5-7, day 8-17,
// dusk 18-20, night 21-4).
enum { TOD_DAWN, TOD_DAY, TOD_DUSK, TOD_NIGHT, TOD_COUNT };
int art_time_of_day(int hour);

// --- Sprites -------------------------------------------------------------------------------

enum {
    SPR_KITTEN, // 16x16, PET_FRAMES
    SPR_YOUNG,  // 32x32, PET_FRAMES: the adolescent
    SPR_ADULT,  // 32x32, PET_FRAMES: bigger, longer-legged
    SPR_SUN,    // 16x16: the sun, drawn behind the den (its colours follow the hour)
    SPR_MOON,   // 16x16
    SPR_LAMP,   // 16x16: LAMP_FRAMES, the firefly jar hanging from a root
    SPR_DROP,   // 8x8: DROP_FRAMES droppings (two shapes, then a steam wisp)
    SPR_BOWL,   // 16x8: BOWL_FRAMES (full, half, empty)
    SPR_ZZZ,    // 8x8: a big Z and a small one
    SPR_HEART,  // 8x8: a heart, then a small one
    SPR_SPARK,  // 8x8: SPARK_FRAMES of a sparkle
    SPR_TOMB,   // 16x16: a stone with flowers
    SPR_ICON,   // 16x16: ICON_COUNT frames, the care bar's buttons (PAL_ICONS)
    SPR_CURSOR, // 16x16: the bar's selection, a gold frame
    SPR_PANEL,  // 16x16: 9 frames, a box cut in nine (row by row: corners, edges, middle)
    SPR_STRIP,  // 8x8: 3 frames (left end, middle, right end), a one-line box for the bar's label
    SPR_BAR,    // 8x8: BAR_COLORS x 9 frames, a status bar's cell: colour * 9 + columns filled
    SPR_YARN, // 8x8: 3 sizes x YARN_TURNS frames, a ball of yarn turning, then smaller (PAL_ICONS;
              // Yarn Bat)
    SPR_THREAD, // 8x8: THREAD_FRAMES, a bit of loose thread, newest first
    SPR_PAW,    // 16x8: PAW_FRAMES, the pet's arm reaching right, in its coat (PAL_PET)
    SPRITE_COUNT,
};

// The serval's frames (facing right; SPRITE_FLIP_H for left), the same at
// every size.
enum {
    PF_STAND,
    PF_STAND_TAIL, // the tail flicked
    PF_BLINK,
    PF_WALK0,
    PF_WALK1,
    PF_WALK2,
    PF_WALK3,
    PF_HAPPY, // eyes shut in a smile, head up
    PF_EAT0,  // head down at the bowl
    PF_EAT1,
    PF_SIT,  // sad: sitting, ears down, head low
    PF_SIT1, // the same, a tail twitch
    PF_LIE,  // sick: lying, ears down
    PF_LIE1, // the same, eyes shut
    PF_SLEEP0,
    PF_SLEEP1, // breathing
    PET_FRAMES,
};

#define LAMP_FRAMES 3 // lit, lit flickering, dark
#define DROP_FRAMES 3
#define BOWL_FRAMES 3
#define SPARK_FRAMES 3
#define YARN_TURNS 4
#define YARN_SIZES 3
#define PAW_FRAMES 3
#define THREAD_FRAMES 3

// The care bar's icons (SPR_ICON's frames), left to right.
enum {
    ICON_FEED,
    ICON_PLAY,
    ICON_PET,
    ICON_CLEAN,
    ICON_MEDICINE,
    ICON_LIGHTS,
    ICON_STATUS,
    ICON_ALBUM,
    ICON_COUNT,
};

// SPR_BAR's colours: a status bar per need.
enum { BAR_FULL, BAR_FUN, BAR_ENERGY, BAR_HEALTH, BAR_COLORS };

// Sprite palettes.
enum {
    PAL_PET,   // the coat: art_pet_colors()
    PAL_ITEMS, // droppings, bowl, Z, hearts, sparkle, the stone
    PAL_LAMP,
    PAL_SKY_OBJ,   // sun and moon: their colours follow the hour (art_set_scene)
    PAL_ICONS,     // the care bar's icons, its cursor and the status bars
    PAL_ICONS_OFF, // the same in grey, for what can't be chosen now
    OBJ_PALETTES,
};

// The serval's 16 colours for a coat, tinted: `tint` mixed in by `amount`
// (0-256; color_mix).
void art_pet_colors(Coat coat, Color tint, u32 amount, Color out[16]);

// Writes the serval's palette for a coat, as the den's light shows it:
// `sick` 0 (well), 1 or 2 tints it blue, then the hour's light and the
// lights off as for the den (palette writes, in VBlank).
void art_set_pet(Coat coat, int sick, int tod, bool lights_off);

// --- Loading ------------------------------------------------------------------

// Loads the tiles, palettes and sprites into VRAM and shows the den and the
// sky. Call with the screen black.
void art_load(void);

// Takes the den and the sky off the screen (a full-screen page over a plain
// backdrop); den_resume() brings them back. The screen must be black.
void art_hide(void);

// The den's colours for a time of day, with the lights on or off, the sky's,
// the savanna's, the sun's and the things on the floor (palette writes, in
// VBlank). Call when either changes, not every frame.
void art_set_scene(int tod, bool lights_off);

#endif // DEN_ART_H
