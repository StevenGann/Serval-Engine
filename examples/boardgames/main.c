// boardgames: "PAWS & KINGS", chess and checkers in one cartridge, against
// the computer at three strengths or for two players. (This file grows with
// the game; its final description comes with the title screen.)
//
// Demonstrates:
//   - A board game's portable core, host-tested (tests/chess_tests.c,
//     tests/checkers_tests.c, tests/boardgames_tests.c): chess's rules and
//     notation proven by perft counts, checkers' with every jump path, one
//     search shared by both games through a table of functions, and the save
//   - A computer opponent that thinks across frames (search.c): the search
//     keeps its tree on a stack of its own, so it stops when the frame's
//     share of work is done (as much as the last frames' cost says fits in
//     the frame) and goes on the next, while the cursor moves, the pieces
//     slide and THINKING animates; the same position, strength and seed
//     give the same move on every build, however the work is split
//   - Art drawn at boot from pixel rules and ASCII pictures (art.c): the
//     table as one map (a maple and walnut board in a walnut frame with
//     brass labels, on green felt, the side panel), each 16x16 cell a
//     metatile, tiles kept once; both games' pieces as 16x16 sprites in two
//     palettes, the knight a serval's head
//   - map_set_cell() for what changes on the board (view.c): the labels
//     that follow the board when it is flipped, checkers' square numbers,
//     and the highlighted squares, which are the same tiles in another
//     palette bank (the king in check's flashes by palette writes); the
//     whole picture dimmed behind the menu by palette writes
//   - SPRITE_BLEND sprites (the move targets) darkening the board under
//     them, the rest of the sprites opaque
//   - PSG sound effects (sound.c): knocks on the noise channel, chimes and
//     jingles on the squares
//   - button_repeat() for the cursor and the menus; frame_cpu_permille() for
//     a CPU readout
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, then a temporary chooser over the dimmed
//     board (Task 8 replaces it with the title and the setup screens): GAME
//     CHESS or CHECKERS, PLAYERS 1 or 2, SIDE (the human's colour), LEVEL
//     CUB, HUNTER or SERVAL. UP/DOWN pick a line, LEFT/RIGHT change it, A
//     starts the game; CONTINUE, shown when a game is saved, resumes it.
//   - The board: squares of maple and walnut, its frame lettered A-H along
//     the bottom and 1-8 up the left in brass (chess; checkers numbers its
//     dark squares 1-32 small in their corners instead); ivory and ebony
//     pieces with dark outlines, the knights servals' heads (big ears, amber
//     eyes), the kings with a gold cross; checkers' men discs with a paw
//     print, its kings crowned. In one-player games the human's side is at
//     the bottom. The panel at the right: the game, the opponent ("VS CUB")
//     or 2 PLAYERS, whose turn and the move number, the pieces each side has
//     taken, the last six moves (SAN in chess, 11-15 / 15x24 in checkers)
//     and the status: YOUR MOVE, THINKING with a moving dot, CHECK in red,
//     MUST JUMP, or the result.
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
//     about seven.
//   - SELECT flips the board (pieces, cursor, labels and effects follow,
//     even mid-slide). L shows the CPU used a frame at the panel's foot.
//   - START: the menu over the dimmed board: RESUME, UNDO (one player: your
//     last move and the computer's reply; the computer stops thinking
//     first; dimmed while you have no move to take back), FLIP BOARD, NEW
//     GAME, QUIT TO TITLE (the game stays saved). B or START resumes.
//   - The end: the result over the dimmed board (CHECKMATE, STALEMATE, DRAW
//     BY REPETITION, FIFTY-MOVE DRAW, DRAW: NOT ENOUGH MATERIAL; checkers'
//     NO MOVES LEFT, DRAW: 40 MOVES; DRAW: GAME TOO LONG in either), who
//     won, a jingle (higher for a win than for a loss or a draw); A goes
//     back to the chooser and the record is saved.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The game is split
// into the portable core (chess_board.c, chess_rules.c, chess_eval.c,
// checkers.c, checkers_eval.c, search.c, match.c, records.c), art.c (the
// pictures, drawn at boot), view.c (the board and the panel), play.c (the
// play screen) and sound.c.

