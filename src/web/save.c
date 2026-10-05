// Save memory on the web, for src/core/save.c: a 32 KiB buffer standing in for
// the cartridge's SRAM, kept by the page (shell.html) in the browser's
// localStorage. Loaded from there on first use (before the game's first save
// call reads it), stored after every save_write() and save_erase().
//
// Games that never save don't link this file, and their pages never touch
// localStorage.

#include "../core/save_internal.h"

#include <emscripten.h>

#include <string.h>

static u8 memory[SAVE_MEMORY_SIZE];

// shell.html: copies the stored save memory to dst and returns 1, or returns 0
// if there is none (first run, no localStorage, or a stored copy of another
// size).
EM_JS(int, web_save_load, (u8 * dst, u32 size), { return Module.servalSaveLoad(dst, size); });

// shell.html: stores size bytes from src (in localStorage, if available).
EM_JS(void, web_save_store, (const u8* src, u32 size), { Module.servalSaveStore(src, size); });

static u8* web_memory(void) {
    static bool loaded;
    if (!loaded) {
        loaded = true;
        if (!web_save_load(memory, sizeof memory))
            memset(memory, 0xFF, sizeof memory); // like never-written SRAM
    }
    return memory;
}

static void web_save_read(u32 offset, u8* dst, u32 count) {
    memcpy(dst, web_memory() + offset, count);
}

static void web_save_write(u32 offset, const u8* src, u32 count) {
    memcpy(web_memory() + offset, src, count);
}

static void web_save_flush(void) {
    web_save_store(web_memory(), sizeof memory);
}

const SaveDevice serval_platform_save_device = {
    .read = web_save_read, .write = web_save_write, .flush = web_save_flush};
