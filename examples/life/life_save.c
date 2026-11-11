// Life's save (life_save.h): packing and checked unpacking.

#include "life_save.h"

void life_save_pack(u8* out, const LifeSettings* settings, const u32* grid) {
    out[0] = settings->rule;
    out[1] = settings->theme;
    out[2] = settings->speed;
    out[3] = 0;
    out[4] = settings->sound;
    out[5] = settings->stamp;
    out[6] = settings->turns;
    out[7] = 0;
    for (int i = 0; i < 4; i++)
        out[8 + i] = (u8)(settings->generation >> (8 * i));
    const u8* cells = (const u8*)grid;
    for (int i = 0; i < LIFE_CELLS / 8; i++) {
        u32 bits = 0;
        for (int b = 0; b < 8; b++)
            bits |= (u32)life_alive(cells[i * 8 + b]) << b;
        out[12 + i] = (u8)bits;
    }
}

bool life_save_unpack(const u8* in, LifeSettings* settings, u32* grid) {
    if (in[0] >= LIFE_RULE_COUNT || in[1] >= LIFE_THEMES || in[2] >= LIFE_SPEEDS || in[3] ||
        in[4] > 1 || in[5] >= LIFE_PAT_COUNT || in[6] > 3 || in[7])
        return false;
    settings->rule = in[0];
    settings->theme = in[1];
    settings->speed = in[2];
    settings->sound = in[4];
    settings->stamp = in[5];
    settings->turns = in[6];
    settings->generation = 0;
    for (int i = 0; i < 4; i++)
        settings->generation |= (u32)in[8 + i] << (8 * i);
    u8* cells = (u8*)grid;
    for (int i = 0; i < LIFE_CELLS; i++)
        cells[i] = (in[12 + i / 8] >> (i % 8) & 1) ? LIFE_OLDEST : LIFE_EMPTY;
    return true;
}
