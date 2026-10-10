// Tests for the VM's sound calls on the GBA (src/gba/vm_platform.c): a
// script's SYS calls for tracker music and sampled effects (audio.h's
// music_* and sfx_*, SYS 17 to 30) reach the engine's functions, here with no
// sound bank registered, as in every scripted game that has none: the main
// test ROM links no Maxmod. With a bank, through a compiled script:
// tests/rom/sampled_audio_vm_tests.c, in the sampled audio test ROM. The blob
// is hand-assembled (docs/vm.md "Blob format"); the shared suite
// (tests/vm_tests.c) covers the interpreter's side: the arguments in C's
// ranges, the results, arity and stack room.

#include "../test.h"
#include "serval/audio.h"
#include "serval/vm.h"

#include <tonc.h>

// One object whose Create makes every sound call once, keeping the results
// in globals 1 to 5 and storing the 55 it pushed first in global 0, which is
// still 55 only if every call popped its arguments and pushed only its
// result.
static const u8 sound_caller[] = {
    'S', 'V', 'M', 'B', VM_FORMAT_VERSION, VM_CELL_BYTES, 0, 0, // magic, version, cells, flags
    1, 0, 0, 0, 6, 0, 0, 0, // 1 object, no strings, 6 globals, no arrays
    // 0x10: object 0: mask 0, sprite 0, Create @ 0x30
    0, 0, 0, 0, 0, 0, 0, 0, 0x30, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //
    VM_OP_PUSH8, 55,                                   // 0x30: 55
    VM_OP_PUSH8, 0,                                    //       55 MOD 0
    VM_OP_PUSH8, 1,                                    //       55 MOD 0, loop
    VM_OP_SYS, VM_SYS_MUSIC_PLAY,                      //
    VM_OP_SYS, VM_SYS_MUSIC_PLAYING,                   //       55 playing
    VM_OP_STG, 1,                                      //
    VM_OP_SYS, VM_SYS_MUSIC_PAUSE,                     //
    VM_OP_SYS, VM_SYS_MUSIC_PAUSED,                    //       55 paused
    VM_OP_STG, 2,                                      //
    VM_OP_SYS, VM_SYS_MUSIC_RESUME,                    //
    VM_OP_PUSH16, 200, 0,                              //       55 volume
    VM_OP_SYS, VM_SYS_MUSIC_SET_VOLUME,                //
    VM_OP_PUSH16, 150, 0,                              //       55 percent
    VM_OP_SYS, VM_SYS_MUSIC_SET_SPEED,                 //
    VM_OP_PUSH8, 0,                                    //       55 SFX 0
    VM_OP_SYS, VM_SYS_SFX_PLAY,                        //       55 handle
    VM_OP_STG, 3,                                      //
    VM_OP_PUSH8, 0,                                    //       55 SFX 0
    VM_OP_PUSH16, 200, 0,                              //       55 id volume
    VM_OP_PUSH8, 0xC0,                                 //       55 id volume pan (-64)
    VM_OP_PUSH16, 0x00, 0x02,                          //       55 id volume pan pitch (2.0)
    VM_OP_PUSH8, 3,                                    //       55 id volume pan pitch priority
    VM_OP_SYS, VM_SYS_SFX_PLAY_EX,                     //       55 handle
    VM_OP_STG, 4,                                      //
    VM_OP_LDG, 4,                                      //       55 handle
    VM_OP_SYS, VM_SYS_SFX_PLAYING,                     //       55 playing
    VM_OP_STG, 5,                                      //
    VM_OP_LDG, 3,                                      //       55 handle
    VM_OP_SYS, VM_SYS_SFX_STOP,                        //
    VM_OP_PUSH8, 100,                                  //       55 volume
    VM_OP_SYS, VM_SYS_SFX_SET_VOLUME,                  //
    VM_OP_SYS, VM_SYS_SFX_STOP_ALL,                    //
    VM_OP_SYS, VM_SYS_MUSIC_STOP,                      //
    VM_OP_STG, 0,                                      //
    VM_OP_HALT,                                        //
};

// vm.md "Engine calls": with no sound bank registered, the sound calls play
// nothing, as from C (audio.h: without a bank music_play() and sfx_play()
// are ignored, warning): the queries answer false and the plays SFX_NONE,
// and nothing touches the hardware the mixer claims (Direct Sound, timer 0,
// DMA 1 and 2). The script carries on with its stack as it should be.
static void sound_calls_without_a_bank(void) {
    u16 soundcnt_h = REG_SNDDSCNT, timer0 = REG_TM0CNT;
    u32 dma1 = REG_DMA[1].cnt, dma2 = REG_DMA[2].cnt;
    CHECK(vm_load(sound_caller, sizeof sound_caller));
    for (u16 g = 1; g <= 5; g++)
        vm_set_global(g, 99); // each must be written
    CHECK(vm_start(0, VM_EV_CREATE) >= 0);
    vm_step();
    CHECK(vm_idle());
    CHECK(vm_global(0) == 55);
    CHECK(vm_global(1) == 0 && vm_global(2) == 0); // not playing, not paused
    CHECK(vm_global(3) == SFX_NONE && vm_global(4) == SFX_NONE);
    CHECK(vm_global(5) == 0);
    CHECK(!music_playing() && !music_paused());
    CHECK(REG_SNDDSCNT == soundcnt_h && REG_TM0CNT == timer0);
    CHECK(REG_DMA[1].cnt == dma1 && REG_DMA[2].cnt == dma2);
    vm_unload();
}

TEST_SUITE(gba_vm_sound_tests, "gba_vm_sound",
           {"sound_calls_without_a_bank", sound_calls_without_a_bank});
