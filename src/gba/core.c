#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/random.h"
#include "serval/screen.h"
#include "serval/sprites.h"

#include <tonc.h>

#include "internal.h"

// The portable button bits are the GBA's KEYINPUT bits, so no translation is needed.
_Static_assert(BUTTON_A == KEY_A && BUTTON_B == KEY_B && BUTTON_SELECT == KEY_SELECT &&
                   BUTTON_START == KEY_START && BUTTON_RIGHT == KEY_RIGHT &&
                   BUTTON_LEFT == KEY_LEFT && BUTTON_UP == KEY_UP && BUTTON_DOWN == KEY_DOWN &&
                   BUTTON_R == KEY_R && BUTTON_L == KEY_L && BUTTON_ANY == KEY_ANY,
               "button bits must match KEYINPUT");

// Rebuilt from draw calls every frame and copied to OAM in VBlank, so sprites
// that are not drawn disappear. See "Sprite submission model" in docs/core-api.md.
OBJ_ATTR serval_shadow_oam[128] ALIGN4;
u32 serval_oam_used;

// CPU cycles per frame: 228 scanlines of 1232 cycles.
#define FRAME_BUDGET_CYCLES 280896u

static u32 frame_start_cycles;
static u32 last_frame_cycles;

// Timers 2 and 3 cascade into a free-running 32-bit CPU cycle counter.
static void cycle_counter_start(void) {
    REG_TM2CNT = 0;
    REG_TM3CNT = 0;
    REG_TM2D = 0;
    REG_TM3D = 0;
    REG_TM3CNT = TM_CASCADE | TM_ENABLE;
    REG_TM2CNT = TM_FREQ_1 | TM_ENABLE;
}

static u32 cycles_now(void) {
    // Re-read if the low half wrapped between reading the two halves.
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

void serval_init(void) {
    irq_init(NULL);
    irq_enable(II_VBLANK);

    oam_init(oam_mem, 128); // hide whatever OAM held at power-on
    oam_init(serval_shadow_oam, 128);
    serval_oam_used = 0;

    // Mode 0 (tiled backgrounds), sprites on, sprite tiles mapped linearly.
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D;

    sprite_groups_reset();
    ecs_reset();
    random_seed(0);

    cycle_counter_start();
    frame_start_cycles = cycles_now();
    last_frame_cycles = 0;
}

void frame_begin(void) {
    frame_start_cycles = cycles_now();
    key_poll();
    serval_oam_used = 0;
}

void frame_end(void) {
    for (u32 i = serval_oam_used; i < 128; i++)
        serval_shadow_oam[i].attr0 = ATTR0_HIDE;

    last_frame_cycles = cycles_now() - frame_start_cycles;
    VBlankIntrWait();
    oam_copy(oam_mem, serval_shadow_oam, 128);
}

u32 frame_cpu_cycles(void) {
    return last_frame_cycles;
}

u32 frame_budget_cycles(void) {
    return FRAME_BUDGET_CYCLES;
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
    if (serval_oam_used >= 128)
        return false;
    obj_set_attr(&serval_shadow_oam[serval_oam_used++], attr0, attr1, attr2);
    return true;
}