#include "art.h"
#include "play.h"
#include "records.h"
#include "sound.h"
#include "view.h"

static Match match SERVAL_EWRAM_BSS; // about 27 KB: never on the stack

// --- The temporary chooser (Task 8 replaces it with the title) ------------------

enum { ROW_CONTINUE, ROW_GAME, ROW_PLAYERS, ROW_SIDE, ROW_LEVEL, ROWS };
static int choice[ROWS] = {0, 0, 0, 0, 0};
static const int choices[ROWS] = {1, 2, 2, 2, LEVEL_COUNT};

static void draw_chooser(int row) {
    static const char* const games[2] = {"CHESS", "CHECKERS"};
    static const char* const players[2] = {"1 PLAYER", "2 PLAYERS"};
    static const char* const sides[2][2] = {{"WHITE", "BLACK"}, {"DARK", "LIGHT"}};
    static const char* const levels[LEVEL_COUNT] = {"CUB", "HUNTER", "SERVAL"};
    text_clear_area(0, 0, 19, TEXT_ROWS);
    const char* lines[ROWS] = {
        "CONTINUE",
        games[choice[ROW_GAME]],
        players[choice[ROW_PLAYERS]],
        sides[choice[ROW_GAME]][choice[ROW_SIDE]],
        levels[choice[ROW_LEVEL]],
    };
    for (int i = board_records_has_game() ? 0 : 1; i < ROWS; i++) {
        text_set_style(i == row ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_centered_in(1, 18, 5 + 2 * i,
                               i == row ? text_format("< %s >", lines[i]) : lines[i]);
    }
    text_set_style(STYLE_DIM);
    text_print_centered_in(1, 18, 17, "A: PLAY");
    text_set_style(TEXT_NORMAL);
}

// Runs the chooser until A; sets up match.
static void choose(void) {
    int row = board_records_has_game() ? ROW_CONTINUE : ROW_GAME;
    view_set_dim(true);
    draw_chooser(row);
    button_repeat_reset();
    for (;;) {
        frame_begin();
        int first = board_records_has_game() ? ROW_CONTINUE : ROW_GAME;
        int step = button_repeat(BUTTON_DOWN) ? 1 : button_repeat(BUTTON_UP) ? -1 : 0;
        int turn = button_repeat(BUTTON_RIGHT) ? 1 : button_repeat(BUTTON_LEFT) ? -1 : 0;
        if (step && row + step >= first && row + step < ROWS) {
            row += step;
            draw_chooser(row);
        } else if (turn) {
            choice[row] = (choice[row] + choices[row] + turn) % choices[row];
            draw_chooser(row);
        }
        if (button_pressed(BUTTON_A | BUTTON_START)) {
            random_seed(random_entropy()); // the computer's choices differ from game to game
            if (row != ROW_CONTINUE || !board_records_restore_game(&match)) {
                GameKind game = choice[ROW_GAME] ? GAME_CHECKERS : GAME_CHESS;
                int side = game == GAME_CHECKERS ? (choice[ROW_SIDE] ? CK_LIGHT : CK_DARK)
                                                 : (choice[ROW_SIDE] ? BLACK : WHITE);
                match_new(&match, game, choice[ROW_PLAYERS] ? MODE_TWO_PLAYERS : MODE_ONE_PLAYER,
                          side, choice[ROW_LEVEL]);
            }
            view_set_dim(false);
            text_clear_area(0, 0, 19, TEXT_ROWS);
            frame_end();
            return;
        }
        view_draw_match(&match);
        view_panel(&match, "");
        frame_end();
    }
}

int main(void) {
    serval_init();
    serval_splash();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while VRAM is written
    art_build();
    view_init();
    sound_init();
    board_records_load();
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    for (int b = SCREEN_BRIGHTNESS_MIN; b < 0; b += 2) {
        frame_begin();
        screen_set_brightness(b + 2);
        view_draw_match(&match);
        view_panel(&match, "");
        frame_end();
    }
    for (;;) {
        choose();
        play_begin(&match);
        for (;;) {
            frame_begin();
            PlayExit exit = play_frame();
            frame_end();
            if (exit == PLAY_QUIT_TO_TITLE)
                break;
        }
    }
}
