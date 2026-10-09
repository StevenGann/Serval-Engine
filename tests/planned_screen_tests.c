// The planned API of screen.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// screen.h has no planned names in this version: alpha blending
// (tests/rom/blend_tests.c) and raster effects (tests/rom/raster_tests.c) are
// implemented. A planned function added later gets its cases here.

#include "serval/screen.h"
#include "test.h"

TEST_SUITE(planned_screen_tests, "planned_screen");
