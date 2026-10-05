# Audio

Audio is planned to use Maxmod for tracker music and sampled SFX, plus a lightweight API for the legacy PSG channels, which also plays chiptune music. Samples stay in ROM; the scarce resource is CPU time for software mixing.

**Status:** PSG sound effects (with priorities) and PSG music are implemented ([below](#psg-channels), reference in [api-reference.md](api-reference.md#audioh)); they play in web builds too, which emulate the PSG from the same registers. Maxmod music and sampled SFX are planned: Maxmod is not linked yet and its fork is still to be chosen ([open-questions.md](open-questions.md)).

**Hardware:** Direct Sound A/B (two 8-bit PCM channels via DMA FIFO, timer-clocked) and four legacy PSG channels (2 square, wave, noise). More than two PCM voices requires software mixing, which scales with voice count × mix rate.

## Maxmod (planned)

- Plays MOD, S3M, XM and IT modules plus SFX through one IWRAM mixer.
- Soundbanks are built with `mmutil` (the editor runs it behind the scenes).
- Integrates with libtonc interrupts: `mmVBlank()` in the VBlank IRQ, `mmFrame()` once per frame ([frame-loop.md](frame-loop.md)).

## Configuration (planned)

Set per project at build time:

- Mix rate: 13–18 kHz is the sweet spot, using Maxmod's frame-aligned presets.
- Voice counts, e.g. 8 music + 4 SFX.

## PSG channels

Unused by Maxmod, so exposed as a zero-mixer-cost API for sound effects (UI bleeps, pickups) and chiptune music. Until Maxmod lands, PSG music is the engine's music.

**Sound effects** (`include/serval/audio.h`, `src/gba/psg.c`): `PsgSound` effects on square channels 1-2 and the noise channel, registered with `psg_table_set()` and played by ID with `psg_play()`. A sound has a frequency in Hz, a duration in frames, duty (tone color), volume, a fade envelope, a pitch slide (square 1), and optionally a melody of notes (with rests, each lasting `.frames`), stepped once per frame by `frame_end()`. Fields left out default sensibly (duty 50%, full volume, or silence for a fade-in); out-of-range fields are clamped and sounds that can't play are skipped, with a warning in debug builds. The wave channel (3) is unused so far. `examples/pong` and `examples/asteroids` use it for every sound, and `serval_splash()` for its jingle.

**Priorities:** each channel plays one sound at a time. A sound's `.priority` (0 by default) decides what happens when another is played on its channel while it plays: one of equal or higher priority replaces it, one of lower priority is dropped. So a jingle with priority 1 can share square 2 with priority-0 gunfire without being cut off. A sound plays for its `.frames`; one without `.frames` that fades out holds its channel until it is silent (computed from its volume and fade), one that holds its volume or fades in holds it until replaced.

**Slides past the top:** the sweep unit silences square 1 when an upward slide would pass the highest frequency register value (2047), checked when the tone starts and after every step. Debug builds work out from the frequency, slide speed, step size and duration whether that happens before the sound ends, and warn once, naming the frame and what to change (start lower, slide slower, smaller steps, or a shorter sound). The model is checked against mGBA in the ROM tests.

### PSG music

**Implemented** (`PsgSong` in `audio.h`; sequencer in `src/core/psg_sequencer.c`, portable and host-tested; player in `src/gba/music.c`). A song has up to one track per channel (square 1, square 2, noise; the wave channel is not used yet). A track is an array of `PsgNote`s, each a note number and a length in ticks, plus the track's duty, volume and fade envelope (the same fields as a sound, applied to every note) and a loop point.

- **Notes:** `PSG_C0` to `PSG_B10` (MIDI numbering: `PSG_C4` = 60 is middle C, `PSG_A4` = 440 Hz; sharps are `PSG_CS4` and so on, flats are the sharp below; plus 12 is an octave up), and `PSG_REST` (0). Squares play `PSG_C2` (65 Hz) and up. On the noise channel a note selects the noise rate closest to its pitch, so low notes rumble (kick) and high ones hiss (snare, hi-hat). Notes map to register values through two tables in ROM (432 bytes): no division while playing.
- **Time:** `.tempo` in beats per minute (default 120) and `.ticks_per_beat` (default 4, so a tick is a 16th note). Each frame adds the tempo's ticks per frame (16.16 fixed point) to a phase; each tick falls on the frame closest to its exact time, so tempos needn't divide the frame rate, and a song stays in time indefinitely. A tick can't be shorter than a frame (3583 ticks a minute; faster clamps, with a warning). A note's length 0 means the track's `.length`, which defaults to a beat.
- **Loops:** each track loops on its own, from its `.loop` note (0 by default: the start; set it past an intro) or plays once with `PSG_NO_LOOP`. A song whose tracks have all ended stops (`psg_music_playing()` turns false).
- **Sound effects over music:** a sound effect takes over a channel the music uses if its priority is at least the song's `.priority` (0 by default: every sound effect does). The music keeps time underneath and comes back when the sound ends: a held note (track `.fade` 0 or fading in) comes back at once at the current position; a fading note would come back louder than it would be by then, so that track comes back with its next note.
- **Pause:** `psg_music_pause()` stops the song's time and silences its channels; sound effects play on, on every channel, whatever their priority (the song's `.priority` is set aside while paused), and the paused music doesn't come back when they end. `psg_music_resume()` carries on exactly where it stopped, to the fraction of a tick: held notes start again at once, fading tracks come back with their next note (as after a sound effect). `psg_music_playing()` stays true while paused; `psg_music_paused()` tells the two apart. Pausing with no song, pausing twice and resuming music that isn't paused do nothing; `psg_music_play()`, `psg_music_stop()` and `psg_stop_all()` end a pause.
- **Tempo changes:** `psg_music_set_tempo(bpm)` changes the tempo of the song playing from where it is (0: back to the song's `.tempo`); `psg_music_play()` starts every song at its own tempo. The phase keeps the song's exact position within its tick and is re-centred on half a frame at the new tempo, so the song neither jumps nor drifts: later ticks fall on the frame closest to their time at the new tempo (the first one at most a frame late, when it was due within half a frame of the change). The division (ticks per frame for the tempo) happens in the call, not per frame. Without a song it does nothing and warns.
- **Volume:** `psg_music_set_volume()` scales each note's starting volume from each channel's next note; sound effects keep theirs. It doesn't use the master volume (SOUNDCNT_L), which would turn the sound effects down too.
- **Cost:** about 200 cycles per frame on average for a three-track song (peak about 1,300 when all three channels start a note in the same frame), under 0.1% of a frame; a few cycles while paused; nothing when no song plays (the player is hooked in by `psg_music_play()`, so games without music don't link it). IWRAM: 4 bytes in every ROM, about 60 more with music.

```c
static const PsgNote melody[] = {{PSG_E5, 2}, {PSG_A5, 2}, {PSG_C6, 4}, {PSG_REST, 8}};
static const PsgNote drums[] = {{PSG_C3, 2}, {PSG_C9, 2}, {PSG_C7, 2}, {PSG_C9, 2}};
static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE1, .notes = melody, .note_count = 4, .duty = PSG_DUTY_25, .volume = 10},
    {.channel = PSG_NOISE, .notes = drums, .note_count = 4, .volume = 9, .fade = -1},
};
static const PsgSong theme = {.tempo = 150, .tracks = tracks, .track_count = 2};

psg_music_play(&theme);   // loops until psg_music_stop() or psg_stop_all()
```

```c
static const u16 win_notes[] = {523, 659, 784, 1047};          // Hz; 0 is a rest
static const PsgSound hit = {.channel = PSG_SQUARE1, .frequency = 880, .frames = 8, .fade = -1};
static const PsgSound win = {.channel = PSG_SQUARE2, .frames = 7, .notes = win_notes, .note_count = 4};
static const PsgSound *const sounds[SOUND_COUNT] = {[SND_HIT] = &hit, [SND_WIN] = &win};

psg_table_set(sounds, SOUND_COUNT);   // once at startup
psg_play(SND_HIT);
```

## API

Implemented (PSG):

```c
void psg_table_set(const PsgSound *const *table, u16 count); // table of pointers; stops sound effects
void psg_play(u16 sound_id);      // replaces what its channel plays, unless that has higher priority
void psg_stop_all(void);          // sound effects and music

void psg_music_play(const PsgSong *song);
void psg_music_stop(void);
bool psg_music_playing(void);
void psg_music_set_volume(u8 volume); // 0-15, music only
void psg_music_pause(void);           // time stands still, silent; sound effects play on
void psg_music_resume(void);          // exactly where it paused
bool psg_music_paused(void);
void psg_music_set_tempo(u16 tempo);  // BPM from the current position; 0: the song's own
```

Planned (Maxmod, not implemented yet; the Maxmod fork is still to be chosen):

```c
SoundHandle sfx_play(u16 sfx_id, u8 vol, u8 pan, u8 priority);
void        sfx_stop(SoundHandle h);

void music_play(u16 song_id, bool loop);
void music_stop(void);
void music_fade(u16 frames, u8 target_vol);
void music_set_tempo(u16 percent);
```

SFX priority drives voice stealing when voices run out. In the VM, sounds are ordinary script ops.

## Post-1.0 (planned)

- Streamed PCM (voice, recorded music; ROM-heavy, ADPCM costs decode time).
- Interactive music (pattern switching or layering by game state).
