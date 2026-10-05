// bunnymark's game: the bunny sprites, their physics and the HUD. See main.c
// for what the example demonstrates and what to expect when it runs.

#include "bunnymark.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_BUNNY_WHITE, SPR_BUNNY_GOLD, SPR_BUNNY_BLUE, SPR_BUNNY_GREEN, SPRITE_COUNT };

#define BUNNY_SIZE 16

// A 16x16 bunny, 4 bits per pixel, as four 8x8 tiles (top-left, top-right,
// bottom-left, bottom-right). Generated from this art:
//
//   ................    . transparent (color 0)
//   ...KK.....KK....    W fur         (color 1)
//   ..KWWK...KWWK...    P pink        (color 2)
//   ..KWPK...KPWK...    K outline     (color 3)
//   ..KWPK...KPWK...
//   ..KWPK...KPWK...
//   ...KWWK.KWWK....
//   ...KWWWKWWWK....
//   ..KWWWWWWWWWK...
//   ..KWKWWWWWKWK...
//   .KWWWWWPWWWWWK..
//   .KWWWWWWWWWWWK..
//   ..KWWWWWWWWWK...
//   ..KWWKKKKKWWK...
//   ...KK.....KK....
//   ................
static const u32 bunny_tiles[32] = {
    0x00000000, 0x00033000, 0x00311300, 0x00321300, 0x00321300, 0x00321300, 0x03113000, 0x31113000,
    0x00000000, 0x00003300, 0x00031130, 0x00031230, 0x00031230, 0x00031230, 0x00003113, 0x00003111,
    0x11111300, 0x11131300, 0x21111130, 0x11111130, 0x11111300, 0x33311300, 0x00033000, 0x00000000,
    0x00031111, 0x00031311, 0x00311111, 0x00311111, 0x00031111, 0x00031133, 0x00003300, 0x00000000,
};

// One palette per bunny color; only the fur color differs.
#define BUNNY_PALETTE(r, g, b)                                                                     \
    {0, COLOR_RGB(r, g, b), COLOR_RGB(255, 140, 170), COLOR_RGB(40, 30, 50)}

static const u16 bunny_palettes[SPRITE_COUNT][16] = {
    [SPR_BUNNY_WHITE] = BUNNY_PALETTE(255, 255, 255),
    [SPR_BUNNY_GOLD] = BUNNY_PALETTE(255, 210, 90),
    [SPR_BUNNY_BLUE] = BUNNY_PALETTE(140, 200, 255),
    [SPR_BUNNY_GREEN] = BUNNY_PALETTE(150, 240, 170),
};

#define BUNNY_SPRITE(color) {.size = SPRITE_16x16, .tiles = bunny_tiles, .palette_slot = (color)}

static const SpriteAsset bunny_sprites[SPRITE_COUNT] = {
    BUNNY_SPRITE(SPR_BUNNY_WHITE),
    BUNNY_SPRITE(SPR_BUNNY_GOLD),
    BUNNY_SPRITE(SPR_BUNNY_BLUE),
    BUNNY_SPRITE(SPR_BUNNY_GREEN),
};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    &bunny_sprites[0],
    &bunny_sprites[1],
    &bunny_sprites[2],
    &bunny_sprites[3],
};

// Every sprite in the table (no sprite_ids), one palette each.
static const SpriteGroup bunny_group = {
    .palettes = &bunny_palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = SPRITE_COUNT,
};

// --- Game --------------------------------------------------------------------

// Game-defined component: marks entities the bunny physics system handles.
#define C_BUNNY C_GAME(0)

#define HUD_HEIGHT 24 // three text rows; bunnies stay below them

// Like raylib's bunnymark: up to 250 pixels per second each way, at 60 fps.
#define MAX_SPEED (FX(250) / 60)

// Added to a bunny's velocity every frame, in the direction of gravity.
#define GRAVITY (FX_ONE / 4)

// A bounce against the wall gravity pulls toward keeps 7/8 of the speed; below
// this speed the bunny stops instead, so it rests without jittering.
#define REST_SPEED (GRAVITY * 2)

// While touching that wall, friction removes 1/8 of the speed along it each
// frame, stopping the bunny below this speed.
#define STOP_SPEED (FX_ONE / 16)

static Entity bunnies[MAX_ENT];
static int count;

static int gravity_x = 0, gravity_y = 1; // starts pointing down

void bunnymark_init(void) {
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&bunny_group);
    screen_set_backdrop(COLOR_RGB(24, 24, 40));
    text_clear(); // sets up the text layer now, not during the first frame
}

