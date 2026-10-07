// Stage 1-3, the treetops: high in a forest's crowns under a daytime sky,
// from canopy to canopy over bottomless gaps. Caterpillars and tree frogs
// stay out of the gaps, birds weave through the air on a path, and burr
// trees drop spiky burrs that bounce along the canopies for ever (a perfect
// bounce). At the end, the goal pole stands on a great tree's bough, with a
// hollow in its trunk to walk into.

#include "stage_treetops.h"

// Legend: level.c's, plus
//   (  a canopy's left end      )  its right end
//   T  a trunk (2 wide), under a canopy
//   H  the great tree's trunk (4 wide), above the exit
//   f  leafy sprigs in front of the sprites
//   b  a bird                   q  a burr tree: burrs drop from here
// A canopy's middle is '-' (one-way, like its ends); trunks aren't solid.
// '#' is a great tree's flat top, 'X' a log end, 'e' a caterpillar, 'r' a
// tree frog, 'D' the hollow (the exit). Nothing is below the canopies: the
// gaps are bottomless. Row 9 is the great trees' tops, at the start and the
// end; the serval starts on it in row 8. Canopies are on rows 4 to 9.
static const char level_text[][16 + 1] = {
    // Screen 0: columns 0-15
    "................",
    "................",
    "................",
    "................",
    "................",
    "...........P...o",
    "................",
    "................",
    "..s.........fe..",
    "#######..(----).",
    "#######....TT...",
    "#######....TT...",
    "#######....TT...",
    // Screen 1: columns 16-31
    "................",
    "................",
    "................",
    "........BFB.....",
    "................",
    "o............oo.",
    "...........r....",
    ".......(----)...",
    ".(--)....TT.....",
    "..TT.....TT....(",
    "..TT.....TT.....",
    "..TT.....TT.....",
    "..TT.....TT.....",
    // Screen 2: columns 32-47
    "................",
    "................",
    "................",
    "................",
    "..............q.",
    "................",
    "................",
    "................",
    ".f...(--------).",
    "--)......TT.....",
    "TT.......TT.....",
    "TT.......TT.....",
    "TT.......TT.....",
    // Screen 3: columns 48-63
    "................",
    "................",
    "..oo............",
    "................",
    "................",
    "................",
    ".(--).........q.",
    "..TT.....f......",
    "..TT...(------).",
    "..TT......TT....",
    "..TT......TT....",
    "..TT......TT....",
    "..TT......TT....",
    // Screen 4: columns 64-79
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".(--)....e.....(",
    "..TT....(---)...",
    "..TT.....TT.....",
    "..TT.....TT.....",
    "..TT.....TT.....",
    "..TT.....TT.....",
    // Screen 5: columns 80-95
    "................",
    "................",
    "................",
    "...oo...........",
    "................",
    "...b............",
    "c....(--).......",
    "--)...TT.....r..",
    "TT....TT...(----",
    "TT....TT.....TT.",
    "TT....TT.....TT.",
    "TT....TT.....TT.",
    "TT....TT.....TT.",
    // Screen 6: columns 96-111
    "................",
    "................",
    "................",
    "b...............",
    ".....BGBPB......",
    "................",
    "...............(",
    ".....f..........",
    ")...(-------)...",
    ".......TT.......",
    ".......TT.......",
    ".......TT.......",
    ".......TT.......",
    // Screen 7: columns 112-127
    "................",
    ".....oo.........",
    ".b..............",
    "................",
    "................",
    ".....().........",
    "--)..TT....e.e..",
    "TT...TT...(---).",
    "TT...TT....TT...",
    "TT...TT....TT...",
    "TT...TT....TT...",
    "TT...TT....TT...",
    "TT...TT....TT...",
    // Screen 8: columns 128-143
    "................",
    "................",
    "................",
    "................",
    ".........().....",
    ".........TT.....",
    ".........TT.....",
    ".....()..TT.....",
    ".()..TT..TT..(--",
    ".TT..TT..TT....T",
    ".TT..TT..TT....T",
    ".TT..TT..TT....T",
    ".TT..TT..TT....T",
    // Screen 9: columns 144-159
    "................",
    "................",
    "................",
    "................",
    "..q.....o.......",
    "................",
    ".....().........",
    ".....TT......(--",
    "--)..TT..()...TT",
    "T....TT..TT...TT",
    "T....TT..TT...TT",
    "T....TT..TT...TT",
    "T....TT..TT...TT",
    // Screen 10: columns 160-175
    "................",
    "................",
    "...............b",
    "................",
    "..oob...........",
    "............()..",
    ".......r....TT..",
    ")...(----)..TT..",
    "......TT....TT..",
    "......TT....TT..",
    "......TT....TT..",
    "......TT....TT..",
    "......TT....TT..",
    // Screen 11: columns 176-191
    "..........^.....",
    "..........|.....",
    "..........|.....",
    "..f.......|.....",
    "(--)......|.....",
    ".TT.......|.....",
    ".TT.......|.....",
    ".TT.......|.....",
    ".TT.......X....f",
    ".TT....#########",
    ".TT....#########",
    ".TT....#########",
    ".TT....#########",
    // Screen 12: columns 192-207
    "HHHH............",
    "HHHH............",
    "HHHH............",
    "HHHH............",
    "HHHH............",
    "HHHH............",
    "DDDD............",
    "DDDD............",
    "DDDD............",
    "################",
    "################",
    "################",
    "################",
};

