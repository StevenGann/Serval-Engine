# Platforms and portability

GBA is the baseline: every Serval game is a GBA game first, and other targets run it with the GBA's limits. The planned order after 1.0 is GB/GBC, then DS. The web is a secondary target that runs any game unchanged.

**Status:** GBA is implemented. The [web target](#web) is implemented: every example runs in a browser, with sound, saves and input; Direct Sound, interrupts, timers and DMA are not emulated ([known limits](#what-is-faked-or-missing)), and each planned feature that needs them on the GBA has a web path of its own ([below](#planned-features-on-the-web)). GB/GBC and DS are post-1.0 plans. Platform-neutral modules (`src/ecs/`, `src/core/`) also build natively for unit tests ([development.md](development.md#tests)), which keeps them free of hardware access.

## Portability rules

These keep future targets possible without a rewrite:

- The [core API](core-api.md) is the abstraction boundary: each call means the same thing on every target.
- VM opcodes are platform-neutral: no hardware addresses, no hard dependency on 32-bit values ([vm.md](vm.md)).
- Assets are stored at source quality by the tooling and converted per target at build time.
- Each target declares a profile of its limits (sprites, palettes, VRAM, resolution).

## Targets

| Target | Runtime base | Emulator | Key challenges |
| --- | --- | --- | --- |
| GBA (1.0) | libtonc; Maxmod from BlocksDS for tracker music and sampled sound (*planned*, [audio.md](audio.md#maxmod-blocksds)) | mGBA fork (MPL) | Baseline |
| Web | The GBA backend on virtual GBA hardware, compiled with Emscripten | A browser | Faking the GBA's hardware closely enough ([below](#web)) |
| GB/GBC | GBDK-2020 | mGBA already supports it | The VM on the 8-bit SM83 must be very lean; study GBVM (MIT) |
| DS | BlocksDS (libnds-based) | melonDS or DeSmuME, both GPL | GPL emulator must run as a separate process over the [debug link](debug-link.md). Two screens, touch, 3D |

## Web

**Status:** implemented. Every example runs in a browser: graphics (sprites, map layers, text, fades), PSG sound effects and music, keyboard, gamepad and touch input, and saves in `localStorage`. Not emulated: Direct Sound, interrupts, timers, DMA and mid-frame changes ([below](#what-is-faked-or-missing)); the planned features that use them on the GBA get web paths of their own ([below](#planned-features-on-the-web)).

The web build turns a game into **one self-contained HTML file**: the game, the engine and the WebAssembly are all inside it, so it can be opened from disk or put on any static host (e.g. GitHub Pages) as is. Build it with the `web` preset ([development.md](development.md#web-builds)); `serval_add_rom()` then produces `<target>.html` instead of a ROM. No game code changes.

### How it works

The web build doesn't port the engine's API to the browser. It compiles the GBA backend (`src/gba/`) unchanged for WebAssembly and runs it on **virtual GBA hardware**:

- **Memory map.** WebAssembly memory is made large enough (112 MiB, mostly never touched, so browsers don't back it) that the GBA's I/O registers, palette RAM, VRAM and OAM sit at their real addresses (`0x04000000` to `0x070003FF`). The engine, libtonc's C code and the game read and write them exactly as on the GBA, including through the `gba.h` escape hatch and libtonc itself.
- **Video.** At every VBlank (`frame_end()`), `src/web/ppu.c` draws what the GBA's video hardware would show for that memory: tiled backgrounds, sprites (with rotation and double size), priorities and color effects. The page shows it on a canvas scaled by whole multiples, without smoothing.
- **Sound.** `src/web/apu.c` emulates the PSG channels from the sound registers (so sound effects and PSG music play as on the GBA), one frame's worth of samples per VBlank, played through an AudioWorklet.
- **Frame loop.** A game's main loop never returns, which a browser can't allow. Emscripten's Asyncify lets the VBlank wait hand control back to the browser and resume the game on the next frame, paced at the GBA's 59.73 Hz whatever the display's refresh rate. Hidden tabs pause.
- **The rest of the hardware.** `src/web/platform.c` stands in for the BIOS calls Serval uses (`VBlankIntrWait`, `BitUnPack`), libtonc's assembly copy and fill routines, and the debug output, which goes to the browser console (debug builds report warnings there too). The `sys8` font the text layer uses is assembly data in libtonc; `cmake/ServalWeb.cmake` converts it to C at build time.
- **Input.** Keyboard (arrows; X = A, Z = B, A/S = L/R, Enter = Start, Backspace = Select), gamepads (standard mapping, by position) and, on touch screens, an on-screen pad. KEYINPUT is written once per frame. F or a double-click toggles full screen.

- **Saves.** The cartridge's save memory is a buffer (`src/web/save.c`) of the game's save type's size (`serval_add_rom()`'s `SAVE`: 32 KiB for SRAM, 64 or 128 KiB for Flash, 8 KiB or 512 bytes for EEPROM), with the GBA's slot layout, so the game gets the same slots and capacity as on the GBA. It is kept in `localStorage` under `serval-save:<title>:<game code>` (both from `serval_add_rom()`, so give each game its own `GAME_CODE`), as base64: the title as the browser shows it, without spaces at either end and with each run of spaces made one (`"  MY  GAME "` gives `serval-save:MY GAME:MYGM`), and the game code as it is. Any printable ASCII title and game code work, `<`, `&` and quotes included: the page gets them escaped, as JSON in a `<script type="application/json">` element (and the title in `<title>`), and makes the key from that (`src/web/shell.html`, `cmake/ServalWeb.cmake`). Pages skip Emscripten's HTML minifier, which would undo some of these escapes (it saved 3 to 10 KB a page). Pages built by earlier engine commits (before 1.0.0-rc.1) read the title from `document.title` and the game code from a `<meta>` tag, unescaped; for every title and game code that reached them unchanged, the key is the same, so their saves are still found. (They got a title like `A&lt;B` or a game code with a `"` wrong, or broke on a title with `</title>`.) It is loaded the first time the game uses its saves and stored after every `save_write()` and `save_erase()`; pages of games that never save don't touch `localStorage`. The contents are byte for byte an mGBA `.sav` file of that type (Flash sector erases are mimicked, so even the unused bytes match); a stored save shorter than the type's (mGBA keeps an 8 KiB EEPROM's `.sav` at 512 bytes until the game writes past them) is padded with `0xFF`, a longer one (another save type) is ignored with a warning. Writes are instant: none of Flash's or EEPROM's write times are imitated. Without `localStorage` (some privacy modes throw on access) the game runs normally, its saves last until the page closes, and the console gets one `serval:` warning. Storage is per origin: the same page served from another address, or opened from disk, has its own saves.

Since browsers only play sound after a click or key press, the page waits for one before starting the game, so the first sounds (the splash jingle) aren't lost.

### What is faked or missing

- **CPU time.** The game runs natively, much faster than on an ARM7TDMI. `frame_cpu_cycles()` reports real time converted to GBA cycles, which says nothing about how the game runs on hardware. **Performance is only measured on the GBA**, and a game that is smooth in a browser can still drop frames on hardware.
- **Random seeds are not faked: they match.** `random_entropy()` hashes `frame_count()` and the button history, never CPU time, so the same input seeds the same game on the GBA and the web (and in every build). A game seeded from the player's timing replays identically from recorded input, which is how the web build is checked against mGBA frame by frame.
- **Interrupts, timers, DMA and serial** are not emulated, except DMA started by the horizontal blank, which the renderer does between the lines it draws (below). The implemented engine doesn't need the rest on the web: frame timing comes from `VBlankIntrWait`, and the CPU cycle count (timers 2 and 3 on the GBA) from the browser's clock. Games that program them directly don't work on the web. The planned features that will use them on the GBA (Maxmod's timer 0, DMA 1 and 2 and VBlank handler) take another path on the web ([below](#planned-features-on-the-web)); raster effects' VBlank handler isn't needed there, since the renderer starts HBlank DMA from its source registers every frame.
- **Mid-frame changes** are not seen: each frame is drawn from the state at VBlank, so a game's own scanline-timed writes (from HBlank or VCount interrupts) are not reproduced, and the boot frame (written while the GBA is drawing it) differs. **HBlank DMA is**: between the lines it draws, the renderer does the copies of every DMA channel set to start at HBlank, as the hardware does, into its own copy of the I/O registers and palette RAM, from the channel's source and destination registers at the start of each frame (as if restarted in VBlank, as the engine does; writes to other memory are dropped). So the engine's raster effects ([runtime-systems.md](runtime-systems.md#raster-effects)), which copy a table by HBlank DMA 0, look the same on the web, line for line. Sound register writes take effect at the next frame boundary (at most one frame, about 17 ms, late).
- **Video.** `ppu.c` implements all six display modes, regular and affine backgrounds, sprites (regular, affine, double size, 1D/2D mapping, the per-scanline sprite budget), windows and color effects, HBlank DMA between lines, and matches mGBA pixel for pixel on the examples (fades follow the hardware's 5-bit arithmetic, where mGBA rounds differently). Not implemented: mosaic, the green-swap register; modes 6 and 7 show only the backdrop and sprites.
- **Sound.** `apu.c` emulates all four PSG channels (squares with sweep, envelope and length; the wave channel with both banks; noise), matching mGBA's note timing and pitch. Not emulated: **Direct Sound** (the DMA-fed sample channels that tracker music and sampled sound effects will use on the GBA), so those take another path on the web ([below](#planned-features-on-the-web)); PSG music works today. Wave RAM written in one frame is attributed to the playing bank; loading both banks in one frame keeps only the last.
- **libtonc assembly** other than the routines above (other BIOS calls, other TTE fonts) is not available: a game using it fails to link for the web.

### Planned features on the web

The planned API ([api-freeze.md](api-freeze.md#planned-in-1x-declared-now)) compiles for the web as for the GBA: the same stubs, the same warnings. When each is implemented, the web does this:

| Feature | Names | On the web |
| --- | --- | --- |
| Tracker music, sampled sound effects, the sound bank | `music_*()`, `sfx_*()`, `audio_bank_set()` | Silent stubs that warn once, now and after the GBA plays them, until the web has a player of its own: Maxmod's player is C, so it can run with a C mixer writing straight into the page's audio output, from the same bank (BlocksDS's headless backend does this, unreleased so far). No API change ([audio.md](audio.md#web)) |
| The PSG wave channel | `PSG_WAVE`, `psg_waves_set()` | Plays as on the GBA: `apu.c` already emulates the channel with both banks ([audio.md](audio.md#wave-channel)) |
| Alpha blending (implemented) | `screen_set_blend()`, `SPRITE_BLEND` | Drawn as on the GBA: `ppu.c` has the hardware's blending and semi-transparent sprites, and needed nothing new ([runtime-systems.md](runtime-systems.md#alpha-blending)) |
| Raster effects (**implemented**) | `raster_scroll()`, `raster_backdrop()`, `raster_clear()` | Drawn as on the GBA: the engine's code is the same, and the renderer does HBlank DMA 0's copies between the lines it draws, from the per-line table `frame_end()` prepared ([above](#what-is-faked-or-missing), [runtime-systems.md](runtime-systems.md#raster-effects)) |
| LZ77 sprites and tilesets | `SPRITE_ASSET_LZ77`, `TILESET_LZ77` | A C decoder for the BIOS's LZ77 format joins the BIOS stand-ins in `src/web/platform.c` ([sprites.md](sprites.md#lz77-compression), [tilemaps.md](tilemaps.md#tilesets)) |
| Streamed sprite groups (implemented) | `SPRITE_GROUP_STREAMED` | Nothing web-specific: VRAM copies in `frame_end()`, which the web build runs unchanged and needed nothing new for ([frame-loop.md](frame-loop.md#vblank-flush)), as runtime sprite tiles (`sprite_set_tiles()`) and palette writes (`sprite_set_colors()`, `tileset_set_colors()`), both implemented, already are |
| Ladders and floor slopes | `MAP_LADDER`, `MAP_CONTACT_LADDER`, `MAP_SLOPE_*` | Nothing web-specific: map collision is portable code (`src/core/`, `src/ecs/`) |

Implemented since: palette writes (`sprite_set_colors()`, `tileset_set_colors()`), nothing web-specific: palette RAM copies in `frame_end()`, which the web build runs unchanged, and its renderer reads palette RAM as the GBA's does.

Portable code, the same on every target and host-tested: `vm_collide()` (the VM's collision pass), `map_tags_in()`, `color_mix()`, and the ladders and slopes above when they come.

### Testing

`src/web/ppu.c` and `src/web/apu.c` are plain C, unit tested natively by the host tests on arrays standing in for GBA memory. Test mode (`#frames=N...`, used by `tools/web-shots.py`) leaves `localStorage` alone, so runs are deterministic: each starts with blank save memory. `&save=<base64>` (or `&saveurl=<url>`, fetched before the game starts: a 128 KiB save is too long for a URL on a command line) starts from given save memory instead, and the page then appends `save <base64>` (the memory at the end) to its output; `web-shots.py save=FILE` does both (serving the file), so consecutive runs see each other's saves (the file is a `.sav`). `&persist=1` uses `localStorage` like a normal page. `tools/web-shots.py` runs a page headless in Chrome or Chromium with scripted buttons and saves chosen frames as PNG files; frames are numbered and buttons named as in the capture tool, so they can be compared with an emulator's. CI builds every example for the web and checks that each page is self-contained and boots, runs and draws.
