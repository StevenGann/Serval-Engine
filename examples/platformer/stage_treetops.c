// Stage 1-3, the treetops: high in the trees, from canopy to canopy over
// bottomless gaps.
//
// A placeholder: a short level on the overworld's art (stage_treetops.h),
// with hedges for one-way canopies on tree-stump trunks, a few frogs, and the
// goal pole and den at the end.

#include "stage_treetops.h"
#include "stage_overworld.h" // (the placeholder's art: the overworld's metatiles)

// Legend: level.c's, plus
//   T  a trunk (2 wide), under a canopy ('-')
// Row 11 is the ground's surface; the serval stands on it in row 10.
static const char level_text[][16 + 1] = {
    // Screen 0: columns 0-15
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "...s............",
    "################",
    "################",
    // Screen 1: columns 16-31
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....r..........",
    "....----........",
    ".....TT.........",
    ".....TT.....r...",
    "###..TT...######",
    "###..TT...######",
    // Screen 2: columns 32-47
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".........----...",
    "................",
    "...----...TT....",
    "....TT....TT....",
    "....TT....TT....",
    "#...TT....TT...#",
    "#...TT....TT...#",
    // Screen 3: columns 48-63
    "................",
    ".......^........",
    ".......|........",
    ".......|........",
    ".......|........",
    ".......|........",
    ".......|........",
    ".......|........",
    ".......|...DDDD.",
    ".......|...DDDD.",
    "..r....X...DDDD.",
    "################",
    "################",
    // Screen 4: columns 64-79
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "################",
    "################",
};

// The stage's own characters (StageDef.cell).
static u16 cell(char c, int mx, int my, u16* front) {
    (void)front;
    if (c == 'T')
        return (u16)(MT_STUMP + level_run_left(mx, my) % 2);
    return MT_EMPTY;
}

const StageDef stage_treetops = {
    .name = "1-3",
    .text = level_text,
    .screens = sizeof level_text / sizeof level_text[0] / 13,
    .height = 13,
    .time = 300,
    // Borrowed from the overworld until the treetops have art of their own.
    .tileset = &overworld_tileset,
    .metatiles = overworld_metatiles,
    .metatile_count = OW_MT_COUNT,
    .bonus_tile = OW_BONUS_TILE,
    .far_layer = &overworld_far_layer,
    .backdrop = COLOR_RGB(120, 190, 250), // a paler sky, higher up
    .sprites = &overworld_group,
    .walker_sprite = SPR_BEETLE,
    .walker_flat_sprite = SPR_BEETLE_FLAT,
    .hopper_sprite = SPR_FROG,
    .song = &treetops_song,
    .hurry_tempo = 165, // from 132
    .cell = cell,
};
