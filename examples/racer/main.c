// racer: "Savanna Run", a pseudo-3D road racer in the manner of the 1980s'
// arcade racers. A serval drives an open safari jeep three laps of a
// savanna circuit against the clock, from late afternoon into dusk.
//
// The first game on the engine's raster effects, written to find out how
// far one effect at a time carries a game. The road is one still picture of
// a straight road; everything that makes it a racer is done to whole screen
// lines: each line shifted sideways by its own amount, and each band of
// lines recoloured light or dark. view.c's top comment explains the three
// tricks, road.c's the arithmetic.
//
// Demonstrates:
//   - A road bent line by line (road.c, view.c): raster_scroll() on a road
//     layer 256 pixels wide that wraps (MAP_LAYER_FIXED | MAP_LAYER_WRAP),
//     so that any line can take any shift; each line shifted by the curves
//     ahead, summed from the car up to that line (the classic dx/ddx sum
//     over the circuit's segments), and by where the car is across the
//     road. Every line's road kept clear of the screen's edges, so that its
//     wrapped copy never shows: the road follows the car only so far, and
//     the jeep sprite moves for the rest
//   - Stripes by palette writes alone: each of the road's 12 tile rows has
//     its own palette bank, whose colours are a material (grass, rumble
//     strip, tarmac, dash) in a group of the row's lines, so 12
//     tileset_set_colors() calls a frame make light and dark bands that
//     roll toward the player; far up, a haze where stripes would flicker
//   - A sky (a second wrapping layer, map_set_scroll()) that swings
//     against the curves, and a time of day by lap (afternoon, sunset,
//     dusk): 33 stages of every colour that changes, mixed at boot with
//     color_mix and walked through in about two seconds
//   - Level-of-detail sprites (objects.c): road_project() gives a point on
//     the ground its screen line, a size class (each object drawn at boot
//     in four sizes) and a scale in sixteenths; draws are only ever shrunk
//     (sprite_draw_ex()), so they cost half the scanline time of grown ones
//     and draws at one scale share a matrix; drawn nearest first
//   - The rules as portable C, tested on the host (track.c, road.c,
//     tests/racer_tests.c): speed, steering, curves pushing outward, off the
//     road, crashes, bumps, traffic, checkpoints, laps, records; no division
//     in a frame (tables built at boot)
//   - Art drawn at boot from ASCII pictures and pixel functions (art.c):
//     tiles kept once, the road's right half the left half flipped
//   - Sound on the tone generators (sound.c): an engine drone of 16 held
//     sounds on the wave channel, stepping with the speed (a playing tone's
//     pitch can't change), a looping race tune, jingles and effects by
//     priority
//   - Records in save data (records.c): one versioned struct in one slot,
//     read at boot, written only when a record falls
//   - The frame's CPU time, the sprites, matrices and busiest scanline on
//     screen (frame_cpu_permille, sprite_stats, sprite_stats_scanlines):
//     SELECT
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then the title at sunset: the road drives
//     itself under a big "SAVANNA RUN" logo, curving left and right, with
//     acacias, termite mounds, rocks, signs and giraffes going by, and the
//     jeep, seen from behind with the serval's spotted head over the
//     driver's seat, leaning into the curves. "PRESS START" blinks; under
//     it BEST RACE and BEST LAP ("--'--"--" until a race sets them). A
//     short jingle plays once. START fades to a race.
//   - The race starts in the late afternoon, on the start line. Top left:
//     LAP 1/3 and the race's time; in the middle TIME and the seconds left
//     (34); top right the speed in KM/H. Big 3, 2, 1 over the sky with low
//     beeps and the engine idling, then GO with a high beep, and the race
//     tune starts. The pedals and the steering do nothing before GO.
//   - A accelerates (290 KM/H in about four seconds), B brakes (the tail
//     lights light up), with neither the jeep coasts down slowly. LEFT and
//     RIGHT steer, faster the faster the jeep goes, not at all at a
//     standstill. The road bends toward the horizon, light and dark bands
//     rush down the screen, the sun and the hills slide the other way, and
//     the engine's note rises in steps with the speed. Curves push the jeep
//     to their outside; steering hard in a curve at speed skids: a squeal
//     and dust from the wheels.
//   - Off the road the speed falls to a third, the jeep jolts, dust flies
//     and a low rumble plays. Hitting an acacia, a mound, a rock, a sign or
//     a giraffe off the road is a crash: a crunch and a falling tone, the
//     picture shakes, the jeep spins three times, slowing, and vanishes,
//     and a second and a half after the crash it is back in the middle of
//     the road at rest. The clock runs on.
//   - Traffic: other safari jeeps and trucks, slower, in both lanes, far
//     ahead first and growing as they come closer. Running into one from
//     behind is a bump: a thud, a shake, and the jeep drops below its
//     speed. One that catches up with a slow jeep waits behind it.
//   - Checkpoints: a chequered banner on two posts, at the start line and
//     halfway round. Each adds 25 seconds: "+25" in the middle of the
//     screen and a jingle; at the start line "LAP 2" or "FINAL LAP" and a
//     longer jingle. Lap 2 turns to sunset and lap 3 to dusk, the colours
//     changing over about two seconds.
//   - The clock reaching 0 is TIME UP, with a falling phrase; the end of
//     lap 3 is FINISH, with a fanfare. The picture stops, then the results
//     come up over the road: the race's time (or, after TIME UP, the
//     distance reached in km), the three laps with BEST by the fastest, or
//     NEW RECORD (and a chime) where a saved record fell, the records as
//     they stand, and "PRESS A" blinking. A fades back to the title.
//   - START pauses: a blip, "PAUSED", the music and the engine's drone
//     stop (an effect already sounding fades out on its own) and the
//     picture holds; START again goes on from where it was, with the blip.
//   - SELECT, in a race, shows a readout on the bottom two rows: the CPU
//     time of the busiest frame of the last 16 and its highest so far, the
//     sprites and matrices used, the busiest scanline's sprite time (cycles
//     of 1,210) and its highest, and CUT and LOST, which stay 0. An
//     optimized build runs at about 30-50%, the readout's own scanline
//     count included (the web build's CPU figure is the browser's). A Debug
//     build draws its art at boot several seconds longer, under the black.
//   - The records survive power-off. On the title, L and R held with
//     SELECT erases them: "RECORDS ERASED" and four falling notes (or "SAVE
//     FAILED" if the save memory couldn't be written).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (the screens, the race's events, the sounds' cues), track.c
// (the circuit and the rules), road.c (the road's lines and the
// projection), view.c (the road, the stripes, the sky, the time of day),
// objects.c (the sprites on the road, the jeep, the dust), hud.c, screens.c
// (the title and the results), records.c, art.c (the pictures, drawn at
// boot) and sound.c.

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init(); // draws the art, then the title
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
