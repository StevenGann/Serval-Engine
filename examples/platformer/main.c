// platformer: "Serval Dash", a side-scroller in the classic style, in four
// stages. Run and jump as a small serval through grassland (stage 1-1), a
// cave (1-2), the treetops (1-3) and a castle (1-4): bonus blocks, bricks,
// enemies to stomp, pits, one-way ledges, spikes and lava, fire bars, banner
// poles before the den and exits back to daylight, a dragon on a bridge at
// the castle's end, then an ending. Lives, score and gems carry
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
// Demonstrates in stage 1-3, the treetops (stage_treetops.c):
//   - One-way canopies (MAP_ONEWAY) at varied heights over bottomless gaps,
//     on trunks that aren't solid, and a daytime sky whose parallax layer is
//     clouds over a forest's crowns far below
//   - A perfect bounce (body_bounce 255, with no body_max_fall): burrs
//     dropped by burr trees roll toward the serval, bouncing back up to the
//     height they dropped from on every landing, until they run out of
//     canopy; they hurt on touch (body_overlap). A burr tree is an entity
//     with a position and a timer but no sprite
//   - Paths (path.h, sys_path): birds fly in on a looping weave, dipping and
//     climbing as they come (bodies with BODY_GRAVITY(0))
//   - The stage's update hook steering the frame's objects after
//     objects_update(): caterpillars (the walkers) and the fish turn back
//     over a bottomless drop, and a frog about to hop into one hops in place
//     (map_collision_at below them)
//   - A song in 6/8 (PsgSong.ticks_per_beat 3: a tick is an eighth note)
//
// Demonstrates in stage 1-4, the castle (stage_castle.c, boss.c):
//   - Lava as a hazard: open metatiles tagged TAG_HAZARD, which the serval
//     sinks into and dies in, big or small (the stage's hazard hook), over
//     solid ones tagged too; the surface is only a picture, half a metatile
//     below the floor, so standing on a pit's edge is safe. Enemies and fish
//     that walk into it burn up (map_tags_in on their bodies). Its surface
//     rolls and the torches flicker (tileset_set_tiles).
//   - Fire bars: chains of fireballs turning about their blocks, placed with
//     fx_cos and fx_sin and drawn by the stage's draw hook (sprite_draw), not
//     entities; their contact with the serval is tested by hand
//   - Embers: map bodies resting on the deep lava, drawn behind the playfield
//     (SPRITE_BEHIND_PLAYFIELD) so the lava hides them until they leap out of
//     it
//   - color_mix() for the lava's glow: the backdrop swells and flickers (the
//     effects hook) and flashes when the lever is pulled; the far colonnade's
//     palette is the near stone's mixed toward the backdrop when the stage
//     loads (the load hook)
//   - A boss drawn as a metasprite (SPRITE_ASSET_METASPRITE): the 48x48
//     dragon, nine 16x16 pieces a frame, one entity and one map body, flipped
//     as a whole to face the serval; its fireballs aimed with angle_of(); a
//     white flash drawn with another palette of its sprite group
//     (SPRITE_PALETTE), its own mixed toward white with color_mix when the
//     stage loads; sinking into the lava behind the playfield
//   - camera_stop_x holding the camera at the boss's arena, and a bridge
//     falling one metatile at a time (map_set_cell: 12 of the 64 changes
//     MAP_MAX_CHANGES allows), its pieces tumbling away as debris
//   - The ending hooks (StageDef.ending_start, ending_update): a screen of two
//     map layers, the serval running between them into its den (behind the
//     near one), twinkling stars (tileset_set_tiles), a tune played once
//     (PSG_NO_LOOP)
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
//   - The classic cheat code during a stage, or paused, skips to the next stage (a jingle).
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
//   Stage 1-3, the treetops:
//   - High in a forest under a pale blue sky with white clouds, the forest's
//     crowns far below in a blue-green haze, scrolling at half speed. The
//     serval starts on a great tree's mossy top and jumps from canopy to
//     canopy: leafy crowns with pink blossoms on brown trunks that branch
//     out under them, from the lower part of the screen to near its top,
//     with nothing below them (falling between them loses a life). It jumps
//     up through a canopy from below and lands on it. Leafy sprigs stand in
//     front of it here and there. A bright, lilting tune in A major in 6/8
//     (melody, bass and drums) that loops about every 29 seconds, sped up
//     from 100 to 125 beats per minute when time runs low.
//   - Green caterpillars with orange heads crawl along the canopies, turning
//     back at the ends, and lie flat when stomped; green tree frogs with red
//     eyes hop toward you, but at a canopy's end they hop in place. Blue
//     birds fly in from the right with a chirp, dipping and climbing as they
//     come: stomp one (it vanishes in a sparkle), or pass under it or over
//     it.
//   - Three burr trees: spiky brown burrs drop out of the leaves with a
//     rustle, one every two and a half seconds, and roll toward you along
//     the canopy, spinning and bouncing back up to the same height every
//     time, until they fall off its end. They hurt on touch and can't be
//     stomped: walk under the first and third trees' high bounces, jump the
//     second's low ones.
//   - Gold bonus blocks and pale wooden bricks over some canopies, one with
//     the fish (which turns back at a canopy's end too), and blue gems in
//     the air, most of them over the gaps.
//   - The end: a broad bough on a great tree, the goal pole with a golden
//     acorn standing on a log end, and the great tree's trunk with a hollow
//     at its foot, lit from within; the serval walks into it.
//   Stage 1-4, the castle:
//   - Grey stone halls: through the arches of a dim colonnade scrolling
//     behind, a red-black glow that swells and flickers every two seconds or
//     so; a ceiling of dark masonry with hanging teeth, torches flickering on
//     the walls, a floor of big grey blocks. A tense tune in C minor (melody,
//     a bass in eighth notes, drums) that loops every 32 seconds, sped up from
//     120 to 150 beats per minute when time runs low.
//   - Pits of rolling orange lava, its surface half a metatile below the
//     floor: falling in loses a life, big or small (the serval sinks a few
//     pixels in, hops up and falls away). Embers leap out of some pits with a
//     soft hiss, rise nearly five metatiles above the floor and fall back,
//     every two seconds or so; fire bars of five fireballs turn about iron
//     blocks, a turn in four seconds, some each way. Touching an ember or a
//     fireball hurts.
//   - Black salamanders with yellow blotches walk like beetles and go flat
//     when stomped; one that walks into a pit vanishes in a sparkle.
//     Grey-violet bricks, gold bonus blocks (one with a fish) and gems;
//     one-way stone ledges over a wide lava lake, a low passage, stone pillars
//     across another lake.
//   - At the end the hall opens over a lava lake crossed by a stone bridge,
//     and on it stands a teal dragon with ivory horns and magenta wings, three
//     times as tall as the small serval. As the serval comes near, the dragon
//     roars (a rumble and a falling alarm of notes), and the camera stops with
//     a lever at the screen's right edge. The dragon paces its half of the
//     bridge facing the serval, now throwing its head back and breathing a
//     fireball (a hiss) that flies at the serval, now crouching and hopping
//     straight up (a thud as it lands). Touching it or its fire hurts. Jump
//     over it (most easily as it walks toward you) or run under it while it
//     hops, and walk into the lever: the music stops, a clunk, the hall
//     flashes white, the dragon flashes white and freezes, and the bridge
//     breaks up from the lever's end into tumbling pieces. The dragon falls
//     into the lava (a long hiss) and sinks out of sight, and the serval walks
//     out through a gateway, its portcullis raised, into the moonlit night,
//     with the fanfare.
//   After the last stage:
//   - "STAGE CLEAR!" and the score, then a fade to the savanna at night: a
//     full moon, twinkling stars, an acacia on a rise and under it a mound
//     with a dark opening, the serval's den. A gentle tune in E flat plays
//     once. The serval runs in from the left and into its den, and a "z" over
//     the mound grows and shrinks; "THE BRIDGE FELL, / AND THE DRAGON SANK /
//     INTO THE LAVA.", "THE SERVAL IS HOME, / SAFE IN ITS DEN.", "THE END" and
//     "THANK YOU FOR PLAYING" appear one after another. After 18 seconds (or
//     START, once the serval is home) the title comes back, showing the best
//     score saved and letting SELECT pick any stage.
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
