// Compile-only check, built by the host preset, where libtonc is not
// available: the public headers, all included together, must not need any
// third-party header. Each header is also compiled on its own
// (serval_add_header_check in tests/CMakeLists.txt).

#include "serval/gba.h"
#include "serval/serval.h"

void public_headers_check(void);
void public_headers_check(void) {}
