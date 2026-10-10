// Tests for a script's tracker music and sampled effects on the GBA, with a
// sound bank registered: the threads of tests/rom/sampled_audio_vm.lua,
// compiled by svlua.py and assembled at build time (serval_add_script() in
// tests/CMakeLists.txt, with the bank's generated header, test_bank.h), make
// the VM's SYS calls 17 to 30, which src/gba/vm_platform.c passes to Maxmod.
// What the script's queries answered is in its globals; what played is
// checked from C, and numerically in the samples Maxmod mixed, as in
// tests/rom/sampled_audio_tests.c. Part of the sampled audio test ROM
// (sampled_audio_main.c). The main test ROM runs the same calls without a
// bank (tests/rom/vm_sound_tests.c); tests/vm_tests.c covers the
// interpreter's side.
//
// Every case unloads the script and unregisters the bank at its end.

#include "../test.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/text.h"
#include "serval/vm.h"

#include <tonc.h>

#include "../../src/gba/internal.h"
#include "sampled_audio_vm_script.h"
#include "test_bank.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

#define MIX_RATE 15768 // Maxmod's 16 kHz: 2^24 / 1064
#define HALF 264       // samples a VBlank
#define SIDE 528       // bytes from the left speaker's samples to the right's

// Frames of a scripted game: the VM's two phases between frame_begin() and
// frame_end(), which mixes.
static void frames(u32 n) {
    while (n--) {
        frame_begin();
        vm_step();
        vm_events();
        frame_end();
    }
}

static void begin(void) {
    ecs_reset();
    audio_bank_set(test_bank);
    CHECK(vm_load(sampled_audio_vm_script, sampled_audio_vm_script_size));
}

static void end(void) {
    vm_unload();
    audio_bank_set(NULL);
}

static void run(u16 object) {
    CHECK(vm_start(object, VM_EV_ROOM_START) >= 0);
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

// vm.md "Engine calls": music_play, music_pause, music_resume and music_stop
// from a script play, hold and stop the module as from C, and
// music_playing and music_paused answer the script as they answer C.
static void a_script_plays_pauses_and_stops_music(void) {
    begin();
    u32 warnings = debug_warning_count();
    run(OBJ_MUSIC);
    frame_begin();
    vm_step(); // the thread runs to its first wait: the music is paused
    CHECK(vm_global(G_PLAYING) == 1 && vm_global(G_PAUSED) == 1);
    CHECK(music_playing() && music_paused());
    vm_events();
    frame_end();
    frames(2); // resumed
    CHECK(vm_global(G_RESUMED) == 0 && music_playing() && !music_paused());
    frames(1);
    CHECK(peak(0) > 16); // it plays
    frames(2);           // stopped
    CHECK(vm_global(G_ENDED) == 0 && !music_playing());
    frames(1);
    CHECK(peak(0) == 0);
    CHECK(vm_idle());
    CHECK(debug_warning_count() == warnings);
    end();
}

// vm.md "Engine calls": music_set_volume reaches Maxmod; a volume past 255
// plays at 255 (warns), not at what a C cast would make of it (300 is 44).
static void a_script_sets_the_music_volume(void) {
    begin();
    u32 warnings = debug_warning_count();
    run(OBJ_LOUDER);
    frames(9);
    u32 half = peak(0);
    frames(10); // past the script's music_set_volume(300)
    u32 loud = peak(0);
    CHECK(debug_warning_count() - warnings == WARNINGS(1)); // the VM's: clamped
    music_set_volume(255);
    frames(10);
    CHECK(loud > 16 && peak(0) == loud); // 300 played as 255
    CHECK(near(half * 2, loud, 15));     // and 128 at half volume
    music_set_volume(255);
    end();
}

// vm.md "Engine calls": sfx_play and sfx_play_ex from a script play the
// bank's samples and return their handles, which sfx_playing and sfx_stop
// take back: the hum an octave up (1000 Hz) and panned all to the left;
// the beep, a one-shot, still playing a frame later. An ID or a handle past
// 65535 plays and stops nothing (the ID warns, in C).
static void a_script_plays_and_stops_effects(void) {
    begin();
    u32 warnings = debug_warning_count();
    run(OBJ_EFFECTS);
    frames(2);
    Sfx hum = (Sfx)vm_global(G_HUM), beep = (Sfx)vm_global(G_BEEP);
    CHECK(hum != SFX_NONE && beep != SFX_NONE && hum != beep);
    CHECK(vm_global(G_HUMMING) == 1 && vm_global(G_BEEPING) == 1);
    CHECK(sfx_playing(hum));
    frames(8); // the beep (0.1 s) is over; the hum loops
    CHECK(!sfx_playing(beep) && sfx_playing(hum));
    CHECK(near(pitch_hz(20), 1000, 3));
    CHECK(peak(0) > 16 && peak(1) == 0); // all left
    CHECK(debug_warning_count() == warnings);

    run(OBJ_STRAYS);
    frames(1);
    CHECK(vm_global(G_STRAYS) == SFX_NONE);
    CHECK(debug_warning_count() - warnings == WARNINGS(1)); // sfx_play: no such sample
    CHECK(sfx_playing(hum));                                // handle 65536 + hum: none

    run(OBJ_STOP);
    frames(1);
    CHECK(vm_global(G_STOPPED) == 0 && !sfx_playing(hum));
    frames(1);
    CHECK(peak(0) == 0);
    CHECK(vm_idle());
    end();
}

// CPU cycles, from the cascaded timers serval_init() starts.
static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// What a script's sound loop costs a frame, logged: the jukebox example's
// pads with no button pressed (vm_step and vm_events, the queries and SYS
// calls included), and the frame with the mixer and music playing.
static void costs(void) {
    begin();
    music_play(MOD_TONE, true);
    run(OBJ_PADS);
    frames(4);
    u32 vm = 0, frame = 0, n = 60;
    for (u32 f = 0; f < n; f++) {
        frame_begin();
        u32 t0 = cycles();
        vm_step();
        vm_events();
        vm += cycles() - t0;
        frame_end();
        frame += frame_cpu_cycles();
    }
    u32 ops = vm_ops_this_frame();
    debug_log(text_format("vm sound: the jukebox's pads, idle: %u ops, %u cycles a frame in the "
                          "VM; the frame with the music: %u cycles",
                          ops, vm / n, frame / n));
    CHECK(vm_global(G_STRAYS) == 0); // none playing
    CHECK(ops > 0 && ops < 40);
    end();
}

TEST_SUITE(gba_sampled_audio_vm_tests, "gba_sampled_audio_vm",
           {"a script plays, pauses and stops music", a_script_plays_pauses_and_stops_music},
           {"a script sets the music volume", a_script_sets_the_music_volume},
           {"a script plays and stops effects", a_script_plays_and_stops_effects},
           {"costs", costs});
