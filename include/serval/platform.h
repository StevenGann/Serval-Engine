#ifndef SERVAL_PLATFORM_H
#define SERVAL_PLATFORM_H

// Integer types and memory placement macros, shared by every engine module.
//
// SERVAL_GBA is defined when building for the Game Boy Advance. Without it
// (host builds for unit tests), the placement macros expand to nothing, so
// platform-neutral modules compile natively.
//
// Public engine headers never include third-party headers. The typedefs below
// are identical to libtonc's, so games may still include <tonc.h> alongside
// (tests/rom/compat_*.c check this). The C headers here are the compiler's
// own freestanding ones (no C library): bool, the fixed-width integers, and
// NULL, size_t and offsetof.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef s32 FIXED;

#ifdef SERVAL_GBA
// Hot code: placed in IWRAM and compiled as ARM code (32-bit bus, no wait states).
#define SERVAL_IWRAM_CODE                                                                          \
    __attribute__((section(".iwram.text"), long_call, target("arm"), noinline))
// Data placed in IWRAM. Ordinary globals already live there; use this to be explicit.
#define SERVAL_IWRAM_DATA __attribute__((section(".iwram.data")))
// Initialized data placed in the larger, slower EWRAM.
#define SERVAL_EWRAM_DATA __attribute__((section(".ewram.data")))
// Zero-initialized data placed in EWRAM: large buffers that would crowd IWRAM.
#define SERVAL_EWRAM_BSS __attribute__((section(".sbss")))
#else
#define SERVAL_IWRAM_CODE
#define SERVAL_IWRAM_DATA
#define SERVAL_EWRAM_DATA
#define SERVAL_EWRAM_BSS
#endif

#endif // SERVAL_PLATFORM_H
