// Tests for tracker music and sampled sound effects on the GBA
// (src/gba/sampled_audio.c, Maxmod): the sound bank (registering, replacing,
// unregistering, refusing what isn't one), the hardware it claims and the
// PSG's share it keeps, tracker music (play once or looped, stop, pause and
// resume, volume, speed and its clamps), effects (handles going stale,
// priority, volume, pan, pitch and its clamps, loops, sfx_stop_all()), the
// frames that overrun or skip frame_end(), the mixer's cycles in
// frame_cpu_cycles(), and its costs, logged.
//
// What plays is checked numerically in the samples Maxmod mixed (the wave
// buffer DMA feeds Direct Sound from): pitch by counting zero crossings,
// level by the peak, length by when an effect's handle goes stale. The
// bank's modules and samples (tools/make-example-audio.py, set "tests"):
// tone.mod, one held note, 16 rows of 120 ms (1.92 s, 115 frames), wide.mod,
// the same with a note on channel 10 at row 4, beep.wav, 0.1 s of a 1000 Hz
// square wave at 16 kHz, and hum.wav, a looped 500 Hz square wave.
//
// Every case unregisters the bank at its end, so the suites after it run
// without the mixer.

#include "../test.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/fixed.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"
#include "test_bank.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

#define MIX_RATE 15768 // Maxmod's 16 kHz: 2^24 / 1064
#define HALF 264       // samples a VBlank
#define SIDE 528       // bytes from the left speaker's samples to the right's
#define FRAME_CYCLES 280896u

