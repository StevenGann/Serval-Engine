# Serval Engine

An open-source Game Boy Advance game runtime written in C on top of [libtonc](https://github.com/gbadev-org/libtonc).

Serval Engine is the code linked into every game ROM built with Studio Advance, the commercial editor, but it is designed to be usable on its own by anyone writing GBA homebrew in C.

It is designed as three layers:

1. **Core API**: a flat, raylib-style C API over libtonc (input, sprites, sound; backgrounds planned).
2. **World**: a fixed-pool, bitmask ECS for entity data.
3. **Game logic** (planned): GameMaker-style objects and events, run by a compact bytecode VM. Until then, games are written in C against the first two layers.

> **Status:** pre-alpha, no release yet. The API will change.

## Features

Implemented:

- Frame loop with CPU-cycle timing, button input, 24.8 fixed point, integer and trig helpers, deterministic random numbers.
- Sprites: resident sprite groups, 12 hardware sizes, animation frames, flips, layers, rotation (32 shared matrices per frame), depth sorting; 128 on screen.
- ECS: 128 entities with generational handles; engine components for position, velocity, sprite and body; movement and render systems; game-defined components and systems.
- Physics for bouncing bodies: gravity in any direction, bounce and friction, open edges, wrap-around, rectangle overlap tests.
- PSG sound effects on the tone generators: tones, envelopes, pitch slides, short melodies.
- HUD and debug text (8x8 font) with printf-style formatting, and a "Made with Serval Engine" splash screen.
- Save data: numbered slots with checksums, version numbers and power-loss-safe writes, on the cartridge's SRAM, Flash (64 or 128 KiB) or EEPROM (8 KiB or 512 bytes), picked per game (`localStorage` in web builds).
- Web builds: any game also builds into one self-contained HTML page (WebAssembly inside) that runs it in a browser on virtual GBA hardware, ready for GitHub Pages or any static host. Keyboard, gamepad and touch input, sound.
- Debug builds report API misuse in the emulator log. Tests run natively and on emulated hardware; a benchmark tracks performance. No C library or `malloc` in the ROM.

Planned (designed in [`docs/`](docs/README.md), not implemented): tiled backgrounds and scrolling tilemaps, Maxmod music and sampled sound effects, streamed and compressed sprites, palette sharing and fades, dialogue text, the bytecode VM, and the editor debug link.

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

Each example's `main.c` starts by describing what it demonstrates and what you should see and hear. Planned examples, and the engine gaps each would expose, are in [docs/examples-roadmap.md](docs/examples-roadmap.md). `examples/build-all.sh` builds them all into `examples/roms/`, and with Emscripten set up also as web pages into `examples/html/`.

## Building

Requires CMake ≥ 3.25, Ninja, Python 3 and an `arm-none-eabi` GCC ([ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 15.3 is what CI uses).

```sh
export ARM_GNU_TOOLCHAIN=/path/to/arm-gnu-toolchain   # or put arm-none-eabi-gcc on PATH
cmake --preset gba-release
cmake --build --preset gba-release
# -> build/gba-release/examples/hello.gba, bunnymark.gba, pong.gba, asteroids.gba, platformer.gba
```

Open the `.gba` files in mGBA or any GBA emulator. With [Emscripten](https://emscripten.org/) installed, `cmake --preset web-release && cmake --build --preset web-release` builds the same examples as web pages (`build/web-release/examples/*.html`). A game is its own CMake project that adds the engine (a release archive or a checkout) with `add_subdirectory()` and builds its ROM with `serval_add_rom()`; see [docs/getting-started.md](docs/getting-started.md) to make your own game and [docs/development.md](docs/development.md) for tests.

## License

Serval Engine is released under the [MIT License](LICENSE), so it can be linked into any game, including commercial ones. Games must include the engine's copyright notice and libtonc's (and Maxmod's, once audio uses it); see [docs/licensing.md](docs/licensing.md).
