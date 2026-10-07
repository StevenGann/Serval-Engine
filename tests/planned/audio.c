// Every planned name in audio.h (SERVAL_PLANNED), used once, each on its own line
// ending "// planned". tools/check-planned.py (CTest planned_api; part of the
// build in web builds) compiles this file and fails unless each marked line
// warns as planned, no other line warns, and every planned name in the public
// headers is used in one of these files. When a name is implemented and its
// SERVAL_PLANNED goes, its line goes too. This file is in no build target: it
// warns on purpose. See docs/development.md#planned-api.

#include "serval/audio.h"

void planned_audio(void);
void planned_audio(void) {
    // The PSG wave channel (B4).
    static const u8 channel = PSG_WAVE; // planned
    static const u32 waves[4] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476};
    psg_waves_set(waves, 1); // planned

    // The sound bank and tracker music (B1, B2).
    audio_bank_set(NULL);           // planned
    music_play(0, true);            // planned
    bool playing = music_playing(); // planned
    music_pause();                  // planned
    bool paused = music_paused();   // planned
    music_resume();                 // planned
    music_set_volume(128);          // planned
    music_set_speed(125);           // planned
    music_stop();                   // planned

    // Sampled sound effects (B3). Sfx and SFX_NONE are not planned themselves.
    Sfx sfx = SFX_NONE;
    sfx = sfx_play(0);                          // planned
    sfx = sfx_play_ex(0, 255, -64, 2 * 256, 1); // planned
    bool sfx_on = sfx_playing(sfx);             // planned
    sfx_stop(sfx);                              // planned
    sfx_set_volume(200);                        // planned
    sfx_stop_all();                             // planned

    (void)channel;
    (void)playing;
    (void)paused;
    (void)sfx_on;
}