static void frames(u32 n) {
    while (n--) {
        frame_begin();
        frame_end();
    }
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Works for `n` cycles, as a game's frame might.
static void work(u32 n) {
    u32 start = cycles();
    while (cycles() - start < n)
        ;
}

// Direct Sound A and B playing, each on one speaker (A left, B right), on
// timer 0, at full volume.
static bool direct_sound_on(void) {
    return (REG_SNDDSCNT & 0x770C) == (SDS_A100 | SDS_B100 | SDS_AL | SDS_BR);
}

static bool direct_sound_off(void) {
    return (REG_SNDDSCNT & 0xFF0C) == 0 && !(REG_TM0CNT & TM_ENABLE) &&
           !(REG_DMA[1].cnt & DMA_ENABLE) && !(REG_DMA[2].cnt & DMA_ENABLE);
}

// The tone generators' share of the output: 100%, as serval_init() sets it.
static bool psg_full(void) {
    return (REG_SNDDSCNT & 3) == SDS_DMG100;
}

// The biggest sample of the last VBlank's mix on one side (0 left, 1 right).
static u32 peak(u32 side) {
    const s8* half = serval_mixer_last();
    u32 top = 0;
    for (u32 i = 0; half && i < HALF; i++) {
        int v = half[side * SIDE + i];
        u32 a = (u32)(v < 0 ? -v : v);
        top = a > top ? a : top;
    }
    return top;
}

// The pitch of what plays on the left, in Hz, over `n` frames: rising zero
// crossings (with a little hysteresis) per second of samples.
static u32 pitch_hz(u32 n) {
    u32 rises = 0;
    bool high = false;
    for (u32 f = 0; f < n; f++) {
        frames(1);
        const s8* half = serval_mixer_last();
        for (u32 i = 0; i < HALF; i++) {
            int v = half[i];
            if (!high && v > 4) {
                high = true;
                rises++;
            } else if (high && v < -4) {
                high = false;
            }
        }
    }
    return rises * MIX_RATE / (n * HALF);
}

static bool near(u32 value, u32 expected, u32 percent) {
    u32 slack = expected * percent / 100;
    return value + slack >= expected && value <= expected + slack;
}

// --- The sound bank ------------------------------------------------------------

static void bank_claims_the_hardware_and_keeps_the_psg(void) {
    CHECK(direct_sound_off() && psg_full());
    audio_bank_set(test_bank);
    CHECK(direct_sound_on() && psg_full());
    CHECK((REG_TM0CNT & TM_ENABLE) && REG_TM0D != 0);
    CHECK((REG_DMA[1].cnt & DMA_ENABLE) && (REG_DMA[2].cnt & DMA_ENABLE));
    // frame_end() mixes once a frame.
    ServalMixerStats before = serval_mixer_stats();
    frames(10);
    ServalMixerStats after = serval_mixer_stats();
    CHECK(after.mixes - before.mixes == 10 && after.early == before.early);
    CHECK(after.late == before.late && after.stale == before.stale);
    // Unregistered: all of it off again, the PSG still at full share, and
    // nothing mixed.
    audio_bank_set(NULL);
    CHECK(direct_sound_off() && psg_full());
    before = serval_mixer_stats();
    frames(3);
    CHECK(serval_mixer_stats().mixes == before.mixes);
}

// A copy of the bank in EWRAM, to break.
static SERVAL_EWRAM_BSS u32 bank_copy[sizeof(test_bank) / 4 + 1];

static void bad_banks_are_refused(void) {
    static const u32 not_a_bank[8] = {0x00010001};
    u32 warnings = debug_warning_count();
    audio_bank_set(not_a_bank);
    CHECK(debug_warning_count() - warnings == WARNINGS(1));
    CHECK(direct_sound_off());
    // No bank: nothing plays (each warns once).
    warnings = debug_warning_count();
    music_play(MOD_TONE, true);
    CHECK(!music_playing());
    CHECK(sfx_play(SFX_BEEP) == SFX_NONE);
    CHECK(debug_warning_count() - warnings == WARNINGS(1)); // the same problem: once

    // Misaligned; a sample whose MAS version isn't this Maxmod's; a sample of
    // the DS's kind. Each leaves no bank, the valid one registered before too.
    for (u32 i = 0; i < 3; i++) {
        memcpy32(bank_copy, test_bank, sizeof(bank_copy) / 4);
        const void* bad = bank_copy;
        u8* entry = (u8*)bank_copy + bank_copy[3]; // the first sample's MAS prefix
        if (i == 0)
            bad = (const u8*)test_bank + 2;
        else if (i == 1)
            entry[5]++; // version
        else
            entry[4] = 2; // MAS_TYPE_SAMPLE_NDS
        audio_bank_set(test_bank);
        CHECK(direct_sound_on());
        audio_bank_set(bad);
        CHECK(direct_sound_off() && psg_full());
        CHECK(sfx_play(SFX_BEEP) == SFX_NONE);
    }
    // The intact copy is a bank like any other.
    memcpy32(bank_copy, test_bank, sizeof(bank_copy) / 4);
    audio_bank_set(bank_copy);
    CHECK(direct_sound_on() && sfx_play(SFX_BEEP) != SFX_NONE);
    audio_bank_set(NULL);
}

static void a_new_bank_stops_everything(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, true);
    music_pause();
    Sfx hum = sfx_play(SFX_HUM);
    frames(2);
    CHECK(music_playing() && music_paused() && sfx_playing(hum));
    audio_bank_set(test_bank); // the same one again: still a new registration
    CHECK(!music_playing() && !music_paused() && !sfx_playing(hum));
    frames(2);
    CHECK(peak(0) == 0 && peak(1) == 0);
    CHECK(direct_sound_on() && psg_full());
    // And a PSG note plays on through it, at its volume.
    static const PsgSound held = {.channel = PSG_SQUARE2, .frequency = 440};
    static const PsgSound* const sounds[] = {&held};
    psg_table_set(sounds, 1);
    psg_play(0);
    audio_bank_set(NULL);
    audio_bank_set(test_bank);
    CHECK((REG_SND2CNT >> 12) == 15 && (REG_SNDSTAT & SSTAT_SQR2) && psg_full());
    psg_stop_all();
    audio_bank_set(NULL);
}

