// The game itself: the paddle, the balls, the bricks and the power-ups.
//
// Why the bricks are entities (and not cells of a map):
//   Each brick is an entity with a 16x8 body and a sprite; the ball asks
//   body_hit_side() which of its sides met a brick and bounces off that side.
//   The other way to build this game is a map: bricks as metatiles on the
//   playfield (background 2), removed with map_set_cell(), and the ball a map
//   body that sys_map_movement() stops against them. Entities won here:
//   - Bricks are 16x8, the classic shape; metatiles are 16x16, so map bricks
//     would be squares (or half-empty cells that still collide whole).
//   - The first level has 84 bricks, and map_set_cell() keeps at most
//     MAP_MAX_CHANGES (64) changed cells per room: the 65th broken brick
//     would stay solid. Entities have no such limit beyond the pool of 128.
//   - The ball keeps a constant speed. A map body loses speed at every wall,
//     ceiling and brick (body_bounce is at most 255/256), so the game would
//     have to restore it after each contact; and body_contact says which
//     side touched the map but not which cell, which the game would have to
//     work out from the position to break the right brick.
//   - body_hit_side() reports the brick and the side together, and a brick
//     can flash, crack or be destroyed like any other entity.
//   What a map would have done better: bricks would cost no sprites or
//   entities (here a full level uses most of the 128 hardware sprites and
//   entities, so effects are only created while there is room), and
//   sys_map_movement() moves in steps of at most 7 pixels, so no speed could
//   make the ball pass through a brick. Here that is guaranteed by the speed
//   limit instead (see BALL_SPEED_MAX).

#include "game.h"

// --- Tuning ------------------------------------------------------------------

#define MAX_BALLS 3
#define PADDLE_SPEED FX(3)
#define PADDLE_W_NORMAL 32
#define PADDLE_W_WIDE 48

// Ball speed in pixels per frame. It starts a little faster on each level,
// rises as bricks break and never exceeds BALL_SPEED_MAX: at 3.5 pixels per
// frame on any axis, a 6-pixel ball can't jump over an 8-pixel brick (that
// would take 14), so every brick it passes is found overlapping it.
#define BALL_SPEED_START FX(2)
#define BALL_SPEED_PER_LEVEL (FX_ONE / 8)
#define BALL_SPEED_LEVEL_MAX FX(3)
#define BALL_SPEED_MAX (FX(7) / 2)
#define BALL_SPEED_SLOW (FX(3) / 2) // after a SLOW capsule
#define BALL_SPEEDUP (FX_ONE / 16)  // every BRICKS_PER_SPEEDUP broken bricks
#define BRICKS_PER_SPEEDUP 8

// Directions are angles (clockwise from "right", 0x4000 is straight down), so
// the speed stays exactly the same whatever the ball bounces off. The paddle
// sends the ball off between 15 and 60 degrees from vertical, and no bounce
// may leave it flatter than MIN_TILT from horizontal: no endless
// side-to-side loops.
#define ANGLE_UP 0xC000
#define PADDLE_ANGLE_MIN 15 // degrees from vertical, at the paddle's center
#define PADDLE_ANGLE_SPAN 45
#define MIN_TILT ANGLE_DEG(25)
#define MULTI_SPREAD ANGLE_DEG(30)
// A ball that bounces this often off walls and gold bricks without touching
// the paddle or breaking a brick may be stuck in a loop: it gets a nudge.
#define DULL_BOUNCES 12
#define NUDGE ANGLE_DEG(8)

#define SERVE_HOLD 240             // frames a served ball waits before launching itself
#define CATCH_HOLD 150             // frames a caught ball waits
#define FLASH_FRAMES 6             // a hit brick that didn't break shows white
#define POWER_CHANCE 6             // one broken brick in this many drops a capsule...
#define MAX_POWERS 2               // ...while fewer than this many are falling
#define POWER_MAX_FALL (FX(3) / 2) // pixels per frame (the game's gravity is in game.c)
#define POWER_POINTS 100
// Effects (dust) are only created while this many entity slots are free, so
// bricks, balls and capsules always fit.
#define FREE_SLOTS_FOR_EFFECTS 8

// --- State -------------------------------------------------------------------

