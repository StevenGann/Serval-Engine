// The records, saved: one small struct in one save slot, as asteroids keeps
// its table.
//
// Records (track.h) is two u32s of frames, 0 meaning none yet: fixed-size
// fields only, no pointers, so it means the same after a power-off. It is
// read once at boot and written only on the results screen, and only when
// records_update() says a record fell: a write takes a millisecond or two,
// and the save memory is not worn for nothing.

#include "records.h"

// Versioning: RECORDS_VERSION is a number this game picks for the layout of
// Records. Raise it whenever the struct changes, and save_read() reports a
// save in the old layout as SAVE_OTHER_VERSION instead of reading it as
// garbage (a game that wanted to keep old saves would then read the slot
// with its old struct and convert). This is version 1, so there is nothing
// older to convert: anything else starts with no records.
#define RECORDS_SLOT 0
#define RECORDS_VERSION 1

static Records records;
static bool save_failed; // the last save_write() didn't succeed

void records_load(void) {
    switch (save_read(RECORDS_SLOT, &records, sizeof records, RECORDS_VERSION)) {
    case SAVE_OK:
        break;
    case SAVE_EMPTY:         // first boot, or the slot was erased
    case SAVE_CORRUPT:       // damaged in storage (bad checksum): nothing to rescue
    case SAVE_OTHER_VERSION: // another layout or size: see RECORDS_VERSION above
    default:
        // No records yet. The slot isn't written now: the first record
        // saves over it.
        records = (Records){0, 0};
        break;
    }
}

const Records* records_get(void) {
    return &records;
}

static void save(void) {
    save_failed = !save_write(RECORDS_SLOT, &records, sizeof records, RECORDS_VERSION);
}

u32 records_take(const Race* r) {
    u32 fell = records_update(&records, r);
    if (fell)
        save();
    return fell;
}

void records_erase(void) {
    records = (Records){0, 0};
    save();
}

bool records_save_failed(void) {
    return save_failed;
}
