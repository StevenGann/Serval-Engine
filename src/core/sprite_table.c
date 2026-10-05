// The registered sprite table (sprite_table_set() is in the platform's sprite
// code). Portable, so sys_animate can read sprite assets on every target.

#include "sprite_internal.h"

const SpriteAsset* const* serval_sprite_table;
u16 serval_sprite_count;
