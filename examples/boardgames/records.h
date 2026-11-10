#ifndef BOARDGAMES_RECORDS_H
#define BOARDGAMES_RECORDS_H

// The saved records of Paws & Kings: each game's wins, draws and losses
// against the computer at each strength, the two-player games finished, and
// the game in progress (one, of either kind), all in one save slot (save.h).
// The game in progress is its settings and its moves, one byte a move: the
// move's index in the game's legal-move list at that ply (generation order is
// deterministic), so restoring replays them and refuses an index past a
// list's end. Portable C. The functions carry a board_ prefix: the host test
// binary links every example's records.c, and rhythm's has records_*().

#include "match.h"

typedef struct GameRecord {
    u16 wins[LEVEL_COUNT], draws[LEVEL_COUNT], losses[LEVEL_COUNT]; // against the computer
    u16 two_player_games;                                           // finished
} GameRecord;

// Reads the save (no records and no game if there is none, or one of another
// version or size). Call once at boot.
void board_records_load(void);

// A game's record as it stands.
const GameRecord* board_records_get(GameKind game);

// Whether a game in progress is saved, and of which kind.
bool board_records_has_game(void);
GameKind board_records_saved_kind(void);

// The board's flip, saved with the game in progress: set whenever the view
// flips (kept in memory; written with the next save of the game), and read
// back for a game restored.
bool board_records_saved_flip(void);
void board_records_set_flip(bool flipped);

// Replays the saved game into m; false, with the saved game forgotten, if it
// cannot be (an index past a list's end, a move after the end).
bool board_records_restore_game(Match* m);

// Saves m as the game in progress (a game not begun, or over, forgets it).
void board_records_save_game(const Match* m);

// A finished game: counts it in its record (a win, draw or loss for the
// human at m's level; a two-player game in its own count), forgets the game
// in progress and saves. A game still on is only forgotten.
void board_records_end_game(const Match* m);

// Forgets the game in progress.
void board_records_forget_game(void);

// Clears everything and saves it cleared.
void board_records_erase(void);

// Whether the last write failed (the save then holds only until power-off).
bool board_records_save_failed(void);

#endif // BOARDGAMES_RECORDS_H
