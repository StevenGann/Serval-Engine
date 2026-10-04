# Audio

Audio uses Maxmod for tracker music and sampled SFX, plus a lightweight API for the legacy PSG channels. Samples stay in ROM; the scarce resource is CPU time for software mixing.

**Hardware:** Direct Sound A/B (two 8-bit PCM channels via DMA FIFO, timer-clocked) and four legacy PSG channels (2 square, wave, noise). More than two PCM voices requires software mixing, which scales with voice count × mix rate.

## Maxmod

- Plays MOD, S3M, XM and IT modules plus SFX through one IWRAM mixer.
- Soundbanks are built with `mmutil` (the editor runs it behind the scenes).
- Integrates with libtonc interrupts: `mmVBlank()` in the VBlank IRQ, `mmFrame()` once per frame ([frame-loop.md](frame-loop.md)).

## Configuration

Set per project at build time:

- Mix rate: 13–18 kHz is the sweet spot, using Maxmod's frame-aligned presets.
- Voice counts, e.g. 8 music + 4 SFX.

## PSG channels

Unused by Maxmod, so exposed as a zero-mixer-cost SFX API for UI bleeps and pickups.

## API

```c
SoundHandle sfx_play(u16 sfx_id, u8 vol, u8 pan, u8 priority);
void        sfx_stop(SoundHandle h);
void        psg_play(u16 psg_sfx_id);

void music_play(u16 song_id, bool loop);
void music_stop(void);
void music_fade(u16 frames, u8 target_vol);
void music_set_tempo(u16 percent);
```

SFX priority drives voice stealing when voices run out. In the VM, sounds are ordinary script ops.

## Post-1.0

- Streamed PCM (voice, recorded music; ROM-heavy, ADPCM costs decode time).
- Interactive music (pattern switching or layering by game state).