static u32 paddle; // entity slot
static int paddle_w;
static bool catching; // CATCH power-up: balls stick to the paddle
static int level_index;
static FIXED ball_speed;
static int bricks_broken; // this level, for the speed-ups
static int hold_timer;    // frames until held balls launch themselves
static int last_dir = 1;  // the paddle's last direction, for a ball held at its center

// Game component arrays (one entry per entity slot), as ecs.h suggests.
static u8 brick_kind[MAX_ENT];
static u8 brick_hits[MAX_ENT]; // hits left; 0 for gold (unbreakable)
static u8 brick_flash[MAX_ENT];
static u8 brick_row[MAX_ENT], brick_col[MAX_ENT];
static u16 ball_angle[MAX_ENT];
static u8 ball_dull[MAX_ENT];
static bool ball_held[MAX_ENT];
static s8 ball_hold_x[MAX_ENT]; // where on the paddle a held ball sits
static u8 power_kind[MAX_ENT];

// Which brick is in each cell of the level's grid (slot + 1; 0 for none), so a
// ball only tests the bricks around it, and a brick's neighbors are known.
static u8 grid[BRICK_ROWS][BRICK_COLS];

typedef struct {
    u8 palette; // drawn with SPRITE_PALETTE(palette)
    u8 frame;
    u8 hits; // 0: unbreakable
    u16 points;
    u16 sound; // when it breaks
} BrickInfo;

static const BrickInfo brick_info[] = {
    [BRICK_RED] = {PAL_RED, BRICK_FRAME_PLAIN, 1, 10, SND_BRICK_RED},
    [BRICK_ORANGE] = {PAL_ORANGE, BRICK_FRAME_PLAIN, 1, 20, SND_BRICK_ORANGE},
    [BRICK_YELLOW] = {PAL_YELLOW, BRICK_FRAME_PLAIN, 1, 30, SND_BRICK_YELLOW},
    [BRICK_GREEN] = {PAL_GREEN, BRICK_FRAME_PLAIN, 1, 40, SND_BRICK_GREEN},
    [BRICK_BLUE] = {PAL_BLUE, BRICK_FRAME_PLAIN, 1, 50, SND_BRICK_BLUE},
    [BRICK_PURPLE] = {PAL_PURPLE, BRICK_FRAME_PLAIN, 1, 60, SND_BRICK_PURPLE},
    [BRICK_SILVER] = {PAL_SILVER, BRICK_FRAME_SILVER, 2, 100, SND_BRICK_PURPLE},
    [BRICK_GOLD] = {PAL_GOLD, BRICK_FRAME_GOLD, 0, 0, SND_CLANK},
};

// --- Angles ------------------------------------------------------------------

static bool going_up(u16 a) {
    return fx_sin(a) < 0;
}

static bool going_right(u16 a) {
    return fx_cos(a) > 0;
}

// Mirrors a direction: horizontally (off a side wall) or vertically.
static u16 flip_x(u16 a) {
    return (u16)(0x8000 - a);
}

static u16 flip_y(u16 a) {
    return (u16)(0x10000 - a);
}

// Keeps a direction at least MIN_TILT away from horizontal (0 and 0x8000).
static u16 steepen(u16 a) {
    u16 half = a & 0x7FFF; // 0 and 0x8000 are both horizontal
    if (half < MIN_TILT)
        return (u16)((a & 0x8000) | MIN_TILT);
    if (half > 0x8000 - MIN_TILT)
        return (u16)((a & 0x8000) | (0x8000 - MIN_TILT));
    return a;
}

static void set_velocity(u32 b) {
    vel_x[b] = fx_mul(fx_cos(ball_angle[b]), ball_speed);
    vel_y[b] = fx_mul(fx_sin(ball_angle[b]), ball_speed);
}

static void set_angle(u32 b, u16 a) {
    ball_angle[b] = steepen(a);
    set_velocity(b);
}

// The direction off the paddle: straighter near its center, up to 60 degrees
// from vertical at its ends. offset: the ball's center relative to the
// paddle's, in pixels.
static u16 paddle_angle(int offset) {
    int reach = paddle_w / 2 + BALL_SIZE / 2;
    int dir = offset > 0 ? 1 : offset < 0 ? -1 : last_dir;
    int degrees = PADDLE_ANGLE_MIN + PADDLE_ANGLE_SPAN * int_min(int_abs(offset), reach) / reach;
    return (u16)(ANGLE_UP + dir * ANGLE_DEG(degrees));
}

