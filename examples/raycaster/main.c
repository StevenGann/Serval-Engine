// raycaster: "Wisp Warren", a first-person maze game in the manner of the
// early 1990s' texture-mapped shooters. A serval with a spark wand finds the
// key and the way out of a warren that wisps have taken over.
//
// The engine's first step into 3D, and what it took was small: the pixel
// surface (surface.h), a picture the game draws itself, one byte per pixel,
// which the hardware stretches over the screen. Everything first-person
// about this example is the game's own code: the rays, the textured columns,
// the things drawn over them, the map, the doors. Another kind of 3D (a rail
// shooter's ground plane, filled polygons) would draw other pixels into the
// same surface.
//
// Demonstrates:
//   - The pixel surface: surface_begin() turns it on at boot and it stays on
//     on every screen; the game draws into surface_pixels(), the page that
//     isn't on screen, and surface_flip() shows it (view.c, game.c)
//   - Letting the hardware stretch the picture (surface_set_transform): the
//     surface is turned on its side, so that a column of the view is 160
//     consecutive bytes, and shown at half width, so 120 columns fill the
//     240-pixel screen. Neither costs any CPU time
//   - A raycaster on a 16.78 MHz CPU with no divide instruction (view.c):
//     16.16 fixed point, a table of reciprocals in place of division, the
//     drawing code in fast RAM as ARM code (SERVAL_IWRAM_CODE), textures
//     stored column after column, a column drawn in RAM and copied to video
//     memory in words, near walls and things drawn two pixels a texel
//   - A picture drawn over two frames: 52 columns in one frame, 68 and the
//     flip in the next (the first also steps the world), so the view runs
//     at 30 pictures a second and neither frame runs over; the world steps
//     once per picture and a button pressed in either frame counts. Behind
//     the title the split follows what the last picture's halves took
//   - 256 colors spent as 31 base colors at 7 brightness levels, fading to
//     a blue haze, plus a row for the hex wisps' colors (surface_set_colors,
//     mixed with color_mix), so darkening a texel with distance is one OR;
//     glowing colors (fire, gold, eyes) darken less; a second table turns
//     the screen red for a moment when the serval is hurt, a third washes
//     it with light when an urn bursts, and darker ones dim the view behind
//     the menus
//   - Screen shake for free: the surface's transform moved a pixel or three
//     each picture (surface_set_transform), when an urn bursts or a blow lands
//   - Sliding doors in a raycaster: thin, set in the middle of their cell,
//     with the wall's depth showing beside them; animated walls (torches,
//     the moon gate's swirl)
//   - Menus without a text layer: the title, the difficulty menu and the
//     results are sprites over the running view. Text is composed at run
//     time from the game's own two fonts into 32-pixel-wide sprite frames,
//     so a line costs a hardware sprite per 32 pixels (title.c,
//     sprite_set_tiles for lines that change); a drawn logo; a band of dark
//     glass (SPRITE_BLEND, screen_set_blend); pulsing colors
//     (sprite_set_colors); the title's and the menu's sprite groups loaded in
//     turn above the HUD's (sprite_groups_mark, sprite_groups_release). The
//     surface leaves sprites 512 of their 1,024 tiles
//   - An attract mode: the game's own world and view behind the title
//   - Fades (screen_set_brightness): to black, or through the moon gate to
//     white
//   - Things that move and push back (world.c): urns that a spark knocks
//     sliding across the floor, stopped by walls, doors, wisps and each
//     other, and that burst at the third spark, hurting what is near and
//     setting off the urns near them
//   - Simple enemies on a grid map (world.c): wisps that wake when they see
//     the serval (a line of sight walked through the map), chase it around
//     corners by sliding along walls, and keep apart from each other; aimed
//     with angle_of, measured with fx_length
//   - Music on the four tone generators: two tunes sharing a motif, the
//     melody and bass on the channels effects leave alone, custom waves for
//     the wave channel's bass; effects by priority (sound.c)
//   - The frame's CPU use on screen (frame_cpu_permille): SELECT
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then, after half a second of black, the
//     title fades in over the warren's pillar hall, dimmed to a moonlit
//     blue: the camera circles slowly while wisps (one of them violet) drift
//     round the hall behind the pillars. "WISP" in icy blue, its I topped by
//     a flickering blue flame, drops in letter by letter, then "WARREN" in
//     gold, then a pale crescent moon; the letters bounce as they land. WISP
//     then floats gently and every few seconds a white glint runs across the
//     letters. "PRESS START" pulses gold to near white; under it a line of
//     the story at a time fades in and out ("Wisps have taken the warren.",
//     "Find the key.", "Reach the moon gate."). A harp-like arpeggio opens a
//     slow, mysterious waltz in D minor; drums join quietly halfway, and it
//     swells to a small fanfare before it loops (about 46 seconds).
//   - START or A (a two-note chime): the logo flies up off the screen letter
//     by letter and the view darkens. A band of dark glass with gold edges
//     opens across the upper two thirds, the view still moving behind it:
//     "HOW BOLD ARE THE WISPS?" in gold, then EASY, NORMAL and HARD in big
//     letters. NORMAL is chosen at first: gold, on a blue bar, a twinkling
//     spark at each side; the others pale blue-grey. Below them a line about
//     the choice. Under the band, the controls in two columns with button
//     icons, and "A begin   B back". UP and DOWN (a blip; they wrap around,
//     and repeat when held) slide the bar to the next line. A or START (the
//     chime) begins: the screen fades to black and play starts; B (a blip)
//     closes the band and the title comes back.
//   - Play fades in on a small stone room: straight ahead a plank door with
//     iron bands, a barred window and a ring pull, a wall torch burning on
//     each side. Walls are bevelled grey blocks lit from the upper left; they
//     fade toward a deep blue haze with distance, and those facing north or
//     south are a shade darker. Torches, the moon gate, wisps' eyes, crystals
//     and the key stay bright however far away they are. The ceiling is near
//     black and the floor brown, both darkening toward the horizon in fine
//     dithered steps. Bottom right of the middle, the serval's spotted paw
//     holds a wooden wand with a faceted blue crystal; a small ring
//     crosshair marks the center. Bottom left: a red heart and "100"
//     (health); bottom right: a blue crystal and "24" (sparks). A tense march
//     in A minor plays: a reedy bass, march drums, a melody on the title's
//     motif; a quieter creeping middle part, then a snare roll into a high,
//     grand last part (about 72 seconds a loop).
//   - The D-pad walks and turns (the paw and wand sway with the steps), L
//     and R step sideways; walls stop the serval and let it slide along
//     them. B opens the door ahead: it rumbles and slides sideways into the
//     wall in half a second, stays open three seconds, and closes unless
//     someone stands in the doorway.
//   - Beyond the door, a hall with four fluted stone pillars and a brazier
//     on a tripod in each corner, its fire flickering and throwing sparks;
//     then the rest of the warren: mossy tunnels with drain grates at the
//     foot of the walls, a wood-panelled room with bookcases and urns (red
//     clay amphoras with a blue band), brick rooms with barred windows, red
//     banners with a paw print, torches along the corridors. Pillars,
//     braziers and urns block the way but not the view (urns move: below).
//   - Wisps: big blue ghosts with a hooked tip, heavy black brows over
//     glowing orange eyes and a toothy grin, ragged hems trailing off into
//     see-through wisps, each drifting in its own rhythm. They bob in place
//     until they see the serval, then (a rising three-note call) come for
//     it; up close one rears up, arms raised (a hiss), then lunges with a
//     fanged red maw: the screen flashes red, with a thud, and health drops
//     (6, 10 or 15 by difficulty) unless the serval has backed away; the
//     picture jolts with the blow. Violet
//     hex wisps with green eyes keep their distance and cast instead: they
//     raise a green orb overhead and hurl it (a quick rising run of notes);
//     the hex lands unless the serval gets out of their sight first, less
//     surely from afar.
//   - A (held) fires a spark about three times a second: the wand's crystal
//     flashes and bursts, a falling zap sounds, the spark count drops. A
//     wisp under the crosshair flinches white, eyes squeezed shut (two
//     falling notes); a blue wisp is gone after two sparks and a violet one
//     after four, breaking up into fading motes with a falling run of
//     notes. A spark that hits a wall bursts there in a small blue-white
//     star. With no sparks the wand only clicks, and finds one spark again
//     every second and a half (up to 4), so the wand never stays empty.
//   - Urns hold lamp oil. A spark knocks one with a hollow clay knock and
//     sends it sliding a little way along the spark's path until a wall, a
//     door, a pillar, a wisp, another urn or the serval stops it; the first
//     spark cracks it, the second sets the oil glowing through the cracks
//     with smoke curling from the neck, and the third bursts it: a white
//     flash, a fireball, smoke and embers and clay shards (about 0.4
//     seconds), the whole screen washed bright for a moment and shaking,
//     a deep roar of noise with a thump under it. The blast takes 3 health
//     from every wisp within about two cells that it can reach (a blue wisp
//     is gone, a violet one nearly) and sets off any urn as near a moment
//     later, so the paired urns of the brick rooms go up together. The
//     serval is hurt only within a cell and a half (8, 12 or 16 by
//     difficulty, half that at the edge), and a spark carries farther than
//     that. One urn stands just inside the door of the room with the key:
//     walking in is blocked until it is pushed in or burst.
//   - At 25 health or less the heart in the HUD beats.
//   - Walking over things picks them up, with a chime and a glint of gold
//     over the view: a round red potion flask gives 25 health (left alone at full health), a
//     cluster of blue crystals 12 sparks (to 99 at most), and the sparkling gold key (a longer
//     chime) puts a key icon beside the health.
//   - One door has a gold frame and a large gold keyhole plate: without the
//     key B only gets two low buzzes; with it, a rising chime, and it opens
//     as a plain door from then on. Behind it, a hall of hex wisps and, in
//     its east wall between crescent-moon reliefs and torches, the moon
//     gate: a gold arch around a turning violet swirl. B at the gate ends the
//     game: the music stops, a bright fanfare plays (the wisps' call in A
//     major, rising to a held high note), the screen fades to white and
//     comes back on the last picture in pale moonlit blue under a glass
//     band: "THE MOON GATE OPENS" in gold, "The serval slips out into the
//     night.", then TIME, WISPS (downed of those in the warren) and
//     DIFFICULTY, and a pulsing "PRESS START".
//   - At 0 health the picture stays red, the music stops and a falling tune
//     sinks to a low note, and the screen fades to black and back on the
//     last picture in dark red: "THE WISPS HAVE YOU" in red, "The warren
//     keeps its serval.", the same numbers. START or A on either closes the
//     band and fades back to the title.
//   - START pauses (two notes; the music freezes; "PAUSED" over the view)
//     and resumes the music where it was. SELECT, on any screen, shows top
//     left how much of a frame's CPU time the busier of a picture's two
//     frames took: between about 55% and 80% in play, most with walls and
//     things at middle distances, and up to about 75% behind the menus (the
//     web build shows the browser's time instead, usually 0%). Those are an
//     optimized build's numbers. A Debug build isn't optimized: it plays the
//     same at about a third of the speed, the readout above 200%.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (the screens, the two-frame picture, the HUD, the tour behind
// the title), title.c (the logo, the fonts and the text composed from them,
// the menu's glass), view.c (the first-person view), world.c (the map,
// doors, the serval, wisps, things), art.c (the game's graphics as ASCII
// art) and sound.c (the music and the effects).

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init(); // converts the art, then the title
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
