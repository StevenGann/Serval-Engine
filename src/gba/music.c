#include "serval/audio.h"

#include "../core/psg_sequencer.h"
#include "../core/psg_wave.h"
#include "../core/warn.h"
#include "internal.h"

// PSG music: plays the notes of the sequencer (src/core/psg_sequencer.c) on
// the channels no sound effect holds. Hooked into psg.c by psg_music_play(),
// so games that play no music don't link it.

static PsgSequencer seq;
static u16 controls[SERVAL_PSG_CHANNELS]; // control register bits of each track's notes
static u8 quieter;                        // 15 - the music volume (0: full volume)
static bool paused;                       // psg_music_pause(): time stands still, silent
static u8 priority;                       // the song's .priority, while paused

// Each track's control bits at the current music volume.
static void set_controls(void) {
    u32 music_volume = 15u - quieter;
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++) {
        const PsgTrack* t = seq.tracks[c].track;
        if (!t)
            continue;
        u32 volume = t->volume ? t->volume : t->fade > 0 ? 0 : 15;
        volume = volume > 15 ? 15 : volume;
        // volume * music_volume / 15, rounded.
        volume = (volume * music_volume * 17 + 128) >> 8;
        controls[c] = music_volume ? serval_psg_control(c, t->duty, volume, t->fade) : 0;
    }
}

// Starts the note a channel's track is on (or silences it, for a rest).
static void play_note(u32 c) {
    u32 note = seq.tracks[c].note;
    if (note == PSG_REST)
        serval_psg_quiet(c);
    else if (c == PSG_WAVE) // wave channel: the square table an octave up
        serval_psg_tone(c, controls[c], serval_psg_wave_note_rate(note));
    else
        serval_psg_tone(c, controls[c],
                        c == PSG_NOISE ? serval_psg_noise_rates[note]
                                       : serval_psg_square_rates[note]);
}

static void stop(void);

static void update(void) {
    if (paused)
        return;
    u32 changed = serval_psg_seq_advance(&seq);
    if (!changed)
        return;
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        if ((changed & 1u << c) && !serval_psg_sfx_active(c))
            play_note(c);
    serval_music_channels = (u8)serval_psg_seq_channels(&seq);
    if (!serval_music_channels)
        stop(); // a song that doesn't loop has ended
}

// A sound effect ended on a channel the music uses. A held note comes back at
// once; a note that fades out would come back louder than it would have been
// by now, so that track comes back with its next note.
static void resume(u32 c) {
    const PsgSeqTrack* t = &seq.tracks[c];
    if (!paused && t->track && t->track->fade >= 0 && t->note != PSG_REST)
        play_note(c);
}

static const ServalMusicHooks hooks = {update, resume, stop};

static void stop(void) {
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        if ((serval_music_channels & 1u << c) && !serval_psg_sfx_active(c))
            serval_psg_quiet(c);
    serval_music_channels = 0;
    serval_music_hooks = NULL;
    seq = (PsgSequencer){0};
    paused = false;
}

void psg_music_play(const PsgSong* song) {
    stop();
    u32 channels = serval_psg_seq_start(&seq, song);
    if (!channels)
        return;
    serval_music_priority = song->priority;
    serval_music_channels = (u8)channels;
    serval_music_hooks = &hooks;
    set_controls();
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        if ((channels & 1u << c) && !serval_psg_sfx_active(c))
            play_note(c);
}

void psg_music_stop(void) {
    stop();
}

bool psg_music_playing(void) {
    return serval_music_channels != 0;
}

void psg_music_set_volume(u8 volume) {
    if (volume > 15) {
#ifdef SERVAL_DEBUG
        static bool warned;
        if (!warned) {
            warned = true;
            SERVAL_WARN("psg_music_set_volume: volume %u is above 15; 15 is used", volume);
        }
#endif
        volume = 15;
    }
    quieter = (u8)(15 - volume);
    set_controls();
}

void psg_music_pause(void) {
    if (!serval_music_channels || paused)
        return;
    paused = true;
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        if ((serval_music_channels & 1u << c) && !serval_psg_sfx_active(c))
            serval_psg_quiet(c);
    // Silent music keeps no sound effect out.
    priority = serval_music_priority;
    serval_music_priority = 0;
}

void psg_music_resume(void) {
    if (!paused)
        return;
    paused = false;
    serval_music_priority = priority;
    // As after a sound effect: held notes come back at once, fading tracks
    // with their next note.
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        if ((serval_music_channels & 1u << c) && !serval_psg_sfx_active(c))
            resume(c);
}

bool psg_music_paused(void) {
    return paused;
}

void psg_music_set_tempo(u16 tempo) {
    if (!serval_music_channels) {
#ifdef SERVAL_DEBUG
        static bool warned;
        if (!warned) {
            warned = true;
            SERVAL_WARN("psg_music_set_tempo: no song is playing; psg_music_play() starts a song "
                        "at its own tempo, so set the tempo after starting it");
        }
#endif
        return;
    }
    serval_psg_seq_set_tempo(&seq, tempo);
}
