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
- [ ] Optimize the per-frame path measured by bunnymark (`sprite_draw`, `sys_render`, `sys_movement`, `frame_end`'s OAM rebuild): ARM code in IWRAM, fewer EWRAM lookups ([development.md](development.md#benchmark)).
- [ ] Choose the toolchain: devkitARM vs ARM `arm-none-eabi-gcc`.
- [ ] Decide whether raster effects make 1.0 ([tilemaps.md](tilemaps.md#raster-effects)).
- [ ] Decide where the mGBA fork lives (its own repository is likely).
- [x] Choose the build system for the engine library: CMake ([development.md](development.md)).
- [x] Set up the release workflow: build `serval-engine-X.Y.Z.zip` and check that `serval.json` matches the tag ([releases.md](releases.md)).
