#ifndef RHYTHM_PLAY_H
#define RHYTHM_PLAY_H

// One song played: the count-in, the notes falling in time with the music,
// the presses judged, the pads, the judgement word, the combo, the score,
// the serval's dance and the beat's pulse, the pause menu, the end.

#include "judge.h"

#include <serval/serval.h>

typedef enum {
    PLAY_RUNNING,  // the song goes on (or is paused)
    PLAY_FINISHED, // every note judged and the music at its end: play_result() holds the score
    PLAY_QUIT,     // QUIT from the pause menu: the music stopped
} PlayResult;

// Starts song `song` (SONG_*) at `difficulty` (DIFF_*): shows the play
// screen and plays the module from row 0. `autoplay` presses every note
// itself, in the first frame that reaches it, judged as a button press in
// that frame would be (a score that is never a record; no calibration).
// `calibration_ms`: the player's offset, how much later than the music the
// presses come (docs/audio.md#position). RETRY on the pause menu starts
// the same again.
void play_begin(u8 song, u8 difficulty, bool autoplay, s16 calibration_ms);

// One frame of it, between frame_begin() and frame_end().
PlayResult play_frame(void);

// The score of the song played (final after PLAY_FINISHED).
const Score* play_result(void);

// The last frame's picture again, nothing moving: for a fade to black after
// PLAY_FINISHED or PLAY_QUIT.
void play_draw(void);

#endif // RHYTHM_PLAY_H
