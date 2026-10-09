// Alpha blending (docs/runtime-systems.md#alpha-blending): screen_set_blend()
// keeps its settings, and frame_end() applies them in VBlank (step 7 of the
// flush, docs/frame-loop.md). Sprites drawn with SPRITE_BLEND get the
// hardware's semi-transparent mode in sprites.c; the hardware blends them
// over the second targets set here, whatever the first targets and the mode.
//
// The hardware has one color special effect, which the brightness
// (screen.c) and the splash screen (splash.c) use too. Blending owns
// BLDALPHA, which neither of them reads, so it is written at every apply;
// BLDCNT is the brightness's while the brightness is not 0, and the
// splash's while it runs. The brightness puts the applied blend control back
// when it returns to 0, and so does the splash when it ends: blending pauses
// and resumes, with the settings last applied.

#include "serval/screen.h"

#include <tonc.h>

#include "../core/warn.h"
#include "screen_internal.h"

// The settings screen_set_blend() last gave, as BLDCNT and BLDALPHA values,
// until frame_end() applies them (`pending`); and the BLDCNT value applied
// at the last frame_end() that had any (0, off, until then).
static u16 pending_control, pending_alpha, applied_control;
static bool pending;
static bool borrowed; // the splash screen holds BLDCNT

#ifdef SERVAL_DEBUG
static bool warned_layers, warned_weights;
#endif

void screen_set_blend(u32 top, u32 bottom, u32 top_weight, u32 bottom_weight) {
    if ((top | bottom) & ~(u32)LAYER_ALL) {
#ifdef SERVAL_DEBUG
        if (!warned_layers) {
            warned_layers = true;
            SERVAL_WARN("screen_set_blend(0x%x, 0x%x, ...): only the LAYER_* bits (0x%x) are "
                        "layers; the others are ignored",
                        top, bottom, LAYER_ALL);
        }
#endif
        top &= LAYER_ALL;
        bottom &= LAYER_ALL;
    }
    if (top_weight > 16 || bottom_weight > 16) {
#ifdef SERVAL_DEBUG
        if (!warned_weights) {
            warned_weights = true;
            SERVAL_WARN("screen_set_blend(..., %u, %u): weights go from 0 to 16; clamped to 16",
                        top_weight, bottom_weight);
        }
#endif
        top_weight = top_weight > 16 ? 16 : top_weight;
        bottom_weight = bottom_weight > 16 ? 16 : bottom_weight;
    }
    // The first targets, the alpha blending mode and the second targets. With
    // no second target nothing can blend, so the effect is off (also for
    // SPRITE_BLEND sprites, which need a second target behind them).
    pending_control = bottom ? (u16)(top | BLD_STD | bottom << BLD_BOT_SHIFT) : BLD_OFF;
    pending_alpha = (u16)BLDA_BUILD(top_weight, bottom_weight);
    pending = true;
}

void serval_blend_apply(void) {
    if (!pending)
        return;
    pending = false;
    applied_control = pending_control;
    REG_BLDALPHA = pending_alpha;
    if (serval_screen_brightness() == 0 && !borrowed)
        REG_BLDCNT = applied_control;
}

u16 serval_blend_control(void) {
    return applied_control;
}

void serval_blend_borrow(bool on) {
    borrowed = on;
}
