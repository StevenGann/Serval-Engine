#ifndef SERVAL_TESTS_PLANNED_FAKE_H
#define SERVAL_TESTS_PLANNED_FAKE_H

// Planned API of the tests' own, which no engine version implements: a
// function and a constant marked SERVAL_PLANNED as a public header would mark
// them. tools/check-planned.py reads planned names from the headers here as
// from include/serval/, and refuses to run without a planned function and a
// planned constant among them, so CTest planned_api checks the marker itself
// (SERVAL_PLANNED in platform.h: the warning at every use, its text, and
// SERVAL_NO_PLANNED_WARNINGS) in every version, whatever the public headers
// hold: also in one with no planned name. fake.c uses each name, and the
// script assembler's tests refuse the constant (tools/svm_test.py). Nothing
// else includes this file. See docs/development.md#planned-api.

#include "serval/platform.h"

// A planned function: what a stub would look like (returns 0, does nothing).
SERVAL_PLANNED("fake planned API (the tests' own), docs/development.md#planned-api")
int planned_fake_function(int value);

enum {
    PLANNED_FAKE_DONE = 1, // an ordinary constant beside it, which warns nowhere
    PLANNED_FAKE_CONSTANT SERVAL_PLANNED(
        "fake planned API (the tests' own), docs/development.md#planned-api") = 2,
};

#endif // SERVAL_TESTS_PLANNED_FAKE_H
