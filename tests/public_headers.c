// Compile-only check, built by the host preset, where libtonc is not
// available: the public headers must not need any third-party header.

#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/platform.h"
#include "serval/screen.h"
#include "serval/serval.h"
#include "serval/sprites.h"

void public_headers_check(void);
void public_headers_check(void) {}
