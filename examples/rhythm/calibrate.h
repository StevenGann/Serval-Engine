#ifndef RHYTHM_CALIBRATE_H
#define RHYTHM_CALIBRATE_H

// The calibration screen: steady clicks to tap along to, the player's
// offset measured from the taps (their median error), adjusted by hand and
// saved with the records (docs/audio.md#position: what the engine can't
// know of a screen's and a speaker's delays).

#include <serval/serval.h>

typedef enum {
    CALIBRATE_RUNNING,
    CALIBRATE_SAVED,     // A: the offset saved (records_save_calibration())
    CALIBRATE_CANCELLED, // B: left as it was
} CalibrateResult;

// Starts the clicks and the measuring, from the offset saved.
void calibrate_show(void);

// A frame of it; `input` false during the fades.
CalibrateResult calibrate_frame(bool input);

#endif // RHYTHM_CALIBRATE_H
