# Audio

Two kinds of sound: the PSG (the GBA's tone generators, almost free to play) for sound effects and chiptune music, and tracker music and sampled sound effects mixed in software by Maxmod. Samples stay in ROM; the scarce resource is CPU time for software mixing.

**Status:** PSG sound effects (with priorities) and PSG music on all four tone generators, square 1, square 2, the [wave channel](#wave-channel) (`PSG_WAVE`, `psg_waves_set()`) and noise, are implemented ([below](#psg-channels), reference in [api-reference.md](api-reference.md#audioh)); they play in web builds too, which emulate the PSG from the same registers. So are the [sound bank](#sound-bank) (`audio_bank_set()`, built by `mmutil`, with `serval_add_soundbank()` for games built by hand), [tracker music](#tracker-music) (`music_*()`) and [sampled sound effects](#sampled-sound-effects) (`sfx_*()`, with the `Sfx` handle type and `SFX_NONE`), played on the GBA by Maxmod's [BlocksDS fork](#maxmod-blocksds) (`v1.24.0-blocks`, vendored in `third_party/maxmod/`, `src/gba/maxmod.c`, with the calls in `src/gba/sampled_audio.c`); web builds keep silent stubs for them ([below](#web)). The `jukebox` example plays both. `audio.h` has no planned names left.

**Hardware:** Direct Sound A/B (two 8-bit PCM channels, each fed from a FIFO by DMA at a timer's rate) and four legacy PSG channels (two squares, wave, noise). More than two PCM voices requires software mixing, whose cost scales with voice count × mix rate.

## Two players, two volume ranges

The PSG and the mixer are separate, and so are their APIs: `psg_*()` for the tone generators, `music_*()` and `sfx_*()` for the mixer. Both play at once.

| | PSG music | Tracker music |
| --- | --- | --- |
| Calls | `psg_music_play()` ... `psg_music_set_volume()` | `music_play()` ... `music_set_speed()` |
| Data | a `PsgSong` of notes (C data, generated or hand-written), by pointer | a module (MOD, S3M, XM, IT) in the [sound bank](#sound-bank), by ID |
| Plays on | the tone generators: square 1, square 2, the wave channel, noise | Direct Sound A and B, mixed in software |
| CPU | about 200 cycles a frame for three tracks | about 3% of a frame for the mixer, plus 1.4% per channel playing ([measured](#cpu-and-memory)) |
| Volume | 0-15 | 0-255 |
| Tempo | `psg_music_set_tempo()`, beats per minute | `music_set_speed()`, percent (50-200) |
| Scripts | SYS calls ([vm.md](vm.md#engine-calls)) | none yet (appended to the SYS page later) |
| Web | plays | stub: silent ([below](#web)) |

Sound effects are split the same way: `psg_play()` plays a `PsgSound` (a tone or a melody) on a tone generator, `sfx_play()` a recorded sample through the mixer. `psg_stop_all()` stops only the PSG; `music_stop()` and `sfx_stop_all()` only the mixer's sounds.

**Volume ranges differ, on purpose.** `psg_music_set_volume()` takes 0-15 because the tone generators have 16 volume steps, so every value is a real step. `music_set_volume()`, `sfx_set_volume()` and `sfx_play_ex()`'s volume take 0-255, the mixer's per-channel range (Maxmod's effect volume is 0-255; the engine scales the music and effects master volumes to Maxmod's 0-1024, 255 being exactly 1024), fine enough that a fade of one step a frame is smooth.

## PSG channels

Unused by Maxmod, so exposed as a zero-mixer-cost API for sound effects (UI bleeps, pickups) and chiptune music, which play beside tracker music and sampled effects at their full volume (the engine keeps the tone generators' share of the output at 100% when Maxmod starts).

**Sound effects** (`include/serval/audio.h`, `src/gba/psg.c`): `PsgSound` effects on square channels 1-2, the [wave channel](#wave-channel) (3) and the noise channel, registered with `psg_table_set()` and played by ID with `psg_play()`. A sound has a frequency in Hz, a duration in frames, duty (tone color; on the wave channel, the waveform), volume, a fade envelope, a pitch slide (square 1), and optionally a melody of notes (with rests, each lasting `.frames`), stepped once per frame by `frame_end()`. Fields left out default sensibly (duty 50%, full volume, or silence for a fade-in); out-of-range fields are clamped (`.duty` wraps around) and sounds that can't play are skipped, with a warning in debug builds (a square's `.frequency` or note below 64 Hz plays at 64 Hz, the wave channel's below 32 Hz at 32 Hz). Every example except `hello`, `bunnymark` and `jukebox` (whose music and effects are sampled, with PSG blips for its menu) uses the PSG for all its sounds, and `serval_splash()` for its jingle; `breakout`, `platformer`, `shmup`, `blackjack` and `fireflies` (from its Lua script) also play PSG music, `blackjack`'s walking bass on the wave channel.

**Priorities:** each channel plays one sound at a time. A sound's `.priority` (0 by default) decides what happens when another is played on its channel while it plays: one of equal or higher priority replaces it, one of lower priority is dropped. So a jingle with priority 1 can share square 2 with priority-0 gunfire without being cut off. A sound plays for its `.frames`; one without `.frames` that fades out holds its channel until it is silent (computed from its volume and fade), one that holds its volume or fades in holds it until replaced.

**Slides past the top:** the sweep unit silences square 1 when an upward slide would pass the highest frequency register value (2047), checked when the tone starts and after every step. Debug builds work out from the frequency, slide speed, step size and duration whether that happens before the sound ends, and warn once, naming the frame and what to change (start lower, slide slower, smaller steps, or a shorter sound). The model is checked against mGBA in the ROM tests.

### PSG music

**Implemented** (`PsgSong` in `audio.h`; sequencer in `src/core/psg_sequencer.c`, portable and host-tested; player in `src/gba/music.c`). A song has up to one track per channel (square 1, square 2, the [wave channel](#wave-channel), noise). A track is an array of `PsgNote`s, each a note number and a length in ticks, plus the track's duty, volume and fade envelope (the same fields as a sound, applied to every note) and a loop point.

- **Notes:** `PSG_C0` to `PSG_B10` (MIDI numbering: `PSG_C4` = 60 is middle C, `PSG_A4` = 440 Hz; sharps are `PSG_CS4` and so on, flats are the sharp below; plus 12 is an octave up), and `PSG_REST` (0). Squares play `PSG_C2` (65 Hz) and up, the wave channel `PSG_C1` (33 Hz) and up (lower notes play as 64 and 32 Hz, warning). On the noise channel a note selects the noise rate closest to its pitch, so low notes rumble (kick) and high ones hiss (snare, hi-hat). Notes map to register values through two tables in ROM (432 bytes): no division while playing.
- **Time:** `.tempo` in beats per minute (default 120) and `.ticks_per_beat` (default 4, so a tick is a 16th note). Each frame adds the tempo's ticks per frame (16.16 fixed point) to a phase; each tick falls on the frame closest to its exact time, so tempos needn't divide the frame rate, and a song stays in time indefinitely. A tick can't be shorter than a frame (3583 ticks a minute; faster clamps, with a warning). A note's length 0 means the track's `.length`, which defaults to a beat.
- **Loops:** each track loops on its own, from its `.loop` note (0 by default: the start; set it past an intro) or plays once with `PSG_NO_LOOP`. A song whose tracks have all ended stops (`psg_music_playing()` turns false).
- **Sound effects over music:** a sound effect takes over a channel the music uses if its priority is at least the song's `.priority` (0 by default: every sound effect does). The music keeps time underneath and comes back when the sound ends: a held note (track `.fade` 0 or fading in) comes back at once at the current position; a fading note would come back louder than it would be by then, so that track comes back with its next note.
- **Pause:** `psg_music_pause()` stops the song's time and silences its channels; sound effects play on, on every channel, whatever their priority (the song's `.priority` is set aside while paused), and the paused music doesn't come back when they end. `psg_music_resume()` carries on exactly where it stopped, to the fraction of a tick: held notes start again at once, fading tracks come back with their next note (as after a sound effect). `psg_music_playing()` stays true while paused; `psg_music_paused()` tells the two apart. Pausing with no song, pausing twice and resuming music that isn't paused do nothing; `psg_music_play()`, `psg_music_stop()` and `psg_stop_all()` end a pause.
- **Tempo changes:** `psg_music_set_tempo(bpm)` changes the tempo of the song playing from where it is (0: back to the song's `.tempo`); `psg_music_play()` starts every song at its own tempo. The phase keeps the song's exact position within its tick and is re-centred on half a frame at the new tempo, so the song neither jumps nor drifts: later ticks fall on the frame closest to their time at the new tempo (the first one at most a frame late, when it was due within half a frame of the change). The division (ticks per frame for the tempo) happens in the call, not per frame. Without a song it does nothing and warns.
- **Volume:** `psg_music_set_volume()` (0-15; above 15 plays as 15, with a warning) scales each note's starting volume from each channel's next note; sound effects keep theirs. It doesn't use the master volume (SOUNDCNT_L), which would turn the sound effects down too.
- **Scripts:** the VM's SYS calls play PSG music: a song by its index in `vm_bind()`'s bindings, plus stop, pause and resume ([vm.md](vm.md#engine-calls)).
- **Cost:** about 200 cycles per frame on average for a three-track song (peak about 1,300 when all three channels start a note in the same frame), under 0.1% of a frame, and about 200 more with a fading bass on the wave channel too, whose fade the engine steps; about 50 cycles while paused; nothing when no song plays (the player is hooked in by `psg_music_play()`, so games without music don't link it). The PSG step itself, `frame_end()`'s, visits only the sound effects that count frames: about 100-125 cycles a frame with nothing playing (150-175 before the wave channel, when it visited every channel). IWRAM: 6 bytes in every ROM, about 75 more with music; the sound effects' state is in EWRAM.

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

A walking bass on the wave channel, in a waveform of its own:

```c
// 32 steps of sin(x) + 0.3 sin(2x) + 0.1 sin(3x), from 0 to 15
static const u32 bass_wave[4] = {0xFEFFCE8A, 0x98BACBDD, 0x32446587, 0x35010021};
static const PsgNote walk[] = {{PSG_F2, 0}, {PSG_A2, 0}, {PSG_C3, 0}, {PSG_E3, 0}};
static const PsgTrack bass = {
    .channel = PSG_WAVE, .duty = 0, .fade = -3, .length = 6, .notes = walk, .note_count = 4};

psg_waves_set(bass_wave, 1);   // once at startup; .duty 0 plays bass_wave
```

```c
static const u16 win_notes[] = {523, 659, 784, 1047};          // Hz; 0 is a rest
static const PsgSound hit = {.channel = PSG_SQUARE1, .frequency = 880, .frames = 8, .fade = -1};
static const PsgSound win = {.channel = PSG_SQUARE2, .frames = 7, .notes = win_notes, .note_count = 4};
static const PsgSound *const sounds[SOUND_COUNT] = {[SND_HIT] = &hit, [SND_WIN] = &win};

psg_table_set(sounds, SOUND_COUNT);   // once at startup
psg_play(SND_HIT);
```

### Wave channel

**Implemented** (`PSG_WAVE` (3) for `PsgSound.channel` and `PsgTrack.channel`, and `psg_waves_set(const u32* waves, u8 count)`; the hardware side in `src/gba/wave.c`, the portable parts (pitch, levels, the fade, the waveform a note picks) in `src/core/psg_wave.c`, host-tested). The fourth tone generator plays a waveform of the game's own, so it suits bass lines and soft leads: `blackjack`'s walking bass plays on it, in a round waveform that sounds like a plucked upright bass where a square buzzes. Sound effects, priorities, music, `psg_music_set_volume()`, pause and tempo work on it as on the other channels; `serval_init()` sends all four channels to both speakers. Tests: `tests/psg_wave_tests.c` (portable, host and ROM) and `tests/rom/wave_tests.c` (the registers and wave RAM in mGBA).

**The hardware:** tone channel 3 plays 32 4-bit samples from wave RAM, at 2,097,152 / (2048 − n) samples a second for frequency register value n, so a 32-step waveform sounds at 65,536 / (2048 − n) Hz: 32 Hz to 65.5 kHz, an octave below a square at the same n. Wave RAM has two 16-byte banks: the CPU reads and writes the one not selected for playback. Its volume has five settings (0, 25%, 50%, 75%, 100%); it has no envelope and no sweep. A bank that plays is rotated in place, a step at a time (it is a shift register), so a waveform restarts from wherever it got to: the same sound, in another phase.

- **Waveforms:** `psg_waves_set()` registers a table of `count` waveforms, each 4 words as wave RAM holds them: the 32 steps play from the first word's lowest byte up, the high nibble of each byte first (a first word of `0x67452301` plays 0, 1, 2, ... 7). The table stays in ROM (word-aligned, as a `u32` array is). `.duty` picks the waveform: n plays the nth (0, the default, the first); with none registered, a built-in triangle (0, 1, ... 15, 15, ... 0), which every `.duty` plays (past 0, with a warning); a `.duty` of `count` or more plays waveform 0, with a warning (once until the next `psg_waves_set()` or `psg_table_set()`). `count` 0 goes back to the triangle; a NULL, invalid or unaligned table with a count is ignored, warning each time: the table registered before plays on. A new table takes effect from the channel's next note. A track's `.duty` is checked as its notes play, not by `psg_music_play()`, since the table may change while a song plays.
- **Banks:** a note whose waveform isn't the one playing writes it (16 bytes) into the bank the CPU sees, the idle one, then selects that bank, turns the channel on and restarts it; a note with the waveform playing only restarts the channel. So a bass line in one waveform copies it once, and a sound effect in another waveform costs a copy going in and the music's note one coming back. `psg_waves_set()` makes the next note copy afresh. Silence (a rest, the end of a sound, a pause, `psg_stop_all()`) turns the channel's DAC off, which stops it at once.
- **Pitch:** `.frequency` 32 to 65535 Hz (lower plays at 32 Hz, with a warning, as squares warn below 64 Hz); notes from `PSG_C1` (33 Hz). No new note table: a note on the wave channel uses the square table's value for the note an octave up (65,536 / f = 131,072 / 2f); the top octave, past the table (`PSG_C10` and up, 16.7 kHz and more), is halfway between the square's value and 2048. Notes below `PSG_C1` play at 32 Hz (warns). Measured in mGBA and in the web build: a 220 Hz tone plays at 220.66 Hz (65,536 / 297) on both, and `blackjack`'s bass within 0.5 cents of the hardware's pitch for its notes on almost every beat (3.2 at worst), within 3 cents of equal temperament.
- **Volume:** `.volume` plays as the nearest of the four non-zero levels: 1-5 as 25%, 6-9 as 50%, 10-13 as 75% and 14-15 as 100% of 15 (0 means 15, or silence for a fade-in, as on the other channels). `psg_music_set_volume()` scales a track's volume first, then the result is rounded to a level.
- **Fades:** the channel has no envelope, so `frame_end()`'s PSG step moves `.fade` along once a frame, timed exactly as the hardware's envelope on the other channels: a volume step every |`.fade`| 64ths of a second (a frame is 280,896 cycles and a 64th of a second 262,144, counted in units of 64 cycles with no drift), the level following the volume through 100%, 75%, 50% and 25% to silence (or up from silence, for a fade-in). A sound that fades out without `.frames` holds its channel until silent, as on the others. The step costs about 300 cycles a frame while a note fades (measured with a sound effect's frame count) and nothing otherwise (a flag in IWRAM says whether one does).
- **`.slide`** is ignored with a warning, as on the noise channel.
- **Memory and cost:** the channel's state is in EWRAM (16 bytes), and so are the sound effects' voices now (48 bytes, all four channels'), which the PSG step reads only while they count frames; IWRAM keeps two flags. So a game that never plays the wave channel pays nothing for it: 28 bytes less IWRAM than before it (`hello`: 5,716 to 5,688 bytes of `.bss`; with music, whose sequencer has a fourth track, 20 less: `blackjack`, 12,508 to 12,488), and the idle PSG step went from 150-175 cycles to 100-125 ([above](#psg-music)). ROM: about 550 bytes of code more in every ROM, 1.2 KB with music. A note costs about 100 cycles more when it copies a waveform.
- **Web:** `src/web/apu.c` emulates the wave channel with both banks ([platforms.md](platforms.md#what-is-faked-or-missing)), and plays it as mGBA does: the same pitch, the four levels in the same ratios, the same fades and harmonics (checked by rendering both; `tests/web_apu_tests.c` also plays the engine's bank switching). It doesn't rotate a playing bank, so a waveform restarts from its first step: inaudible. Like the rest of the web's sound it sees the registers once a frame, so when two notes change waveform in the same frame the second's plays, unless it is the one the idle bank held before that frame (the web can't see that write), and then the bank playing keeps the first's until the next change.

## Maxmod (BlocksDS)

**Implemented:** tracker music and sampled sound effects are played by Maxmod, from the BlocksDS fork ([blocksds/maxmod](https://github.com/blocksds/maxmod), a mirror of [codeberg.org/blocksds/maxmod](https://codeberg.org/blocksds/maxmod)): it is actively maintained, needs no devkitARM, and is the ecosystem the DS target plans on ([platforms.md](platforms.md#targets)). The engine vendors tag `v1.24.0-blocks` (`a797317`, 2026-09-13, the latest tag), its GBA sources only (`third_party/maxmod/`, whose `VENDORED.md` says what was left out: the DS build, the makefiles, the documentation), compiled into the engine's library as upstream's GBA build compiles it (Thumb, `-O2`, its hot routines in IWRAM as ARM code). A game links it only if it registers a bank: `audio_bank_set()` hooks the mixer into `frame_end()` and the VBlank handler, as palette writes and raster effects hook themselves in, and sets the table through which `music_*()` and `sfx_*()` reach Maxmod (`src/gba/sampled_audio.c`, which without a bank only warns), so other games keep their IWRAM, also those that call `music_*()` and `sfx_*()` (every scripted game does, through the VM's SYS calls; `tests/rom/sampled_audio_calls_main.c` checks its link map).

- **Licence: ISC** (`COPYING`: © 2008-2009 Mukunda Johnson, 2021-2025 Antonio Niño Díaz, 2023 Lorenzooone). A game that links it includes its notice ([licensing.md](licensing.md)); no copyleft.
- **`mmutil` from BlocksDS builds the sound bank** ([blocksds/mmutil](https://github.com/blocksds/mmutil), also ISC: `COPYING` © 2008 Mukunda Johnson; its `source/nds.c` adds © 2026 Antonio Niño Díaz), at the same tag, `v1.24.0-blocks` (`f8abd4f`). Studio Advance runs it; games built by hand call `serval_add_soundbank()` ([below](#sound-bank)). `tools/build-mmutil.sh DIR` builds it from that tag (checking its commit), and `tools/setup-dev.sh` installs it into `~/opt/mmutil-1.24.0-blocks/` and exports `SERVAL_MMUTIL`.
- **The bank's format is the linked Maxmod's soundbank format**, opaque to games ([below](#sound-bank)), so the API doesn't depend on it.
- **`serval.json` names the `mmutil` version**, `"toolchain": {"mmutil": "1.24.0-blocks"}` (exact; the BlocksDS tag is `v` and this, and `mmutil -V` prints `mmutil v1.24.0-blocks`), an additive manifest field ([releases.md](releases.md#manifest)), so that the editor runs the `mmutil` matching the engine's Maxmod. `serval_add_soundbank()` refuses any other version.

### What the player and the mixer are written in

| Part | Language | Files |
| --- | --- | --- |
| The player: module playback, effects, the bank | C | `source/core/mas.c`, `source/core/effect.c`, `source/core/mas_arm.c` (C despite the name: on the GBA some of its routines are compiled as ARM code into IWRAM) |
| GBA glue: `mmInit()`, `mmFrame()` (runs the player's ticks, calls the mixer) | C | `source/gba/main_gba.c` |
| GBA mixer setup, DMA and timer, `mmVBlank()` | C | `source/gba/mixer.c` |
| **The GBA mixer, `mmMixerMix()`** | **ARM assembly**, in IWRAM | `source/gba/mixer_asm.s` (the only assembly in the GBA build; the other `.s` file is the DS's ARM7 mixer) |
| A GBA mixer in C (`MM_GBA_MIXER_IN_C`; `master` only, since 2026-09-24) | C | `source/gba/mixer.c`; its comment says it is for debugging: "much slower than the assembly version", with noise during playback |
| A headless backend (`master` only, since 2026-09-25): the same player, with a C mixer writing 8-bit stereo at any sample rate into a buffer (`mmMix()`) | C | `source/headless/`, `include/maxmod_headless.h`; built by the repository's `CMakeLists.txt` (which builds only this backend), for Emscripten too (`build_emscripten.sh`, with an SDL3 demo player) |

**What this means for the web:** the GBA mixer can't run there (it is ARM assembly, and feeds DMA FIFOs the web doesn't emulate), but the player is C, so the web doesn't need a different MOD/XM player: Maxmod's C player with a C mixer can mix straight into the page's audio output, from the same bank. The headless backend is exactly that, once it is in a release: it isn't in `v1.24.0-blocks`, the release the engine vendors (checked when vendoring it, 2026-10-09: the tag has no `source/headless/`). Its mixer reads the GBA bank's sample format (`mm_mas_gba_sample`, in `source/headless/mixer.c`). To check then: it allocates (its mixer `calloc`s a buffer in every `mmMix()` call, `mmInitDefaultMem()` allocates the channels, and the `mmInit()` taking the caller's memory is still private: "TODO: Make this public"), against the engine's no-malloc rule, and it outputs 8-bit samples. Until a web player exists the web keeps [silent stubs](#web).

### Hardware it claims

From `source/gba/mixer.c` (every hardware register the GBA build writes is in that file) and Maxmod's `documentation/hardware_usage.md`. All of it from `audio_bank_set()` with a bank until `audio_bank_set(NULL)`; nothing before the first bank, in a game that never registers one, or in web builds.

- **Timer 0:** the sample clock (reloaded with −2^24 / rate: −1064 at 15,768 Hz).
- **DMA 1 and DMA 2:** feed FIFO A and FIFO B from the wave buffer (repeat, FIFO timing, 32-bit), restarted by `mmVBlank()` every other VBlank.
- **Direct Sound A and B** and their FIFOs: A plays left, B right. `SOUNDCNT_H` is written whole: `0x9A0C` in `mmInit()` (A and B at 100%, both on timer 0, FIFOs reset), 0 when the bank is unregistered (Maxmod's `mmMixerEnd()`). Both writes also set its bits 0-1, the tone generators' share of the volume, to 25%; the engine sets them back to 100% (`SDS_DMG100`, as `serval_psg_init()` does) after each, so the PSG keeps its volume.
- **`SOUNDCNT_X`:** master enable (`0x80`; the engine already turns sound on).
- **The VBlank interrupt:** `mmVBlank()` swaps the double buffer's halves. "The timing is extremely critical, so make sure the handler does not get interrupted" (`hardware_usage.md`): it runs first in the engine's VBlank handler, from libtonc's interrupt dispatcher, which doesn't nest. The handler then mixes for frames that overrun ([below](#frame-loop)), then restarts raster effects' DMA 0 if one is on ([core-api.md](core-api.md#hardware-the-engine-uses)).
- **Not used:** DMA 0 and 3, timers 1-3, `SOUNDCNT_L` and the tone generators.

### CPU and memory

Measured with the engine's fixed settings ([configuration](#configuration): 15,768 Hz, 12 mixer channels), in a release build, by the sampled audio tests (`tests/rom/sampled_audio_tests.c`, which log them), as `frame_cpu_cycles()` counts them; a frame's budget is 280,896 cycles:

| Playing | Cycles a frame | Of a frame |
| --- | --- | --- |
| Nothing (a bank registered) | 8,175 | 2.9% |
| A module's single note | 14,539 | 5.2% |
| 4 effects | 23,832 | 8.5% |
| 8 effects | 39,563 | 14.1% |
| 12 effects (every mixer channel) | 55,470 | 19.7% |

So about 2.9% + 1.4% per channel playing: 14-15% for a busy 8-channel module, 20% with every channel busy. Maxmod's own figures (`documentation/cpu_usage.md`, 8 channels at 16 kHz, the same `WAITCNT`) are in line: about 2.6% with nothing playing, 1.2-1.5% per channel mixing, about 2% with module playback. The `jukebox` example's theme (a four-channel MOD) takes 11.0% of a frame on average and 17.9% at its busiest, the example's own work included (`frame_cpu_cycles()` over its first 30 seconds, release build); with two effects started every 2 seconds on top, 11.5% and 20.3%.

**Memory**, for the `jukebox` example's release build (`arm-none-eabi-size`, the map): IWRAM 6,504 bytes (Maxmod's assembly mixer 1,968, its ARM routines 2,504, the mixing buffer 1,056, the mixer's fetch buffer 400, the player's state and the engine's), EWRAM 2,028 bytes (the wave buffer, 1,056, the channels, 848, the effects' handles, 96, and the mixer's counts), ROM about 15 KB of code, plus the bank (the jukebox's is 50 KB). Maxmod's own figures say about 5.7 KB of IWRAM. IWRAM is the scarce one: 32 KB shared with games, of which the bigger examples use 16-22 KB ([development.md](development.md#memory-use)). Games that never register a bank link none of it, even if they call `music_*()` and `sfx_*()` (about 400 bytes of ROM and 8 of IWRAM for the calls); every ROM pays 8 bytes of IWRAM for `frame_end()`'s hook and flag.

## Sound bank

**Implemented** (`audio_bank_set(const void* bank)`, `src/gba/maxmod.c`; `serval_add_soundbank()` in `cmake/Serval.cmake`, `tools/soundbank.py`).

- **Built by BlocksDS's `mmutil`** from the project's modules and WAV files: a binary bank, linked into the ROM, and a header numbering modules `MOD_<file>` and samples `SFX_<file>` from 0, plus `MSL_NSONGS`, `MSL_NSAMPS` and `MSL_BANKSIZE` (`source/msl.c` in mmutil). The numbers follow the order the files are given; the sample count includes the modules' samples, numbered as each module is added, before the WAVs after it (a jukebox of two modules then four WAVs numbers the WAVs 11-14). A file's name gives its define up to the first dot, upper-cased, with punctuation as `_`. A sample file named `none` would make mmutil define `SFX_NONE`, which `audio.h` already defines: the build refuses it.
- **`serval_add_soundbank(<target> <name> <files...>)`** builds one at build time for a game built by hand (Studio Advance runs `mmutil` itself), from modules (`.mod`, `.s3m`, `.xm`, `.it`) and WAV samples (`.wav`): `<name>.c`, the bank as `const unsigned char <name>[]` (word-aligned, in ROM), joins the target's sources, and `<name>.h`, mmutil's defines plus the bank's declaration, its include path. The game calls `audio_bank_set(<name>)`. It refuses another kind of file, two files whose names give the same define, and a sample named `none` (also a module's sample named `#none`, which mmutil also gives an `SFX_` define), and an `mmutil` other than `serval.json`'s version; it finds `mmutil` as `SERVAL_MMUTIL` (a CMake or environment variable) or on the `PATH`. The engine's tests and the `jukebox` example use it (`tests/CMakeLists.txt`, `examples/CMakeLists.txt`); web builds build the bank too, since the game's code names it.
- **Scripts:** the header's defines are plain `#define NAME number` lines, so `svm.py --header` reads them (mmutil's own header too, with its CRLF line ends): a Lua script names `MOD_*` and `SFX_*` like any other constant of the headers `serval_add_script()` is given.
- **Opaque:** its format is the linked Maxmod's soundbank (mmutil's GBA format). Games and Studio Advance pass it whole; nothing in the engine's API depends on its layout. Projects keep modules and WAVs as sources, like all assets, so a format change in a Maxmod update costs a rebuild with the matching `mmutil`, not a project change; `serval.json`'s `toolchain.mmutil` says which one.
- **Stays in ROM:** Maxmod reads samples and patterns in place (`mmInit()` keeps the pointer), so the bank must stay valid while registered.
- **Registering** stops any tracker music and sampled effects (their handles go stale) and starts Maxmod ([hardware](#hardware-it-claims)); another bank replaces it; NULL unregisters it, stopping Maxmod: Direct Sound off, DMA 1 and 2 and timer 0 stopped, the mixer out of `frame_end()` and the VBlank handler, costing nothing again. The music and effects volumes set before carry over. The PSG is unaffected, and keeps its share of the output.
- **Refused** (warns, leaving no bank, the one registered before stopped too): a pointer outside RAM and ROM or not word-aligned, data without the bank's `*maxmod*` mark, and a bank whose samples and modules aren't where its table says, of their kind (a DS bank's samples are another kind) and of the MAS format version this Maxmod reads (`0x18`, written by `mmutil` 1.24.0: a bank from an `mmutil` of another format is refused rather than misread).

## Tracker music

**Implemented** (`music_play(u16 music_id, bool loop)`, `music_stop()`, `music_playing()`, `music_pause()`, `music_resume()`, `music_paused()`, `music_set_volume(u8 volume)`, `music_set_speed(u16 percent)`). The calls mirror `psg_music_*()`:

- `music_play()` starts a module (a `MOD_*` number) from its beginning, replacing the one playing, paused or not; `loop` false plays it once, then `music_playing()` turns false. It resets the speed to 100; the volume stays. Without a bank, or with an ID the bank doesn't have, it is ignored with a warning. (Maxmod: `mmStart()` with `MM_PLAY_LOOP` or `MM_PLAY_ONCE`.)
- **Channels:** a module plays at most 8 channels (the engine's fixed setting, [configuration](#configuration)). One that uses more stops when it reaches the first row that does (Maxmod stops it, `MMCB_SONGERROR`), and debug builds warn; `music_playing()` turns false.
- `music_stop()`; `music_playing()` is true while a module plays, paused or not.
- `music_pause()` / `music_resume()` hold the music exactly where it is; `music_paused()` tells paused from playing. Pausing with nothing playing, pausing twice and resuming music that isn't paused do nothing; `music_play()`, `music_stop()` and `audio_bank_set()` end a pause. (Maxmod: `mmPause()`, which silences the module's channels and stops its time, and `mmResume()`.) Effects play on.
- `music_set_volume()`: 0 (silent) to 255 (the default: the module's own volumes), from the module's next tick (at most a few frames), for this module and the next, and across banks. (Maxmod: `mmSetModuleVolume()`, 0-1024.)
- `music_set_speed()`: percent of the module's tempo, from where it is, pitch unchanged; 0 means 100, and values outside 50-200 are clamped with a warning: Maxmod's tempo factor ranges from 0.5 to 2.0 (`mmSetModuleTempo()`, `0x200`-`0x800` in Q10). Without a module playing it is ignored, with a warning (as `psg_music_set_tempo()`).
- **Scripts:** none yet (appended to the SYS page later, [post-1.0](#post-10-planned)).

## Sampled sound effects

**Implemented** (`typedef u16 Sfx`, `SFX_NONE`, `sfx_play(u16 sfx_id)`, `sfx_play_ex(u16 sfx_id, u8 volume, s8 pan, FIXED pitch, u8 priority)`, `sfx_stop(Sfx)`, `sfx_playing(Sfx)`, `sfx_stop_all()`, `sfx_set_volume(u8 volume)`).

- **IDs and handles:** `sfx_id` is a sample's number in the bank (an `SFX_*` from mmutil's header, from 0). `sfx_play()` returns an `Sfx`, a handle to that one playing, like an `Entity`: it goes stale when the effect ends (`sfx_playing()` turns false at the next `frame_end()`) or is stopped, or a bank is registered, and is generational, so a stale handle never stops a later effect: handles count up from 1, skipping `SFX_NONE` and those in use, so a stale one could name a later effect only after 65,535 more have played. `SFX_NONE` (0) never refers to an effect; it is also Maxmod's invalid handle (`MM_SFXHAND_INVALID`). The engine keeps its own handles beside Maxmod's (whose counter is 8 bits).
- **`sfx_play_ex()`:** volume 0-255; pan −128 (left) to 127 (right), 0 centred; pitch a `FIXED` factor of the recorded pitch (`FX_ONE` as recorded, `FX(2)` an octave up; 0 means `FX_ONE`; clamped to `FX_ONE / 16`-`FX(16)` with a warning); priority 0-255. `sfx_play(id)` is `sfx_play_ex(id, 255, 0, FX_ONE, 0)`. (Maxmod: `mmEffectEx()`, whose rate is 6.10 fixed point, the pitch times 4, and panning 0-255, the pan plus 128.)
- **Priority:** when no mixer channel is free, the playing effect of lowest priority (the oldest of those) stops for the new one if the new one's priority is at least as high, as `PsgSound.priority` works; otherwise the new one doesn't play (`SFX_NONE`). Maxmod's effects have no priority (a new one takes a free channel, else the quietest channel in the background, never a note the music is playing or an effect that hasn't been released: `mmAllocChannel()` in `source/core/mas_arm.c`, channel types in `source/core/channel_types.h`), so the engine keeps it: when `mmEffectEx()` finds no channel, it cancels that effect and tries again. A module's notes keep their channels, and since a module plays at most 8 of the 12, at least 4 effects can always play.
- **Loops:** an effect whose sample has loop points (a WAV's `smpl` chunk, forward) plays until `sfx_stop()`, `sfx_stop_all()` or a new bank.
- **`sfx_set_volume()`:** the volume of all effects, 0-255 (255, the default, leaves each effect's own), at once, playing ones too (Maxmod scales an effect's volume when it starts, so the engine rescales those playing), and across banks: an options menu's "sound volume". The music and the PSG are unaffected. (Maxmod: `mmSetEffectsVolume()`, 0-1024.)
- **Scripts:** none yet (appended to the SYS page later, [post-1.0](#post-10-planned)).

## Frame loop

**Implemented.** Maxmod's wave buffer holds two VBlanks of samples a side (264 at 15,768 Hz, round(rate / 59.737)), in two halves: DMA plays one while the next VBlank's samples are mixed into the other.

- **`frame_end()`**, after the PSG step, calls Maxmod's `mmFrame()`: it runs the effects' and the player's ticks and mixes the next VBlank's samples into the half DMA isn't playing ([frame-loop.md](frame-loop.md#vblank-flush), step 8).
- **The VBlank interrupt** runs `mmVBlank()` first, which on alternate VBlanks restarts DMA 1 and 2 at the buffer's start or rewinds the write position (`source/gba/mixer.c`); then raster effects' part, if one is on.
- **`frame_cpu_cycles()` counts the mixer:** `frame_end()` measures `mmFrame()` and adds its cycles to the frame's, which it counts up to the VBlank wait (the mixer runs after it). So `frame_cpu_cycles()` and `frame_cpu_permille()` say what the game and the mixer together take of a frame; the rest of the VBlank flush (copies to VRAM, OAM and palette RAM, the PSG) stays uncounted, as before.

**Frames that overrun.** `mmFrame()` must run once per VBlank: a half left unmixed plays again what it held two VBlanks before (stale, audible). A game frame that overruns into a second VBlank would leave one, and its music would lose a frame's time. So the VBlank handler checks whether `frame_end()` is waiting for that VBlank (`frame_end()` sets a flag around its wait): if not, the game is still at work and can't mix before the next VBlank, so the handler runs `mmFrame()` itself, at once, a whole frame before that half is due. No stale audio, and the music keeps time; the same keeps the audio going while a game runs without calling `frame_end()` (a long load). It costs the overrunning frame the mixer's time, inside the interrupt (other interrupts wait that long; Direct Sound and HBlank DMA don't). Two cases are handled apart:

- **The game is inside a call into Maxmod** (`sfx_play()`, `music_play()`, ...) when the VBlank comes: the handler leaves the mixing to the call, which mixes as it returns (early in the frame: the VBlank has just happened).
- **A frame ends within a few cycles of VBlank,** after `frame_end()` has set its flag but before the BIOS's `VBlankIntrWait()` starts waiting, which then sleeps through that VBlank to the next. The handler finds the half starting to play unmixed, mixes it at once, from its start, ahead of the DMA reading it (the mixer writes faster than the DMA plays), and points the mixer at the other half again. Only the first few milliseconds of that half (the mixer's time before it writes) can play stale; the music keeps time.

Measured by the sampled audio tests in mGBA: frames of 1.5 VBlanks and 20 VBlanks without `frame_end()` get one mix per VBlank, every one early (none late, none stale), and a module played once ends on the frame it would have without the overruns; frames inside effect calls at VBlank mix as the call returns; the late case is mixed late in each half, with no half left silent; and at every VBlank the half starting to play is the one last mixed (the handler checks it, and the tests read the count).

**Not done:** mixing always from the interrupt would serve every case alike, but would put the mixer's 3-20% of a frame at the start of VBlank, before `frame_end()`'s copies to VRAM, OAM and palette RAM, which must finish within VBlank.

## Configuration

**Fixed in this version** (per game at build time is planned, post-1.0: new `serval_add_rom()` keywords, additive): Maxmod's 16 kHz rate, **15,768 Hz** (`MM_MIX_16KHZ`, its standard: 264 samples a VBlank), **8 module channels** and **12 mixer channels**, the music's notes and the effects sharing the mixer channels. Chosen from Maxmod's figures and measured ([above](#cpu-and-memory)): 15,768 Hz is Maxmod's "OK quality" standard, the mixer costs per channel playing, and 12 channels at their busiest take 20% of a frame; 8 module channels play most MODs and XMs (4-8 channels), and leave 4 mixer channels to effects whatever the music does. Maxmod's rates are presets with a whole number of samples per VBlank: 8, 10, 13, 16 (its standard), 18, 21, 27 and 31 kHz, quality and CPU cost both rising with the rate. It takes up to 32 module channels and 32 mixer channels (`mmInit()` refuses more).

## Web

The PSG plays on the web as on the GBA: `src/web/apu.c` emulates the tone generators from the same registers, the wave channel included ([above](#wave-channel)). Tracker music and sampled effects don't: the web build compiles silent stubs (`src/web/sampled_audio.c`), so the calls work, play nothing (`music_playing()` false, `sfx_play()` `SFX_NONE`), and warn once each in debug builds (in the browser console); the `jukebox` example's page shows its menus without the music and effects. Direct Sound, timers, DMA and interrupts are not emulated ([platforms.md](platforms.md#what-is-faked-or-missing)), and Maxmod's headless C backend, which could mix into the page's audio output from the same bank ([above](#what-the-player-and-the-mixer-are-written-in)), isn't in the release the engine vendors. A web player is post-1.0, with no API change.

## Other targets

- **GB/GBC:** the same four tone generators (the GBA's PSG is the Game Boy's), so the `psg_*()` calls and the wave channel carry over; no Direct Sound, so tracker music and sampled effects will be stubs there too.
- **DS:** Maxmod's DS version (BlocksDS's `maxmod9` and `maxmod7`, from the same sources) behind the same calls, mixing on the ARM7; the PSG on the DS's own tone channels.

## API

Implemented (PSG):

```c
PSG_SQUARE1, PSG_SQUARE2, PSG_WAVE, PSG_NOISE   // PsgSound.channel, PsgTrack.channel: 0, 1, 3, 2
void psg_table_set(const PsgSound *const *table, u16 count); // table of pointers; stops sound effects
void psg_play(u16 sound_id);      // replaces what its channel plays, unless that has higher priority
void psg_stop_all(void);          // PSG sound effects and music
void psg_waves_set(const u32 *waves, u8 count);   // PSG_WAVE's waveforms: 32 4-bit steps (4 words) each

void psg_music_play(const PsgSong *song);
void psg_music_stop(void);
bool psg_music_playing(void);
void psg_music_set_volume(u8 volume); // 0-15, music only
void psg_music_pause(void);           // time stands still, silent; sound effects play on
void psg_music_resume(void);          // exactly where it paused
bool psg_music_paused(void);
void psg_music_set_tempo(u16 tempo);  // BPM from the current position; 0: the song's own
```

Implemented (Maxmod; silent stubs on the web):

```c
// The sound bank, tracker music
void audio_bank_set(const void *bank);             // mmutil's bank, in ROM; NULL: none
void music_play(u16 music_id, bool loop);          // a MOD_* from the bank
void music_stop(void);
bool music_playing(void);
void music_pause(void);
void music_resume(void);
bool music_paused(void);
void music_set_volume(u8 volume);                  // 0-255
void music_set_speed(u16 percent);                 // 50-200; 0: 100

// Sampled sound effects
typedef u16 Sfx;                                   // a handle; SFX_NONE (0): none
Sfx  sfx_play(u16 sfx_id);                         // an SFX_* from the bank
Sfx  sfx_play_ex(u16 sfx_id, u8 volume, s8 pan, FIXED pitch, u8 priority);
void sfx_stop(Sfx sfx);
bool sfx_playing(Sfx sfx);
void sfx_stop_all(void);
void sfx_set_volume(u8 volume);                    // 0-255, all effects
```

```cmake
serval_add_soundbank(<target> <name> <files...>)   # modules and WAVs -> <name>.c, <name>.h
```

Planned (declared, warn when used; [releases.md](releases.md#planned-api)):

```c
// The wave channel
PSG_WAVE                                           // PsgSound.channel, PsgTrack.channel: 3
void psg_waves_set(const u32 *waves, u8 count);    // 32 4-bit steps (4 words) per waveform
```

## Post-1.0 (planned)

Each is additive: new functions, CMake keywords or SYS calls.

- Configuration: the mix rate and channel counts per game.
- Tracker music and sampled effects on the web ([above](#web)).
- Jingles (Maxmod's second player layer, `mmJingleStart()`), the module's position and song events, changing a playing effect (volume, pan, pitch: `mmEffectVolume()`, `mmEffectPanning()`, `mmEffectRate()`).
- SYS calls for tracker music and effects, appended to the VM's SYS page.
- Streamed PCM (voice, recorded music; ROM-heavy, ADPCM costs decode time).
- Interactive music (pattern switching or layering by game state).
