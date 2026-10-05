# Open questions

Engine-side decisions still to be made. Editor and product questions are tracked in the Studio Advance repository.

- [ ] Confirm the frame loop order ([frame-loop.md](frame-loop.md)).
- [ ] Define the bytecode VM opcode set and encoding ([vm.md](vm.md)).
- [ ] Define the debug link protocol: transport and message format ([debug-link.md](debug-link.md)).
- [x] Choose the engine license: MIT ([licensing.md](licensing.md)).
- [x] Verify licenses: libtonc (MIT), Maxmod (ISC), `mmutil` (BSD-3-Clause) ([licensing.md](licensing.md)).
- [ ] Choose the Maxmod fork: BlocksDS (actively maintained) or devkitPro.
- [x] Write the engine's own crt0, linker script and ROM header (avoids devkitARM's MPL startup code and unlicensed linker script).
- [x] Provide `memcpy`/`memset`/`memmove` in the engine so newlib is never linked.
- [x] Define the sprite asset format and implement `sprite_draw` for resident groups ([sprites.md](sprites.md#api)).
- [ ] Remaining sprite features: streamed sprites, LZ77 groups, metasprites, palette sharing, shadow palette, global/room watermark ([sprites.md](sprites.md#api)).
- [x] Optimize the per-frame path measured by bunnymark: sprite drawing, render, movement and physics systems in IWRAM as ARM code, cartridge wait states, division-free text formatting ([development.md](development.md#benchmark)).
- [ ] Further optimization: `text_format` stays in ROM (2.5 KB; IWRAM is shared with games), depth sorting costs ~10,000 cycles for 128 sprites.
- [x] Background and tilemap API (BG1-BG3, metatiles, streaming, camera, map collision) ([tilemaps.md](tilemaps.md)).
- [x] Animated tiles (`tileset_set_tiles`), sprite animation (`sys_animate`), screen brightness fades, text color and shadow ([tilemaps.md](tilemaps.md), [sprites.md](sprites.md#animation)).
- [ ] Remaining tilemap features: tileset groups, LZ77 tilesets, slopes and ladders, 8bpp layers ([tilemaps.md](tilemaps.md)).
- [x] Save data on SRAM: slot format, checksums, versions, power-loss safety; `localStorage` on the web ([runtime-systems.md](runtime-systems.md#save-data)).
- [x] Flash (64 and 128 KiB) and EEPROM (8 KiB and 512 bytes) save types, picked per game with `serval_add_rom(... SAVE <type>)`; slot count and capacity per type (`save_slot_count()`, `save_slot_capacity()`), tested in mGBA ([runtime-systems.md](runtime-systems.md#save-types)).
- [ ] Testing saves on real cartridges and flash carts, for every save type ([runtime-systems.md](runtime-systems.md#real-cart-testing)).
- [ ] Maxmod music and sampled sound effects ([audio.md](audio.md)); the PSG wave channel is unused.
- [ ] A logo for `serval_splash()` ([core-api.md](core-api.md#splash-screen)).
- [ ] Add the mGBA capture tool to the repository and use it for screenshot tests in CI ([development.md](development.md#checking-what-a-game-shows-and-plays)).
- [ ] First release (`v0.1.0`).
- [ ] Choose the toolchain: devkitARM vs ARM `arm-none-eabi-gcc`.
- [ ] Decide whether raster effects make 1.0 ([tilemaps.md](tilemaps.md#raster-effects)).
- [ ] Decide where the mGBA fork lives (its own repository is likely).
- [x] Choose the build system for the engine library: CMake ([development.md](development.md)).
- [x] Set up the release workflow: build `serval-engine-X.Y.Z.zip` and check that `serval.json` matches the tag ([releases.md](releases.md)).
