// blackjack: a card table in a bold, bouncy modern style. Bet chips, play
// blackjack against the dealer, and keep your bankroll from one session to
// the next. Also a sketch of what a card game in general (a poker
// roguelike, say) looks like on this engine.
//
// Rules: six decks, reshuffled once three quarters have been dealt (the cut
// card). Bets of 10 to 500 in steps of 10. Blackjack pays 3 to 2. The dealer
// peeks under an ace or a ten for blackjack, and stands on all 17s (soft
// ones too). Double down on any first two cards (one more card, the bet
// doubled), split a pair once (two hands, each with the bet; split aces get
// one card each, and 21 after a split is not a blackjack). No insurance, no
// surrender. A broke player is staked 1000 chips again by the house.
//
// Demonstrates:
//   - Cards as metasprites built at boot: a face is one sprite whose pieces
//     every card shares: a base (face, golden court face, back), a
//     rank-and-suit index top-left and upside down bottom-right, and a big
//     suit, ace or court emblem in the middle. 52 whole faces wouldn't fit
//     sprite VRAM, and which pieces a face shows depends on its rank and
//     suit, so the 52 faces (and glowing variants) are built into SpritePiece
//     frames at boot; art.c explains the design and its costs
//   - Rotation (sprite_draw_rotated) on metasprite cards: the engine turns
//     the pieces about the card's center by one shared angle, so a card
//     takes one or two of the 32 rotation matrices; cards tilt as they fly,
//     wobble on a spring when they land, the selected button sways, banner
//     letters wobble in. table.c limits how many cards tilt at once (rotated
//     sprites cost four times as much of the per-scanline sprite budget)
//   - A card flip without a scaling API: the base squashed to 24, 14 and 4
//     pixels and back, as animation frames built at boot
//   - Tweens with easing (an overshooting slide for a deal, ease-in for a
//     discard), chips flying in arcs, letters dropping in with a damped
//     bounce, rising "+75" pops, a bankroll that counts up and down
//   - Palette variants of the same tiles (SPRITE_PALETTE): a gold outline
//     for a winning hand, shadows, greyed buttons, gold and red digits and
//     banner letters
//   - Entities for sparks (sys_animate, SPRITE_ASSET_ANIM_ONCE) and confetti
//     (sys_movement, tumbling through four shared rotation angles)
//   - A swirling background: two map layers of marbled paint computed at boot
//     from sines (128 pixels square, MAP_LAYER_WRAP), drifting on a slow
//     circle by themselves (MAP_LAYER_FIXED, map_set_scroll); the veil on top
//     moves half as far, so the layers slide over each other. Green felt at the table, plum on the
//     title (two tilesets with the same tiles)
//   - Screen fades (screen_set_brightness) between the title and the table
//   - Music (PsgSong): an original swing tune with a walking bass, and sound
//     effects over it with priorities (a card or a chip never cuts a result's
//     jingle short)
//   - Save data (save.h): the bankroll and a few stats in one versioned slot,
//     written after every round (bank.c)
//   - random_entropy() seeding the shuffle when START is pressed: the same
//     input deals the same cards on the GBA and the web
//
// What to expect when booting the ROM:
//   - The Serval Engine splash: "made with" and the engine's logo (about 3
//     seconds; any button skips it once the logo is in), a moment of black
//     while the art is built, then the title
//     fades in over swirling plum paint: "BLACKJACK" in gold letters that
//     drop in one by one and then wave, "A SERVAL ENGINE CARD TABLE", three
//     cards (ace of spades, king of hearts, jack of diamonds) flying up from
//     below, flipping over in the air and settling into a gently swaying fan,
//     a blinking "PRESS START", the bankroll and best bankroll, and the house
//     rules. A slow swing tune plays (F major, about 38 seconds, looping).
//   - START: a rising chime and a fade to the table, green swirling felt.
//     Top left: the bankroll (1000 chips on a first boot). Top right: the
//     shoe, a pile of card backs that thins as it empties. "PLACE YOUR BET"
//     with the bet in gold and its chips in the middle: LEFT/RIGHT change it
//     by 10, UP/DOWN by 50 (held, they repeat), each step with a chip clink
//     and the new chips dropping onto the pile (button_repeat). A or the swaying DEAL button
//     deals; START goes back to the title.
//   - The deal: the chips slide to the bet's place at the left, the bankroll
//     counts down in red, and four cards hiss out of the shoe one after the
//     other, tilted as they fly, overshooting a little and wobbling as they
//     land with a thump: yours face up (each flips over in the air with a
//     click), the dealer's second face down. Totals appear beside each hand
//     as cards land: "1/11" or "7/17" for soft hands, gold at 21, red when
//     bust. Under an ace or a ten the dealer's hole card lifts and wobbles for
//     a moment (the peek).
//   - Your turn: HIT (green), STAND (red), DOUBLE (orange), SPLIT (blue)
//     along the bottom, greyed when not allowed. LEFT/RIGHT move the
//     selection (a blip; the selected button floats and sways), A chooses (a
//     rising blip; a buzz and a shake for a greyed one), B stands. 21 stands
//     by itself; going over shows "BUST" in red letters with a falling tone.
//     After a split the two hands sit side by side, the one being played
//     raised a little, their totals listed at the left ("HANDS", ">1 13").
//   - The dealer's turn: the hole card flips, the dealer draws to 17 (each
//     card with its hiss and thump), "DEALER BUST" in gold over the dealer's
//     cards if it goes over.
//   - The result, per hand, in letters that drop in and bounce: "WIN!" (gold,
//     an arpeggio, the hand's cards get a gold outline and jiggle, sparks fly,
//     winnings arc over from the dealer and a gold "+50" rises over the
//     hand), "BLACKJACK!" (a longer fanfare and confetti too; pays 3 to 2),
//     "PUSH" (silver, two even notes), "LOSE" (red, a falling three-note
//     phrase, the chips fly to the dealer and a red "-50" rises). Then the
//     chips that came back fly to the bankroll, which counts up in gold with
//     a tick per step, the cards fly off to the left, and the next bet
//     begins. "SHUFFLE" (silver, a riffle) when the cut card has come out.
//   - With fewer than 10 chips left: "REFILL" and "HOUSE STAKES YOU 1000".
//   - The bankroll is saved after every round: power off and on, and the
//     title shows the same bankroll. For demos: on the title, hold L and R
//     and press SELECT to reset it to 1000 ("BANKROLL RESET", a falling
//     tone).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into game.c (screens, the round, HUD), rules.c (the shoe and totals),
// table.c (cards, chips, banners and effects on screen), bank.c (the saved
// bankroll), art.c (graphics, built at boot) and sound.c.

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init(); // builds the art (a quarter of a second), then the title
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
