// fireflies: dusk in a meadow, and a serval catching fireflies before the
// light is gone. A round lasts 60 seconds. The game's logic is entirely
// bytecode for Serval Engine's VM: the proof example of docs/vm.md.
//
// Demonstrates:
//   - The bytecode VM (vm.h) running a whole game. The split between the two
//     halves is the point of the example:
//       * Scripts (fireflies.svm) decide everything that happens: they spawn the
//         serval and the fireflies, walk the serval with the D-pad, fly the
//         fireflies, count the score, run the 60-second timer, draw the HUD,
//         play every sound and the music, fade the screen, end the round
//         and ask for the next one. Objects with event handlers, GameMaker
//         style: a Room and a Spawner (threads with no entity, Room Start),
//         the Player (Create, Step), the Firefly (Create, Collision,
//         Destroy), the Sparkle (Create, Animation End) and the resting
//         serval (Create). They use waits (WAIT, WAIT_MOVE, WAIT_ANIM),
//         SPAWN and KILL, SELF and OTHER, entity properties (GETP/SETP,
//         including the body size), a subroutine shared by two handlers
//         (CALL/RET: print the score), globals, locals (ENTER), loops,
//         behaviours and reactions (a firefly's Create is its behaviour,
//         waiting most of the time; a catch is a Collision reaction that runs
//         on top of it) and the SYS engine calls (sound, music, text and
//         numbers, random numbers, buttons, brightness, paths).
//       * C (this file) does only what Studio Advance generates as C for a
//         game made in its editor: main(), the art, sound and path tables
//         (art.c, sound.c), vm_load of the blob, vm_bind, the frame loop in
//         vm.h's order, the collision pairs (the player's body against each
//         firefly's with body_overlap, reported to the firefly as a
//         Collision event) and a full restart when the scripts set
//         G_RESTART. No game rule is in C.
//   - The blob assembled at build time from the listing by the engine's
//     assembler (tools/svm.py, run by serval_add_script in
//     examples/CMakeLists.txt) into fireflies_script, ROM data, with
//     fireflies_script.h giving C the object, string and global numbers. A
//     real project's blob is ROM data emitted by Studio Advance's script
//     compiler in the same format (docs/vm.md).
//   - Entities with bodies kept on screen by the physics bounds, paths
//     (path.h) mirrored at random, animated sprites that loop and play once
//     (sys_animate, SPRITE_ASSET_ANIM_ONCE), depth sorting, two map layers
//     (sky, treeline and meadow behind; tall grass in front), music and
//     sound effects with priorities, screen fades.
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, a moment of black, then the meadow fades in:
//     a dark night sky with stars and a crescent moon over a violet glow and
//     a black treeline, the deep blue-violet meadow with tufts of grass and
//     pale flowers, tall grass along the bottom. A golden spotted serval
//     stands in the middle; "CATCH THE FIREFLIES!" for three seconds. The
//     HUD: "SCORE 0" top left, "TIME 60" top right, counting down each
//     second. A rising four-note chime over the start of a gentle tune (A
//     minor into C major, with crickets chirping), which loops.
//   - The D-pad walks the serval (1.5 pixels per frame, diagonals too),
//     turning to face left or right; it stands and blinks when you let go.
//     It stays on the meadow, below the treeline; at the bottom its legs
//     vanish into the tall grass.
//   - Fireflies appear one by one (every 40-90 frames, up to 8 at once,
//     never right on the serval), blinking yellow-green (mostly a dim ember, then a bright flash),
//     each out of step with the others. They drift, loop and zigzag slowly, hovering a moment
//     between flights, and bounce off the screen's edges. After about 6-10 seconds each one fades:
//     a last flash, a ring, scattered dots, gone.
//   - Touch one with the serval to catch it: a two-note chime, a white and
//     gold starburst where it was, the score goes up, and the serval turns
//     to face it. Every 10th catch plays a five-note jingle, and fireflies
//     come a little faster (four times, down to every 20-42 frames).
//   - The last ten seconds tick. At 0: the music stops, a falling phrase
//     plays, the serval sits down where it stood (fireflies touching it now
//     are not caught), the fireflies fade away, and "TIME UP!" and "CAUGHT"
//     with the score appear, then "PRESS START". START fades to black and a
//     new round begins from the start: score 0, time 60.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, START = Enter.)
//
// Uses only Serval Engine's API; no third-party headers. The files:
// fireflies.svm (all the game's logic, as a listing), art.c (graphics and
// flight paths, built at boot), sound.c, game.h (the numbers both sides
// share, and the generated fireflies_script.h).

#include "game.h"

// The Collision handler in the listing ORs a comparison (0 or 1) into the
// serval's flags to face it left or right.
_Static_assert(SPRITE_FLIP_H == 1, "fireflies.svm relies on SPRITE_FLIP_H being 1");

// A new round, from scratch: no entities, the blob loaded afresh (zeroed
// globals, no scripts running), and the Room and Spawner threads started.
// Their Room Start handlers run in the next vm_step().
static void start_round(void) {
    ecs_reset();
    vm_load(fireflies_script, fireflies_script_size);
    vm_start(OBJ_ROOM, VM_EV_ROOM_START);
    vm_start(OBJ_SPAWNER, VM_EV_ROOM_START);
}

// The collision pairs: the player against every firefly. A touching firefly
// gets a Collision event with the player as OTHER; its handler decides what
// that means.
static void report_catches(void) {
    static u8 players[MAX_ENT], fireflies[MAX_ENT];
    u32 player_count = ecs_gather(C_PLAYER, players);
    u32 firefly_count = ecs_gather(C_FIREFLY, fireflies);
    for (u32 p = 0; p < player_count; p++)
        for (u32 f = 0; f < firefly_count; f++)
            if (body_overlap(players[p], fireflies[f]))
                vm_event(entity_at(fireflies[f]), entity_at(players[p]), VM_EV_COLLISION);
}

int main(void) {
    serval_init();
    serval_splash();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black until the Room fades in
    art_build();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    psg_table_set(sound_table, SOUND_COUNT);
    tileset_load(&meadow_tileset);
    map_load(&meadow_layer);
    map_load(&grass_layer);
    screen_set_backdrop(COLOR_RGB(48, 38, 100)); // the meadow at dusk
    text_set_color(COLOR_RGB(255, 246, 200), COLOR_RGB(10, 8, 30));
    text_set_shadow(true);
    physics_set_bounds(0, FIELD_TOP, SCREEN_W, SCREEN_H);
    vm_bind(&(VmBindings){
        .songs = songs, .song_count = SONG_COUNT, .paths = paths, .path_count = PATH_COUNT});
    start_round();

    for (;;) {
        frame_begin();
        vm_step(); // waits, queued events (Creates), Step reactions
        sys_path();
        sys_movement();
        sys_physics(); // keeps bodies inside the bounds
        report_catches();
        vm_events(); // the Collision handlers, this frame
        sys_animate();
        sys_render_by_depth();
        frame_end();
        if (vm_global(G_RESTART)) // the Room asks for a new round
            start_round();
    }
}
