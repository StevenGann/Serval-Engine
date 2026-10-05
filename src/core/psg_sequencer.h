#ifndef SERVAL_CORE_PSG_SEQUENCER_H
#define SERVAL_CORE_PSG_SEQUENCER_H

// Engine-internal: the PSG music sequencer (psg_sequencer.c). Portable: it
// only keeps time and says which notes start when; src/gba/music.c plays them
// on the sound registers. Not part of the public API.

#include "serval/audio.h"

#define SERVAL_PSG_CHANNELS 3       // PSG_SQUARE1, PSG_SQUARE2, PSG_NOISE
#define SERVAL_PSG_NOTE_MAX PSG_B10 // higher notes play as this one

// One frame, in 16.16 ticks: the fastest tempo (a tick per frame).
#define SERVAL_PSG_TICK 0x10000u

// What one channel's track is playing.
typedef struct {
    const PsgTrack* track; // NULL: no track on this channel, or it has ended
    u16 index;             // the note playing
    u16 loop;              // checked track->loop: a note index, or PSG_NO_LOOP
    u8 ticks_left;         // of the note playing
    u8 note;               // the note playing (clamped to SERVAL_PSG_NOTE_MAX), or PSG_REST
    u8 default_length;     // ticks of notes whose .length is 0
} PsgSeqTrack;

typedef struct {
    u32 step;  // ticks per frame, 16.16; at most SERVAL_PSG_TICK
    s32 phase; // fraction of the next tick elapsed, 16.16, plus half a frame
               // (step / 2), so each tick falls on the frame closest to its time.
               // Below 0 after a tempo change: the tick that just played fell
               // early
    PsgSeqTrack tracks[SERVAL_PSG_CHANNELS]; // by channel
    u16 tempo;                               // the song's tempo (0 means 120)
    u8 ticks_per_beat;                       // the song's (0 means 4)
} PsgSequencer;

// Starts a song: every track is on its first note. Returns the channels
// (bits 1 << PSG_*) that have a track. A NULL or invalid song, and invalid
// tracks, are reported (debug builds) and left out.
u32 serval_psg_seq_start(PsgSequencer* seq, const PsgSong* song);

// Advances the song by one frame. Returns the channels whose track moved to
// its next note this frame (the note to play is tracks[c].note; a track that
// ended has moved to PSG_REST and its track is NULL).
u32 serval_psg_seq_advance(PsgSequencer* seq);

// Changes the tempo from the current position on (0: the song's own tempo):
// the song neither jumps nor drifts, and the next tick falls on the frame
// closest to its time at the new tempo. serval_psg_seq_start() resets it.
void serval_psg_seq_set_tempo(PsgSequencer* seq, u16 tempo);

// The channels whose track is still playing.
u32 serval_psg_seq_channels(const PsgSequencer* seq);

// 16.16 ticks per frame for a tempo (0 means 120) and ticks per beat (0 means
// 4), clamped to one tick per frame (reported, debug builds).
u32 serval_psg_tick_step(u16 tempo, u8 ticks_per_beat);

// Frequency register values by note: square channels (f = 131072 / (2048 - n);
// notes below C2 play as the lowest, 64 Hz) and the noise channel (the closest
// noise rate, f = 524288 / r / 2^(s+1), as s << 4 | r).
extern const u16 serval_psg_square_rates[SERVAL_PSG_NOTE_MAX + 1];
extern const u8 serval_psg_noise_rates[SERVAL_PSG_NOTE_MAX + 1];

// Each kind of song problem is reported once; this makes them reportable
// again (psg_table_set() calls it, as it does for sound problems).
void serval_psg_seq_reset_warnings(void);

#endif // SERVAL_CORE_PSG_SEQUENCER_H