// --- Tracker music -----------------------------------------------------------------

// Frames until the music stops playing, up to `limit`.
static u32 frames_until_silent(u32 limit) {
    u32 n = 0;
    while (music_playing() && n < limit) {
        frames(1);
        n++;
    }
    return n;
}

static void music_plays_once_or_loops(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, false);
    frames(4);
    CHECK(music_playing() && !music_paused());
    CHECK(peak(0) > 8 && peak(1) > 8);
    u32 n = 4 + frames_until_silent(400);
    CHECK(n >= 112 && n <= 120); // 1.92 s: 115 frames
    frames(2);
    CHECK(peak(0) == 0);

    music_play(MOD_TONE, true);
    frames(200);
    CHECK(music_playing() && peak(0) > 8);
    // Its pitch: C-2 on a 32-sample square wave, 8363 / 32 Hz.
    CHECK(near(pitch_hz(30), 261, 3));
    music_stop();
    CHECK(!music_playing());
    frames(2);
    CHECK(peak(0) == 0);
    music_stop(); // nothing plays: nothing happens

    // An ID the bank doesn't have: ignored, warns.
    u32 warnings = debug_warning_count();
    music_play(MSL_NSONGS, true);
    CHECK(!music_playing() && debug_warning_count() - warnings == WARNINGS(1));
    audio_bank_set(NULL);
}

static void music_pauses_and_resumes(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, false);
    frames(50);
    music_pause();
    CHECK(music_playing() && music_paused());
    frames(2);
    CHECK(peak(0) == 0); // quiet
    frames(200);         // time stands still
    CHECK(music_playing() && music_paused());
    music_pause(); // twice: nothing more
    music_resume();
    CHECK(music_playing() && !music_paused());
    frames(2);
    CHECK(peak(0) > 8);
    u32 n = 52 + frames_until_silent(400);
    CHECK(n >= 112 && n <= 122); // the rest of its 115 frames
    music_resume();              // not paused: nothing happens
    CHECK(!music_paused());

    // music_play() and music_stop() end a pause.
    music_play(MOD_TONE, true);
    music_pause();
    music_play(MOD_TONE, true);
    CHECK(music_playing() && !music_paused());
    music_pause();
    music_stop();
    CHECK(!music_playing() && !music_paused());
    music_pause(); // nothing plays: nothing happens
    CHECK(!music_paused());
    audio_bank_set(NULL);
}

static void music_volume(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, true);
    frames(10);
    u32 full = peak(0);
    music_set_volume(128);
    frames(10);
    u32 half = peak(0);
    music_set_volume(0);
    frames(10);
    CHECK(peak(0) == 0 && music_playing());
    CHECK(full > 16 && near(half * 2, full, 15));
    // It stays for the next module, and for the next bank.
    audio_bank_set(test_bank);
    music_play(MOD_TONE, true);
    frames(10);
    CHECK(peak(0) == 0);
    music_set_volume(255);
    frames(10);
    CHECK(peak(0) == full);
    audio_bank_set(NULL);
}

static void music_speed_and_its_clamps(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, false);
    music_set_speed(200);
    CHECK(near(frames_until_silent(400), 57, 4)); // 115 frames at double speed
    music_play(MOD_TONE, false);
    music_set_speed(50);
    CHECK(near(frames_until_silent(400), 230, 3));
    // music_play() resets it to 100; the pitch doesn't change with it.
    music_play(MOD_TONE, true);
    music_set_speed(150);
    CHECK(near(pitch_hz(20), 261, 3));
    music_play(MOD_TONE, false);
    CHECK(near(frames_until_silent(400), 115, 3));

    // Outside 50-200: clamped, warning once; 0 means 100.
    u32 warnings = debug_warning_count();
    music_play(MOD_TONE, false);
    music_set_speed(10);
    music_set_speed(1000);
    CHECK(debug_warning_count() - warnings == WARNINGS(1));
    CHECK(near(frames_until_silent(400), 57, 4)); // 200%
    music_play(MOD_TONE, false);
    music_set_speed(400);
    music_set_speed(0);
    CHECK(near(frames_until_silent(400), 115, 3));
    // No module playing: ignored, warns.
    warnings = debug_warning_count();
    music_set_speed(150);
    CHECK(debug_warning_count() - warnings == WARNINGS(1));
    audio_bank_set(NULL);
}

