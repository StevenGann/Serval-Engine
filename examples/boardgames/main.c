// boardgames: "PAWS & KINGS", chess and checkers in one cartridge, against
// the computer at three strengths or for two players. (This file grows with
// the game; its final description comes with the title screen.)
//
// Demonstrates:
//   - A board game's portable core, host-tested (tests/chess_tests.c,
//     tests/checkers_tests.c, tests/boardgames_tests.c): chess's rules and
//     notation proven by perft counts, checkers' with every jump path, one
//     search shared by both games through a table of functions, and the save
//   - Art drawn at boot from pixel rules and ASCII pictures (art.c): the
//     table as one map (a maple and walnut board in a walnut frame with
//     brass labels, on green felt, the side panel), each 16x16 cell a
//     metatile, tiles kept once; both games' pieces as 16x16 sprites in two
//     palettes, the knight a serval's head
//   - map_set_cell() for what changes on the board (view.c): the labels
//     that follow the board when it is flipped, checkers' square numbers,
//     and the highlighted squares, which are the same tiles in another
//     palette bank (the king in check's flashes by palette writes)
//   - SPRITE_BLEND sprites (the move targets) darkening the board under
//     them, the rest of the sprites opaque
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then the board: a two-player chess game at
//     its start, White at the bottom, the board's squares maple and walnut
//     with their grain, its frame lettered a-h along the bottom and 1-8 up
//     the left in brass; ivory and ebony pieces with dark outlines, the
//     knights servals' heads (big ears, amber eyes), the kings with a gold
//     cross. The panel at the right: CHESS in gold, 2 PLAYERS, a king icon
//     and WHITE (to move), MOVE 1, and the rest empty.
//   - The D-pad moves the yellow cursor (corner brackets that pulse) from
//     square to square, repeating when held.
//   - SELECT switches between the chess and the checkers start position
//     (for checking the art only: this key is temporary and goes when the
//     play screen comes): checkers' men are discs with a paw print pressed
//     into them, Dark's twelve at the top, Light's at the bottom, the dark
//     squares numbered 1-32 small in their corners, the frame's letters
//     gone; the panel says CHECKERS and DARK (to move).
//   - No sound yet.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into the portable core (chess_board.c, chess_rules.c, chess_eval.c,
// checkers.c, checkers_eval.c, search.c, match.c, records.c), art.c (the
// pictures, drawn at boot) and view.c (the board and the panel).

#include "art.h"
#include "view.h"

static Match match SERVAL_EWRAM_BSS; // about 27 KB: never on the stack

// The cursor moves on the board as it is seen: UP is toward the top of the
// screen, whichever way up the board is.
static int move_cursor(int sq) {
    int file = SQ_FILE(sq), rank = SQ_RANK(sq);
    int up = view_flipped() ? -1 : 1;
    if (button_repeat(BUTTON_UP))
        rank += up;
    if (button_repeat(BUTTON_DOWN))
        rank -= up;
    if (button_repeat(BUTTON_RIGHT))
        file += up;
    if (button_repeat(BUTTON_LEFT))
        file -= up;
    if (file < 0 || file > 7 || rank < 0 || rank > 7)
        return sq;
    return SQ(file, rank);
}

int main(void) {
    serval_init();
    serval_splash();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    art_build();
    view_init();
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    int cursor = SQ(4, 1);
    int brightness = SCREEN_BRIGHTNESS_MIN;
    button_repeat_reset();
    for (;;) {
        frame_begin();
        if (brightness < 0)
            screen_set_brightness(brightness += 2);
        if (button_pressed(BUTTON_SELECT)) {
            GameKind game = match.game == GAME_CHESS ? GAME_CHECKERS : GAME_CHESS;
            match_new(&match, game, MODE_TWO_PLAYERS, game == GAME_CHESS ? WHITE : CK_DARK,
                      LEVEL_CUB);
        }
        cursor = move_cursor(cursor);
        view_draw_cursor(cursor);
        view_draw_match(&match);
        view_panel(&match, "");
        frame_end();
    }
}
