#ifndef SERVAL_CORE_SAVE_INTERNAL_H
#define SERVAL_CORE_SAVE_INTERNAL_H

// Engine-internal: the save memory behind save.h. Not part of the public API.
//
// src/core/save.c keeps the slots (format, checksums, copies) on a plain byte
// device; each platform supplies one: battery-backed SRAM on the GBA
// (src/gba/save_sram.c), a buffer kept in localStorage on the web
// (src/web/save.c), a RAM array on the host (src/host/platform.c). Tests swap
// in their own device (an array that can simulate power loss).

#include "serval/platform.h"

// The save memory's size: the GBA cartridge SRAM's 32 KiB.
#define SAVE_MEMORY_SIZE 0x8000u

// Each slot is two copies of SAVE_COPY_SIZE bytes: a SAVE_HEADER_SIZE-byte
// header, then the data. Slot s's copies are copy 2s and 2s + 1, copy c at
// offset c * SAVE_COPY_SIZE.
#define SAVE_COPY_SIZE 2048u
#define SAVE_HEADER_SIZE 16u

typedef struct {
    // Copies count bytes from save memory at offset to dst.
    void (*read)(u32 offset, u8* dst, u32 count);
    // Copies count bytes from src to save memory at offset.
    void (*write)(u32 offset, const u8* src, u32 count);
    // Called after save_write() and save_erase() finish changing the memory
    // (whether or not they succeeded). NULL if the memory needs nothing more
    // (the web stores its buffer in localStorage here).
    void (*flush)(void);
} SaveDevice;

// The platform's save memory, and the one save.c uses (initially the
// platform's; tests point it at their own).
extern const SaveDevice serval_platform_save_device;
extern const SaveDevice* serval_save_device;

// CRC-32 (IEEE 802.3, as in zip and PNG) of count bytes, continuing from crc
// (start with 0). Exposed for tests.
u32 serval_save_crc32(u32 crc, const u8* data, u32 count);

#endif // SERVAL_CORE_SAVE_INTERNAL_H
