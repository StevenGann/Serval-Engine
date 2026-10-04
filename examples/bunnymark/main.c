// bunnymark: Serval Engine's CPU benchmark, after raylib's bunnymark example.
//
// Demonstrates:
//   - Entities built from engine components (position, velocity, sprite),
//     updated and drawn by the engine's systems (sys_movement, sys_render)
//   - A game-defined component and system (C_BUNNY, bounce_bunnies) working
//     alongside the engine's
//   - Several colors of one sprite: one SpriteAsset per palette, same tiles
//   - Random numbers, the HUD text layer, and per-frame CPU timing
//
// What to expect when booting the ROM:
//   - A dark screen with 16 bunnies (16x16 pixels; white, gold, blue or green)
//     flying out from the top center and bouncing off the screen edges.
//   - Two lines of white text at the top, which bunnies bounce below:
//       BUNNIES  16/128  A:ADD B:DEL
//       CPU   x.x%    nnnnn CYCLES
//   - Hold A to add bunnies (two per frame) up to 128, the engine's entity
//     limit; hold B to remove them.
//   - CPU is the share of each frame spent on game work (the previous frame's
//     measurement). Under 100%, the game keeps a steady 60 frames per second.
//   - No sound. The bunnies are the same on every boot (fixed random seed).
//   (In mGBA's default keyboard mapping, A is X and B is Z.)
//
// Benchmark build: compiled with BUNNYMARK_BENCH (target bunnymark_bench, run
// by tools/bench.sh), the same code starts with 128 bunnies from a fixed seed,
// runs 600 frames headless in mGBA and logs the average and peak CPU cycles
// per frame. That figure is what engine optimizations are measured against.
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

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

#define BUNNY_SPRITE(color)                                                                        \
    {.shape = SPRITE_SHAPE_SQUARE,                                                                 \
     .size = 1, /* 16x16 */                                                                        \
     .frame_count = 1,                                                                             \
     .tiles_per_frame = 4,                                                                         \
     .tiles = bunny_tiles,                                                                         \
     .palette_slot = (color)}

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

static const u16 bunny_group_sprites[SPRITE_COUNT] = {
    SPR_BUNNY_WHITE,
    SPR_BUNNY_GOLD,
    SPR_BUNNY_BLUE,
    SPR_BUNNY_GREEN,
};

static const SpriteGroup bunny_group = {
    .sprite_count = SPRITE_COUNT,
    .palette_count = SPRITE_COUNT,
    .flags = SPRITE_GROUP_RESIDENT,
    .sprite_ids = bunny_group_sprites,
    .palettes = &bunny_palettes[0][0],
    .tile_count = SPRITE_COUNT * 4,
};

// --- Game --------------------------------------------------------------------

// Game-defined component: marks entities the bounce system handles.
#define C_BUNNY C_GAME(0)

#define START_BUNNIES 16
#define HUD_HEIGHT 16 // two text rows; bunnies bounce below them

// Like raylib's bunnymark: up to 250 pixels per second each way, at 60 fps.
#define MAX_SPEED (FX(250) / 60)

#define BENCH_SEED 12345
#define BENCH_FRAMES 600

static Entity bunnies[MAX_ENT];
static int bunny_count;

static void add_bunny(void) {
    if (bunny_count == MAX_ENT)
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
    bunnies[bunny_count++] = e;
}

#ifndef BUNNYMARK_BENCH
static void remove_bunny(void) {
    if (bunny_count > 0)
        entity_destroy(bunnies[--bunny_count]);
}
#endif

// Game system: reverses a bunny's velocity when it moves past a screen edge
// or up into the HUD.
static void bounce_bunnies(void) {
    const FIXED max_x = FX(screen_width() - BUNNY_SIZE);
    const FIXED min_y = FX(HUD_HEIGHT);
    const FIXED max_y = FX(screen_height() - BUNNY_SIZE);
    for (u32 i = 0; i < MAX_ENT; i++) {
        if (!(ent_mask[i] & C_BUNNY))
            continue;
        if ((pos_x[i] < 0 && vel_x[i] < 0) || (pos_x[i] > max_x && vel_x[i] > 0))
            vel_x[i] = -vel_x[i];
        if ((pos_y[i] < min_y && vel_y[i] < 0) || (pos_y[i] > max_y && vel_y[i] > 0))
            vel_y[i] = -vel_y[i];
    }
}

static void draw_hud(void) {
    u32 cycles = frame_cpu_cycles();
    u32 tenths = cycles * 1000 / frame_budget_cycles(); // percent, one decimal
    text_print(0, 0, text_format("BUNNIES %3d/%d  A:ADD B:DEL", bunny_count, MAX_ENT));
    text_print(0, 1, text_format("CPU %3u.%u%%  %7u CYCLES", tenths / 10, tenths % 10, cycles));
}

// Everything bunnymark does in a frame, shared by the demo and the benchmark.
static void update(void) {
    sys_movement();
    bounce_bunnies();
    sys_render();
    draw_hud();
}

#ifdef BUNNYMARK_BENCH
static void run_benchmark(void) {
    random_seed(BENCH_SEED);
    while (bunny_count < MAX_ENT)
        add_bunny();

    u32 total = 0, peak = 0;
    for (int frame = 0; frame < BENCH_FRAMES; frame++) {
        frame_begin();
        update();
        frame_end();
        u32 cycles = frame_cpu_cycles(); // the frame that just ended
        total += cycles;
        if (cycles > peak)
            peak = cycles;
    }

    u32 average = total / BENCH_FRAMES;
    u32 tenths = average * 1000 / frame_budget_cycles();
    debug_log(text_format("bunnymark: %d bunnies, %d frames: avg %u cycles (%u.%u%%), peak %u",
                          bunny_count, BENCH_FRAMES, average, tenths / 10, tenths % 10, peak));
    debug_exit(0);
}
#endif

int main(void) {
    serval_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&bunny_group);
    screen_set_backdrop(COLOR_RGB(24, 24, 40));
    text_clear(); // sets up the text layer now, not during the first frame

#ifdef BUNNYMARK_BENCH
    run_benchmark();
#else
    for (int i = 0; i < START_BUNNIES; i++)
        add_bunny();

    for (;;) {
        frame_begin();
        if (button_down(BUTTON_A)) {
            add_bunny();
            add_bunny();
        }
        if (button_down(BUTTON_B)) {
            remove_bunny();
            remove_bunny();
        }
        update();
        frame_end();
    }
#endif
}
