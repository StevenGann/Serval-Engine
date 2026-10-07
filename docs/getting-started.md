# Getting started

Write a GBA game in C with Serval Engine. This covers building the examples, the shape of a game, and the engine's main pieces. Function details are in [api-reference.md](api-reference.md).

**Status:** describes what is implemented today. This page covers sprites, entities, bouncing physics, PSG sound effects and HUD text; the examples and the [Next](#next) links cover the rest that is implemented (sprite animation, tilemaps and the camera, map collision, PSG music, fades, paths, save data, web builds). Maxmod music is not implemented yet ([the README](../README.md#features) lists what is planned). The scripting VM is, and `fireflies` runs a whole game on it from a listing assembled by the engine's assembler (`tools/svm.py`), but its bytecode is meant to come from Studio Advance's script compiler; games written by hand are written in C, as this page describes.

## 1. Build the examples

Install CMake ≥ 3.25, Ninja, Python 3 and an `arm-none-eabi` GCC (the [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 15.3 is what CI uses; devkitARM should also work). On Linux, `tools/setup-dev.sh` installs everything at the versions CI uses. Details: [development.md](development.md#requirements).

```sh
tools/setup-dev.sh --add-to-shell && . ~/opt/serval-env.sh   # Linux; or install by hand and
                                                             # export ARM_GNU_TOOLCHAIN=/path/to/arm-gnu-toolchain
cmake --preset gba-debug
cmake --build --preset gba-debug
# -> build/gba-debug/examples/hello.gba, bunnymark.gba, pong.gba, asteroids.gba,
#    breakout.gba, platformer.gba, shmup.gba, blackjack.gba, fireflies.gba
```

Open a `.gba` file in [mGBA](https://mgba.io/) (or any GBA emulator, or a flash cart). Each example's `main.c` begins with what it demonstrates and what you should see and hear. Read them in this order:

| Example | Read it for |
| --- | --- |
| [`hello`](../examples/hello/main.c) | The smallest game: a hand-made sprite moved with the D-pad |
| [`bunnymark`](../examples/bunnymark/main.c) | Entities, engine systems, gravity, a game-defined component and system, HUD text |
| [`pong`](../examples/pong/main.c) | A complete game: title screen, states, collisions, wall contacts (`body_contact`), sound, splash screen |
| [`asteroids`](../examples/asteroids/main.c) | Rotation, wrap-around, trigonometry, many short-lived entities, a saved high-score table with initials entry (`save.h`, `button_repeat`, text styles) |
| [`breakout`](../examples/breakout/main.c) | A game split into files: bricks as entities and which side the ball hit (`body_hit_side`), game components and `ecs_count`, directions as angles, power-ups, map layers as a static background, music, save data |
| [`platformer`](../examples/platformer/main.c) | A bigger game split into files: tilesets, metatiles and map layers, a scrolling camera, map bodies and collision, changing the map at runtime, a platformer controller |
| [`shmup`](../examples/shmup/main.c) | A vertical shooter: a stage scrolled by the camera with a wrapping parallax layer, a HUD panel layer, an entity budget with caps, movement patterns from a wave table, aimed bullets, a multi-phase boss, cheap per-frame loops over the entities of each kind |
| [`blackjack`](../examples/blackjack/main.c) | A card game: cards composed of several sprites and rotated as one, a flip made of animation frames, tweens with easing and springs, banners and number pops, art built at boot, a scrolling background without a playfield, a round as a sequence of steps, save data |
| [`fireflies`](../examples/fireflies/main.c) | Scripting: a small game whose logic is all bytecode for the VM ([vm.md](vm.md)), objects with event handlers (Create, Step, Collision, Destroy, Animation End, Room Start) written as a listing (`fireflies.svm`) assembled at build time, and the little C a game made in Studio Advance keeps: the frame loop, collision pairs, a restart |

Use the `gba-debug` preset while developing: debug builds report API misuse in mGBA's log (*Tools > View Logs*) as `serval: ...` warnings ([core-api.md](core-api.md#debug-builds-report-misuse)).

## 2. Create your game

A game is its own CMake project that adds the engine with `add_subdirectory()`. Get the engine as a release archive, `serval-engine-X.Y.Z.zip` from [GitHub Releases](https://github.com/StevenGann/Serval-Engine/releases) (check it against the `.sha256` next to it; until the first release, `tools/package-release.sh <dir>` builds one from a checkout, or use the checkout itself). Extract it into the game's directory and copy `examples/hello/main.c` as a start:

```
my_game/
  CMakeLists.txt
  main.c
  serval-engine/      # the extracted serval-engine-X.Y.Z/
```

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_game LANGUAGES C)

add_subdirectory(serval-engine)
serval_add_rom(my_game SOURCES main.c TITLE "MY GAME" GAME_CODE "MYGM")
```

Configure with the engine's toolchain file, and a Debug build while developing:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/serval-engine/cmake/arm-gba-toolchain.cmake"
cmake --build build
# -> build/my_game.gba
```

`TITLE` (up to 12 characters) and `GAME_CODE` (4 characters) go into the ROM header. If the game saves (`save.h`) and will ship on a cartridge with Flash or EEPROM rather than SRAM, add `SAVE FLASH64K`, `FLASH128K`, `EEPROM8K` or `EEPROM512` to match it (the default, `SRAM`, suits emulators and flash carts; [save types](runtime-systems.md#save-types)). A game can have any number of source files; unused functions are dropped at link time. The engine's tests and examples are not built when it is added this way. [`tests/consumer/`](../tests/consumer/CMakeLists.txt) in the engine repository is a complete, CI-tested example of such a project.

**Scripting.** Games are written in C today. The VM ([vm.md](vm.md)) is implemented, and a game can also carry logic as a script listing assembled at build time: `serval_add_script(my_game scripts.svm HEADERS game.h serval/ecs.h)` next to `serval_add_rom()` runs the engine's assembler (`tools/svm.py`, in the release archive) and gives the game `scripts_script.h` and a blob to `vm_load` ([development.md](development.md#building-a-game)). [`fireflies`](../examples/fireflies/fireflies.svm) is the example: its whole game is one listing, and its `main.c` the C a scripted game keeps. The assembler is a reference for the blob format, not a language; the script compiler that turns event blocks into it belongs to Studio Advance ([vm.md](vm.md#tools)).

To experiment inside the engine's own tree instead, add the same `serval_add_rom()` line to `examples/CMakeLists.txt` (target name = directory name, e.g. `examples/my_game/main.c`).

### Playing it in a browser

The same project builds into one self-contained web page with Emscripten ([development.md](development.md#web-builds)). Use the web toolchain file in a separate build directory:

```sh
export EMSDK=/path/to/emsdk   # set by setup-dev.sh's serval-env.sh; or source emsdk_env.sh
cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/serval-engine/cmake/web-toolchain.cmake"
cmake --build build-web
# -> build-web/my_game.html
```

The page needs nothing else: open it from disk, or publish it on GitHub Pages or any static host. It runs the GBA game on virtual GBA hardware, so it behaves as on the GBA, with the GBA's limits. Test performance on the GBA (or in mGBA), not in the browser, which is far faster. [platforms.md](platforms.md#web) describes what the web build fakes.

## 3. The shape of a game

```c
#include "serval/serval.h"   // the whole API; no other headers needed

int main(void) {
    serval_init();           // once, first
    // load sprites and sounds, set up the world

    for (;;) {
        frame_begin();       // read buttons, start a new sprite list
        // update the game and draw
        frame_end();         // wait for the screen refresh, show this frame
    }
}
```

Everything runs at 60 frames per second, one loop iteration per frame. Sprites are immediate-mode: draw every sprite you want to see on every frame, and anything not drawn disappears. There is no `malloc`: entities, sprites and sounds come from fixed pools and constant tables.

Numbers with fractions use 24.8 fixed point (`FIXED`): `FX(3)` is 3.0, `FX_ONE / 4` is 0.25, `fx_to_int()` converts back, `fx_mul()`/`fx_div()` multiply and divide. The GBA has no floating-point hardware; avoid `float`.

## 4. Sprites

Sprite data is constant C in ROM. Studio Advance generates it from images; by hand it looks like this (`hello` does the same for a square):

```c
enum { SPR_BALL, SPRITE_COUNT };

// One 8x8 tile, 4 bits per pixel, a row per word, low nibble = leftmost pixel.
static const u32 ball_tiles[8] = {
    0x00111100, 0x01111110, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x01111110, 0x00111100,
};
static const u16 ball_palette[16] = {0, COLOR_RGB(255, 255, 255)}; // color 0 is transparent

static const SpriteAsset ball = {.size = SPRITE_8x8, .tiles = ball_tiles};
static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {[SPR_BALL] = &ball};
static const SpriteGroup game_group = {
    .palettes = ball_palette, .sprite_count = SPRITE_COUNT, .palette_count = 1};
```

At startup, register the table and load the group into video memory; then draw by ID each frame:

```c
sprite_table_set(sprite_table, SPRITE_COUNT);
sprite_group_load(&game_group);              // false if VRAM or palettes run out
...
sprite_draw(SPR_BALL, 0, x, y, 0);           // frame 0 at (x, y); flags: SPRITE_FLIP_H, ...
```

Limits: 128 sprites on screen, 1,024 8x8 tiles of sprite graphics, 16 palettes of 15 colors. Rotation, flips, layers and animation frames: [api-reference.md](api-reference.md#spritesh), [sprites.md](sprites.md).

## 5. Entities, physics and sound

For more than a few objects, use entities: slots in fixed arrays (128 at most), each with a mask of components. The engine's systems move, bounce and draw them. This loop adds a bouncing ball each time A is pressed, with a sound:

```c
enum { SND_BOUNCE, SOUND_COUNT };
static const PsgSound bounce = {.frequency = 880, .frames = 6, .fade = -1};
static const PsgSound* const sound_table[SOUND_COUNT] = {[SND_BOUNCE] = &bounce};

int main(void) {
    serval_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&game_group);
    psg_table_set(sound_table, SOUND_COUNT);
    physics_set_gravity(0, FX_ONE / 8);      // 1/8 pixel per frame per frame, downward

    for (;;) {
        frame_begin();

        if (button_pressed(BUTTON_A)) {
            Entity e = entity_create(C_POS | C_VEL | C_SPR | C_BODY);
            if (e != ENTITY_NONE) {          // ENTITY_NONE: all 128 slots in use
                u32 i = entity_index(e);
                pos_x[i] = FX(116);
                pos_y[i] = FX(20);
                vel_x[i] = FX(random_range(-2, 2));
                spr_id[i] = SPR_BALL;
                body_w[i] = body_h[i] = 8;
                body_bounce[i] = 224;        // keeps 7/8 of its speed per bounce
                psg_play(SND_BOUNCE);
            }
        }

        sys_movement();                      // position += velocity
        sys_physics();                       // gravity, bounces off the screen edges
        sys_render();                        // draws every C_POS | C_SPR entity

        frame_end();
    }
}
```

Your own components are bits from `C_GAME(0)` to `C_GAME(14)` with arrays you declare yourself (`static u8 hp[MAX_ENT];`); your own systems loop with `ECS_FOR_EACH(i, C_POS | C_ENEMY) { ... }` (after `#define C_ENEMY C_GAME(0)`). Collisions between two entities are `body_overlap(a, b)`. See [ecs.md](ecs.md) and `bunnymark`.

## 6. Text and debugging

```c
text_print_line(0, 0, text_format("SCORE %5d", score));   // column 0, row 0 of 30x20
text_set_style(TEXT_HIGHLIGHT);                            // yellow until set back
text_print_centered(5, text_format("%.3s %6d", initials, best)); // %.3s: a char[3]
text_set_style(TEXT_NORMAL);
debug_log(text_format("spawned %u", count));              // mGBA's log window
```

`frame_cpu_permille()` shows how much of the frame your game uses; `examples/bunnymark` displays it. Warnings in the log (`serval: sprite_draw: sprite 3 is not loaded; ...`) say what went wrong and how to fix it.

## Next

- [api-reference.md](api-reference.md): every function, its units and limits.
- [core-api.md](core-api.md): hardware the engine uses and reserves, and what games must not touch.
- [sprites.md](sprites.md#animation): sprite animation (`sys_animate`, frame sequences).
- [tilemaps.md](tilemaps.md): tilesets, map layers, the camera, map bodies and map collision.
- [audio.md](audio.md#psg-music): PSG music.
- [runtime-systems.md](runtime-systems.md): save data, fades, paths, physics details.
- [development.md](development.md#memory-use): memory (IWRAM) budgets and the benchmark.
- [licensing.md](licensing.md): the notices a shipped game must include.
