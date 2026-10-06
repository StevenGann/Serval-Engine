# API reference

**Status:** implemented. Every public function, macro and type in `include/serval/`, grouped by header. Behaviour described here is what the code does today; planned API lives in the design docs and is marked there.

Include `serval/serval.h` for everything except `gba.h`, which must be included explicitly. For a guided introduction, see [getting-started.md](getting-started.md); for the design behind the API, [core-api.md](core-api.md).

**Conventions**

- Types: `u8`/`u16`/`u32`, `s8`/`s16`/`s32` and `FIXED` (`s32`), identical to libtonc's, from `platform.h`.
- `FIXED` is 24.8 fixed point: `FX(1)` (= `FX_ONE` = 256) is 1.0. Positions are in pixels, velocities in pixels per frame.
- Angles are `u16` turns: `0x10000` is a full circle (`ANGLE_DEG(90)` is a quarter turn), clockwise on screen.
- A *frame* is one 60 Hz display refresh (about 59.73 per second).
- **Misuse:** in debug builds (`SERVAL_DEBUG`: Debug and RelWithDebInfo), the calls marked *warns* below write a `serval: ...` warning to the emulator's debug log, once per problem, and count it in `debug_warning_count()`. Release builds compile the checks out; the call still fails safely as described. See [core-api.md](core-api.md#debug-builds-report-misuse).

