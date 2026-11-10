// boardgames: "PAWS & KINGS", chess and checkers in one cartridge, against
// the computer at three strengths (CUB, HUNTER, SERVAL) or for two players.
//
// Demonstrates:
//   - Two games through one search (search.c): iterative deepening,
//     alpha-beta, quiescence and a transposition table written once and
//     driven by each game through a SearchGame table of functions
//     (chess_eval.c, checkers_eval.c: generate, make, unmake, evaluate,
//     key, draws), moves crossing it as u32
//   - A board game's portable core, host-tested (tests/chess_tests.c,
//     tests/checkers_tests.c, tests/boardgames_tests.c): chess's rules and
//     notation proven by perft counts, checkers' with every jump path, the
//     search, the match and the save
//   - A computer that thinks across frames: the search keeps its tree on a
//     stack of its own, so it stops when the frame's allowance is done and
//     goes on the next, while the cursor moves, the pieces slide and
//     THINKING animates. The allowance follows the frame (search_pace():
//     the cycles each unit of work took lately, aiming at 76% of the
//     frame); the same position, strength and seed give the same move on
//     every build, however the work is split
//   - Art drawn at boot from pixel rules and ASCII pictures (art.c): the
//     table as one map (a maple and walnut board in a walnut frame with
//     brass labels, on green felt, the side panel), each 16x16 cell a
//     metatile, tiles kept once; both games' pieces as 16x16 sprites in two
//     palettes, the knight a serval's head; the logo's letters blown up
//     from a 5x7 font into four 64x32 sprites
//   - map_set_cell() for what changes on the board (view.c): the labels
//     that follow the board when it is flipped, checkers' square numbers,
//     and the highlighted squares, which are the same tiles in another
//     palette bank (the king in check's flashes by palette writes); the
//     whole picture dimmed behind the menus by palette writes, the logo
//     left bright
//   - Saves (records.c, save.h): one versioned struct holding each game's
//     record per strength and the game in progress as one byte a move (its
//     index in the legal-move list), written after every move outside the
//     search and replayed by CONTINUE
//   - SPRITE_BLEND sprites (the move targets) darkening the board under
//     them, the rest of the sprites opaque
//   - PSG sound effects (sound.c): knocks on the noise channel, chimes and
//     jingles on the squares
//   - button_repeat() for the cursor and the menus; frame_cpu_permille() for
//     a CPU readout
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then the title fades in: PAWS & KINGS in
//     gold letters (the & in ivory) over the dimmed, empty board, a menu
//     under it and two pieces of the game the cursor is on facing each
//     other at the board's foot (servals' heads for chess, crowned discs for
//     checkers; the board shows checkers' square numbers then). CONTINUE,
//     with the saved game's kind under it ("CHESS GAME"), only when a game
//     is saved; CHESS; CHECKERS. UP/DOWN move the cursor (a tick), A or
//     START chooses (a knock). The panel at the right shows the record of
//     the game the cursor is on: wins, draws and losses against CUB, HUNTER
//     and SERVAL, and the two-player games played. If the last write
//     failed, a red SAVE FAILED shows there (the game still plays).
//   - CONTINUE resumes the saved game (either kind) exactly where it was
//     left, settings and board side included. A save that can no longer be
//     replayed is dropped with "SAVED GAME LOST" in red and a low "no".
//   - Holding L + R and pressing SELECT (in any order) asks, in red, "ERASE
//     RECORDS AND SAVED GAME?" (a two-note chime): A erases both and says
//     RECORDS ERASED (a hard knock), or, if the write failed, SAVE FAILED in
//     red and the low "no"; B leaves everything as it was (a tick).
//   - After 20 seconds with no button held the attract mode starts: the
//     board brightens and the computer plays itself at CUB, chess the first
//     time, checkers the next, and so on, in silence, the panel as in play
//     and its status line saying DEMO GAME and PRESS ANY BUTTON by turns.
//     It stops at the end of the game or after 80 moves (160
//     plies), holds the last position four seconds and goes back to the
//     menu; any button goes back at once (and does nothing else). It saves
//     nothing.
//   - The setup, after choosing a game, over the dimmed board: PLAYERS (1
//     PLAYER or 2 PLAYERS) and, for one player, YOUR SIDE (chess WHITE,
//     BLACK or RANDOM; checkers DARK, LIGHT or RANDOM, with "DARK MOVES
//     FIRST" under it) and STRENGTH (CUB, HUNTER, SERVAL). UP/DOWN pick a
//     line, LEFT/RIGHT change it (a tick; the board's labels turn to the
//     side chosen), A or START begins (a knock), B goes back to the title.
//   - The board: squares of maple and walnut, its frame lettered A-H along
//     the bottom and 1-8 up the left in brass (chess; checkers numbers its
//     dark squares 1-32 small in their corners instead); ivory and ebony
//     pieces with dark outlines, the knights servals' heads (big ears, amber
//     eyes), the kings with a gold cross; checkers' men discs with a paw
//     print, its kings crowned. In one-player games the human's side is at
//     the bottom; playing Black (chess) or Light (checkers) the computer
//     moves first. The panel at the right: the game, the opponent ("VS
//     CUB") or 2 PLAYERS, whose turn and the move number, the pieces each
//     side has taken, the last six moves (SAN in chess, 11-15 / 15x24 in
//     checkers) and the status: YOUR MOVE, THINKING with a moving dot,
//     CHECK in red, MUST JUMP, or the result.
//   - The D-pad moves the yellow cursor (corner brackets that pulse),
//     repeating when held. A on one of your pieces picks it up (a tick; its
//     square turns green) and shows where it can go: dots, and rings on
//     captures; a piece that cannot move shows none (a low "no"). A on a
//     dot moves it: it slides there (a wooden knock), a captured piece goes
//     in a puff of dust (a harder knock), castling slides the rook after the
//     king (a double knock), en passant takes the passed pawn. A on another
//     of your pieces picks that one instead; B or A on the same piece puts
//     it down. A pawn reaching the far rank opens a chooser: a queen, rook,
//     bishop and knight on green squares along the rank; LEFT/RIGHT choose,
//     A promotes (a chime and a sparkle), B cancels the move. The last
//     move's squares are tinted yellow; a king in check has its square
//     flashing red (a two-note chime).
//   - Checkers: when a capture is forced, the pieces that can capture are
//     ringed in orange and MUST JUMP shows; picking another piece gives the
//     "no". A multi-jump is landed one jump at a time: each landing waits
//     on its square for the next (B goes back to the start of the move);
//     the last landing plays it, each jumped man going in a puff. A man
//     reaching the far row is crowned with a sparkle and a chime.
//   - The computer (one-player games) thinks while the board stays alive:
//     THINKING with a moving dot, the cursor still moves. CUB answers in
//     about a second and makes mistakes, HUNTER in two or three, SERVAL in
//     about seven. Every move is saved as it is played.
//   - SELECT flips the board (pieces, cursor, labels and effects follow,
//     even mid-slide). L shows the CPU used a frame at the panel's foot.
//   - START: the menu over the dimmed board: RESUME, UNDO (one player: your
//     last move and the computer's reply; the computer stops thinking
//     first; dimmed while you have no move to take back), FLIP BOARD, NEW
//     GAME, QUIT TO TITLE (the game stays saved: CONTINUE). B or START
//     resumes.
//   - The end: the result over the dimmed board (CHECKMATE, STALEMATE, DRAW
//     BY REPETITION, FIFTY-MOVE DRAW, DRAW: NOT ENOUGH MATERIAL; checkers'
//     NO MOVES LEFT, DRAW: 40 MOVES; DRAW: GAME TOO LONG in either), who
//     won, a jingle (higher for a win than for a loss or a draw); the
//     record is saved and the saved game forgotten; A goes back to the
//     title.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into the portable core (chess_board.c, chess_rules.c, chess_eval.c,
// checkers.c, checkers_eval.c, search.c, match.c, records.c), art.c (the
// pictures, drawn at boot), view.c (the board and the panel), screens.c
// (the title, the setup and the attract mode), play.c (the play screen) and
// sound.c. main.c runs one screen a frame.

#include "art.h"
#include "play.h"
#include "records.h"
#include "screens.h"
#include "sound.h"
#include "view.h"

static Match match SERVAL_EWRAM_BSS; // about 27 KB: never on the stack

int main(void) {
    serval_init();
    serval_splash();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    art_build();
    view_init();
    sound_init();
    board_records_load();
    Screen screen = SCREEN_TITLE;
    screens_title_deaf(true); // buttons are ignored while the title fades in
    for (int b = SCREEN_BRIGHTNESS_MIN; b <= 0; b += 2) {
        frame_begin();
        screen_set_brightness(b);
        screens_title_frame(&match);
        frame_end();
    }
    screens_title_deaf(false);
    for (;;) {
        frame_begin();
        Screen next = screen;
        switch (screen) {
        case SCREEN_TITLE:
            next = screens_title_frame(&match);
            break;
        case SCREEN_SETUP:
            next = screens_setup_frame(&match);
            break;
        case SCREEN_PLAY:
            if (play_frame() == PLAY_QUIT_TO_TITLE)
                next = SCREEN_TITLE;
            break;
        }
        frame_end();
        if (next == SCREEN_PLAY && screen != SCREEN_PLAY)
            play_begin(&match);
        screen = next;
    }
}
