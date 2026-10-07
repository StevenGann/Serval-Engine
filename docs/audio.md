# Audio

Two kinds of sound: the PSG (the GBA's tone generators, almost free to play) for sound effects and chiptune music, and, planned, tracker music and sampled sound effects mixed in software by Maxmod. Samples stay in ROM; the scarce resource is CPU time for software mixing.

**Status:** PSG sound effects (with priorities) and PSG music on square 1, square 2 and noise are implemented ([below](#psg-channels), reference in [api-reference.md](api-reference.md#audioh)); they play in web builds too, which emulate the PSG from the same registers. Planned, and declared in `audio.h` so that the API is complete ([releases.md](releases.md#planned-api): every use compiles with a warning; the calls do nothing yet and warn once in debug builds): the [wave channel](#wave-channel) (`PSG_WAVE`, `psg_waves_set()`), the [sound bank](#sound-bank) (`audio_bank_set()`), [tracker music](#tracker-music) (`music_*()`) and [sampled sound effects](#sampled-sound-effects) (`sfx_*()`, with the `Sfx` handle type and `SFX_NONE`). Tracker music and sampled effects will use Maxmod's [BlocksDS fork](#maxmod-blocksds), which is chosen but not linked yet.

**Hardware:** Direct Sound A/B (two 8-bit PCM channels, each fed from a FIFO by DMA at a timer's rate) and four legacy PSG channels (two squares, wave, noise). More than two PCM voices requires software mixing, whose cost scales with voice count × mix rate.

## Two players, two volume ranges

The PSG and the mixer are separate, and so are their APIs: `psg_*()` for the tone generators, `music_*()` and `sfx_*()` for the mixer. Both play at once.

| | PSG music | Tracker music (planned) |
| --- | --- | --- |
| Calls | `psg_music_play()` ... `psg_music_set_volume()` | `music_play()` ... `music_set_speed()` |
| Data | a `PsgSong` of notes (C data, generated or hand-written), by pointer | a module (MOD, S3M, XM, IT) in the [sound bank](#sound-bank), by ID |
| Plays on | the tone generators: square 1, square 2, noise | Direct Sound A and B, mixed in software |
| CPU | about 200 cycles a frame for three tracks | a cost per channel playing (to be measured; [Maxmod's figures](#cpu-and-memory)) |
| Volume | 0-15 | 0-255 |
| Tempo | `psg_music_set_tempo()`, beats per minute | `music_set_speed()`, percent (50-200) |
| Scripts | SYS calls ([vm.md](vm.md#engine-calls)) | none yet (appended to the SYS page later) |
| Web | plays | stub: silent ([below](#web)) |

Sound effects are split the same way: `psg_play()` plays a `PsgSound` (a tone or a melody) on a tone generator, `sfx_play()` a recorded sample through the mixer. `psg_stop_all()` stops only the PSG; `music_stop()` and `sfx_stop_all()` only the mixer's sounds.

**Volume ranges differ, on purpose.** `psg_music_set_volume()` takes 0-15 because the tone generators have 16 volume steps, so every value is a real step. `music_set_volume()`, `sfx_set_volume()` and `sfx_play_ex()`'s volume take 0-255, the mixer's per-channel range (Maxmod's effect volume is 0-255; the engine will scale the music and effects master volumes to Maxmod's 0-1024), fine enough that a fade of one step a frame is smooth.

## PSG channels

Unused by Maxmod, so exposed as a zero-mixer-cost API for sound effects (UI bleeps, pickups) and chiptune music. PSG music is the engine's only music until tracker music is implemented.

**Sound effects** (`include/serval/audio.h`, `src/gba/psg.c`): `PsgSound` effects on square channels 1-2 and the noise channel, registered with `psg_table_set()` and played by ID with `psg_play()`. A sound has a frequency in Hz, a duration in frames, duty (tone color), volume, a fade envelope, a pitch slide (square 1), and optionally a melody of notes (with rests, each lasting `.frames`), stepped once per frame by `frame_end()`. Fields left out default sensibly (duty 50%, full volume, or silence for a fade-in); out-of-range fields are clamped and sounds that can't play are skipped, with a warning in debug builds. The wave channel (3) is [planned](#wave-channel): a sound on it is skipped, with a warning that says so. Every example except `hello` and `bunnymark` uses the PSG for all its sounds, and `serval_splash()` for its jingle; `breakout`, `platformer`, `shmup`, `blackjack` and `fireflies` (from its Lua script) also play PSG music.

**Priorities:** each channel plays one sound at a time. A sound's `.priority` (0 by default) decides what happens when another is played on its channel while it plays: one of equal or higher priority replaces it, one of lower priority is dropped. So a jingle with priority 1 can share square 2 with priority-0 gunfire without being cut off. A sound plays for its `.frames`; one without `.frames` that fades out holds its channel until it is silent (computed from its volume and fade), one that holds its volume or fades in holds it until replaced.

**Slides past the top:** the sweep unit silences square 1 when an upward slide would pass the highest frequency register value (2047), checked when the tone starts and after every step. Debug builds work out from the frequency, slide speed, step size and duration whether that happens before the sound ends, and warn once, naming the frame and what to change (start lower, slide slower, smaller steps, or a shorter sound). The model is checked against mGBA in the ROM tests.

### PSG music

**Implemented** (`PsgSong` in `audio.h`; sequencer in `src/core/psg_sequencer.c`, portable and host-tested; player in `src/gba/music.c`). A song has up to one track per channel (square 1, square 2, noise; the wave channel is [planned](#wave-channel), and a track on it is left out with a warning). A track is an array of `PsgNote`s, each a note number and a length in ticks, plus the track's duty, volume and fade envelope (the same fields as a sound, applied to every note) and a loop point.

- **Notes:** `PSG_C0` to `PSG_B10` (MIDI numbering: `PSG_C4` = 60 is middle C, `PSG_A4` = 440 Hz; sharps are `PSG_CS4` and so on, flats are the sharp below; plus 12 is an octave up), and `PSG_REST` (0). Squares play `PSG_C2` (65 Hz) and up. On the noise channel a note selects the noise rate closest to its pitch, so low notes rumble (kick) and high ones hiss (snare, hi-hat). Notes map to register values through two tables in ROM (432 bytes): no division while playing.
- **Time:** `.tempo` in beats per minute (default 120) and `.ticks_per_beat` (default 4, so a tick is a 16th note). Each frame adds the tempo's ticks per frame (16.16 fixed point) to a phase; each tick falls on the frame closest to its exact time, so tempos needn't divide the frame rate, and a song stays in time indefinitely. A tick can't be shorter than a frame (3583 ticks a minute; faster clamps, with a warning). A note's length 0 means the track's `.length`, which defaults to a beat.
- **Loops:** each track loops on its own, from its `.loop` note (0 by default: the start; set it past an intro) or plays once with `PSG_NO_LOOP`. A song whose tracks have all ended stops (`psg_music_playing()` turns false).
- **Sound effects over music:** a sound effect takes over a channel the music uses if its priority is at least the song's `.priority` (0 by default: every sound effect does). The music keeps time underneath and comes back when the sound ends: a held note (track `.fade` 0 or fading in) comes back at once at the current position; a fading note would come back louder than it would be by then, so that track comes back with its next note.
- **Pause:** `psg_music_pause()` stops the song's time and silences its channels; sound effects play on, on every channel, whatever their priority (the song's `.priority` is set aside while paused), and the paused music doesn't come back when they end. `psg_music_resume()` carries on exactly where it stopped, to the fraction of a tick: held notes start again at once, fading tracks come back with their next note (as after a sound effect). `psg_music_playing()` stays true while paused; `psg_music_paused()` tells the two apart. Pausing with no song, pausing twice and resuming music that isn't paused do nothing; `psg_music_play()`, `psg_music_stop()` and `psg_stop_all()` end a pause.
- **Tempo changes:** `psg_music_set_tempo(bpm)` changes the tempo of the song playing from where it is (0: back to the song's `.tempo`); `psg_music_play()` starts every song at its own tempo. The phase keeps the song's exact position within its tick and is re-centred on half a frame at the new tempo, so the song neither jumps nor drifts: later ticks fall on the frame closest to their time at the new tempo (the first one at most a frame late, when it was due within half a frame of the change). The division (ticks per frame for the tempo) happens in the call, not per frame. Without a song it does nothing and warns.
- **Volume:** `psg_music_set_volume()` (0-15; above 15 plays as 15, with a warning) scales each note's starting volume from each channel's next note; sound effects keep theirs. It doesn't use the master volume (SOUNDCNT_L), which would turn the sound effects down too.
- **Scripts:** the VM's SYS calls play PSG music: a song by its index in `vm_bind()`'s bindings, plus stop, pause and resume ([vm.md](vm.md#engine-calls)).
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

### Wave channel

**Planned.** Declared: `PSG_WAVE` (3), a channel for `PsgSound.channel` and `PsgTrack.channel`, and `psg_waves_set(const u32* waves, u8 count)`. In this engine version `psg_play()` skips a sound on `PSG_WAVE` and `psg_music_play()` leaves a track on it out, each warning once (debug builds) that the wave channel is planned, apart from the warning for an invalid channel; `psg_waves_set()` ignores its table and warns once. Nothing touches the channel's registers or wave RAM: it stays off (`serval_init()` sends only channels 1, 2 and 4 to the speakers). Tested by `tests/planned_audio_tests.c`.

**The hardware:** tone channel 3 plays 32 4-bit samples from wave RAM, at 2,097,152 / (2048 − n) samples a second for frequency register value n, so a 32-step waveform sounds at 65,536 / (2048 − n) Hz: 32 Hz to 65.5 kHz, an octave below a square at the same n. Wave RAM has two 16-byte banks: the CPU writes the one not playing. Its volume has five settings (0, 25%, 50%, 75%, 100%); it has no envelope and no sweep.

**The design** (fixed by the declarations; no data format changes):

- **Waveforms:** `psg_waves_set()` registers a table of `count` waveforms, each 4 words as wave RAM holds them: the 32 steps play from the first word's lowest byte up, the high nibble of each byte first (a first word of `0x67452301` plays 0, 1, 2, ... 7). The table stays in ROM. `.duty` picks the waveform: n plays the nth (0, the default, the first); with none registered, a built-in triangle; a `.duty` of `count` or more plays waveform 0, with a warning. `count` 0 goes back to the triangle; a NULL or invalid table with a count is ignored (warns). A new table takes effect from the channel's next note. Changing waveform costs a 16-byte copy into the idle bank.
- **Pitch:** `.frequency` 32 to 65535 Hz; notes from `PSG_C1` (33 Hz). No new note table: a note on the wave channel uses the square table's value for the note an octave up (65,536 / f = 131,072 / 2f).
- **Volume:** `.volume` plays as the nearest of the four non-zero levels (25%, 50%, 75%, 100% of 15); `.fade` is stepped by the engine between them, since the channel has no envelope. `.slide` is ignored with a warning, as on the noise channel. Priorities, music and `psg_music_set_volume()` behave as on the other channels.
- **Web:** `src/web/apu.c` already emulates the wave channel with both banks ([platforms.md](platforms.md#what-is-faked-or-missing)), so web builds will play it as the GBA does.

## Maxmod (BlocksDS)

**Decided:** tracker music and sampled sound effects will be played by Maxmod, from the BlocksDS fork ([blocksds/maxmod](https://github.com/blocksds/maxmod), a mirror of [codeberg.org/blocksds/maxmod](https://codeberg.org/blocksds/maxmod)): it is actively maintained, needs no devkitARM, and is the ecosystem the DS target plans on ([platforms.md](platforms.md#targets)). Not linked yet. What the choice implies:

- **Licence: ISC** (`COPYING`: © 2008-2009 Mukunda Johnson, 2021-2026 Antonio Niño Díaz, 2023 Lorenzooone). Games will include its notice ([licensing.md](licensing.md)); no copyleft.
- **`mmutil` from BlocksDS builds the sound bank** ([blocksds/mmutil](https://github.com/blocksds/mmutil), also ISC). Studio Advance runs it; a CMake function for games built by hand is post-1.0.
- **The bank's format is the linked Maxmod's soundbank format**, opaque to games ([below](#sound-bank)), so the API doesn't depend on it.
- **`serval.json` will gain an `mmutil` version** (an additive manifest field, [releases.md](releases.md#manifest)) when the sound bank, tracker music and sampled effects are implemented, so that the editor runs the `mmutil` matching the engine's Maxmod.

Checked against BlocksDS's sources at tag `v1.24.0-blocks` (`a797317`, 2026-09-13, the latest tag) and `master` at `0ca65d5` (2026-10-07).

### What the player and the mixer are written in

| Part | Language | Files |
| --- | --- | --- |
| The player: module playback, effects, the bank | C | `source/core/mas.c`, `source/core/effect.c`, `source/core/mas_arm.c` (C despite the name: on the GBA some of its routines are compiled as ARM code into IWRAM) |
| GBA glue: `mmInit()`, `mmFrame()` (runs the player's ticks, calls the mixer) | C | `source/gba/main_gba.c` |
| GBA mixer setup, DMA and timer, `mmVBlank()` | C | `source/gba/mixer.c` |
| **The GBA mixer, `mmMixerMix()`** | **ARM assembly**, in IWRAM | `source/gba/mixer_asm.s` (the only assembly in the GBA build; the other `.s` file is the DS's ARM7 mixer) |
| A GBA mixer in C (`MM_GBA_MIXER_IN_C`; `master` only, since 2026-09-24) | C | `source/gba/mixer.c`; its comment says it is for debugging: "much slower than the assembly version", with noise during playback |
| A headless backend (`master` only, since 2026-09-25): the same player, with a C mixer writing 8-bit stereo at any sample rate into a buffer (`mmMix()`) | C | `source/headless/`, `include/maxmod_headless.h`; built by the repository's `CMakeLists.txt` (which builds only this backend), for Emscripten too (`build_emscripten.sh`, with an SDL3 demo player) |

**What this means for the web:** the GBA mixer can't run there (it is ARM assembly, and feeds DMA FIFOs the web doesn't emulate), but the player is C, so the web doesn't need a different MOD/XM player: Maxmod's C player with a C mixer can mix straight into the page's audio output, from the same bank. The headless backend is exactly that, once it is in a release; its mixer reads the GBA bank's sample format (`mm_mas_gba_sample`, in `source/headless/mixer.c`). To check then: it allocates (its mixer `calloc`s a buffer in every `mmMix()` call, `mmInitDefaultMem()` allocates the channels, and the `mmInit()` taking the caller's memory is still private: "TODO: Make this public"), against the engine's no-malloc rule, and it outputs 8-bit samples. Until a web player exists the web keeps [silent stubs](#web), also after the GBA plays tracker music and sampled effects.

### Hardware it claims

From `source/gba/mixer.c` (every hardware register the GBA build writes is in that file) and `documentation/hardware_usage.md`:

- **Timer 0:** the sample clock (reloaded with −2^24 / rate: −1064 at 15,768 Hz), from `mmInit()` to `mmEnd()`.
- **DMA 1 and DMA 2:** feed FIFO A and FIFO B from the wave buffer (repeat, FIFO timing, 32-bit), restarted by `mmVBlank()` every other VBlank.
- **Direct Sound A and B** and their FIFOs: A plays left, B right. `SOUNDCNT_H` is written whole: `0x9A0C` in `mmInit()` (A and B at 100%, both on timer 0, FIFOs reset), 0 in `mmEnd()`. Both writes also set its bits 0-1, the tone generators' share of the volume, to 25%: the engine must set them back to 100% (`SDS_DMG100`, as `serval_psg_init()` does) after either, or the PSG drops to a quarter of its volume when a bank is registered.
- **`SOUNDCNT_X`:** master enable (`0x80`; the engine already turns sound on).
- **The VBlank interrupt:** `mmVBlank()` swaps the double buffer's halves. "The timing is extremely critical, so make sure the handler does not get interrupted" (`hardware_usage.md`): it runs first in the engine's VBlank handler, from libtonc's interrupt dispatcher, which the engine owns.
- **Not used:** DMA 0 and 3, timers 1-3, `SOUNDCNT_L` and the tone generators.

### CPU and memory

Maxmod's own figures, for 8 channels mixed at 16 kHz (`documentation/cpu_usage.md`, `documentation/memory_usage.md`), measured with the same `WAITCNT` as `serval_init()` sets (`0x4317`, libtonc's `WS_STANDARD`):

- **CPU:** about 2.6% of a frame with nothing playing; per active channel, about 1.2-1.5% for mixing (hard-panned channels mix fastest) and about 2% with module playback, so roughly 2.6% + 2% × channels: about 18% of a frame for an 8-channel module at its busiest.
- **Memory:** IWRAM about 5.7 KB (including the assembly mixer, the player's ARM routines and the mixing buffer: 1,056 bytes at 16 kHz), EWRAM about 1.8 KB (the wave buffer, the same size, and the channel arrays: 40 + 28 + 16 bytes per channel), ROM about 6.5 KB, plus the bank. IWRAM is the scarce one: 32 KB shared with games, of which the bigger examples use 16-22 KB ([development.md](development.md#memory-use)).

The engine's own figures (`frame_cpu_cycles()`, `tools/bench.sh`, `arm-none-eabi-size`) are measured when it is implemented.

## Sound bank

**Planned.** Declared: `audio_bank_set(const void* bank)`. In this engine version it ignores the bank and warns once.

- **Built by BlocksDS's `mmutil`** from the project's modules and WAV files: a binary bank, linked into the ROM, and a header numbering modules `MOD_<file>` and samples `SFX_<file>` from 0, plus `MSL_NSONGS`, `MSL_NSAMPS` and `MSL_BANKSIZE` (`source/msl.cpp` in mmutil). The sample count includes the modules' samples. A sample file named `none` would make mmutil define `SFX_NONE`, which `audio.h` already defines: the build must refuse it.
- **Opaque:** its format is the linked Maxmod's soundbank (mmutil's GBA format). Games and Studio Advance pass it whole; nothing in the engine's API depends on its layout. Projects keep modules and WAVs as sources, like all assets, so a format change in a Maxmod update costs a rebuild with the matching `mmutil`, not a project change; `serval.json`'s `mmutil` version says which one.
- **Stays in ROM:** Maxmod reads samples and patterns in place (`mmInit()` keeps the pointer), so the bank must stay valid while registered.
- **Registering** stops any tracker music and sampled effects; another bank replaces it; NULL unregisters it; an invalid pointer is refused (warns), leaving no bank. The PSG is unaffected.

## Tracker music

**Planned.** Declared: `music_play(u16 music_id, bool loop)`, `music_stop()`, `music_playing()`, `music_pause()`, `music_resume()`, `music_paused()`, `music_set_volume(u8 volume)`, `music_set_speed(u16 percent)`. In this engine version each does nothing (`music_playing()` and `music_paused()` return false) and warns once. The calls mirror `psg_music_*()`:

- `music_play()` starts a module (a `MOD_*` number) from its beginning, replacing the one playing, paused or not; `loop` false plays it once, then `music_playing()` turns false. It resets the speed to 100; the volume stays. Without a bank, or with an ID the bank doesn't have, it is ignored with a warning. (Maxmod: `mmStart()` with `MM_PLAY_LOOP` or `MM_PLAY_ONCE`.)
- `music_stop()`; `music_playing()` is true while a module plays, paused or not.
- `music_pause()` / `music_resume()` hold the music exactly where it is; `music_paused()` tells paused from playing. Pausing with nothing playing, pausing twice and resuming music that isn't paused do nothing; `music_play()`, `music_stop()` and `audio_bank_set()` end a pause. (Maxmod: `mmPause()`, `mmResume()`.)
- `music_set_volume()`: 0 (silent) to 255 (the default: the module's own volumes), at once, for this module and the next. (Maxmod: `mmSetModuleVolume()`, 0-1024.)
- `music_set_speed()`: percent of the module's tempo, from where it is, pitch unchanged; 0 means 100, and values outside 50-200 are clamped with a warning: Maxmod's tempo factor ranges from 0.5 to 2.0 (`mmSetModuleTempo()`, `0x200`-`0x800` in Q10).

## Sampled sound effects

**Planned.** Declared: `typedef u16 Sfx`, `SFX_NONE`, `sfx_play(u16 sfx_id)`, `sfx_play_ex(u16 sfx_id, u8 volume, s8 pan, FIXED pitch, u8 priority)`, `sfx_stop(Sfx)`, `sfx_playing(Sfx)`, `sfx_stop_all()`, `sfx_set_volume(u8 volume)`. In this engine version each does nothing (`sfx_play()` and `sfx_play_ex()` return `SFX_NONE`, `sfx_playing()` false) and warns once. `Sfx` and `SFX_NONE` are not planned themselves: `SFX_NONE` already means what it always will.

- **IDs and handles:** `sfx_id` is a sample's number in the bank (an `SFX_*` from mmutil's header, from 0). `sfx_play()` returns an `Sfx`, a handle to that one playing, like an `Entity`: it goes stale when the effect ends or is stopped, and is generational, so a stale handle never stops a later effect. `SFX_NONE` (0) never refers to an effect; it is also Maxmod's invalid handle (`MM_SFXHAND_INVALID`).
- **`sfx_play_ex()`:** volume 0-255; pan −128 (left) to 127 (right), 0 centred; pitch a `FIXED` factor of the recorded pitch (`FX_ONE` as recorded, `FX(2)` an octave up; 0 means `FX_ONE`; clamped to `FX_ONE / 16`-`FX(16)` with a warning); priority 0-255. `sfx_play(id)` is `sfx_play_ex(id, 255, 0, FX_ONE, 0)`. (Maxmod: `mmEffectEx()`, whose rate is 6.10 fixed point and panning 0-255.)
- **Priority:** when no mixer channel is free, the playing effect of lowest priority stops for the new one if the new one's priority is at least as high, as `PsgSound.priority` works; otherwise the new one doesn't play (`SFX_NONE`). Maxmod's effects have no priority (a new one takes a free channel, else the quietest channel in the background, never a note the music is playing or an effect that hasn't been released: `mmAllocChannel()` in `source/core/mas_arm.c`, channel types in `source/core/channel_types.h`), so the engine keeps it.
- **Loops:** an effect whose sample has loop points plays until `sfx_stop()` or `sfx_stop_all()`.
- **`sfx_set_volume()`:** the volume of all effects, 0-255 (255, the default, leaves each effect's own), at once, playing ones too: an options menu's "sound volume". The music and the PSG are unaffected. (Maxmod: `mmSetEffectsVolume()`, 0-1024.)

## Frame loop

Today `frame_end()` waits for VBlank, does the VBlank flush, then steps PSG sound effects and PSG music ([frame-loop.md](frame-loop.md)). Once tracker music and sampled effects are implemented:

- **`frame_end()`**, after the PSG step, calls Maxmod's `mmFrame()`: it runs the player's ticks and mixes the next VBlank's worth of samples (round(rate / 59.737): 264 at 15,768 Hz) into the half of the double buffer that DMA isn't playing.
- **The VBlank interrupt** runs `mmVBlank()` first, which on alternate VBlanks restarts DMA 1 and 2 at the buffer's start or rewinds the write position (`source/gba/mixer.c`).

To settle in the implementation:

- `mmFrame()` must run exactly once per VBlank. A game frame that overruns into a second VBlank leaves a half buffer unmixed, which plays stale (audible); the implementation must decide how to handle it (e.g. mixing from the VBlank interrupt instead).
- `frame_cpu_cycles()` counts a frame up to the VBlank wait, so mixing after it would go unreported: the mixer's cost needs to be reported (folded into the count, or separately).
- `SOUNDCNT_H`'s PSG share must be restored after `mmInit()` and `mmEnd()` ([above](#hardware-it-claims)).
- Initialise with `mmInit()` and static buffers (mixing buffer in IWRAM, wave buffer and channels in EWRAM), never `mmInitDefault()`, which `calloc`s. `mmEnd()` calls `free()` (for `mmInitDefault()`'s buffer), so calling it would link newlib's allocator, which every ROM's `*_rom_checks` refuses ([licensing.md](licensing.md#rules-that-keep-it-this-way)); changing banks must avoid it or the engine must supply `free`.

## Configuration

**Planned, post-1.0.** The mix rate and the channel counts will be set per game at build time (new `serval_add_rom()` keywords; additive); until then the implementation uses fixed defaults. Maxmod's rates are presets with a whole number of samples per VBlank: 8, 10, 13, 16 (its standard), 18, 21, 27 and 31 kHz, quality and CPU cost both rising with the rate. It takes up to 32 module channels and 32 mixer channels (`mmInit()` refuses more); the music's notes and the effects share the mixer channels.

## Web

The PSG plays on the web as on the GBA: `src/web/apu.c` emulates the tone generators from the same registers, the wave channel included. Tracker music and sampled effects don't: the web build compiles the same stubs (`src/gba/sampled_audio.c`), so the calls work, play nothing, and warn once in debug builds (in the browser console), and it keeps them after the GBA implements the mixer, until it has a player of its own (post-1.0, no API change). Direct Sound, timers, DMA and interrupts are not emulated ([platforms.md](platforms.md#what-is-faked-or-missing)); Maxmod's player being C, the web's player can be [Maxmod's own](#what-the-player-and-the-mixer-are-written-in), mixing into the page's audio output instead of going through emulated Direct Sound.

## Other targets

- **GB/GBC:** the same four tone generators (the GBA's PSG is the Game Boy's), so the `psg_*()` calls and the wave channel carry over; no Direct Sound, so tracker music and sampled effects will be stubs there too.
- **DS:** Maxmod's DS version (BlocksDS's `maxmod9` and `maxmod7`, from the same sources) behind the same calls, mixing on the ARM7; the PSG on the DS's own tone channels.

## API

Implemented (PSG):

```c
void psg_table_set(const PsgSound *const *table, u16 count); // table of pointers; stops sound effects
void psg_play(u16 sound_id);      // replaces what its channel plays, unless that has higher priority
void psg_stop_all(void);          // PSG sound effects and music

void psg_music_play(const PsgSong *song);
void psg_music_stop(void);
bool psg_music_playing(void);
void psg_music_set_volume(u8 volume); // 0-15, music only
void psg_music_pause(void);           // time stands still, silent; sound effects play on
void psg_music_resume(void);          // exactly where it paused
bool psg_music_paused(void);
void psg_music_set_tempo(u16 tempo);  // BPM from the current position; 0: the song's own
```

Planned (declared, warn when used; [releases.md](releases.md#planned-api)):

```c
// The wave channel
PSG_WAVE                                           // PsgSound.channel, PsgTrack.channel: 3
void psg_waves_set(const u32 *waves, u8 count);    // 32 4-bit steps (4 words) per waveform

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

## Post-1.0 (planned)

Each is additive: new functions, CMake keywords or SYS calls.

- Configuration: the mix rate and channel counts per game, and a CMake function building a bank with `mmutil` for games built without Studio Advance.
- Tracker music and sampled effects on the web ([above](#web)).
- Jingles (Maxmod's second player layer, `mmJingleStart()`), the module's position and song events, changing a playing effect (volume, pan, pitch: `mmEffectVolume()`, `mmEffectPanning()`, `mmEffectRate()`).
- SYS calls for tracker music and effects, appended to the VM's SYS page.
- Streamed PCM (voice, recorded music; ROM-heavy, ADPCM costs decode time).
- Interactive music (pattern switching or layering by game state).
