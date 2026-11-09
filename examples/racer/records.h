#ifndef RACER_RECORDS_H
#define RACER_RECORDS_H

// The saved records: the best race time and the best lap, kept in one save
// slot (save.h), read at boot and written only when one falls.

#include "track.h"

// Reads the records from the save (none if there is no save, or one this
// version of the game can't read). Call once at boot.
void records_load(void);

// The records as they stand.
const Records* records_get(void);

// Takes a race that is over into the records (records_update()) and saves
// them if one fell. Returns the RECORD_* bits of what fell.
u32 records_take(const Race* r);

// Clears the records and saves them cleared.
void records_erase(void);

// Whether the last write failed (the records then hold only until power-off).
bool records_save_failed(void);

#endif // RACER_RECORDS_H
