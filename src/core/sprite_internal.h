#ifndef SERVAL_CORE_SPRITE_INTERNAL_H
#define SERVAL_CORE_SPRITE_INTERNAL_H

// Engine-internal: the sprite table registered with sprite_table_set(), shared
// by the platform's sprite code (src/gba/sprites.c) and the portable animation
// system (src/ecs/animate.c). Not part of the public API.

#include "serval/sprites.h"

// table[id] is the sprite with that ID; count is at most SPRITE_MAX.
extern const SpriteAsset* const* serval_sprite_table;
extern u16 serval_sprite_count;

#endif // SERVAL_CORE_SPRITE_INTERNAL_H
