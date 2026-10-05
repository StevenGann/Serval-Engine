// bunnymark_bench: Serval Engine's CPU benchmark, run headless by
// tools/bench.sh (not meant to be played).
//
// Runs bunnymark's game (bunnymark.c) with 128 bunnies, the entity limit,
// from a fixed random seed with gravity down and no input, for 600 frames in
// mGBA, then logs the average and peak CPU cycles per frame and exits. The
// result is deterministic for a given build; see docs/development.md.

#include "bunnymark.h"

#define BENCH_SEED 12345
#define BENCH_FRAMES 600

int main(void) {
    serval_init();
    bunnymark_init();
    random_seed(BENCH_SEED);
    while (bunny_count() < MAX_ENT)
        bunny_add();

    u32 total = 0, peak = 0;
    for (int frame = 0; frame < BENCH_FRAMES; frame++) {
        frame_begin();
        bunnymark_update();
        frame_end();
        u32 cycles = frame_cpu_cycles(); // the frame that just ended
        total += cycles;
        peak = cycles > peak ? cycles : peak;
    }

    u32 average = total / BENCH_FRAMES;
    u32 permille = average * 1000 / frame_budget_cycles();
    debug_log(text_format("bunnymark: %d bunnies, %d frames: avg %u cycles (%u.%u%%), peak %u",
                          bunny_count(), BENCH_FRAMES, average, permille / 10, permille % 10,
                          peak));
    debug_exit(0);
}
