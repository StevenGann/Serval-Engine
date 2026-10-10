# Object/event model and bytecode VM

Game logic uses GameMaker's mental model — objects with event handlers — compiled to a compact custom bytecode VM in the style of GB Studio's GBVM.

**Status:** format v1 and milestones 2-7 implemented (interpreter, scheduler, engine bridge: [`src/core/vm.c`](../src/core/vm.c), tested on the host and in the test ROM; the format's reference assembler and disassembler, [`tools/svm.py`](../tools/svm.py), see [Tools](#tools); the revision for compiled Lua: call frames, behaviours and reactions, instance fields, arrays, Lua's arithmetic and the globals' initial values; and the [Lua-subset](lua.md) compiler, [`tools/svlua.py`](../tools/svlua.py), which [`fireflies`](../examples/fireflies/main.c), a game whose logic is all one script, is written in), and the engine's own [collision pass](#collisions) (`vm_collide`), so a scripted game needs no collision code in C. Milestone 8 (the debug link's side, a script benchmark) is still to come. The SYS calls are named after the C functions they call (`VM_SYS_PSG_MUSIC_PLAY`, `VM_SYS_SCREEN_SET_BRIGHTNESS`), and the Lua builtins after the same functions. Format v1 is frozen with the engine's API in 1.0.0-rc.1 ([api-freeze.md](api-freeze.md)): changes before 1.0.0 must come from integrating the editor and are recorded in the release notes; from 1.0.0 on, a change to an existing part of the format is a major version. Games can be written in C against the [core API](core-api.md) and [ECS](ecs.md) or in the Lua subset, and C stays a first-class escape hatch forever.

## Why a custom VM

"Scripting" covers two different needs, and the VM serves only the first:

1. **What the event editor emits.** Objects with Create/Step/Collision handlers, written in a block editor by non-programmers. Needs per-entity cooperative execution, waits ("move here, wait 20 frames, play a sound"), hot patching over the debug link, determinism, zero allocation — and none of closures, tables, dynamic typing or garbage collection. Scripts decide *what* happens; the C systems do the per-frame work.
2. **A real language for power users.** Plain C modules, always; and a **subset of Lua 5.4 compiled to this VM** ([lua.md](lua.md)): statically checked, with no runtime of its own, so it costs what the event editor's scripts cost. Running an actual Lua interpreter on a 16.78 MHz ARM7TDMI with no cache still loses to both (below). Post-1.0, the same subset could also be transpiled to C for native speed (e.g. via Nelua).

### Off-the-shelf options considered

The hard constraints filter hard: no malloc and no GC at runtime, fixed pools, 32 KB IWRAM shared with the game, MIT-compatible ROM-linked licensing, platform-neutral bytecode (GB/DS targets later), ~a hundred tiny per-entity contexts, deterministic across the GBA and web builds.

| Candidate | Why not |
| --- | --- |
| **Lua** (MIT) | Heap + incremental GC + a footprint in the hundreds of KB; GC pauses at 16.78 MHz. Out on the no-malloc/no-GC constraint alone. |
| **Wren, Squirrel, Berry, MicroPython, JerryScript, Duktape, QuickJS** | All GC'd heaps, 10-100x the budget. Same reason. |
| **wasm3** (MIT) | The strongest small interpreter (~64 KB code, ~10 KB RAM, ~11.5x slower than native per its own figures), but that RAM is per module with one linear memory — not 128 contexts of ~100 bytes — and shipping clang→wasm inside the editor dwarfs the problem it solves. |
| **Pawn / AMX** (Apache-2.0) | The closest fit: no GC, integer cells, bytecode, `sleep` yields, embeddable compiler. Still loses: each AMX instance carries its own header + data + stack block (KBs each), 32-bit cell semantics are baked in (the GB target forbids a hard 32-bit dependency), Apache-2.0's NOTICE and patent terms sit heavier on ROM-linked code than this project's "light attribution" bar, and hot reload, breakpoints and the editor's event-block debug maps all want a format we control. It would be forked beyond recognition. |
| **GBVM** (MIT, GB Studio) | The existence proof and design blueprint — a command-style VM driving a fixed engine on a weaker CPU, shipping real games — but it is SM83 assembly + GBDK C. Study it; don't port it. |

The deciding economics: the expensive half of scripting is the event-script **compiler**, which lives in Studio Advance and must be written no matter what it emits. The half an off-the-shelf VM saves — a GBVM-class interpreter — is a few KB of C. Owning the format buys hot reload, debugger integration, GB portability and determinism; the saving would have been the cheap half. Commercial GBA-era games (Pokémon's event scripts, Fire Emblem's event engine) all shipped exactly this kind of tiny custom event VM; none shipped a language runtime.

**Why not transpile events to C and skip the VM?** The editor bundles GCC anyway, but that path loses: the edit-and-continue loop (patching bytes in EWRAM over the debug link versus recompile + relink + reboot, losing game state), event-block-level breakpoints (C needs DWARF→block mapping through the emulator), bytecode density for branchy event logic, and platform neutrality (a GB target would mean SDCC and per-platform codegen).

## Mapping GameMaker concepts

- **Object:** a prefab = default component set + sprite + a table of event → bytecode handler.
- **Events (v1):** Create, Step, Destroy, Collision, Animation End, Room Start. Create and Room Start start an instance's *behaviour*, a script that may wait; the others are *reactions* that run to completion ([Behaviours and reactions](#behaviours-and-reactions)).
- **Room:** tilemap layers + placed instances + required asset groups + camera spec (generated by the editor as C data + `vm_attach` calls; rooms are not a VM concept).
- **Instance:** an ECS entity bound to an object with `vm_attach()`.

## Execution model

### Cells

The unit of data is the **cell**: a signed two's-complement integer, 32 bits in v1. The blob header declares the cell width, and opcode semantics are defined width-agnostically (arithmetic wraps modulo 2^width, shift counts mask to width−1), so a future 16-bit-cell target (GB) stays legal without new opcodes. Positions and velocities are the engine's 24.8 `FIXED` in a cell; `FXMUL`/`FXDIV` provide fixed-point multiply and divide. There are no runtime strings: scripts pass indices into the blob's string table.

### Contexts

Scripts run in fixed-size **contexts** from a static pool — no allocation, ever. The shape (the implementation's layout differs in details):

```c
typedef struct {
    u32 pc;                    // blob offset of the next opcode
    Entity self;               // bound entity, or ENTITY_NONE for a thread
    Entity other;              // the running handler's other entity, else ENTITY_NONE
    u8 state;                  // free, ready, running, or one of the three waits
    u8 event;                  // the VM_EV_* handler it runs
    u16 wait_frames;           // for WAIT
    u8 sp, fp, cp, base;       // stack top, frame start, call depth, the activation's floor
    s32 stack[VM_STACK];       // operands, and the locals of every frame
    struct { u32 pc; u8 fp; } calls[VM_CALLS]; // return points
    VmSaved below;             // a waiting behaviour's pc, sp, fp, cp, base, state, event,
                               // other and wait counter, while a reaction runs on top of it
} VmContext;
```

Constants (in `vm.h`, compile-time): `VM_CONTEXTS 32`, `VM_STACK 64`, `VM_CALLS 16`, `VM_GLOBALS 256`, `VM_FIELDS 16`, `VM_ARRAY_CELLS 1024`, `VM_EVENT_QUEUE 256`, `VM_OPS_PER_SLICE 256`. Five of them are part of format v1 and a compiler may rely on them: `VM_STACK`, `VM_CALLS`, `VM_GLOBALS`, `VM_FIELDS` and `VM_ARRAY_CELLS` ([What format v1 fixes](#what-format-v1-fixes)). `VM_CONTEXTS`, `VM_EVENT_QUEUE` and `VM_OPS_PER_SLICE`, like `VM_COLLIDE_PAIRS` ([Collisions](#collisions)), are the engine's limits, not the format's: a minor version may raise them. `VM_EVENT_QUEUE` is 256, two events for each of the `MAX_ENT` entities, so a room loader can attach every instance (a Create each) and queue every Room Start before the first drain, which then runs every Create before any Room Start, each Create seeing the whole room (it was 32 until Studio Advance's room loader found it had to drain mid-room, running Creates in a half-built room). EWRAM cost (`SERVAL_EWRAM_BSS`) about 27.5 KB: about 12 KB of contexts, 8 KB of instance fields (`VM_FIELDS` cells for each of the 128 entity slots), 4 KB of array cells, 1.5 KB of queue, 1 KB of globals, and the bindings and the collision pass's lists ([measured](#implementation-notes)). A ROM that never calls the VM links none of it.

### Frames

Each context has one stack for operands and locals. A handler starts with an empty frame at the bottom of its stack (its *activation*); `ENTER p, n` makes the top `p` cells (the arguments a caller pushed) the frame's first locals and pushes `n` zeroed locals after them; `LDL`/`STL` address locals relative to the frame. `CALL` saves the return point and the caller's frame; `RET` drops the callee's whole frame (arguments, locals and anything above them) and returns; `RETV` does the same and pushes one value. A handler that uses locals starts with `ENTER 0, n`. Recursion works to the depth `VM_CALLS` and `VM_STACK` allow.

### Behaviours and reactions

Events come in two kinds:

- **Behaviours:** Create and Room Start, and threads started by `vm_start`. A behaviour may wait (`WAIT`, `WAIT_ANIM`, `WAIT_MOVE`), so it can be an instance's whole life: a firefly wandering until it fades. An instance has at most one; starting another (a Room Start `vm_event` for an instance whose behaviour is live) halts the old one first.
- **Reactions:** every other event — Step, Collision, Animation End, Destroy, and events added later. A reaction runs to completion in the drain (or Step pass) that dispatches it. If the instance's behaviour is waiting, the reaction runs **on top of it**, on the same context, like an interrupt: the behaviour's state is saved, the reaction runs above its stack, and afterwards the behaviour is restored and carries on waiting exactly as before. An instance with no live behaviour gets a context for the reaction alone, freed when it ends. A wait inside a reaction, or a reaction running past its budget, warns and halts the reaction, not the behaviour beneath it.

No event is dropped because of what the instance's behaviour is doing: every reaction its object handles runs. (The only drops left are a full queue and an exhausted context pool, both of which warn.) Threads are behaviours with no entity and receive no events. This is GameMaker's model, and it is what lets a compiled `function Firefly:collision(player)` simply run.

### Scheduling: two phases per frame

The VM runs in two phases, fixing the [frame loop order](frame-loop.md) question:

1. `frame_begin()` — input.
2. **`vm_step()`** — phase 1, in this order: resume waiting contexts (pool index order); queue Animation End events; drain the event queue (so Create runs before an entity's first Step); run the Step reaction of every attached entity whose object has one and whose Create has been dispatched (entity index order; on top of its behaviour if one waits); drain the queue again (spawns from Step reactions run their Create this phase).
3. C systems: `sys_path`, `sys_movement`, `sys_map_movement`, `sys_physics`.
4. **`vm_events()`** — phase 2, in this order: drain the event queue (events game code queued since phase 1, such as Collision events from its own collision tests with `vm_event`); then the [collision pass](#collisions), which tests the pairs `vm_collide` set and runs each overlap's Collision reactions as it finds it. So collision handlers run the same frame as the collision, after movement, GameMaker-style.
5. Game C code, `camera_set`, `sys_animate` (an animation it finishes raises Animation End in the next frame's phase 1), `sys_render*`, HUD.
6. `frame_end()`.

Draining runs behaviours to their first wait or their end, and reactions to their end; events queued during a drain (e.g. by `SPAWN`) are drained in the same phase. A full queue drops the event with a debug warning.

### Collisions

`vm_collide(a, b)` makes the VM find collisions itself: the game names, once, the pairs of entity sets whose bodies collide, and `vm_events()` tests them every frame and raises the Collision events. A scripted game then has no collision loop in C (`fireflies` sets one pair, `vm_collide(C_PLAYER, C_FIREFLY)`). It is C configuration, set at boot like `vm_bind`: scripts have no call for it ([lua.md](lua.md#engine-functions)).

- **The sets.** A pair is two component masks. Set `a` is every live entity with all of `a`'s components (`ent_has(i, a)`), set `b` likewise; game components are the usual choice (`C_GAME(n)`, which scripts read and write as `VM_P_TAGS`). Entities are tested whether or not they are attached: an entity C code runs can be the other side of a scripted one's collision. A mask of 0 is refused (it would be every live entity; `C_BODY` means every body).
- **Overlap** is `body_overlap()`'s ([physics.h](../include/serval/physics.h)): the `body_w` × `body_h` rectangles at `pos_x`, `pos_y` overlap, touching edges not counting; a `SPRITE_SCREEN` entity against a world one is compared in the world (the camera added to the screen-space one). A 0 × 0 body (the size until one is set) is a point.
- **The events.** For an overlapping pair, each of the two that is attached to an object with a Collision handler gets `VM_EV_COLLISION` with the other as `OTHER`: `a`'s entity first, then `b`'s. An entity without a handler, or unattached, gets nothing and warns nothing. The two events are queued and the queue drained at once, before the next test: the reactions run in this `vm_events()`, and the tests after them see what they did. An entity a reaction killed (`KILL`; its Destroy has run) is tested no more; one it moved is tested where it is now; one it took out of a set (by its tags) is tested no more as a member of that set. The queue never fills this way, however many overlaps a frame has. Bodies that stay overlapped collide again every frame, as GameMaker's collision event does; a reaction meant for the first touch keeps its own state (an instance field, or a tag it clears).
- **Order** (deterministic): the pairs in the order they were set; within a pair, the entities of `a` in slot order, each against the entities of `b` in slot order. Each pair lists its sets (`ecs_gather`) when its turn comes: an entity spawned by an earlier pair's reactions is in a later pair's lists, one spawned while its own pair runs is tested from the next frame. An entity destroyed during the pass is never tested again in it, even if a spawn has taken its slot. An entity is never paired with itself, and two entities that are both in both sets are tested once, with the lower slot as `a`.
- **Limits.** `VM_COLLIDE_PAIRS` (8) pairs; one more is refused (`false`, a warning). Setting a pair again, either way round, changes nothing and returns `true` (`(a, b)` and `(b, a)` raise the same events). Pairs are the game's configuration, not script state: `vm_load`, `vm_reload` and `vm_unload` keep them, `vm_collide_clear()` removes them all. With no blob loaded nothing is tested.
- **Cost**, measured in mGBA (`tests/rom/vm_platform_tests.c` logs it): each pair lists its two sets, about 1,000 cycles each (one list when `a == b`), then tests every entity of `a` against every one of `b`, about 75 cycles a test, plus the reactions. A player against 8 fireflies costs about 3,000 cycles a frame; 10 shots against 20 enemies about 17,000 (6% of a frame). There is no broad phase: keep the sets small and apart (shots against enemies, not every body against every body).

Game code can still raise Collision events itself with `vm_event(e, other, VM_EV_COLLISION)`, for rules `vm_collide` doesn't express (a stomp judged with `body_hit_side`, a collision with map tiles); queued before `vm_events()`, they run in its first drain, before the pass.

**Budget:** a behaviour that executes more than `VM_OPS_PER_SLICE` opcodes in one run (since it started or last waited; a reaction on top of it counts its own) is forced into a one-frame wait with a debug warning, and a reaction that does is halted — an endless loop warns and throttles instead of hanging the game. The engine never fails silently: every misuse case below warns via `SERVAL_WARN` (debug builds) and fails safe.

### Determinism

No opcode reads anything but cells, entity properties and engine calls; the engine's RNG is already deterministic from input history and frame count. All iteration orders are fixed (context pool order, entity index order, FIFO events, the collision pass's pair and slot order). The same inputs produce the same run on GBA and web, like everything else in the engine.

## Blob format

One **script blob** holds every object, handler and string for a game (or room set). It is `const` data in ROM, or a buffer in EWRAM when the debug link hot-swaps it. All multi-byte fields are little-endian; nothing requires alignment (the interpreter reads bytes), so the same blob bytes serve a future 8-bit target.

### Header (16 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `"SVMB"` |
| 4 | 1 | Format version = 1 |
| 5 | 1 | Cell width in bytes = 4 |
| 6 | 2 | Flags. Bit 0: a [globals' initial values](#array-table) table follows the array table. Bit 1: reserved for an extended handler table (events beyond an object record's six handler slots, in a later version). Every bit but bit 0 must be 0: the loader rejects a blob with any other bit set, bit 1 included, so a blob that needs a later engine never runs half-understood on this one, and a later version can give the bits meaning |
| 8 | 2 | Object count |
| 10 | 2 | String count |
| 12 | 2 | Global count used (≤ `VM_GLOBALS`) |
| 14 | 2 | Array count (0: none; [Array table](#array-table)) |

### Object table

Immediately after the header: one 32-byte record per object.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Default component mask (`C_*` bits, as `entity_create` takes) |
| 4 | 2 | Default sprite ID |
| 6 | 2 | Reserved: must be 0 |
| 8 | 4 × 6 | Handler offsets for `VM_EV_CREATE, VM_EV_STEP, VM_EV_DESTROY, VM_EV_COLLISION, VM_EV_ANIM_END, VM_EV_ROOM_START` (blob-relative; 0 = no handler) |

### String table and code

After the object table: string count × u32 blob-relative offsets, each to NUL-terminated bytes in printable ASCII (what `text_print` draws; other bytes show as `?`, see [`text.h`](../include/serval/text.h)). The [array table](#array-table) follows. Code, string bytes and ROM array data fill the rest of the blob, in any order: the loader accepts every order. **Tools should produce code, then ROM data, then strings**, as `svm.py` and Studio Advance's `VmAssembler` do by default, so two implementations given the same program produce the same bytes (the [golden example](#worked-example-golden-bytes) puts its string first, and stays valid). Changing a string's or a ROM array's contents then never moves code; adding a string or an array still does, since each adds a table entry before the code. Object and string counts are bounded only by their 16-bit fields and by the tables fitting in the blob. Offsets are absolute (`CALL` targets too): a blob is always built whole, and the compiler's PC → event-block map is regenerated with it.

**Load-time validation** (`vm_load` returns false and warns on the first failure): magic, version, cell width, the header's flags (only bit 0 may be set) and every reserved field zero, counts within limits, the tables (the globals' initial values included) inside the blob, every array record valid (a known kind; a RAM range with position + length ≤ `VM_ARRAY_CELLS`, so an empty one may sit at 1024; ROM data with offset + bytes ≤ the blob size and the offset past the tables, so an empty one may sit exactly at the end), every handler and string offset past the tables and strictly less than the blob size, and every string's terminating NUL inside the blob (so no later use can read past its end). The header does not mark where code starts, so the loader cannot tell a handler offset pointing into string bytes from one pointing at code; the runtime bounds checks make that safe, and the compiler is responsible for it. **Runtime bound:** the dispatcher checks `pc` stays inside the blob; escaping it (a bad jump, or falling off the end) warns and halts the context — so a handler must end in `HALT` or `RET`-to-empty, which the compiler guarantees and the interpreter doesn't trust.

### Array table

After the string table: array count × 8-byte records. A blob with no arrays has a count of 0 and no table, so it is laid out exactly as before arrays existed (the [golden bytes](#worked-example-golden-bytes) included).

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | Length, in elements |
| 2 | 1 | Kind: 0 RAM cells; ROM data: 1 `s8`, 2 `u8`, 3 `s16`, 4 `u16`, 5 `s32` |
| 3 | 1 | Reserved: must be 0 |
| 4 | 4 | RAM: the first cell in the VM's array pool (`VM_ARRAY_CELLS` cells). ROM: the blob offset of the elements, little-endian, packed |

**Globals' initial values.** With flag bit 0 set, the global count × one cell each (the header's cell width, little-endian: 4-byte signed values in v1) follow the array table, and `vm_load` sets the globals to them instead of zeroing them (globals past the count start at 0); `vm_reload` keeps the old values as usual when the global count matches, and otherwise starts them at the new blob's initial values, as `vm_load` would. Without it, globals start at 0. The table is part of the tables: handlers, strings and ROM data lie past it. A blob whose initial values are all 0 needs no table, and `svm.py` writes none.

RAM arrays are cells the scripts read and write, zeroed by `vm_load`; RAM ranges may overlap (the compiler's concern, never a memory-safety one). ROM arrays are constant data read in place: level tables, wave lists, lookup tables, in the narrowest kind that holds them.

## Opcode reference

One opcode byte, then operands as listed. `rel16` is a signed 16-bit offset from the address immediately after the operand. Stack effects are written `pop → push`. Unknown opcode: warn, halt context. Stack or call-stack over/underflow, on any op: warn, halt context.

### Stack and variables

| Op | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x00 | `NOP` | | |
| 0x01 | `HALT` | | End the handler; free the context |
| 0x02 | `PUSH8` | s8 | → value (sign-extended) |
| 0x03 | `PUSH16` | s16 | → value (sign-extended) |
| 0x04 | `PUSH32` | s32 | → value |
| 0x05 | `DUP` | | a → a, a |
| 0x06 | `DROP` | | a → |
| 0x07 | `SWAP` | | a, b → b, a |
| 0x08 | `LDG` | u8 | → `glob[n]` |
| 0x09 | `STG` | u8 | a → (`glob[n] = a`) |
| 0x0A | `LDL` | u8 | → the frame's local n (outside the frame: warn, push 0) |
| 0x0B | `STL` | u8 | a → (outside the frame: warn, dropped) |
| 0x0C | `LDA` | u16 array | i → element i (0-based; out of range: warn, push 0) |
| 0x0D | `STA` | u16 array | i, a → (out of range, or a ROM array: warn, dropped) |
| 0x0E | `LEN` | u16 array | → the array's length |

An array number the blob doesn't have: warn; `LDA` and `LEN` push 0, `STA` drops.

### Arithmetic, logic, comparison

No operands. Binary ops: `a, b → a ∘ b`; unary `NEG`, `BNOT`, `LNOT`: `a → result`. Arithmetic wraps; `DIV`/`MOD`/`FXDIV` by zero warn once and produce 0; `DIV` rounds toward zero. Shifts mask the count to width−1; `SHR` is arithmetic. Comparisons push 1 or 0.

| Range | Ops |
| --- | --- |
| 0x10–0x15 | `ADD SUB MUL DIV MOD NEG` |
| 0x16–0x17 | `FXMUL` (`(a×b)>>8`, double-width intermediate) · `FXDIV` (`(a<<8)/b`) |
| 0x18–0x1D | `AND OR XOR BNOT SHL SHR` |
| 0x1E | `LNOT` (0 → 1, else 0) |
| 0x1F | `LSH`: Lua's shifts. `a` shifted left by `b`, or logically right by −`b` when `b` < 0; a count of 32 or more either way gives 0 (`a >> b` compiles to `LSH a, −b`) |
| 0x20–0x25 | `EQ NE LT LE GT GE` |
| 0x26–0x27 | `IDIV IMOD`: floored, as Lua's `//` and `%` (`IDIV(-7, 2) = -4`, `IMOD(-7, 2) = 1`; by zero: warn, 0; `IDIV(INT32_MIN, -1) = INT32_MIN`, `IMOD(INT32_MIN, -1) = 0`) |

### Control flow

| Op | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x28 | `JMP` | rel16 | |
| 0x29 | `JZ` | rel16 | a → (jump if a == 0) |
| 0x2A | `JNZ` | rel16 | a → (jump if a != 0) |
| 0x2B | `CALL` | u32 | Save the return point and the caller's frame; jump to the blob offset (more than `VM_CALLS` deep: warn, halt) |
| 0x2C | `RET` | | Drop the frame (arguments, locals, temporaries) and return; at the handler's own level: end the handler |
| 0x2D | `RETV` | | a → ; as `RET`, then push a. At the handler's own level: end the handler (a is discarded) |
| 0x2E | `ENTER` | u8 p, u8 n | The frame's locals: the top p cells become locals 0 to p−1, and n zeroed locals follow (fewer than p cells in this activation, or no room on the stack: warn, halt) |

### Waits

No operands (`WAIT` pops its frame count). A waiting context sleeps until its condition holds, checked at the start of each `vm_step()`. Waits are for behaviours: in a reaction, a wait that would suspend warns and halts the reaction (one that continues at once, `WAIT 0` or `WAIT_MOVE` with no path, is fine).

| Op | Mnemonic | Effect |
| --- | --- | --- |
| 0x30 | `WAIT` | n → ; resume after n frames (n ≤ 0: continue immediately; n = 1: next frame) |
| 0x31 | `WAIT_ANIM` | Resume when `anim_finished(self)`. No one-shot animation (no `C_SPR` or `C_ANIM`, or a sprite without `SPRITE_ASSET_ANIM_ONCE`), checked when the op runs **and on every resume pass while waiting** (the game may switch sprites mid-wait): warn, continue. Already finished: continue immediately. In a thread with no entity: warn, continue |
| 0x32 | `WAIT_MOVE` | Resume when `self` has no `C_PATH` (`sys_path` removes it when a path ends). Already pathless: continue immediately |

0x33 is unassigned: it held `INTERRUPTIBLE` before v1 was released, which reactions made unnecessary.

### Entities

Entity handles travel in cells (`Entity` is a u16; `ENTITY_NONE` is 0).

| Op | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x38 | `SELF` | | → bound entity (detached thread: warn, push 0) |
| 0x39 | `OTHER` | | → the event's other entity, else 0 |
| 0x3A | `GETP` | u8 prop | e → value |
| 0x3B | `SETP` | u8 prop | e, value → |
| 0x3C | `SPAWN` | u16 object | x, y → entity. `entity_create` with the object's component mask, sprite and position set; Create queued (runs this phase) |
| 0x3D | `KILL` | | e → ; queues Destroy: the Destroy reaction runs (on top of e's behaviour if it waits), then e's behaviour is halted and e destroyed |
| 0x3E | `NEXTI` | u16 object | e → the next attached instance of the object after e, in entity slot order (e = 0: the first), or 0 when there are none left. A loop over every instance |

Properties (`GETP`/`SETP` page, v1): `VM_P_X 0, VM_P_Y 1` (FIXED, world), `VM_P_VX 2, VM_P_VY 3` (FIXED), `VM_P_SPR 4, VM_P_FRAME 5, VM_P_FLAGS 6, VM_P_ANGLE 7, VM_P_DEPTH 8, VM_P_SCALE 9`, `VM_P_BODY_W 10, VM_P_BODY_H 11` (whole pixels, the size `body_overlap` tests; component `C_BODY`) — the ECS arrays of the same names. `SETP` truncates to the array's type; `GETP` extends it back (sign-extending `s16`, zero-extending `u8` and `u16`). Dead or `ENTITY_NONE` entity: warn; `GETP` pushes 0, `SETP` is dropped. A property whose component bit the entity lacks warns in debug builds but still reads/writes the (zeroed-at-create) array. Unknown property: warn, 0/dropped. Further engine properties: `VM_P_TAGS 12` (the entity's game components `C_GAME(0)` to `C_GAME(14)` as bits 0–14; `SETP` changes only those bits), `VM_P_ANIM_TIME 13` and `VM_P_ANIM_STEP 14` (`spr_anim_time`, `spr_anim_step`: with `VM_P_FRAME`, what restarting an animation sets; component `C_ANIM`); the body's tuning, as [physics.h](../include/serval/physics.h) has it (component `C_BODY`): `VM_P_BODY_BOUNCE 15` (`body_bounce`, `u8`: 0–254 the 256ths of its speed a floor bounce keeps, 255 a perfect bounce), `VM_P_BODY_FRICTION 16` (`body_friction`, `u8`: 256ths of its speed lost per frame on a floor), `VM_P_BODY_MAX_FALL 17` (`body_max_fall`, FIXED pixels per frame in a `u16`: 0 no limit, up to 255.996) and `VM_P_BODY_GRAVITY 18` (`body_gravity`, `s8`: the gravity scale in 16ths minus 16, as `BODY_GRAVITY(sixteenths)` writes it, so 0 is normal gravity); and `VM_P_BODY_CONTACT 19` (`body_contact`, `u8`, component `C_BODY`: what the body touched in the last `sys_physics()` with `physics_set_contacts(true)`, or `sys_map_movement()`; the `BODY_SIDE_*`, `MAP_CONTACT_*` and `BODY_CONTACT_EXIT` bits, [ecs.md](ecs.md#bodies)), and `VM_P_OBJECT 20`: the object the entity is attached to (its number, `OBJ_*`), or −1 if it isn't attached (never attached, detached, or attached before the latest load; a dead entity or `ENTITY_NONE` warns, as for any property, and reads −1, never object 0's number), what `vm_object_of()` returns to C. These two are **read-only**: `SETP` of them warns and writes nothing, whatever the entity, popping both its operands. Properties 21–63 are reserved for the engine. **Naming:** each later property's Lua name (and so its `VM_P_*` name) starts with one of the prefixes the Lua subset reserves, `anim_`, `body_`, `ent_`, `map_`, `path_`, `pos_`, `spr_`, `vel_` or `vm_`, normally as its C pool's name (`body_bounce`; a later `path_speed` property would be `VM_P_PATH_SPEED`). A script can't name an instance field that way, so adding a property changes no script's meaning; no other name becomes a property ([lua.md](lua.md#reserved-names)). **Instance fields** are properties `VM_P_FIELD0` (64) to `VM_P_FIELD0 + VM_FIELDS − 1` (79): cells of the attached instance, zeroed when it is attached, readable and writable on any attached entity (an unattached one: warn, 0 or dropped). The page is append-only.

### Engine calls

| Op | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x40 | `SYS` | u8 fn | Pops the function's arguments (pushed left to right, so the last argument is on top), pushes its result if it has one |

SYS page v1 (append-only; the interpreter holds a static table of `{arity, returns, fn}`). Each call is named after the C function it calls: its `VM_SYS_*` name is that name in capitals, and the [Lua subset's](lua.md#engine-functions) builtin has the same name. `text_print_number` is the one call with no C function of its own (C prints numbers with `text_format`).

| # | `VM_SYS_*` | Calls | Args (top of stack last) | Returns |
| --- | --- | --- | --- | --- |
| 0 | `PSG_PLAY` | `psg_play` | sound id | |
| 1 | `PSG_MUSIC_PLAY` | `psg_music_play` | song index (`VmBindings.psg_songs`) | |
| 2 | `PSG_MUSIC_STOP` | `psg_music_stop` | | |
| 3 | `PSG_MUSIC_PAUSE` | `psg_music_pause` | | |
| 4 | `PSG_MUSIC_RESUME` | `psg_music_resume` | | |
| 5 | `CAMERA_SET` | `camera_set` | x, y (whole pixels) | |
| 6 | `TEXT_PRINT` | `text_print` | col, row, string index | |
| 7 | `RANDOM_RANGE` | `random_range` | lo, hi | cell |
| 8 | `BUTTON_DOWN` | `button_down` | button mask | 0/1 |
| 9 | `BUTTON_PRESSED` | `button_pressed` | button mask | 0/1 |
| 10 | `SCREEN_SET_BRIGHTNESS` | `screen_set_brightness` | level | |
| 11 | `PATH_START` | `path_start` | entity, path index (`VmBindings.paths`), flags | |
| 12 | `TEXT_PRINT_NUMBER` | (see below) | col, row, value, width | |
| 13 | `PATH_STOP` | `path_stop` | entity | |
| 14 | `SCREEN_SET_BLEND` | `screen_set_blend` | top, bottom, top_weight, bottom_weight | |
| 15 | `SPRITE_SET_COLORS` | `sprite_set_colors` | sprite id, index, array, count | |
| 16 | `TILESET_SET_COLORS` | `tileset_set_colors` | index, array, count | |

`text_print_number` prints the value in decimal. With a width of 1 or more it is right-aligned in that many columns, spaces in front, so a number that got shorter (10, then 9) leaves nothing behind; a number wider than the width prints in full. A width of 0 or less prints just the digits. Calls take at most 4 arguments.

The palette calls take their colors from an array (its number: `ARR_NAME` in a listing): the first `count` elements, read at the call, each element's low 16 bits a `Color`; the C function gets them as its `colors`, so the array may change right after. An array the blob doesn't have, or a count that is negative, past the array's length or past 256 (more than any call can write), warns (once per kind) and makes no call; the arguments are popped as usual. The C function checks the rest (a sprite that isn't loaded, colors past its group's palettes or past background color 239) and warns as it does for C.

The page has no calls yet for tracker music (`music_*`) and sampled sound (`sfx_*`), implemented in C ([audio.md](audio.md)), nor for API the engine declares but doesn't implement: each arrives appended, as `SCREEN_SET_BLEND` did with alpha blending's and `SPRITE_SET_COLORS` and `TILESET_SET_COLORS` with palette writes', and as do calls for API added later. Runtime sprite tiles (`sprite_set_tiles()`, implemented) have none: a script can't hold tile data. Meanwhile the Lua subset keeps scripts from declaring their names ([lua.md](lua.md#planned-functions)), and every other C function's ([lua.md](lua.md#c-functions)), so each call's builtin takes its C name without breaking a script. The numbers above never change.

Pointer-taking engine calls go through **bindings** the game registers once: `vm_bind(&(VmBindings){.psg_songs = ..., .psg_song_count = ..., .paths = ..., .path_count = ...})`. A bad index or missing binding warns and does nothing (returns 0). `vm_load` and `vm_unload` keep the bindings.

### Debug

| Op | Mnemonic | Operands | Effect |
| --- | --- | --- | --- |
| 0x50 | `BRK` | | v1: logs and continues (debug builds). Reserved for the debug link: will suspend the context until resumed ([debug-link.md](debug-link.md)) |
| 0x51 | `TRACE` | u16 string | Debug builds: logs the string and the top of stack. Release: skips the operand, no output |

## Hot reload

- `vm_load(blob, size)`: validate, reset all contexts, set the globals to the blob's initial values (or zero), bind the object table.
- `vm_reload(blob, size)`: the same, but keeps global values when the global count matches (else starts them as `vm_load` does and warns) — so the debug link can swap scripts mid-game without losing story flags. RAM array cells are kept the same way when the new blob's RAM arrays are laid out identically (same count, kinds, lengths and pool positions), otherwise zeroed with a warning. The comparison uses a fingerprint of the layout taken at load, because the debug link may write the new blob over the old one in place; instance fields are kept for the entities the reload keeps attached. Swaps happen between frames (outside `vm_step`/`vm_events`).
- Scripts execute from the blob in place; the editor's debugger owns the PC → event-block mapping (emitted by its compiler, never shipped in the blob).

## Public API

[`include/serval/vm.h`](../include/serval/vm.h) is the API and names every number in this document (`VM_OP_*`, `VM_EV_*`, `VM_P_*`, `VM_SYS_*`, the limits): loading (`vm_load`, `vm_reload`, `vm_unload`, `vm_bind`), entities and events (`vm_attach`, `vm_detach`, `vm_kill`, `vm_start`, `vm_event`), [collisions](#collisions) (`vm_collide`, `vm_collide_clear`, `VM_COLLIDE_PAIRS`), the two phases (`vm_step`, `vm_events`) and inspection (`vm_global`, `vm_set_global`, `vm_object_of`, `vm_ops_this_frame`, `vm_idle`). The ECS gains one query for `WAIT_ANIM`:

```c
// ecs.h: true if e is alive, has C_SPR and C_ANIM, its sprite has
// SPRITE_ASSET_ANIM_ONCE, and it is on its last frame (or, with a
// frame_order, its last step), where sys_animate leaves it.
bool anim_finished(Entity e);
```

C code that destroys scripted entities directly must use `vm_kill`/`vm_detach` so the VM's entity→context map stays honest (the stale-binding guard below makes a mistake safe, not correct).

## Tools

[`tools/svm.py`](../tools/svm.py) is the blob format's reference implementation, this document made executable: an **assembler** (`svm.py asm`) that turns a text listing into a blob, and a **disassembler** (`svm.py dis`) that turns a blob back into a listing the assembler reproduces byte for byte. Every number it uses (`VM_OP_*`, `VM_EV_*`, `VM_P_*`, `VM_SYS_*`, the limits and sizes) is read from [`vm.h`](../include/serval/vm.h) when it starts; the one table it keeps is each opcode's operand layout from the [opcode reference](#opcode-reference), and it refuses to run if that table and `vm.h`'s opcode list disagree. Python 3, standard library only.

It is an assembler, not a language: one mnemonic per opcode, labels, constants and directives for the blob's tables. There are no expressions beyond integer constant arithmetic, no `if` or `while`, no variables and no event blocks. Those are the job of the compilers that emit this format: the [Lua subset's](lua.md), and Studio Advance's event editor through it.

### Listing syntax

One statement per line; `;` starts a comment; names are case-sensitive and, labels aside, must be defined before they are used.

```text
.const NAME expr                     a constant for expressions
.object NAME mask=expr sprite=expr   an object, numbered from 0 in order of appearance
.string NAME "text"                  a string, numbered from 0; printable ASCII (\" and \\)
.globals NAME[=expr] ...             globals, numbered from 0; may repeat. NAME=expr starts
                                     one at expr instead of 0 (an expression with spaces goes
                                     in parentheses: SPEED=(FX(1) + 128))
.array NAME length [at=expr]         a RAM array; arrays (.array and .rom) are numbered from 0
                                     in order of appearance. Its cells follow the previous
                                     .array's in the pool (the first starts at cell 0), or
                                     start at cell expr
.rom NAME kind expr, expr, ...       a ROM array: kind s8, u8, s16, u16 or s32, its values
                                     range-checked against the kind (s32 takes any 32-bit
                                     value, as PUSH32 does); none is an empty array
.handler OBJECT EVENT                the code that follows is OBJECT's handler for EVENT
                                     (CREATE STEP DESTROY COLLISION ANIM_END ROOM_START)
label:                               a label at the next byte (a jump or CALL target)
MNEMONIC [operand, ...]              one opcode, by its VM_OP_* name without the prefix,
                                     its operands separated by commas (ENTER 0, 3);
                                     PUSH expr picks the smallest of PUSH8, PUSH16 and PUSH32
.byte expr, expr, ...                raw bytes, where they appear
.strings [NAME ...]                  these strings' bytes here instead of after the code
.data [NAME ...]                     these ROM arrays' elements here instead of after the code
```

Operands: `GETP`/`SETP` take a property (`X`, `BODY_W`, `TAGS`, `FIELD0`; an instance field past the first is an expression, `VM_P_FIELD0 + 3`), `SYS` an engine call (`TEXT_PRINT`), `SPAWN` and `NEXTI` an object, `TRACE` a string, `LDG`/`STG` a global, `LDA`/`STA`/`LEN` an array, `LDL`/`STL` a number, `ENTER` two numbers (p, n), `JMP`/`JZ`/`JNZ`/`CALL` a label, `PUSH8/16/32` an expression (range-checked). A number works wherever a name does, except as a jump or call target. In expressions, objects, strings, globals and arrays are `OBJ_NAME`, `STR_NAME`, `G_NAME` and `ARR_NAME`, the names the generated header gives C. Expressions are integers (decimal or `0x` hex), names, `+ - * / << >> | & ~` and parentheses with C precedence, and the engine's macros `FX(n)` (`n * 256`), `C_GAME(n)` and `BODY_GRAVITY(n)` (`n − 16`, n from −112 to 143, as `physics.h`'s `s8` holds it); `--header FILE` (repeatable) adds a C header's `#define`s and enumerators, except planned ones (`SERVAL_PLANNED`: a listing that uses one fails with an error saying it is planned), so a listing uses the game's and the engine's names (`SPR_SERVAL_IDLE`, `C_POS`, `BUTTON_START`, `SPRITE_FLIP_H`). Lookup order: the listing's names, then the headers' (a name two headers define with different values is an error, whatever their order), then `vm.h`'s.

The [golden bytes](#worked-example-golden-bytes) as a listing (laid out with the string after the code):

```text
.object THING mask=0 sprite=0
.string HI "HI"
.globals SUM
.handler THING CREATE
    PUSH 5      ; 5
    PUSH 7      ; 5 7
    ADD         ; 12
    STG SUM     ;
    HALT
```

Layout: the header, the object table, the string table, the array table, the globals' initial values (only when one isn't 0, with header flag bit 0; otherwise no table, so a listing without initial values lays out as before they existed), the code in listing order (handler offsets and `CALL` targets patched in, `rel16` jumps counted from the byte after the operand), then every ROM array's elements in array order, then every string's bytes, so changing a string's or a ROM array's contents never moves code (adding one adds a table entry before the code, which does) (data before strings: the data's alignment-free records come first, and a string table edit, the commonest change, moves nothing else). Errors (an unknown mnemonic or name, an operand that doesn't fit or a missing or extra one, a label bound twice or never, a handler for an object or event that doesn't exist, two handlers for the same event, a non-printable string, a ROM value that doesn't fit its kind, a RAM array outside the pool of `VM_ARRAY_CELLS`, too many objects, strings, globals or arrays, an array or object used before it is declared, a jump out of `rel16` range) name the listing's line and leave nothing written; a handler whose last op isn't `HALT`, `RET`, `RETV` or `JMP` is a warning.

### Outputs

- `-o OUT.bin`: the raw blob.
- `--c OUT.c --symbol NAME`: the blob as `const unsigned char NAME[]` with `const unsigned int NAME_size` (plain C types, so it compiles anywhere), 16 bytes per line with the offset in a comment.
- `--defs OUT.h`: an include-guarded header with `OBJ_<NAME>`, `STR_<NAME>`, `G_<NAME>` and `ARR_<NAME>` for every object, string, global and array, `OBJ_COUNT`, `STR_COUNT`, `G_COUNT` and `ARR_COUNT`, and (with `--symbol`) the two `extern`s; `--prefix P` prefixes the names.

`svm.py dis BLOB.bin [-o OUT.svm]` validates the blob as `vm_load` would (magic, version, cell width, flags and reserved fields, tables inside the blob, offsets in range, strings NUL-terminated, array records valid) and writes a listing: numbered objects, strings, globals (`2=384` where an initial value isn't 0) and arrays (`.array` with `at=` where a RAM array doesn't follow the previous one, `.rom` with its values), a `.handler` line at every handler offset (two when handlers share one), `L_<offset>:` labels at handler offsets and at jump and call targets, properties, engine calls and events by name, `PUSH8/16/32` as written, and as `.byte` lines whatever the assembler wouldn't accept (padding, an unknown opcode, a jump into the middle of an instruction, an operand naming an object, string or array the blob doesn't have), so odd blobs round-trip too; when the ROM arrays' elements and the strings aren't laid out after the code as the assembler would (the data in array order, then the strings in index order), every one of them gets a `.data` or `.strings` line where it is. `asm(dis(b)) == b` for every blob `vm_load` accepts, except one whose strings or ROM arrays share bytes, or with a handler starting inside them, or with header flag bit 0 set and every initial value 0, which the assembler can't lay out: `dis` refuses those.

### In a build

`serval_add_script(<target> <script.lua | listing.svm> [SYMBOL name] [PREFIX p] [HEADERS h...])` ([`cmake/Serval.cmake`](../cmake/Serval.cmake)) compiles a script in the [Lua subset](lua.md) to a listing with `tools/svlua.py`, then runs the assembler on it (or on a hand-written listing) whenever the script, a header, the tools or `vm.h` changes, generating `<basename>_script.c` and `<basename>_script.h` in the target's binary directory and adding them to the target, in the engine's tree and from a game's own project against a release archive (which ships `tools/svlua.py` and `tools/svm.py`); [`fireflies`](../examples/fireflies/fireflies.lua) and [`tests/consumer`](../tests/consumer/consumer.lua) use it. Details in [development.md](development.md#building-a-game).

The engine's own tests ([`tests/vm_tests.c`](../tests/vm_tests.c)) keep their small blob builder on purpose: they were written from this document alone, independently of the tool, and `tools/svm_test.py` (CTest `svm_tool`, host preset) checks the assembler against the same golden bytes, so the spec, the interpreter and the tool are three readings that must agree. Studio Advance's compiler is cross-checked against the same golden bytes.

## Exact semantics

The rules an implementation must follow where the sections above leave room. Tests are written against these.

**Starting scripts.**
- `vm_attach(e, obj)`: binds `e` to `obj`, zeroes its instance fields and queues Create (even when the object has no Create handler: then the queued event simply does nothing), unless its Create is already pending: attaching twice before a drain queues one Create. An entity already attached is rebound: its live context is halted first. Dead entity, object out of range or no blob: warn, ignore.
- `vm_start(obj, ev)` allocates a context at once (`self = other = ENTITY_NONE`) in the *ready* state; ready contexts first run in the next `vm_step()`'s resume pass. `vm_events()` never resumes contexts.
- A queued behaviour event (Create, Room Start), when drained, halts the instance's live behaviour if it has one, allocates a context and runs it to its first wait or its end, within the drain. A queued reaction runs as [Behaviours and reactions](#behaviours-and-reactions) describes.

**The resume pass** (start of `vm_step()`): visits each context once, in pool index order. A ready context runs. A `WAIT n` context decrements its counter and runs when it reaches 0 (so `WAIT 1` resumes in the next frame's pass; counters are clamped to 65535). `WAIT_ANIM`/`WAIT_MOVE` contexts run when their condition holds. A context that waits again during the pass is not visited again in the same pass.

**Step reactions** run after the first drain of `vm_step()`, in entity index order, for attached entities whose object has a Step handler and whose Create event has been dispatched, on top of the instance's behaviour if one waits. Each binding carries a *Create pending* flag, set by `vm_attach` and cleared when its Create event is drained (whether or not the object has a Create handler, and also if the event is dropped). It is never set if the Create event can't be queued (queue full: the attach warns), and `vm_reload` clears it, since it empties the queue. So an entity never runs Step before Create: one attached before `vm_step()` runs Create in the first drain and its first Step the same frame (on top of Create if Create is waiting); one spawned by a Step handler runs Create in the second drain and its first Step next frame.

**Animation End** is raised by the VM itself, not by `sys_animate` (the ECS has no VM hook): after the resume pass, for each attached entity whose object has an Animation End handler, the VM checks `anim_finished(e)` and queues `VM_EV_ANIM_END` when it is true and was false at the previous check (each binding keeps that last value, false at attach). Edge-triggered: an animation restarted by the game (`spr_frame = 0`) can raise it again when it next finishes.

**Draining** (both of `vm_step()`'s drains, `vm_events()`'s, and the [collision pass's](#collisions) after each overlap): FIFO until the queue is empty, including events queued during the drain. Per entry:
- `VM_EV_DESTROY` (from `KILL`): if the entity is dead, skip. If attached: run its Destroy reaction if the object has one (on top of its behaviour if one waits), then halt the behaviour and unbind. Then `entity_destroy`. Unattached live entities are just destroyed.
- Any other event: skip silently if the entity is dead or unattached, or its object has no handler for the event. A behaviour event halts the instance's live behaviour and starts the new one; a reaction runs to completion, on top of the behaviour if one waits, otherwise in a context of its own (no free context: warn, drop).

**Reactions on top of a behaviour.** The behaviour's `pc`, `sp`, `fp`, `cp`, `base`, state, event, other and wait counter are saved, and so are its return points (`calls[0..cp)`: a behaviour may be waiting inside a function, and the reaction's own calls reuse that table); the reaction's activation starts at the behaviour's `sp` (its `base`, with `fp = sp` and `cp = 0`). When the reaction ends — `HALT`, `RET` or `RETV` at its own level, or any fault that halts it — all of them are restored, return points included, so the behaviour's stack, frames and wait are exactly as before. A reaction has the full `VM_CALLS` depth of its own. A Destroy reaction is followed by halting the behaviour. Budget, ops counting and warnings apply to the reaction on its own.

**Frames.** `base` is 0 for a behaviour or a reaction in its own context, and the behaviour's `sp` for a reaction on top of one. `CALL`: `calls[cp++] = {pc after the operand, fp}`, then `fp = sp`. `ENTER p, n`: needs `p ≤ sp − base` and room for n more cells; `fp = sp − p`, then n zeros are pushed. `LDL`/`STL n`: needs `fp + n < sp` (after `STL`'s pop). `RET`: `sp = fp`, then `{pc, fp} = calls[--cp]`. `RETV`: pop v, then as `RET`, then push v. `RET` or `RETV` with `cp = 0` ends the handler, whatever its stack holds. A handler starts with `sp = fp = base`.

**Arrays, fields, instances.** Array indices are 0-based (a compiler for a 1-based language subtracts 1). `NEXTI` compares entity slot indices, so a loop that `KILL`s the instance it is on still visits the rest (the kill is queued). Instance fields belong to the binding: `vm_detach` and destruction clear them with it.

**Stale bindings.** The VM stores each binding's `Entity` handle. Wherever it looks a binding up, a handle that no longer matches a live entity (destroyed behind the VM's back) counts as unbound: the binding is cleared, its context halted, and it warns once.

**`vm_kill(e)`** runs the Destroy logic above immediately when called outside the VM's phases; if called during one it is queued like `KILL`.

**Spawning.** `SPAWN obj` pops `y` then `x` (x was pushed first), calls `entity_create` with the object's component mask, sets `pos_x`/`pos_y` to x/y, sets `spr_id` to the object's sprite if the mask has `C_SPR`, attaches it (queueing Create, which runs in the same drain or, from the resume pass or a Step handler, in this phase's drain), and pushes the entity. `entity_create` failing (pool full) or an object out of range: warn, push 0.

**Arithmetic** is defined on two's-complement 32-bit wrapping and must be implemented without C undefined behaviour (the host tests run under UBSan): do `ADD SUB MUL NEG SHL` in `u32`; `DIV` and `MOD` of `INT32_MIN` by −1 give `INT32_MIN` and 0; `SHR` is arithmetic; `FXMUL` is the 64-bit product shifted right arithmetically by 8 (rounding toward negative infinity: `FXMUL(-3, 100) = -2`), then truncated modulo 2^32; `FXDIV` is `(s64)a * 256 / b` (rounding toward zero), truncated modulo 2^32 (never `a << 8` on a negative value). `IDIV`/`IMOD` are the truncating results corrected toward negative infinity when the signs differ and the remainder isn't 0; `LSH` shifts in `u32`. GCC and Clang shift signed values arithmetically, which the engine already relies on; do the final truncation through `u32`.

**Budget.** Ops are counted per handler run: each run of a behaviour (from its start or a wait to its next wait or its end) and each reaction counts from 0, so a reaction on top of a behaviour has a count of its own. The op that makes the count exceed `VM_OPS_PER_SLICE` is not run: the context becomes `WAIT 1` at that op. Inside a reaction (which cannot wait) the reaction is halted instead; a behaviour beneath it is untouched. `vm_ops_this_frame()` counts ops run since the latest `vm_step()` began (both phases).

**Halting a context** frees it (its state becomes free), and the entity's context link (if any) is cleared. Every "warn, halt context" in this document halts only the offending context; the phase continues with the next one.

**Warnings repeat once per problem, per loaded blob** (this replaces any "warn once" or per-occurrence reading elsewhere in this document; tests should expect exactly one warning the first time a kind of problem happens and none for repeats until the next load): a *kind* is one distinct warning message of the implementation (which problems share one is an implementation detail; the engine's own tests follow its grouping). Each kind warns the first time it happens after `vm_load`, `vm_reload` or `vm_unload`, then stays quiet, so a problem that recurs every frame (a Step handler over budget, say) does not flood the log; the text names the object and event (or entity, or offset) involved so the first report is actionable.

**Re-entry.** While a phase runs — inside `vm_step`/`vm_events`, and inside `vm_kill`'s immediate Destroy reaction, which counts as one — `vm_load`, `vm_reload` and `vm_unload` warn and do nothing (`vm_load` and `vm_reload` return false), `vm_step` and `vm_events` warn and do nothing, and `vm_kill` is queued like `KILL`. None of these can be reached from a script in v1; the guards keep one saved behaviour per context enough. A failed `vm_load`/`vm_reload` leaves the VM unloaded.

**Destroy details.** A Destroy handler sees `OTHER` = 0. `vm_event(e, x, VM_EV_DESTROY)` behaves exactly like `KILL`. `KILL`, `vm_kill` or a Destroy `vm_event` of `ENTITY_NONE` warns (a bug); one of an entity that is already dead is skipped silently (two scripts killing the same thing is normal). `vm_event` with an event number of `VM_EV_COUNT` or more warns and queues nothing. Room Start has no automatic source in v1: a game runs it with `vm_start(obj, VM_EV_ROOM_START)` or `vm_event(e, ENTITY_NONE, VM_EV_ROOM_START)` when it builds a room.

**Build time versus run time.** What the VM handles safely at run time (an `ENTER` that can never fit the stack, a `SPAWN` or `NEXTI` of an object the blob doesn't have, an out-of-range property or SYS number, a `SETP` of a read-only property) a compiler or assembler may reject at build time. Studio Advance's assembler and `svlua.py` do (`svlua.py` emits only valid properties and calls, never assigns a read-only property, and rejects a frame larger than `VM_STACK`); `svm.py` rejects the `SPAWN` and `NEXTI`, but only warns about a property or SYS number outside `vm.h`'s pages, assembles a `SETP` of a read-only property, and doesn't check `ENTER` against `VM_STACK`. The VM's run-time warnings exist for blobs that skip those checks.

**Entity cells.** A cell used as an entity handle is valid only within 0..0xFFFF; other values count as `ENTITY_NONE` (never truncated into a real handle).

**Debug ops.** `TRACE` peeks at the top of the stack (does not pop); with an empty stack it logs only the string; an invalid string index logs `?`. `BRK` logs `vm: BRK at <offset>` in debug builds. Both log through `debug_log` and are silent no-ops in release builds (operands still skipped).

## Worked example (golden bytes)

The smallest complete blob: one object (component mask 0, sprite 0) with only a Create handler that stores 5 + 7 in global 0, and one string, `"HI"`. Both this engine's tests and the editor's compiler tests must reproduce these 63 bytes exactly.

```text
0000: 53 56 4D 42 01 04 00 00 01 00 01 00 01 00 00 00   header: "SVMB" v1, cells 4, 1 object, 1 string, 1 global
0010: 00 00 00 00 00 00 00 00 37 00 00 00 00 00 00 00   object 0: mask 0, sprite 0, Create @ 0x37, Step 0
0020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   Destroy, Collision, Anim End, Room Start: none
0030: 34 00 00 00 48 49 00 02 05 02 07 10 09 00 01      string 0 @ 0x34 = "HI"; code @ 0x37:
                                                         PUSH8 5, PUSH8 7, ADD, STG 0, HALT
```

`vm_load` accepts it; `vm_start(0, VM_EV_CREATE)` returns a context, the next `vm_step()` runs it, and afterwards `vm_global(0) == 12` and `vm_idle()`.

## Implementation notes

- **Files:** interpreter, scheduler and loader in `src/core/vm.c` (portable: no hardware access, unit-testable on the host); public header `include/serval/vm.h` with the `VM_OP_*`, `VM_EV_*`, `VM_P_*` and `VM_SYS_*` enums public (the tests, [`tools/svm.py`](#tools) and the compilers all need the numbers; the blob format and the Lua-subset compiler ([lua.md](lua.md)) are MIT, and Studio Advance's event editor compiles through the same Lua path).
- **Platform calls:** `src/core` must link on the host, where sound, music, text, buttons, brightness and blending don't exist. `vm.c` makes the portable SYS calls (`camera_set`, `random_range`, `path_start`, `path_stop`) itself and passes the others to `serval_vm_platform_call` ([`src/core/vm_internal.h`](../src/core/vm_internal.h)): `src/gba/vm_platform.c` (listed for both the GBA and web builds in `CMakeLists.txt`) calls the engine, and `src/host/platform.c` records each call in `serval_host_vm_calls` for the host tests.
- **Placement:** the dispatch loop and the collision pass run as Thumb in ROM. Measured with GCC 15.3 and `arm-none-eabi-size -A` (text and read-only data), `vm.o` is about 12.4 KB of ROM in a Release build (11.8 KB before the body properties, `VM_P_OBJECT` and the larger queue; 10.6 KB before `vm_collide`; 10.2 KB after milestone 6; the interpreter loop about 5.1 KB plus a 552 B jump table) and about 18.2 KB with debug checks (RelWithDebInfo). Its state takes 27.5 KB of EWRAM (`SERVAL_EWRAM_BSS`): contexts 11.6 KB (372 B each: 64 stack cells, 16 return points, the registers twice), instance fields 8 KB, array cells 4 KB, the queue 1.5 KB (256 events of 6 B), globals 1 KB, bindings 768 B, the collision pass's 352 B (the pairs, the two lists of slots, two 128-bit sets), 256 B counting the queue's Create events per entity slot and 80 B for a waiting behaviour's return points; the rest is 61 B of IWRAM (106 B with debug checks). A ROM that never calls the VM links none of it. The loop moves to IWRAM as ARM only if a script-heavy benchmark shows it pays, per the house rule.
- **The collision pass** (`collide_pair` in `vm.c`) lists each set with `ecs_gather` and tests with `body_overlap`, the overlap test first (most tests miss, and for them that is the whole cost); the checks that matter only on a hit (still alive and in the set, not the same entity, not a pair already tested the other way round) come after it. `destroy()` marks the slots it frees in a 128-bit set, cleared when a pair lists its sets, so a slot whose entity died during the pass is skipped even after a spawn reuses it: `destroy()` is the only way an entity dies while the VM runs.
- **Reactions on top of a behaviour:** a context keeps the behaviour's registers in a second set while a reaction runs, and the reaction's activation starts at the behaviour's stack top. The reaction's calls start at depth 0, in the same return-point slots as the behaviour's, so when the behaviour waits inside a `CALL` its return points are copied aside (one static set: reactions don't nest) and put back when the reaction ends. Events are never dispatched while a script runs, so one saved set is always enough: `vm_step` and `vm_events` called from inside a phase (by C code a script reached; no SYS call does in v1) warn and do nothing, and while `vm_kill` runs a Destroy reaction outside the phases it counts as a phase (loads are refused, another `vm_kill` is queued).
- **The queue** is a ring of `VM_EVENT_QUEUE` events. Attaching an entity (or `SPAWN`) queues its Create unless one is queued for it already, which the queue would have to be searched for; a count of the Create events queued for each entity slot makes that search happen only for a slot that has one, so attaching a whole room costs about 450 cycles per entity (`tests/rom/vm_platform_tests.c` logs it: 128 attaches about 58,000 cycles in a Release build, draining their Creates about 55,000), where searching the queue on every attach cost 282,000.
- **Hot reload of arrays:** `vm_reload` compares the new blob's RAM array layout with a 32-bit fingerprint of the old one's (FNV-1a over the array count, each array's kind and each RAM array's record), taken at load, rather than reading the old blob, which the debug link may have overwritten in place.
- **Dispatch:** a `switch` on the opcode byte is fine for v1; measure before anything cleverer.
- **Cost intuition:** ~280,000 CPU cycles per frame at 60 fps; budget scripts at well under 40,000. The design estimate was 50-100 cycles per simple op from a ROM Thumb switch dispatch; measured, an op costs about 195 (below), so the practical ceiling is a few hundred ops per frame across all scripts — consistent with "scripts decide what happens": a Step handler should be a handful of ops, and anything per-frame-heavy belongs in a C system.
  - **Measured** (the first script workload, [`fireflies`](../examples/fireflies/main.c), before `vm_collide`, when its collision test was C code outside the VM's time: a 1,117-byte blob, two threads, the serval's 30-op Step handler every frame, up to 8 fireflies waiting on paths, spawns, catches, HUD prints; gba-release in mGBA, one 60-second round without input, VM time read around `vm_step` and `vm_events`, figures for frames with 8 fireflies alive). Ops per frame: 33 on average, peak 148 (167 in a web run with scripted D-pad input; the same scripts, so the same counts on the GBA). VM time: 21,500 cycles per frame on average, peak 43,500; the whole frame (VM, paths, physics, collisions, animation, depth-sorted render) 55,400 on average, peak 77,100 (27% of the frame). Fitting VM time against ops over 2,400 frames gives about 14,500 cycles plus about 195 per op (an op's share of the SYS calls it makes included), 2-4 times the estimate above; at a steady 30 ops a frame the VM takes 19,400 cycles with 5 fireflies waiting and 20,800 with 8 (about 500 per waiting script). So in a typical frame most of the VM's time is fixed cost (the scheduler's passes over 32 contexts and 128 bindings, each waiting script's resume check) rather than ops. Debug checks on (RelWithDebInfo) add about 18% to the VM's time and 9% to the frame's. The busiest frame is the round's first, about 155,000 cycles (128,000 of them in the VM) while the screen is still black: the Room's setup, with the game's first `text_print` calls (the first sets the font up), the music and the serval's spawn. Milestone 8 starts from these numbers.

## Test plan

Shared suite `tests/vm_tests.c`, registered in both `tests/host/main.c` (ASan/UBSan) and `tests/rom/main.c`, with programs built by a small blob builder in the test file, kept independent of [`tools/svm.py`](#tools) on purpose (the tests were written from this document alone). Required coverage:

- Every opcode at least once; arithmetic edge cases (wrap, `DIV`/`MOD`/`FXDIV` by zero → 0 + warn, shift masking, `FXMUL` precision).
- Loader rejection: bad magic, version, cell width, counts, out-of-range handler and string offsets.
- Runtime safety: stack overflow/underflow, call depth, unknown opcode, unknown SYS id (its arity is unknown), `pc` escaping the blob — each warns and halts only the offending context. An unknown property warns and continues (`GETP` pushes 0, `SETP` drops), as the opcode reference says.
- Scheduling across simulated frames: `WAIT` counts, `WAIT_ANIM`, `WAIT_MOVE` (with a real path), reactions on top of waiting behaviours (Step every frame, Collision and Animation End while the behaviour waits, the behaviour's stack, frame and wait restored exactly), a wait or a budget overrun in a reaction halting only the reaction, the Destroy reaction then the halt, one Create for a double attach, `SPAWN` running Create in-phase, queue overflow, budget throttling.
- Frames: arguments and locals, recursion to the limits, `RET`/`RETV` dropping the frame, `ENTER`/`LDL`/`STL` bounds. Arrays (RAM and every ROM kind, bounds, `LEN`, reload rules), instance fields, `NEXTI` (including killing mid-loop), `VM_P_TAGS`, the animation and body properties (each truncated to its pool's type, `VM_P_BODY_CONTACT` read-only and reading what `sys_physics()` reported), `VM_P_OBJECT` and `vm_object_of` (attached, never attached, detached, dead, stale), `LSH`/`IDIV`/`IMOD` against Lua 5.4's results.
- Collisions (`vm_collide`): both sides and `OTHER`, only attached entities with a handler, the order of pairs and slots, each pair of entities once (one set against itself, sets that share entities, a pair set twice), reactions running before the next test (a kill, a tag change), a slot reused during the pass, the queue drained before the pass, more events than the queue holds, the limits, and the pairs outlasting loads.
- Determinism: two identical runs leave identical globals.
- Per the house rule, verify each new test can fail.

## Milestones

1. ~~Specify the format and opcode set~~ (this document).
2. ~~**Core interpreter:** loader + stack/variable/arithmetic/control ops, budget, validation; `vm_tests.c` for all of it, green on host and in the test ROM~~ ([`src/core/vm.c`](../src/core/vm.c), [`tests/vm_tests.c`](../tests/vm_tests.c)).
3. ~~**Scheduler:** contexts, waits, the two phases, event queue, `vm_attach`/`vm_detach`/`vm_kill`/`SPAWN`/`KILL`, Step dispatch, one-per-entity rule; frame-simulation tests~~.
4. ~~**Engine bridge:** `GETP`/`SETP`, the SYS page, bindings, `WAIT_ANIM` (adds `anim_finished()` to the ECS) and `WAIT_MOVE`; update [frame-loop.md](frame-loop.md) from proposed to confirmed~~. Collisions first reached scripts only through `vm_event(a, b, VM_EV_COLLISION)` from game C code after `body_overlap`; the engine-side pass, [`vm_collide`](#collisions), came with the API freeze.
5. ~~**Proof example:** a small `examples/` game whose logic is entirely hand-assembled bytecode (objects, Step movement, a collision, waits, a spawn, sound) — the usual example rules apply (header comment, `serval_add_rom`, ROM checks, web build, screenshots)~~ ([`fireflies`](../examples/fireflies/main.c), its listing now assembled at build time by [`tools/svm.py`](#tools); [what it exposed](examples-roadmap.md#what-fireflies-exposed): `VM_P_BODY_W`/`VM_P_BODY_H`, `VM_SYS_TEXT_PRINT_NUMBER`, `INTERRUPTIBLE`, and the costs below).
6. ~~**Revision for compiled Lua** (described in this document): frames, behaviours and reactions (`INTERRUPTIBLE` removed), instance fields, arrays, `LSH`/`IDIV`/`IMOD`, `NEXTI`, the new properties and SYS calls, header checks; `vm.c`, the tests, `svm.py` and `fireflies`' listing updated~~. Studio Advance's assembler is updated in that repository.
7. ~~**Lua-subset compiler prototype** ([lua.md](lua.md)): `fireflies` rewritten in Lua, with the same screenshots as the listing~~ ([`tools/svlua.py`](../tools/svlua.py), built by `serval_add_script`; the globals' initial values in the blob; tests on the VM and against real Lua; [`fireflies.lua`](../examples/fireflies/fireflies.lua), whose frames came out pixel-identical to the listing's, [what porting it showed](examples-roadmap.md#porting-fireflies-to-lua)).
8. **Debug and performance:** `BRK` semantics finalized with [debug-link.md](debug-link.md) (it only logs today), how `vm_reload` restarts behaviours (the first [open item](#open-items); `vm_reload` itself is implemented), the PC → line maps the debugger needs ([lua.md](lua.md#open-questions)), a script benchmark (in the spirit of bunnymark: [Cost intuition](#implementation-notes) has fireflies' numbers), and the IWRAM decision from its numbers.

Format v1 freezes with the first engine release.

Milestones 2 and 3 are pure `src/core` work with no hardware dependencies — buildable and testable entirely on the host.

## What format v1 fixes

Beyond the byte layout, these are part of format v1, and a compiler may rely on them: the limits `VM_STACK` (64), `VM_CALLS` (16), `VM_GLOBALS` (256), `VM_FIELDS` (16) and `VM_ARRAY_CELLS` (1024); the SYS page's numbers, names and argument and result counts; the property page; the header's flag bits (bit 0 the globals' initial values, bit 1 reserved for an extended handler table, the rest reserved); behaviours and reactions. `LDG`/`STG` may use any index below `VM_GLOBALS`; the header's global count only sizes the globals' initial values (with flag bit 0) and tells `vm_reload` whether the old values still fit. The opcode space is append-only like the pages: new opcodes take unassigned bytes and never change an existing one's meaning, so an older engine meets a newer opcode only as an unknown opcode (warn, halt context). After the first release that ships the VM, a change to the layout, an existing opcode or one of these limits is a new format version and a major engine version.

## Open items

- **Hot reload and behaviour scripts.** `vm_reload` keeps entities attached but halts every context, so an entity whose behaviour lives in a long-running Create handler (a firefly's wander loop) stands still after a reload: nothing restarts it, and re-queueing Create would repeat its side effects (`fireflies` counts live fireflies in Create). To settle with the debug link (milestone 8): a Resume event (a seventh event, so it needs the extended handler table header flag bit 1 is reserved for), a reload flag that re-queues Create, or a convention that behaviour belongs in Step.
- What the proof example found scripts couldn't say: per-instance variables, iterating an object's instances, arrays, game components a script can set and a field width for printed numbers are closed by milestone 6 ([examples-roadmap.md](examples-roadmap.md#what-fireflies-exposed)). Still open: aiming a path from a script (`path_start` has no heading).
- The event set will grow (buttons, timers, script-to-script messages); `VM_EV_*`, the property page and the SYS page are all append-only by design. An object record has six handler slots, all taken: header flag bit 1 is reserved for an extended handler table that holds the handlers of later events (a later version defines it; this one refuses blobs that set the bit).
- SYS calls for tracker music and sampled sound ([audio.md](audio.md), implemented in C), and for the API this version declares but doesn't implement yet, come appended to the page; so do calls for later API, and properties for new pools.
- `vm_collide` has no broad phase: a pair costs a test per entity of one set times entity of the other. A spatial grid could come later without changing the API, if games need large sets.
- 16-bit cells for a GB target: the header field and width-agnostic semantics keep the door open; nothing else is done for it in v1.
- Whether rooms bring per-room global banks or the compiler just partitions the global space (compiler-side concern for now).
- Everything type-shaped (checking, constant folding, dead handler elimination) is the compilers' job ([lua.md](lua.md), and Studio Advance's event editor through it) and stays out of the engine.
