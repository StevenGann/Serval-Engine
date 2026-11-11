#ifndef DEN_SCENES_H
#define DEN_SCENES_H

// Serval Den's life events on screen (what to show comes from events.h):
// the cards at boot (a reset clock, the 30-day question, the summary of the
// time away), growing up, the visitor and the litter, the tombstone and the
// new kitten.
//
// The scenes in the den are called from the den's frame, between its
// frame_begin() and frame_end(), with the den on the screen; they run their
// own frames and return in a begun frame with the screen black and the den
// loaded behind it (the serval placed, the care bar ready). Each changes the
// save as its event says and returns true when it did: the caller stores it,
// then fades in (menu_fade_in()), so that what the scene did is written
// before the den is seen again.

#include "den_save.h"
#include "events.h"

// A card of text on the dark title screen (the den not loaded): `count`
// lines centred, then A to go on. Clears the text after.
void scenes_card(const char* const* lines, int count);

// THE CLOCK JUMPED AHEAD BY N DAYS. COUNT IT? on the dark screen: true for
// YES (NO is chosen first). Clears the text after.
bool scenes_ask_jump(u32 days);

// The summary of the time away (events_summary()) as a card; nothing if
// there is nothing to say.
void scenes_summary(const Pet* p, const Away* a);

// More than an hour of game time to catch up while the game was on (a web
// page's clock moved, a game ran long): lived at once behind a black screen,
// as at boot, then the summary card. Closes a page if one is open.
bool scenes_catch_up_in_play(DenSave* s, u32 now);

// The clock jumped more than 30 days ahead while the game was on (a web
// page's clock can): the same question as at boot, on a dark page. YES lives
// the time (events_live()) and shows the summary; NO rebases the pet.
bool scenes_jump_in_play(DenSave* s, u32 now);

// Growing up: the serval grows in a burst of sparkles to a fanfare, then
// GROWN UP! and the games its new stage opens. Marks the stage shown.
bool scenes_grow_up(DenSave* s);

// The litter: a visiting serval walks in, the two court (purring, hearts),
// then the kittens (2-4, litter_make()) in a row, each in its coat and with
// its name; LEFT and RIGHT choose, A takes one; the parents (and the other
// kittens) leave for the savanna and go into the album (END_LITTER); the
// chosen kitten is named (its litter name to start) and is the new pet, a
// generation on. The visitor and the litter are drawn from the pet's birth
// time, so a power cut before the choice is stored replays the same ones.
bool scenes_litter(DenSave* s);

// The tombstone: a stone with flowers, the name, generation and days lived,
// a soft tune; A: the album keeps the pet (END_TOMBSTONE), A NEW KITTEN
// ARRIVES, naming, and the new kitten (a generation on, a random coat).
bool scenes_tombstone(DenSave* s);

#endif // DEN_SCENES_H
