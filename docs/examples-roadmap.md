# Examples roadmap

Examples drive the engine: each one is written as a game developer would write it, and what it needs (or has to work around) becomes engine API. This is the list of candidates, chosen to show the engine's strengths, teach its use, and expose its weaknesses. The existing examples are in [`examples/`](../examples/) ([getting-started.md](getting-started.md#1-build-the-examples) has a reading order).

**Status:** `platformer` is implemented ([`examples/platformer`](../examples/platformer/main.c); what it exposed is [below](#what-platformer-exposed)). Everything else on this page is a candidate. ✱ marks examples blocked on engine features that are still only designs.

Each entry says what the example **shows** and what it would **expose**: missing API, limits, or workarounds that should become engine features.

## Showing the strengths

| Example | Shows | Would expose |
| --- | --- | --- |
| **Breakout** | Paddle, ball, a grid of bricks as entities, power-ups falling with gravity. Small, readable, fun; which side the ball hit a brick on comes from `body_hit_side` | Whether `body_hit_side` suits a ball that can stay inside a brick or paddle for several frames (Pong keeps its own paddle-face test; see [below](#what-platformer-exposed)), no `ecs_count` |
| **Particle fountain / fireworks** | 128 short-lived entities, gravity, rotation, PSG noise bursts: the ECS and physics at full load | The 128-entity cap, the 32 rotation matrices, no palette fades, free-list churn |
| **Vertical shoot-'em-up** | Player, bullets, enemy waves on movement patterns, a parallax starfield, the per-frame budget, depth sorting | No scrolling background layer (stars as sprites), no path or pattern helper, no hitbox separate from the sprite, the per-scanline sprite limit when bullets bunch up |
| **Rotation showcase** | Turrets tracking a cursor, a rotating radar sweep: affine sprites at their best | Rotation but no scaling API; the 32-matrix limit with many distinct angles |
| **Sound board** | A menu playing every PSG sound: duties, envelopes, sweeps, noise, melodies, songs, priorities. A reference for sound design and a regression demo | No sampled sound; no way to pause a song where it is |

## Teaching how to use it

| Example | Shows | Would expose |
| --- | --- | --- |
| **Tutorial series** (`tutorial/01`–`05`) | One concept per step: a sprite, input, entities, physics, sound and text. Could become the backbone of [getting-started.md](getting-started.md) | Gaps in the beginner path |
| **Animation playground** | A walk cycle with frame timing (`sys_animate`, `frame_times`, `SPRITE_ASSET_ANIM_ONCE`), flips, layers | `sys_animate` plays a sprite's frames in order: a ping-pong cycle or a flipped frame needs duplicated tiles (platformer's spinning gem stores its turning frame twice, once mirrored) |
| **Game states and menus** | Title, options, pause, game over, fades between screens (`screen_set_brightness`), structured without hidden objects | No menu cursor or highlight support for text (`text_set_color` changes all text at once) |
| **Custom components and systems** | Health, a damage-flash system, a homing system: `C_GAME(n)` and `ECS_FOR_EACH` | Only 15 game component bits, adding and removing components by writing `ent_mask`, no `ecs_count` |
| **Fixed-point and trig primer** | Orbits, sine waves, a clock face: 24.8 fixed point and angles | The cost of `fx_mul`/`fx_div` (64-bit software division in Thumb code) |

## Stress tests

| Example | Shows | Would expose |
| --- | --- | --- |
| **Sprite stress test** | 128 mixed-size rotating sprites with a readout of dropped draws and why (OAM full, out of matrices, per-scanline budget) | Hardware limits made visible; doubles as a regression test for the GBA and web renderers |
| **VRAM juggling** | Loading and unloading sprite groups at runtime (many enemy types across levels) | All-or-nothing residency, no allocator or defragmentation, VRAM writes outside VBlank |
| **CPU budget meter** | Spawning bodies of growing cost until frames drop, with a live CPU graph: how much game fits in a frame | The web build's CPU readout is meaningless ([platforms.md](platforms.md#web)) |
| **Collision torture** | Fast small bodies against thin walls, stacks, bodies bigger than the bounds | Bounce-only physics: no swept collision, no body-to-body response |

## Genre games

| Example | Shows | Would expose |
| --- | --- | --- |
| **`platformer`** (implemented: "Serval Dash") | A side-scrolling first level in the classic style, with original art and characters: a serval runs and jumps, hits bonus blocks and bricks from below, grows by eating a fish, stomps beetles and frogs, clears pits and stone staircases, and slides down a banner pole into a tent | Drove the tilemap, camera, scrolling and map-collision API ([tilemaps.md](tilemaps.md)); see [below](#what-platformer-exposed) |
| ✱ **Top-down RPG slice** | Map, NPCs, dialogue boxes, inventory, save and load | Needs dialogue text, save data and the scripting VM ([vm.md](vm.md)) |
| ✱ **Music player** | Maxmod modules with sound effects on top | The mixer's CPU cost against game logic; music on the web, which doesn't emulate the DMA sound channels ([platforms.md](platforms.md#web)) |

## Web and portability

| Example | Shows | Would expose |
| --- | --- | --- |
| **One game, two platforms** | The same game as a ROM and a page, with a GitHub Pages demo site (could be published by release CI) | Differences between the builds |
| **Input lab** | Held and newly pressed buttons for keyboard, gamepad and touch on the web, the D-pad on hardware | No input remapping, no touch API (the DS will need one) |
| **Determinism / replay** | Recorded input replayed to an identical result on GBA and web (`random_entropy()` depends only on the input history and `frame_count()`) | No API to record or play back input |
| **Raster effects** | Wavy water or a split HUD from mid-frame register writes | No HBlank / per-scanline API; the web draws once per frame |
| **Save and high scores** | Persistent scores | Forces the save API (cartridge SRAM on the GBA, `localStorage` on the web) |

## Order

1. **`platformer`** (done): the biggest gap between Serval and a typical engine was backgrounds, scrolling and map collision, and a first level in the classic style needs all three.
2. **Breakout**: quick, pushes for directional contacts and `ecs_count`, a good web demo.
3. **Sprite stress test**: turns hidden limits into documented numbers and tests both renderers.
4. **Tutorial series**: the biggest win for newcomers, kept honest by CI building it.

## What `platformer` exposed

Written against `map.h` as a game developer would; everything it needed beyond that was worked around in the example and listed here as candidates for engine API. Most are now closed, and the example uses the new API instead of its workarounds:

- ~~**No animation timing.**~~ Closed by `sys_animate` with `C_ANIM`, `spr_anim_time` and `SpriteAsset.frame_times` ([ecs.h](api-reference.md#ecsh)): beetles walk, gems spin, sparkles twinkle and debris tumbles by themselves. The serval's frame is still picked by the game (it depends on what the serval does), and blinking uses `SPRITE_HIDDEN`.
- ~~**No fade API, no animated tiles, an empty frame after `map_load()`.**~~ Closed by `screen_set_brightness` (fades between title, stage card, level, game over and stage clear), `tileset_set_tiles` (a glint sweeps across the bonus blocks) and `map_load` drawing at once. Palette cycling as such still has no API; animated tiles cover the bonus block.
- ~~**Text has one color and no background.**~~ Closed by `text_set_color` and `text_set_shadow`: the HUD has a drop shadow, so the clouds are back up behind it. `text_print_centered` replaced the example's own helper. Enemies walking on the highest bricks still pass behind the HUD row (a HUD band would avoid it).
- ~~**Sound: no music, no priority, the silent slide.**~~ Closed by music (`PsgSong`, `psg_music_play`), `PsgSound.priority` (jingles aren't cut short by a gem chime; screen changes stop only the music with `psg_music_stop`, so the start chime plays on) and `psg_play`'s warning for an upward slide that runs past the highest pitch. Still open: pausing a song in place (the example mutes it with `psg_music_set_volume(0)` while paused, so it keeps time), and changing the tempo of a song that is playing (a second `PsgSong` with the same tracks restarts it faster).
- ~~**Gravity for things that aren't map bodies, no maximum fall speed.**~~ Closed by map bodies bouncing and sliding (`body_bounce`, `body_friction` in `sys_map_movement`) and `body_max_fall`: popping gems, the fish and knocked-out enemies are map bodies, the serval's terminal speed is `body_max_fall`, and brick debris falls through the level under `sys_physics` with every edge of its bounds open. The extra gravity for short hops stays game logic.
- ~~**Bodies against bodies.**~~ Closed by `body_hit_side`: a stomp is `body_hit_side(player, enemy) == BODY_SIDE_BOTTOM`. It judges bodies that already overlapped before the frame by their smallest overlap, which suits stomps but not Pong's paddles: a ball that slipped past a paddle's face can later be reported as touching that face (Pong keeps its own test).
- **A body placed overlapping a solid metatile falls through it** (by design, so bodies can leave walls). A fish rising out of a block still has to be snapped exactly on top before becoming a map body.
- ~~**Randomness and replays.**~~ Closed: `random_entropy()` now depends only on the input history and `frame_count()`, so the same input plays the same game on the GBA and the web. The frogs hop at random intervals again, seeded when START is pressed.
- **Levels are text converted at boot** into cells in EWRAM. A game made in Studio Advance would get const cells in ROM from its build; nothing in the engine needs to change for that.
