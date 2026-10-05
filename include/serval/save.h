#ifndef SERVAL_SAVE_H
#define SERVAL_SAVE_H

// Save data: a few numbered slots, each holding one block of game data (a
// struct, typically) that survives power-off. See docs/runtime-systems.md.
//
// GBA: the cartridge's battery-backed SRAM (32 KiB). Linking this module puts
// the "SRAM_V" ID string in the ROM, which tells emulators and flash carts to
// give the game save RAM. Web builds: the browser's localStorage, one entry per
// game (named after its title and game code).
//
// Every slot is checked (a checksum over its data) and versioned, so a save
// written by an older version of the game is recognized instead of being read
// as garbage, and a save damaged in storage reads as corrupt rather than
// wrong. Writes keep the slot's previous contents until the new data is
// complete, so a power loss during save_write() leaves the old save readable
// (or the slot empty, if it held none).

#include "serval/platform.h"

// Number of slots, and the most data one slot holds, in bytes.
#define SAVE_SLOTS 8
#define SAVE_SLOT_MAX 2000

// save_read() results.
#define SAVE_OK 0
#define SAVE_EMPTY 1         // nothing saved in the slot (or it was erased)
#define SAVE_CORRUPT 2       // a damaged save (checksum mismatch): treat it as empty
#define SAVE_OTHER_VERSION 3 // saved with another version or size: see below

// Writes size bytes (1 to SAVE_SLOT_MAX) to a slot (0 to SAVE_SLOTS - 1),
// tagged with the game's save format version (any number the game picks,
// raised when the saved struct changes). Returns false (warning in debug
// builds) for a bad slot, size or data pointer, or if the save didn't verify
// (read back differently: no save RAM); the slot then keeps its previous
// save. On the GBA it takes about 1.3 ms for 100 bytes and 22 ms (over a
// frame) for a full slot: call it at a natural pause (game over, a menu), not
// every frame.
bool save_write(u32 slot, const void* data, u32 size, u16 version);

// Reads a slot into data. SAVE_OK only if the slot holds an intact save of
// exactly this version and size; otherwise data is left untouched and the
// result says why. For SAVE_OTHER_VERSION, save_slot_version() and
// save_slot_size() tell what is there, so the game can read it with its old
// struct and version and convert it. A bad slot or data pointer reads as
// SAVE_EMPTY (warning in debug builds). About 0.7 ms for 100 bytes on the
// GBA, 11 ms for a full slot.
int save_read(u32 slot, void* data, u32 size, u16 version);

// The version and size of the intact save in a slot, or 0 if it holds none
// (empty or corrupt). A save's size is never 0, so save_slot_size(slot) != 0
// tells whether the slot holds a save.
u16 save_slot_version(u32 slot);
u32 save_slot_size(u32 slot);

// Empties a slot (both of its copies; all or nothing on power loss).
void save_erase(u32 slot);

#endif // SERVAL_SAVE_H
