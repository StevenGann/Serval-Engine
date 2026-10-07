# Core API

The lowest engine layer is a raylib-style flat C API over libtonc. It is also the abstraction boundary for future platforms: each call means the same thing on every target (see [platforms.md](platforms.md)).

**Status:** implemented: input, the frame loop, sprites, map backgrounds and the camera, map collision, the ECS and its systems, PSG sound effects and music, brightness fades and color mixing, text, math, random, paths, save data, the bytecode VM, debug output. Declared as *planned* API, implemented in a later 1.x version ([releases.md](releases.md#planned-api): every use compiles with a warning, and does nothing harmful today): tracker music, sampled sound effects, the sound bank and the PSG wave channel ([audio.md](audio.md)); streamed sprite groups, LZ77-compressed sprites and tilesets, runtime sprite tiles and palette writes ([sprites.md](sprites.md), [tilemaps.md](tilemaps.md)); ladders and floor slopes ([tilemaps.md](tilemaps.md#collision-types)); alpha blending and raster effects ([runtime-systems.md](runtime-systems.md#special-effects)). After 1.0, with no API yet: tileset groups, palette sharing, windows and mosaic, among others ([api-freeze.md](api-freeze.md#later-additively-no-api-now)). The full list of names with units, limits and misuse behaviour is in [api-reference.md](api-reference.md); this page covers the design, the [hardware the engine uses](#hardware-the-engine-uses) and the [bits and values it reserves](#reserved-bits-and-values).

```c
void serval_init(void);               // once at startup: wait states, interrupts, display, ECS, sound
void serval_splash(void);             // optional: the "Made with Serval Engine" splash

void frame_begin(void);               // poll buttons, empty the sprite draw list
void frame_end(void);                 // VBlank sync, copy shadow OAM, stream maps, step sound and music

u32  frame_count(void);               // frames since serval_init()
u32  frame_cpu_cycles(void);          // previous frame's work, in CPU cycles
u32  frame_cpu_permille(void);        // the same, in thousandths of the frame budget
u32  frame_budget_cycles(void);       // 280,896 per frame at 60 Hz

bool button_down(u16 buttons);        // BUTTON_A, BUTTON_LEFT, ... (OR-able: any of them)
bool button_pressed(u16 buttons);
bool button_repeat(u16 buttons);      // menus: on the press, then after 20 frames every 4 while held

int  screen_width(void);              // also SCREEN_W / SCREEN_H constants
int  screen_height(void);
void screen_set_backdrop(Color color); // Color from COLOR_RGB(r, g, b), 0-255 components

void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags);
void sprite_draw_ex(u16 sprite_id, u8 frame, int x, int y, u16 angle, FIXED scale_x, FIXED scale_y, u16 flags);
```

`serval/serval.h` includes every public header except `gba.h`. Sprite loading is described in [sprites.md](sprites.md#api).

Modules, all in `include/serval/` (details in [api-reference.md](api-reference.md)):

| Header | Implemented | Planned (declared) |
| --- | --- | --- |
| `core.h` | Init, splash, frame loop, frame count, CPU timing, buttons, held-button repeat (`button_repeat`, `button_repeat_set`, `button_repeat_reset`) | |
| `screen.h` | Screen size, `Color`, `COLOR_RGB`, `color_mix`, backdrop color, brightness fades (`screen_set_brightness`), the `LAYER_*` constants | [Alpha blending](runtime-systems.md#alpha-blending) (`screen_set_blend`), [raster effects](runtime-systems.md#raster-effects) (`raster_scroll`, `raster_backdrop`, `raster_clear`) |
| `sprites.h` | Sprite assets, resident groups, loading in layers with marks (`sprite_groups_mark`, `sprite_groups_release`), drawing, rotation, scaling, metasprites, layers, per-draw palettes, animation data, `sprite_stats` ([sprites.md](sprites.md)) | [Streamed groups](sprites.md#residency-modes) (`SPRITE_GROUP_STREAMED`), [LZ77 sprites](sprites.md#lz77-compression) (`SPRITE_ASSET_LZ77`), [runtime tiles](sprites.md#runtime-tiles) (`sprite_set_tiles`, `SPRITE_MAX_TILE_UPDATES`), [palette writes](sprites.md#palettes) (`sprite_set_colors`), [blended sprites](sprites.md#alpha-blending) (`SPRITE_BLEND`) |
| `map.h` | Tilesets and animated tiles, metatile map layers on BG1-BG3, scroll offsets, the camera, runtime cell changes, map collision (empty, solid, one-way), tags and `map_tags_in`, map bodies (`C_MAPBODY`, `sys_map_movement`) ([tilemaps.md](tilemaps.md)) | [LZ77 tilesets](tilemaps.md#tilesets) (`TILESET_LZ77`), [palette writes](tilemaps.md#palette-writes) (`tileset_set_colors`), [ladders](tilemaps.md#collision-types) (`MAP_LADDER`, `MAP_CONTACT_LADDER`), [floor slopes](tilemaps.md#collision-types) (`MAP_SLOPE_*`) |
| `ecs.h` | Entities, engine components, `ECS_FOR_EACH`, `ecs_count`, `ecs_gather`, `sys_movement`, `sys_animate`, `sys_render`, `sys_render_by_depth` ([ecs.md](ecs.md)) | |
| `path.h` | Movement patterns as `PathStep` tables: `path_start()`, `sys_path()` ([runtime-systems.md](runtime-systems.md#paths)) | |
| `physics.h` | Bouncing bodies (`C_BODY`): gravity (global and per body), maximum fall speed, bounces up to a perfect one, bounds, open edges, wrap-around, contacts, `sys_physics()`, `body_overlap()`, `body_hit_side()`. For balls, particles and debris; platformer characters are map bodies | |
| `audio.h` | PSG sound effects (`psg_table_set()`, `psg_play()`, `psg_stop_all()`) and PSG music (`psg_music_play()`, pause, tempo, volume) on square 1, square 2 and noise ([audio.md](audio.md)) | The [wave channel](audio.md#wave-channel) (`PSG_WAVE`, `psg_waves_set`), the [sound bank](audio.md#sound-bank) (`audio_bank_set`), [tracker music](audio.md#tracker-music) (`music_*`), [sampled sound effects](audio.md#sampled-sound-effects) (`sfx_*`; their `Sfx` handle and `SFX_NONE` are implemented) |
| `save.h` | Save slots with checksums and versions on SRAM, Flash or EEPROM (`localStorage` on the web) ([runtime-systems.md](runtime-systems.md#save-data)) | |
| `text.h` | HUD/debug text on BG0 (8x8 font, 30x20 cells, up to four color styles such as a highlight, centering within columns) and `text_format()` (printf-style without a C library) | |
| `vm.h` | The bytecode VM: script blobs, objects and their instances, events, behaviours and reactions, collision pairs (`vm_collide`), the two phases (`vm_step`, `vm_events`), and every number of the blob format ([vm.md](vm.md)) | |
| `fixed.h` | 24.8 fixed point: `FX(n)`, `fx_to_int()`, `FX_ONE` | |
| `math.h` | `int_min`, `int_max`, `int_abs`, `int_clamp`, `fx_mul`, `fx_div` (prefixed to avoid libtonc's `clamp`/`min`/`max`); u16 angles (`ANGLE_DEG(d)`, clockwise on screen) with `fx_sin`/`fx_cos`, `angle_of` (atan2) and `fx_length` | |
| `random.h` | Deterministic xorshift32: `random_seed()`, `random_u32()`, `random_range(lo, hi)`, and `random_entropy()`, a seed from the player's input timing (the same input gives the same value on every platform) | |
| `debug.h` | `debug_log()` (mGBA debug log), `debug_warning_count()`, `debug_exit()` | |
| `platform.h` | Integer types, `FIXED`, `NULL`, IWRAM/EWRAM placement macros, the planned-API marker (`SERVAL_PLANNED`) | |
| `gba.h` | GBA-only escape hatches (`gba_oam_submit()`); not included by `serval.h` | |

## Debug builds report misuse

In Debug and RelWithDebInfo builds (`SERVAL_DEBUG`), the engine reports API misuse as warnings in the emulator's debug log (mGBA: *Tools > View Logs*), prefixed `serval:`, instead of failing silently. For example:

```
serval: sprite_draw: sprite 3 is not loaded; load a sprite group containing it
serval: sprite_group_load: needs 1025 tiles, but only 1024 of 1024 are free
serval: entity_create: all 128 entities are in use; returning ENTITY_NONE
```

Each problem is reported once rather than every frame, as one line of at most 247 characters after the `serval:` prefix (mGBA's log line holds 255; on the web it goes to the browser's console as a warning). Release builds compile the checks out entirely, so they cost nothing; the API still fails safely (nothing is drawn, `false` or `ENTITY_NONE` is returned). Games can use `SERVAL_DEBUG` for their own debug code too. [api-reference.md](api-reference.md) marks which calls warn.

## Hardware the engine uses

On the GBA the engine programs the hardware itself, and what it takes is part of the API: a later version that took a timer, a DMA channel, an interrupt or a piece of VRAM that a 1.0 game was using would break that game. So everything the planned features will need is reserved now, before they are implemented ([api-freeze.md](api-freeze.md#principles)), and claiming anything more is a breaking change. Below, *in use* is what this version programs, *reserved* is what a named planned feature will take, and *free* is the game's. Games may program free hardware themselves, through libtonc (which stays on the include path) or its registers; the engine's, in use or reserved, they leave alone and reach only through Serval API. This is the GBA's hardware: the web build runs the same code on virtual GBA hardware with no interrupts, timers or DMA ([platforms.md](platforms.md#what-is-faked-or-missing)), so what a game programs itself works only on the GBA.

**What `serval_init()` sets up:** `WAITCNT` to the standard 3/1 ROM wait states with prefetch and 8 for save RAM (power-on default: 4/2 without prefetch), which speeds up all code and data in ROM, including the game's; libtonc's interrupt dispatcher, with the VBlank interrupt enabled and no handler; display mode 0 with sprites on and 1D sprite tile mapping, every hardware sprite hidden; sound on, with the tone generators at full volume on both speakers; and the cycle counter (timers 2 and 3).

**Timers**

| Hardware | Status | Games |
| --- | --- | --- |
| Timer 0 | Reserved: the sample clock of tracker music and sampled sound effects (*planned*: Maxmod, [audio.md](audio.md#hardware-it-claims)) | Must not use it, or its interrupt |
| Timer 1 | Free | The game's, with its interrupt |
| Timers 2 and 3 | In use: cascaded into a free-running 32-bit cycle counter, started by `serval_init()`, behind `frame_cpu_cycles()` (`random_entropy()` doesn't use it, so games stay deterministic) | May read them; must not stop, reload or reconfigure them, or enable their interrupts |

**DMA**

| Hardware | Status | Games |
| --- | --- | --- |
| DMA 0 | Reserved: raster effects (*planned*), started by every horizontal blank ([runtime-systems.md](runtime-systems.md#raster-effects)) | Must not use it |
| DMA 1, DMA 2 | Reserved: they will feed the Direct Sound FIFOs A and B for tracker music and sampled sound (*planned*, [audio.md](audio.md#hardware-it-claims)) | Must not use them |
| DMA 3 | Shared, in use inside engine calls: the engine may use it during its own calls and leaves it idle when they return. Today only the EEPROM save backend does, inside `save_*()` calls of a game built with `SAVE EEPROM8K` or `EEPROM512`, with interrupts off while it runs | Free between engine calls, for transfers that complete at once (libtonc's `dma3_cpy()`); never one set to repeat or to start at VBlank or HBlank |

**Interrupts**

| Hardware | Status | Games |
| --- | --- | --- |
| The dispatcher | In use: `serval_init()` installs libtonc's (`irq_init()`, master handler `isr_master`, which doesn't nest) and turns interrupts on | Add handlers for the free interrupts with libtonc's `irq_add()` or `irq_set()`; never install another master handler (`irq_init()`, `irq_set_master()`) |
| VBlank | In use: enabled by `serval_init()`; `frame_end()` waits for it with the BIOS's `VBlankIntrWait()`, with no handler of the engine's yet. Its handler is reserved: Maxmod's `mmVBlank()` will run there first, uninterrupted (*planned*, [audio.md](audio.md#frame-loop)), and anything else the engine needs each VBlank later | Must not set a VBlank handler (`irq_add(II_VBLANK, ...)` would replace the engine's) |
| HBlank | Reserved: raster effects (*planned*) | Must not use it |
| Timer 0, timers 2-3, DMA 0-2 | Reserved with their timers and channels, above | Must not use them |
| VCount, timer 1, DMA 3, serial, keypad, cartridge | Free (DMA 3's for the game's own transfers) | The game's |

**Sound** ([audio.md](audio.md#hardware-it-claims))

| Hardware | Status | Games |
| --- | --- | --- |
| `SOUNDCNT_X` (master enable) | In use: sound on since `serval_init()` | Leave it on |
| `SOUNDCNT_L` | In use: the tone generators' master volume (full) and speaker enables, set by `serval_init()` for channels 1, 2 and 4 on both speakers (bits 8, 9, 11, 12, 13 and 15). Bits 10 and 14, channel 3 on each speaker, are reserved for the wave channel (*planned*) | Must not write it (`psg_music_set_volume()` sets the music's volume) |
| `SOUNDCNT_H` | In use: bits 0-1, the tone generators' share of the output (100%). Bits 2-15, Direct Sound A and B, are reserved for the mixer (*planned*), which writes the whole register; the engine then sets bits 0-1 back | Must not write it |
| PSG channels 1, 2 and 4: square 1, square 2, noise | In use: `audio.h`'s sound effects and music; `serval_splash()`'s jingle on square 1 | Play them through `audio.h` |
| PSG channel 3 (`SOUND3CNT_L`/`_H`/`_X`, 0x4000070-0x4000075) and wave RAM (0x4000090-0x400009F, both banks) | Reserved: the wave channel (`PSG_WAVE`, `psg_waves_set()`, *planned*, [audio.md](audio.md#wave-channel)); off today | Must not use them |
| Direct Sound A and B, FIFOs A and B (0x40000A0-0x40000A7) | Reserved: tracker music and sampled sound effects (*planned*), mixed by Maxmod; A plays left, B right | Must not use them |

**Display and effects**

| Hardware | Status | Games |
| --- | --- | --- |
| `DISPCNT` | In use: `serval_init()` sets mode 0, sprites on and 1D sprite tile mapping; the text layer and map layers turn BG0-BG3 on and off. The engine changes only those bits after `serval_init()` | Must keep mode 0, sprites and the mapping, and leave the BG enable bits to the engine. May set bit 5, "H-Blank interval free" (`sprite_stats_scanlines()` counts with it) |
| BG0: `BG0CNT`, `BG0HOFS`, `BG0VOFS` | In use once text is used: the text layer (`serval_splash()` borrows `BG0CNT` and BG0's on/off bit and puts them back) | Through `text.h` |
| BG1-BG3: control and scroll registers | In use once a map layer is loaded: set by `map_load()` and at every `frame_end()` (`map.h`); BG1-BG3's scroll registers per line by raster effects (*planned*) | Through `map.h` |
| `BLDCNT`, `BLDALPHA`, `BLDY` | The engine's at all times: brightness fades (`screen_set_brightness()`, in use: `BLDCNT` and `BLDY`; borrowed by `serval_splash()`) and alpha blending (`screen_set_blend()`, `SPRITE_BLEND`, *planned*: `BLDCNT` and `BLDALPHA`) share the hardware's one color effect ([runtime-systems.md](runtime-systems.md#special-effects)) | Must not write them |
| Windows (`WIN0H` ... `WINOUT`, `DISPCNT` bits 13-15) and `MOSAIC` | Free: not used. Windows and mosaic are later engine features with no API yet ([runtime-systems.md](runtime-systems.md#windows-and-mosaic)), which will use them only for a game that calls them; the draw flags they will need are reserved ([below](#reserved-bits-and-values)) | The game's |
| OAM | In use: the shadow OAM, rebuilt every frame from draw calls and copied whole, with the 32 rotation matrices, in VBlank ([below](#sprite-submission-model)) | Through draw calls, or raw entries with `gba_oam_submit()` (`gba.h`); never write OAM directly |

**VRAM**

| Region | Status | Games |
| --- | --- | --- |
| BG charblock 0 (16 KB, tiles 0-511) | The text layer's: tiles 0-383 hold the font, 96 for each text style used, written the first time a style is used. Tiles 384-511 are unused today and reserved for the text layer (larger fonts, dialogue boxes, after 1.0) | Through `text.h` |
| BG charblocks 1-2 (tiles 0-1023 at character base 1) | In use: the tileset (`tileset_load()`, `tileset_set_tiles()`); `serval_splash()` draws its logo into charblock 1 and leaves it there (games load their tilesets after it) | Through `map.h` |
| Screenblocks 24-27 (the first 8 KB of charblock 3) | Unused today; reserved for the text layer and the engine | Must not use them |
| Screenblocks 28, 29, 30 | In use once a map layer is loaded: the maps of BG1, BG2, BG3, streamed around the camera ([tilemaps.md](tilemaps.md#vram-layout)) | Through `map.h` |
| Screenblock 31 | In use once text is used: the text layer's map | Through `text.h` |
| OBJ VRAM (charblocks 4-5, 1,024 tiles) | In use: sprite groups, allocated from tile 0 up in load order and released by marks; the top is reserved for the slots of streamed groups (*planned*), which will be taken from the top down ([sprites.md](sprites.md#vram-allocation)) | Through sprite groups |

**Palette RAM**

| Region | Status | Games |
| --- | --- | --- |
| BG color 0 (bank 0, color 0) | In use: the backdrop (`screen_set_backdrop()`); per line with `raster_backdrop()` (*planned*) | Through `screen.h` |
| BG banks 0-14, colors 1-15 | In use once a tileset is loaded: its palettes (`MAP_MAX_PALETTES`); writes through a shadow palette with `tileset_set_colors()` (*planned*). `serval_splash()` borrows colors 1-15 of bank 13 and one color of bank 14, and puts them back | Through `map.h` |
| BG bank 15 | The text layer's: colors 1-8 are a text and a shadow color for each of the four styles; colors 9-15 are unused today and reserved for the text layer | Through `text.h` |
| OBJ banks 0-15 | In use: sprite groups' palettes, allocated with their groups and released by marks; writes through a shadow palette with `sprite_set_colors()` (*planned*) | Through `sprites.h` |

Games write palette RAM only through the API (loads, `screen_set_backdrop()`, `text_set_style_color()`, the planned palette writes), never directly: once palette writes are implemented, the shadow palettes are copied to palette RAM in VBlank.

**Other**

| Hardware | Status | Games |
| --- | --- | --- |
| `WAITCNT` | In use: set by `serval_init()`; the EEPROM backend sets wait state 2 to 8 cycles before each transfer | Must not change it |
| `KEYINPUT` | Read by `frame_begin()` | May read it; `button_*()` report the same state |
| Cartridge save memory (SRAM, Flash or EEPROM) | In use by `save.h` in a game that calls it, of the type `serval_add_rom(... SAVE <type>)` picks; timeouts count scanlines (`VCOUNT`), taking no timer or interrupt ([runtime-systems.md](runtime-systems.md#save-data)) | Through `save.h` |

`serval_splash()` borrows more while it runs, and puts it back: the backdrop, BG0, `BLDCNT` and `BLDY`, colors of BG banks 13 and 14, and PSG square 1 ([below](#splash-screen)).

## Reserved bits and values

Every bit and value of the data formats and flag words that this version doesn't define belongs to a later engine version. Loaders refuse data that uses one (they return false and warn in debug builds), so a later version can give it a meaning without changing what existing data means ([releases.md](releases.md#versioning), "What breaks a data format"); where nothing checks, games must leave it clear. Values for planned features are refused, or inert, the same way until the feature is implemented.

| Where | Reserved | Planned | In this version | Details |
| --- | --- | --- | --- | --- |
| `SpriteAsset.flags` | Bits 0 and 4-7 | Bit 3, `SPRITE_ASSET_LZ77` ([LZ77](sprites.md#lz77-compression)) | `sprite_group_load()` refuses the group | [sprites.md](sprites.md#rom-data-format) |
| `SpriteAsset.size` | 13-255 | | Refused (ordinary sprites; metasprites don't use it) | [sprites.md](sprites.md#rom-data-format) |
| `SpriteGroup.flags` | Bits 1-7 | Bit 0, `SPRITE_GROUP_STREAMED` ([streaming](sprites.md#residency-modes)) | Refused | [sprites.md](sprites.md#rom-data-format) |
| `SpriteGroup.slots` | Any value but 0 in a resident group | The slot count of a streamed group | Refused | [sprites.md](sprites.md#residency-modes) |
| `SpritePiece.flags` | Bits 13-14; bits 2-7 and 15 belong to the whole draw | Bit 12, `SPRITE_BLEND` ([blending](sprites.md#alpha-blending)) | Refused, except bit 12 (drawn opaque, warns) | [sprites.md](sprites.md#rom-data-format) |
| `SpritePiece` padding (byte 9) | All of it: leave it zero | | Not checked | [sprites.md](sprites.md#rom-data-format) |
| Draw flags (`sprite_draw*()` flags, `spr_flags`) | Bits 13 and 14 (mosaic and the object window, later) | Bit 12, `SPRITE_BLEND` ([blending](sprites.md#alpha-blending)) | Not checked per draw (the hot path): games must never set 13 and 14. Bit 12 is drawn opaque and warns | [sprites.md](sprites.md#alpha-blending) |
| `Tileset.flags` | Bit 1 (8bpp tilesets, later), bits 2-7 | Bit 0, `TILESET_LZ77` ([LZ77](tilemaps.md#tilesets)) | `tileset_load()` refuses the tileset | [tilemaps.md](tilemaps.md#tilesets) |
| `MapLayer.flags` | Bits 2-7 | | `map_load()` refuses the layer | [tilemaps.md](tilemaps.md#rom-data-format) |
| Collision types (`Metatile.collision`, bits 0-3) | 10-15 (ceiling slopes, for one) | 3, `MAP_LADDER`; 4-9, the `MAP_SLOPE_*` ([ladders and slopes](tilemaps.md#collision-types)) | Load, with a warning once per kind, and collide: 3 and 10-15 as `MAP_EMPTY`, 4-9 as `MAP_SOLID` | [tilemaps.md](tilemaps.md#collision-types) |
| `body_contact` | Bits 4 and 7 | Bit 6, `MAP_CONTACT_LADDER` ([ladders](tilemaps.md#collision-types)) | Never set (the engine writes it; games read it) | [ecs.md](ecs.md#bodies) |
| `ent_mask` and component masks | Bits 7-15 (engine components of later versions) | | `entity_create()` leaves them out (warns); nothing checks them in `ent_mask`: games must never set them | [ecs.md](ecs.md#component-bits) |
| `PathStep` | The 2 bytes of padding after `frames` (a step flag, later) | | Not read: leave them zero | [runtime-systems.md](runtime-systems.md#paths), [api-freeze.md](api-freeze.md#later-additively-no-api-now) |
| `PsgSound.channel`, `PsgTrack.channel` | 4-255 are invalid | 3, `PSG_WAVE` ([wave channel](audio.md#wave-channel)) | Refused: `psg_play()` skips the sound, `psg_music_play()` leaves the track out (warns) | [audio.md](audio.md#wave-channel) |
| VM blob header flags | Bit 1 (an extended handler table), bits 2-15 | | `vm_load()` refuses the blob | [vm.md](vm.md#header-16-bytes) |
| VM blob reserved fields | Bytes 6-7 of an object record, byte 3 of an array record: must be 0 | | Refused | [vm.md](vm.md#blob-format) |
| VM entity properties | 15-63 (later engine properties) | | Read 0, write nothing (warns) | [vm.md](vm.md#entities) |
| VM engine calls and opcodes | SYS numbers from `VM_SYS_COUNT` on (calls for tracker music, sampled sound and later features, appended); unassigned opcodes | | Halt the script (warns) | [vm.md](vm.md#engine-calls) |

## Splash screen

`serval_splash()` shows "made with" (grey, the text font) over the Serval Engine logo on a black backdrop and returns about three seconds later: a 500 ms fade-in, a 500 ms hold, a coin-like jingle, a 1.5 s hold and a 500 ms fade-out (hardware fade-to-black on the text layer, which the logo shares). Any button after the fade-in skips the rest. The logo is also available as transparent PNGs for branding (`docs/images/`, made from this same drawing code by `tools/logo-png.py`).

The logo is the mark-and-wordmark design chosen after three rounds of candidates ([open-questions.md](open-questions.md)): a front-on serval's head with big green eyes, whisker dots and enormous ears (the left upright with a notch bitten from its edge, the right swivelled out as if listening), outlined in a deep warm brown, with light from above on the ear rims and the top of the forehead and a touch of shade inside the ears and under the chin, beside "SERVAL" over "ENGINE" in chunky gold and cream letters whose bottom edge is a shade darker than their bevel; "made with" sits above. It is drawn when the splash starts, from ASCII pictures and a few rules in `src/gba/splash_art.c`, into charblock 1 (BG0 keeps charblock 0 as its base; a 10-bit tile index reaches both; tiles with nothing drawn take no VRAM) and BG palette bank 13, and shown on the text layer's map, once, before the fade-in. The timing and the buttons are the portable `src/core/splash_logic.c`, tested natively (`tests/splash_logic_tests.c`).

It borrows the backdrop, BG0 (control register and on/off state), the text layer (drawing without the text shadow), one color in BG palette bank 14 and colors 1-15 of bank 13, the color effect's `BLDCNT` and `BLDY` (the game's `screen_set_brightness` level) and PSG square 1, and puts them back, so the screen then shows the game's backdrop (black by default). Not restored: text already on the layer (cleared), charblock 1 (the logo's tiles stay, as a game loads its tilesets after the splash) and, if the game hadn't used text yet, charblock 0 tiles 0-95 (the font) and BG0's scroll. Every example except `hello` and `bunnymark` calls it.

## Dependencies stay behind the API

Games never need to include or call a third-party library (libtonc, or Maxmod once tracker music and sampled sound are implemented) directly; everything they need is Serval API. They may still use libtonc, which stays on the include path:

- Public headers include no third-party headers. The host build compiles them, and the examples, with no libtonc available, so CI fails if one sneaks in.
- Public names never collide with libtonc's (hence `BUTTON_*` rather than libtonc's `KEY_*` macros). `tests/rom/compat_*.c` include both in either order and must compile warning-free.
- GBA-only escape hatches live in `serval/gba.h` with a `gba_` prefix, e.g. `gba_oam_submit()` for raw OAM entries, so the portable API stays the same on every target.

New engine features are driven by the examples: when an example needs something, it gets a Serval API rather than a direct library call.

## Sprite submission model

Sprites are retained by the hardware (the PPU reads OAM every scanline), but the API rebuilds a shadow OAM each frame from draw calls, then copies it to OAM in VBlank (`frame_end()`).

- Priority among same-priority sprites is OAM order, so depth sorting is just sorting the draw list.
- Anything not submitted disappears, so there are no stale sprites.
- Flicker multiplexing and metasprites need no slot management.
- Cost: rebuilding 1 KB of OAM in ARM-mode IWRAM code is a few thousand cycles out of about 280,000 per frame.

## Persistently managed resources

Immediate-mode submission applies to OAM only. These scarce resources are managed persistently:

- Tile VRAM: sprite groups, bump-allocated in load order and released in layers by marks, with the slots of streamed groups (planned) at the top ([sprites.md](sprites.md#vram-allocation)), and one tileset per room ([tilemaps.md](tilemaps.md#tilesets); tileset groups after 1.0)
- Palette banks: a bank per palette of each sprite group, bump-allocated with the group and released by marks, and the tileset's banks. Palette writes through shadow palettes are planned (`sprite_set_colors()`, `tileset_set_colors()`), copy-on-write so that sharing identical banks between groups, after 1.0, changes nothing a game sees ([sprites.md](sprites.md#palettes))
- Background maps: streamed around the camera from map layers in ROM; the camera is a position the game sets each frame, which `sys_render()` and `sys_render_by_depth()` subtract from entity positions, while `sprite_draw()` takes screen coordinates ([tilemaps.md](tilemaps.md#streaming), [runtime-systems.md](runtime-systems.md#camera))

The 32 sprite rotation matrices are not persistent: like OAM, they are rebuilt each frame from draw calls, shared by sprites with the same angle, flips and scales ([sprites.md](sprites.md#api)).
