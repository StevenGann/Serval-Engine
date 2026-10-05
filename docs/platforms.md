# Platforms and portability

GBA is the baseline: every Serval game is a GBA game first, and other targets run it with the GBA's limits. The planned order after 1.0 is GB/GBC, then DS. The web is a secondary target that runs any game unchanged.

**Status:** GBA is implemented. The [web target](#web) is implemented at a basic level. Platform-neutral modules (`src/ecs/`, `src/core/`) also build natively for unit tests ([development.md](development.md#tests)), which keeps them free of hardware access.

## Portability rules

These keep future targets possible without a rewrite:

- The [core API](core-api.md) is the abstraction boundary: each call means the same thing on every target.
- VM opcodes are platform-neutral: no hardware addresses, no hard dependency on 32-bit values ([vm.md](vm.md)).
- Assets are stored at source quality by the tooling and converted per target at build time.
- Each target declares a profile of its limits (sprites, palettes, VRAM, resolution).

## Targets

| Target | Runtime base | Emulator | Key challenges |
| --- | --- | --- | --- |
| GBA (1.0) | libtonc + Maxmod | mGBA fork (MPL) | Baseline |
| Web | The GBA backend on virtual GBA hardware, compiled with Emscripten | A browser | Faking the GBA's hardware closely enough ([below](#web)) |
| GB/GBC | GBDK-2020 | mGBA already supports it | The VM on the 8-bit SM83 must be very lean; study GBVM (MIT) |
| DS | BlocksDS (libnds-based) | melonDS or DeSmuME, both GPL | GPL emulator must run as a separate process over the [debug link](debug-link.md). Two screens, touch, 3D |

## Web

**Status:** implemented (basic). Every example runs in a browser.

The web build turns a game into **one self-contained HTML file**: the game, the engine and the WebAssembly are all inside it, so it can be opened from disk or put on any static host (e.g. GitHub Pages) as is. Build it with the `web` preset ([development.md](development.md#web-builds)); `serval_add_rom()` then produces `<target>.html` instead of a ROM. No game code changes.

### How it works

The web build doesn't port the engine's API to the browser. It compiles the GBA backend (`src/gba/`) unchanged for WebAssembly and runs it on **virtual GBA hardware**:

- **Memory map.** WebAssembly memory is made large enough (112 MiB, mostly never touched, so browsers don't back it) that the GBA's I/O registers, palette RAM, VRAM and OAM sit at their real addresses (`0x04000000` to `0x070003FF`). The engine, libtonc's C code and the game read and write them exactly as on the GBA, including through the `gba.h` escape hatch and libtonc itself.
- **Video.** At every VBlank (`frame_end()`), `src/web/ppu.c` draws what the GBA's video hardware would show for that memory: tiled backgrounds, sprites (with rotation and double size), priorities and color effects. The page shows it on a canvas scaled by whole multiples, without smoothing.
- **Sound.** `src/web/apu.c` emulates the PSG channels from the sound registers, one frame's worth of samples per VBlank, played through an AudioWorklet.
- **Frame loop.** A game's main loop never returns, which a browser can't allow. Emscripten's Asyncify lets the VBlank wait hand control back to the browser and resume the game on the next frame, paced at the GBA's 59.73 Hz whatever the display's refresh rate. Hidden tabs pause.
- **The rest of the hardware.** `src/web/platform.c` stands in for the BIOS calls Serval uses (`VBlankIntrWait`, `BitUnPack`), libtonc's assembly routines (fast copies, the font) and the debug output, which goes to the browser console. Debug builds report warnings there too.
- **Input.** Keyboard (arrows; X = A, Z = B, A/S = L/R, Enter = Start, Backspace = Select), gamepads (standard mapping, by position) and, on touch screens, an on-screen pad. KEYINPUT is written once per frame. F or a double-click toggles full screen.

Since browsers only play sound after a click or key press, the page waits for one before starting the game, so the first sounds (the splash jingle) aren't lost.

### What is faked or missing

- **CPU time.** The game runs natively, much faster than on an ARM7TDMI. `frame_cpu_cycles()` reports real time converted to GBA cycles, which says nothing about how the game runs on hardware. **Performance is only measured on the GBA**, and a game that is smooth in a browser can still drop frames on hardware.
- **Random seeds are not faked: they match.** `random_entropy()` hashes `frame_count()` and the button history, never CPU time, so the same input seeds the same game on the GBA and the web (and in every build). A game seeded from the player's timing replays identically from recorded input, which is how the web build is checked against mGBA frame by frame.
- **Interrupts, timers, DMA and serial** are not emulated. Serval doesn't use them (frame timing comes from `VBlankIntrWait`); games that program them directly don't work on the web.
- **Mid-frame changes** are not seen: each frame is drawn from the state at VBlank, so raster effects (scanline-timed writes) are not reproduced, and the boot frame (written while the GBA is drawing it) differs. Sound register writes take effect at the next frame boundary (at most one frame, about 17 ms, late).
- **Video.** `ppu.c` implements all six display modes, regular and affine backgrounds, sprites (regular, affine, double size, 1D/2D mapping, the per-scanline sprite budget), windows and color effects, and matches mGBA pixel for pixel on the examples (fades follow the hardware's 5-bit arithmetic, where mGBA rounds differently). Not implemented: mosaic, the green-swap register; modes 6 and 7 show only the backdrop and sprites.
- **Sound.** `apu.c` emulates all four PSG channels (squares with sweep, envelope and length; the wave channel with both banks; noise), matching mGBA's note timing and pitch. Not emulated: **Direct Sound** (the DMA-fed sample channels that Maxmod music and sampled sound effects will use), so music needs its own web path when it is added. Wave RAM written in one frame is attributed to the playing bank; loading both banks in one frame keeps only the last.
- **libtonc assembly** other than the routines above (other BIOS calls, other TTE fonts) is not available: a game using it fails to link for the web.
- **Saves**: not implemented yet (planned: `localStorage`).

### Testing

`src/web/ppu.c` and `src/web/apu.c` are plain C, unit tested natively by the host tests on arrays standing in for GBA memory. `tools/web-shots.py` runs a page headless in Chrome or Chromium with scripted buttons and saves chosen frames as PNG files; frames are numbered and buttons named as in the capture tool, so they can be compared with an emulator's. CI builds every example for the web and checks that each page is self-contained and boots, runs and draws.
