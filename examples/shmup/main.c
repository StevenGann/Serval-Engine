// shmup: "Star Veldt", a vertical shoot-'em-up. Fly the Caracal up through a
// nebula of asteroids and station hulls, shoot down the swarm, and destroy
// the Hive Lantern at the end of the stage.
//
// Demonstrates:
//   - A vertically scrolling stage driven by the camera (camera_set): a tall
//     map on background 2 (11 x 458 metatiles, built at boot from segments
//     written as text, stage.c) that the camera climbs a pixel per frame, over
//     a starfield on background 3 that repeats (MAP_LAYER_WRAP) at half speed
//     (scroll_factor): parallax. During the boss the camera rests at the top
//     of the map and the stars scroll on by themselves (map_set_scroll).
//   - Screen and world positions side by side: the ship, enemies, bullets
//     and effects live on the screen (SPRITE_SCREEN: drawn ignoring the
//     camera), turrets on the map, scrolling with it
//   - A narrow playfield with a HUD panel: the left 176 pixels are the field,
//     the right 64 a panel on background 1 (in front of sprites, held still
//     by MAP_LAYER_FIXED) with the score, high score, ships, bombs and power
//     in text and sprite icons
//   - Many short-lived entities on a budget (game.h): shots, enemy bullets,
//     explosions and sparks, each capped so the total stays within the
//     engine's 128 entities and 128 sprites; past a cap the extra bullet or
//     spark is skipped (patterns thin out) rather than anything failing
//   - Enemy waves from a wave table, flying movement patterns as paths
//     (path.h, sys_path: lines, swoops, weaves, dives, hover-and-leave,
//     zigzags, mirrored with PATH_MIRROR_X for either side)
//   - Aimed shots (angle_of), spreads, rings and spirals (fx_sin, fx_cos),
//     and turrets fixed to the map that scroll with it, leaving a crater
//     (map_set_cell) when destroyed; lamps on the hulls blink
//     (tileset_set_tiles)
//   - Rotating turrets: big cannons on the station forts, each a metasprite
//     (a dome and long twin barrels, turning about the dome's center: the
//     pivot, set by its origin) with its own angle (spr_angle, a rotation
//     matrix per cannon) that turns toward the ship at a limited rate and
//     fires along its barrels from the muzzles; destroyed, the head spins
//     away shrinking (sprite scaling: SPRITE_SCALED and spr_scale). Their 32 x 32 emplacements and
//     craters are four metatiles each, one quarter of art flipped four ways
//   - Collisions between screen-space shots and world-space turrets with
//     body_overlap, which adds the camera for such pairs
//   - A boss of three entities with three attack phases set by its hit
//     points, and a red palette for its last one (SPRITE_PALETTE)
//   - Hitboxes much smaller than the sprites (4 x 4 pixels for the 16 x 16
//     ship, shown as a dot while focusing), sprite origins centering the art
//     on them, collisions with body_overlap
//   - Draw order by depth (sys_render_by_depth): enemy bullets in front of
//     everything, the player's shots behind; sprite animation (sys_animate,
//     SPRITE_ASSET_ANIM_ONCE) for darts, items, explosions and sparks; a
//     rotating sprite sharing one angle (spinners); blinking with
//     SPRITE_HIDDEN; white "hit" flashes, the orange carrier and the boss's
//     red phase as the same sprites drawn with another palette of their
//     group (SPRITE_PALETTE)
//   - Per-frame lists of each kind of entity (ecs_gather), so collisions and
//     updates loop over a few slots instead of the whole pool
//   - Screen fades and white flashes (screen_set_brightness), text with a
//     drop shadow, text centered on the field (text_print_centered_in), a
//     highlighted entry in the high-score table (text styles)
//   - Music (PsgSong): an original stage tune and boss tune on all three tone
//     generators, paused with the game (psg_music_pause), the boss tune sped
//     up in its last phase (psg_music_set_tempo), a fanfare played once; sound
//     effects over it with priorities (sound.c)
//   - Save data (save.h): a top-5 high-score table with initials (scores.c)
//   - random_entropy(): the same input plays the same game on the GBA and
//     the web
//   - In debug builds, SELECT shows the CPU load (frame_cpu_permille: this
//     frame and the highest since the stage or the boss fight began), the
//     live entities (ecs_count) and the hardware sprites used last frame with
//     the draws lost to the limits, including scanlines that ran out of
//     sprite time (sprite_stats, sprite_stats_scanlines; should stay 0) at
//     the bottom of the panel
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash: "made with" and the engine's logo (about
//     3 seconds; any button skips it once the logo is in).
//   - The title fades in over a slowly scrolling starfield with purple
//     nebula wisps: "S T A R   V E L D T", a blinking "PRESS START", the
//     controls, the high score and the ship, a silver dart with orange ear
//     fins and a flickering flame. Every 5 seconds it alternates with the
//     "HIGH SCORES" table (top 5; a star marks runs that beat the boss). On a
//     first boot it holds made-up scores, KIT 30000 down to ZOE 5000; the
//     table survives power-off (in mGBA, the .sav file next to the ROM).
//   - START: a rising jingle, a fade to the stage. Grey asteroids, cyan
//     crystals and later steel station hulls scroll down the field behind
//     the stars; "STAGE 1 / THE STAR VELDT" shows for 3 seconds. The panel on
//     the right shows SCORE, HI, SHIPS (3), BOMBS (3) and POWER (LV 1). The
//     stage tune, a driving melody in D minor over pumping bass and drums,
//     loops about every 25 seconds.
//   - The D-pad moves the ship (it banks left and right); holding A fires
//     twin green bolts with a soft hiss (the drums give way while firing);
//     holding R focuses: slower, the side shots straight ahead, and a white
//     dot shows the hitbox. B fires a bomb: a white flash and a roar, every
//     enemy bullet vanishes (10 points each), enemies on screen take damage,
//     and the ship is safe for a moment.
//   - Enemies come in waves: purple darts in lines, swoops, side entries,
//     zigzags and dives, some firing pink orbs at the ship; spinning
//     four-bladed spinners weave down firing blue three-way spreads; green
//     gunships hover and fire rings and aimed fans (most drop a capsule);
//     orange carriers drift down and drop one too. Turrets on the station
//     hulls open their red eye before firing pairs of shots; destroyed, they
//     leave a glowing crater. Lamps on the hulls blink. Twice (after about 50
//     and 90 seconds) a fort comes by with two big cannons in round
//     emplacements: steel domes with a red eye and long twin barrels (well
//     past the dome) that swing smoothly about the dome to follow the ship
//     (a ship that keeps moving stays ahead of them) and, once aimed, fire
//     three pairs of pink shots with muzzle flashes. They take many hits
//     (3,000 points); destroyed, the head spins away shrinking to nothing and
//     a big glowing crater is left. Shot enemies flash white and burst into
//     fireballs and sparks (100 to 2,000 points).
//   - A blue P capsule raises the power (a chime): side needles at level 2,
//     two more at 3, heavier bolts at MAX (then P gives 2,000 points). A pink
//     B capsule adds a bomb (up to 5).
//   - A hit loses the ship in a big explosion with a deep rumble, clears the
//     enemy bullets and drops a power level; a new ship appears at the
//     bottom after a moment, blinking for 2 seconds. An extra ship at 50,000
//     points and every 100,000 after (a jingle).
//   - After about 110 seconds the stage tune stops, an alarm sounds and
//     "WARNING / THE HIVE LANTERN" flashes; the boss tune starts (E minor,
//     grinding bass) and the Hive Lantern comes down: a purple armored
//     lantern with a glowing orange core and two steel gun pods. It sways
//     from side to side: first the pods fire aimed three-way bursts and the
//     core slow rings; below about two thirds of its strength the core spins
//     a double blue spiral; below one third it turns red, its core purple,
//     the tune speeds up, and it fires rings and pink fans while swaying
//     faster. The pods can be shot off (5,000 points each).
//   - Destroying the core: a chain of explosions, then a big one, a white
//     flash, a short fanfare, "STAGE CLEAR!" (50,000 points) and a bonus for
//     ships and bombs left counted into the score with ticks.
//   - With no ships left: "GAME OVER" with a falling tune.
//   - After either, the screen fades; a score that makes the table brings
//     "NEW HIGH SCORE" and three letters: UP and DOWN change the letter
//     (a blip; held, they run through the letters), A moves on, B back, START
//     (or A on the last) saves it with a chime and shows the table with the
//     new entry in yellow.
//   - START pauses ("PAUSED", a tick; the music stops) and resumes (it
//     carries on where it stopped).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (states, scrolling, HUD, the stage's flow), player.c, enemies.c
// (the wave table and its paths, enemies, bullets, effects, items), boss.c,
// stage.c (the stage map), art.c (graphics as ASCII art), sound.c and
// scores.c.

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init(); // converts the art and builds the stage map, then the title
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
