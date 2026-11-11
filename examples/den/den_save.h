#ifndef DEN_SAVE_H
#define DEN_SAVE_H

// The den's save, in slot 0: the pet, the best scores, the album and the
// game time it was last seen at, with the game clock's offset. It is written field by field, little
// endian, into a packed byte layout (DEN_SAVE_BYTES), so struct padding and
// the compiler never change the format; every field, the pet's dropping_clock
// and treat_clock included, is in it. Loading checks every value's range and
// refuses what this version could not have written.

#include "album.h"

#define DEN_SAVE_VERSION 1
#define DEN_SAVE_BYTES 451

typedef struct DenSave {
    Pet pet;
    u16 best[3];
    u8 album_count;
    AlbumEntry album[ALBUM_MAX];
    u32 last_seen; // game time (clock.h)
    u32 offset;    // the game clock's offset from the cartridge clock (clock.h)
} DenSave;

// False: none, damaged, another version or size, or values out of range (then
// start a new den; `out` is left alone).
bool den_save_load(DenSave* out);
// Writes the save; false if the memory refused.
bool den_save_store(const DenSave* in);

#endif // DEN_SAVE_H
