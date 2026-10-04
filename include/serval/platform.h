#ifndef SERVAL_PLATFORM_H
#define SERVAL_PLATFORM_H

// Integer types and memory placement macros, shared by every engine module.
//
// SERVAL_GBA is defined when building for the Game Boy Advance. Without it
// (host builds for unit tests), the placement macros expand to nothing, so
// platform-neutral modules compile natively.

#include <stdbool.h>
#include <stdint.h>

#ifdef SERVAL_GBA
#include <tonc_types.h> // u8..u32, s8..s32, FIXED: identical to the host definitions below
#else
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef s32 FIXED;
#endif

#ifdef SERVAL_GBA
// Hot code: placed in IWRAM and compiled as ARM code (32-bit bus, no wait states).
#define SERVAL_IWRAM_CODE                                                                          \
    __attribute__((section(".iwram.text"), long_call, target("arm"), noinline))
// Data placed in IWRAM. Ordinary globals already live there; use this to be explicit.
#define SERVAL_IWRAM_DATA __attribute__((section(".iwram.data")))
// Initialized data placed in the larger, slower EWRAM.
#define SERVAL_EWRAM_DATA __attribute__((section(".ewram.data")))
#else
#define SERVAL_IWRAM_CODE
#define SERVAL_IWRAM_DATA
#define SERVAL_EWRAM_DATA
#endif

#endif // SERVAL_PLATFORM_H
