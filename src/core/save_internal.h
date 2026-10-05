#ifndef SERVAL_CORE_SAVE_INTERNAL_H
#define SERVAL_CORE_SAVE_INTERNAL_H

// Engine-internal: the save memory behind save.h. Not part of the public API.
//
// src/core/save.c keeps the slots (format, checksums, copies) on a save
// memory; each platform supplies one per save type, chosen per game by
// serval_add_rom(... SAVE <type>) (docs/runtime-systems.md#save-data):
// battery-backed SRAM (src/gba/save_sram.c), Flash (src/gba/save_flash.c) or
// EEPROM (src/gba/save_eeprom.c) on the GBA, a buffer of the same size and
// layout kept in localStorage on the web (src/web/save.c), and SRAM-sized RAM
// on the host (src/host/platform.c). Tests swap in their own memory (one that
// can simulate power loss, Flash erase rules and EEPROM blocks).

#include "serval/platform.h"

// Each slot is two copies, A and B: slot s's copies are copy 2s and 2s + 1,
// copy c at offset c * copy_size. A copy is a SAVE_HEADER_SIZE-byte header,
// then the data.
#define SAVE_HEADER_SIZE 16u

// The save types' memories and slot layouts. Every copy fits its memory, owns
// whole erase sectors (Flash) and starts on a block (EEPROM).
//   SRAM:      32 KiB, 8 slots of 2 x 2048 bytes
//   FLASH64K:  64 KiB, 8 slots of 2 x 4096 bytes (one 4 KiB sector each)
//   FLASH128K: 128 KiB in two 64 KiB banks, 8 slots of 2 x 8192 bytes (two
//              sectors each; only the first is used): slots 4-7 in bank 1
//   EEPROM8K:  8 KiB, 8 slots of 2 x 512 bytes (64 blocks of 8 bytes each)
//   EEPROM512: 512 bytes, 2 slots of 2 x 128 bytes (16 blocks each)
// Slot capacity is the copy minus its header, at most SAVE_SLOT_MAX (2000
// bytes, so SRAM and Flash games hold the same).
#define SAVE_SIZE_SRAM 0x8000u
#define SAVE_SIZE_FLASH64K 0x10000u
#define SAVE_SIZE_FLASH128K 0x20000u
#define SAVE_SIZE_EEPROM8K 0x2000u
#define SAVE_SIZE_EEPROM512 0x200u
#define SAVE_LAYOUT_SRAM                                                                           \
    .name = "SRAM (32 KiB)", .size = SAVE_SIZE_SRAM, .copy_size = 2048u, .slots = 8u,              \
    .block_size = 1u, .erase_size = 0u
#define SAVE_LAYOUT_FLASH64K                                                                       \
    .name = "Flash (64 KiB)", .size = SAVE_SIZE_FLASH64K, .copy_size = 4096u, .slots = 8u,         \
    .block_size = 1u, .erase_size = 4096u
#define SAVE_LAYOUT_FLASH128K                                                                      \
    .name = "Flash (128 KiB)", .size = SAVE_SIZE_FLASH128K, .copy_size = 8192u, .slots = 8u,       \
    .block_size = 1u, .erase_size = 4096u
#define SAVE_LAYOUT_EEPROM8K                                                                       \
    .name = "EEPROM (8 KiB)", .size = SAVE_SIZE_EEPROM8K, .copy_size = 512u, .slots = 8u,          \
    .block_size = 8u, .erase_size = 0u
#define SAVE_LAYOUT_EEPROM512                                                                      \
    .name = "EEPROM (512 bytes)", .size = SAVE_SIZE_EEPROM512, .copy_size = 128u, .slots = 2u,     \
    .block_size = 8u, .erase_size = 0u
// SAVE_LAYOUT(SERVAL_SAVE_TYPE) and SAVE_SIZE(SERVAL_SAVE_TYPE), for backends
// compiled once per save type (CMakeLists.txt defines SERVAL_SAVE_TYPE, e.g.
// to FLASH64K, and SERVAL_SAVE_<type>).
#define SAVE_LAYOUT(type) SAVE_LAYOUT_(type)
#define SAVE_LAYOUT_(type) SAVE_LAYOUT_##type
#define SAVE_SIZE(type) SAVE_SIZE_(type)
#define SAVE_SIZE_(type) SAVE_SIZE_##type

typedef struct {
    // Copies count bytes from save memory at offset to dst.
    void (*read)(u32 offset, u8* dst, u32 count);
    // Stores count bytes from src at offset. With block_size > 1, offset and
    // count are multiples of it. With erase_size, it programs: bytes must be
    // erased first, except that writing zeros is always allowed (programming
    // only clears bits). False if the memory reported a failure (a timeout):
    // save.c then stops.
    bool (*write)(u32 offset, const u8* src, u32 count);
    // Memories with erase_size only (NULL otherwise): sets the bytes at
    // offset (a multiple of erase_size) to 0xFF, count rounded up to whole
    // sectors. False on failure.
    bool (*erase)(u32 offset, u32 count);
    // Called after save_write() and save_erase() finish changing the memory
    // (whether or not they succeeded). NULL if the memory needs nothing more
    // (the web stores its buffer in localStorage here).
    void (*flush)(void);
    // The ROM's save type ID string ("SRAM_V113"...), which emulators and
    // flash carts look for; NULL off the GBA. Referencing it keeps it linked.
    const char* id;
    const char* name; // for warnings, e.g. "EEPROM (512 bytes)"
    u32 size;         // bytes of save memory
    u16 copy_size;    // bytes per copy
    u16 erase_size;   // Flash: sector size; 0 if bytes are rewritable
    u8 slots;         // at most SAVE_SLOTS
    u8 block_size;    // EEPROM: bytes per write (8); 1 if bytes are writable
} SaveDevice;

// The platform's save memory, and the one save.c uses (initially the
// platform's; tests point it at their own).
extern const SaveDevice serval_platform_save_device;
extern const SaveDevice* serval_save_device;

// CRC-32 (IEEE 802.3, as in zip and PNG) of count bytes, continuing from crc
// (start with 0). Exposed for tests.
u32 serval_save_crc32(u32 crc, const u8* data, u32 count);

#endif // SERVAL_CORE_SAVE_INTERNAL_H