// The stage's own characters (StageDef.cell).
static u16 cell(char c, int mx, int my, u16* front) {
    switch (c) {
    case '(':
        return MT_CANOPY_LEFT;
    case ')':
        return MT_CANOPY_RIGHT;
    case 'T': { // its top branches out under the canopy
        char above = level_text_at(mx, my - 1);
        bool top = above == '(' || above == '-' || above == ')';
        return (u16)((top ? MT_TRUNK_TOP : MT_TRUNK) + level_run_left(mx, my) % 2);
    }
    case 'H':
        return (u16)(MT_GREAT_TRUNK + level_run_left(mx, my) % 4);
    case 'f':
        *front = MT_FRONDS;
        return MT_EMPTY;
    case 'b':
        level_add_spawn(SPAWN_BIRD, mx, my);
        return MT_EMPTY;
    case 'q':
        level_add_spawn(SPAWN_BURRS, mx, my);
        return MT_EMPTY;
    default:
        return MT_EMPTY;
    }
}

// --- Staying out of the gaps ------------------------------------------------
//
// Walking off a canopy means falling into a gap, so in the treetops
// caterpillars (the walkers) and the fish turn back where the next step
// would leave them over a bottomless drop, and a tree frog about to hop into
// one hops straight up instead. Off the end of a brick row, with a canopy
// below, they step off and fall as in the other stages.

#define HOP_DISTANCE 32 // how far a frog's hop carries it (objects.c: 32 frames at 1 pixel)

// True if world pixel (x, y), or anything below it in the level, has
// something to stand on (solid or one-way).
static bool floor_below(int x, int y) {
    for (; y < level_pixel_h; y += TILE) {
        if (MAP_TYPE(map_collision_at(x, y)) != MAP_EMPTY)
            return true;
    }
    return false;
}

// Something to land on under a body moved dx pixels sideways: under its left
// or right edge, from the row below its feet down.
static bool floor_under(u32 i, int dx) {
    int left = fx_to_int(pos_x[i]) + dx, below = fx_to_int(pos_y[i]) + body_h[i];
    return floor_below(left, below) || floor_below(left + body_w[i] - 1, below);
}

// A body walking along the floor whose next step leaves it over a drop with
// nothing below: turn it back. (objects_update() set its velocity, to
// obj_dir times its speed.)
static void turn_at_edge(u32 i) {
    if ((body_contact[i] & MAP_CONTACT_FLOOR) && !floor_under(i, obj_dir[i])) {
        obj_dir[i] = (s8)-obj_dir[i];
        vel_x[i] = -vel_x[i];
    }
}

static void keep_out_of_gaps(void) {
    ECS_FOR_EACH(i, C_ENEMY | C_MAPBODY) {
        if (obj_kind[i] == SPAWN_WALKER) {
            turn_at_edge(i);
        } else if (obj_kind[i] == SPAWN_HOPPER && (body_contact[i] & MAP_CONTACT_FLOOR) &&
                   vel_y[i] < 0 && !floor_under(i, obj_dir[i] * HOP_DISTANCE)) {
            vel_x[i] = 0; // a hop starting this frame would land in a gap
        }
    }
    ECS_FOR_EACH(i, C_FISH | C_MAPBODY) {
        turn_at_edge(i);
    }
}

// --- Birds ---------------------------------------------------------------------
//
// A bird comes in from the right with a chirp and flies left on a weaving
// path (path.h: sys_path turns it into velocity, sys_movement moves it): it
// dips 24 pixels and climbs back every 96 frames, so the serval can pass
// under it at the top of its weave or jump it at the bottom. It is an enemy
// like the others: land on it to stomp it, touch it otherwise and the serval
// is hurt. It flies on until it is far behind the camera.

#define C_BIRD C_STAGE(0)
#define BIRD_W 12
#define BIRD_H 10
#define BIRD_SPEED (FX_ONE * 5 / 4)

