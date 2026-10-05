# Serval Engine

An open-source Game Boy Advance game runtime written in C on top of [libtonc](https://github.com/gbadev-org/libtonc).

Serval Engine is the code linked into every game ROM built with Studio Advance, the commercial editor, but it is designed to be usable on its own by anyone writing GBA homebrew in C.

It is built as three layers:

1. **Core API**: a flat, raylib-style C API over libtonc (input, sprites, backgrounds, sound).
2. **World**: a fixed-pool, bitmask ECS for entity data.
3. **Game logic**: GameMaker-style objects and events, run by a compact bytecode VM.

> **Status:** pre-alpha. The build system, startup code, frame loop, input, sprites (resident groups), ECS with movement, physics and (depth-sorted) render systems, HUD text, random numbers, frame timing and PSG sound effects are in place, with tests running natively and on emulated hardware. The design is documented in [`docs/`](docs/README.md).

## Building

Requires CMake ≥ 3.25, Ninja, Python 3 and an `arm-none-eabi` GCC ([ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 15.3 is what CI uses).

```sh
export ARM_GNU_TOOLCHAIN=/path/to/arm-gnu-toolchain   # or put arm-none-eabi-gcc on PATH
cmake --preset gba-release
cmake --build --preset gba-release
# -> build/gba-release/examples/hello.gba
```

See [docs/development.md](docs/development.md) for tests, the source layout and the release process.

## License

Serval Engine is released under the [MIT License](LICENSE), so it can be linked into any game, including commercial ones. Games must include the engine's copyright notice, along with those of libtonc and Maxmod; see [docs/licensing.md](docs/licensing.md).
