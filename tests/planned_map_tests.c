// The planned API of map.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// map.h has no planned names in this version: LZ77-compressed tilesets
// (lz77_tests.c, tests/rom/lz77_tests.c), ladders and floor slopes
// (map_slope_tests.c) are implemented. A planned function added later gets
// its cases here.

#include "serval/map.h"
#include "test.h"

TEST_SUITE(planned_map_tests, "planned_map");