// --- Creating things ---------------------------------------------------------

static int paddle_x(void) {
    return fx_to_int(pos_x[paddle]);
}

static u32 create_ball(FIXED x, FIXED y) {
    Entity e = entity_create(C_POS | C_VEL | C_SPR | C_BODY | C_BALL);
    if (e == ENTITY_NONE)
        return MAX_ENT;
    u32 b = entity_index(e);
    pos_x[b] = x;
    pos_y[b] = y;
    spr_id[b] = SPR_BALL;
    // In front of the bricks: among sprites on one layer, lower slots are in
    // front, and balls are created after the bricks.
    spr_flags[b] = SPRITE_ABOVE_FOREGROUND;
    body_w[b] = body_h[b] = BALL_SIZE;
    body_gravity[b] = BODY_GRAVITY(0); // flies straight while capsules fall
    ball_dull[b] = 0;
    ball_held[b] = false;
    ball_angle[b] = ANGLE_UP;
    return b;
}

// A ball waiting on the paddle (a serve, or a CATCH).
static void hold_ball(u32 b, int hold_x, int frames) {
    ball_held[b] = true;
    ball_hold_x[b] = (s8)int_clamp(hold_x, 0, paddle_w - BALL_SIZE);
    vel_x[b] = vel_y[b] = 0;
    hold_timer = frames;
}

// Removes balls, capsules and dust. ECS_FOR_EACH matches entities with every
// bit of its mask, so each kind takes a loop.
static void remove_flying(void) {
    ECS_FOR_EACH(i, C_BALL) {
        entity_destroy(entity_at(i));
    }
    ECS_FOR_EACH(i, C_POWER) {
        entity_destroy(entity_at(i));
    }
    ECS_FOR_EACH(i, C_BURST) {
        entity_destroy(entity_at(i));
    }
}

static void spawn_burst(u32 brick) {
    if (ecs_free_count() < FREE_SLOTS_FOR_EFFECTS)
        return;
    Entity e = entity_create(C_POS | C_SPR | C_ANIM | C_BURST);
    u32 i = entity_index(e);
    pos_x[i] = pos_x[brick];
    pos_y[i] = pos_y[brick];
    spr_id[i] = SPR_BURST;
}

static void spawn_power(u32 brick) {
    if (random_range(1, POWER_CHANCE) != 1 || ecs_count(C_POWER) >= MAX_POWERS ||
        ecs_free_count() < FREE_SLOTS_FOR_EFFECTS)
        return;
    // Extra lives are rarer than the rest.
    static const u8 bag[] = {POWER_WIDE, POWER_WIDE,  POWER_MULTI, POWER_MULTI, POWER_SLOW,
                             POWER_SLOW, POWER_CATCH, POWER_CATCH, POWER_LIFE};
    u8 kind = bag[random_range(0, (int)sizeof bag - 1)];
    if (kind == POWER_MULTI && ecs_count(C_BALL) >= MAX_BALLS)
        kind = POWER_WIDE;
    // A body, so sys_physics pulls it down with the game's gravity (the
    // balls have none of it) and reports when it leaves through the bottom.
    Entity e = entity_create(C_POS | C_VEL | C_SPR | C_ANIM | C_BODY | C_POWER);
    u32 i = entity_index(e);
    pos_x[i] = pos_x[brick];
    pos_y[i] = pos_y[brick];
    vel_y[i] = -FX(1); // a little hop out of the brick, then it falls
    spr_id[i] = (u16)(SPR_CAPSULE_WIDE + kind);
    spr_flags[i] = SPRITE_ABOVE_FOREGROUND; // in front of the bricks, like the balls
    body_w[i] = BRICK_W;
    body_h[i] = BRICK_H;
    body_max_fall[i] = POWER_MAX_FALL;
    power_kind[i] = kind;
}