static void a_module_with_too_many_channels_stops(void) {
    audio_bank_set(test_bank);
    u32 warnings = debug_warning_count();
    music_play(MOD_WIDE, true);
    frames(20); // row 4 is at frame 29
    CHECK(music_playing());
    frames(20);
    CHECK(!music_playing());
    CHECK(debug_warning_count() - warnings == WARNINGS(1));
    audio_bank_set(NULL);
}

// --- Sampled sound effects --------------------------------------------------------

static void effects_end_and_their_handles_go_stale(void) {
    audio_bank_set(test_bank);
    Sfx beep = sfx_play(SFX_BEEP);
    CHECK(beep != SFX_NONE && sfx_playing(beep));
    frames(4);
    CHECK(sfx_playing(beep) && peak(0) > 8);
    frames(5); // 0.1 s: 6 frames
    CHECK(!sfx_playing(beep));
    frames(1);
    CHECK(peak(0) == 0);
    // A later effect on the same mixer channel: the old handle doesn't reach it.
    Sfx again = sfx_play(SFX_BEEP);
    CHECK(again != SFX_NONE && again != beep && sfx_playing(again));
    sfx_stop(beep);
    CHECK(sfx_playing(again));
    sfx_stop(again);
    CHECK(!sfx_playing(again));
    sfx_stop(SFX_NONE);
    CHECK(!sfx_playing(SFX_NONE));
    // Handles of a bank that was replaced are stale too.
    Sfx hum = sfx_play(SFX_HUM);
    audio_bank_set(test_bank);
    CHECK(!sfx_playing(hum));
    // An ID the bank doesn't have: SFX_NONE, warns.
    u32 warnings = debug_warning_count();
    CHECK(sfx_play(MSL_NSAMPS) == SFX_NONE);
    CHECK(debug_warning_count() - warnings == WARNINGS(1));
    audio_bank_set(NULL);
    CHECK(!sfx_playing(hum) && sfx_play(SFX_HUM) == SFX_NONE);
}

static void effect_pitch_and_its_clamps(void) {
    audio_bank_set(test_bank);
    // hum.wav: 500 Hz, looped, so it plays as long as it's measured.
    Sfx hum = sfx_play(SFX_HUM);
    frames(1);
    CHECK(near(pitch_hz(20), 500, 3));
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 255, 0, FX(2), 0);
    frames(1);
    CHECK(near(pitch_hz(20), 1000, 3));
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 255, 0, FX_ONE / 2, 0);
    frames(1);
    CHECK(near(pitch_hz(20), 250, 3));
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 255, 0, 0, 0); // 0: as recorded
    frames(1);
    CHECK(near(pitch_hz(20), 500, 3));
    sfx_stop(hum);
    // Outside FX_ONE / 16 to FX(16): clamped, warning once.
    u32 warnings = debug_warning_count();
    hum = sfx_play_ex(SFX_HUM, 255, 0, FX_ONE / 64, 0);
    frames(1);
    CHECK(near(pitch_hz(60), 31, 10)); // 500 / 16
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 255, 0, FX(40), 0);
    frames(1);
    CHECK(near(pitch_hz(10), 8000, 3)); // 500 * 16
    sfx_stop(hum);
    CHECK(debug_warning_count() - warnings == WARNINGS(1));
    // A one-shot played an octave down lasts twice as long: 0.2 s, 12 frames.
    Sfx beep = sfx_play_ex(SFX_BEEP, 255, 0, FX_ONE / 2, 0);
    frames(10);
    CHECK(sfx_playing(beep));
    frames(4);
    CHECK(!sfx_playing(beep));
    audio_bank_set(NULL);
}

