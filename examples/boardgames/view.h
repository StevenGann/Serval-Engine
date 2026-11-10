#ifndef BOARDGAMES_VIEW_H
#define BOARDGAMES_VIEW_H

// The board as the player sees it, for either game: the table (art.c), the
// pieces as sprites, the squares' highlights, the move targets, the
// cursor, the moves' animations and the side panel.
//
// Squares here are always chess's 0..63 (a1 = 0, SQ(file, rank)), in
// both games: checkers code converts its squares with ck_square_file() and
// ck_square_rank(). The view shows them flipped (Black's side at the
// bottom) when view_set_flipped(true).

#include "match.h"

#include <serval/serval.h>

// Text styles the panel uses besides TEXT_NORMAL (cream) and
// TEXT_HIGHLIGHT (gold): older moves and labels, and the alerts.
#define STYLE_DIM 2
#define STYLE_ALERT 3

// Loads the art (after art_build()), shows the table and sets the text's
// colours. Call with the screen black, once.
void view_init(void);

void view_set_flipped(bool flipped);
bool view_flipped(void);

// The screen position of a square's top-left pixel, as the board is seen.
int view_square_x(int sq);
int view_square_y(int sq);

// Draws every piece of m's position (or, while an animation runs, of the
// animation's), the marks and the targets, and moves the animation and the
// highlights on. Call every frame, after view_draw_cursor().
void view_draw_match(const Match* m);

// The squares tinted: the last move's two, the king in check's (flashing
// red; unused in checkers) and the selected piece's. -1 for none. Kept
// until changed.
void view_set_highlights(int last_from, int last_to, int alert_sq, int selected_sq);

// The selected piece's destinations: a dot on each, or a ring where
// captures[i] (a chess capture, or a checkers jump's landing). count 0
// clears them. Kept until changed; at most 64.
void view_set_targets(const int* squares, const bool* captures, int count);

// Checkers: the pieces that can capture (when a capture is forced), each
// ringed in orange. count 0 clears them. Kept until changed.
void view_set_marks(const int* squares, int count);

// The cursor on a square this frame. Call before view_draw_match() so that
// it is drawn in front of the pieces.
void view_draw_cursor(int sq);

// Animates move (a Move in chess, a CkMove in checkers) played from the
// position `before`: chess slides the piece (castling's rook after the
// king), a captured piece goes in a puff and a promotion sparkles;
// checkers hops landing by landing, each piece it jumps going in a puff,
// and a crowning sparkles. Until view_animating() is false the view shows
// the animation, not the match it is given, so the caller may play the
// move at once.
void view_animate(const Match* before, u32 move);
bool view_animating(void);

// The side panel: the game, the opponent ("VS SERVAL") or "2 PLAYERS",
// whose turn it is and the move number, the pieces each side has taken, the
// last six moves in the game's notation (oldest first, each beside an icon
// of the side that played it) and `status` (CHECK, THINKING, a result: up
// to two lines of ten characters, split at a space; red when it starts
// with "CHECK", else gold). The text is redrawn only when something in it
// changes; the icons are sprites: call every frame. Takes a Match that it
// can write because the notation of a past move is found by taking moves
// back and playing them again (match_notation()); m is as it was after.
void view_panel(Match* m, const char* status);

#endif // BOARDGAMES_VIEW_H