void play_start_level(int level) {
    ecs_reset();
    level_index = level;
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            grid[r][c] = 0;
    const Level* l = &levels[level % LEVEL_COUNT];
    for (int r = 0; r < BRICK_ROWS && l->rows[r]; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            BrickKind kind = brick_kind_of(l->rows[r][c]);
            if (kind == BRICK_NONE)
                continue;
            u32 mask = C_POS | C_SPR | C_BRICK | (kind == BRICK_GOLD ? 0 : C_BREAKABLE);
            u32 i = entity_index(entity_create(mask));
            pos_x[i] = FX(FIELD_LEFT + c * BRICK_W);
            pos_y[i] = FX(BRICKS_TOP + r * BRICK_H);
            body_w[i] = BRICK_W;
            body_h[i] = BRICK_H;
            spr_id[i] = SPR_BRICK;
            spr_frame[i] = brick_info[kind].frame;
            spr_flags[i] = SPRITE_PALETTE(brick_info[kind].palette);
            brick_kind[i] = (u8)kind;
            brick_hits[i] = brick_info[kind].hits;
            brick_flash[i] = 0;
            brick_row[i] = (u8)r;
            brick_col[i] = (u8)c;
            grid[r][c] = (u8)(i + 1);
        }
    }
    // The paddle has a velocity (set from the buttons each frame and applied
    // by sys_movement) so body_hit_side() sees it move. It is no C_BODY:
    // sys_physics leaves it alone, and play.c keeps it inside the walls.
    paddle = entity_index(entity_create(C_POS | C_VEL));
    bricks_broken = 0;
    play_serve();
}

void play_serve(void) {
    remove_flying();
    paddle_w = PADDLE_W_NORMAL;
    catching = false;
    pos_x[paddle] = FX((SCREEN_W - paddle_w) / 2);
    pos_y[paddle] = FX(PADDLE_Y);
    vel_x[paddle] = 0;
    body_w[paddle] = (u8)paddle_w;
    body_h[paddle] = PADDLE_H;
    ball_speed =
        int_min(BALL_SPEED_START + level_index * BALL_SPEED_PER_LEVEL, BALL_SPEED_LEVEL_MAX);
    u32 b = create_ball(0, 0);
    hold_ball(b, paddle_w / 2 - BALL_SIZE / 2 + 4, SERVE_HOLD);
}

bool play_holding_ball(void) {
    ECS_FOR_EACH(i, C_BALL) {
        if (ball_held[i])
            return true;
    }
    return false;
}

static void launch(void) {
    bool any = false;
    ECS_FOR_EACH(b, C_BALL) {
        if (!ball_held[b])
            continue;
        ball_held[b] = false;
        int offset = ball_hold_x[b] + BALL_SIZE / 2 - paddle_w / 2;
        set_angle(b, paddle_angle(offset));
        any = true;
    }
    if (any)
        psg_play(SND_LAUNCH);
}

// --- Power-ups ---------------------------------------------------------------

static void set_paddle_width(int w) {
    int grow = w - paddle_w;
    paddle_w = w;
    body_w[paddle] = (u8)w;
    int x = int_clamp(paddle_x() - grow / 2, FIELD_LEFT, FIELD_RIGHT - w);
    pos_x[paddle] = FX(x);
    ECS_FOR_EACH(b, C_BALL) {
        if (ball_held[b])
            ball_hold_x[b] = (s8)(ball_hold_x[b] + grow / 2);
    }
}

static void split_balls(void) {
    launch(); // a held ball leaves first, so the new ones fly with it
    u32 first = MAX_ENT;
    ECS_FOR_EACH(b, C_BALL) {
        first = b;
        break;
    }
    if (first == MAX_ENT)
        return;
    int count = (int)ecs_count(C_BALL);
    for (int k = 0; count < MAX_BALLS; k++, count++) {
        u32 b = create_ball(pos_x[first], pos_y[first]);
        if (b == MAX_ENT)
            return;
        set_angle(b, (u16)(ball_angle[first] + (k % 2 ? -MULTI_SPREAD : MULTI_SPREAD)));
    }
}

