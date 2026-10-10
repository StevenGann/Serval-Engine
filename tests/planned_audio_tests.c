// The planned API of audio.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// audio.h has no planned names in this version: the wave channel
// (psg_wave_tests.c, tests/rom/wave_tests.c), the sound bank, tracker music
// and sampled effects (tests/rom/sampled_audio_tests.c) are implemented. A
// planned function added later gets its cases here.

#include "serval/audio.h"
#include "test.h"

TEST_SUITE(planned_audio_tests, "planned_audio");
