// Stage 1-4, the castle: grey stone halls over lava, with fire bars turning
// about their blocks, embers leaping out of the lava, salamanders and stone
// ledges; at the end the dragon on its bridge (boss.c). After it, the ending:
// the serval home in its den under the night sky.

#include "stage_castle.h"

// Legend: level.c's, plus
//   =  wall, ceiling          ~  lava (its surface, then lava, then deep lava)
//   w  an ember, in the deep lava under a pit
//   f  a fire bar turning clockwise, on its block; q  one turning the other way
//   t  a torch on the wall    h  a chain hanging from the ceiling
//   b  the dragon's bridge    L  the lever that drops it
//   Z  the dragon, standing on the bridge (its body's middle at the cell's
//      right edge)
// 'e' is a salamander, '-' a stone ledge, 'D' the gate out. Row 10 is the
// floor's surface; the serval stands on it in row 9. A pit's lava shows
// half a metatile below the floor, so the serval can stand on a pit's edge;
// it dies once it sinks into the row below.
static const char level_text[][16 + 1] = {
    // Screen 0: columns 0-15
    "================",
    "================",
    "================",
    "................",
    "................",
    "...t.......t....",
    "................",
    "........P.......",
    "................",
    "..s.............",
    "################",
    "################",
    "################",
    // Screen 1: columns 16-31
    "================",
    "================",
    "================",
    "................",
    "................",
    "..t.........t...",
    "................",
    "..BPBP..........",
    "................",
    "............e...",
    "#########~~#####",
    "#########~~#####",
    "#########~~#####",
    // Screen 2: columns 32-47
    "================",
    "================",
    "================",
    "................",
    "................",
    "...t........t...",
    "................",
    "................",
    ".......XX.......",
    ".......XX.......",
    "##~~~######~~~##",
    "##~~~######~~~##",
    "##~w~######~w~##",
    // Screen 3: columns 48-63
    "================",
    "================",
    "================",
    "................",
    "................",
    "..t..........t..",
    "................",
    "........f.......",
    "................",
    "............e...",
    "################",
    "################",
    "################",
    // Screen 4: columns 64-79
    "================",
    "================",
    "================",
    "................",
    "................",
    "................",
    "................",
    "......--........",
    "..--.......--...",
    "................",
    "#~~~~~~~~~~~~~##",
    "#~~~~~~~~~~~~~##",
    "#~~~~w~~~w~~~~##",
    // Screen 5: columns 80-95
    "================",
    "================",
    "================",
    "................",
    "................",
    "..t.........t...",
    "................",
    "....BGBFB.......",
    "................",
    ".c..........e.e.",
    "################",
    "################",
    "################",
    // Screen 6: columns 96-111
    "================",
    "================",
    "================",
    "................",
    "................",
    "................",
    "................",
    "..f...........q.",
    "................",
    "................",
    "#######~~~######",
    "#######~~~######",
    "#######~w~######",
    // Screen 7: columns 112-127
    "================",
    "================",
    "================",
    "....============",
    "....============",
    "....============",
    "................",
    "................",
    "................",
    "......e.....e...",
    "################",
    "################",
    "################",
    // Screen 8: columns 128-143
    "================",
    "================",
    "================",
    "===.............",
    "===.............",
    "===.............",
    "................",
    "................",
    "................",
    ".......XX...XX..",
    "#####~~XX~~~XX~~",
    "#####~~XX~~~XX~~",
    "#####~~XX~w~XX~~",
    // Screen 9: columns 144-159
    "================",
    "================",
    "================",
    "................",
    "................",
    "..t...ooo....t..",
    "................",
    "......q.........",
    "................",
    "................",
    "~###############",
    "~###############",
    "~###############",
    // Screen 10: columns 160-175
    "================",
    "================",
    "==========......",
    "................",
    "................",
    "..t.............",
    "................",
    "................",
    "................",
    "......e...####bb",
    "##############~~",
    "##############~~",
    "##############~~",
    // Screen 11: columns 176-191
    "================",
    "================",
    ".h.....h...=====",
    ".h.....h...=====",
    "...........=====",
    "...........=====",
    "............DDDD",
    "............DDDD",
    ".....Z....L.DDDD",
    "bbbbbbbbbb######",
    "~~~~~~~~~~######",
    "~~~~~~~~~~######",
    "~~~~~~~~~~######",
};

int bridge_first_mx SERVAL_EWRAM_BSS, bridge_last_mx SERVAL_EWRAM_BSS, bridge_my SERVAL_EWRAM_BSS;
int lever_mx SERVAL_EWRAM_BSS, lever_my SERVAL_EWRAM_BSS;

