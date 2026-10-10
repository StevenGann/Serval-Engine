// A ROM that calls every tracker music and sampled effect function but never
// registers a sound bank, as a scripted game's SYS calls do (the VM reaches
// them all): it must link none of Maxmod, which only audio_bank_set() brings
// in (tests/rom/check-no-maxmod.cmake reads its link map). Run, it plays
// nothing and passes: no bank, so nothing plays.

#include "serval/audio.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/fixed.h"

int main(void) {
    serval_init();
    music_play(0, true);
    music_set_volume(128);
    music_set_speed(150);
    music_pause();
    music_resume();
    bool on = music_playing() || music_paused();
    music_stop();
    Sfx sfx = sfx_play(0);
    sfx = sfx_play_ex(1, 200, -64, FX(2), 3);
    on = on || sfx_playing(sfx) || sfx != SFX_NONE;
    sfx_stop(sfx);
    sfx_set_volume(100);
    sfx_stop_all();
    frame_begin();
    frame_end();
    debug_exit(on ? 1 : 0);
}
