#ifndef DEN_EVENTS_H
#define DEN_EVENTS_H

// Serval Den's life events, without the screen (host-tested): which event
// the den shows next, the time away lived minute by minute with what it
// changed counted, and the words of the summary shown at boot. The scenes
// that show them are in scenes.c.
//
// Rulings:
//  - An event is shown once: the save keeps the stage last shown
//    (DenSave.stage_seen), so a pet that grew up while the game was off, or
//    while a page was open, gets its scene at the next chance and never
//    twice. Death and the litter need no such mark: they last until their
//    scene replaces the pet.
//  - Order: a dead pet's tombstone first (a pet that grew up and then died
//    is shown only its stone), then growing up, then the litter.
//  - A pet that grew two stages while away (kitten to adult) gets one scene,
//    from the stage last shown to the stage now.

#include "pet.h"

typedef enum { EVENT_NONE, EVENT_TOMBSTONE, EVENT_GROW_UP, EVENT_LITTER } LifeEvent;

// The event the den shows next for this pet, given the stage last shown.
LifeEvent events_next(const Pet* p, u8 stage_seen);

// What a stretch of time did to the pet.
typedef struct Away {
    u32 minutes;       // time lived (whole minutes stepped, and a dead pet's time)
    u32 slept_minutes; // of which the pet was asleep
    u8 stage_before, stage_after;
    bool died;   // dead after, alive before
    bool litter; // the litter came due
} Away;

// pet_advance(p, now, hour_of), a minute at a time, counting the minutes
// asleep: the pet that comes out is the one pet_advance() makes (the core's
// stepping is split-invariant). Once dead, the rest is one step. Costs a
// little more than pet_advance(): call it at boot.
void events_live(Pet* p, u32 now, int (*hour_of)(u32 seconds), Away* out);

#define EVENT_LINES 4
#define EVENT_LINE_MAX 31 // 30 columns and the NUL

// The summary of the time away, a line each (for 30 columns): the time
// away, then what happened ("SPOTS GREW UP!", "SPOTS SLEPT 8H", "SPOTS IS
// SICK", "A VISITOR IS AT THE DEN"), the pet as it is now. Returns the
// number of lines, 0 when nothing is worth a card (less than an hour away,
// no event).
int events_summary(const Pet* p, const Away* a, char lines[EVENT_LINES][EVENT_LINE_MAX]);

// A duration as the den writes it: "45M", "8H", "8H 30M", "2D 4H" (no
// minutes past a day; whole hours drop the minutes). Writes at most 12
// characters and the NUL; returns the length.
int events_duration(u32 minutes, char* out);

#endif // DEN_EVENTS_H
