// fireflies_bench: the fireflies example timed the way scriptmark is, run
// headless by tools/bench.sh: the light end of the script benchmark (a whole
// game's logic in a script, about 40 ops a frame).
//
// examples/fireflies/main.c, without the splash and with the VM timed: one round of
// the game from boot with no input (the serval stands still, fireflies come
// and go, the round ends after 60 seconds and waits for START), 30 frames of
// warm-up then 3,670 measured. Logs the same numbers as scriptmark (bench.c)
// and exits; the result is deterministic for a given build
// (docs/development.md#script-benchmark). Keep the setup and the frame loop
// in step with examples/fireflies/main.c, whose art.c and sound.c it links.

#include "../../../examples/fireflies/game.h"
#include "vmbench.h"

#define WARMUP_FRAMES 30
#define BENCH_FRAMES 3670

static void start_round(void) {
    ecs_reset();
    vm_load(fireflies_script, fireflies_script_size);
    vm_start(OBJ_ROOM, VM_EV_ROOM_START);
    vm_start(OBJ_SPAWNER, VM_EV_ROOM_START);
}

int main(void) {
    serval_init();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN);
    art_build();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    psg_table_set(sound_table, SOUND_COUNT);
    tileset_load(&meadow_tileset);
    map_load(&meadow_layer);
    map_load(&grass_layer);
    screen_set_backdrop(COLOR_RGB(48, 38, 100));
    text_set_color(COLOR_RGB(255, 246, 200), COLOR_RGB(10, 8, 30));
    text_set_shadow(true);
    physics_set_bounds(0, FIELD_TOP, SCREEN_W, SCREEN_H);
    vm_bind(&(VmBindings){.psg_songs = songs,
                          .psg_song_count = SONG_COUNT,
                          .paths = paths,
                          .path_count = PATH_COUNT});
    vm_collide(C_PLAYER, C_FIREFLY);
    start_round();

    VmBench bench;
    vmbench_start(&bench);
    for (int frame = 0; frame < WARMUP_FRAMES + BENCH_FRAMES; frame++) {
        frame_begin();
        u32 t0 = vmbench_cycles();
        vm_step();
        u32 t1 = vmbench_cycles();
        sys_path();
        sys_movement();
        sys_physics();
        u32 t2 = vmbench_cycles();
        vm_events();
        u32 t3 = vmbench_cycles();
        sys_animate();
        sys_render_by_depth();
        frame_end();
        if (frame >= WARMUP_FRAMES)
            vmbench_frame(&bench, vm_ops_this_frame(), (t1 - t0) + (t3 - t2), frame_cpu_cycles());
        if (vm_global(G_RESTART))
            start_round();
    }
    vmbench_state(&bench, G_COUNT);
    vmbench_report(&bench, "fireflies");
    debug_exit(0);
}
