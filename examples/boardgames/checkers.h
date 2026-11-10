#ifndef BOARDGAMES_CHECKERS_H
#define BOARDGAMES_CHECKERS_H

// Checkers (English draughts): the board, legal moves with every jump path,
// make and unmake, Zobrist keys, results, notation and perft. Portable C only
// (no hardware), so tests/checkers_tests.c runs it on the host.
//
// The rules: men step one square diagonally forward, kings one square either
// way (no flying kings). Captures are compulsory, but the player picks any of
// them (no rule to take the most); a multi-jump must be completed; a man that
// reaches the far row is crowned and its move ends there. CK_DARK moves first.
// A side with no legal move loses.
//
// Squares are 0..31, the dark squares, numbered as the standard notation's
// 1..32 minus one, with Dark's men starting on 1-12 at the top of the board
// as checkers literature prints it, and Light's on 21-32 at the bottom.
// File 0..7 is a..h, left to right; rank 0..7 is rank 1..8, bottom to top
// ("up" is toward rank 8, the way Light's men move; Dark's men move down).
// Square s is on row k = s / 4 from the top (rank 7 - k); rows 0, 2, 4, 6
// hold files b, d, f, h and rows 1, 3, 5, 7 files a, c, e, g:
//
//        a  b  c  d  e  f  g  h
//     8     1     2     3     4      (Dark's men start on 1-12;
//     7  5     6     7     8          Light's crowning row is 1-4)
//     6     9    10    11    12
//     5 13    14    15    16
//     4    17    18    19    20
//     3 21    22    23    24
//     2    25    26    27    28      (Light's men start on 21-32;
//     1 29    30    31    32          Dark's crowning row is 29-32)
//
// So a1 (29) is each side's single corner and 1-5 and 28-32 the double
// corners. Nothing here divides.
//
// Call ck_start() or ck_from_text() before any other ck_ function: they build
// the move tables and keys the others use (once, at first use).

#include "chess_board.h" // ChessKey, MAX_MOVES, GAME_PLY_MAX, SEARCH_PLY_MAX
#include "game_result.h"

// A piece is CK_EMPTY or colour | kind.
enum { CK_EMPTY = 0, CK_MAN = 1, CK_KING = 2 };
enum { CK_DARK = 0, CK_LIGHT = 4 }; // the colour bit; CK_DARK moves first
#define CK_KIND(p) ((p) & 3)
#define CK_COLOR(p) ((p) & 4)

// Directions, "up" toward rank 8.
enum { CK_UP_LEFT = 0, CK_UP_RIGHT = 1, CK_DOWN_LEFT = 2, CK_DOWN_RIGHT = 3 };

// A move: from (bits 0-4) | step count (bits 5-8: 0 for a simple move of one
// step, else the number of jumps) | directions (2 bits a step from bit 9:
// the simple move's one direction, or each jump's in turn). A move is its
// path: two jumps that capture the same pieces by different routes are two
// moves (only a king can do that). At most CK_JUMPS_MAX jumps fit in 32
// bits; a longer jump (no such position is known to be reachable) stops there.
typedef u32 CkMove;
#define CK_MOVE_NONE 0xFFFFFFFFu
#define CK_JUMPS_MAX 11
#define CK_MAX_MOVES MAX_MOVES // room ck_gen_legal() needs; it never writes more
#define CK_MOVE_FROM(m) ((int)((m) & 31))

// What ck_unmake() needs to take a move back: 8 bytes a ply.
typedef struct {
    CkMove move;
    u16 kings;  // bit i: the piece taken by jump i was a king
    u8 clock;   // the clock before the move
    u8 crowned; // 1 if the move crowned a man
} CkUndo;

// A position and the plies that led to it. Treat as opaque: read it with the
// ck_*() functions. It carries its history (an undo record and a key a ply)
// for GAME_PLY_MAX + SEARCH_PLY_MAX plies, about 13.9 KB: keep it in EWRAM
// on the GBA, never on the stack.
typedef struct CheckersPosition {
    u8 board[32];                       // a piece a square
    u8 side;                            // CK_DARK or CK_LIGHT, to move
    u8 clock;                           // plies since the last capture or man move (stops at 255)
    u16 ply;                            // plies made since the position was set
    ChessKey keys[POS_HISTORY_MAX + 1]; // keys[ply] is the current key
    CkUndo undo[POS_HISTORY_MAX];
} CheckersPosition;

