// Moves a square sprite with the D-pad. Hold A to move faster.

#include "serval/gba.h"
#include "serval/serval.h"

#include <tonc.h>

int main(void) {
    serval_init();

    // One 8x8 4bpp tile filled with palette index 1.
    for (unsigned i = 0; i < 8; i++)
        tile_mem[4][0].data[i] = 0x11111111;
    pal_obj_mem[1] = RGB15(31, 25, 0);
    pal_bg_mem[0] = RGB15(2, 4, 10);

    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D;

    int x = 116, y = 76;
    for (;;) {
        frame_begin();

        int speed = key_down(KEY_A) ? 3 : 1;
        if (key_down(KEY_LEFT))
            x -= speed;
        if (key_down(KEY_RIGHT))
            x += speed;
        if (key_down(KEY_UP))
            y -= speed;
        if (key_down(KEY_DOWN))
            y += speed;
        x = clamp(x, 0, SCREEN_WIDTH - 8 + 1);
        y = clamp(y, 0, SCREEN_HEIGHT - 8 + 1);

        gba_oam_submit((u16)(ATTR0_SQUARE | ATTR0_4BPP | (y & ATTR0_Y_MASK)),
                       (u16)(ATTR1_SIZE_8 | (x & ATTR1_X_MASK)), ATTR2_PALBANK(0) | 0);

        frame_end();
    }
}
