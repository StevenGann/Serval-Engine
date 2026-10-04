#ifndef SERVAL_GBA_H
#define SERVAL_GBA_H

// GBA-specific escape hatches for C code that needs the hardware directly.
// These are not portable to other targets; games built in the editor use the
// portable core API instead (docs/core-api.md).

#include "serval/platform.h"

// Appends a raw OAM entry (attributes 0-2) to this frame's shadow OAM. Returns
// false if all 128 entries are used. Entries are flushed by frame_end().
bool gba_oam_submit(u16 attr0, u16 attr1, u16 attr2);

#endif // SERVAL_GBA_H