// --- Fire bars -------------------------------------------------------------------
//
// A chain of FIRE_BAR_BALLS balls of fire turning about the middle of its
// block, placed with the engine's trigonometry (fx_cos, fx_sin) and drawn by
// the stage's draw hook: they are not entities. Touching a ball hurts.

#define MAX_FIRE_BARS 8
#define FIRE_BAR_BALLS 5
#define FIRE_BAR_SPACING 8 // pixels between balls: the last one's middle 32 out
#define FIRE_BAR_TURN 273  // per frame: 1.5 degrees, a turn in 4 seconds (ANGLE_DEG(1.5))
#define FIRE_BALL_HIT 3    // half the side of a ball's square that hurts

typedef struct {
    s16 x, y;  // world position of the pivot (the block's middle)
    u16 angle; // where the bar points (math.h angles: clockwise on screen)
    s16 turn;  // angle per frame: positive clockwise
} FireBar;

// The stage's own state, in EWRAM: IWRAM is nearly full in debug builds.
static struct {
    FireBar bars[MAX_FIRE_BARS];
    int bar_count;
} castle SERVAL_EWRAM_BSS;

static void add_fire_bar(int mx, int my, int turn) {
    if (castle.bar_count == MAX_FIRE_BARS)
        return;
    castle.bars[castle.bar_count++] = (FireBar){
        .x = (s16)(mx * TILE + TILE / 2), .y = (s16)(my * TILE + TILE / 2), .turn = (s16)turn};
}

// Where ball k of a bar is: its middle, in world pixels.
static int ball_x(const FireBar* b, int k) {
    return b->x + ((fx_cos(b->angle) * k * FIRE_BAR_SPACING) >> FX_SHIFT);
}

static int ball_y(const FireBar* b, int k) {
    return b->y + ((fx_sin(b->angle) * k * FIRE_BAR_SPACING) >> FX_SHIFT);
}

static void turn_fire_bars(void) {
    for (int n = 0; n < castle.bar_count; n++)
        castle.bars[n].angle = (u16)(castle.bars[n].angle + castle.bars[n].turn);
}

static void touch_fire_bars(void) {
    int x = fx_to_int(pos_x[player]), y = fx_to_int(pos_y[player]);
    int w = body_w[player], h = body_h[player];
    for (int n = 0; n < castle.bar_count; n++) {
        const FireBar* b = &castle.bars[n];
        if (int_abs(b->x - x) > 3 * TILE)
            continue;
        for (int k = 0; k < FIRE_BAR_BALLS; k++) {
            int bx = ball_x(b, k), by = ball_y(b, k);
            if (bx + FIRE_BALL_HIT > x && bx - FIRE_BALL_HIT < x + w && by + FIRE_BALL_HIT > y &&
                by - FIRE_BALL_HIT < y + h) {
                player_hurt();
                return;
            }
        }
    }
}

static void draw_fire_bars(void) {
    u8 frame = (u8)((frame_count() / 4) % 2);
    for (int n = 0; n < castle.bar_count; n++) {
        const FireBar* b = &castle.bars[n];
        if (b->x < cam_x - 3 * TILE || b->x > cam_x + SCREEN_W + 3 * TILE)
            continue;
        for (int k = 0; k < FIRE_BAR_BALLS; k++)
            sprite_draw(SPR_FIREBALL, (u8)(frame ^ (k & 1)), ball_x(b, k) - cam_x,
                        ball_y(b, k) - cam_y, 0);
    }
}

// --- Embers ------------------------------------------------------------------------
//
// An ember rests on the deep lava under a pit, out of sight, then leaps up
// out of it and falls back, again and again. It is a map body drawn behind
// the playfield, so the lava hides it until it rises above the surface.
// Touching one hurts.

#define EMBER_W 8
#define EMBER_H 8
#define EMBER_LEAP FX(7)                  // rises about 100 pixels: 4 metatiles above the floor
#define EMBER_REST 60                     // frames in the lava between leaps
#define EMBER_HEARD (SCREEN_W / 2 + TILE) // leaps closer to the screen's middle are heard

static void spawn_ember(const Spawn* s) {
    u32 i = object_create(C_VEL | C_BODY | C_MAPBODY | C_ANIM | C_EMBER, SPR_EMBER,
                          s->mx * TILE + (TILE - EMBER_W) / 2, s->my * TILE - EMBER_H);
    if (i == MAX_ENT)
        return;
    body_w[i] = EMBER_W;
    body_h[i] = EMBER_H;
    body_max_fall[i] = FX(6);
    spr_flags[i] = SPRITE_BEHIND_PLAYFIELD;
    obj_timer[i] = (s16)(20 + (s->mx * 37) % 50); // embers side by side leap at different times
}