static void apply_power(PowerKind kind) {
    switch (kind) {
    case POWER_WIDE:
        set_paddle_width(PADDLE_W_WIDE);
        break;
    case POWER_MULTI:
        split_balls();
        break;
    case POWER_SLOW:
        ball_speed = BALL_SPEED_SLOW;
        break;
    case POWER_CATCH:
        catching = true;
        break;
    case POWER_LIFE:
    case POWER_COUNT:
        break;
    }
    add_score(POWER_POINTS);
    if (kind == POWER_LIFE)
        add_life(); // plays its own jingle
    else
        psg_play(SND_POWER_UP);
}

static void update_powers(void) {
    ECS_FOR_EACH(i, C_POWER) {
        if (body_overlap(i, paddle)) {
            apply_power((PowerKind)power_kind[i]);
            entity_destroy(entity_at(i));
        } else if (body_contact[i] & BODY_CONTACT_EXIT) { // fell out of the bottom
            entity_destroy(entity_at(i));
        }
    }
}

// --- Bricks ------------------------------------------------------------------

static u32 brick_at(int row, int col) {
    if (row < 0 || row >= BRICK_ROWS || col < 0 || col >= BRICK_COLS || !grid[row][col])
        return MAX_ENT;
    return grid[row][col] - 1u;
}

// body_hit_side() judges two bodies on their own. In a wall of bricks, the
// side a ball "hit" can be a face shared with a neighbor: a ball going up a
// column of bricks along their sides grazes the bottom corner of the one
// above, and would bounce down off a face it can't reach. The neighbor
// reports the real side, so a hidden face is ignored here (0).
static u32 exposed_side(u32 brick, u32 side) {
    int r = brick_row[brick], c = brick_col[brick];
    switch (side) {
    case BODY_SIDE_TOP: // the ball's top met the brick's bottom face
        return brick_at(r + 1, c) == MAX_ENT ? side : 0;
    case BODY_SIDE_BOTTOM:
        return brick_at(r - 1, c) == MAX_ENT ? side : 0;
    case BODY_SIDE_LEFT: // the ball's left met the brick's right face
        return brick_at(r, c + 1) == MAX_ENT ? side : 0;
    case BODY_SIDE_RIGHT:
        return brick_at(r, c - 1) == MAX_ENT ? side : 0;
    default:
        return side;
    }
}

static void break_brick(u32 i) {
    const BrickInfo* info = &brick_info[brick_kind[i]];
    add_score(info->points);
    psg_play(info->sound);
    grid[brick_row[i]][brick_col[i]] = 0;
    spawn_burst(i);
    spawn_power(i);
    entity_destroy(entity_at(i));
    if (++bricks_broken % BRICKS_PER_SPEEDUP == 0)
        ball_speed = int_min(ball_speed + BALL_SPEEDUP, BALL_SPEED_MAX);
}

// A ball hit brick i. Returns true if the brick broke.
static bool hit_brick(u32 i) {
    if (brick_hits[i] == 0) { // gold
        brick_flash[i] = FLASH_FRAMES;
        psg_play(SND_CLANK);
        return false;
    }
    if (--brick_hits[i] > 0) { // silver, first hit
        spr_frame[i] = BRICK_FRAME_CRACKED;
        brick_flash[i] = FLASH_FRAMES;
        psg_play(SND_CRACK);
        return false;
    }
    break_brick(i);
    return true;
}

