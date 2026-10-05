#ifndef SERVAL_GBA_INTERNAL_H
#define SERVAL_GBA_INTERNAL_H

// Engine-internal state shared between GBA modules. Not part of the public API.

#include <tonc_oam.h>
#include <tonc_types.h>

// The shadow OAM: rebuilt from draw calls every frame and copied to OAM in
// VBlank by frame_end(). serval_oam_used counts this frame's entries.
extern OBJ_ATTR serval_shadow_oam[128];
extern u32 serval_oam_used;

#endif // SERVAL_GBA_INTERNAL_H