// Square numbering (see above): -1 from ck_square_at() for a light square or
// one off the board.
int ck_square_file(int s);
int ck_square_rank(int s);
int ck_square_at(int file, int rank);

// The standard starting position: Dark's men on 1-12, Light's on 21-32,
// Dark to move.
void ck_start(CheckersPosition* p);
// Sets p from a PDN FEN string: the side to move ('B' Dark, 'W' Light), then
// each side's pieces after 'B' or 'W', as square numbers 1..32 or ranges
// ("1-12"), a 'K' before a king's; e.g. "B:W21,22,K30:B1,2,K3" or
// "W:W21-32:B1-12". A side may have no pieces ("W:W:B1"); a final '.' is
// allowed. Returns false, leaving p unchanged, if the text is malformed, a
// square is named twice, or a man stands on its own crowning row.
bool ck_from_text(CheckersPosition* p, const char* s);

u8 ck_piece(const CheckersPosition* p, int s); // CK_EMPTY or colour | kind
int ck_side(const CheckersPosition* p);        // CK_DARK or CK_LIGHT, to move
int ck_clock(const CheckersPosition* p);       // plies since a capture or man move
int ck_ply(const CheckersPosition* p);         // plies made since the position was set

// Every legal move into out (room for CK_MAX_MOVES); returns the count. When
// any jump exists these are only jumps: every complete path of every piece,
// none stopping while it could go on, and a man's ending where it is crowned.
int ck_gen_legal(CheckersPosition* p, CkMove* out);
// The jumps alone (none when there is no capture), for quiescence.
int ck_gen_jumps(CheckersPosition* p, CkMove* out);

// Plays m, which must be one ck_gen_legal() returned for this position.
// Does nothing once POS_HISTORY_MAX plies are made (the game ends before).
void ck_make(CheckersPosition* p, CkMove m);
// Takes back the last move made; nothing if none was.
void ck_unmake(CheckersPosition* p);

ChessKey ck_key(const CheckersPosition* p);
// The key plies_back plies ago (0 = now); 0 beyond the history.
ChessKey ck_key_at(const CheckersPosition* p, int plies_back);

// Reading a move.
int ck_move_steps(CkMove m);          // landings: 1 for a simple move, else the jumps
int ck_move_landing(CkMove m, int i); // the i-th landing square (0..steps-1)
int ck_move_to(CkMove m);             // the last landing
bool ck_move_is_jump(CkMove m);

// Whether the game is over, and how. Checked in this order: no legal move
// (NO_MOVES: the side to move has lost); 80 plies, 40 by each side, with no
// capture and no man moved (FORTY); the position, same side to move, seen
// three times since the last capture or man move (REPETITION); and ck_ply()
// at GAME_PLY_MAX (LENGTH). Callers stop at any result but NONE.
GameResult ck_result(CheckersPosition* p);

// The notation of m, a legal move of p, with the squares numbered 1..32:
// "11-15" for a move, "15x24" for a jump, "15x24x31" for a multi-jump. A
// multi-jump too long for out (more than seven jumps) is written by its ends
// only, "1x28", as PDN allows. Returns the length; out is NUL-terminated.
int ck_notation(const CheckersPosition* p, CkMove m, char out[24]);

// The legal move from `from` whose first count landings are these; with
// count smaller than its steps, only if it is the one legal move beginning
// that way. CK_MOVE_NONE if there is none (or several).
CkMove ck_find(CheckersPosition* p, int from, const int* landings, int count);
// The distinct next landing squares (at most 4) of the legal moves from
// `from` that begin with these count landings, into next; returns how many.
// With count 0 they are the piece's first landings.
int ck_continuations(CheckersPosition* p, int from, const int* landings, int count, int* next);

// The number of leaf nodes of the legal move tree depth plies deep, each
// complete move (path) counted once.
unsigned long long ck_perft(CheckersPosition* p, int depth);

#endif // BOARDGAMES_CHECKERS_H
