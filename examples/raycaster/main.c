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
//   - The pixel surface: surface_begin() turns it on for the play screen and
//     surface_end() off again for the results; the game draws into
//     surface_pixels(), the page that isn't on screen, and surface_flip()
//     shows it (view.c, game.c)
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
//     once per picture and a button pressed in either frame counts
//   - 256 colors spent as 31 base colors at 8 brightness levels
//     (surface_set_colors, mixed with color_mix), so darkening a texel with
//     distance is one OR; and a second table that turns the screen red for a
//     moment when the serval is hurt
//   - Sliding doors in a raycaster: thin, set in the middle of their cell,
//     with the wall's depth showing beside them
//   - Sprites over the surface (sprite_group_load after surface_begin: the
//     surface leaves sprites 512 of their 1,024 tiles): the paw and wand,
//     the crosshair, and a HUD of icons and digits, since the text layer is
//     off while the surface is on
//   - Text screens and the surface in one game: the title, the difficulty
//     menu and the results are text; the switch happens behind a fade
//     (screen_set_brightness), to black or, through the moon gate, to white
//   - Simple enemies on a grid map (world.c): wisps that wake when they see
//     the serval (a line of sight walked through the map), chase it around
//     corners by sliding along walls, and keep apart from each other; aimed
//     with angle_of, measured with fx_length
//   - Sound effects by priority and a looping tune with its bass on the wave
//     channel (sound.c)
//   - The frame's CPU use on screen (frame_cpu_permille): SELECT
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then the title on a dark blue screen:
//     "W I S P   W A R R E N" in yellow, three lines of story, a blinking
//     "PRESS START". START or A (a two-note chime) fades to the menu.
//   - "HOW BOLD ARE THE WISPS?": EASY, NORMAL (chosen at first) and HARD, the
//     chosen one in yellow with a ">" and a line about it below; UP and DOWN
//     move (a blip; they wrap around, and repeat when held). The controls are
//     listed underneath. A or START begins, B goes back to the title.
//   - The screen fades in on a first-person view of a small stone room, a
//     plank door with iron bands straight ahead: grey stone blocks with
//     visible texture, a dark ceiling, a brown floor that darkens toward the
//     horizon; walls get darker with distance, and those facing north or
//     south are a shade darker than those facing east or west. At the bottom
//     right of the middle the serval's spotted paw holds a wand with a blue
//     crystal; a small crosshair marks the center. Bottom left: a red heart
//     and "100" (health); bottom right: a blue crystal and "24" (sparks). A
//     slow tune in A minor plays.
//   - The D-pad walks and turns (the paw and wand sway with the steps), L
//     and R step sideways; walls stop the serval and let it slide along
//     them. B opens the door ahead: it rumbles and slides sideways into the
//     wall in half a second, stays open three seconds, and closes unless
//     someone stands in the doorway.
//   - Beyond the door, a hall with four stone pillars and a brazier burning
//     in each corner (their flames flicker, and stay bright however far away
//     they are), then the rest of the warren: mossy tunnels, a wood-panelled
//     room with urns, brick rooms, red banners with a paw print. Pillars,
//     urns and braziers block the way but not the view.
//   - Wisps: pale blue ghosts with angry brows that bob in place until they
//     see the serval, then (a rising three-note call) drift toward it; up
//     close one opens a fanged mouth (a hiss) and a third of a second later
//     the screen flashes red, with a thud, and health drops (6, 10 or 15 by
//     difficulty) unless the serval has backed away. Violet hex wisps keep
//     their distance and cast instead (a quick rising run of notes): the hex
//     lands unless the serval gets out of their sight first, less surely
//     from afar.
//   - A (held) fires a spark about three times a second: the wand flashes,
//     a falling zap sounds, the spark count drops. A wisp under the
//     crosshair flinches (two falling notes); a blue wisp is gone
//     after two sparks and a violet one after four, fading out with a
//     falling run of notes. With no sparks the wand only clicks, and finds
//     one spark again every second and a half (up to 4).
//   - Walking over things picks them up, with a chime: a red flask gives 25
//     health (left alone at full health), blue crystals 12 sparks (to 99 at
//     most), and the gold key (which glows in the dark; a longer chime) puts
//     a key icon beside the health.
//   - One door has gold bands and a keyhole: without the key B only gets two
//     low buzzes; with it, a rising chime, and it opens as a plain door from
//     then on. Behind it, a hall of hex wisps and, in its east wall, the moon
//     gate: a gold arch around a violet swirl. B at the gate ends the game:
//     a fanfare, the screen fades to white, then "THE MOON GATE OPENS" with
//     the time taken, the wisps downed out of those in the warren, and the
//     difficulty.
//   - At 0 health the picture stays red, a falling tune plays, and the
//     screen fades to black: "THE WISPS HAVE YOU", with the same numbers.
//     START or A on either screen goes back to the title.
//   - START pauses (two notes; the tune stops; "PAUSED" over the view) and
//     resumes. SELECT shows, top left, how much of a frame's CPU time the
//     busier of a picture's two frames took: between about 55% and 75%,
//     most with walls and things at middle distances (the web build shows
//     the browser's time instead, usually 0%). Those are an optimized
//     build's numbers. A Debug build isn't optimized: it plays the same at
//     about a third of the speed, the readout above 200%.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (the screens, the two-frame picture, the HUD), view.c (the
// first-person view), world.c (the map, doors, the serval, wisps, things),
// art.c (graphics as ASCII art) and sound.c.

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
