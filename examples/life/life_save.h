#ifndef LIFE_SAVE_H
#define LIFE_SAVE_H

// Life's save, portable C (tested on the host): the settings and the world,
// in a packed little-endian byte layout so struct padding never changes the
// format. The world is kept as one bit per cell (alive or not): ages and
// trails are not saved.
//
//   0  rule (LIFE_RULE_*)      4  sound (0 off, 1 on)
//   1  theme (< LIFE_THEMES)   5  stamp (LIFE_PAT_*)
//   2  speed (< LIFE_SPEEDS)   6  stamp's quarter turns (0-3)
//   3  reserved, 0             7  reserved, 0
//   8  generation, 32 bits little endian
//   12 the cells, LIFE_CELLS bits: cell i is bit i % 8 of byte 12 + i / 8
//
// Unpacking refuses (returns false, changes nothing) any value this version
// could not have written.

#include "life_rules.h"

#define LIFE_SAVE_VERSION 1
#define LIFE_SAVE_BYTES (12 + LIFE_CELLS / 8)

#define LIFE_THEMES 4
#define LIFE_SPEEDS 7

typedef struct {
    u8 rule, theme, speed, sound, stamp, turns;
    u32 generation;
} LifeSettings;

void life_save_pack(u8* out, const LifeSettings* settings, const u32* grid);
bool life_save_unpack(const u8* in, LifeSettings* settings, u32* grid);

#endif // LIFE_SAVE_H