Contents: [core.h](#coreh) · [screen.h](#screenh) · [sprites.h](#spritesh) · [ecs.h](#ecsh) · [physics.h](#physicsh) · [map.h](#maph) · [audio.h](#audioh) · [text.h](#texth) · [fixed.h](#fixedh) · [math.h](#mathh) · [path.h](#pathh) · [random.h](#randomh) · [save.h](#saveh) · [vm.h](#vmh) · [debug.h](#debugh) · [platform.h](#platformh) · [gba.h](#gbah)

## core.h

Startup, the frame loop, CPU timing and buttons.

| Function | Description |
| --- | --- |
| `void serval_init(void)` | Call once at the start of `main()`. Sets cartridge wait states (`WAITCNT` 3/1 + prefetch), enables the VBlank interrupt, hides all sprites, sets display mode 0 with sprites on (1D tile mapping), unloads sprite groups, resets the ECS, seeds `random` with its fixed default, resets `frame_count()`, the input history behind `random_entropy()` and `button_repeat()` (held buttons, default timing), turns sound on and silences the PSG channels, and starts the cycle counter (timers 2-3). |
| `void serval_splash(void)` | Shows the "made with / Serval Engine" splash (about 3 s: 500 ms fade-in, 500 ms hold, jingle, 1.5 s hold, 500 ms fade-out) and returns. Any button after the fade-in skips the rest. Call after `serval_init()`, before loading graphics. Runs its own `frame_begin()`/`frame_end()` loop. Fades in on a black backdrop. Afterwards it puts back the backdrop color (the screen then shows the game's backdrop, black by default), BG0's control register and on/off state, the blend control register and the game's brightness (`screen_set_brightness`), the two palette entries it uses (BG banks 14 and 15), whether the text layer was set up and the text shadow setting (its text is drawn without a shadow), and silences PSG square 1. Not restored: the text layer's map (anything printed with `text_print` is cleared) and, if the text layer wasn't set up before, charblock 0 tiles 0-95 (overwritten by the font) and BG0's scroll (reset to 0). |
| `void frame_begin(void)` | Starts a frame: starts the CPU cycle measurement, polls the buttons (recording changes for `random_entropy()` and counting held buttons for `button_repeat()`), and empties the sprite draw list and the rotation matrices. |
| `void frame_end(void)` | Ends a frame: hides unused hardware sprites, updates the map layers' screenblock copies for the camera (once a map layer has been loaded or tiles queued; [map.h](#maph)), records the frame's CPU cycles, waits for VBlank, copies the shadow OAM (with rotation matrices) to hardware, copies queued tileset tiles (`tileset_set_tiles`) and the changed map rows and columns to VRAM and sets the map backgrounds' registers, and advances PSG sound effects by one frame. Then counts the frame (`frame_count()`). |
| `u32 frame_count(void)` | Frames since `serval_init()`: the number of `frame_end()` calls, so 0 during the first frame. The same on every platform for the same game and input (unlike CPU cycles). |
| `u32 frame_cpu_cycles(void)` | CPU cycles the previous frame spent between `frame_begin()` and `frame_end()` (work only, not the VBlank wait). |
| `u32 frame_cpu_permille(void)` | The same in thousandths of the frame budget (500 = half; over 1000 for a frame that overran, correct even for very long frames). Print as a percentage with `text_format("%u.%u%%", p / 10, p % 10)`. |
| `u32 frame_budget_cycles(void)` | Cycles per frame: 280,896 (228 scanlines × 1,232). A frame that needs more misses the next refresh. |
| `bool button_down(u16 buttons)` | True while the button, or *any* of several OR'd buttons, is held. |
| `bool button_pressed(u16 buttons)` | True only on the frame a button went down (any of several OR'd buttons). |
| `bool button_repeat(u16 buttons)` | For menus and cursors: true on the frame a button went down, then while it stays held again after a delay and from then on at an interval (default 20 frames, then every 4: about 1/3 s, then 15 a second). Each button is counted on its own; with OR'd buttons, true if any is due. Counted from input alone, so deterministic. See the caveat below. |
| `void button_repeat_set(int delay, int interval)` | Frames from a press to its first repeat and between later repeats, each 1 to 65,535 (else ignored, *warns*). A wait already under way for a held button finishes first. `serval_init()` restores the defaults. |

Buttons: `BUTTON_A`, `BUTTON_B`, `BUTTON_SELECT`, `BUTTON_START`, `BUTTON_RIGHT`, `BUTTON_LEFT`, `BUTTON_UP`, `BUTTON_DOWN`, `BUTTON_R`, `BUTTON_L`, and `BUTTON_ANY` (all ten). Button state changes only in `frame_begin()`.

**Caveats**

- `button_repeat()` counts from the press, not from when a menu opens: a button still held from the previous screen (the A that opened the menu) keeps repeating in the menu at once. If the menu should wait for a fresh press, act on `button_pressed()` until the button has been released once.

## screen.h

| Name | Description |
| --- | --- |
| `Color` | `u16`, the target's native color format (BGR555 on the GBA). |
| `COLOR_RGB(r, g, b)` | Builds a `Color` from 0-255 components (the low 3 bits are dropped). Usable in static initializers, e.g. palettes. |
| `SCREEN_W`, `SCREEN_H` | 240 and 160, as constants. |
| `int screen_width(void)`, `int screen_height(void)` | The same values as functions. |
| `void screen_set_backdrop(Color color)` | The color shown wherever no sprite or background pixel is drawn (BG palette entry 0). Takes effect immediately. |
| `void screen_set_brightness(int level)` | Brightness of everything on screen (backgrounds, sprites, backdrop): `SCREEN_BRIGHTNESS_MIN` (−16, black) … 0 (normal, the default) … `SCREEN_BRIGHTNESS_MAX` (16, white), in 16 even steps each way. Out-of-range levels are clamped (*warns*). Stays until changed; fade by stepping it once per frame. Takes effect immediately (it writes `BLDCNT`/`BLDY`), so set it right after `frame_begin()`, while the previous frame's VBlank lasts, to change whole frames. Uses the hardware's color special effect, which does one effect at a time: no other blending while the level isn't 0 (the engine has no alpha blending yet). `serval_splash()` borrows the effect and restores the level. |

## sprites.h

Sprite assets in ROM, loaded into VRAM in groups and drawn by ID. Data format and design: [sprites.md](sprites.md).

**Types**

`SpriteAsset`, one sprite (only `.size` and `.tiles` are required):

| Field | Meaning |
| --- | --- |
| `const u32* tiles` | Pixel data: 8x8 tiles, 4 bits per pixel, 8 words per tile (low nibble = leftmost pixel). Frame after frame; within a frame, tiles row by row (1D mapping). |
| `u8 size` | `SPRITE_8x8`, `SPRITE_16x16`, `SPRITE_32x32`, `SPRITE_64x64`, `SPRITE_16x8`, `SPRITE_32x8`, `SPRITE_32x16`, `SPRITE_64x32`, `SPRITE_8x16`, `SPRITE_8x32`, `SPRITE_16x32`, `SPRITE_32x64` (width x height). Required: 0 is invalid. |
| `u8 tiles_per_frame` | 0 (the usual case): width × height / 64. If set, at least that (more leaves padding between frames); `sprite_group_load` rejects a smaller value. |
| `u8 frame_count` | Animation frames; 0 means 1. |
| `u8 order_length` | Steps in `frame_order`; 0 (the default): no sequence, frames play in order. Non-zero with a NULL `frame_order`: nothing plays (*warns*); a `frame_order` with 0 here is ignored (*warns*). |
| `u8 palette_slot` | Which of the group's palettes the sprite uses (0-based). |
| `s8 origin_x, origin_y` | Drawn position = (x, y) − origin. For a metasprite, where its pivot is drawn (negative: right of and below (x, y), e.g. the center of an entity's hitbox). |
| `u8 flags` | `SPRITE_ASSET_ANIM_ONCE`: `sys_animate` stops on the last frame (or `frame_order` step) instead of looping. `SPRITE_ASSET_METASPRITE`: made of other sprites (below). `SPRITE_ASSET_STREAMED`: not supported yet; `sprite_group_load` rejects it. |
| `const u8* frame_times` | Animation timing for `sys_animate` ([ecs.h](#ecsh)): `frame_count` entries (`order_length` with a `frame_order`), how many frames (1/60 s, 1-255) each animation frame (or step) shows; 0 holds it (the animation stops there). `NULL`: each shows for one frame. |
| `const u8* frame_order` | The sequence `sys_animate` plays, `order_length` steps: each a frame index 0-63, optionally `\| SPRITE_FRAME_FLIP_H` / `SPRITE_FRAME_FLIP_V` to draw it mirrored. Frames can repeat, play backwards or mirrored without duplicate tiles, e.g. `{0, 1, 2, 1 \| SPRITE_FRAME_FLIP_H}`. A step naming a missing frame shows frame 0 (*warns*). See [sprites.md](sprites.md#animation). |

**Metasprites** (`SPRITE_ASSET_METASPRITE`): a sprite made of others, drawn, flipped, rotated, scaled, animated and depth-sorted as one. `.pieces` (in place of `.tiles`) holds `.piece_count` (in place of `.tiles_per_frame`) `SpritePiece`s per frame: frame `f` is `pieces[f * piece_count]` onward; `.size` is unused. Each piece is `{s16 x, y; u16 sprite; u16 flags; u8 frame}`: the center of a frame of an ordinary sprite, relative to the pivot (the point drawn at (x, y) − origin), with its own `SPRITE_FLIP_H`/`_V` and `SPRITE_PALETTE(n)`. Rotation and scaling turn and scale the offsets about the pivot, so the pivot can be anywhere (a gun's mount, a hinge); whole flips mirror the offsets and toggle each piece's flips; the draw's palette, if any, replaces the pieces'; the layer is the draw's. Pieces are drawn in order, the first in front, each taking a hardware sprite (pieces sharing flips share a matrix). The pieces' own origins are ignored. Loading checks every piece names an ordinary sprite's frame (*warns* otherwise); a metasprite takes no VRAM, and the sprites of its pieces must be loaded (in its group or another) for them to be drawn (*warns*). Ordinary sprites don't pay for it: the drawing path's existing rejection test sends metasprites to their own path, out of line in ROM; plain `sys_render` costs about 5 cycles more per sprite for having that call in its loop (`sys_render_by_depth` nothing measurable).

`SpriteGroup`, sprites loaded together with the palettes they share:

| Field | Meaning |
| --- | --- |
| `const u16* sprite_ids` | IDs of its sprites; `NULL` means IDs 0 to `sprite_count` − 1. |
| `const u16* palettes` | `palette_count` banks of 16 colors each; color 0 of each bank is transparent. |
| `u8 sprite_count`, `u8 palette_count` | Up to 255 sprites; up to 16 palettes (all OBJ palette banks). |
| `u8 flags` | `SPRITE_GROUP_RESIDENT` (0, default). `SPRITE_GROUP_STREAMED` is not supported yet. |

**Functions**

| Function | Description |
| --- | --- |
| `void sprite_table_set(const SpriteAsset* const* table, u16 count)` | Registers the game's sprite table: `table[id]` is sprite `id`. Unloads all groups. At most `SPRITE_MAX` (512) entries are used; *warns* if `count` is larger. |
| `bool sprite_group_load(const SpriteGroup* group)` | Copies the group's tiles and palettes to VRAM (immediately), so its sprites can be drawn. Tiles and palette banks are allocated in load order. Returns false and loads nothing if OBJ VRAM (1,024 tiles) or palette banks (16) would run out, the group's data is incomplete (`group` is NULL; `palettes` missing while `palette_count` is non-zero; a `sprite_ids` pointer that isn't valid; a sprite ID outside the table or whose table entry is NULL; a sprite with no `tiles`, no valid size or a `tiles_per_frame` smaller than its size needs; a `palette_slot` the group lacks), or it uses an unsupported feature; *warns* with the reason. |
| `void sprite_groups_reset(void)` | Unloads every group, freeing all sprite VRAM and palette banks. |
| `void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags)` | Draws frame `frame` of a sprite this frame at (x, y) − origin. Fully off-screen sprites are skipped and use no hardware sprite. Does nothing if the sprite is not loaded or the frame doesn't exist (*warns*, once per sprite ID; IDs of 512 and above share one separate warning), or if 128 sprites were already drawn this frame (*warns*). |
| `void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags)` | Like `sprite_draw`, rotated around the sprite's center by `angle`; art drawn facing right then faces (`fx_cos(angle)`, `fx_sin(angle)`). Uses the hardware's double-size affine mode, so the sprite occupies twice its width and height for clipping. The 32 rotation matrices per frame are shared by sprites with the same angle, flips and scales; past 32, sprites are drawn unrotated (*warns*, and counted by `sprite_stats()`). Sprites that are off screen or don't fit in OAM take no matrix, and angle 0 draws exactly like `sprite_draw` (no matrix). |
| `void sprite_draw_ex(u16 sprite_id, u8 frame, int x, int y, u16 angle, FIXED scale_x, FIXED scale_y, u16 flags)` | Like `sprite_draw_rotated`, also scaled around the sprite's center along the art's own axes: `FX_ONE` is normal size, `FX(2)` twice, `FX_ONE / 2` half; a negative scale mirrors along that axis (a card flip runs `scale_x` from `FX_ONE` through 0 to `-FX_ONE`), and 0 draws nothing. Limited to ±128 (*warns*). Rotated or enlarged sprites use the double-size box, so art grown past twice its size (beyond 2 unrotated, about 1.4 at 45°) is cut off; sprites only shrunk or mirrored (angle 0, scales within ±`FX_ONE`) use their own box, at half the scanline cost. With angle 0 and both scales `FX_ONE` it draws exactly like `sprite_draw`. Shares the 32 matrices with rotation (same angle, flips and scales: one matrix), so animate a scale in a few steps. |
| `SpriteStats sprite_stats(void)` | The last frame's hardware sprite use, counted between `frame_begin()` and `frame_end()`, in release builds too: `drawn` (of 128), `matrices` (of 32), `dropped` (draws not shown because OAM was full) and `untransformed` (rotated or scaled draws shown plain because all 32 matrices were used). Zero before the first `frame_end()`. With `sprite_stats_scanlines(true)` also `cut_short` (sprites missing from at least one scanline whose sprite time ran out) and `busiest_line` (the cycles the busiest line asked for). |
| `void sprite_stats_scanlines(bool on)` | Makes `frame_end()` work out the per-scanline sprite budget (off by default). The hardware has 1,210 cycles a line for sprites (954 with DISPCNT's "H-Blank interval free"), takes them in OAM order (drawing order, first in front) and skips what doesn't fit. An ordinary sprite costs its width, an affine one 10 + 2 × its box's width (74 for a rotated 16x16, 266 for a rotated 64x64); sprites entirely off screen cost nothing. The web renderer draws by the same rules. Walks every line of every sprite: a few thousand cycles for a busy screen, counted in `frame_cpu_cycles()`, so it's a tool for a debug readout. |

Draw flags (combine with `|`): `SPRITE_FLIP_H`, `SPRITE_FLIP_V`, `SPRITE_HIDDEN` (not drawn at all and uses no hardware sprite: toggle it in `spr_flags` to make an entity blink), `SPRITE_PALETTE(n)` (draws with palette `n`, 0-14, of the sprite's group instead of its `palette_slot`, e.g. a white hit flash or another color of the same art; a palette the group doesn't have draws with the sprite's own and *warns*; change it on an entity with `spr_flags[i] = (spr_flags[i] & ~SPRITE_PALETTE_MASK) | SPRITE_PALETTE(n)`), and at most one layer flag. `SPRITE_SCREEN` (only in `spr_flags`; `sprite_draw` ignores it): the render systems draw the entity at its position on the screen, not minus the camera, so it stays put while the camera scrolls (a shooter's ship, enemies and bullets); `body_overlap()` and `body_hit_side()` add the camera when they compare such an entity with a world one. `SPRITE_SCALED` (only in `spr_flags`): the render systems draw the entity scaled by `spr_scale` on both axes, as `sprite_draw_ex()` does (a flag, so the render loops test it with the others and unscaled sprites don't pay; a `spr_scale` set without it is drawn at normal size and *warns*). `SPRITE_ANIM_FLIP_H`/`_V` are set in `spr_flags` by `sys_animate` (the flips a `frame_order` step added); drawing ignores them. Front to back, the layers are HUD (BG0, text), foreground (BG1), sprites, playfield (BG2), background (BG3); by default sprites sit between foreground and playfield. `SPRITE_ABOVE_FOREGROUND` (below the HUD only), `SPRITE_ABOVE_HUD` (above everything), `SPRITE_BEHIND_PLAYFIELD` (above BG3 only). Among sprites on the same layer, the one drawn first is in front.

## ecs.h

Fixed-pool bitmask ECS of `MAX_ENT` (128) entities. Design: [ecs.md](ecs.md).

**Components** are bits in a `u32` mask; their data lives in global arrays indexed by slot (`entity_index(e)`, or `i` in `ECS_FOR_EACH`). All are zeroed by `entity_create()`.

| Bit | Arrays | Meaning |
| --- | --- | --- |
| `C_POS` | `FIXED pos_x[], pos_y[]` | Top-left position in pixels (24.8). |
| `C_VEL` | `FIXED vel_x[], vel_y[]` | Velocity in pixels per frame (24.8). |
| `C_SPR` | `u16 spr_id[]`, `u8 spr_frame[]`, `u16 spr_flags[]`, `s16 spr_depth[]`, `u16 spr_angle[]`, `s16 spr_scale[]` | Sprite ID and frame, `sprite_draw` flags, draw depth for `sys_render_by_depth`, rotation angle (0 = unrotated), scale in 256ths (`FX_ONE` normal size; used only with `SPRITE_SCALED` in `spr_flags`). |
| `C_BODY` | `body_w`, `body_h`, `body_bounce`, `body_friction`, `body_max_fall`, `body_gravity`, `body_contact` | See [physics.h](#physicsh). |
| `C_MAPBODY` | `u8 body_contact[]` | Bit 4, defined in `map.h`: with `C_POS \| C_VEL \| C_BODY`, a body that collides with the map. See [map.h](#maph). |
| `C_ANIM` | `u8 spr_anim_time[]`, `u8 spr_anim_step[]` | Bit 5: with `C_SPR`, `sys_animate` plays the sprite's animation. `spr_anim_time` counts the frames `spr_frame` (or the step) has shown so far; `spr_anim_step` is the `frame_order` step, for sprites with one. |
| `C_PATH` | `u16 path_heading[]`, `FIXED path_speed[]`, `u8 path_step[]`, `u16 path_time[]` | Bit 6, defined in `path.h`: with `C_VEL`, `sys_path` steers the entity along a path. Add it only with `path_start()`. See [path.h](#pathh). |
| `C_GAME(n)` | (game-defined) | Bits for game components, `n` = 0-14 (there is no `C_GAME(15)`; a constant `n` outside 0-14 is a compile error, "size of array is negative"). Games keep their own arrays of `MAX_ENT`. |
| `C_ALIVE` | | Bit 31, set on every live slot by the engine. Don't pass it to `entity_create()` (*warns*). |

`u32 ent_mask[MAX_ENT]` holds each slot's mask, including `C_ALIVE` (0 for free slots). Games may add and remove their components there (`ent_mask[i] |= C_SPR`) but must keep `C_ALIVE`: a slot without it matches no system and no `ent_has()`. Create and destroy entities only with `entity_create()` and `entity_destroy()`.

**Handles:** `Entity` is a `u16`: low 8 bits slot index, high 8 bits generation (1-255). Destroying an entity, or `ecs_reset()`, makes its handles stale. Freed slots are reused oldest-first, so a stale handle could match a new entity again only after its slot has been reused 255 times. `ENTITY_NONE` (0) never refers to an entity. `entity_index(e)` and `entity_generation(e)` take a handle apart.

| Function / macro | Description |
| --- | --- |
| `Entity entity_create(u32 components)` | Creates an entity with the given component bits (`C_ALIVE` is added) and zeroed component data. Returns `ENTITY_NONE` if all 128 slots are used (*warns*, once until `ecs_reset()`). O(1). |
| `void entity_destroy(Entity e)` | Destroys the entity; does nothing for a stale handle or `ENTITY_NONE`. Also frees an entity whose `C_ALIVE` bit was cleared by hand (*warns*). Safe inside `ECS_FOR_EACH`. |
| `bool entity_alive(Entity e)` | True if the handle refers to a live entity. |
| `Entity entity_at(u32 index)` | The handle of the live entity in slot `index`, or `ENTITY_NONE` if the slot is free or out of range. E.g. `ECS_FOR_EACH(i, C_ROCK) entity_destroy(entity_at(i));`. |
| `u32 ecs_count(u32 mask)` | The number of live entities that have every component in `mask` (0 counts all live entities), e.g. `ecs_count(C_ROCK)` for the rocks left in a wave. Scans the pool as ARM code in IWRAM, about 870 cycles (an `ECS_FOR_EACH` in game code: about 4,300): call it a few times per frame, not per entity. |
| `u32 ecs_gather(u32 mask, u8* out)` | Writes the slot indices of the live entities that have every component in `mask` (0: all live entities) to `out`, in ascending order, and returns how many. `out` must hold `MAX_ENT` entries. About 1,000 cycles (IWRAM). The cheap way to loop over a kind more than once a frame, or over pairs of kinds: gather each kind once per frame and loop over the lists ([ecs.md](ecs.md#iterating)). The list is a snapshot: an entity destroyed afterwards is still in it (`ent_has(i, 0)` tells). |
| `u32 ecs_free_count(void)` | The number of entities `entity_create()` can still create, O(1). Create optional entities (effects) only while it leaves a reserve, so a full pool never *warns*: `if (ecs_free_count() > 8) spawn_spark(x, y);`. |
| `void ecs_reset(void)` | Destroys every entity (e.g. on room change); outstanding handles go stale, including those of entities whose `C_ALIVE` bit was cleared by hand. Slots are then handed out in ascending order. |
| `bool anim_finished(Entity e)` | True if `e` is alive, has `C_SPR` and `C_ANIM`, its sprite has `SPRITE_ASSET_ANIM_ONCE`, and it is on its last frame (`spr_frame == frame_count − 1`, a `frame_count` of 0 counting as 1) or, for a sprite with a `frame_order` (`order_length` set), its last step (`spr_anim_step == order_length − 1`): where `sys_animate` leaves a one-shot animation that has finished. False for looping sprites, sprite IDs outside the sprite table, and dead entities. The VM's `WAIT_ANIM` waits for it, and `vm_step()` raises Animation End when it turns true. |
| `ECS_FOR_EACH(i, mask) { ... }` | Loops `u32 i` over every live slot that has *all* components in `mask`, in slot order. A mask of 0 visits every live entity. An entity created inside the loop may or may not be visited in the same loop. |
| `bool ent_has(u32 i, u32 mask)` | True if slot `i` is alive (has `C_ALIVE`) and has every component in `mask` (mask 0 tests only that it is alive). Prefer it to `ent_mask[i] & (A \| B)`, which is true for either. `i` must be below `MAX_ENT` (not checked). |

**Systems** (call once per frame, between `frame_begin()` and `frame_end()`):

| Function | Description |
| --- | --- |
| `void sys_movement(void)` | `pos += vel` for entities with `C_POS \| C_VEL`, except map bodies (`C_MAPBODY`, moved by `sys_map_movement()`). |
| `void sys_physics(void)` | See [physics.h](#physicsh). |
| `void sys_render(void)` | `sprite_draw` (or `sprite_draw_ex`, if `spr_angle` is non-zero or `spr_flags` has `SPRITE_SCALED`) at `fx_to_int(pos)` minus the camera (`camera_set()`, [map.h](#maph); (0, 0) unless the game scrolls; not subtracted with `SPRITE_SCREEN`) for every entity with `C_POS \| C_SPR`, with its `spr_flags`. Same layer: lower slot index in front. |
| `void sys_render_by_depth(void)` | Like `sys_render`, but higher `spr_depth` is drawn in front (equal depths: lower index in front). For a top-down look, set `spr_depth` to y each frame; to put one kind of entity in front of another (balls in front of bricks), give each kind a depth. Costs depend on the depths: when they never decrease from one slot to the next (all equal, or each kind of entity created in front of the ones before) there is no sort, about 400 cycles over `sys_render` for 128 sprites; depths within 256 of each other take one counting pass over that range (two depths, 88 sprites: about 4,100; depth = y, 128 sprites: about 8,200); wider ranges two passes. |
| `void sys_animate(void)` | Plays animations of entities with `C_SPR \| C_ANIM`: each call adds a frame to `spr_anim_time`; once `spr_frame` has shown for its `frame_times` entry (one frame if `frame_times` is NULL; 0 holds the frame), moves to the next frame and zeroes `spr_anim_time`. After the last frame it loops to 0, or with `SPRITE_ASSET_ANIM_ONCE` stays on the last frame (`spr_frame == frame_count − 1` then means it is over). Run once per frame before rendering. To start an animation, or switch to another sprite's, set `spr_id`, `spr_frame = 0` and `spr_anim_time = 0`. A `spr_frame` the sprite doesn't have restarts the animation at 0 (*warns*); a `spr_id` outside the sprite table or with a NULL entry is skipped (*warns*). Reads the sprite table, so it works whether or not the sprite is loaded. **Sequences** (`order_length` set): `spr_anim_step` steps through `frame_order` instead (looping, or stopping on the last step with `SPRITE_ASSET_ANIM_ONCE`: `spr_anim_step == order_length − 1`), and every call sets `spr_frame` to the step's frame, so `spr_frame` is always the frame drawn. The step's flips are XORed with the game's in `spr_flags` (recorded in `SPRITE_ANIM_FLIP_H`/`_V`): assign `spr_flags` whole or toggle flips with `^=`. Start with `spr_anim_step = 0`, `spr_anim_time = 0`; a step past the end restarts at 0 (*warns*). |

All but `sys_animate` run as ARM code from IWRAM (`sys_animate` runs from ROM; it is cheap: a few loads per animated entity).

## physics.h

Bouncing bodies inside a world rectangle: balls, particles, debris. `sys_physics()` doesn't collide bodies with each other (the game tests pairs with `body_overlap()` and `body_hit_side()`) or with tilemaps: characters and items that walk on, land on or bounce off a tilemap are map bodies ([map.h](#maph)), which `sys_physics()` skips.

An entity with `C_POS | C_VEL | C_BODY` is a body. Body pools, zeroed by `entity_create()`:

| Array | Meaning |
| --- | --- |
| `u8 body_w[], body_h[]` | Size in pixels, kept inside the bounds. |
| `u8 body_bounce[]` | Speed kept by a floor bounce, in 256ths (224 = 7/8). At most 255/256: a floor bounce always loses a little speed. |
| `u8 body_friction[]` | Speed lost per frame while touching a floor, in 256ths (0 = none). The loss is rounded up, so any non-zero friction eventually stops a body. |
| `u16 body_max_fall[]` | Maximum fall speed in pixels per frame, fixed point like velocities (`FX(5)`, or `FX(3) / 2` for 1.5; 0 = no limit; under 256): after gravity is added, the velocity in the direction gravity pulls is limited to it, on each axis gravity acts on. A faster speed the game sets (a jump against gravity) is kept until gravity is next applied. |
| `s8 body_gravity[]` | How strongly gravity pulls the body, written with `BODY_GRAVITY(sixteenths)`: `BODY_GRAVITY(16)` normal, `(8)` half, `(0)` none, `(-16)` reversed (range −112 to 143). Stores the scale minus 16, so 0 (the default) is normal gravity. Floors follow the body's own gravity. While every body has normal gravity `sys_physics()` costs the same; otherwise it takes its general loop and moves scaled bodies out of line, in ROM (32 bodies, 4 scaled: ~4,000 cycles more). |
| `u8 body_contact[]` | With `physics_set_contacts(true)`: the walls of the bounds the body touched in the last `sys_physics()`, as `BODY_SIDE_*` bits of the body's side (`BODY_SIDE_BOTTOM` for the bottom bound...), set on the frame it bounces and on every frame it rests against a wall. `BODY_CONTACT_EXIT` plus a side: the body ended up entirely outside through that open edge this frame (set on that frame only). A wrapping axis reports nothing. The same pool as map bodies' contacts ([map.h](#maph)); read only. |

Map bodies use `body_bounce`, `body_friction`, `body_max_fall` and `body_gravity` too ([map.h](#maph)).

| Function | Description |
| --- | --- |
| `void physics_set_gravity(FIXED x, FIXED y)` | Acceleration added to every body's velocity each frame, in pixels per frame per frame (`FX_ONE / 4` = a quarter pixel). Default 0 (off). |
| `void physics_set_bounds(int left, int top, int right, int bottom)` | The rectangle bodies stay inside, in world pixels (the coordinates of `pos_x`/`pos_y`); left/top inclusive, right/bottom exclusive. Default: `(0, 0, SCREEN_W, SCREEN_H)`, the screen only while the camera is at 0, 0; in a scrolling world set them to the area bodies may use, e.g. the level's map: `physics_set_bounds(0, 0, level.width * 16, level.height * 16)`. Nothing else changes them (not `map_load()`, not `camera_set()`). Ignored if `right < left` or `bottom < top` (*warns*). A body bigger than the bounds is pinned to their left or top edge (*warns*, from `sys_physics`). |
| `void physics_set_open_edges(u32 edges)` | Edges bodies pass through instead of bouncing: `PHYSICS_EDGE_LEFT`, `_RIGHT`, `_TOP`, `_BOTTOM`, OR'd. Default 0. The game decides what happens to a body that has left. |
| `void physics_set_wrap(bool x, bool y)` | Wrap around horizontally and/or vertically instead of bouncing: a body completely past one edge reappears just outside the opposite edge (a body exactly touching the low edge from outside doesn't count as past it). A wrapping axis has no floor. Default off. |
| `void physics_set_contacts(bool on)` | Whether `sys_physics()` reports contacts in `body_contact`. Default off: contacts take its general loop, about 80 cycles more per body per frame (bunnymark's fast loop is unchanged). Switching off clears them. Use them for wall sounds and bounces instead of comparing velocity signs. |
| `void sys_physics(void)` | Run once per frame after `sys_movement()`. Skips map bodies (`C_MAPBODY`). Per axis: bounces bodies off the bounds, then applies gravity (scaled by `body_gravity`) and `body_max_fall`; sets `body_contact` if contacts are on. A wall gravity pulls toward is a floor: floor bounces keep `body_bounce`/256 of the speed, and touching it loses `body_friction`/256 of the speed along it per frame, rounded up (sliding stops below 1/16 pixel per frame); other walls bounce perfectly. A floor bounce slower than twice one frame's gravity becomes a rest: the body sits on the floor with zero velocity until gravity changes or the game moves it. |
| `bool body_overlap(u32 a, u32 b)` | True if two entities' rectangles (position + `body_w` × `body_h`) overlap; touching edges don't count. Takes slot indices. Works for any entities with `C_POS`, so a body without `C_VEL` is a static collider (e.g. a paddle the game moves). Between a screen-space entity (`SPRITE_SCREEN` in `spr_flags`) and a world one, the camera is added to the screen-space position, so a shooter's bullets hit turrets on a scrolling map; pairs of one kind compare positions as they are. |
| `u32 body_hit_side(u32 a, u32 b)` | Which side of body `a` met body `b`: 0 if they don't overlap (as `body_overlap`, touching edges don't count), otherwise exactly one of `BODY_SIDE_BOTTOM` (`a` came down onto `b`: a stomp), `BODY_SIDE_TOP`, `BODY_SIDE_LEFT`, `BODY_SIDE_RIGHT` (the same bits as `MAP_CONTACT_*`) or `BODY_SIDE_INSIDE`. Judged from their positions before this frame's movement (position minus velocity; no `C_VEL` counts as still, so a body the game moves by setting its position is judged as if it had always been there: give it `C_VEL` and let `sys_movement()` move it for its motion to count) and their motion relative to each other, so it is right for fast bodies and for two moving bodies: the side is the one `a` crossed last to overlap `b`, and an exact corner counts as top/bottom. Bodies that already overlapped before the frame get `BODY_SIDE_INSIDE` (still touching, no side crossed this frame), never a guessed side. Call it after the movement systems and before changing velocities: a velocity reversed by a bounce this frame, or a position the game set directly, makes the earlier position a guess. 0 if `a == b`. A screen-space entity against a world one is judged in the world, as `body_overlap()` does; the camera's own movement this frame doesn't count as motion. |

**Caveats**

- `BODY_CONTACT_EXIT` counts a body touching the edge from outside as exited: `pos_x + body_w == left` (or `pos_x == right`; likewise top and bottom). A body that lands exactly there exits a frame earlier than a typical `pos_x < left - body_w` check in game code says it is out; use one test or the other, not both. (Wrapping is the other way round: a body touching the low edge from outside hasn't wrapped yet.)

## map.h

Tiled backgrounds of 16×16 metatiles on BG1-BG3, a camera that scrolls them, and bodies that collide with the playfield (BG2). Maps can be far bigger than the screen: the engine streams the part around the camera into VRAM. World coordinates are pixels from the top-left of the playfield's map. Design, VRAM layout and costs: [tilemaps.md](tilemaps.md).

**Types**

`Tileset`, one room's background graphics:

| Field | Meaning |
| --- | --- |
| `const u32* tiles` | 4bpp 8x8 tiles, 8 words each (low nibble = leftmost pixel). Tile 0 should be blank: layers that don't wrap show it outside their map. |
| `u16 tile_count` | 1 to `MAP_MAX_TILES` (1024). |
| `const u16* palettes` | `palette_count` banks of 16 colors; color 0 of each is transparent and not loaded. |
| `u8 palette_count` | 0 to `MAP_MAX_PALETTES` (15), loaded into BG palette banks 0, 1, ... (bank 15 is the text layer's). |

`Metatile`, a 16×16 block: `u16 se[4]` (screen entries: top-left, top-right, bottom-left, bottom-right) and `u8 collision`. Build entries with `MAP_SE(tile, palette, flips)`, flips `MAP_SE_FLIP_H` and/or `MAP_SE_FLIP_V` (e.g. `MAP_SE(12, 0, MAP_SE_FLIP_H)`). The collision byte is a type, `MAP_EMPTY`, `MAP_SOLID` or `MAP_ONEWAY` (solid only to bodies falling onto it from above), in the low 4 bits (`MAP_TYPE(c)` extracts it), plus up to four game-defined tag bits `MAP_TAG(0)` to `MAP_TAG(3)` (bonus block, hazard...), which the engine ignores.

`MapLayer`, one background:

| Field | Meaning |
| --- | --- |
| `u16 width, height` | Size in metatiles (not 0). |
| `const u16* cells` | `width × height` metatile indices, row by row. A cell naming a metatile the layer doesn't have shows and collides as empty (*warns* at load). |
| `const Metatile* metatiles`, `u16 metatile_count` | The definitions `cells` index (at least one). |
| `u8 bg` | Background 1-3; its priority is its number (BG1 in front of sprites, BG2 the playfield, BG3 behind). |
| `u8 flags` | `MAP_LAYER_WRAP`: repeats in both directions (small parallax backgrounds); on the playfield only the graphics repeat (collision, `map_cell` and the camera clamp use the single map). Other layers show tile 0 outside their map. `MAP_LAYER_FIXED`: ignores the camera and shows the layer's top-left, moved only by `map_set_scroll()` (a HUD panel, a frame). |
| `FIXED scroll_factor` | The layer scrolls by camera × factor (`FX_ONE / 2`: half speed, a distant layer); 0 means `FX_ONE`. The playfield normally uses `FX_ONE`. Not used by fixed layers. |

| Function | Description |
| --- | --- |
| `bool tileset_load(const Tileset* tileset)` | Copies the tiles to background VRAM (charblocks 1-2) and the palettes to BG banks 0, 1, ... (colors 1-15; color 0 of bank 0 is the backdrop) immediately. Call while loading a room, before its layers show (on a shown layer the change may tear for a frame). Returns false and loads nothing if the tileset is NULL, has no tiles, more than 1,024 tiles or 15 palettes, or `palette_count` without `palettes` (*warns*). |
| `bool map_load(const MapLayer* layer)` | Shows the layer on its background, replacing any layer there: the window around the camera is drawn into VRAM and the background turned on immediately (like `tileset_load`, a VRAM write at load time: load layers while loading a room, e.g. during a fade to black), so the next frame shows it. Set the camera first; later camera moves are drawn at `frame_end()`. Loading the playfield (BG2) clears `map_set_cell()` changes and clamps the camera to it. Returns false and changes nothing for a NULL layer, a background outside 1-3, a size of 0, or missing cells or metatiles (*warns*). The data must stay valid while shown (normally `const` data in ROM). |
| `void tileset_set_tiles(u16 first, const u32* tiles, u16 count)` | Replaces `count` tiles of the loaded tileset from tile `first` with `tiles` (8 words per tile), for animated tiles (water, shimmering bonus blocks): every cell showing them changes. Queued and copied in VBlank at the next `frame_end()`, so `tiles` must stay valid until then. Up to `MAP_MAX_TILE_UPDATES` (8) calls per frame; another call for the same `first` in a frame replaces the earlier one. Ignored (*warns*) without a tileset, past its `tile_count`, for a NULL `tiles`, or when the frame's queue is full. Keep it to a few dozen tiles per frame (VBlank is short: each tile is 32 bytes to copy). `tileset_load` drops queued updates. |
| `void map_unload(u32 bg)` | Hides the layer on background `bg` (turned off at the next `frame_end()`) and forgets it, and its `map_set_scroll()` offset. Nothing if none is shown; *warns* for `bg` outside 1-3. |
| `void map_set_scroll(u32 bg, int x, int y)` | Moves background `bg`'s layer (1-3) by (x, y) pixels from where the camera puts it: the screen's top-left shows layer pixel camera × `scroll_factor` + (x, y), or (x, y) on a fixed layer. Applied at the next `frame_end()` and streamed like camera moves, so a layer can scroll by itself (a starfield drifting past while the camera stays put): add the speed every frame and let the offset count on (a jump of a screen or more redraws the window). On the playfield it moves the graphics away from collision and entities. Kept until changed or `map_unload()`, also when `map_load()` replaces the layer (set it before, like the camera). *Warns* for `bg` outside 1-3. |
| `void camera_set(int x, int y)` | Sets the world position shown at the screen's top-left, in pixels. With a playfield loaded, clamped so the view stays inside its map (0 on an axis where the map is smaller than the screen); without one, kept as given. Backgrounds scroll at the next `frame_end()`; `sys_render` and `sys_render_by_depth` subtract it from entity positions (except those with `SPRITE_SCREEN`; `sprite_draw` doesn't: it takes screen coordinates), so set it before them. Starts at (0, 0). |
| `int camera_x(void)`, `int camera_y(void)` | The camera position (after clamping). |
| `u16 map_cell(int mx, int my)` | The playfield's metatile index at metatile coordinates (mx, my), including runtime changes; 0 outside the map or without a playfield. |
| `void map_set_cell(int mx, int my, u16 metatile)` | Changes a playfield cell at runtime (broken block, open door). Up to `MAP_MAX_CHANGES` (64) changed cells per room; setting a cell back to its original metatile frees its place. Redrawn at the next `frame_end()` if on screen, and affects `map_cell`, `map_collision_at` and map bodies at once. Ignored (*warns*) when the table is full, outside the playfield, without one, or for a metatile it doesn't have. |
| `u8 map_collision_at(int x, int y)` | The playfield's collision byte at world pixel (x, y). Outside the map: `MAP_SOLID` left and right of it (at any height), `MAP_EMPTY` above and below. `MAP_EMPTY` everywhere without a playfield. |
| `void sys_map_movement(void)` | Moves map bodies (below). Run once per frame, where `sys_movement()` runs. |

**Map bodies:** entities with `C_POS | C_VEL | C_BODY | C_MAPBODY`. `sys_movement()` and `sys_physics()` skip them; `sys_map_movement()` adds gravity (`physics_set_gravity()`, scaled by `body_gravity`) to their velocity and limits it to `body_max_fall`, then moves them along x, then y, in steps of at most 7 pixels (so they never pass through a metatile), stopping flush against `MAP_SOLID` metatiles, and `MAP_ONEWAY` ones when moving down into them from above. A body covers `body_w × body_h` pixels from its position (any size up to 255×255; a size of 0 moves as 1×1 and *warns*), and can move out of a solid metatile it overlaps. An entity with `C_MAPBODY` but not all of `C_POS | C_VEL | C_BODY` doesn't move (*warns*); without a playfield, bodies move without colliding (*warns*).

`u8 body_contact[MAX_ENT]`: which sides of each map body touched the map in the last `sys_map_movement()`, `MAP_CONTACT_FLOOR`, `MAP_CONTACT_CEILING`, `MAP_CONTACT_LEFT`, `MAP_CONTACT_RIGHT` OR'd, e.g. to allow jumping only from the floor. The velocity toward a touched side becomes `-velocity * body_bounce / 256`: 0 (the default) stops the body; gems, power-ups or knocked-out enemies can bounce. Unlike `sys_physics()`'s bounds, walls and ceilings use `body_bounce` too. On a floor (the side gravity pulls toward), a rebound slower than twice one frame's gravity is a rest (zero velocity), and `body_friction` slows a body sliding along it as in `sys_physics()`. A body standing on a floor touches it every frame while gravity pulls it; one stopped by a wall touches it again only when pushed into it again.

**Caveats**

- `camera_set()` clamps to the playfield (BG2) alone, silently. Where the playfield is no bigger than the screen on an axis, even a wrapping one, the camera stays at 0 on that axis, and so do the layers it scrolls. To drift a small or wrapping layer (a starfield, clouds) on such a screen, use `map_set_scroll()`.

## audio.h

Sound effects and music on the GBA's tone generators (PSG): square 1, square 2 and noise. They cost almost no CPU while playing. Tracker music and sampled sounds are planned ([audio.md](audio.md)).

`PsgSound` (a minimal sound needs `.frequency` and `.frames`; fields left out take the defaults shown):

| Field | Meaning |
| --- | --- |
| `u8 channel` | `PSG_SQUARE1` (0, default), `PSG_SQUARE2` or `PSG_NOISE`. Each channel plays one sound at a time; a new one replaces it unless it has a lower `priority`. |
| `u8 duty` | Square tone color: `PSG_DUTY_12` (1), `PSG_DUTY_25` (2), `PSG_DUTY_50` (3), `PSG_DUTY_75` (4). 0 means `PSG_DUTY_50`. Other values *warn*. Ignored on the noise channel. |
| `u8 volume` | Starting volume 1-15; 0 means 15, except for a sound that fades in, which then starts from silence. Above 15 is clamped (*warns*). |
| `s8 fade` | Envelope: −1 (fast) to −7 (slow) fades out, 1 (fast) to 7 (slow) fades in, 0 holds. Outside −7 to 7 is clamped (*warns*). |
| `s8 slide` | `PSG_SQUARE1` only: pitch slide, −1 (fast) to −7 (slow) down, 1 to 7 up, 0 none. Outside −7 to 7 is clamped (*warns*); ignored on other channels (*warns*). |
| `u8 slide_size` | `PSG_SQUARE1`: step size of the slide, 1 (big) to 7 (small); 0 means 1. Above 7 is clamped (*warns*). An upward slide that passes the highest pitch silences the channel; if that happens before the sound ends, `psg_play` *warns* (debug builds), naming the frame. |
| `u16 frequency` | Pitch in Hz: squares 64-65,535 (lower is raised to 64); noise 4-65,535 (the closest of the channel's coarse rates; higher is hissier). |
| `u16 frames` | How long it plays (each note, for a melody). 0: until the envelope fades it out (it holds its channel until then) or it is replaced. Required for a melody: one with `frames` 0 doesn't play (*warns*). |
| `const u16* notes`, `u8 note_count` | Optional melody: `note_count` frequencies in Hz (0 = rest), played in turn instead of `.frequency`. |
| `u8 priority` | 0 (default) to 255. While the sound plays, `psg_play` of a sound with lower priority on its channel does nothing; equal or higher priority replaces it. On a channel the music uses, the sound plays only if its priority is at least the song's `priority`. |

| Function | Description |
| --- | --- |
| `void psg_table_set(const PsgSound* const* table, u16 count)` | Registers the game's sound table: `table[id]` is sound `id`. Stops the sound effects playing (not the music). An invalid `table` pointer registers no sounds (*warns*). |
| `void psg_play(u16 sound_id)` | Plays a sound, replacing whatever its channel was playing, unless that is a sound of higher priority still playing, or music of higher priority. Does nothing (*warns*) if the ID is outside the table, its table entry is NULL, its channel is invalid, it has a `note_count` but no `notes`, or it is a melody with `frames` 0. Each kind of problem is reported once per `psg_table_set()`. Notes and lengths advance in `frame_end()`. |
| `void psg_stop_all(void)` | Silences every PSG channel: sound effects and music. |

**Music.** `PsgSong`: up to one track per channel, played together; each track loops on its own. Notes are `PSG_C0`…`PSG_B10` (`PSG_C4` = 60 is middle C, `PSG_A4` 440 Hz, sharps `PSG_CS4`…, an octave is 12) and `PSG_REST` (0). Square channels play `PSG_C2` and up; on the noise channel a note picks the noise rate closest to its pitch (drums).

| Type / field | Meaning |
| --- | --- |
| `PsgNote {u8 note; u8 length;}` | A note or `PSG_REST`, held for `length` ticks (0: the track's `length`). Notes above `PSG_B10` play as it, notes below `PSG_C2` on a square as 64 Hz (*warn*). |
| `PsgTrack.channel` | `PSG_SQUARE1` (default), `PSG_SQUARE2` or `PSG_NOISE`. A track on an invalid channel, or on a channel an earlier track uses, is left out (*warns*). |
| `PsgTrack.duty`, `.volume`, `.fade` | As in `PsgSound`, for each note. |
| `PsgTrack.length` | Ticks of notes whose `length` is 0; 0 means one beat (`ticks_per_beat`). |
| `PsgTrack.notes`, `.note_count` | The notes, played in turn. A track without notes is left out (*warns*). |
| `PsgTrack.loop` | Index of the note the track loops back to (0: the start); `PSG_NO_LOOP` plays it once. An index past the end loops from the start (*warns*). |
| `PsgSong.tempo` | Beats per minute; 0 means 120. |
| `PsgSong.ticks_per_beat` | 0 means 4 (a tick is a 16th note). Ticks fall on the closest frame; at most one per frame (3583 a minute; faster is clamped, *warns*). |
| `PsgSong.priority` | Sound effects below it don't play on the music's channels (0: every one does). |
| `PsgSong.tracks`, `.track_count` | The tracks, one per channel (so at most 3 play). A `track_count` without `tracks` plays nothing (*warns*). |

| Function | Description |
| --- | --- |
| `void psg_music_play(const PsgSong* song)` | Starts a song from the beginning at its own tempo, replacing the one playing (paused or not). A NULL song plays nothing (*warns*). Sound effects playing keep their channels. A sound effect that takes over one of the music's channels (see `priority`) plays over it while the music keeps time; when it ends, a held note (track `fade` ≥ 0) comes back at once, a fading track with its next note. Advanced in `frame_end()`. |
| `void psg_music_stop(void)` | Stops the music; sound effects play on. |
| `bool psg_music_playing(void)` | True while a song plays, paused or not: until stopped, or until every track of a song that doesn't loop has ended. |
| `void psg_music_pause(void)` | Pauses the music where it is: its time stands still and its channels go quiet. Sound effects play on, on every channel, whatever their priority. Does nothing without a song or when already paused. |
| `void psg_music_resume(void)` | Resumes paused music exactly where it stopped: held notes start again at once, fading tracks with their next note. Does nothing if not paused. `psg_music_play`, `psg_music_stop` and `psg_stop_all` also end a pause. |
| `bool psg_music_paused(void)` | True while the music is paused. |
| `void psg_music_set_tempo(u16 tempo)` | Changes the tempo of the song playing (beats per minute; 0: the song's own `tempo`) from where it is, with no jump and no drift. `psg_music_play` resets it. Faster than a tick per frame is clamped (*warns*); without a song it does nothing (*warns*). |
| `void psg_music_set_volume(u8 volume)` | 0 (silent) to 15 (default; above is clamped, *warns*). Scales each note's starting volume from each channel's next note; sound effects are unaffected. Tracks that fade in still rise to full volume, unless the volume is 0. |

## text.h

A fixed 8x8 font (libtonc's `sys8`, printable ASCII) on a `TEXT_COLS` × `TEXT_ROWS` (30 × 20) grid on BG0, white by default, optionally with a drop shadow, in up to `TEXT_STYLES` (4) styles: text and shadow color pairs, so a line can stand out (`TEXT_HIGHLIGHT`) while other text keeps its color. The first text call sets up the font and turns BG0 on. Uses charblock 0, screenblock 31 and BG palette bank 15 only (map layers keep banks 0-14): style *n* draws with glyph tiles 96*n* to 96*n* + 95 and colors 2*n* + 1 (text) and 2*n* + 2 (shadow), so `TEXT_NORMAL` is tiles 0-95 and colors 1-2, `TEXT_HIGHLIGHT` tiles 96-191 and colors 3-4; colors 9-15 and tiles 384-511 stay unused. A style's 96 tiles (3 KB) are written the first time it is used.

| Function | Description |
| --- | --- |
| `void text_print(int col, int row, const char* s)` | Writes `s` from a character cell. Clipped at the edges (cells with negative `col` are skipped; a row outside 0-19 prints nothing). Characters outside printable ASCII show as `?`. |
| `void text_print_line(int col, int row, const char* s)` | Like `text_print`, then blanks the rest of the row, so shorter text leaves nothing behind. Use it for HUD lines redrawn with changing content. |
| `void text_print_centered(int row, const char* s)` | Blanks the row and writes `s` centered on it, from column (`TEXT_COLS` − length) / 2 (one column left of center for an odd leftover). A string longer than 30 characters loses characters at both ends. |
| `void text_print_centered_in(int col, int width, int row, const char* s)` | Like `text_print_centered` within `width` columns from `col` (a field beside a HUD panel, say): blanks those cells of the row and writes `s` from column `col` + (`width` − length) / 2. Nothing outside the columns changes; a string wider than `width` loses characters at both ends. A negative `width` changes nothing (*warns*). |
| `void text_clear(void)` | Clears every cell (and sets the layer up if needed). |
| `void text_clear_area(int col, int row, int width, int height)` | Clears a `width` × `height` rectangle of cells from (`col`, `row`); cells off the screen are skipped. A negative size changes nothing (*warns*). |
| `void text_set_style(int style)` | The style of text printed from now on, by any print function: `TEXT_NORMAL` (0, the default), `TEXT_HIGHLIGHT` (1), 2 or 3. Text already shown keeps its style. Out of range: ignored (*warns*). |
| `void text_set_style_color(int style, Color text, Color shadow)` | A style's text and shadow colors, for all text shown in it, including what is already shown. Defaults: white (`TEXT_NORMAL`), yellow (`TEXT_HIGHLIGHT`), light red (2) and grey (3) text, all with black shadows. Before the layer is set up, applied when it is. Out of range: ignored (*warns*). |
| `void text_set_color(Color text, Color shadow)` | `text_set_style_color(TEXT_NORMAL, text, shadow)`: the colors of all text in the normal style (all text, unless the game uses styles), including what is already shown (BG bank 15 colors 1 and 2). Default: white text, black shadow. |
| `void text_set_shadow(bool on)` | Turns the drop shadow on or off for all text on the layer: each glyph gets a copy of itself one pixel right and one down, in the shadow color, behind it and clipped to its 8×8 cell, so light text stays readable over light art. Applies to every style. Rewrites the font tiles (3 KB of VRAM per style used) when the setting changes. Default: off. |
| `const char* text_format(const char* fmt, ...)` | printf-style formatting without a C library. Conversions `%d %i %u %x %s %c %%`; flags `-` (left-align) and `0` (zero-pad, numbers only); a field width (`%5d`, `%03u`, `%-10s`; capped at `TEXT_FORMAT_MAX`); a precision for `%s` only: `%.3s` prints at most 3 characters and reads no further, so a `char[3]` without a terminating zero (initials) prints without a copy, and `%.*s` takes the count as an `int` argument before the string (negative: no limit). A precision on another conversion is ignored (*warns*, once). Length modifiers `h`, `hh`, `l` and `ll` are accepted (`%ld` reads a `long`; `%lld` prints only the low 32 bits, *warns*). `%d %i %u %x` take any 32-bit integer (`int`, `unsigned`, `s32`, `u32`); `%x` is lowercase. Other conversions (such as `%f`) are printed as written, still consuming their argument so later ones print correctly (*warns*, once). Returns one of four rotating static buffers of `TEXT_FORMAT_MAX` (128) bytes, so up to four results can be used together; longer output is truncated. Not type-checked by the compiler: a NULL `%s` prints `(null)`; on the GBA, debug builds print `(?)` and *warn* for a `%s` argument that isn't a pointer. |

Text writes (and color, style and shadow changes) go to VRAM immediately; they are not tied to `frame_end()`.

## fixed.h

| Name | Description |
| --- | --- |
| `FX_SHIFT`, `FX_ONE` | 8 and 256: the 24.8 format. |
| `FX(n)` | Whole number to `FIXED`; usable in constant expressions. For fractions, divide: `FX(7) / 8`, `FX_ONE / 4`. |
| `int fx_to_int(FIXED f)` | To a whole number, rounding toward negative infinity. |

## math.h

Prefixed (`int_`, `fx_`) to avoid libtonc's `min`/`max`/`clamp`.

| Function / macro | Description |
| --- | --- |
| `int int_min(int a, int b)`, `int int_max(int a, int b)`, `int int_abs(int v)` | As named. |
| `int int_clamp(int v, int lo, int hi)` | `v` limited to `[lo, hi]`, both inclusive. |
| `FIXED fx_mul(FIXED a, FIXED b)` | `a × b` in 24.8 (64-bit intermediate). E.g. `fx_mul(speed, FX(7) / 8)`. |
| `FIXED fx_div(FIXED a, FIXED b)` | `a / b` in 24.8. `b` must not be 0 (not checked). Slow: no hardware divider; multiply by a constant reciprocal where you can. |
| `ANGLE_DEG(d)` | Degrees to a `u16` angle: `ANGLE_DEG(90)` = `0x4000`. Clockwise on screen (y points down). |
| `FIXED fx_sin(u16 angle)`, `FIXED fx_cos(u16 angle)` | Sine and cosine in 24.8 (−256 to 256), from a table with 1,024 steps per turn. A heading `a`, clockwise from "right", is the direction (`fx_cos(a)`, `fx_sin(a)`). |
| `u16 angle_of(FIXED dx, FIXED dy)` | The heading of the vector (`dx`, `dy`), the inverse of `fx_cos`/`fx_sin` (atan2): 0 = right, `ANGLE_DEG(90)` = down, `ANGLE_DEG(180)` = left, `ANGLE_DEG(270)` = up. Aim with `angle_of(tx - x, ty - y)`. Within 0.1° for any `FIXED` values, tiny or huge (0.07° worst measured); (0, 0) gives 0. No division: shifts, one multiply and two small tables. About 310 cycles from ROM. |
| `FIXED fx_length(FIXED dx, FIXED dy)` | The length of (`dx`, `dy`), e.g. a distance, within 0.1% plus 1/256 pixel. Saturates at the largest `FIXED` instead of overflowing (squaring 24.8 distances overflows past about 180 pixels). About 320 cycles from ROM. |

## path.h

Movement patterns as data, "turtle" style: each step moves the entity along its heading for some frames, turning by a fixed amount and changing speed by a fixed amount every frame. Enemy formations and swoops, patrols, circling, weaving, stop-and-go. `sys_path()` turns it into velocity, so pathed entities move, collide and render like any other. Design: [runtime-systems.md](runtime-systems.md#paths).

```c
static const PathStep swoop_steps[] = {
    {.frames = 40, .speed = FX(2)},                       // straight down
    {.frames = 90, .speed = FX(2), .turn = ANGLE_DEG(2)}, // U-turn (180 degrees)
    {.speed = FX(3)},                                     // away, forever
};
static const Path swoop = {PATH_STEPS(swoop_steps), .heading = ANGLE_DEG(90)};

path_start(e, &swoop, from_right ? PATH_MIRROR_X : 0); // e has C_VEL
```

| Type / macro | Description |
| --- | --- |
| `PathStep` | One step (write it with designated initializers: see the caveat below): `u16 frames` (how long; **0 = forever**, the path never ends), `s32 turn` (heading change per frame in `u16` angle units, clockwise on screen; `ANGLE_DEG(-2)` or `-ANGLE_DEG(2)` turns the other way), `FIXED speed` (pixels per frame along the heading when the step starts; negative moves backward; 0 stands still, so every moving step gives one), `FIXED accel` (speed change per frame). Every frame of a step applies the turn and the acceleration first, then sets the velocity: after the step, the heading has turned by `frames × turn` and the speed is `speed + frames × accel`. |
| `Path` | `const PathStep* steps`, `u8 step_count` (1-255), `bool loop` and `u8 loop_step` (after the last step, go back to step `loop_step`; without `loop` the path ends), `u16 heading` (the starting heading). |
| `PATH_STEPS(array)` | `.steps` and `.step_count` for an array (not a pointer) of steps, inside a `Path` initializer. More than 255 steps is a compile error. |
| `PATH_MIRROR_X`, `PATH_MIRROR_Y` | `path_start()` flags: mirror left-right (heading `h` becomes 180° − `h`) and/or top-bottom (−`h`). Either one also reverses the turns; both together rotate the path half a turn. |
| `C_PATH` | Bit 6: the entity follows a path. Added by `path_start()`, removed by `path_stop()` or when the path ends. Debug builds catch a `C_PATH` added by hand: `sys_path` removes it (*warns*). |

Per-entity state, indexed by slot and set by `path_start()`: `u16 path_heading[]` and `FIXED path_speed[]` (the current heading and speed; games may change them and the path goes on from there, e.g. `path_heading[i] = angle_of(dx, dy)` after `path_start()` aims the whole path, since later turns are relative), `u8 path_step[]` and `u16 path_time[]` (the current step and the frames done in it; read them to time events, such as firing at the bottom of a swoop). `spr_angle[i] = path_heading[i]` turns a sprite drawn facing right along its path.

| Function | Description |
| --- | --- |
| `void path_start(Entity e, const Path* path, u32 flags)` | Starts `e` on `path` from its first step at the path's heading, mirrored by `flags`, replacing any path it was following, and adds `C_PATH`. `path` must stay valid while it runs (keep paths `static const`). Ignored for a dead entity, a NULL or empty path, or a `loop_step` past the last step (*warns*). An entity without `C_VEL` starts, but doesn't move (*warns*). |
| `void path_stop(Entity e)` | Removes `C_PATH`; the velocity stays as it was. |
| `bool path_active(Entity e)` | True while `e` is alive and following a path: false once a path without `loop` has done its last step, or after `path_stop()`. |
| `void sys_path(void)` | Advances every entity with `C_PATH \| C_VEL` one frame and sets `vel_x`, `vel_y` from its heading and speed. Run once per frame before `sys_movement()`. The velocity is recomputed only when the heading or speed changed (so a velocity the game sets lasts through a straight, constant-speed stretch). When a path without `loop` finishes, removes `C_PATH`: the entity keeps its last velocity and flies on straight. Speeds are capped at `FX(4096)`. A `path_step` past the end stops the path (*warns*). Runs from ROM; per pathed entity per frame, about 160 cycles on straight, constant-speed stretches and about 450 while turning or accelerating (release build; debug builds add about 150 for the `C_PATH` check), on top of about 10,000 for the loop over the 128 slots. |

**Caveats**

- Write `PathStep` tables with designated initializers, as in the example. A positional initializer that leaves fields out, such as `{40, 0, FX(2)}`, triggers `-Wmissing-field-initializers` under `-Wextra`, an error with warnings as errors; a positional one must give all four fields (`{40, 0, FX(2), 0}`).

## random.h

Deterministic xorshift32: the same seed always gives the same sequence. Not for security.

| Function | Description |
| --- | --- |
| `void random_seed(u32 seed)` | Restarts the sequence. 0 is replaced by a fixed non-zero value, which `serval_init()` also uses, so a game that never seeds plays the same on every boot. |
| `u32 random_entropy(void)` | A value that varies with the player, for seeding: a hash of `frame_count()` and the button history since `serval_init()` (which buttons, and on exactly which frame each was pressed or released). Not CPU timing: the same input gives the same value on the GBA and the web and in every build, so recorded input replays the same game. Call it after waiting for the player (e.g. when START is pressed): `random_seed(random_entropy())`. Before any input it returns the same value every time. Changes only from frame to frame. |
| `u32 random_u32(void)` | The next 32 random bits. |
| `int random_range(int lo, int hi)` | A random integer in `[lo, hi]`, both inclusive (scaled by multiplication: no division, no modulo bias). Any `int` range works, even the full one. Returns `lo` if `hi <= lo` (*warns* if `hi < lo`). |

## save.h

Save data that survives power-off: numbered slots, each holding one block of game data (typically a struct), checked with a CRC-32 and tagged with the game's own version number. GBA: the cartridge's save memory of the type the game picks with `serval_add_rom(... SAVE <type>)`: `SRAM` (32 KiB battery-backed, the default), `FLASH64K`, `FLASH128K`, `EEPROM8K` or `EEPROM512`. Linking the save code puts the type's ID string in the ROM (`SRAM_V113`, `FLASH512_V131`, `FLASH1M_V103`, `EEPROM_V124`), so emulators and flash carts provide that memory; mGBA writes it to a `.sav` file next to the ROM. The type sets the slots: 8 of up to 2000 bytes for SRAM and Flash, 8 of 496 bytes for `EEPROM8K`, 2 of 112 bytes for `EEPROM512`. `SAVE_SLOTS` (8) and `SAVE_SLOT_MAX` (2000) are the largest of any type; `save_slot_count()` and `save_slot_capacity()` are the game's. Flash: an Atmel chip, or a 64 KiB chip in a `FLASH128K` game, isn't usable: every slot reads as `SAVE_EMPTY` and `save_write` returns false (*warns*); an unknown chip ID is used like the known ones (*warns*). Web: the browser's `localStorage`, with the same slots as on the GBA ([platforms.md](platforms.md#web)). Types, format, guarantees and costs: [runtime-systems.md](runtime-systems.md#save-data).

| Function | Description |
| --- | --- |
| `bool save_write(u32 slot, const void* data, u32 size, u16 version)` | Saves `size` bytes (1 to `save_slot_capacity()`) to `slot` (0 to `save_slot_count() - 1`), tagged with `version` (any number the game picks; raise it when the saved struct changes). Returns true once the save is written and read back intact. The slot's previous save stays readable until then: a power loss mid-write leaves it (or an empty slot), never a corrupt one. Returns false, keeping the previous save, if the memory didn't keep the data (no save memory of this type, or a Flash or EEPROM timeout; *warns*), or for a bad slot, a size of 0 or over the capacity, or a data pointer that isn't one (*warns*). GBA cost with SRAM: about 1.4 ms for 100 bytes, 22 ms (1.3 frames) for 2000; Flash about 17 ms for 100 bytes, 130 ms for 2000; EEPROM about 7 ms per 8 bytes (115 ms for 100): call it at a natural pause, not every frame. |
| `int save_read(u32 slot, void* data, u32 size, u16 version)` | Copies the slot's save into `data` and returns `SAVE_OK` only if it is intact and of exactly this `version` and `size`. Otherwise `data` is untouched and the result says why: `SAVE_EMPTY` (never saved, erased, or blank memory), `SAVE_CORRUPT` (a save is there but its checksum fails: treat it as empty), `SAVE_OTHER_VERSION` (an intact save of another version or size: read it with the struct and version it was written with, given by `save_slot_version()` and `save_slot_size()`, and convert it). A bad slot or data pointer returns `SAVE_EMPTY` (*warns*); a size of 0 or over the capacity never matches (*warns*). GBA cost: about 0.7 ms for 100 bytes, 11 ms for 2000 (SRAM and Flash); EEPROM about 6 ms for 100 bytes. |
| `u32 save_slot_count(void)` | The game's number of slots: 8 (`SAVE_SLOTS`), or 2 with `EEPROM512`. |
| `u32 save_slot_capacity(void)` | The most bytes one slot holds: 2000 (`SAVE_SLOT_MAX`) with SRAM and Flash, 496 with `EEPROM8K`, 112 with `EEPROM512`. |
| `u16 save_slot_version(u32 slot)` | The version of the slot's intact save; 0 if it holds none (empty or corrupt) or for a bad slot (*warns*). |
| `u32 save_slot_size(u32 slot)` | The size in bytes of the slot's intact save; 0 if it holds none or for a bad slot (*warns*). Never 0 for a save, so it also tells whether the slot holds one. |
| `void save_erase(u32 slot)` | Empties the slot. All or nothing on power loss. Nothing for an empty slot; *warns* for a bad slot. |

Typical use, with a version to raise whenever `Scores` changes:

```c
#define SCORES_SLOT 0
#define SCORES_VERSION 1
Scores scores;
if (save_read(SCORES_SLOT, &scores, sizeof scores, SCORES_VERSION) != SAVE_OK)
    scores = default_scores; // first boot, erased, damaged or an old layout
...
save_write(SCORES_SLOT, &scores, sizeof scores, SCORES_VERSION); // after a game over
```

## vm.h

The bytecode VM: objects with event handlers (GameMaker's model), run as cooperative scripts with no allocation. The editor's script compiler emits one **script blob** holding every object, handler and string; its format, the opcodes, the property and engine-call pages and every scheduling rule are specified in [vm.md](vm.md), and `vm.h` names all their numbers (`VM_OP_*`, `VM_EV_*`, `VM_P_*`, `VM_SYS_*`, `VM_FORMAT_VERSION`, `VM_CELL_BYTES`, `VM_HEADER_SIZE`, `VM_OBJECT_SIZE`). An *instance* is an entity attached to an object; scripts can also run as *threads* with no entity (`vm_start`). Each entity runs at most one script at a time.

```c
vm_load(game_scripts, sizeof game_scripts);
vm_bind(&(VmBindings){.songs = songs, .song_count = 2, .paths = paths, .path_count = 3});
Entity e = entity_create(C_POS | C_SPR);
vm_attach(e, OBJ_PLAYER); // its Create handler runs in the next phase
for (;;) {
    frame_begin();
    vm_step();   // waits, queued events, Step handlers
    sys_path();
    sys_movement();
    sys_physics();
    if (body_overlap(entity_index(e), entity_index(coin)))
        vm_event(e, coin, VM_EV_COLLISION);
    vm_events(); // the collision handlers, this frame
    sys_animate();
    sys_render();
    frame_end();
}
```

**Limits** (compile-time): `VM_CONTEXTS` 32 scripts running or waiting at once, `VM_STACK` 8 cells of value stack and `VM_CALLS` 4 levels of `CALL` per script, `VM_LOCALS` 8 locals per script (zeroed when it starts), `VM_GLOBALS` 256 globals shared by all scripts, `VM_EVENT_QUEUE` 32 queued events, `VM_OPS_PER_SLICE` 256 opcodes per script per phase. Contexts, globals, bindings and the queue take about 4.9 KB of EWRAM.

**Events** (`VM_EV_*`, an object's handler slots): `VM_EV_CREATE`, `VM_EV_STEP`, `VM_EV_DESTROY`, `VM_EV_COLLISION`, `VM_EV_ANIM_END`, `VM_EV_ROOM_START`; `VM_EV_COUNT` is 6. Where they come from: Create is queued by `vm_attach` (and `SPAWN`) and always runs before the entity's first Step; Step runs in every `vm_step()`; Destroy comes from `KILL`, `vm_kill` or `vm_event`; Collision from game code (`vm_event`); Animation End from `vm_step()` itself, when `anim_finished()` turns true for an entity whose object has the handler (once per finish, so an animation the game restarts raises it again; one that `sys_animate` finishes raises it in the next `vm_step()`); Room Start from the game, with `vm_start(obj, VM_EV_ROOM_START)` or `vm_event(e, ENTITY_NONE, VM_EV_ROOM_START)` when it builds a room.

**Properties** (`GETP`/`SETP`, `VM_P_*`) are the ECS arrays of the same names: `pos_x`, `pos_y`, `vel_x`, `vel_y`, the sprite's `spr_id`, `spr_frame`, `spr_flags`, `spr_angle`, `spr_depth` and `spr_scale`, and the body size `body_w` and `body_h` (what `body_overlap` tests, so a script can size an entity it spawns). Writes truncate to the array's type. A property whose component the entity lacks (`C_POS`, `C_VEL`, `C_SPR`, or `C_BODY` for the body size) still reads and writes the array (*warns*).

**Engine calls** (`SYS`, `VM_SYS_*`; arguments pushed in order, the last on top): `psg_play`, `psg_music_play` (a bound song), `psg_music_stop`, `psg_music_pause`, `psg_music_resume`, `camera_set`, `text_print` (a string of the blob), `random_range`, `button_down`, `button_pressed`, `screen_set_brightness`, `path_start` (a bound path) and `text_print_number` (a value, in decimal). Like `text_print`, `text_print_number` doesn't blank what was printed there before.

`VmBindings` holds what the `SYS` engine calls reach by index, since scripts hold no pointers: `const PsgSong* const* songs` and `u16 song_count` (`VM_SYS_MUSIC_PLAY`), `const Path* const* paths` and `u16 path_count` (`VM_SYS_PATH_START`). The arrays must stay valid while scripts run.

| Function | Description |
| --- | --- |
| `bool vm_load(const u8* blob, u32 size)` | Validates the blob (magic `"SVMB"`, format version 1, 4-byte cells, at most 256 globals, its object and string tables inside it, every handler and string offset inside it and past the tables, every string's NUL inside it) and makes it the running scripts: halts every script, detaches every entity, empties the event queue and zeroes the globals. The blob is read in place (byte by byte: any alignment), so keep it valid while it is loaded. Returns false for an invalid blob (*warns*, naming the first problem), which also unloads the previous one: nothing runs. Call it after `ecs_reset()`, between frames: called from inside `vm_step()` or `vm_events()` it does nothing and returns false (*warns*). |
| `bool vm_reload(const u8* blob, u32 size)` | Like `vm_load`, for hot reload (the debug link): keeps the globals' values if the new blob declares the same global count (else zeroes them, *warns*), and keeps entities attached to objects the new blob still has (without a new Create; one whose Create was still queued loses it with the queue and runs Step from the next `vm_step()`). Scripts are halted and the queue emptied. Like `vm_load`, refused during `vm_step()` or `vm_events()` (*warns*). |
| `void vm_unload(void)` | Halts every script, detaches every entity and empties the queue. The globals keep their values. Ignored during `vm_step()` or `vm_events()` (*warns*). |
| `void vm_bind(const VmBindings* bindings)` | Registers the songs and paths `SYS` calls reach by index (copies the struct; NULL clears them). |
| `void vm_attach(Entity e, u16 object)` | Attaches `e` to an object and queues its Create event (even when the object has no Create handler). Its Step handler runs only once that Create has been dispatched: attached before `vm_step()`, it runs Create then (if Create doesn't wait) its first Step in that `vm_step()`; attached between `vm_step()` and `vm_events()`, Create in `vm_events()` and Step from the next frame. Re-attaching an attached entity halts its script first. Ignored for a dead entity, an object the blob doesn't have, or no blob loaded (*warns*). |
| `void vm_detach(Entity e)` | Halts `e`'s script and detaches it, without a Destroy event. Nothing for an entity that isn't attached. |
| `void vm_kill(Entity e)` | Destroys an attached entity the script way: halts its script, runs its Destroy handler (if any) to completion, detaches it, then `entity_destroy(e)`. A wait in the Destroy handler halts it (*warns*). Unattached live entities are just destroyed; dead handles are ignored; `ENTITY_NONE` *warns*. Called during `vm_step()` or `vm_events()`, it is queued like the `KILL` opcode. Use it, or `vm_detach`, instead of `entity_destroy` for attached entities. |
| `int vm_start(u16 object, u8 event)` | Starts the object's handler for `event` as a thread with no entity (`SELF` warns and pushes 0); it first runs in the next `vm_step()`. Returns the context index, or −1 if there is no blob, no such object or handler, or no free context (*warns*). |
| `void vm_event(Entity e, Entity other, u8 event)` | Queues an event for `e`, e.g. `vm_event(a, b, VM_EV_COLLISION)` after `body_overlap`; `OTHER` gives `other` to the handler. When drained, it runs if `e` is alive and attached and its object has a handler for the event, and is dropped (*warns*) if `e`'s script is still running or waiting. `VM_EV_DESTROY` behaves exactly like `KILL`: queued, then what `vm_kill` does when drained, with `OTHER` 0 in the Destroy handler; for `ENTITY_NONE` it is ignored (*warns*). An event number of `VM_EV_COUNT` or more is ignored (*warns*). |
| `void vm_step(void)` | Phase 1, after `frame_begin()` and before movement: resumes waiting scripts (in context order; `WAIT n` counts down one frame per call), queues Animation End events, runs queued events (oldest first, including events queued meanwhile, such as Creates), runs the Step handler of every attached entity with no running script whose Create has been dispatched (in entity order), then runs the events the Step handlers queued (an entity a Step handler `SPAWN`s runs Create here and its first Step next frame). |
| `void vm_events(void)` | Phase 2, after movement and physics: runs queued events, e.g. the collisions game code reported this frame. Never resumes waiting scripts. |
| `s32 vm_global(u16 index)` | Global `index` (0-255). Others return 0 (*warns*). |
| `void vm_set_global(u16 index, s32 value)` | Sets global `index` (0-255). Others are ignored (*warns*). |
| `u32 vm_ops_this_frame(void)` | Opcodes run since the latest `vm_step()` began, both phases (and `vm_kill` Destroy handlers in between). |
| `bool vm_idle(void)` | True when no script is running, ready or waiting and no event is queued. |

**Scripts that go wrong** never stop the game: each kind of problem warns the first time it happens after a `vm_load`, `vm_reload` or `vm_unload`, then stays quiet until the next one (so a fault repeated every frame warns once), and fails safe. Stack overflow or underflow, `CALL` nested deeper than 4, an unknown opcode or `SYS` call, and a jump, call or handler end that leaves the blob halt only that script. Dividing by zero (`DIV`, `MOD`, `FXDIV`) gives 0. A script that reaches `VM_OPS_PER_SLICE` opcodes in one phase stops before the next one, waits a frame and runs it then (an endless loop throttles instead of hanging); a Destroy handler, which can't wait, is halted there instead. `GETP`/`SETP` of a dead entity or an unknown property read 0 and write nothing; `SPAWN` of an unknown object or with all 128 entities in use pushes 0; `KILL` of `ENTITY_NONE` does nothing (*warns*), while `KILL` of an entity that is already dead is silently ignored; `WAIT_ANIM` without a one-shot animation to wait for (also when the game switches the sprite mid-wait) goes on; a `SYS` song or path index that isn't bound, or a string index the blob doesn't have, skips the call.

**Caveats**

- An Animation End event arriving while the entity's script is still running or waiting is dropped (*warns*) like any other event, and the same finish doesn't raise it again.
- An entity attached to an object and then destroyed with `entity_destroy` leaves a stale binding: the VM notices, halts its script and *warns*. Use `vm_kill` or `vm_detach`.

## debug.h

| Function | Description |
| --- | --- |
| `void debug_log(const char* message)` | Writes a line to mGBA's debug log (*Tools > View Logs*, or `mgba-rom-test`'s console), truncated to 255 characters. Does nothing on hardware and other emulators. Combine with `text_format()` for numbers. |
| `u32 debug_warning_count(void)` | Number of `serval:` warnings reported so far; always 0 in release builds. Useful in tests. |
| `void debug_exit(int code)` | Does not return. Under `mgba-rom-test -S 3 -R r0`, ends the run with exit code `code`; elsewhere, stops the program. |

`SERVAL_DEBUG` is defined for Debug and RelWithDebInfo builds of the engine and of games linking it, so games can key their own debug code off it.

## platform.h

Included by every header. Integer types (`u8` … `s32`, `FIXED`), `bool`, and `NULL`, `size_t` and `offsetof` (from the compiler's own freestanding `<stdbool.h>`, `<stdint.h>` and `<stddef.h>`, no C library), and memory placement macros for GBA builds (they expand to nothing in host builds):

| Macro | Places |
| --- | --- |
| `SERVAL_IWRAM_CODE` | A function in IWRAM as ARM code: fastest, but IWRAM is 32 KB shared with the engine, globals and the stack ([development.md](development.md#memory-use)). |
| `SERVAL_IWRAM_DATA` | Data in IWRAM (where ordinary globals already live). |
| `SERVAL_EWRAM_DATA` | Initialized data in EWRAM (256 KB, slower). |
| `SERVAL_EWRAM_BSS` | Zero-initialized data in EWRAM: large buffers that would crowd IWRAM. |

## gba.h

GBA-only escape hatches, not portable to other targets; not included by `serval.h`.

| Function | Description |
| --- | --- |
| `bool gba_oam_submit(u16 attr0, u16 attr1, u16 attr2)` | Appends a raw OAM entry to this frame's shadow OAM, after any sprites already drawn. Returns false if all 128 entries are used. Flushed by `frame_end()`. The rotation matrices in OAM are owned by the engine's rotated draws. |
