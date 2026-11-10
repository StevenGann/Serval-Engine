#ifndef BOARDGAMES_CHESS_BOARD_H
#define BOARDGAMES_CHESS_BOARD_H

// The chess board: pieces, moves, FEN, attacks, legal move generation,
// make and unmake, Zobrist keys and perft. Portable C only (no hardware), so
// tests/chess_tests.c runs it on the host.
//
// Squares are 0..63, a1 = 0, b1 = 1, ..., h8 = 63 (SQ(file, rank)). Inside,
// the board is 0x88; nothing here divides. A Position carries its own
// history (undo records and keys) for GAME_PLY_MAX + SEARCH_PLY_MAX plies,
// so it is about 12 KB: keep it in EWRAM on the GBA, never on the stack.

#include <serval/core.h>

// The search's hottest functions (move generation, make and unmake, the
// evaluation) are SERVAL_IWRAM_CODE, ARM code in IWRAM on the GBA, in
// optimized builds; unoptimized (Debug) ARM code would not fit IWRAM, so
// there they stay in ROM.
#ifdef __OPTIMIZE__
#define CHESS_HOT SERVAL_IWRAM_CODE
#else
#define CHESS_HOT
#endif

typedef u8 Piece; // EMPTY or colour | kind
enum { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { WHITE = 0, BLACK = 8 }; // the colour bit
#define PIECE_KIND(p) ((p) & 7)
#define PIECE_COLOR(p) ((p) & 8)

#define SQ(file, rank) ((rank) * 8 + (file))
#define SQ_FILE(sq) ((sq) & 7)
#define SQ_RANK(sq) ((sq) >> 3)

// A move: from (bits 0-5) | to (6-11) | flags (12-15). MOVE_NONE is 0
// (a1 to a1, never a move).
typedef u16 Move;
#define MOVE_NONE 0
#define MOVE_FROM(m) ((m) & 63)
#define MOVE_TO(m) (((m) >> 6) & 63)
#define MOVE_FLAGS(m) ((m) >> 12)
#define MOVE_MAKE(from, to, flags) ((Move)((from) | ((to) << 6) | ((flags) << 12)))

// Flags. A promotion is MF_PROMO | (0 N, 1 B, 2 R, 3 Q), with MF_CAPTURE
// added (12-15) when it takes a piece.
enum {
    MF_QUIET = 0,
    MF_DOUBLE = 1, // a pawn's double step
    MF_CASTLE_K = 2,
    MF_CASTLE_Q = 3,
    MF_CAPTURE = 4,
    MF_EP = 5, // en passant (also a capture)
    MF_PROMO = 8,
};
// The piece kind a promotion makes (KNIGHT..QUEEN).
#define MOVE_PROMO_KIND(m) (KNIGHT + (MOVE_FLAGS(m) & 3))

#define MAX_MOVES 256
#define GAME_PLY_MAX 800 // the game's length cap
#define SEARCH_PLY_MAX 64
#define POS_HISTORY_MAX (GAME_PLY_MAX + SEARCH_PLY_MAX)

// Castling rights (pos_castling()).
enum { CASTLE_WK = 1, CASTLE_WQ = 2, CASTLE_BK = 4, CASTLE_BQ = 8 };

typedef unsigned long long ChessKey; // Zobrist key

// What pos_unmake() needs to take a move back: 6 bytes a ply.
typedef struct {
    Move move;
    u16 halfmove;
    u8 captured; // the piece taken, EMPTY if none
    u8 state;    // castling rights (bits 0-3) | en passant file + 1 (bits 4-7, 0 if none)
} PosUndo;

// A position and the plies that led to it. Treat as opaque: read it with the
// pos_*() functions.
typedef struct Position {
    u8 board[128]; // 0x88: square = rank * 16 + file
    u8 side;       // WHITE or BLACK, to move
    u8 castling;   // CASTLE_* bits
    s8 ep;         // en passant target (0..63) or -1
    u8 king[2];    // each side's king, 0x88 ([0] White, [1] Black)
    u16 halfmove;  // plies since the last capture or pawn move
    u16 fullmove;  // FEN's move number
    u16 ply;       // plies made since the position was set
    // Kept by pos_make() and pos_unmake() for the evaluation (chess_eval.c),
    // so that it need not scan the board: the pawns (pawns[side * 2] files
    // a-d, pawns[side * 2 + 1] files e-h, a byte a file from the low end and
    // a bit a rank), each side's material and piece-square values (its king
    // left out) and the phase its pieces make.
    u32 pawns[4];
    s16 psq[2];
    u8 phase;
    // Whether the side to move is in check: 0 not known yet, 1 no, 2 yes.
    u8 check_known;
    // Each side's pieces, a bit a square (0..63): [side][0] a1-h4, [1] a5-h8.
    // Move generation finds the pieces from these.
    u32 occ[2][2];
    ChessKey keys[POS_HISTORY_MAX + 1]; // keys[ply] is the current key
    PosUndo undo[POS_HISTORY_MAX];
} Position;

// The evaluation's tables (chess_eval.c), which a Position keeps summed: each
// kind's value, its piece-square table (White's view as printed: a white
// piece on sq reads entry sq ^ 56, a black one entry sq; NULL for the king)
// and its share of the game phase.
extern const s16 chess_piece_value[7];
extern const s8* const chess_pst[7];
extern const u8 chess_phase_weight[7];

// The standard starting position.
void pos_start(Position* p);
// Sets p from a FEN string (the halfmove and fullmove fields may be left
// out). Returns false, leaving p unchanged, if the FEN is malformed or the
// position impossible: not one king a side, a pawn on the first or eighth
// rank, the side not to move in check, an en passant square with no pawn that
// just made a double step. Castling rights whose king or rook is not at home
// are dropped. Like pos_make(), it keeps an en passant square only when a pawn
// stands beside the pawn that could take it, so keys of equal positions match.
bool pos_from_fen(Position* p, const char* fen);

Piece pos_piece(const Position* p, int sq);
int pos_side(const Position* p);      // WHITE or BLACK, to move
int pos_ep_square(const Position* p); // the en passant target, -1 if none
u8 pos_castling(const Position* p);   // CASTLE_* bits: 1 K, 2 Q, 4 k, 8 q
int pos_halfmove(const Position* p);  // the fifty-move rule's clock
int pos_ply(const Position* p);       // plies made since the position was set

// Every legal move into out (room for MAX_MOVES); returns the count.
int pos_gen_legal(Position* p, Move* out);
// The legal captures (en passant included) and promotions, for quiescence.
int pos_gen_captures(Position* p, Move* out);

// Plays m, which must be one pos_gen_legal() returned for this position. Does
// nothing once POS_HISTORY_MAX plies are made (the game ends long before).
void pos_make(Position* p, Move m);
// Takes back the last move made; nothing if none was.
void pos_unmake(Position* p);

bool pos_in_check(const Position* p); // the side to move's king attacked
bool pos_attacked(const Position* p, int sq, int by_color);

ChessKey pos_key(const Position* p);
// The key plies_back plies ago (0 = now); 0 beyond the history.
ChessKey pos_key_at(const Position* p, int plies_back);

// The number of leaf nodes of the legal move tree depth plies deep.
unsigned long long chess_perft(Position* p, int depth);

#endif // BOARDGAMES_CHESS_BOARD_H
