#ifndef SERVAL_GBA_INTERNAL_H
#define SERVAL_GBA_INTERNAL_H

// Engine-internal state shared between GBA modules. Not part of the public API.

#include <tonc_oam.h>

#include "serval/audio.h"
#include <tonc_types.h>

// The shadow OAM: rebuilt from draw calls every frame and copied to OAM in
// VBlank by frame_end(). serval_oam_used counts this frame's entries.
extern OBJ_ATTR serval_shadow_oam[128];
extern u32 serval_oam_used;
// Rotation matrices used this frame. The 32 matrices live in the shadow OAM's
// otherwise unused fourth halfwords (OBJ_AFFINE overlay), copied with it.
extern u32 serval_matrices_used;

// PSG sound effects (psg.c): set up by serval_init(), advanced once per frame
// by frame_end().
void serval_psg_init(void);
void serval_psg_update(void);
// Text layer (text.c), for the splash screen: whether the game's text layer
// is set up, switching it off again, and printing through any BG palette bank.
bool serval_text_active(void);
void serval_text_deactivate(void);
void serval_text_print_bank(int col, int row, const char* s, u32 palbank);

// Plays a sound that isn't in the game's sound table, and silences one channel.
void serval_psg_play_sound(const PsgSound* sound);
void serval_psg_silence(u32 channel);

// The frequency register value last written for a PSG channel (for tests:
// the hardware's square frequency bits are write-only).
u16 serval_psg_rate(u32 channel);

#endif // SERVAL_GBA_INTERNAL_H
