# Other runtime systems

These systems are scoped; each section's **Status** says what is implemented. Implemented: save data, HUD text, brightness fades, color mixing, math, paths, bouncing-body physics, pairwise entity collision and the camera. Designed and declared as *planned* API (in the headers, warned about at compile time, implemented in a later 1.x version; [releases.md](releases.md#planned-api)): alpha blending and raster effects. Planned and not designed in detail yet: dialogue text, a collision broad phase and events, camera following. After 1.0, with no API yet: windows and mosaic.

## Save data

**Status:** implemented for SRAM, Flash (64 and 128 KiB) and EEPROM (8 KiB and 512 bytes) (`include/serval/save.h`), each tested in mGBA; web builds keep saves in `localStorage` ([platforms.md](platforms.md#web)). Not yet verified on real cartridges or flash carts (see [Real-cart testing](#real-cart-testing)).

### Save types

A GBA cartridge has one kind of save memory, and emulators and flash carts find out which from an ID string in the ROM. Each game picks its type at build time with `serval_add_rom(<target> ... SAVE <type>)`; Serval links that type's code and ID string only (and none of it, ID string included, if the game never calls `save_*`):

| `SAVE` | Memory | ID string | Slots × capacity | Choose it when |
| --- | --- | --- | --- | --- |
| `SRAM` (default) | 32 KiB battery-backed SRAM | `SRAM_V113` | 8 × 2000 bytes | Almost always: simplest, fastest, supported by every flash cart and emulator |
| `FLASH64K` | 64 KiB Flash (no battery needed) | `FLASH512_V131` | 8 × 2000 bytes | The cartridge (e.g. a reproduction board) has a 64 KiB Flash chip |
| `FLASH128K` | 128 KiB Flash, two banks | `FLASH1M_V103` | 8 × 2000 bytes | The cartridge has a 128 KiB Flash chip |
| `EEPROM8K` | 8 KiB serial EEPROM | `EEPROM_V124` | 8 × 496 bytes | The cartridge has an 8 KiB EEPROM |
| `EEPROM512` | 512-byte serial EEPROM | `EEPROM_V124` | 2 × 112 bytes | The cartridge has a 512-byte EEPROM |

Pick the type the cartridge the game ships on has; for emulators and flash carts, `SRAM` is the safe choice. SRAM and Flash games get the same slots, so switching between them needs no code change. `save_slot_count()` and `save_slot_capacity()` return the game's; `SAVE_SLOTS` (8) and `SAVE_SLOT_MAX` (2000) stay the largest of any type, fine for sizing arrays. A slot or size beyond the game's type fails (`save_write()` returns false) with a warning naming the type. The web build gives the game the same slots and capacity as on the GBA, so games behave identically.

### API

`save_write(slot, &data, sizeof data, version)`, `save_read(slot, &data, sizeof data, version)`, `save_slot_version()`, `save_slot_size()`, `save_erase()`, `save_slot_count()`, `save_slot_capacity()`. `save_read()` returns `SAVE_OK`, `SAVE_EMPTY`, `SAVE_CORRUPT` or `SAVE_OTHER_VERSION`, and touches the game's struct only for `SAVE_OK`. The version is the game's: raise it when the saved struct changes, and read old saves with the old struct (`save_slot_version()` says which) to convert them. See [api-reference.md](api-reference.md#saveh).

### Format

The portable code (`src/core/save.c`, host-tested) works on a byte-addressed save memory supplied per type and platform (`src/core/save_internal.h`): on the GBA `src/gba/save_sram.c`, `save_flash.c` (both Flash sizes) or `save_eeprom.c` (both EEPROM sizes), each compiled once per type into an object `serval_add_rom()` links (`serval_save_<type>`); on the web a buffer of the type's size kept in `localStorage` (`src/web/save.c`). The memory tells the portable code its layout, whether it must be erased before it is written (Flash: 4 KiB sectors) and its write unit (EEPROM: 8-byte blocks). The layout is the same on every platform, so a web page's save memory is byte for byte an mGBA `.sav` file of its type, and vice versa (checked for every type: the same boot from the same `.sav` gives identical files in mGBA and on the web, and each continues the other's saves).

Each slot is two **copies**, A and B (slot *s*: copies 2*s* and 2*s*+1, copy *c* at `c * copy size`):

| Type | Copy size | Layout |
| --- | --- | --- |
| SRAM | 2048 | 16 copies fill the 32 KiB |
| FLASH64K | 4096 | each copy owns one 4 KiB sector; 16 copies fill the 64 KiB |
| FLASH128K | 8192 | each copy owns two sectors (only the first is written); slots 0-3 in bank 0, 4-7 in bank 1 |
| EEPROM8K | 512 | 64 blocks per copy; 16 copies fill the 8 KiB |
| EEPROM512 | 128 | 16 blocks per copy; 4 copies fill the 512 bytes |

Since every copy owns whole sectors, writing one never erases a sector that holds the other copy or another slot. A copy is a 16-byte header, then the data. Header, little-endian:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `SVS1`: this copy holds a save |
| 4 | 4 | Sequence number: the newer copy has the higher one (compared modulo 2^32, so wrapping is harmless) |
| 8 | 2 | The game's save version |
| 10 | 2 | Data size in bytes, 1 to the slot capacity |
| 12 | 4 | CRC-32 (IEEE, as in zip and PNG) of bytes 4-11 and the data |

A copy is **empty** without the magic (never-written memory reads `0xFF`, garbage rarely matches 4 bytes, erasing zeroes the magic), **valid** if the size is in range and the CRC matches, and **damaged** otherwise. A slot's save is its newest valid copy. `save_read()` returns:

- `SAVE_OK` or `SAVE_OTHER_VERSION` if a copy is valid (the newest valid one decides, even if a newer copy is damaged: the slot falls back to the previous save);
- `SAVE_CORRUPT` only if no copy is valid and one is damaged: a header claims a save whose checksum fails (bit rot, a dying battery, another program writing the memory);
- `SAVE_EMPTY` otherwise: blank or never-written memory is empty, not corrupt.

### Guarantees

- **Power loss during `save_write()`** leaves the slot's previous save (or an empty slot) readable, never a corrupt or mixed one. The write goes to the copy that does *not* hold the newest valid save: it clears that copy's magic (Flash: programs it to zeros, which needs no erase; EEPROM: rewrites the block holding it), erases the copy's sector (Flash), writes the data, then the rest of the header, and the magic last (EEPROM: the block with the version, size and CRC, then the block with the magic and sequence number). Until the last write the copy reads as empty; with it, the copy is complete and, with the next sequence number, the newest. Every write is then read back and compared; if anything differs or the memory reports a failure (no save memory of this type, a Flash or EEPROM timeout), the copy's magic is cleared again, `save_write()` returns false and the previous save stays.
- **Power loss during `save_erase()`** leaves the slot either as it was or empty: the older copy's magic is cleared first, then the newer one's. On Flash this programs zeros, so erasing a slot never erases a sector.
- **Damage** to a save is detected by the CRC-32 (every 1- to 3-bit error, and burst errors up to 32 bits) and, if the other copy holds an intact older save, reading falls back to it. A Flash sector erase or EEPROM block write cut short by power loss leaves bytes in between states: the CRC catches them, and the other copy still holds the previous save.
- These are tested on the host for every type's layout (`tests/save_tests.c`) with simulated memories that lose power after every possible step (a byte, an 8-byte EEPROM block, a quarter of a Flash sector erase), flip bits and ignore writes; the Flash one also fails the test if a byte is programmed without being erased first (programming can only clear bits) or an erase reaches outside the copy being written, and the EEPROM one if a write isn't whole blocks. On mGBA's real save memories, `tests/rom/save_tests.c` runs in `serval_tests` (SRAM) and `serval_tests_<type>` for the other four: both copies of a slot where the layout puts them, every slot full at once (on 128 KiB Flash slots 4-7 are in bank 1, on 8 KiB EEPROM past the first 512 bytes, so a memory that ignored the bank or the address bits would mix them up), exactly one ID string in the ROM, and costs. Their ctests also check that mGBA detected the expected type and logged no malformed Flash or EEPROM access.

### Costs

Measured in mGBA (`tests/rom/save_tests.c` logs them), release build, a full slot and 100 bytes:

| Type | Data | `save_write` | `save_write`, slot empty | `save_read` | `save_erase` |
| --- | --- | --- | --- | --- | --- |
| SRAM | 2000 bytes | 362,000 cycles (21.6 ms, 1.3 frames) | 232,000 (13.8 ms) | 188,000 (11.2 ms) | 3,200 (0.2 ms) |
| SRAM | 100 bytes | 23,000 (1.4 ms) | 15,900 (0.9 ms) | 11,900 (0.7 ms) | 3,200 (0.2 ms) |
| FLASH64K | 2000 bytes | 2,180,000 (130 ms, 7.8 frames) | 2,030,000 (121 ms) | 225,000 (13.4 ms) | 8,200 (0.5 ms) |
| FLASH64K | 100 bytes | 288,000 (17.2 ms) | 281,000 (16.7 ms) | 14,300 (0.9 ms) | 8,200 (0.5 ms) |
| FLASH128K | 2000 bytes | 2,210,000 (132 ms) | 2,060,000 (123 ms) | 226,000 (13.4 ms) | 8,500 (0.5 ms) |
| EEPROM8K | 496 bytes | 8,190,000 (488 ms, 29 frames) | 7,980,000 (476 ms) | 415,000 (24.7 ms) | 269,000 (16 ms) |
| EEPROM8K | 100 bytes | 1,930,000 (115 ms) | 1,880,000 (112 ms) | 98,000 (5.8 ms) | 269,000 (16 ms) |
| EEPROM512 | 112 bytes | 2,040,000 (122 ms) | 2,000,000 (119 ms) | 97,000 (5.8 ms) | 266,000 (16 ms) |

- **SRAM** is on an 8-bit bus with 8 wait states (`WAITCNT`, set by `serval_init()`), read and written a byte at a time: per byte, a write costs about 180 cycles (checking the slot's current save, the new CRC, writing, reading back) and a read about 94.
- **Flash** reads like SRAM, but every byte is programmed with a command and waited on, and each write erases a 4 KiB sector first and checks it reads blank. mGBA charges 650 cycles (39 us) per byte and 30,000 (1.8 ms) per sector erase. Real chips' times are still to be measured (GBATEK gives about 20 us per byte for SST, and timeouts of 10 ms per byte and 40 ms to 2 s per sector erase): expect real-cart writes to take about as long, or longer if a chip's sector erase is slow. Bytes of `0xFF` cost nothing (erased bytes already hold it). The first save call of a boot also reads the chip's ID: 40 ms, once (the waits Atmel's datasheet asks for). `save_erase()` only programs zeros over two magics.
- **EEPROM** moves one bit per DMA halfword and each 8-byte block write takes about 6.5 ms on the chip (mGBA: 115,000 cycles, 6.9 ms), so writes cost about 7 ms per block of 8 bytes. Blocks that already hold what is written are skipped (a block read is about 0.1 ms), which also spares the chip's write endurance (100,000 writes per block). `save_erase()` rewrites two blocks (16 ms).

Call `save_write()` at natural pauses (game over, a menu), not every frame, and with Flash or EEPROM expect a visible pause for large saves: show "saving..." first. Everything runs from ROM (Thumb) except the Flash routines that read the chip, which GBATEK says must run from WRAM: about 380 bytes of code in EWRAM, plus 8 bytes of state, in ROMs using Flash only. ROM cost over SRAM's backend: about 1 KiB for Flash, 0.8 KiB for EEPROM. No IWRAM beyond a 4-byte pointer, no timer and no interrupt: timeouts count scanlines (`VCOUNT`). Games that never call `save_*` link none of it (and get no ID string).

### Hardware notes

Written from GBATEK ("GBA Cart Backup Flash ROM", "GBA Cart Backup EEPROM"):

- **Flash** (`src/gba/save_flash.c`): commands are byte writes of `0xAA` to `0x0E005555` and `0x55` to `0x0E002AAA`, then the command: `0x90`/`0xF0` enter/leave ID mode, `0xA0` program a byte, `0x80` then `0x30` (to the sector) erase a 4 KiB sector, `0xB0` then the bank number (to `0x0E000000`) select a bank. A program or erase is done when the byte reads back as written (`0xFF` for an erase), or fails after a timeout: 10 ms per byte, per chip for an erase (SST 40 ms, Panasonic 500 ms, Macronix and unknown chips 2 s); Macronix chips then get `0xF0` to end the command, and an erase is retried up to 3 times. Known IDs: SST `D4BF` (also Sanyo's 64 KiB chip), Macronix `1CC2`, Panasonic `1B32`, Sanyo `1362` and Macronix `09C2` (128 KiB). Atmel `3D1F` writes 128-byte pages instead of bytes and isn't supported: saves fail with a warning. A 64 KiB chip in a `FLASH128K` game also fails with a warning (bank 1 would alias bank 0). Unknown IDs are used like the known ones, with a warning in debug builds. Leaving ID mode is written twice on Sanyo chips, which need it.
- **mGBA** (0.10) has no ID-string scan: it detects Flash by the first command, as a 64 KiB Panasonic chip, and becomes a 128 KiB Sanyo chip when bank 1 is selected. `FLASH128K` games therefore select bank 1 and then bank 0 before reading the ID (real 64 KiB chips ignore it). mGBA logs the second "leave ID mode" write on its Sanyo chip as a bad Flash write and ignores it.
- **EEPROM** (`src/gba/save_eeprom.c`): 64-bit blocks, streamed one bit per halfword through DMA3 at `0x0DFFFF00` (fine for ROMs up to 32 MiB minus 256 bytes) with wait state 2 at 8 cycles (set before every transfer). Reading a block sends `11`, the block number (6 bits for 512 bytes, 14 for 8 KiB) and `0`, then receives 68 bits (4 ignored). Writing sends `10`, the block number, the 64 bits and `0`, then reads until bit 0 is 1, with a 15 ms timeout. Interrupts are off while DMA3 is set up and running. Byte *i* of a block is bits 8*i* to 8*i*+7, most significant first, as mGBA stores them. mGBA detects EEPROM at the first access, as 512 bytes, and grows to 8 KiB (and its `.sav` file) when an address past 512 bytes is used; the web build accepts a shorter stored save and pads it with `0xFF`, as mGBA does.

### Real-cart testing

Testing on cartridges is still to do; the code follows the hardware documentation, but has run only in mGBA. On each kind of cartridge (reproduction boards with each Flash chip and EEPROM size, retail-style boards, EverDrive and EZ-Flash in each save mode), check:

- **Detection:** the flash cart or board gives the game the right memory from the ID string (`SRAM_V113`, `FLASH512_V131`, `FLASH1M_V103`, `EEPROM_V124`), including EEPROM8K vs EEPROM512, which share an ID string and differ by address width; repro boards often patch or ignore it.
- **Flash chip IDs** read correctly (debug build: no "unknown Flash chip" warning) for SST, Macronix, Panasonic, Sanyo, Macronix 128 KiB, and the chips on repro boards; the 20 ms waits around ID mode suffice; Sanyo leaves ID mode with the second `0xF0`.
- **Flash timing:** programming and sector erase finish within the timeouts; the erase check (whole sector `0xFF`) passes; Macronix recovers after a forced timeout (`0xF0`); reading from EWRAM works and nothing reads the chip from ROM; programming zeros over already-programmed magic bytes works (no erase).
- **Flash banks** on 128 KiB chips: slots 4-7 land in bank 1 and slots 0-3 survive writing them (the ROM test's "full slots stay separate"); a 64 KiB chip in a `FLASH128K` game is refused rather than aliased; a 128 KiB chip in a `FLASH64K` game works on bank 0.
- **EEPROM:** both address widths, the 15 ms write timeout, wait state 2 at 8 cycles, and DMA3 transfers with sound DMA (DMA1/2) active; a 512-byte chip in an `EEPROM8K` game (and the reverse) fails cleanly rather than corrupting.
- **Power loss:** pull power (or the cartridge) during `save_write()` repeatedly for each type; the slot must read as the old save or the new one, never corrupt (`SAVE_CORRUPT` is possible only when a sector erase or block write is cut, and the other copy then still holds the previous save).
- **Persistence and costs:** saves survive power-off (SRAM: the battery), and the measured `save_write()`/`save_read()` times are close to the table above.
- **Atmel** chips: saves fail with the warning, without hanging or damaging the chip.

## Text and dialogue

**Status:** HUD text implemented; dialogue planned.

Variable-width font renderer drawing glyphs into BG tiles; text boxes with typewriter effect and choices; localization support. Japanese glyph sets need early planning.

**Implemented so far:** a minimal fixed-width HUD/debug text layer (`include/serval/text.h`): libtonc's 8x8 `sys8` font on BG0, a 30x20 character grid, up to four color styles with a drop shadow (`text_set_style()`: white by default, `TEXT_HIGHLIGHT` yellow; `text_set_color()`, `text_set_shadow()`, `text_set_style_color()`), `text_print_line()` for lines redrawn with changing content, centering on the screen or within columns (`text_print_centered()`, `text_print_centered_in()`), `text_clear_area()`, and `text_format()` (printf-style, including `%.3s`) for numbers without a C library. The full system above will build on or replace it.

## Special effects

**Status:** brightness fades and color mixing implemented (`include/serval/screen.h`). Alpha blending (`screen_set_blend()`, with the `LAYER_*` constants, and `SPRITE_BLEND` in `sprites.h`) and raster effects (`raster_scroll()`, `raster_backdrop()`, `raster_clear()`) are declared as *planned* API: designed below, stubbed, and warned about wherever a game uses them ([releases.md](releases.md#planned-api)). Palette writes are planned in the sprite and tilemap docs ([below](#palette-effects)). Windows and mosaic come after 1.0, with no API yet.

Screen-wide effects for transitions and atmosphere, exposed as API calls (and, for brightness, to scripts: [vm.md](vm.md)). The GBA's color special effect (`BLDCNT`, `BLDALPHA`, `BLDY`) does one thing at a time, so brightness fades and alpha blending share it ([below](#alpha-blending)); the engine owns those registers.

### Brightness

**Implemented:** `screen_set_brightness(level)` (−16 black … 0 … 16 white) fades the whole screen through the hardware's brightness effect (`BLDCNT`/`BLDY`, every layer and the backdrop as first targets, none as second targets); games fade by stepping it once per frame. It writes the registers at once, so set it right after `frame_begin()` to change whole frames. `serval_splash()` borrows the effect for its own fade and restores the game's level.

### Color mixing

**Implemented:** `color_mix(a, b, amount)` (`src/core/color.c`, portable) mixes two colors: `amount` 0 gives `a`, 256 gives `b`, 128 is halfway. Red, green and blue are mixed separately, each as `(a * (256 - amount) + b * amount) / 256`, rounded down, so `color_mix(a, b, t) == color_mix(b, a, 256 - t)`; bit 15 (unused) is 0 in the result, and amounts past 256 are clamped to 256 (*warns*). It is the building block for palette effects: a palette faded toward black, white or a dusk tint, a hit flash, a glowing backdrop (Pong's score glow is `screen_set_backdrop()` with colors it mixes itself).

- **Matches the hardware.** Rounding down is what the hardware does, so at multiples of 16 `color_mix` gives exactly what [alpha blending](#alpha-blending) shows for `a` over `b` with weights `16 - amount / 16` and `amount / 16`, and, toward white, exactly what `screen_set_brightness(amount / 16)` does. Toward black the hardware's brightness decrease rounds the other way (it subtracts `v * -level / 16`, rounded down), so a palette faded with `color_mix` is the same or one step (of 31) darker per channel. A palette fade therefore looks like the brightness fade, but leaves alone what it isn't applied to (the HUD's palette, say) and keeps alpha blending working, which a brightness fade pauses.
- **Cost:** about 140 cycles per color on the GBA (Thumb code in ROM; the test ROM logs it: 35,855 cycles for 256 colors in a Release build), so mixing all 256 background and sprite colors takes about 13% of a frame. Fade the palette banks that need it.
- **Tested** (`tests/color_tests.c`, on the host and in the test ROM): every pair of channel values at every amount against the definition (checked as `m * 256 <= sum < (m + 1) * 256`, not with color.c's arithmetic), the endpoints, rounding, symmetry, bit 15, the clamp and its warning, and the equivalences with the hardware's blending and brightness above.

### Palette effects

**Planned:** writing colors at run time, through a shadow palette that `frame_end()` copies to palette RAM in VBlank: `sprite_set_colors()` for a sprite group's palettes ([sprites.md](sprites.md#palettes)) and `tileset_set_colors()` for the background palettes ([tilemaps.md](tilemaps.md#tilesets)), both declared as planned API. Palette cycling (water, lava, a flashing sign) and palette fades are game code on top of them, with `color_mix()` for the colors; helpers for both may follow after 1.0. Until then games recolor with per-draw palettes (`SPRITE_PALETTE(n)`: Breakout's flashing bricks), sprite variants with their own palette (Pong's paddle flash) and `screen_set_backdrop()`.

### Alpha blending

**Planned:** declared in `screen.h` (`screen_set_blend()`, `LAYER_*`) and `sprites.h` (`SPRITE_BLEND`), not implemented yet. `screen_set_blend()` does nothing and warns once in debug builds, and sprites drawn with `SPRITE_BLEND` are drawn opaque (see [sprites.md](sprites.md)): everything stays opaque. See-through layers and sprites: a foreground the player shows through, water, shadows and ghosts, glows.

`screen_set_blend(top, bottom, top_weight, bottom_weight)`: where a pixel of one of the `top` layers is in front of a pixel of one of the `bottom` layers, the screen shows, for each 5-bit channel, `min(31, (top * top_weight + bottom * bottom_weight) / 16)`, rounded down (the hardware's alpha blending). The layers are bits, combinable with `|`:

| Constant | Value | Layer |
| --- | --- | --- |
| `LAYER_HUD` | `1 << 0` | Background 0, the text layer |
| `LAYER_FOREGROUND` | `1 << 1` | Background 1, in front of sprites |
| `LAYER_PLAYFIELD` | `1 << 2` | Background 2, the playfield |
| `LAYER_BACKGROUND` | `1 << 3` | Background 3, the parallax background |
| `LAYER_SPRITES` | `1 << 4` | Every sprite |
| `LAYER_BACKDROP` | `1 << 5` | The backdrop color |
| `LAYER_ALL` | `0x3F` | All of them |

The backgrounds are named after their [default roles](tilemaps.md#default-layer-roles); each bit stands for its background whatever it shows. The values are the hardware's (`BLDCNT`'s target bits; libtonc's `LAYER_BG0` … `LAYER_BD` are the same bits under other names).

- **Weights** go from 0 to 16 (16 is the whole color); larger ones are clamped to 16 (*warns*). Weights summing to 16 mix (8 and 8 is half and half, as `color_mix(top, bottom, 128)`); summing past 16 they brighten (16 and 16 adds the colors, for glows and light beams). A see-through foreground: `screen_set_blend(LAYER_FOREGROUND, LAYER_ALL & ~LAYER_FOREGROUND, 8, 8)`.
- **Which pixels blend.** Only the frontmost pixel blends, with the pixel directly behind it, and only if that one's layer is in `bottom`; otherwise the front pixel is drawn opaque. A layer may be in both masks. Sprites are one layer to the hardware: a sprite never blends with a sprite behind it; where two overlap, the front one blends with the layer behind both, and the sprite behind doesn't show there.
- **`SPRITE_BLEND` sprites** (in `spr_flags`, `sprite_draw*()` flags and metasprite pieces; [sprites.md](sprites.md)) blend over the `bottom` layers with these weights whether or not `top` has `LAYER_SPRITES`, so `screen_set_blend(0, bottom, ...)` blends those sprites only (blackjack's solid card shadows are the case that asked for it; its veil, a layer, would be `top`). They will use the hardware's semi-transparent sprite mode.
- **Off and timing.** With `bottom` 0 nothing blends: `screen_set_blend(0, 0, 0, 0)`, the default, is off. Bits outside `LAYER_ALL` are ignored (*warns*). The settings are applied at the next `frame_end()`, in VBlank, and kept until changed.
- **Shared with the brightness.** While `screen_set_brightness()` is not 0, blending pauses: the brightness effect takes the registers (every layer a first target, no second targets), so everything, `SPRITE_BLEND` sprites included, is drawn opaque, then faded. When the brightness returns to 0, the blend settings come back. The hardware can't do both at once, and the fade wins because it must cover the whole screen. A fade that must keep its blending fades the palettes with `color_mix()` instead ([above](#color-mixing)). `serval_splash()` borrows the effect and puts the game's settings back.
- **Web:** the renderer (`src/web/ppu.c`) already draws the hardware's alpha blending, semi-transparent sprites and brightness, matched against mGBA, so the implementation needs nothing new there.

### Raster effects

**Planned:** declared in `screen.h` (`raster_scroll()`, `raster_backdrop()`, `raster_clear()`), not implemented yet: each stub does nothing (layers scroll as a whole, the backdrop is one color) and warns once in debug builds.

The hardware draws the screen one line at a time; a raster effect changes one setting between lines (in the horizontal blank), so each of the 160 lines can scroll a background differently (a rippling lake, heat haze, bands of a sky moving at different speeds) or have its own backdrop color (a sky gradient). The API is declarative: the game gives a table of `SCREEN_H` values, entry 0 for the top line, and the engine applies it every frame.

- **Tables.** The engine reads the table at every `frame_end()` and doesn't copy it at the call, so it must stay valid while the effect is set (static or `const` data, not a local array); changing its values between frames animates the effect. Each `frame_end()` applies the table as it is then to the frame drawn next, all of it: a frame never changes halfway down. One effect at a time (the hardware has one channel for it): setting another replaces the one set. A new effect, and `raster_clear()`, take effect at the next `frame_end()`.
- **`raster_scroll(bg, vertical, offsets)`** scrolls each line of a map layer (background 1-3) by its own offset, added to the layer's scroll position `(sx, sy)`: the camera times the layer's `scroll_factor` plus its `map_set_scroll()` offset, or the offset alone on a `MAP_LAYER_FIXED` layer ([tilemaps.md](tilemaps.md#fixed-and-self-scrolling-layers)). Screen line `y` shows the layer from layer pixel `(sx + offsets[y], sy + y)` on, or with `vertical`, from `(sx, sy + y + offsets[y])`. On the playfield (background 2) only the picture moves: collision, entities and the camera stay where they are. The effect belongs to the background, like `map_set_scroll()`'s offset, but also stays when its layer is unloaded or replaced. A background outside 1-3 or a NULL table is ignored (*warns*).
- **How far lines can move.** A layer lives in VRAM as a 32×32-tile (256×256-pixel) ring around `(sx, sy)` ([tilemaps.md](tilemaps.md#streaming)). A span of `W` pixels can touch ⌊(`W` + 6) / 8⌋ + 1 tiles, which fits the ring's 32 for `W` up to 249: so one frame's offsets must lie within 9 pixels of each other horizontally (240 + 9; e.g. −4 to 4, a ripple) and within 89 vertically (160 + 89). The implementation streams the whole range the lines show, not just the screen's. The exception is an axis on which the layer wraps (`MAP_LAYER_WRAP`) and its map is 16, 8, 4, 2 or 1 metatiles long: the ring then holds all of it, so any offsets work there (sky bands from one repeating strip). Offsets spread further show wrong tiles at the edges of the lines furthest out (*warns*).
- **`raster_backdrop(colors)`** gives each line its own backdrop color, instead of `screen_set_backdrop()`'s one color. When the effect ends, the backdrop is `screen_set_backdrop()`'s color again: the one last set, also if it was set while the effect was on. A NULL table is ignored (*warns*).
- **`raster_clear()`** ends the effect, if any, at the next `frame_end()`.
- **Hardware (GBA):** DMA channel 0, started by each horizontal blank (HBlank DMA), copying one value per line into the scroll register or background palette color 0. DMA 0 and the HBlank interrupt are reserved for raster effects from 1.0, before they are implemented, so implementing them breaks no game ([core-api.md](core-api.md)); games must not use them. For `raster_scroll()` the engine adds the layer's scroll to each offset when it reads the table, so the values the DMA copies are prepared before VBlank, like the map's rows and columns.
- **Web:** the renderer draws each frame at once, from the state at VBlank, so it can't see register writes made between lines ([platforms.md](platforms.md#what-is-faked-or-missing)). It will apply the per-line values itself as it draws each line, which the declarative table makes possible.
- **Debugging:** the debug link's viewers are to show them ([debug-link.md](debug-link.md)).

### Windows and mosaic

**After 1.0**, no API yet: windows (rectangular regions and the object window: spotlights, masked HUD regions, effects inside a region only) and mosaic. They will be new functions, added without changing anything that exists. Draw-flag bits 13 (mosaic) and 14 (object window) of `spr_flags` and `sprite_draw*()` flags are reserved for them ([sprites.md](sprites.md)).

## Math

**Status:** implemented.

Standardize on fixed-point types and lookup tables for trig early. There is no FPU or hardware divider.

**Implemented so far:** sine and cosine from a 1024-step table, with u16 angles (`include/serval/math.h`); their inverse `angle_of(dx, dy)` (atan2, within 0.1°) and `fx_length(dx, dy)` (within 0.1%, saturating), both without division: the larger component is scaled into [2^16, 2^17) by shifts, the ratio of the smaller to it comes from a 257-entry reciprocal table and one multiply, and atan or sqrt(1 + r²) of the ratio from 33-entry tables with linear interpolation (all tables precomputed, with the formulas that generated them, in `src/core/trig.c`); 24.8 fixed point (`include/serval/fixed.h`) and deterministic random numbers (`include/serval/random.h`; `random_range` scales by multiplication, not division).

## Paths

**Status:** implemented (`include/serval/path.h`).

Movement patterns as data, for shmup formations and other genres' patrols, bosses and projectiles. A path is a `static const` table of steps; each step says for how many frames the entity moves along its heading at what speed, while the heading turns and the speed changes by fixed amounts per frame ("turtle" steering). That covers most of the patterns with a few numbers: a straight line is one step; a swoop is in, a 180° turning step, out; a circle is one turning step that never ends (`frames` 0); a weave is turns left and right, looping; stop-and-go is a decelerating step, one at speed 0 and one leaving. One table serves formations from either side through mirroring (`PATH_MIRROR_X`/`_Y` at `path_start()`), and setting `path_heading` after the start rotates the whole path (aim it with `angle_of`).

Paths are an ECS component (`C_PATH`, bit 6) with per-slot state (the path, step, frames into it, heading, speed). `sys_path()` advances every pathed entity a frame and writes `vel_x`/`vel_y`, so `sys_movement()` moves them and collisions and rendering need nothing new. Choices:

- **Velocity, not position.** Paths steer; they don't place. Entities keep moving through the same systems as everything else, and when a path without a loop ends, `C_PATH` goes and the entity flies on with its last velocity (a shmup enemy leaves the screen; the game culls it).
- **Zero defaults.** In a designated initializer, a step needs only what it uses: `{.frames = 30, .speed = FX(2)}` is straight; `turn` and `accel` default to 0. `frames` 0 means forever, so a never-ending step can't be a zero-length one, and no table can make `sys_path` loop without advancing. `loop` defaults to off.
- **Cheap per frame, no division.** The velocity is recomputed (`fx_sin`, `fx_cos`, two 32-bit multiplies) only when the heading or speed changed. Measured on the GBA (release, from ROM): about 160 cycles per entity on straight, constant-speed stretches, about 450 while turning, plus the loop over the 128 slots (about 10,000 cycles in all). The state lives in EWRAM (about 2.5 KiB, dropped from games that don't use paths).
- **Safe misuse.** Bad paths (NULL, empty, a loop step past the end) and dead entities are refused with a warning; `C_PATH` added by hand is removed (debug builds; it would otherwise resume a previous entity's path); speeds are capped at `FX(4096)` so an accelerating endless step can't overflow.

Scripts start and stop paths too, and can wait for one to end ([vm.md](vm.md)). Planned: per-step events (for now, games read `path_step`/`path_time`), absolute headings per step, and aiming a path from a script (scripts can't set `path_heading` yet).

## Physics

**Status:** bouncing bodies and map bodies implemented. Not in this version, and each can be added later without changing anything below: swept body-against-body tests (a new function next to `body_hit_side()`), body-to-body response (bodies pushing each other, moving platforms, a platformer controller with coyote time) and a collision broad phase ([Entity collision](#entity-collision)). Slopes and ladders for map bodies are declared and planned ([tilemaps.md](tilemaps.md#collision)).

**Bouncing bodies** (`include/serval/physics.h`): entities with `C_POS | C_VEL | C_BODY` and not `C_MAPBODY`. `sys_physics()` gives them gravity in any direction (`physics_set_gravity()`), keeps them inside a world rectangle (`physics_set_bounds()`, in world coordinates; any edge can be left open with `physics_set_open_edges()`) or wraps them around its edges (`physics_set_wrap()`), with per-body bounciness and friction, and lets them come to rest. They don't collide with each other or with tilemaps (the game tests pairs: [Entity collision](#entity-collision); map bodies collide with the map). The pools and `body_contact`'s bits are in [ecs.md](ecs.md#bodies). Each frame, after `sys_movement()` has moved them, `sys_physics()`:

1. with contacts on, clears `body_contact` and reports exits through open edges (below);
2. for each body, on x then y: wraps it on a wrapping axis, or bounces it off a wall it is at (moving into it, or resting against it); then adds the body's gravity to its velocity, unless it is resting on its floor; and while it touches its floor, applies friction to its speed along it;
3. while there is gravity, limits fall speeds (`body_max_fall`).

**Bounces.** A wall that the body's gravity pulls toward is its floor. A body that moved past a wall is mirrored back inside by the distance it overshot, as if it had bounced mid-frame, and gravity is added after the bounce, so it slows the rebound rather than adding to it.

- **Walls that aren't floors** keep all of the speed, whatever `body_bounce` is (a body without gravity bounces off every wall this way).
- **Floors** keep `body_bounce`/256 of the speed and of the overshoot for `body_bounce` 0-254 (224: 7/8; 0, the default, stops the body), so bouncing bodies come to rest.
- **`body_bounce` 255 is a perfect bounce:** the body comes back up as high as it fell from, bounce after bounce. Keeping the speed would not do that: the integration (position += velocity, then velocity += gravity) changes a falling body's position and speed in steps of a frame, and a bounce that keeps the speed peaks higher or lower than the last by up to the distance the body moves in a frame, depending on where in the frame it met the floor; a body that lands exactly on the floor at a frame's end (as one dropped from rest from the right height does) loses one frame's gravity of speed on every bounce and comes to rest. Instead, the overshoot is mirrored whole and the body leaves at the speed that keeps the height this integration preserves in free flight (`v² − g·v − 2·g·x`, v the velocity and x the position along gravity, g its acceleration): about the speed it hit at, computed by a bisection of about a dozen steps on the bounce frame only. Peaks stay within about a pixel of the starting height: `physics_tests.c` drops bodies from three heights for 6,000 frames each, and in a measurement bodies dropped from rest from every whole-pixel height 5 to 90 pixels above a floor, with gravity from 1/16 to 1 pixel per frame per frame, never rested and peaked within 1.4 pixels of where they started for 60,000 frames (keeping the speed instead, at a quarter pixel per frame per frame: up to 5 pixels off, and from 6 of the 90 heights the body came to rest within 30,000 frames). A `body_max_fall` the body reaches still limits its fall, and costs it height. Map bodies' floor bounces at 255 are perfect too, by the same solver for their integration (velocity += gravity, then move, stopping flush against the floor): `sys_map_movement()` keeps `v² + g·v − 2·g·x` instead, and the body loses `past` rather than twice its overshoot ([tilemaps.md](tilemaps.md#map-bodies)).
- **Rests:** a floor bounce of a body that hits the floor, or would leave it, slower than twice one frame's gravity becomes a rest: the body sits exactly on the floor with zero speed, and the floor cancels gravity, so it stays put (even if gravity is switched off) until gravity changes direction or the game moves it. A perfect bounce rests this way only when its bounces are tiny: when the body hit the floor slower than about 3.6 times a frame's gravity (a bounce about 6 times a frame's gravity high: 1.6 pixels at `FX_ONE / 4`) and ended far enough past it.
- **Friction:** a body touching a floor loses `body_friction`/256 of its speed along it each frame, rounded up, so any non-zero friction stops a sliding body.
- A body bigger than the bounds is pinned to their left or top edge (warning once in debug builds).

**Per-body gravity:** `body_gravity[i] = BODY_GRAVITY(sixteenths)` scales gravity for one body (16 normal, 8 half, 0 none, −16 reversed), e.g. Breakout's capsules fall while the ball flies straight. The pool stores the scale minus 16, so the zero `entity_create()` leaves is normal gravity. Floors follow each body's own gravity (a body without gravity bounces off every wall keeping its speed; one with reversed gravity rests on the ceiling). `sys_map_movement()` applies it too. Cost: `sys_physics()` checks for scales (sixteen per word read, ~110 cycles a frame, only while there is gravity); with none it runs its fast loop unchanged, otherwise its general loop, handing the scaled bodies to an out-of-line update in ROM (32 bodies, 4 scaled: ~4,300 cycles more per frame). Scaling inside the loops, which are out of registers, cost every body ~27 cycles.

**Contacts:** with `physics_set_contacts(true)`, `sys_physics()` reports in `body_contact` (the pool map bodies use; `BODY_SIDE_*` = `MAP_CONTACT_*` bits; every bit is listed in [ecs.md](ecs.md#bodies)) the walls each body touched: on the frame it bounces, and every frame it rests against one (on a floor, or at a wall with no speed away from it). `BODY_CONTACT_EXIT` with a side bit marks the frame a body became entirely outside through that open edge (Pong's goal, Breakout's lost ball). Exactly: the `sys_physics()` call (with contacts on, on an axis that doesn't wrap) that finds, in pixels with the bounds as given (right and bottom exclusive), `pos_x + body_w <= left`, `pos_x >= right`, `pos_y + body_h <= top` or `pos_y >= bottom` now, and not at the position minus the velocity (where the body was before this frame's `sys_movement()`). It is the body's only contact that frame, apart from walls of the other axis, and the next call clears it. A game's own `pos_x < left - body_w` is a frame late for a body that lands exactly on `left - body_w` (its last column is already outside), so games use `BODY_CONTACT_EXIT` rather than a position test of their own. Wrapping axes have no walls and no exits and report nothing. Off by default (`body_contact` stays 0 for bouncing bodies), because it costs: contacts run the general loop (~90 cycles more per body); recording them in the fast loop cost bunnymark ~1,000-2,000 cycles a frame even though only a few dozen bodies touch a wall per frame (the loop is out of registers). The contacts of physics bodies are cleared four bytes at a time (~60 cycles); map bodies' contacts, set by `sys_map_movement()`, are left alone (then clearing goes slot by slot, in ROM: ~3,400 cycles). Switching contacts off clears them.

**Loops:** `sys_physics()` has a fast loop (no wrapping, no contacts, no scaled gravity: bunnymark's case) and a general one (both in IWRAM), so the common case never tests settings per body. The general loop bounces bodies through an out-of-line call, which kept it at 2.7 KB of IWRAM instead of 4.6 KB; it costs wrapping games about 30-70 cycles more per body than the dedicated wrap loop it replaced. A body resting on a floor, which meets it every frame, is recognized first, so it pays neither the bounce's multiply nor the perfect-bounce test; the perfect bounce's computation is inlined in both loops, as a call (to ROM) from the fast loop cost bunnymark, which has no perfect bounces, about 2,600 cycles a frame. With it inlined, bunnymark went from 73,426 to 73,412 cycles a frame on average (peak 78,643 to 77,647), and the test ROM's measurement (32 bodies, half of them resting, release build) from 6,630 to 6,510 cycles for the fast loop and from 9,212 to 9,436 for the general one with contacts (9,512 to 9,736 wrapping): moving code shifted how the loops' registers are allocated.

Map bodies (`C_MAPBODY`, `include/serval/map.h`) are the platformer side: `sys_map_movement()` applies the same gravity and stops them flush against solid and one-way metatiles of the playfield, bouncing off every side of the map with `body_bounce` and reporting which sides touched (`body_contact`); see [tilemaps.md](tilemaps.md#collision).

## Entity collision

**Status:** pairwise tests implemented, and collision events to scripts (`vm_collide()`, [vm.md](vm.md#collisions)). Not in this version: a broad phase, which can come later without an API change.

**Implemented so far:** `body_overlap(a, b)` (`include/serval/physics.h`), a rectangle test between two bodies, which the game calls for the pairs it cares about (Pong: ball against each paddle; Asteroids: every shot against every rock, and the ship against every rock), and `body_hit_side(a, b)`, which side of `a` met `b` (a stomp is `BODY_SIDE_BOTTOM`), judged from their positions before the frame's movement and their relative motion, so it holds for fast bodies and static colliders; bodies that already overlapped before the frame get `BODY_SIDE_INSIDE` rather than a guessed side. A screen-space entity (`SPRITE_SCREEN`) against a world one is compared in the world, with the camera added to the screen-space position (Shmup: shots on the screen against turrets on the scrolling map); a pair of one kind costs one more test than before. Hitboxes can be smaller than sprites, with the sprite's origin centering the art.

**Collision events to scripts** (implemented, `include/serval/vm.h`): `vm_collide(a, b)` names a pair of entity sets by component masks, once at boot (`vm_collide(C_PLAYER, C_FIREFLY)` in `fireflies`), and `vm_events()` tests every entity of one set against every entity of the other with `body_overlap()` each frame, raising Collision events for the entities attached to objects with a handler; up to `VM_COLLIDE_PAIRS` (8) pairs, removed with `vm_collide_clear()`. A scripted game needs no collision code in C; game code can still raise Collision events itself with `vm_event()` for rules a pair doesn't express. The sets, order, limits and costs (about 75 cycles a test, plus listing each set) are in [vm.md](vm.md#collisions).

**Later, no API yet: a broad phase.** Every test is pairwise: all pairs at 128 entities would be about 8,000 tests, so games keep their sets small and apart (shots against enemies, not every body against every body). A coarse spatial grid or collision groups could cut that inside `vm_collide()`, or come as a new public grid, without changing anything that exists ([api-freeze.md](api-freeze.md#later-additively-no-api-now)).

## Camera

**Status:** implemented (`include/serval/map.h`): a position the game sets; following a target is game code for now.

Follows a target entity, clamps to room bounds, and drives BG streaming ([tilemaps.md](tilemaps.md#streaming)).

**Implemented so far:**

- `camera_set(x, y)` sets the world position shown at the screen's top-left, in pixels; `camera_x()` and `camera_y()` read it. While a playfield (BG2) map is loaded, it is clamped so the view stays inside it; read it back with `camera_x()`/`camera_y()` if the game needs to know. Clamping at the map's edges is silent, since a camera following the player is clamped near every edge, and loading the playfield clamps the current position too. On an axis where the map is smaller than the screen the camera can only be 0, so asking for any other value there warns (once, in debug builds): the usual mistake is drifting a small layer with the camera, which does nothing; use `map_set_scroll()` for that ([tilemaps.md](tilemaps.md#fixed-and-self-scrolling-layers)). Without a playfield, any position is kept.
- `sys_render()` and `sys_render_by_depth()` draw entities at their world position minus the camera, so entity positions are world coordinates, except for entities with `SPRITE_SCREEN` in `spr_flags`, whose positions are screen coordinates (a shooter's ship and bullets over a scrolling stage); `sprite_draw()` takes screen coordinates (HUD sprites). The camera starts at (0, 0), where world and screen coordinates are the same, so games that don't scroll never notice it.
- At the next `frame_end()`, every map layer scrolls to `camera * scroll_factor` plus its `map_set_scroll()` offset (fixed layers, `MAP_LAYER_FIXED`, to their offset alone) and newly visible rows and columns are streamed in ([tilemaps.md](tilemaps.md#streaming)). Set the camera before the render systems run, so sprites and backgrounds move together.
- Following a target is a few lines of game code, e.g. `camera_set(fx_to_int(pos_x[player]) - SCREEN_W / 2, fx_to_int(pos_y[player]) - SCREEN_H / 2)` (the clamp keeps it inside the room). Planned: dead zones and smoothing.
