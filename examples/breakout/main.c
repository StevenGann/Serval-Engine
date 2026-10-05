// breakout: "Paw Breaker", a brick breaker. Bounce a ball off a serval-spotted
// paddle to break every brick on four levels; catch the power-ups that fall.
//
// Demonstrates:
//   - Bricks as entities (84 on the first level), each a 16x8 body and sprite.
//     body_hit_side() tells which side of a brick the ball hit, so it bounces
//     off that side; a grid of the bricks' slots narrows the test to the
//     bricks around the ball and tells shared faces from exposed ones
//     (play.c explains why bricks aren't map cells here, and what that would
//     have changed)
//   - Engine physics for the balls (sys_physics: bounds inside the frame,
//     the bottom edge open so a missed ball falls out) and a paddle whose
//     velocity sys_movement applies, so body_hit_side() sees it move
//   - A constant ball speed: directions are angles (fx_sin, fx_cos), every
//     bounce mirrors the angle, and no bounce leaves the ball flatter than 25
//     degrees: no horizontal loops. The speed limit keeps the ball from
//     jumping over a brick.
//   - Game components (C_GAME) with their own arrays, and ecs_count(): bricks
//     left to break, balls in play, capsules falling, room for effects
//   - Animated sprites (sys_animate with frame_times): capsules glint, broken
//     bricks leave dust that plays once (SPRITE_ASSET_ANIM_ONCE)
//   - Several sprites sharing tiles with different palettes (eight brick
//     colors and a white hit flash); a paddle drawn in pieces with
//     sprite_draw, wider after a power-up
//   - Map layers as backgrounds (map.h): a pipe frame on background 2 and a
//     wrapping pattern of serval spots on background 3, in each level's colors
//   - Screen fades between title, levels and the score table
//     (screen_set_brightness); text with a drop shadow, text_print_centered
//   - Music (PsgSong): an original looping tune on all three tone generators,
//     paused with the game; a fanfare played once per cleared level; sound
//     effects over it with priorities (a jingle isn't cut short by bricks)
//   - Save data (save.h): the five best scores and levels, in one versioned
//     slot (scores.c)
//   - Seeding random numbers with random_entropy(): the same input plays the
//     same game on the GBA and the web
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash (about 3 seconds; any button skips it
//     once the text is in).
//   - The title fades in: a dark navy background of serval spots inside a
//     grey pipe frame, "P A W   B R E A K E R", an arch of colored, silver
//     and gold bricks, an orange spotted paddle with a ball bobbing over it, a
//     blinking "PRESS START" and the controls. The top row shows the best
//     score. Every 5 seconds it switches to "BEST SCORES": five scores with
//     the level each reached (made-up ones on a first boot).
//   - START: a rising chime, a fade to level 1, "STRIPES": six rows of
//     purple, blue, green, yellow, orange and red bricks. The tune starts (A
//     minor, about 27 seconds, looping). The top row shows the score, the
//     best score, the level and a paw print per life (3 to start).
//   - The ball waits on the paddle: Left/Right move the paddle (and the ball
//     with it); A launches it with a rising blip (after 4 seconds it goes by
//     itself). It bounces off the frame (a low tick) and the paddle (a
//     blip): the farther from the paddle's center it lands, the more it
//     angles away, up to 60 degrees from vertical.
//   - A brick breaks with a blip pitched by its color (red lowest, purple
//     highest: 10 to 60 points) and a puff of dust. Silver bricks crack on the
//     first hit (a hiss, a white flash) and break on the second (100 points);
//     gold bricks never break (a metallic clank, a white flash). The ball
//     speeds up a little every 8 bricks.
//   - Some broken bricks drop a glinting capsule that falls with gravity;
//     catch it with the paddle (an arpeggio, 100 points): W (blue) widens the
//     paddle, M (purple) splits the ball into three, S (orange) slows the
//     ball, C (green) makes the paddle glow cyan and catch the ball (A
//     releases it, or it goes by itself after 2.5 seconds), and a heart (red,
//     rarer) adds a life (a jingle; at most 5).
//   - Missing the last ball: a falling tone, a paw print goes, and a new ball
//     waits on a normal paddle. With no lives left: "GAME OVER" and a sad tune;
//     a score that makes the table is saved and marked "NEW BEST SCORE: #n",
//     then after a fade the table shows it between blinking arrows.
//   - Breaking the last breakable brick: "LEVEL CLEAR!", a fanfare, 1000
//     bonus points, and a fade to the next level: 2 "THE SERVAL" (a serval's
//     head with silver ears and gold eyes, teal background), 3 "FORTRESS"
//     (gold walls to get around, plum), 4 "DIAMOND" (rust), then level 1's
//     layout again with a faster ball.
//   - START pauses ("PAUSED", a tick, the music stops) and resumes (the tune
//     carries on where it stopped).
//   - For demos: on the title, hold L and R and press SELECT to erase the
//     saved table ("SCORES RESET", a falling tone).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, L = A,
//   R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (screens, states, HUD), play.c (paddle, balls, bricks,
// power-ups), levels.c (the brick layouts), scores.c (the saved table),
// art.c (graphics, converted from ASCII pixel art) and sound.c.

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
