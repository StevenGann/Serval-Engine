# Open questions

Engine-side decisions still to be made. Editor and product questions are tracked in the Studio Advance repository.

**Status:** living list: open items are unchecked; checked items record decisions already made (and implemented).

- [ ] Confirm the frame loop order ([frame-loop.md](frame-loop.md)).
- [ ] Define the bytecode VM opcode set and encoding ([vm.md](vm.md)).
- [ ] Define the debug link protocol: transport and message format ([debug-link.md](debug-link.md)).
- [x] Choose the engine license: MIT ([licensing.md](licensing.md)).
- [x] Verify licenses: libtonc (MIT), Maxmod (ISC), `mmutil` (BSD-3-Clause) ([licensing.md](licensing.md)).
- [ ] Choose the Maxmod fork: BlocksDS (actively maintained) or devkitPro.
- [x] Write the engine's own crt0, linker script and ROM header (avoids devkitARM's MPL startup code and unlicensed linker script).
- [x] Provide `memcpy`/`memset`/`memmove` in the engine so newlib is never linked.
- [x] Define the sprite asset format and implement `sprite_draw` for resident groups ([sprites.md](sprites.md#api)).
- [x] Per-draw palettes (`SPRITE_PALETTE`), hidden and screen-space sprites (`SPRITE_HIDDEN`, `SPRITE_SCREEN`), frame sequences with per-step flips (`frame_order`) ([sprites.md](sprites.md#api)).
- [x] Sprite scaling (`sprite_draw_ex`, `SPRITE_SCALED` with `spr_scale`) and sprite statistics (`sprite_stats()`: hardware sprites and matrices used, draws dropped) ([sprites.md](sprites.md#api)).
- [ ] Remaining sprite features: streamed sprites, LZ77 groups, metasprites, runtime tile composition, palette sharing, shadow palette and palette writes, global/room watermark ([sprites.md](sprites.md#api)).
- [x] Optimize the per-frame path measured by bunnymark: sprite drawing, render, movement and physics systems in IWRAM as ARM code, cartridge wait states, division-free text formatting ([development.md](development.md#benchmark)).
- [ ] Further optimization: `text_format` stays in ROM (2.5 KB; IWRAM is shared with games), depth sorting costs ~10,000 cycles for 128 sprites.
- [x] Background and tilemap API (BG1-BG3, metatiles, streaming, camera, map collision) ([tilemaps.md](tilemaps.md)).
- [x] Animated tiles (`tileset_set_tiles`), sprite animation (`sys_animate`), screen brightness fades, text styles, color and shadow ([tilemaps.md](tilemaps.md), [sprites.md](sprites.md#animation)).
- [x] Fixed and self-scrolling map layers (`MAP_LAYER_FIXED`, `map_set_scroll`) ([tilemaps.md](tilemaps.md#fixed-and-self-scrolling-layers)).
- [x] PSG music with pause, resume, tempo changes and sound priorities ([audio.md](audio.md#psg-music)).
- [x] Physics: per-body gravity, contact reports, maximum fall speed, `body_hit_side` ([runtime-systems.md](runtime-systems.md#physics)).
- [x] Cheap per-kind entity loops (`ecs_gather`, `ecs_count`, `ecs_free_count`) ([ecs.md](ecs.md#iterating)).
- [x] Movement paths (`path.h`), `angle_of` and `fx_length` ([runtime-systems.md](runtime-systems.md#paths)).
- [x] Held-button repeat for menus (`button_repeat`).
- [ ] Remaining tilemap features: tileset groups, LZ77 tilesets, slopes and ladders, 8bpp layers ([tilemaps.md](tilemaps.md)).
- [x] Save data on SRAM: slot format, checksums, versions, power-loss safety; `localStorage` on the web ([runtime-systems.md](runtime-systems.md#save-data)).
- [x] Flash (64 and 128 KiB) and EEPROM (8 KiB and 512 bytes) save types, picked per game with `serval_add_rom(... SAVE <type>)`; slot count and capacity per type (`save_slot_count()`, `save_slot_capacity()`), tested in mGBA ([runtime-systems.md](runtime-systems.md#save-types)).
- [ ] Testing saves on real cartridges and flash carts, for every save type ([runtime-systems.md](runtime-systems.md#real-cart-testing)).
- [ ] Maxmod music and sampled sound effects ([audio.md](audio.md)); the PSG wave channel is unused (no wave-channel music).
- [ ] A logo for `serval_splash()` ([core-api.md](core-api.md#splash-screen)).
- [ ] Add the mGBA capture tool to the repository and use it for screenshot tests in CI ([development.md](development.md#checking-what-a-game-shows-and-plays)).
- [ ] First release (`v0.1.0`).
- [x] Choose the toolchain: the ARM GNU Toolchain (`arm-none-eabi-gcc`) 15.3, which CI uses and `serval.json`'s `toolchain.gcc` names as the minimum; devkitARM should also work but isn't tested ([development.md](development.md#requirements)).
- [ ] Decide whether raster effects make 1.0 ([tilemaps.md](tilemaps.md#raster-effects)).
- [ ] Decide where the mGBA fork lives (its own repository is likely).
- [x] Choose the build system for the engine library: CMake ([development.md](development.md)).
- [x] Set up the release workflow: build `serval-engine-X.Y.Z.zip` and check that `serval.json` matches the tag ([releases.md](releases.md)).

## API caveats found by the examples

Not bugs, but surprises a game developer hit while writing the examples (details in [api-reference.md](api-reference.md)); each could be smoothed by an API change. Decide for each whether to change the API or keep the caveat.

- [ ] `PathStep` needs designated initializers: a positional initializer that leaves fields out trips `-Wmissing-field-initializers` ([runtime-systems.md](runtime-systems.md#paths)).
- [ ] `camera_set()` clamps silently: with a playfield smaller than the screen it stays at 0, so moving a small layer with the camera does nothing (Blackjack's swirl; use `map_set_scroll()`) ([runtime-systems.md](runtime-systems.md#camera)).
- [x] `body_max_fall` was whole pixels per frame (`u8`), so a top speed of 1.5 was not possible (Breakout's capsules went from 1.5 to 2). Changed: a `u16` in fixed point (`FX(3) / 2`); the capsules fall at 1.5 again.
- [x] `SPRITE_SCREEN` bodies and world bodies: `body_overlap()` compared positions as they are, so testing a screen-space entity against a world-space one needed the camera added by hand. Changed: `body_overlap()` and `body_hit_side()` add the camera for such pairs (Shmup's shots against turrets).
- [ ] `BODY_CONTACT_EXIT` counts a body touching an open edge from outside as gone, one frame earlier than a game's own `pos < -w` test; use one test or the other, not both.
- [ ] `button_repeat()` repeats a button that is still held when a screen opens (say, from gameplay into initials entry); games check `button_pressed()` first when a screen should wait for a fresh press.
