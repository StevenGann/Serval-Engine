// bunnymark: Serval Engine's CPU benchmark, after raylib's bunnymark example.
//
// Demonstrates:
//   - Entities built from engine components (position, velocity, sprite, body)
//     and the engine's systems: sys_movement, sys_physics (gravity, bounces,
//     friction) and sys_render_by_depth (draw order)
//   - A game-defined component and system (C_BUNNY, bunny_animate) written
//     with ECS_FOR_EACH, setting per-entity sprite flags and depth
//   - Several colors of one sprite: one SpriteAsset per palette, same tiles
//   - Random numbers, HUD text that is redrawn every frame (text_print_line,
//     with text_set_color and the font's drop shadow), and per-frame CPU
//     timing (frame_cpu_permille, frame_cpu_cycles)
//   - A backdrop drawn at boot, pixel by pixel, into a tile per screen cell
//     (tileset_load, a MAP_LAYER_FIXED map layer on background 3), animated
//     cheaply: a meter redrawn a few tiles at a time (tileset_set_tiles) and
//     twinkling stars (tileset_set_colors, color_mix)
//
// What to expect when booting the ROM:
//   - A night scene: a starry purple sky, darker at the top, with a full moon
//     at the upper right, two rows of hills and grass along the bottom; the
//     stars twinkle, two sets brightening in turn. 16 bunnies seen from the
//     side (16x16 pixels, shaded, with a cream tail; white, gold, blue or
//     green) fly out from the top center. Each faces the way it's moving.
//     Gravity pulls them down: they bounce lower and lower, slide to a stop
//     and come to rest in the grass. Bunnies don't collide, so they overlap;
//     one lower on screen is drawn in front of one above it.
//   - A dark panel at the top, edged with a violet line, which bunnies stay
//     below, with three lines of light text:
//       BUNNIES  16/128  A:ADD B:DEL
//       CPU   x.x%    nnnnn CYCLES
//       GRAVITY DOWN      START:OFF
//   - The D-pad changes the direction of gravity, and it stays that way after
//     you let go; two directions together pull diagonally. The bunnies fall
//     toward the new side, and the third line shows the direction.
//   - START (with no direction held) turns gravity off: resting bunnies jump
//     off in random directions, and all of them float and bounce off every
//     edge without slowing down. Press a direction to turn it back on.
//   - Hold A to add bunnies (two per frame) up to 128, the engine's entity
//     limit; hold B to remove them.
//   - CPU is the share of each frame spent on game work (the previous frame's
//     measurement). Under 100%, the game keeps a steady 60 frames per second.
//     A bar behind that line shows it too: the panel's whole width is the
//     frame, with ticks at each quarter. It glides to the new length as the
//     share changes, teal, turning amber past 50% and red past 85%. (About
//     26% with 128 bunnies: a quarter of the bar.)
//   - No sound. The bunnies are the same on every boot (fixed random seed).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   START = Enter.)
//
// The game itself (sprites, physics, HUD) and its scenery are in bunnymark.c.
// bench.c runs the same game headless as Serval Engine's CPU benchmark
// (tools/bench.sh), without the scenery: a map layer costs the engine a
// little time each frame (about 650 cycles), and the scenery's animation
// about 450 more, which would change the benchmark's workload.
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
    bunnymark_scenery_load();
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
        bunnymark_scenery_update();
        frame_end();
    }
}
