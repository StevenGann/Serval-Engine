#ifndef BOARDGAMES_SOUND_H
#define BOARDGAMES_SOUND_H

// The board games' sounds, all on the tone generators (PSG), all written for
// this example: a wooden knock for a piece set down, a harder one for a
// capture, chimes for a promotion or a crowning and for check, a jingle at
// the end of a game, and a soft "no".

// Registers the sounds. Call once at boot.
void sound_init(void);

void sound_pick(void);    // a piece picked up (also a menu's cursor moved)
void sound_move(void);    // a piece set down
void sound_capture(void); // a piece taken
void sound_check(void);
void sound_castle(void);  // king and rook set down: a double knock
void sound_promote(void); // a promotion, or a man crowned
void sound_win(void);     // the human won (or, in two-player games, the game was won)
void sound_lose(void);
void sound_draw(void);
void sound_no(void); // a piece that cannot move, or a square it cannot go to

#endif // BOARDGAMES_SOUND_H
