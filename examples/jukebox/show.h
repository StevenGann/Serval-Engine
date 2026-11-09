#ifndef JUKEBOX_SHOW_H
#define JUKEBOX_SHOW_H

// What the jukebox shows besides its menu's text: the cabinet, the record
// turning with the music, the meters, and lights and particles that keep
// time with the beat (music_position()) and answer the effect pads.

#include "jukebox.h"

// Builds and loads the pictures and fades the screen in over the next
// frames. Call once, after the splash.
void show_start(void);

// Draws a frame of it for the jukebox as it is now. Call once a frame,
// after the frame's input and the script's step.
void show_frame(const Jukebox* j);

#endif // JUKEBOX_SHOW_H