static void effect_volume_and_pan(void) {
    audio_bank_set(test_bank);
    Sfx hum = sfx_play(SFX_HUM);
    frames(2);
    u32 left = peak(0), right = peak(1);
    CHECK(left > 8 && near(left, right, 10));
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 255, -128, FX_ONE, 0);
    frames(2);
    u32 hard_left = peak(0);
    CHECK(hard_left > left && peak(1) == 0);
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 255, 127, FX_ONE, 0);
    frames(2);
    CHECK(peak(0) == 0 && near(peak(1), hard_left, 5));
    sfx_stop(hum);
    hum = sfx_play_ex(SFX_HUM, 128, 0, FX_ONE, 0);
    frames(2);
    CHECK(near(peak(0) * 2, left, 15));
    sfx_stop(hum);
    // The effects' volume: for those playing too, at once, and kept across
    // banks; the music is unaffected.
    hum = sfx_play(SFX_HUM);
    music_play(MOD_TONE, true);
    music_set_volume(0);
    frames(2);
    CHECK(near(peak(0), left, 5));
    sfx_set_volume(0);
    frames(2);
    CHECK(peak(0) == 0 && sfx_playing(hum));
    sfx_set_volume(128);
    frames(2);
    CHECK(near(peak(0) * 2, left, 15));
    music_set_volume(255);
    audio_bank_set(test_bank);
    hum = sfx_play(SFX_HUM);
    frames(2);
    CHECK(near(peak(0) * 2, left, 15));
    sfx_set_volume(255);
    frames(2);
    CHECK(near(peak(0), left, 5));
    audio_bank_set(NULL);
}

static void loops_play_until_stopped(void) {
    audio_bank_set(test_bank);
    Sfx a = sfx_play(SFX_HUM);
    Sfx b = sfx_play_ex(SFX_HUM, 200, 64, FX(2), 3);
    music_play(MOD_TONE, true);
    frames(300);
    CHECK(sfx_playing(a) && sfx_playing(b));
    sfx_stop(a);
    CHECK(!sfx_playing(a) && sfx_playing(b));
    Sfx c = sfx_play(SFX_HUM);
    sfx_stop_all(); // every effect; the music plays on
    CHECK(!sfx_playing(b) && !sfx_playing(c) && music_playing());
    frames(2);
    CHECK(peak(0) > 8);
    music_stop();
    frames(2);
    CHECK(peak(0) == 0);
    audio_bank_set(NULL);
}

static void priorities_decide_who_plays(void) {
    audio_bank_set(test_bank);
    // 12 mixer channels: 12 looped effects of priority 5 fill them.
    Sfx playing[12];
    for (u32 i = 0; i < 12; i++) {
        playing[i] = sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 5);
        CHECK(playing[i] != SFX_NONE);
    }
    // Lower priority: doesn't play, and stops nothing.
    CHECK(sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 4) == SFX_NONE);
    for (u32 i = 0; i < 12; i++)
        CHECK(sfx_playing(playing[i]));
    // Equal: the oldest of the lowest gives way.
    Sfx equal = sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 5);
    CHECK(equal != SFX_NONE && !sfx_playing(playing[0]) && sfx_playing(playing[1]));
    // Higher, after one of priority 1: that one gives way, whatever its age.
    sfx_stop(playing[5]);
    Sfx low = sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 1);
    Sfx high = sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 200);
    CHECK(low != SFX_NONE && high != SFX_NONE && !sfx_playing(low));
    CHECK(sfx_playing(playing[1]) && sfx_playing(equal));
    // A module's notes take channels too (tone.mod: one); the effects keep
    // what they hold, and the music plays on what's left.
    sfx_stop_all();
    music_play(MOD_TONE, true);
    frames(2);
    for (u32 i = 0; i < 11; i++)
        CHECK(sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 5) != SFX_NONE);
    CHECK(sfx_play_ex(SFX_HUM, 64, 0, FX_ONE, 4) == SFX_NONE);
    CHECK(music_playing());
    audio_bank_set(NULL);
}

