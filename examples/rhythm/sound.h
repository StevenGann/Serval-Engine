#ifndef RHYTHM_SOUND_H
#define RHYTHM_SOUND_H

// The game's own sounds, on the tone generators (the music is the tracker
// modules'): a blip for each lane's press, and the menus'.

#include <serval/serval.h>

enum {
    SND_LANE,                // SND_LANE + lane: a press in that lane, one pitch a lane
    SND_MOVE = SND_LANE + 4, // a menu's cursor moved
    SND_SELECT,              // a menu's choice taken
    SND_PAUSE,               // the song paused
    SND_EFFECTS,
};

// Registers the sounds. Call once at boot.
void sound_init(void);

void sound_play(int effect);

#endif // RHYTHM_SOUND_H
