#ifndef SERVAL_PLATFORM_H
#define SERVAL_PLATFORM_H

// Integer types, memory placement macros and the planned-API marker, shared by
// every engine module.
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

// Planned API: names this version declares, so that the API is complete, but
// does not implement yet (docs/releases.md#planned-api). Every use of a planned
// function or constant compiles with a warning at the use, e.g.
//
//     warning: 'music_play' is deprecated: Serval: planned, not implemented in
//     this version: tracker music, docs/audio.md [-Wdeprecated-declarations]
//
// and does nothing harmful at run time: a planned function returns 0, false or
// its type's "none" and changes nothing, a loader refuses data that needs a
// planned feature, and debug builds warn once ("serval: ..."). To write code
// against planned API anyway (it starts working in the engine version that
// implements it), define SERVAL_NO_PLANNED_WARNINGS before including any Serval
// header (-DSERVAL_NO_PLANNED_WARNINGS), or silence one place with
// #pragma GCC diagnostic ignored "-Wdeprecated-declarations" (GCC and Clang).
#if defined(SERVAL_NO_PLANNED_WARNINGS) || !(defined(__GNUC__) || defined(__clang__))
#define SERVAL_PLANNED(what)
#else
#define SERVAL_PLANNED(what)                                                                       \
    __attribute__((deprecated("Serval: planned, not implemented in this version: " what)))
#endif

#endif // SERVAL_PLATFORM_H
