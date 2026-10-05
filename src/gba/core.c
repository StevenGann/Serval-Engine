#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/random.h"
#include "serval/screen.h"
#include "serval/sprites.h"

#include <tonc.h>

#include "../core/random_internal.h"
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
u32 serval_matrices_used;

// CPU cycles per frame: 228 scanlines of 1232 cycles.
#define FRAME_BUDGET_CYCLES 280896u

void (*serval_map_prepare_hook)(void);
void (*serval_map_commit_hook)(void);

static u32 frame_start_cycles;
static u32 last_frame_cycles;
static u32 frames; // frame_end() calls since serval_init()

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
#ifdef SERVAL_WEB
    return serval_web_cycles();
#else
    // Re-read if the low half wrapped between reading the two halves.
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
#endif
}

void serval_init(void) {
    // Faster cartridge access than the power-on default (4/2 wait states, no
    // prefetch): ROM at 3/1 wait states with the prefetch buffer on, save RAM
    // at 8. This is the standard setting commercial games use and every
    // cartridge and flash cart supports. Code and data in ROM, including the
    // game's own, run noticeably faster.
    REG_WAITCNT = WS_STANDARD;

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
    serval_entropy_reset();
    frames = 0;
    serval_psg_init();

    cycle_counter_start();
    frame_start_cycles = cycles_now();
    last_frame_cycles = 0;
}

void frame_begin(void) {
    frame_start_cycles = cycles_now();
    key_poll();
    serval_entropy_frame(frames, key_curr_state());
    serval_oam_used = 0;
    serval_matrices_used = 0;
}

void frame_end(void) {
    for (u32 i = serval_oam_used; i < 128; i++)
        serval_shadow_oam[i].attr0 = ATTR0_HIDE;
    if (serval_map_prepare_hook)
        serval_map_prepare_hook();

    last_frame_cycles = cycles_now() - frame_start_cycles;
    VBlankIntrWait();
    oam_copy(oam_mem, serval_shadow_oam, 128);
    if (serval_map_commit_hook)
        serval_map_commit_hook();
    serval_psg_update();
    frames++;
}

u32 frame_count(void) {
    return frames;
}

u32 frame_cpu_cycles(void) {
    return last_frame_cycles;
}

u32 frame_cpu_permille(void) {
    u32 cycles = last_frame_cycles;
    if (cycles <= 0xFFFFFFFFu / 1000) // one (software) division for normal frames
        return cycles * 1000 / FRAME_BUDGET_CYCLES;
    // Over ~15 budgets, cycles * 1000 would overflow 32 bits: split it.
    return cycles / FRAME_BUDGET_CYCLES * 1000 +
           cycles % FRAME_BUDGET_CYCLES * 1000 / FRAME_BUDGET_CYCLES;
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
