// The planned API of audio.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.

// Calls planned API on purpose: without this, every call would warn.
#define SERVAL_NO_PLANNED_WARNINGS

#include "serval/audio.h"
#include "test.h"

TEST_SUITE(planned_audio_tests, "planned_audio");