// Heading left, it turns counterclockwise (toward down) for 24 frames, then
// clockwise for 48 and counterclockwise for 24 again: it dips and climbs back
// to its height, a little over 100 pixels further left, and loops.
static const PathStep weave_steps[] = {
    {.frames = 24, .speed = BIRD_SPEED, .turn = -ANGLE_DEG(2)},
    {.frames = 48, .speed = BIRD_SPEED, .turn = ANGLE_DEG(2)},
    {.frames = 24, .speed = BIRD_SPEED, .turn = -ANGLE_DEG(2)},
};
static const Path weave = {PATH_STEPS(weave_steps), .loop = true, .heading = ANGLE_DEG(180)};

static void spawn_bird(const Spawn* s) {
    u32 i = object_create(C_VEL | C_BODY | C_ENEMY | C_ANIM | C_BIRD, SPR_BIRD,
                          s->mx * TILE + (TILE - BIRD_W) / 2, s->my * TILE + 3);
    if (i == MAX_ENT)
        return;
    body_w[i] = BIRD_W;
    body_h[i] = BIRD_H;
    body_gravity[i] = BODY_GRAVITY(0); // it flies: sys_physics leaves it be
    obj_kind[i] = SPAWN_BIRD;
    obj_dir[i] = -1;
    spr_flags[i] = SPRITE_FLIP_H; // the art faces right
    path_start(entity_at(i), &weave, 0);
    psg_play(SND_BIRD);
}

// --- Burrs -----------------------------------------------------------------------
//
// A burr tree ('q', unseen in the leaves above) drops a burr every
// BURR_PERIOD frames once it comes into view. A burr is a map body with a
// perfect bounce (body_bounce 255) and no limit on its fall speed: it rolls
// left toward the serval at a steady pace, bouncing back up to the height it
// dropped from on every landing, for as long as there is a canopy under it,
// then falls into the gap at the canopy's end. (Off a wall it would bounce
// back the same way.) It hurts the serval on any touch, and can't be stomped.

#define C_BURR_TREE C_STAGE(1)
#define C_BURR C_STAGE(2)
#define BURR_SIZE 8
#define BURR_SPEED (FX_ONE * 3 / 4)
#define BURR_FIRST 30 // frames from coming into view to the first burr
#define BURR_PERIOD 150

static void spawn_burr_tree(const Spawn* s) {
    Entity e = entity_create(C_POS | C_BURR_TREE); // no sprite: hidden in the leaves
    if (e == ENTITY_NONE)
        return;
    u32 i = entity_index(e);
    pos_x[i] = FX(s->mx * TILE + (TILE - BURR_SIZE) / 2);
    pos_y[i] = FX(s->my * TILE + 2);
    obj_timer[i] = BURR_FIRST;
}

static void drop_burr(u32 tree) {
    u32 i = object_create(C_VEL | C_BODY | C_MAPBODY | C_ANIM | C_BURR, SPR_BURR,
                          fx_to_int(pos_x[tree]), fx_to_int(pos_y[tree]));
    if (i == MAX_ENT)
        return;
    body_w[i] = body_h[i] = BURR_SIZE;
    body_bounce[i] = 255; // perfect: as high as it fell from, every time
    // (body_max_fall stays 0, no limit: a limit would take height away)
    vel_x[i] = -BURR_SPEED;
    psg_play(SND_BURR);
}

static void spawn(const Spawn* s) {
    if (s->kind == SPAWN_BIRD)
        spawn_bird(s);
    else if (s->kind == SPAWN_BURRS)
        spawn_burr_tree(s);
}

static void update(void) {
    keep_out_of_gaps();
    ECS_FOR_EACH(i, C_BURR_TREE) {
        if (--obj_timer[i] <= 0) {
            drop_burr(i);
            obj_timer[i] = BURR_PERIOD;
        }
    }
}

static void after_move(void) {
    ECS_FOR_EACH(i, C_BURR) {
        if (player_mode == PLAYER_NORMAL && body_overlap(player, i))
            player_hurt(); // (nothing while it blinks after being hurt)
    }
}

const StageDef stage_treetops = {
    .name = "1-3",
    .text = level_text,
    .screens = sizeof level_text / sizeof level_text[0] / 13,
    .height = 13,
    .time = 300,
    .tileset = &treetops_tileset,
    .metatiles = treetops_metatiles,
    .metatile_count = TT_MT_COUNT,
    .bonus_tile = TT_BONUS_TILE,
    .far_layer = &treetops_far_layer,
    .backdrop = TT_BACKDROP,
    .sprites = &treetops_group,
    .walker_sprite = SPR_CATERPILLAR,
    .walker_flat_sprite = SPR_CATERPILLAR_FLAT,
    .hopper_sprite = SPR_TREE_FROG,
    .song = &treetops_song,
    .hurry_tempo = 125, // from 100
    .cell = cell,
    .spawn = spawn,
    .update = update,
    .after_move = after_move,
};
