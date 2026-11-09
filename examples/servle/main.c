// servle: a five-letter word-guessing game, six tries, whose rules are all a
// script in the Lua subset (servle.lua). (Placeholder: the full description
// of what it demonstrates and what to expect comes with the finished game.)

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black until the script fades in
    art_build();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    psg_table_set(sound_table, SOUND_COUNT);
    tileset_load(&spots_tileset);
    map_load(&spots_far_layer);
    map_load(&spots_near_layer);
    screen_set_backdrop(COLOR_RGB(43, 23, 70)); // plum, behind the spots
    text_set_color(COLOR_RGB(255, 241, 207), COLOR_RGB(23, 11, 40));
    text_set_shadow(true);
    vm_use_saves(); // the script keeps its statistics in save slot 0
    vm_bind(&(VmBindings){.psg_songs = songs, .psg_song_count = SONG_COUNT});
    vm_load(servle_script, servle_script_size);
    vm_start(OBJ_GAME, VM_EV_ROOM_START);

    for (;;) {
        frame_begin();
        vm_step(); // waits, queued events, Step reactions
        sys_movement();
        vm_events();
        sys_render_by_depth();
        frame_end();
    }
}
