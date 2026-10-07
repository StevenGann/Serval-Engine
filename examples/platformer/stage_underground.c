// Stage 1-2, the underground: a dark cave under a rock ceiling, with
// one-way plank ledges over pits, crystal spikes, woodlice and bats that
// swoop down from the ceiling, and a timbered exit back to daylight.

#include "stage_underground.h"

// Legend: level.c's, plus
//   =  ceiling rock         A  crystal spikes (solid; they hurt)
//   v  stalactites          m  glowing mushrooms (in front of sprites)
//   k  a bat, asleep under the ceiling
// 'e' is a woodlouse, '-' a plank ledge, 'D' the exit. Row 11 is the floor's
// surface; the serval stands on it in row 10. The ceiling's lowest row is
// at row 3 (row 5 in the low passage), out of reach from the floor.
static const char level_text[][16 + 1] = {
    // Screen 0: columns 0-15
    "================",
    "================",
    "================",
    "================",
    "..v.......v.....",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..s.....m....m..",
    "################",
    "################",
    // Screen 1: columns 16-31
    "================",
    "================",
    "================",
    "================",
    "....v........v..",
    "................",
    "................",
    "..BPBGB.........",
    "................",
    "................",
    "...........e....",
    "################",
    "################",
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
    "m...............",
    "###.........####",
    "###.........####",
    // Screen 3: columns 48-63
    "================",
    "================",
    "================",
    "================",
    "......k.........",
    "................",
    "................",
    "..........BFB...",
    "................",
    "................",
    "..............m.",
    "####...#########",
    "####AAA#########",
    // Screen 4: columns 64-79
    "================",
    "================",
    "================",
    "=======...======",
    "...........v....",
    "................",
    "........oo......",
    "................",
    ".......X..X.....",
    "......XX..XX....",
    "..e..XXX..XXX...",
    "################",
    "################",
    // Screen 5: columns 80-95
    "================",
    "================",
    "================",
    "================",
    "................",
    "................",
    "................",
    "...BBGBB...P....",
    "................",
    "................",
    ".c.......e.e..m.",
    "################",
    "################",
    // Screen 6: columns 96-111
    "================",
    "================",
    "================",
    "================",
    "....k......k....",
    "................",
    "................",
    "................",
    "..--..--..--....",
    "................",
    "................",
    "#.............##",
    "#.............##",
    // Screen 7: columns 112-127
    "================",
    "================",
    "================",
    "================",
    "================",
    "================",
    "................",
    "................",
    "...P.B.B.P......",
    "................",
    ".e............e.",
    "################",
    "################",
    // Screen 8: columns 128-143
    "================",
    "================",
    "================",
    "================",
    "........k.......",
    "................",
    "................",
    "................",
    "................",
    "...XX..XX..XX...",
    "...XX..XX..XX...",
    "##.XX..XX..XX.##",
    "##AXXAAXXAAXXA##",
    // Screen 9: columns 144-159
    "================",
    "================",
    "================",
    "================",
    "................",
    "................",
    "....BPPB........",
    "................",
    "................",
    "..........--....",
    "....e........m..",
    "#########....###",
    "#########....###",
    // Screen 10: columns 160-175
    "================",
    "================",
    "================",
    "================",
    "..v.....k.....v.",
    "................",
    "................",
    "......ooo.......",
    "................",
    "................",
    "..e........e.m..",
    "######...#######",
    "######AAA#######",
    // Screen 11: columns 176-191
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
    switch (c) {
    case '=': { // its lowest row has a ragged edge, unless something is under it
        char below = level_text_at(mx, my + 1);
        return below == '=' || below == '#' || below == 'D' ? MT_CEILING : MT_CEILING_EDGE;
    }
    case 'A':
        return MT_SPIKES;
    case 'v':
        return MT_STALACTITES;
    case 'm':
        *front = MT_MUSHROOMS;
        return MT_EMPTY;
    case 'k':
        level_add_spawn(SPAWN_BAT, mx, my);
        return MT_EMPTY;
    default:
        return MT_EMPTY;
    }
}

