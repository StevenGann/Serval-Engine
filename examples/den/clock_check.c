// Serval Den's clock checks, without the hardware (host-tested): see clock.h.

#include "clock.h"

ClockCheck clock_resolve(bool present, bool was_reset, u32 raw, u32 last_seen, u32 age, u32 offset,
                         u32* now, u32* new_offset) {
    if (!present) {
        *now = *new_offset = 0;
        return CLOCK_MISSING;
    }
    u32 t = raw + offset;
    // A reading before 2000-01-01 plus the pet's age: the pet couldn't have
    // been born yet, so the clock started over.
    if (was_reset || t < age) {
        *new_offset = offset + (last_seen - t);
        *now = last_seen;
        return CLOCK_WAS_RESET;
    }
    *now = t;
    *new_offset = offset;
    if (t < last_seen)
        return CLOCK_WENT_BACK;
    if (t - last_seen > CLOCK_JUMP_LIMIT)
        return CLOCK_JUMPED;
    return CLOCK_OK;
}