// --- Timing -----------------------------------------------------------------------

// Frames that overrun into the next VBlank, and a stretch without frame_end():
// the VBlank handler mixes what frame_end() can't, before it is due, so every
// VBlank gets its samples once and the music keeps time.
static void overruns_keep_the_audio_fed(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, false);
    frames(1);
    ServalMixerStats before = serval_mixer_stats();
    u32 start = cycles();
    for (u32 i = 0; i < 10; i++) { // 10 frames of 1.5 VBlanks
        frame_begin();
        work(FRAME_CYCLES * 3 / 2);
        frame_end();
    }
    work(FRAME_CYCLES * 20); // 20 VBlanks without frame_end()
    frames(1);
    ServalMixerStats after = serval_mixer_stats();
    u32 vblanks = (cycles() - start + FRAME_CYCLES / 2) / FRAME_CYCLES;
    u32 mixes = after.mixes - before.mixes;
    CHECK(mixes + 1 >= vblanks && mixes <= vblanks + 1);
    CHECK(after.early - before.early >= 20 && after.late == before.late);
    CHECK(after.stale == before.stale && after.misplaced == 0);
    CHECK(music_playing());
    // Kept time: it ends 115 frames after it started, overruns or not.
    u32 n = frames_until_silent(400);
    CHECK(n + vblanks + 1 >= 112 && n + vblanks + 1 <= 120);

    // Frames inside effect calls as the VBlank comes: the call the handler
    // interrupted mixes as it returns.
    before = serval_mixer_stats();
    for (u32 i = 0; i < 40 && serval_mixer_stats().deferred == before.deferred; i++) {
        frame_begin();
        while (REG_VCOUNT != 159)
            ;
        work(i * 37); // a different moment in the calls each time
        while (REG_VCOUNT == 159 || REG_VCOUNT == 160)
            sfx_stop(sfx_play_ex(SFX_BEEP, 0, 0, FX_ONE, 0));
        frame_end();
    }
    after = serval_mixer_stats();
    CHECK(after.deferred > before.deferred);
    CHECK(after.stale == before.stale && after.late == before.late);
    CHECK(after.misplaced == 0);
    audio_bank_set(NULL);
}

// The peak of a half of the left side of the wave buffer (0 first, 1 second).
static u32 half_peak(u32 half) {
    const s8* p = serval_mixer_wave() + half * HALF;
    u32 top = 0;
    for (u32 i = 0; i < HALF; i++) {
        u32 a = (u32)(p[i] < 0 ? -p[i] : p[i]);
        top = a > top ? a : top;
    }
    return top;
}

// A frame that ends just as VBlank comes, too late for its wait to catch that
// VBlank (simulated: frame_end()'s flag set during the game's work): the
// handler finds the half starting to play unmixed, and mixes it at once. An
// effect starts in that frame after silence, so a half left stale would be
// silent. Four times, so that both halves take turns.
static void a_half_found_unmixed_is_mixed_late(void) {
    audio_bank_set(test_bank);
    music_play(MOD_TONE, false);
    music_set_volume(0);
    frames(1);
    ServalMixerStats before = serval_mixer_stats();
    u32 start = cycles();
    for (u32 i = 0; i < 4; i++) {
        frame_begin();
        Sfx hum = sfx_play(SFX_HUM);
        serval_frame_waiting = true;
        work(FRAME_CYCLES * 6 / 5);
        serval_frame_waiting = false;
        frame_end();
        CHECK(half_peak(0) > 8 && half_peak(1) > 8);
        sfx_stop(hum);
        frames(3 + i); // silence in both halves again
        CHECK(half_peak(0) == 0 && half_peak(1) == 0);
    }
    ServalMixerStats after = serval_mixer_stats();
    u32 vblanks = (cycles() - start + FRAME_CYCLES / 2) / FRAME_CYCLES;
    u32 mixes = after.mixes - before.mixes;
    CHECK(after.late - before.late == 4 && after.early == before.early);
    CHECK(mixes + 1 >= vblanks && mixes <= vblanks + 1);
    CHECK(after.stale == before.stale && after.misplaced == 0);
    // The music kept time.
    u32 n = frames_until_silent(400);
    CHECK(n + vblanks + 1 >= 113 && n + vblanks + 1 <= 117);
    music_set_volume(255);
    audio_bank_set(NULL);
}

