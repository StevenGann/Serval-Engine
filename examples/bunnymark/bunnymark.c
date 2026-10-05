// bunnymark's game: the bunny sprites, their physics and the HUD. See main.c
// for what the example demonstrates and what to expect when it runs.

#include "bunnymark.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_BUNNY_WHITE, SPR_BUNNY_GOLD, SPR_BUNNY_BLUE, SPR_BUNNY_GREEN, SPRITE_COUNT };

#define BUNNY_SIZE 16

// A 16x16 bunny seen from the side, facing right, 4 bits per pixel, as four
// 8x8 tiles (top-left, top-right, bottom-left, bottom-right). Generated from
// this art:
//
//   ................    . transparent (color 0)
//   ........KK.KK...    W fur         (color 1)
//   .......KWPKWPK..    P pink        (color 2)
//   .......KWPKWPK..    K outline     (color 3)
//   .......KWPKWPK..
//   ........KWWWK...
//   ......KKWWWWWK..
//   .....KWWWWWWKWK.
//   ....KWWWWWWWWWPK
//   ..KKWWWWWWWWWWK.
//   .KWWWWWWWWWWWK..
//   KWWWWWWWWWWWWK..
//   KWWKWWWWWWWWWK..
//   .KK.KWWKKKWWK...
//   .....KK...KK....
//   ................
static const u32 bunny_tiles[32] = {
    0x00000000, 0x00000000, 0x30000000, 0x30000000, 0x30000000, 0x00000000, 0x33000000, 0x11300000,
    0x00000000, 0x00033033, 0x00321321, 0x00321321, 0x00321321, 0x00031113, 0x00311111, 0x03131111,
    0x11130000, 0x11113300, 0x11111130, 0x11111113, 0x11113113, 0x31130330, 0x03300000, 0x00000000,
    0x32111111, 0x03111111, 0x00311111, 0x00311111, 0x00311111, 0x00031133, 0x00003300, 0x00000000,
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

// Game-defined component: marks entities the bunny_animate system handles.
#define C_BUNNY C_GAME(0)

#define HUD_HEIGHT 24 // three text rows; bunnies stay below them

// Like raylib's bunnymark: up to 250 pixels per second each way, at 60 fps.
#define MAX_SPEED (FX(250) / 60)

// Gravity: a quarter pixel per frame per frame, in the chosen direction.
#define GRAVITY (FX_ONE / 4)

// Physics for every bunny: floor bounces keep 7/8 of the speed, and sliding
// along a floor loses 1/8 of it each frame.
#define BUNNY_BOUNCE 224
#define BUNNY_FRICTION 32

static Entity bunnies[MAX_ENT];
static int count;

static int gravity_x = 0, gravity_y = 1; // direction, for the HUD

void bunnymark_init(void) {
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&bunny_group);
    screen_set_backdrop(COLOR_RGB(24, 24, 40));
    text_clear(); // sets up the text layer now, not during the first frame

    physics_set_bounds(0, HUD_HEIGHT, SCREEN_W, SCREEN_H);
    gravity_set(0, 1); // down
}

void bunny_add(void) {
    if (count == MAX_ENT)
        return;
    Entity e = entity_create(C_POS | C_VEL | C_SPR | C_BODY | C_BUNNY);
    if (e == ENTITY_NONE)
        return;
    u32 i = entity_index(e);
    pos_x[i] = FX((SCREEN_W - BUNNY_SIZE) / 2);
    pos_y[i] = FX(HUD_HEIGHT);
    vel_x[i] = random_range(-MAX_SPEED, MAX_SPEED);
    vel_y[i] = random_range(-MAX_SPEED, MAX_SPEED);
    spr_id[i] = (u16)random_range(0, SPRITE_COUNT - 1);
    body_w[i] = body_h[i] = BUNNY_SIZE;
    body_bounce[i] = BUNNY_BOUNCE;
    body_friction[i] = BUNNY_FRICTION;
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
    physics_set_gravity(x * GRAVITY, y * GRAVITY);
}

// Game system: turns each bunny to face the way it's moving (the art faces
// right) and sorts bunnies lower on screen in front of those above them.
static void bunny_animate(void) {
    ECS_FOR_EACH(i, C_BUNNY) {
        if (vel_x[i] > 0)
            spr_flags[i] = 0;
        else if (vel_x[i] < 0)
            spr_flags[i] = SPRITE_FLIP_H;
        spr_depth[i] = (s16)fx_to_int(pos_y[i]);
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
    sys_physics();
    bunny_animate();
    sys_render_by_depth();
    draw_hud();
}
