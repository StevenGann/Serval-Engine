#ifndef RACER_SOUND_H
#define RACER_SOUND_H

// The sound, all on the tone generators: the engine's drone on the wave
// channel, the music on the squares and the noise channel, and the effects
// over it. game.c calls these from its events and its changes of screen.

#include "track.h"

// The effects, played by sound_play().
enum {
    SND_COUNT,      // a number of the countdown: 3, 2, 1
    SND_GO,         // the countdown's GO
    SND_CHECKPOINT, // a checkpoint passed: time added
    SND_LAP,        // a lap completed
    SND_BUMP,       // ran into traffic from behind
    SND_CRASH,      // hit scenery: the crunch (noise)
    SND_CRASH_TONE, // ... and the falling tone with it
    SND_PAUSE,      // pausing and going on
    SND_RECORD,     // the results came up with a record fallen
    SND_ERASED,     // the records were erased on the title
    SND_EFFECTS,
};

// The tunes, played by sound_tune(): the race's loops, the others play once.
enum { TUNE_TITLE, TUNE_RACE, TUNE_FINISH, TUNE_TIME_UP, TUNES };

// Registers the effects and the drone's waveform. Call once at boot.
void sound_init(void);

void sound_play(int effect);

// Starts a tune from its beginning, replacing the one playing.
void sound_tune(int tune);

// Silence: no tune, no drone. For the title and for a new race, so nothing
// carries over from the screen before.
void sound_quiet(void);

// A frame of racing (not paused, not over): the drone follows the car's
// speed, and the held events (`ev`: race_step()'s RACE_EV_SKID and
// RACE_EV_OFFROAD) sound while they last: the skid's squeal, the rumble
// off the road.
void sound_drive(const Race* r, u32 ev);

// The drone stops (the race is over, or paused); the next sound_drive()
// starts it again.
void sound_drone_off(void);

// Pausing (`paused` true) or going on: the tune pauses or resumes where it
// was, the drone stops, the blip sounds.
void sound_pause(bool paused);

#endif // RACER_SOUND_H