static void update_embers(void) {
    ECS_FOR_EACH(i, C_EMBER) {
        if (!(body_contact[i] & MAP_CONTACT_FLOOR) || --obj_timer[i] > 0)
            continue;
        vel_y[i] = -EMBER_LEAP;
        obj_timer[i] = EMBER_REST;
        if (int_abs(fx_to_int(pos_x[i]) - cam_x - SCREEN_W / 2) < EMBER_HEARD)
            psg_play(SND_LEAP);
    }
}

static void touch_embers(void) {
    ECS_FOR_EACH(i, C_EMBER) {
        if (body_overlap(player, i))
            player_hurt();
    }
}

// Enemies and fish walking into a pit sink into the lava and are gone.
static void burn_in_lava(u32 mask) {
    ECS_FOR_EACH(i, mask) {
        int x = fx_to_int(pos_x[i]), y = fx_to_int(pos_y[i]);
        if (map_tags_in(x, y, body_w[i], body_h[i]) & TAG_HAZARD) {
            spawn_sparkle(x + body_w[i] / 2 - 4, y);
            entity_destroy(entity_at(i));
        }
    }
}

// --- Hooks ---------------------------------------------------------------------------

// Before the level is built: the palettes, and the stage's lists (the cell
// hook fills them).
static void load(void) {
    castle_load_palettes();
    castle.bar_count = 0;
    bridge_first_mx = bridge_last_mx = bridge_my = -1;
    lever_mx = lever_my = -1;
}

// The stage's own characters (StageDef.cell).
static u16 cell(char c, int mx, int my, u16* front) {
    (void)front;
    switch (c) {
    case '=': { // a wall's or ceiling's lowest row has a carved edge
        char below = level_text_at(mx, my + 1);
        return below == '=' || below == '#' || below == 'D' ? MT_WALL : MT_WALL_EDGE;
    }
    case '~': {
        int depth = level_run_up(mx, my);
        return depth == 0 ? MT_LAVA_TOP : depth == 1 ? MT_LAVA : MT_LAVA_DEEP;
    }
    case 'w':
        level_add_spawn(SPAWN_EMBER, mx, my);
        return MT_LAVA_DEEP;
    case 'f':
    case 'q':
        add_fire_bar(mx, my, c == 'f' ? FIRE_BAR_TURN : -FIRE_BAR_TURN);
        return MT_FIREBAR;
    case 't':
        return MT_TORCH;
    case 'h':
        return MT_CHAIN;
    case 'b':
        if (bridge_first_mx < 0)
            bridge_first_mx = mx;
        bridge_last_mx = mx;
        bridge_my = my;
        return MT_BRIDGE;
    case 'L':
        lever_mx = mx;
        lever_my = my;
        return MT_LEVER;
    case 'Z':
        level_add_spawn(SPAWN_DRAGON, mx, my);
        return MT_EMPTY;
    default:
        return MT_EMPTY;
    }
}

static void start(void) {
    // The bars start where they always do, a quarter turn apart.
    for (int n = 0; n < castle.bar_count; n++)
        castle.bars[n].angle = (u16)(n * ANGLE_DEG(90));
    boss_reset();
}

static void spawn(const Spawn* s) {
    if (s->kind == SPAWN_EMBER)
        spawn_ember(s);
    else if (s->kind == SPAWN_DRAGON)
        boss_spawn(s);
}

static void update(void) {
    turn_fire_bars();
    update_embers();
    boss_update();
    if (boss_holds_serval())
        vel_x[player] = 0; // it stops to watch the bridge fall
}

static void after_move(void) {
    touch_fire_bars();
    touch_embers();
    burn_in_lava(C_ENEMY | C_MAPBODY);
    burn_in_lava(C_FISH | C_MAPBODY);
    boss_after_move();
}

// Lava: the serval sinks into it and is lost, big or small.
static void hazard(void) {
    player_die(false);
}