void bunny_add(void) {
    if (count == MAX_ENT)
        return;
    Entity e = entity_create(C_POS | C_VEL | C_SPR | C_BUNNY);
    if (e == ENTITY_NONE)
        return;
    u32 i = entity_index(e);
    pos_x[i] = FX((screen_width() - BUNNY_SIZE) / 2);
    pos_y[i] = FX(HUD_HEIGHT);
    vel_x[i] = random_range(-MAX_SPEED, MAX_SPEED);
    vel_y[i] = random_range(-MAX_SPEED, MAX_SPEED);
    spr_id[i] = (u16)random_range(0, SPRITE_COUNT - 1);
    bunnies[count++] = e;
}

void bunny_remove(void) {
    if (count > 0)
        entity_destroy(bunnies[--count]);
}

int bunny_count(void) {
    return count;
}

void gravity_set(int x, int y) {
    gravity_x = x;
    gravity_y = y;
}

// Speed after bouncing off the wall gravity pulls toward: 7/8 of `speed`, or
// 0 once the bunny is slow enough to rest.
static FIXED floor_bounce(FIXED speed) {
    speed -= speed >> 3; // 7/8: a cheap fx_mul(speed, FX(7) / 8)
    return speed < REST_SPEED ? 0 : speed;
}

// Friction while sliding along the wall gravity pulls toward.
static FIXED friction(FIXED speed) {
    speed -= speed >> 3;
    return (speed < STOP_SPEED && speed > -STOP_SPEED) ? 0 : speed;
}

// Bounces one axis of a bunny off the ends of [lo, hi], then applies that
// axis's gravity (-1, 0 or 1). Returns true if the bunny is on the wall that
// gravity pulls toward (its floor), bouncing or resting.
//
// - A bunny that moved past a wall is mirrored back inside by the distance it
//   overshot, as if it had bounced mid-frame. (Snapping it onto the wall would
//   lift it a little on every bounce and keep it hopping forever.)
// - Gravity is applied after the bounce, so it slows the rebound rather than
//   adding to it.
// - Bounces off the floor lose speed (floor_bounce). Once too slow, the bunny
//   rests: it sits exactly on the floor with zero speed, and the floor cancels
//   gravity, so it stays put even if gravity is switched off.
static bool update_axis(FIXED* pos, FIXED* vel, FIXED lo, FIXED hi, int gravity) {
    bool at_lo = *pos <= lo && *vel <= 0;
    bool at_hi = *pos >= hi && *vel >= 0;
    bool on_floor = (at_lo && gravity < 0) || (at_hi && gravity > 0);

    if (at_lo || at_hi) {
        FIXED wall = at_lo ? lo : hi;
        FIXED speed = *vel < 0 ? -*vel : *vel;
        if (on_floor) {
            speed = floor_bounce(speed);
            if (speed == 0) {
                *pos = wall;
                *vel = 0;
                return true;
            }
        }
        *pos = 2 * wall - *pos;
        if (*pos < lo)
            *pos = lo;
        if (*pos > hi)
            *pos = hi;
        *vel = at_lo ? speed : -speed;
    }

    *vel += gravity * GRAVITY;
    return on_floor;
}

// Game system: bounces every bunny off the screen edges and the bottom of the
// HUD, applies gravity, and slows bunnies down while they slide along a floor.
static void bunny_physics(void) {
    const FIXED max_x = FX(screen_width() - BUNNY_SIZE);
    const FIXED min_y = FX(HUD_HEIGHT);
    const FIXED max_y = FX(screen_height() - BUNNY_SIZE);
    ECS_FOR_EACH(i, C_BUNNY) {
        if (update_axis(&pos_x[i], &vel_x[i], 0, max_x, gravity_x))
            vel_y[i] = friction(vel_y[i]);
        if (update_axis(&pos_y[i], &vel_y[i], min_y, max_y, gravity_y))
            vel_x[i] = friction(vel_x[i]);
    }
}

static const char* gravity_name(void) {
    static const char* const names[3][3] = {
        // [gravity_y + 1][gravity_x + 1]
        {"UP-LEFT", "UP", "UP-RIGHT"},
        {"LEFT", "OFF", "RIGHT"},
        {"DOWN-LEFT", "DOWN", "DOWN-RIGHT"},
    };
    return names[gravity_y + 1][gravity_x + 1];
}

static void draw_hud(void) {
    u32 permille = frame_cpu_permille();
    text_print_line(0, 0, text_format("BUNNIES %3d/%d  A:ADD B:DEL", count, MAX_ENT));
    text_print_line(
        0, 1,
        text_format("CPU %3u.%u%%  %7u CYCLES", permille / 10, permille % 10, frame_cpu_cycles()));
    text_print_line(0, 2, text_format("GRAVITY %-10s START:OFF", gravity_name()));
}

void bunnymark_update(void) {
    sys_movement();
    bunny_physics();
    sys_render();
    draw_hud();
}
