// platformer: "Serval Dash", a side-scroller in the classic style, in four
// stages. Run and jump as a small serval through grassland (stage 1-1), a
// cave (1-2), the treetops (1-3) and a castle (1-4): bonus blocks, bricks,
// enemies to stomp, pits, one-way ledges and spikes, banner poles before the
// den and exits back to daylight, then an ending. Lives, score and gems carry
// from stage to stage; the furthest stage reached and the best score are
// saved.
//
// Demonstrates, in every stage (the frame: game.c, level.c, player.c,
// objects.c):
//   - Stages as modules: a table of stages (StageDef), each with its own
//     level text, tileset, metatiles, parallax layer, sprite group and
//     music, and hooks for its own objects, hazards, screen effects and
//     drawing; stage cards and progression from one to the next
//   - Sprites loaded in layers: the global group (the serval, gems, the fish,
//     the banner, sparkles, HUD icons), then a mark (sprite_groups_mark),
//     then the stage's group (its enemies, and the bouncing block and debris
//     in its colors), released to the mark when the next stage loads
//     (sprite_groups_release). One sprite table and one sound table hold
//     every stage's IDs, each stage a block of its own.
//   - Tiled backgrounds from 16x16 metatiles (map.h): one tileset per stage,
//     shared by up to three map layers. The playfield on background 2 is
//     wider and a little taller than the screen (1-1: 224 x 13 metatiles),
//     so the camera scrolls both ways and the engine streams rows and
//     columns into view. A parallax layer on background 3 (MAP_LAYER_WRAP,
//     half speed) shows the backdrop through color 0. Decoration on
//     background 1 is drawn in front of the sprites. map_load draws a layer
//     at once, so a stage loaded at black fades in complete. Each stage's
//     level is written as text in the source (stage_<name>.c) and built into
//     one map buffer in RAM when the stage starts (level.c).
//   - Animated tiles (tileset_set_tiles): a glint sweeps across every bonus
//     block at once, timed with frame_count
//   - A camera that follows the serval, never scrolls back left and is clamped
//     to the map (camera_set); sys_render draws entities relative to it
//   - Map collision (sys_map_movement, C_MAPBODY, body_contact) for the
//     serval, the enemies, the fish and popping gems; gravity from
//     physics_set_gravity and a terminal falling speed (body_max_fall);
//     knocked-out enemies bounce off the map and slide to a stop
//     (body_bounce, body_friction). Brick debris falls through the level as
//     plain physics bodies (sys_physics with every edge open). One-way
//     platforms (MAP_ONEWAY): the serval jumps up through them and lands on
//     them, and enemies walk on them.
//   - Stomping with body_hit_side: landing on an enemy (BODY_SIDE_BOTTOM)
//     squashes it, touching it from any other side hurts
//   - Metatile tags (MAP_TAG) found with map_tags_in: bonus blocks, bricks,
//     gems, and hazards around the serval's body (TAG_HAZARD, the stage's
//     hazard hook); a bonus block with a fish is told apart by its metatile
//     number. Blocks change at runtime with map_set_cell (used blocks, broken
//     bricks, collected gems), undone by rebuilding the level when a life is
//     lost.
//   - A platformer controller: acceleration, running with B, skidding,
//     variable jump height, growing and shrinking
//   - Sprite animation: sys_animate with frame_times plays walking enemies,
//     spinning gems, twinkling sparkles and tumbling debris; the serval's
//     frame is picked by the game (running, jumping, skidding), and it blinks
//     with SPRITE_HIDDEN
//   - Many short-lived entities with game components: popping gems, bouncing
//     blocks drawn as sprites, brick debris, sparkles; enemies created only as
//     they come into view and destroyed once far behind
//   - Sprites drawn by hand (sprite_draw) next to sys_render: the goal banner
//     and HUD icons above the text layer (SPRITE_ABOVE_HUD); a fish rising out
//     of a block behind the playfield (SPRITE_BEHIND_PLAYFIELD)
//   - Screen fades between title, stage card, stage, game over and stage
//     clear (screen_set_brightness)
//   - HUD text with a drop shadow (text_set_shadow), text_print_centered
//   - Music (PsgSong): an original looping tune per stage on all three tone
//     generators, sped up where it is when time runs low
//     (psg_music_set_tempo), paused and resumed with the game
//     (psg_music_pause), a fanfare played once; sound effects play over it
//     with priorities (jingles aren't cut short) and the music comes back
//     after them
//   - Frogs hop at random intervals, seeded with random_entropy() when START
//     is pressed: the same input plays the same game on the GBA and the web
//   - Saved progress (save.h) in the cartridge's Flash (SAVE FLASH64K in
//     examples/CMakeLists.txt; the web build keeps it in localStorage): the
//     furthest stage reached and the best score, one versioned struct in one
//     slot, written only on black screens (a stage card, game over, the
//     ending)
//
// Demonstrates in stage 1-2, the underground (stage_underground.c):
//   - One-way plank ledges over pits, and a dark stage: no sky, a backdrop
//     that pulses faintly with color_mix (the effects hook), and a
//     parallax layer of rock columns whose palette is the near rock's mixed
//     toward the backdrop with color_mix when the stage loads (the load
//     hook; the tileset's palettes are in RAM)
//   - A hazard: crystal spikes, solid metatiles tagged TAG_HAZARD
//   - Paths (path.h, sys_path): bats asleep under the ceiling swoop down in a
//     U toward the serval and fly away up, flying through the rock (bodies
//     with BODY_GRAVITY(0), moved by sys_movement)
//   - A recolored brick (the stage group's block palette) and an exit
//     without a pole
//
// Demonstrates in stage 1-3, the treetops (stage_treetops.c): a placeholder
// for now, on the overworld's art: one-way canopies over bottomless gaps,
// with frogs on them, and the goal pole.
//
// Demonstrates in stage 1-4, the castle (stage_castle.c, boss.c): a
// placeholder for now, on the underground's art: spikes, plank ledges, an
// exit, and the ending hooks (StageDef.ending_start, ending_update).
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash: "made with" and the engine's logo (about
//     3 seconds; any button skips it once the logo is in).
//   - The title fades in: the start of stage 1-1 under a blue sky, rolling
//     green hills and pale blue mountains behind, white clouds, an orange
//     spotted serval on the grass, "S E R V A L   D A S H", a blinking "PRESS
//     START", the controls and the best score saved ("BEST 000000" on a first
//     boot). The top row is the HUD: score, gems (blue gem icon), "STAGE
//     1-1", time (clock icon) and lives (serval head icon). All text has a
//     dark drop shadow, so it stays readable over the clouds.
//   - Once a later stage has been reached, the title also shows "SELECT:
//     STAGE 1-1": SELECT steps through the stages reached (a tick), back to
//     1-1 after the last, and START begins with the one shown (the HUD shows
//     it too). A game started there has the usual 3 lives and no score.
//   - START: a rising chime, a fade to a black "STAGE 1-1" card with the lives
//     left, then a fade into the stage and its music. The countdown starts
//     at 300 and ticks down a little faster than once a second. Sound effects
//     take over a channel of the tune for a moment.
//   - Left/Right walk, B held runs faster, A jumps (higher when running;
//     letting go of A early makes a short hop). Turning around at speed skids.
//     The far layer scrolls at half speed behind the playfield. The camera
//     follows to the right only (you can't walk back off the left edge) and
//     rises a little when you climb high.
//   - Gold blocks with a paw print, a bright glint sweeping across them about
//     once a second: hit them from below and a spinning gem pops out (a
//     three-note chime, +200) or a pink fish slides out and flops along; the
//     block bounces and turns plain brown. Touching the fish makes the serval
//     grow tall (flickering between sizes, a rising tone). Bricks bounce with
//     a thud when small and burst into four tumbling pieces with a crash when
//     big. Some bricks hold several gems. Blue gems float in the air in a few
//     places. Every 20 gems: an extra life (a jingle).
//   - Enemies walk toward you and turn at walls and at each other, or sit and
//     hop. Land on them to stomp them (a boing) and bounce off; touching them
//     from the side or from below hurts: a big serval shrinks (a falling
//     tone) and blinks for two seconds, a small one loses a life. Hitting a
//     block under an enemy knocks it out: it flips over, bounces on the
//     ground, slides to a stop and vanishes in a sparkle.
//   - With 100 time left: a warning jingle over the tune, which speeds up
//     from where it is until the stage ends.
//   - Losing a life: the music stops, the serval hops up and falls off the
//     screen to a sad tune, then a fade to the stage's card; past the
//     stage's checkpoint (about 40% of the way) the stage restarts there.
//     With no lives left: "GAME OVER", then the title.
//   - At the end of a stage the serval walks into the den or the exit with a
//     short fanfare, the time left counts into the score with ticks (50 per
//     unit), "STAGE CLEAR!" appears with a jingle and the score, and after a
//     few seconds the next stage's card fades in. Lives, score, gems and the
//     serval's size carry over; time starts again at 300.
//   - START pauses ("PAUSED", a tick; the music stops) and resumes (a tick;
//     the music carries on from where it stopped).
//   Stage 1-1, the overworld:
//   - Grassland under a blue sky with hills and clouds, tall grass and
//     flowers in front of the serval, and a bouncy tune in F major (melody,
//     bass and drums) that loops about every 26 seconds, sped up from 150 to
//     180 beats per minute when time runs low.
//   - Purple beetles walk and go flat when stomped; red frogs sit a varying
//     while and hop toward you. Brown tree stumps of rising height block the
//     way; there are pits to jump, two stone pyramids (the second with a gap
//     in the middle) and a tall final staircase. Falling into a pit loses a
//     life.
//   - The goal: a tall pole with a gold knob and a red pennant. Touching it,
//     the serval slides down with the pennant (a falling whistle; bonus
//     points for catching it high, shown under the HUD), hops off with the
//     fanfare and walks into the striped tent.
//   Stage 1-2, the underground:
//   - A dark cave: a blue-black backdrop that glows faintly violet and fades
//     again every four seconds or so, dim columns of rock scrolling behind,
//     a rough rock ceiling with stalactites at the top of the screen (lower
//     in one passage), a floor of blue-grey stones, and glowing cyan
//     mushrooms in front of the serval. A quieter, sparser tune in E minor
//     that loops every 32 seconds, sped up from 120 to 150 beats per minute
//     when time runs low.
//   - Blue-grey bricks (they bounce and break blue-grey too) and the same gold
//     bonus blocks. Grey woodlice walk like beetles and curl up into a ball
//     when stomped. Purple bats hang asleep under the ceiling; when the
//     serval comes near, one squeaks and swoops down in a U toward it, then
//     flies away up through the rock. Stomping a bat makes it vanish in a
//     sparkle.
//   - Wooden plank ledges on braces over pits: the serval jumps up through
//     them from below and lands on them. Pink crystal spikes fill dips and
//     the gaps between stone pillars: landing on them hurts (a big serval
//     shrinks and blinks, and can jump out; a small one loses a life).
//   - At the end a stone staircase climbs to a timbered opening in the rock
//     wall, with daylight and green hills beyond; walking in ends the stage.
//   Stage 1-3, the treetops (a placeholder for now):
//   - The overworld's tiles under a paler sky, with green hedges on tree
//     trunks for canopies: the serval jumps up through them and lands on
//     them, and the gaps between them are bottomless. Red frogs sit on the
//     canopies and the ground. A short sketch of a tune in G major. The goal
//     pole and the striped tent end it.
//   Stage 1-4, the castle (a placeholder for now):
//   - The underground's tiles over a red-black backdrop: a spike dip, a pit
//     of spikes crossed on plank ledges, a woodlouse, and the timbered exit at
//     the end. A short sketch of a tune in D minor.
//   After the last stage:
//   - "STAGE CLEAR!", then a fade to a black screen: "THE END", "THE SERVAL
//     IS HOME AGAIN", "THANK YOU FOR PLAYING" with the fanfare, for ten
//     seconds (START after the first second skips it), then the title, which
//     now shows the best score and lets SELECT pick any stage.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The frame every
// stage runs in is game.c (states, HUD, camera, goal, stage select and
// saving), level.c (building the level, blocks, gems, hazards), player.c,
// objects.c (enemies, items, effects), art.c (shared graphics, converted
// from ASCII pixel art) and sound.c (shared sound effects). Each stage is
// stage_<name>.c (its level and hooks), art_<name>.c, sound_<name>.c and
// stage_<name>.h: overworld (1-1), underground (1-2), treetops (1-3) and
// castle (1-4, with boss.c).

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
