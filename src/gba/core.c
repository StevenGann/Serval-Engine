#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/screen.h"
#include "serval/sprites.h"

#include <tonc.h>

// The portable button bits are the GBA's KEYINPUT bits, so no translation is needed.
_Static_assert(BUTTON_A == KEY_A && BUTTON_B == KEY_B && BUTTON_SELECT == KEY_SELECT &&
                   BUTTON_START == KEY_START && BUTTON_RIGHT == KEY_RIGHT &&
                   BUTTON_LEFT == KEY_LEFT && BUTTON_UP == KEY_UP && BUTTON_DOWN == KEY_DOWN &&
                   BUTTON_R == KEY_R && BUTTON_L == KEY_L && BUTTON_ANY == KEY_ANY,
               "button bits must match KEYINPUT");

// Rebuilt from draw calls every frame and copied to OAM in VBlank, so sprites
// that are not drawn disappear. See "Sprite submission model" in docs/core-api.md.
static OBJ_ATTR shadow_oam[128] ALIGN4;
static u32 oam_used;

void serval_init(void) {
    irq_init(NULL);
    irq_enable(II_VBLANK);

    oam_init(oam_mem, 128); // hide whatever OAM held at power-on
    oam_init(shadow_oam, 128);
    oam_used = 0;

    // Mode 0 (tiled backgrounds), sprites on, sprite tiles mapped linearly.
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D;

    sprite_groups_reset();
    ecs_reset();
}

void frame_begin(void) {
    key_poll();
    oam_used = 0;
}

void frame_end(void) {
    for (u32 i = oam_used; i < 128; i++)
        shadow_oam[i].attr0 = ATTR0_HIDE;

    VBlankIntrWait();
    oam_copy(oam_mem, shadow_oam, 128);
}

bool button_down(u16 buttons) {
    return key_is_down(buttons) != 0;
}

bool button_pressed(u16 buttons) {
    return key_hit(buttons) != 0;
}

void screen_set_backdrop(Color color) {
    pal_bg_mem[0] = color;
}

bool gba_oam_submit(u16 attr0, u16 attr1, u16 attr2) {
    if (oam_used >= 128)
        return false;
    obj_set_attr(&shadow_oam[oam_used++], attr0, attr1, attr2);
    return true;
}
