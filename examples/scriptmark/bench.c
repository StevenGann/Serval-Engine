// scriptmark_bench: Serval Engine's script benchmark, run headless by
// tools/bench.sh (not meant to be played).
//
// Runs scriptmark.lua's scene, a busy scripted scene of the kind Studio
// Advance's event editor makes (12 wandering behaviours that wait, 8 bouncers
// with a Step reaction each, food spawned and eaten through a vm_collide
// pair, puffs that spawn, wait and die, a HUD), from a fixed random seed with
// no input, in mGBA: 60 frames of warm-up, then 600 measured. It logs the
// VM's fixed cost (vm_step() and vm_events() with the blob loaded and nothing
// to run), then the cycles the VM takes a frame (the cycle counter around
// vm_step() and vm_events()), the ops it runs, a least-squares fit of the one
// against the other (the cycles every frame pays, and the cycles per op), the
// whole frame, and a checksum of the run, then exits. The result is
// deterministic for a given build; see docs/development.md#script-benchmark.
//
// Booted in an emulator it shows the scene for 11 seconds, then stops: on a
// dark backdrop, orange discs (wanderers) drifting and pausing, cyan
// diamonds (bouncers) crossing the screen and bouncing off its edges, small
// green dots (food) that vanish when a disc touches one and reappear
// elsewhere, grey rings (puffs) left behind by the bouncers for a moment,
// and two numbers at the top left, the food eaten and the discs resting,
// updated twice a second. No sound.
//
// Uses only Serval Engine's API, and reads the engine's cycle counter
// (vmbench.c).

#include "game.h"
#include "vmbench.h"

#define BENCH_SEED 12345
#define WARMUP_FRAMES 60
#define BENCH_FRAMES 600

// --- Sprites: one 8x8 tile each, 4 bits per pixel, one palette ---------------

static const u32 wanderer_tiles[8] = {0x00111100, 0x01111110, 0x11111111, 0x11111111,
                                      0x11111111, 0x11111111, 0x01111110, 0x00111100};
static const u32 bouncer_tiles[8] = {0x00022000, 0x00222200, 0x02222220, 0x22222222,
                                     0x22222222, 0x02222220, 0x00222200, 0x00022000};
static const u32 food_tiles[8] = {0x00000330, 0x00003333, 0x00003333, 0x00000330, 0, 0, 0, 0};
static const u32 puff_tiles[8] = {0x00444400, 0x04000040, 0x40000004, 0x40000004,
                                  0x40000004, 0x40000004, 0x04000040, 0x00444400};

static const u16 palette[16] = {0, COLOR_RGB(255, 170, 40), COLOR_RGB(80, 200, 255),
                                COLOR_RGB(120, 230, 90), COLOR_RGB(170, 170, 190)};

static const SpriteAsset wanderer = {.size = SPRITE_8x8, .tiles = wanderer_tiles};
static const SpriteAsset bouncer = {.size = SPRITE_8x8, .tiles = bouncer_tiles};
static const SpriteAsset food = {.size = SPRITE_8x8, .tiles = food_tiles};
static const SpriteAsset puff = {.size = SPRITE_8x8, .tiles = puff_tiles};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_WANDERER] = &wanderer,
    [SPR_BOUNCER] = &bouncer,
    [SPR_FOOD] = &food,
    [SPR_PUFF] = &puff,
};

static const SpriteGroup sprite_group = {
    .palettes = palette,
    .sprite_count = SPRITE_COUNT,
    .palette_count = 1,
};

// --- The benchmark -----------------------------------------------------------

int main(void) {
    serval_init();
    random_seed(BENCH_SEED);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    screen_set_backdrop(COLOR_RGB(20, 20, 36));
    text_set_color(COLOR_RGB(255, 255, 255), COLOR_RGB(0, 0, 0));
    vm_load(scriptmark_script, scriptmark_script_size);

    // The fixed cost: the blob loaded, nothing running, nothing attached, no
    // pair; then the benchmark's pair alone, with no entities to list.
    u32 t0 = vmbench_cycles();
    for (int k = 0; k < 16; k++) {
        vm_step();
        vm_events();
    }
    u32 idle = (vmbench_cycles() - t0) / 16;
    vm_collide(C_WANDERER, C_FOOD); // wanderers eat food (Wanderer:collision)
    t0 = vmbench_cycles();
    for (int k = 0; k < 16; k++)
        vm_events();
    u32 pair = (vmbench_cycles() - t0) / 16;
    debug_log(text_format("scriptmark: idle VM %u cycles a frame; the pair, no entities: %u", idle,
                          pair));

    vm_start(OBJ_DIRECTOR, VM_EV_ROOM_START);
    VmBench bench;
    vmbench_start(&bench);
    for (int frame = 0; frame < WARMUP_FRAMES + BENCH_FRAMES; frame++) {
        frame_begin();
        u32 t1 = vmbench_cycles();
        vm_step();
        u32 t2 = vmbench_cycles();
        sys_movement();
        u32 t3 = vmbench_cycles();
        vm_events();
        u32 t4 = vmbench_cycles();
        sys_render();
        frame_end();
        if (frame >= WARMUP_FRAMES)
            vmbench_frame(&bench, vm_ops_this_frame(), (t2 - t1) + (t4 - t3), frame_cpu_cycles());
    }
    vmbench_state(&bench, G_COUNT);
    vmbench_report(&bench, "scriptmark");
    debug_exit(0);
}
