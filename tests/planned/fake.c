// The tests' own planned names (fake.h), used as every planned name is used in
// these files: each on its own line ending "// planned", which
// tools/check-planned.py checks warns as planned (at -O0 and -O2, with this
// build's compiler), and silently with -DSERVAL_NO_PLANNED_WARNINGS. They
// exercise the check in every version, whatever the public headers declare
// planned: also in one with none. Also used in dead code and in an
// initializer and a case label (a planned constant works wherever an int
// constant does), to keep those promises (docs/releases.md#planned-api). This
// file is in no build target: it warns on purpose. See
// docs/development.md#planned-api.

#include "fake.h"

int planned_fake(int value);
int planned_fake(int value) {
    static const int constant = PLANNED_FAKE_CONSTANT;                        // planned
    int result = planned_fake_function(value) + constant + PLANNED_FAKE_DONE; // planned
    if (0)
        result += planned_fake_function(0); // planned
    switch (value) {
    case PLANNED_FAKE_CONSTANT: // planned
        return result;
    default:
        return 0;
    }
}