// Bounces ball b off the bricks it overlaps, after sys_movement. Every brick
// it touches is asked for a side first (body_hit_side compares positions
// before and after this frame's movement, so velocities must not change
// until all are asked); then the ball bounces once per axis, is put against
// the face it hit, and the brick nearest to its center takes the hit.
static void collide_bricks(u32 b) {
    // The cells under the ball. Its far edges are found from the exact
    // position: a ball at x = 18.1 covers pixels up to 24.1, so it reaches
    // the column starting at 24.
    int bx = fx_to_int(pos_x[b]), by = fx_to_int(pos_y[b]);
    int right = fx_to_int(pos_x[b] + FX(BALL_SIZE) - 1),
        bottom = fx_to_int(pos_y[b] + FX(BALL_SIZE) - 1);
    int col0 = (bx - FIELD_LEFT) >> 4, col1 = (right - FIELD_LEFT) >> 4;
    int row0 = (by - BRICKS_TOP) >> 3, row1 = (bottom - BRICKS_TOP) >> 3;

    u32 side_x = 0, side_y = 0;
    FIXED face_x = 0, face_y = 0; // where the ball goes to touch the face it hit
    u32 target = MAX_ENT;
    int best = 0x7FFFFFFF;
    for (int r = row0; r <= row1; r++) {
        for (int c = col0; c <= col1; c++) {
            u32 i = brick_at(r, c);
            if (i == MAX_ENT)
                continue;
            u32 side = body_hit_side(b, i);
            if (side == BODY_SIDE_INSIDE) {
                // It was already inside last frame (it can't get there
                // through an exposed face, but it would if a game moved a
                // ball onto a brick). Send it back the way it came.
                side = vel_y[b] < 0 ? BODY_SIDE_TOP : BODY_SIDE_BOTTOM;
            } else {
                side = exposed_side(i, side);
            }
            if (!side)
                continue;
            if (side & (BODY_SIDE_TOP | BODY_SIDE_BOTTOM)) {
                side_y = side;
                face_y = side == BODY_SIDE_TOP ? pos_y[i] + FX(BRICK_H) : pos_y[i] - FX(BALL_SIZE);
            } else {
                side_x = side;
                face_x = side == BODY_SIDE_LEFT ? pos_x[i] + FX(BRICK_W) : pos_x[i] - FX(BALL_SIZE);
            }
            int d = int_abs(bx * 2 + BALL_SIZE - (c * BRICK_W * 2 + FIELD_LEFT * 2 + BRICK_W)) +
                    int_abs(by * 2 + BALL_SIZE - (r * BRICK_H * 2 + BRICKS_TOP * 2 + BRICK_H)) * 2;
            if (d < best) {
                best = d;
                target = i;
            }
        }
    }
    if (target == MAX_ENT)
        return;

    u16 a = ball_angle[b];
    if (side_y) {
        pos_y[b] = face_y;
        if (going_up(a) == (side_y == BODY_SIDE_TOP))
            a = flip_y(a);
    }
    if (side_x) {
        pos_x[b] = face_x;
        if (going_right(a) == (side_x == BODY_SIDE_RIGHT))
            a = flip_x(a);
    }
    set_angle(b, a);
    if (hit_brick(target))
        ball_dull[b] = 0;
    else
        ball_dull[b]++;
}

static void update_brick_flashes(void) {
    ECS_FOR_EACH(i, C_BRICK) {
        if (brick_flash[i] > 0)
            brick_flash[i]--;
        spr_flags[i] =
            SPRITE_PALETTE(brick_flash[i] ? PAL_FLASH : brick_info[brick_kind[i]].palette);
    }
}

// --- Paddle and balls --------------------------------------------------------

static void control_paddle(void) {
    FIXED dx = 0;
    if (button_down(BUTTON_LEFT))
        dx -= PADDLE_SPEED;
    if (button_down(BUTTON_RIGHT))
        dx += PADDLE_SPEED;
    if (dx)
        last_dir = dx > 0 ? 1 : -1;
    FIXED x = int_clamp(pos_x[paddle] + dx, FX(FIELD_LEFT), FX(FIELD_RIGHT - paddle_w));
    vel_x[paddle] = x - pos_x[paddle]; // sys_movement moves it
}

// Bounces ball b off the paddle, after sys_movement. A ball that came down
// onto the top (BODY_SIDE_BOTTOM: the ball's bottom met the paddle) bounces
// up at an angle set by where it landed. The paddle's ends are generous: a
// ball that clips an end, or that the moving paddle runs into, while its
// bottom is no more than 4 pixels below the top surface still bounces up.
// Lower than that it is knocked aside and falls.
static void collide_paddle(u32 b) {
    if (vel_y[b] <= 0)
        return; // going up: already bounced
    u32 side = body_hit_side(b, paddle);
    if (!side)
        return;
    int ball_bottom = fx_to_int(pos_y[b]) + BALL_SIZE;
    int offset = fx_to_int(pos_x[b]) + BALL_SIZE / 2 - (paddle_x() + paddle_w / 2);
    if (side == BODY_SIDE_BOTTOM || ball_bottom - PADDLE_Y <= 4) {
        pos_y[b] = FX(PADDLE_Y - BALL_SIZE);
        ball_dull[b] = 0;
        if (catching) {
            hold_ball(b, fx_to_int(pos_x[b]) - paddle_x(), CATCH_HOLD);
            psg_play(SND_CATCH);
        } else {
            set_angle(b, paddle_angle(offset));
            psg_play(SND_PADDLE);
        }
        return;
    }
    // Knocked aside by an end: away from the paddle, still falling.
    u16 a = ball_angle[b];
    if (going_right(a) != (offset > 0))
        a = flip_x(a);
    pos_x[b] = offset > 0 ? pos_x[paddle] + FX(paddle_w) : pos_x[paddle] - FX(BALL_SIZE);
    set_angle(b, a);
    psg_play(SND_WALL);
}

