# Serval Engine

An open-source Game Boy Advance game runtime written in C on top of [libtonc](https://github.com/gbadev-org/libtonc).

Serval Engine is the code linked into every game ROM built with Studio Advance, the commercial editor, but it is designed to be usable on its own by anyone writing GBA homebrew in C.

It is designed as three layers:

1. **Core API**: a flat, raylib-style C API over libtonc (input, sprites, tilemaps and the camera, sound, text, save data).
2. **World**: a fixed-pool, bitmask ECS for entity data.
3. **Game logic** (in progress): GameMaker-style objects and events, run by a compact bytecode VM. The VM runs a whole game (the [`fireflies`](examples/fireflies/main.c) example, hand-assembled); until Studio Advance's script compiler emits its bytecode, games are written in C against the first two layers.

> **Status:** pre-alpha, no release yet. The API will change.

## Features

Implemented:

- Frame loop with CPU-cycle timing; buttons with held-button repeat for menus; 24.8 fixed point, integer helpers, trig with `angle_of` (atan2) and `fx_length`, all without division; deterministic random numbers seeded from the player's input.
- Sprites: resident sprite groups, 12 hardware sizes, flips, layers, rotation and scaling (32 shared matrices per frame; per-frame counts of what the hardware limits drop), metasprites (pieces drawn, rotated and depth-sorted as one, about any pivot), depth sorting (cheap with few depths), any palette of the group per draw, hidden and screen-space sprites; animation with per-frame timing and frame sequences with per-step flips; 128 on screen.
- Tilemaps: one tileset per room, up to three layers of 16x16 metatiles on BG1-BG3, streamed around a camera (any map size); parallax, wrapping, fixed and self-scrolling layers; runtime cell changes; animated tiles.
- ECS: 128 entities with generational handles; engine components for position, velocity, sprite, animation, body, map body and path; movement, physics, map movement, animation, path and render systems; game-defined components and systems; `ecs_count` and `ecs_gather` for cheap per-kind loops.
- Physics: bouncing bodies (gravity in any direction and per body, bounce, friction, maximum fall speed, open edges, wrap-around, contact reports); map bodies that collide with solid and one-way metatiles; rectangle overlap and hit-side tests.
- Paths: movement patterns as data tables (lines, swoops, circles, weaves), mirrored or rotated per entity.
- Sound on the PSG tone generators: sound effects (tones, envelopes, pitch slides, melodies, priorities) and music (a track per channel, loops, tempo changes, pause and resume, volume, sound effects over it).
- Screen fades (hardware brightness) and the backdrop color.
- HUD text (8x8 font) in up to four color styles with a drop shadow, centering, printf-style formatting; a "Made with Serval Engine" splash screen.
- Save data: numbered slots with checksums, version numbers and power-loss-safe writes, on the cartridge's SRAM, Flash (64 or 128 KiB) or EEPROM (8 KiB or 512 bytes), picked per game (`localStorage` in web builds).
- Web builds: any game also builds into one self-contained HTML page (WebAssembly inside) that runs it in a browser on virtual GBA hardware, ready for GitHub Pages or any static host. Keyboard, gamepad and touch input, sound, saves.
- Bytecode VM for GameMaker-style objects and events (Create, Step, Destroy, Collision, Animation End, Room Start): cooperative scripts with waits, entity properties and engine calls, no allocation, hot reload.
- Debug builds report API misuse in the emulator log. Tests run natively and on emulated hardware; a benchmark tracks performance. No C library or `malloc` in the ROM.

Planned (designed in [`docs/`](docs/README.md), not implemented): Maxmod music and sampled sound effects, wave-channel music, streamed and compressed sprites, palette sharing and palette writes, alpha blending, tileset groups and compressed tilesets, slopes and ladders, dialogue text, an example game written in VM bytecode, and the editor debug link.

## Documentation

- [Getting started](docs/getting-started.md): build the examples and write a first game.
- [API reference](docs/api-reference.md): every public function, with units and limits.
- [Documentation index](docs/README.md): design documents, each marked implemented or planned.
- [Development](docs/development.md): tests, source layout, benchmark and release process.

## Examples

| Example | Shows |
| --- | --- |
| [`hello`](examples/hello/main.c) | The smallest game: one sprite moved with the D-pad |
| [`bunnymark`](examples/bunnymark/main.c) | ECS, physics with gravity, depth sorting; doubles as the CPU benchmark |
| [`pong`](examples/pong/main.c) | A complete small game: AI opponent, collisions, effects, sound, splash screen |
| [`asteroids`](examples/asteroids/main.c) | Rotating sprites, wrap-around, many short-lived entities, sound, splash screen, a saved high-score table with initials entry |
| [`breakout`](examples/breakout/main.c) | "Paw Breaker", a brick breaker over four levels: bricks as entities with `body_hit_side`, a constant ball speed, falling power-ups (wide paddle, multi-ball, slow ball, catch, extra life), map layers as backgrounds, fades, music, a saved top-5 table |
| [`platformer`](examples/platformer/main.c) | "Serval Dash", a first level in the classic side-scroller style: scrolling tile maps with parallax, map collision, animated sprites and tiles, blocks hit from below, enemies to stomp, screen fades, music, a goal pole |
| [`shmup`](examples/shmup/main.c) | "Star Veldt", a vertical shoot-'em-up: a stage scrolled by the camera over a parallax starfield, a narrow field with a HUD panel, enemy waves on movement patterns, aimed and spread bullets on an entity budget, power-ups, bombs, a three-phase boss, music, a saved top-5 table |
| [`blackjack`](examples/blackjack/main.c) | Blackjack in a bold, bouncy modern card-game style: cards composed of shared sprite pieces that slide, flip, tilt and wobble, a swirling background, banners, chip and number pops, a swing tune, a bankroll kept in save data |
| [`fireflies`](examples/fireflies/main.c) | A serval catching fireflies at dusk, 60 seconds a round, whose logic is entirely bytecode for the VM: objects with event handlers, waits, spawning, scoring, the timer, the HUD, sound and music in scripts; C only builds the blob, runs the frame loop and reports collisions |

Each example's `main.c` starts by describing what it demonstrates and what you should see and hear. Planned examples, and the engine gaps each would expose, are in [docs/examples-roadmap.md](docs/examples-roadmap.md). `examples/build-all.sh` builds them all into `examples/roms/`, and with Emscripten set up also as web pages into `examples/html/`.

## Building

Requires CMake ≥ 3.25, Ninja, Python 3 and an `arm-none-eabi` GCC ([ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 15.3 is what CI uses). On Linux, `tools/setup-dev.sh` installs all of it, plus mGBA's test runner and Emscripten, at the versions CI uses ([details](docs/development.md#requirements)).

```sh
tools/setup-dev.sh --add-to-shell && . ~/opt/serval-env.sh   # Linux; or install by hand and
                                                             # export ARM_GNU_TOOLCHAIN=/path/to/arm-gnu-toolchain
cmake --preset gba-release
cmake --build --preset gba-release
# -> build/gba-release/examples/hello.gba, bunnymark.gba, pong.gba, asteroids.gba,
#    breakout.gba, platformer.gba, shmup.gba, blackjack.gba, fireflies.gba
```

Open the `.gba` files in mGBA or any GBA emulator. With [Emscripten](https://emscripten.org/) installed, `cmake --preset web-release && cmake --build --preset web-release` builds the same examples as web pages (`build/web-release/examples/*.html`). A game is its own CMake project that adds the engine (a release archive or a checkout) with `add_subdirectory()` and builds its ROM with `serval_add_rom()`; see [docs/getting-started.md](docs/getting-started.md) to make your own game and [docs/development.md](docs/development.md) for tests.

## License

Serval Engine is released under the [MIT License](LICENSE), so it can be linked into any game, including commercial ones. Games must include the engine's copyright notice and libtonc's (and Maxmod's, once audio uses it); see [docs/licensing.md](docs/licensing.md).
