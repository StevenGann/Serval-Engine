// The bankroll, kept in save data (save.h) so the chips carry over from one
// session to the next: one small struct in one slot, written after every
// round (a natural pause, and about a millisecond with SRAM).

#include "game.h"

// BANK_VERSION is this game's number for the layout of Bankroll: raise it
// when the struct changes, and save_read() reports an old save as
// SAVE_OTHER_VERSION instead of reading garbage (asteroids/scores.c shows
// converting one).
#define BANK_SLOT 0
#define BANK_VERSION 1

Bankroll bankroll;
static bool save_failed;

static const Bankroll fresh = {.bank = BANK_START, .best = BANK_START, .bet = 50};

void bank_load(void) {
    if (save_read(BANK_SLOT, &bankroll, sizeof bankroll, BANK_VERSION) != SAVE_OK)
        bankroll = fresh; // first boot, erased, damaged, or another layout
}

void bank_save(void) {
    if (bankroll.bank > bankroll.best)
        bankroll.best = bankroll.bank;
    save_failed = !save_write(BANK_SLOT, &bankroll, sizeof bankroll, BANK_VERSION);
}

void bank_reset(void) {
    save_erase(BANK_SLOT);
    bankroll = fresh;
    save_failed = false;
}

bool bank_save_failed(void) {
    return save_failed;
}
