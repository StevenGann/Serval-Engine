// Compile-only check: games may include libtonc alongside Serval's headers, in
// either order, without conflicting names or types. See compat_*.c.

#include <tonc.h>

#include "serval/gba.h"
#include "serval/serval.h"

void compat_tonc_first(void);
void compat_tonc_first(void) {
    u32 value = 0; // same typedef from both
    (void)value;
}
