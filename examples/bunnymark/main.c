// bunnymark: Serval Engine's CPU benchmark, after raylib's bunnymark example.
//
// Demonstrates:
//   - Entities built from engine components (position, velocity, sprite),
//     updated and drawn by the engine's systems (sys_movement, sys_render)
//   - A game-defined component and system (C_BUNNY, bunny_physics: gravity
//     and bounces) written with ECS_FOR_EACH, alongside the engine's
//   - Several colors of one sprite: one SpriteAsset per palette, same tiles
//   - Random numbers, HUD text that is redrawn every frame (text_print_line),
//     and per-frame CPU timing (frame_cpu_permille)
//
// What to expect when booting the ROM:
//   - A dark screen with 16 bunnies (16x16 pixels; white, gold, blue or green)
//     flying out from the top center. Gravity pulls them down: they bounce
//     lower and lower, slide to a stop and come to rest along the floor
//     (overlapping; bunnies don't collide with each other).
//   - Three lines of white text at the top, which bunnies stay below:
//       BUNNIES  16/128  A:ADD B:DEL
//       CPU   x.x%    nnnnn CYCLES
//       GRAVITY DOWN      START:OFF
//   - The D-pad changes the direction of gravity, and it stays that way after
//     you let go; two directions together pull diagonally. The bunnies fall
//     toward the new side, and the third line shows the direction.
//   - START turns gravity off: bunnies float and bounce off every edge without
//     slowing down. Press a direction to turn it back on.
//   - Hold A to add bunnies (two per frame) up to 128, the engine's entity
//     limit; hold B to remove them.
//   - CPU is the share of each frame spent on game work (the previous frame's
//     measurement). Under 100%, the game keeps a steady 60 frames per second.
//   - No sound. The bunnies are the same on every boot (fixed random seed).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   START = Enter.)
//
// The game itself (sprites, physics, HUD) is in bunnymark.c. bench.c runs the
// same game headless as Serval Engine's CPU benchmark (tools/bench.sh).
//
// Uses only Serval Engine's API; no third-party headers.

#include "bunnymark.h"

// Points gravity along the D-pad directions being held; START turns it off.
static void control_gravity(void) {
    int x = button_down(BUTTON_RIGHT) - button_down(BUTTON_LEFT);
    int y = button_down(BUTTON_DOWN) - button_down(BUTTON_UP);
    if (x || y)
        gravity_set(x, y);
    else if (button_pressed(BUTTON_START))
        gravity_set(0, 0);
}

int main(void) {
    serval_init();
    bunnymark_init();
    for (int i = 0; i < BUNNY_START_COUNT; i++)
        bunny_add();

    for (;;) {
        frame_begin();
        control_gravity();
        if (button_down(BUTTON_A)) {
            bunny_add();
            bunny_add();
        }
        if (button_down(BUTTON_B)) {
            bunny_remove();
            bunny_remove();
        }
        bunnymark_update();
        frame_end();
    }
}
