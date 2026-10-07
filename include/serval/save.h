#ifndef SERVAL_SAVE_H
#define SERVAL_SAVE_H

// Save data: a few numbered slots, each holding one block of game data (a
// struct, typically) that survives power-off. See docs/runtime-systems.md.
//
// GBA: the cartridge's save memory, of the type the game picks with
// serval_add_rom(... SAVE <type>): SRAM (32 KiB, the default), FLASH64K,
// FLASH128K, EEPROM8K or EEPROM512. Linking this module puts the type's ID
// string in the ROM ("SRAM_V113", "FLASH512_V131", "FLASH1M_V103",
// "EEPROM_V124"), which tells emulators and flash carts which save memory to
// give the game. Web builds: the browser's localStorage, one entry per game
// (named after its title and game code), with the same slots as on the GBA.
//
// The save type sets how many slots there are and how much each holds:
//   SRAM, FLASH64K, FLASH128K   8 slots of up to 2000 bytes
//   EEPROM8K                    8 slots of up to 496 bytes
//   EEPROM512                   2 slots of up to 112 bytes
// save_slot_count() and save_slot_capacity() return the game's.
//
// Every slot is checked (a checksum over its data) and versioned, so a save
// written by an older version of the game is recognized instead of being read
// as garbage, and a save damaged in storage reads as corrupt rather than
// wrong. Writes keep the slot's previous contents until the new data is
// complete, so a power loss during save_write() leaves the old save readable
// (or the slot empty, if it held none).

#include "serval/platform.h"

// The most slots, and the most data one slot holds in bytes, of any save
// type (SRAM's and Flash's). Fine for sizing arrays; for limits, use the
// game's own: save_slot_count() and save_slot_capacity().
#define SAVE_SLOTS 8
#define SAVE_SLOT_MAX 2000

// The game's number of slots (SAVE_SLOTS, or 2 with EEPROM512) and the most
// data one of them holds (SAVE_SLOT_MAX; 496 bytes with EEPROM8K, 112 with
// EEPROM512).
u32 save_slot_count(void);
u32 save_slot_capacity(void);

// save_read() results.
#define SAVE_OK 0
#define SAVE_EMPTY 1         // nothing saved in the slot (or it was erased)
#define SAVE_CORRUPT 2       // a damaged save (checksum mismatch): treat it as empty
#define SAVE_OTHER_VERSION 3 // saved with another version or size: see below

// Writes size bytes (1 to save_slot_capacity()) to a slot (0 to
// save_slot_count() - 1), tagged with the game's save format version (any
// number the game picks, raised when the saved struct changes). Returns false
// (warning in debug builds) for a bad slot, size or data pointer, or if the
// save didn't verify (read back differently, or the memory reported a failure:
// no save memory of this type, an unsupported Flash chip, a timeout); the
// slot then keeps its previous save. On the GBA with SRAM it takes about
// 1.4 ms for 100 bytes and 22 ms (over a frame) for a full slot; Flash and
// EEPROM are slower (docs/runtime-systems.md#save-data): call it at a natural
// pause (game over, a menu), not every frame.
bool save_write(u32 slot, const void* data, u32 size, u16 version);

// Reads a slot into data. SAVE_OK only if the slot holds an intact save of
// exactly this version and size; otherwise data is left untouched and the
// result says why. For SAVE_OTHER_VERSION, save_slot_version() and
// save_slot_size() tell what is there, so the game can read it with its old
// struct and version and convert it. A bad slot or data pointer reads as
// SAVE_EMPTY (warning in debug builds). About 0.7 ms for 100 bytes on the
// GBA with SRAM, 11 ms for a full slot.
int save_read(u32 slot, void* data, u32 size, u16 version);

// The version and size of the intact save in a slot, or 0 if it holds none
// (empty or corrupt) or for a bad slot (warning in debug builds). A save's
// size is never 0, so save_slot_size(slot) != 0 tells whether the slot holds
// a save.
u16 save_slot_version(u32 slot);
u32 save_slot_size(u32 slot);

// Empties a slot (both of its copies; all or nothing on power loss). A bad
// slot is ignored (warning in debug builds).
void save_erase(u32 slot);

#endif // SERVAL_SAVE_H
