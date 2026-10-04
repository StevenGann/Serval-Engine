#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"

#include <tonc.h>

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

bool key_down(u16 key) {
    return key_is_down(key) != 0;
}

bool key_pressed(u16 key) {
    return key_hit(key) != 0;
}

bool gba_oam_submit(u16 attr0, u16 attr1, u16 attr2) {
    if (oam_used >= 128)
        return false;
    obj_set_attr(&shadow_oam[oam_used++], attr0, attr1, attr2);
    return true;
}
