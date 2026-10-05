// Save memory on the web, for src/core/save.c: a buffer standing in for the
// cartridge's save memory of the game's save type (serval_add_rom's SAVE;
// compiled once per type, CMakeLists.txt defines SERVAL_SAVE_TYPE), with the
// GBA's size and slot layout, so the game gets the same slots and the buffer
// is byte for byte an mGBA .sav file of that type. Flash erases are mimicked
// (sectors set to 0xFF), so the bytes match too. Kept by the page
// (shell.html) in the browser's localStorage: loaded from there on first use
// (before the game's first save call reads it), stored after every
// save_write() and save_erase().
//
// Games that never save don't link this file, and their pages never touch
// localStorage.

#include "../core/save_internal.h"

#include <emscripten.h>

#include <string.h>

#ifndef SERVAL_SAVE_TYPE
#error "compile src/web/save.c with SERVAL_SAVE_TYPE set (CMakeLists.txt)"
#endif

static u8 memory[SAVE_SIZE(SERVAL_SAVE_TYPE)];

// shell.html: copies the stored save memory to dst and returns 1, or returns 0
// if there is none (first run, no localStorage, or a stored copy larger than
// size). A shorter stored copy (mGBA's .sav of an 8 KiB EEPROM only grows to
// 8 KiB once the game writes past 512 bytes) is padded with 0xFF.
EM_JS(int, web_save_load, (u8 * dst, u32 size), { return Module.servalSaveLoad(dst, size); });

// shell.html: stores size bytes from src (in localStorage, if available).
EM_JS(void, web_save_store, (const u8* src, u32 size), { Module.servalSaveStore(src, size); });

static u8* web_memory(void) {
    static bool loaded;
    if (!loaded) {
        loaded = true;
        memset(memory, 0xFF, sizeof memory); // like never-written save memory
        web_save_load(memory, sizeof memory);
    }
    return memory;
}

static void web_save_read(u32 offset, u8* dst, u32 count) {
    memcpy(dst, web_memory() + offset, count);
}

static bool web_save_write(u32 offset, const u8* src, u32 count) {
    memcpy(web_memory() + offset, src, count);
    return true;
}

#if defined(SERVAL_SAVE_FLASH64K) || defined(SERVAL_SAVE_FLASH128K)
static bool web_save_erase(u32 offset, u32 count) {
    const u32 sector = 4096;
    u32 end = (offset + count + sector - 1) / sector * sector;
    memset(web_memory() + offset, 0xFF, end - offset);
    return true;
}
#else
#define web_save_erase NULL
#endif

static void web_save_flush(void) {
    web_save_store(web_memory(), sizeof memory);
}

const SaveDevice serval_platform_save_device = {.read = web_save_read,
                                                .write = web_save_write,
                                                .erase = web_save_erase,
                                                .flush = web_save_flush,
                                                SAVE_LAYOUT(SERVAL_SAVE_TYPE)};
