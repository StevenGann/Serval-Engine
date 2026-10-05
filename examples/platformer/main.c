// platformer: "Serval Dash", a first level in the classic side-scroller style.
// Run and jump through one long stage as a small serval: bonus blocks, bricks,
// beetles and frogs, pits, stone staircases, and a banner pole before the den.
//
// Demonstrates:
//   - Tiled backgrounds from 16x16 metatiles (map.h): one tileset shared by
//     three map layers. The playfield on background 2 is 224 x 13 metatiles,
//     wider and a little taller than the screen, so the camera scrolls both
//     ways and the engine streams rows and columns into view. A parallax layer
//     on background 3 (hills and clouds, MAP_LAYER_WRAP, half speed) shows the
//     sky backdrop through color 0. Tall grass on background 1 is drawn in
//     front of the sprites. map_load draws a layer at once, so a level
//     loaded at black fades in complete.
//   - Animated tiles (tileset_set_tiles): a glint sweeps across every bonus
//     block at once, timed with frame_count
//   - A camera that follows the serval, never scrolls back left and is clamped
//     to the map (camera_set); sys_render draws entities relative to it
//   - Map collision (sys_map_movement, C_MAPBODY, body_contact) for the
//     serval, the enemies, the fish and popping gems; gravity from
//     physics_set_gravity and a terminal falling speed (body_max_fall);
//     knocked-out enemies bounce off the map and slide to a stop
//     (body_bounce, body_friction). Brick debris falls through the level as
//     plain physics bodies (sys_physics with every edge open).
//   - Stomping with body_hit_side: landing on an enemy (BODY_SIDE_BOTTOM)
//     squashes it, touching it from any other side hurts
//   - Metatile tags (MAP_TAG) marking bonus blocks, bricks and gems, found by
//     map_cell and map_collision_at; blocks changed at runtime with
//     map_set_cell (used blocks, broken bricks, collected gems), undone by
//     reloading the map when a life is lost
//   - A platformer controller: acceleration, running with B, skidding,
//     variable jump height, growing and shrinking
//   - Sprite animation: sys_animate with frame_times plays walking beetles,
//     spinning gems, twinkling sparkles and tumbling debris; the serval's
//     frame is picked by the game (running, jumping, skidding), and it blinks
//     with SPRITE_HIDDEN
//   - Many short-lived entities with game components: popping gems, bouncing
//     blocks drawn as sprites, brick debris, sparkles; enemies created only as
//     they come into view and destroyed once far behind
//   - Sprites drawn by hand (sprite_draw) next to sys_render: the goal banner
//     and HUD icons above the text layer (SPRITE_ABOVE_HUD); a fish rising out
//     of a block behind the playfield (SPRITE_BEHIND_PLAYFIELD)
//   - Screen fades between title, stage card, level, game over and stage
//     clear (screen_set_brightness)
//   - HUD text with a drop shadow (text_set_shadow), text_print_centered
//   - A level written as text in the source (level.c), converted at boot
//   - Music (PsgSong): an original looping tune on all three tone
//     generators, sped up where it is when time runs low
//     (psg_music_set_tempo), paused and resumed with the game
//     (psg_music_pause), a fanfare played once; sound effects play over it
//     with priorities (jingles aren't cut short) and the music comes back
//     after them
//   - Frogs hop at random intervals, seeded with random_entropy() when START
//     is pressed: the same input plays the same game on the GBA and the web
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash (about 3 seconds; any button skips it
//     once the text is in).
//   - The title fades in: the start of the level under a blue sky, rolling
//     green hills and pale blue mountains behind, white clouds, an orange
//     spotted serval on the grass, "S E R V A L   D A S H", a blinking "PRESS
//     START" and the controls. The top row is the HUD: score, gems (blue gem
//     icon), "STAGE 1", time (clock icon) and lives (serval head icon). All
//     text has a dark drop shadow, so it stays readable over the clouds.
//   - START: a rising chime, a fade to a black "STAGE 1" card with the lives
//     left, then a fade into the level and its music: a bouncy tune in F major
//     (melody, bass and drums) that loops about every 26 seconds. The
//     countdown starts at 300 and ticks down a little faster than once a
//     second. Sound effects take over a channel of the tune for a moment.
//   - Left/Right walk, B held runs faster, A jumps (higher when running;
//     letting go of A early makes a short hop). Turning around at speed skids.
//     The hills scroll at half speed behind the playfield. The camera follows
//     to the right only (you can't walk back off the left edge) and rises a
//     little when you climb high.
//   - Gold blocks with a paw print, a bright glint sweeping across them about
//     once a second: hit them from below and a spinning gem pops out (a
//     three-note chime, +200) or a pink fish slides out and flops along; the
//     block bounces and turns plain brown. Touching the fish makes the serval
//     grow tall (flickering between sizes, a rising tone). Bricks bounce with
//     a thud when small and burst into four tumbling pieces with a crash when
//     big. One brick holds several gems. Blue gems float in the air in a few
//     places. Every 20 gems: an extra life (a jingle).
//   - Purple beetles walk toward you and turn at walls and at each other;
//     red frogs sit a varying while and hop toward you. Land on them to stomp
//     them (a boing; beetles go flat) and bounce off; touching them from the
//     side or from below hurts: a big serval shrinks (a falling tone) and
//     blinks for two seconds, a small one loses a life. Hitting a block under
//     an enemy knocks it out: it flips over, bounces on the ground, slides to
//     a stop and vanishes in a sparkle.
//   - Brown tree stumps of rising height block the way; there are pits to
//     jump, two stone pyramids (the second with a gap in the middle) and a
//     tall final staircase. Falling into a pit loses a life.
//   - With 100 time left: a warning jingle over the tune, which speeds up
//     from where it is (150 to 180 beats per minute) until the level ends.
//   - Losing a life: the music stops, the serval hops up and falls off the
//     screen to a sad tune, then a fade to the "STAGE 1" card; after the
//     checkpoint (about 40% of the way) the level restarts there. With no
//     lives left: "GAME OVER", then the title.
//   - The goal: a tall pole with a gold knob and a red pennant. Touching it,
//     the serval slides down with the pennant (a falling whistle; bonus
//     points for catching it high, shown under the HUD), hops off with a
//     short fanfare and walks into the striped tent. The time left counts
//     into the score with ticks (50 per unit), "STAGE CLEAR!" appears with a
//     jingle, and after a few seconds the screen fades back to the title.
//   - START pauses ("PAUSED", a tick; the music stops) and resumes (a tick;
//     the music carries on from where it stopped).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   START = Enter.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (states, HUD, camera, goal), level.c (the level and its
// blocks), player.c, objects.c (enemies, items, effects), art.c (graphics,
// converted from ASCII pixel art) and sound.c.

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init();
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
