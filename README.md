# Serval Engine

An open-source Game Boy Advance game runtime written in C on top of [libtonc](https://github.com/gbadev-org/libtonc).

Serval Engine is the code linked into every game ROM built with Studio Advance, the commercial editor, but it is designed to be usable on its own by anyone writing GBA homebrew in C.

It is built as three layers:

1. **Core API**: a flat, raylib-style C API over libtonc (input, sprites, backgrounds, sound).
2. **World**: a fixed-pool, bitmask ECS for entity data.
3. **Game logic**: GameMaker-style objects and events, run by a compact bytecode VM.

> **Status:** pre-alpha. The design is documented in [`docs/`](docs/README.md); no code has been written yet.

## License

The engine will be released under a permissive, attribution-light license (MIT or zlib; not yet chosen) so that it can be linked into any game without imposing terms on it. See [docs/licensing.md](docs/licensing.md).
