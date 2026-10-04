# Open questions

Engine-side decisions still to be made. Editor and product questions are tracked in the Studio Advance repository.

- [ ] Confirm the frame loop order ([frame-loop.md](frame-loop.md)).
- [ ] Define the bytecode VM opcode set and encoding ([vm.md](vm.md)).
- [ ] Define the debug link protocol: transport and message format ([debug-link.md](debug-link.md)).
- [x] Choose the engine license: MIT ([licensing.md](licensing.md)).
- [x] Verify licenses: libtonc (MIT), Maxmod (ISC), `mmutil` (BSD-3-Clause) ([licensing.md](licensing.md)).
- [ ] Choose the Maxmod fork: BlocksDS (actively maintained) or devkitPro.
- [ ] Write the engine's own crt0, linker script and ROM header (avoids devkitARM's MPL startup code and unlicensed linker script).
- [ ] Provide `memcpy`/`memset`/`memmove` in the engine so newlib is never linked.
- [ ] Choose the toolchain: devkitARM vs ARM `arm-none-eabi-gcc`.
- [ ] Decide whether raster effects make 1.0 ([tilemaps.md](tilemaps.md#raster-effects)).
- [ ] Decide where the mGBA fork lives (its own repository is likely).
- [ ] Choose the build system for the engine library.
- [ ] Set up the release workflow: build `serval-engine-X.Y.Z.zip` and check that `serval.json` matches the tag ([releases.md](releases.md)).