// sys_physics bounced balls off the frame by flipping their velocity, and
// body_contact says which walls they touched: the angle follows. A ball
// bouncing for a long time without progress is nudged.
static void after_physics(u32 b) {
    u32 touched = body_contact[b];
    if (!(touched & (BODY_SIDE_LEFT | BODY_SIDE_RIGHT | BODY_SIDE_TOP)))
        return;
    u16 a = ball_angle[b];
    if (touched & (BODY_SIDE_LEFT | BODY_SIDE_RIGHT))
        a = flip_x(a);
    if (touched & BODY_SIDE_TOP)
        a = flip_y(a);
    psg_play(SND_WALL);
    if (++ball_dull[b] >= DULL_BOUNCES) {
        ball_dull[b] = 0;
        a = (u16)(a + (random_range(0, 1) ? NUDGE : -NUDGE));
    }
    set_angle(b, a);
}

// --- A frame -----------------------------------------------------------------

PlayResult play_update(void) {
    control_paddle();
    if (button_pressed(BUTTON_A) || (play_holding_ball() && --hold_timer <= 0))
        launch();
    ECS_FOR_EACH(b, C_BALL) {
        if (!ball_held[b])
            set_velocity(b); // the speed may have changed (speed-ups, SLOW)
    }
    update_powers();

    sys_movement(); // paddle, balls, capsules
    ECS_FOR_EACH(b, C_BALL) {
        if (!ball_held[b]) {
            collide_bricks(b);
            collide_paddle(b);
        }
    }
    sys_physics(); // balls off the frame (the bottom is open), capsules fall
    ECS_FOR_EACH(b, C_BALL) {
        if (ball_held[b]) {
            pos_x[b] = pos_x[paddle] + FX(ball_hold_x[b]);
            pos_y[b] = FX(PADDLE_Y - BALL_SIZE);
        } else if (body_contact[b] & BODY_CONTACT_EXIT) { // lost out of the bottom
            entity_destroy(entity_at(b));
        } else {
            after_physics(b);
        }
    }

    ECS_FOR_EACH(i, C_BURST) {
        if (spr_frame[i] == BURST_LAST_FRAME)
            entity_destroy(entity_at(i));
    }
    sys_animate(); // capsules glint, dust spreads
    update_brick_flashes();

    // When the last ball is lost or the last brick breaks, game.c stops
    // calling play_update() for a while: whatever still flies would hang in
    // the air, so it goes.
    if (ecs_count(C_BALL) == 0) {
        remove_flying();
        return PLAY_BALL_LOST;
    }
    if (ecs_count(C_BREAKABLE) == 0) {
        remove_flying();
        return PLAY_CLEARED;
    }
    return PLAY_ON;
}

void play_draw(void) {
    sys_render(); // bricks, balls, capsules, dust
    // The paddle, in pieces drawn by hand: two ends, and a middle when wide.
    // Drawn after sys_render, so balls are in front of it. It glows (cyan
    // spots) while it catches balls.
    u16 glow = catching ? SPRITE_PALETTE(PAL_CATCH) : 0;
    int x = paddle_x();
    sprite_draw(SPR_PADDLE_LEFT, 0, x, PADDLE_Y, glow);
    if (paddle_w == PADDLE_W_WIDE)
        sprite_draw(SPR_PADDLE_MIDDLE, 0, x + 16, PADDLE_Y, glow);
    sprite_draw(SPR_PADDLE_RIGHT, 0, x + paddle_w - 16, PADDLE_Y, glow);
}
