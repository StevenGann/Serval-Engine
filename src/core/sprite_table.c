// The registered sprite table (sprite_table_set() is in the platform's sprite
// code). Portable, so sys_animate can read sprite assets on every target.

#include "sprite_internal.h"

#include <stddef.h>
#include <stdint.h>

const SpriteAsset* const* serval_sprite_table;
u16 serval_sprite_count;

// The ROM data formats (docs/sprites.md#rom-data-format) are frozen: a new
// field may only go into existing padding, keeping every size and offset
// (SpriteGroup.slots took the padding byte at offset 11). Exact sizes where
// pointers are 32-bit (the GBA, and wasm32 for the web); the offsets, in
// terms of the pointer size, on every target.
#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(SpritePiece) == 10, "SpritePiece's size changed");
_Static_assert(sizeof(SpriteAsset) == 20, "SpriteAsset's size changed");
_Static_assert(sizeof(SpriteGroup) == 12, "SpriteGroup's size changed");
#endif
_Static_assert(offsetof(SpritePiece, frame) == 8, "SpritePiece's layout changed");
_Static_assert(offsetof(SpriteAsset, flags) == sizeof(void*) + 7, "SpriteAsset's layout changed");
_Static_assert(offsetof(SpriteGroup, flags) == 2 * sizeof(void*) + 2,
               "SpriteGroup's layout changed");
_Static_assert(offsetof(SpriteGroup, slots) == 2 * sizeof(void*) + 3,
               "SpriteGroup's layout changed");
