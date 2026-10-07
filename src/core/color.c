// Color math on screen.h's Color. Portable: tested natively and in the test
// ROM (tests/color_tests.c).

#include "serval/screen.h"

#include "warn.h"

#ifdef SERVAL_DEBUG
static bool warned_amount;
#endif

Color color_mix(Color a, Color b, u32 amount) {
    if (amount > 256) {
#ifdef SERVAL_DEBUG
        if (!warned_amount) {
            warned_amount = true;
            SERVAL_WARN(
                "color_mix: amount %u is past 256 (all of the second color); clamped to 256",
                amount);
        }
#endif
        amount = 256;
    }
    // Each channel: (a * (256 - amount) + b * amount) >> 8. The weights sum
    // to 256, so a weighted sum is at most 31 * 256 (under 2^13), a channel
    // stays within 0-31, and the endpoints are exact. Shifting rounds down, as the
    // hardware's blending does (screen_set_blend). Red and blue are mixed
    // together, 16 bits apart in one word (their sums can't reach each other),
    // so a color takes four multiplies instead of six.
    u32 keep = 256 - amount;
    u32 rb_a = (a & 0x001Fu) | ((u32)(a & 0x7C00u) << 6);
    u32 rb_b = (b & 0x001Fu) | ((u32)(b & 0x7C00u) << 6);
    u32 rb = ((rb_a * keep + rb_b * amount) >> 8) & 0x001F001Fu; // red in bits 0-4, blue 16-20
    u32 g = ((((u32)a >> 5) & 31u) * keep + (((u32)b >> 5) & 31u) * amount) >> 8;
    return (Color)((rb & 0x1Fu) | (rb >> 6) | (g << 5));
}