// The hall glows with the lava: the backdrop swells toward red every two
// seconds or so, flickering (color_mix), and flashes when the lever is
// pulled. The lava's surface rolls and the torches flicker
// (tileset_set_tiles).
static void effects(void) {
    u32 t = frame_count();
    int swell = (int)(t % 128);
    int glow = (swell < 64 ? swell : 127 - swell) * 3 + (int)((t * 7) % 24); // 0 to 212
    Color backdrop = color_mix(CASTLE_BACKDROP, CASTLE_GLOW, (u32)glow);
    boss_effects(&backdrop);
    screen_set_backdrop(backdrop);
    if (t % 8 == 0)
        tileset_set_tiles(LAVA_TILE, lava_tiles[(t / 8) % LAVA_FRAMES], 2);
    if (t % 6 == 0)
        tileset_set_tiles(TORCH_TILE, torch_tiles[(t / 6) % TORCH_FRAMES], 2);
}

static void draw(void) {
    draw_fire_bars();
}

// --- The ending ------------------------------------------------------------------------
//
// The savanna at night (the ending's two layers): the serval runs in from
// the left and into its den under the acacia, and the story's last lines
// appear over the stars, which twinkle (tileset_set_tiles). START ends it
// once the serval is home; otherwise the title comes back by itself.

#define ENDING_FRAMES 1080 // 18 seconds
#define ENDING_WALK_SPEED 1
#define TWINKLE_FRAMES 20

static struct {
    int timer;
    int serval_x; // the left of its body, on the screen
    bool home;
} ending SERVAL_EWRAM_BSS;

static void ending_start(void) {
    ending.timer = 0;
    ending.serval_x = -TILE;
    ending.home = false;
    screen_set_backdrop(NIGHT_SKY);
    camera_set(0, 0);
    map_load(&ending_far_layer);
    map_load(&ending_near_layer);
    psg_music_play(&ending_song);
}

// The story's lines, each shown from its frame on.
static const struct {
    u16 frame;
    u8 row;
    const char* text;
} ending_lines[] = {
    {40, 1, "THE BRIDGE FELL,"},        {80, 2, "AND THE DRAGON SANK"}, {120, 3, "INTO THE LAVA."},
    {260, 5, "THE SERVAL IS HOME,"},    {300, 6, "SAFE IN ITS DEN."},   {480, 10, "THE END"},
    {600, 18, "THANK YOU FOR PLAYING"},
};

static bool ending_update(void) {
    int t = ++ending.timer;
    for (u32 k = 0; k < sizeof ending_lines / sizeof ending_lines[0]; k++) {
        if (t == ending_lines[k].frame)
            text_print_centered(ending_lines[k].row, ending_lines[k].text);
    }
    if (t % TWINKLE_FRAMES == 0) {
        static const u8 order[4] = {0, 1, 0, 2};
        tileset_set_tiles(STAR_TILE, star_tiles[order[(t / TWINKLE_FRAMES) % 4]], STAR_TILE_COUNT);
    }
    // The serval, behind the near layer: the den's opening hides it.
    if (!ending.home) {
        ending.serval_x += ENDING_WALK_SPEED;
        u8 frame = (ending.serval_x / 6) % 2 ? SERVAL_RUN2 : SERVAL_RUN1;
        sprite_draw(SPR_SERVAL_SMALL, frame, ending.serval_x, ENDING_GROUND_Y - 15,
                    SPRITE_BEHIND_PLAYFIELD);
        ending.home = ending.serval_x + 6 >= ENDING_DEN_X;
    } else if (t % 30 == 0) {
        // Asleep: a "z" over the den, now small, now big.
        text_print(ENDING_DEN_X / 8 + 2, 13, (t / 30) % 2 ? "Z" : "z");
    }
    if (t >= ENDING_FRAMES || (ending.home && button_pressed(BUTTON_START))) {
        psg_music_stop();
        return true;
    }
    return false;
}

const StageDef stage_castle = {
    .name = "1-4",
    .text = level_text,
    .screens = sizeof level_text / sizeof level_text[0] / 13,
    .height = 13,
    .time = 300,
    .tileset = &castle_tileset,
    .metatiles = castle_metatiles,
    .metatile_count = CASTLE_MT_COUNT,
    .bonus_tile = CASTLE_BONUS_TILE,
    .far_layer = &castle_far_layer,
    .backdrop = CASTLE_BACKDROP,
    .sprites = &castle_group,
    .walker_sprite = SPR_SALAMANDER,
    .walker_flat_sprite = SPR_SALAMANDER_FLAT,
    .hopper_sprite = SPR_SALAMANDER, // (no hoppers here)
    .song = &castle_song,
    .hurry_tempo = 150, // from 120
    .load = load,
    .cell = cell,
    .start = start,
    .spawn = spawn,
    .update = update,
    .after_move = after_move,
    .hazard = hazard,
    .effects = effects,
    .draw = draw,
    .ending_start = ending_start,
    .ending_update = ending_update,
};
