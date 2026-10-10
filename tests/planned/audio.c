// Every planned name in audio.h (SERVAL_PLANNED), used once, each on its own line
// ending "// planned". tools/check-planned.py (CTest planned_api; part of the
// build in web builds) compiles this file and fails unless each marked line
// warns as planned, no other line warns, and every planned name in the public
// headers is used in one of these files. When a name is implemented and its
// SERVAL_PLANNED goes, its line goes too. This file is in no build target: it
// warns on purpose. See docs/development.md#planned-api.
//
// audio.h has no planned names in this version (the wave channel, the sound
// bank, tracker music and sampled effects are implemented); a planned name
// added later gets its line here.

#include "serval/audio.h"

void planned_audio(void);
void planned_audio(void) {}