// --- Bats --------------------------------------------------------------------
//
// A bat sleeps upside down under the ceiling until the serval comes within
// BAT_WAKE_X pixels of it, then squeaks and swoops: straight down, a U-turn
// toward the side the serval is on, and up and away (path.h: sys_path turns
// the path into velocity, sys_movement moves it, through the rock). A bat is
// an enemy like the others: land on it to stomp it, touch it otherwise and
// the serval is hurt.

#define C_BAT C_STAGE(0)
#define BAT_W 12
#define BAT_H 10
#define BAT_WAKE_X 104

// Turning clockwise on screen, from heading down: to the left, then up.
// Mirrored (PATH_MIRROR_X) it swoops to the right.
static const PathStep swoop_steps[] = {
    {.frames = 28, .speed = FX(2)},                       // straight down: 56 pixels
    {.frames = 60, .speed = FX(2), .turn = ANGLE_DEG(3)}, // half a circle, 76 pixels across
    {.speed = FX(2) + FX_ONE / 2},                        // up and away, for ever
};
static const Path swoop = {PATH_STEPS(swoop_steps), .heading = ANGLE_DEG(90)};

static int center_x(u32 i) {
    return fx_to_int(pos_x[i]) + body_w[i] / 2;
}

static void spawn(const Spawn* s) {
    if (s->kind != SPAWN_BAT)
        return;
    // Hanging from the ceiling's underside, at the top of its cell.
    u32 i = object_create(C_VEL | C_BODY | C_ENEMY | C_BAT, SPR_BAT_HANG,
                          s->mx * TILE + (TILE - BAT_W) / 2, s->my * TILE + 2);
    if (i == MAX_ENT)
        return;
    body_w[i] = BAT_W;
    body_h[i] = BAT_H;
    body_gravity[i] = BODY_GRAVITY(0); // it flies: sys_physics leaves it be
    obj_kind[i] = SPAWN_BAT;
    obj_dir[i] = -1;
}

static void update(void) {
    int px = player_center_x();
    int feet = fx_to_int(pos_y[player]) + body_h[player];
    ECS_FOR_EACH(i, C_BAT) {
        if (spr_id[i] != SPR_BAT_HANG)
            continue;
        int dx = px - center_x(i);
        if (int_abs(dx) < BAT_WAKE_X && feet > fx_to_int(pos_y[i]) + BAT_H) {
            spr_id[i] = SPR_BAT_FLY;
            spr_frame[i] = 0;
            ent_mask[i] |= C_ANIM;
            path_start(entity_at(i), &swoop, dx < 0 ? 0 : PATH_MIRROR_X);
            psg_play(SND_BAT);
        }
    }
}

static void after_move(void) {
    ECS_FOR_EACH(i, C_BAT) {
        if (fx_to_int(pos_y[i]) < -2 * TILE) // flown up out of the level
            entity_destroy(entity_at(i));
    }
}

// --- Effects -----------------------------------------------------------------

// The cave's air glows faintly, swelling and fading every four seconds or
// so: the backdrop mixed a little toward violet (color_mix).
static void effects(void) {
    int t = (int)(frame_count() % 256);
    int glow = t < 128 ? t : 255 - t; // 0 to 127 and back
    screen_set_backdrop(color_mix(UG_BACKDROP, UG_GLOW, (u32)glow));
}

const StageDef stage_underground = {
    .name = "1-2",
    .text = level_text,
    .screens = sizeof level_text / sizeof level_text[0] / 13,
    .height = 13,
    .time = 300,
    .tileset = &underground_tileset,
    .metatiles = underground_metatiles,
    .metatile_count = UG_MT_COUNT,
    .bonus_tile = UG_BONUS_TILE,
    .far_layer = &underground_far_layer,
    .backdrop = UG_BACKDROP,
    .sprites = &underground_group,
    .walker_sprite = SPR_LOUSE,
    .walker_flat_sprite = SPR_LOUSE_BALL,
    .hopper_sprite = SPR_LOUSE, // (no hoppers here)
    .song = &underground_song,
    .hurry_tempo = 150, // from 120
    .load = underground_load_palettes,
    .cell = cell,
    .spawn = spawn,
    .update = update,
    .after_move = after_move,
    .effects = effects,
};