static void frame_cpu_cycles_counts_the_mixer(void) {
    frames(2);
    u32 without = frame_cpu_cycles();
    audio_bank_set(test_bank);
    frames(2);
    u32 idle = frame_cpu_cycles();
    music_play(MOD_TONE, true);
    for (u32 i = 0; i < 4; i++)
        sfx_play(SFX_HUM);
    frames(2);
    u32 busy = frame_cpu_cycles();
    CHECK(idle > without + 2000 && busy > idle + 8000);
    audio_bank_set(NULL);
    frames(2);
    CHECK(frame_cpu_cycles() < without + 200);
}

// The mixer's cycles a frame: idle, a module's one note, then 4, 8 and 12
// channels (looped effects).
static void costs(void) {
    frames(2);
    u32 base = frame_cpu_cycles();
    audio_bank_set(test_bank);
    frames(2);
    u32 idle = frame_cpu_cycles() - base;
    music_play(MOD_TONE, true);
    frames(8);
    u32 one = 0;
    for (u32 i = 0; i < 8; i++) {
        frames(1);
        one = frame_cpu_cycles() - base > one ? frame_cpu_cycles() - base : one;
    }
    music_stop();
    u32 channels[3];
    for (u32 k = 0; k < 3; k++) {
        for (u32 i = 0; i < 4; i++)
            sfx_play_ex(SFX_HUM, 255, (s8)(i * 50 - 100), FX_ONE + (FIXED)(k * 4 + i) * 8, 0);
        frames(2);
        channels[k] = frame_cpu_cycles() - base;
    }
    audio_bank_set(NULL);
    debug_log(text_format("sampled audio: mixer cycles a frame: idle %u, a module's note %u, "
                          "4 channels %u, 8 %u, 12 %u",
                          idle, one, channels[0], channels[1], channels[2]));
    CHECK(idle < channels[0] && channels[0] < channels[1] && channels[1] < channels[2]);
    CHECK(serval_mixer_stats().misplaced == 0 && serval_mixer_stats().stale == 0);
}

TEST_SUITE(gba_sampled_audio_tests, "gba_sampled_audio",
           {"bank claims the hardware and keeps the PSG",
            bank_claims_the_hardware_and_keeps_the_psg},
           {"bad banks are refused", bad_banks_are_refused},
           {"a new bank stops everything", a_new_bank_stops_everything},
           {"music plays once or loops", music_plays_once_or_loops},
           {"music pauses and resumes", music_pauses_and_resumes}, {"music volume", music_volume},
           {"music speed and its clamps", music_speed_and_its_clamps},
           {"a module with too many channels stops", a_module_with_too_many_channels_stops},
           {"effects end and their handles go stale", effects_end_and_their_handles_go_stale},
           {"effect pitch and its clamps", effect_pitch_and_its_clamps},
           {"effect volume and pan", effect_volume_and_pan},
           {"loops play until stopped", loops_play_until_stopped},
           {"priorities decide who plays", priorities_decide_who_plays},
           {"overruns keep the audio fed", overruns_keep_the_audio_fed},
           {"a half found unmixed is mixed late", a_half_found_unmixed_is_mixed_late},
           {"frame_cpu_cycles counts the mixer", frame_cpu_cycles_counts_the_mixer},
           {"costs", costs});
