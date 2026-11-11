#ifndef DEN_MENU_H
#define DEN_MENU_H

// The care menu: an icon bar along the foot of the den (FEED, PLAY, PET,
// CLEAN, MEDICINE, LIGHTS, STATUS, ALBUM). Left and right choose, A acts, B
// backs out of a list or page. FEED asks meal or treat; PLAY lists the games
// the pet's stage unlocks (greyed while it can't play); STATUS and ALBUM are
// full-screen pages (the album one serval a page, its portrait in its coat,
// LEFT and RIGHT turning the pages). A refusal shows its reason on the line above the bar.
// START puts the lights out or on, as a shortcut.
//
// Input: a button held when a screen opens does nothing until it is released
// (a press after that is a new one), so the A that chose an icon is not also
// the A that picks the first line of the list it opens.

#include "den.h"

// The mini-games, in the order of PLAY's list.
enum { GAME_YARN, GAME_CLIMB, GAME_POUNCE, GAME_COUNT };

// A game. Called with the den faded out and its text cleared; it sets up
// what it needs, plays, calls pet_played(), leaves the screen black and
// returns whether the pet changed (to be saved). The menu brings the den back.
typedef bool (*MenuGameFn)(Pet* pet, u32 now);

// Plugs a game into PLAY's list (NULL: COMING SOON).
void menu_set_game(int game, MenuGameFn fn);

// Call once after den_enter() and the fade-in: the bar starts on FEED.
void menu_init(void);

// One frame of input and state. True if a care action succeeded or a game
// changed the pet (the caller saves). Page and game transitions fade and
// block for a few frames inside this call (frame_end() and frame_begin()
// are called by them: call it between the frame's own).
bool menu_update(Pet* p, u32 now);

// Draws the bar, a page's sprites. Call before den_draw() (the first sprite
// drawn is in front).
void menu_draw(const Pet* p, u32 now);

// Back to the bar after a scene (scenes.c) took the screen: no list or page
// open, the label redrawn, the buttons held ignored until released. The
// chosen icon is kept.
void menu_resume(void);

// True while a full-screen page hides the den: skip den_draw().
bool menu_covers_den(void);

// Fades the screen to black and back from black (a few frames), drawing the
// den and the bar meanwhile if `draw`.
void menu_fade_out(const Pet* p, bool draw);
void menu_fade_in(const Pet* p, bool draw);

#endif // DEN_MENU_H
