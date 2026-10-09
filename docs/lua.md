# Scripting in a Lua subset

Game logic written in a statically checked **subset of Lua 5.4**, compiled ahead of time to the [bytecode VM](vm.md). There is no Lua runtime on the GBA: a script becomes the same kind of script blob the event editor's scripts do, with the same costs.

**Status:** implemented ([vm.md](vm.md#milestones) milestone 7). [`tools/svlua.py`](../tools/svlua.py) (`svlua.py compile SCRIPT.lua -o OUT.svm`) compiles a script to a listing in [vm.md](vm.md#listing-syntax)'s language, which [`svm.py`](vm.md#tools) assembles; `serval_add_script(my_game game.lua ...)` runs both at build time, in the engine's tree and from a game's own project against a release archive ([development.md](development.md#building-a-game)). [`fireflies`](../examples/fireflies/fireflies.lua) is written in it, and plays a round frame for frame as the hand-written listing it replaced. Tested by `tools/svlua_test.py` (CTest `svlua_tool`: every stage, golden listings, compiled programs run on the engine's VM) and against real Lua by `tools/svlua_difftest.py` (CTest `svlua_difftest`, [below](#testing)).

## The rule

**Every program the compiler accepts means what it means in Lua 5.4 built with 32-bit integers** (`LUA_32BITS`). The subset is a strict subset, never a dialect: no construct changes meaning, and anything whose meaning would differ is a compile error that says why. Two consequences:

- A script can run under real Lua with a stub of the engine API (below), so the compiler is tested by running the same script both ways and comparing the results, and a game's logic can be unit-tested on a PC.
- What doesn't fit is rejected at compile time, never approximated at run time, with one documented exception: fixed-point numbers ([Types](#types)).

`LUA_32BITS` matters: stock Lua 5.4 has 64-bit integers, and the VM's cells are 32-bit. With it, integer overflow wraps the same way in both.

## A script

```lua
-- adapted from fireflies.lua
Firefly = object { components = C_POS | C_VEL | C_SPR | C_ANIM | C_BODY | C_FIREFLY,
                   sprite = SPR_FIREFLY }

score = 0
live = 0
local playing = true

function Firefly:create()            -- a behaviour: it may wait
  self.body_w = 8; self.body_h = 8
  self.frame = random_range(0, FIREFLY_FRAMES - 1)
  live = live + 1
  for flight = 1, random_range(3, 4) do
    if not playing then break end
    path_start(self, random_range(0, PATH_COUNT - 1),
               random_range(0, PATH_MIRROR_X | PATH_MIRROR_Y))
    wait_move()
    wait(random_range(10, 40))
  end
  self.sprite = SPR_FIREFLY_FADE; self.frame = 0
  wait_anim()
  kill(self)
end

function Firefly:collision(player)   -- a reaction: runs to completion
  score = score + 1
  text_print_number(7, 0, score, 3)
  psg_play(SND_CHIME)
  spawn(Sparkle, self.x - 4, self.y - 4)  -- x is fixed point: 4 means 4 pixels
  kill(self)
end

function Firefly:destroy()
  live = live - 1
end
```

## Program structure

A script file is a sequence of top-level statements, compiled once into one blob:

- **Objects:** `Name = object { components = expr, sprite = expr }`. Both fields are constant integer expressions (component bits `C_*` and sprite IDs come from the game's C headers, below). The object's number is its order of declaration.
- **Handlers:** `function Name:create()`, `:step()`, `:destroy()`, `:collision(other)`, `:anim_end()`, `:room_start()`, the six `VM_EV_*` events. `create` and `room_start` are *behaviours* and may wait; the rest are *reactions* and run to completion ([vm.md](vm.md#behaviours-and-reactions)). `self` is the instance; `collision`'s parameter is the other entity. Collision events come from C: the pairs of entity sets the game names once with `vm_collide` ([vm.md](vm.md#collisions)), which the VM then tests every frame, or the game's own `vm_event` calls. A Room Start handler on an object with no components is a *thread*, started from C with `vm_start`.
- **Globals:** top-level assignments and top-level `local` declarations (except `<const>` ones, which are constants) become VM globals (`VM_GLOBALS` scalars). Their initial values must be constants, and they travel in the blob: `vm_load` sets them (a table the header's flag bit 0 announces; [vm.md](vm.md#header-16-bytes)), so a script starts with `playing = true` already true and C does nothing extra. The listing declares them as `.globals PLAYING=1`; a script whose globals all start at 0 (false, none) gets no table, and an object named `Init` is an object like any other.
- **Arrays:** `name = array(n)` (RAM, n cells, zeroed) or `name = { 3, 5, 8, ... }` (ROM, a constant table of integers, stored in the narrowest kind that holds every element). Top level only.
- **Functions:** `function name(a, b) ... end` and `local function name(...)`, top level only.

**Names C sees** are upper-cased: an object `Firefly` is `OBJ_FIREFLY`, a global `score` is `G_SCORE` in the generated header, as C names its constants (two objects, globals or arrays whose names differ only in case are an error). `local NAME <const> = "text"` names a string for `text_print` (C sees a string by its text: `"TIME UP!"` is `STR_TIME_UP`), and a constant table of fixed values is a ROM array of their 256ths.

Names in ALL_CAPS that the script doesn't define are **constants from the game's C headers**, passed to the assembler ([`svm.py`](../tools/svm.py) `--header`), which knows the engine's and the game's `#define`s and enumerators. They are integers. Two of the engine's macros can be called, with a constant argument: `C_GAME(n)` (a game component) and `BODY_GRAVITY(n)` (a body's gravity scale).

## Types

The compiler infers a static type for every expression and variable; mixing types wrongly is a compile error.

| Type | What it is | In the VM |
| --- | --- | --- |
| integer | Lua integer, 32-bit (`LUA_32BITS`) | a cell |
| fixed | a Lua float, kept as 24.8 fixed point | a cell (`FIXED`) |
| boolean | `true`, `false` | 1 or 0 |
| entity | an instance (`self`, `other`, `spawn(...)`) | its handle; no entity is `none` (0) |
| string | a literal, only as an argument to `text_print` | a string-table index |
| array | a top-level array | an array number |

- **Integers** wrap on overflow, as Lua's do with `LUA_32BITS`.
- **Fixed** values come from number literals with a decimal point (`1.5`), from the fixed-point properties (`x`, `y`, `vx`, `vy`; `scale`, whose 8.8 storage has the same 256-is-one scaling: `self.scale = 1.5` is one and a half times the size; and `body_max_fall`, a speed like `vy`), and from arithmetic on fixed values. Mixing an integer into fixed arithmetic converts it (`self.x - 4` subtracts four pixels). `/` always produces fixed (`7 / 2` is `3.5`, as in Lua), `math.floor(f)` turns fixed into an integer, and `math.tointeger` is not supported. **This is the one approximation:** a fixed value is a multiple of 1/256. Each literal is rounded to the nearest 1/256 and each operation rounds its result, and later operations can scale those roundings (`0.1 * 10` is 1.015625), so fixed results approximate Lua's floats with no general bound. Integer and boolean results are exact; a script that needs exact arithmetic uses integers (pixels times 256, say). Tests against real Lua compare fixed values within a tolerance each test states.
- **Variables** take the type of their first assignment; **function parameters and results** take the types their uses and call sites agree on, inferred over the whole program (a function called with both an integer and a fixed argument is an error: write two).

**Conditions must be booleans.** Lua treats `0` as true and the VM as false, so an integer in `if`, `while`, `repeat ... until` or `not` is a compile error with the hint `x ~= 0`. `and` and `or` take booleans and short-circuit; the `a and b or c` idiom on other types is an error.

## What compiles to what

| Lua | VM |
| --- | --- |
| `+ - *` on integers; unary `-` | `ADD SUB MUL NEG` |
| `+ - *` on fixed | `ADD SUB FXMUL` (integers converted with a multiply by 256, folded for constants; a fixed times an integer is a plain `MUL`) |
| `/` | `FXDIV` |
| `//`, `%` | `IDIV`, `IMOD` (floored, as Lua's) |
| `& \| ~ <<`, unary `~` | `AND OR XOR LSH BNOT`; `a >> b` is `LSH a, -b` |
| `== ~= < <= > >=` | `EQ NE LT LE GT GE` (booleans) |
| `and or not` | jumps; `LNOT` |
| `local` in a function | a frame local (`ENTER`, `LDL`, `STL`) |
| a function call | arguments pushed, `CALL`; `ENTER p, n` in the callee; `RET` / `RETV` |
| `if`, `while`, `repeat`, numeric `for`, `break`, `goto` | `JMP`, `JZ`, `JNZ` |
| `self.x`, `other.frame`, ... | `GETP` / `SETP` with the property |
| `self.hp` (any other field name) | an instance field, `VM_P_FIELD0 + n` |
| `a[i]`, `#a` | `LDA` / `STA` with `i - 1`; `LEN` |
| `for e in instances(Firefly) do ... end` | a `NEXTI` loop |
| `spawn(Obj, x, y)`, `kill(e)` | `SPAWN`, `KILL` |
| `wait(n)`, `wait_anim()`, `wait_move()` | `WAIT`, `WAIT_ANIM`, `WAIT_MOVE` |
| the engine functions below | `SYS` |

**Fields.** The engine's properties are fields by these names: `x y vx vy sprite frame flags angle depth scale body_w body_h tags anim_time anim_step body_bounce body_friction body_max_fall body_gravity body_contact`. Each means what its C pool means and is stored in the pool's type, wrapping as C's would (`self.frame = 300` is 44; [vm.md](vm.md#entities)). Any other field name is an instance field; each distinct name gets one of the `VM_FIELDS` slots for the whole program (so `other.hp` means the same slot whatever `other` is), and more than `VM_FIELDS` distinct names is an error.

**Bodies.** The body's properties are its pools in [physics.h](../include/serval/physics.h), with C's numbers, so a script and C code tune a body the same way:

| Field | Type | Meaning |
| --- | --- | --- |
| `body_w`, `body_h` | integer | The size `body_overlap` and `vm_collide` test, in pixels (0-255) |
| `body_bounce` | integer | 0-254: the 256ths of its speed a floor bounce keeps (224 keeps 7/8; 0 stops it); 255: a perfect bounce. An integer, as in C: 255 isn't 255/256 |
| `body_friction` | integer | 256ths of its speed lost per frame sliding on a floor (0-255) |
| `body_max_fall` | fixed | Its fall speed limit in pixels per frame, like `vy` (`self.body_max_fall = 4` or `2.5`); 0 is no limit, and the pool holds 0 to 255.996 |
| `body_gravity` | integer | Its gravity scale, written as C writes it: `self.body_gravity = BODY_GRAVITY(8)` for half, `BODY_GRAVITY(0)` for none, `BODY_GRAVITY(-16)` for reversed. The value is C's: the scale in 16ths minus 16, so 0 is normal gravity. `BODY_GRAVITY(n)` takes a constant n from −112 to 143 |
| `body_contact` | integer | **Read-only** (assigning it is a compile error): what the body touched in the last `sys_physics()` (with `physics_set_contacts(true)`) or `sys_map_movement()`. Test its bits: `self.body_contact & MAP_CONTACT_FLOOR ~= 0` while a map body stands on the floor |

An object whose components include `C_KINEMATIC` (`physics.h`) makes **kinematic bodies**, which move only by their velocity: `sys_physics()` gives them no gravity or bounds, and their bodies collide like any other (`Shot = object { components = C_POS | C_VEL | C_BODY | C_KINEMATIC | C_SHOT }`; [runtime-systems.md](runtime-systems.md#physics)). An instance's components are its object's: a script changes only its game components (`tags`).

The contact bits are the C headers' constants: `MAP_CONTACT_FLOOR`, `_CEILING`, `_LEFT` and `_RIGHT` from `map.h` (the same bits as `physics.h`'s `BODY_SIDE_BOTTOM`, `_TOP`, `_LEFT` and `_RIGHT`) and `BODY_CONTACT_EXIT` from `physics.h`, so `serval_add_script()` names those headers (`HEADERS serval/map.h serval/physics.h`). `MAP_CONTACT_LADDER` is planned: the assembler doesn't know planned names, so a script can't use it until ladders are implemented (C can, with a warning, and it is never set until then).

**Waits** are allowed only in behaviours and in functions called only from behaviours; the compiler checks the call graph, so a wait can never reach a reaction. The rule is static, so it is stricter than the VM: `wait(0)` in a reaction, which would continue at once, is rejected too.

**Numeric `for`** loops run exactly Lua 5.4's iteration count (the limit and step evaluated once, no overflow at the integer limits). A constant step of 0 is a compile error; one that is 0 at run time, an error in Lua, logs `'for' step is zero` (`TRACE`) and ends the handler. **Header constants** are folded only where the assembler's integers compute what Lua does; anything else runs in code, and where a constant is required (an object's components, an array's length, a global's initial value) it is an error.

## Engine functions

Each is named after the C function it calls, and its SYS call ([vm.md](vm.md#engine-calls)) is that name in capitals: `psg_music_play` is `VM_SYS_PSG_MUSIC_PLAY`, which calls `psg_music_play()`.

| Lua | SYS call | Notes |
| --- | --- | --- |
| `psg_play(id)` | `PSG_PLAY` | a sound of the game's `psg_table_set` table |
| `psg_music_play(song)`, `psg_music_stop()`, `psg_music_pause()`, `psg_music_resume()` | `PSG_MUSIC_PLAY`, `_STOP`, `_PAUSE`, `_RESUME` | PSG music; `song` is an index into `VmBindings.psg_songs` |
| `camera_set(x, y)` | `CAMERA_SET` | whole pixels |
| `text_print(col, row, "text")` | `TEXT_PRINT` | a literal string or a `<const>` one, printable ASCII |
| `text_print_number(col, row, n [, width])` | `TEXT_PRINT_NUMBER` | n an integer; with a width of 1 or more, right-aligned in that many columns (no width: just the digits) |
| `random_range(lo, hi)` | `RANDOM_RANGE` | an integer |
| `button_down(mask)`, `button_pressed(mask)` | `BUTTON_DOWN`, `BUTTON_PRESSED` | booleans |
| `screen_set_brightness(level)` | `SCREEN_SET_BRIGHTNESS` | |
| `path_start(e, path, flags)`, `path_stop(e)` | `PATH_START`, `PATH_STOP` | `path` is an index into `VmBindings.paths` |
| `none` | | the entity 0 |

Lua's `print` is not one of them: it is Lua's console output, which the subset doesn't have (a compile error whose hint names `text_print` and `text_print_number`).

**C-only.** `vm_collide`, the collision pairs ([vm.md](vm.md#collisions)), has no builtin: like `vm_bind`'s songs and paths it is the game's configuration, set once from C at boot, and it outlasts every `vm_load`. The other C setup calls (loading assets, `vm_load`, `vm_start`) are C-only too.

**Not yet.** Tracker music (`music_*`) and sampled sound effects (`sfx_*`), which [audio.md](audio.md) declares as planned, have no SYS calls and so no builtins: the SYS page is append-only, and their calls arrive with their implementations, named after the same C functions. Until then a script plays PSG sound and music only.

**Not in the subset**, each a compile error naming the construct: tables other than the arrays above (no table constructors with keys, no nested tables, no `pairs`/`ipairs`), metatables, closures over a function's locals, varargs, multiple results, string operations at run time (`..` of two literals is folded), the standard library (`print` included) beyond `math.floor`, `math.abs`, `math.min`, `math.max`, `math.mininteger` and `math.maxinteger` (±2³¹ with 32-bit integers; the literal `-2147483648` is a float in Lua, as in C it overflows before the minus applies), coroutines (handlers already are), `nil` (use `none` for entities), `^` except between constants (folded: the VM has no power operation), and floats beyond the fixed-point rules.

## The tool

`tools/svlua.py`, Python 3 standard library only like the other tools (3.11 or later; `serval_add_script()` stops with an error on an older Python), MIT like the engine. It compiles a `.lua` script to a `.svm` listing; [`svm.py`](vm.md#tools) assembles that, so `serval_add_script()` accepts a `.lua` file and runs both. Stages: a lexer, a recursive-descent parser for the subset (Lua 5.4's grammar, with anything outside the subset parsed far enough to name it in the error), name resolution, whole-program type inference, the wait and call-graph checks, and stack-machine code generation with constant folding. Errors give `file:line:column`, the construct, and a hint.

Studio Advance's event editor compiles its event blocks through the same path (blocks → this subset → bytecode), so there is one compiler to make correct.

## Testing

- **Against real Lua** (`tools/svlua_difftest.py`, CTest `svlua_difftest`). Each program in [`tests/svlua/diff/`](../tests/svlua/diff) runs under Lua 5.4.8 built with `LUA_32BITS`, with [`tests/svlua/stub.lua`](../tests/svlua/stub.lua) as the engine's API, and compiled on the VM by `svlua_runner` ([`tests/svlua/runner.c`](../tests/svlua/runner.c)), from the same start with the same scripted input. After every printed frame the two must agree on every global, RAM array cell, attached instance's properties and fields, and the frame's engine calls (text, numbers, sounds, brightness): integers, booleans and entities exactly, fixed values within the tolerance each program states (default 1/256). The stub reproduces what a script can observe: instances as tables whose properties are truncated to their arrays' types and whose unset fields read 0, 0.0, false or none by type; entity handles from the ECS's FIFO of free slots and per-slot generations; behaviours as coroutines in a pool of contexts taken lowest first and resumed in pool order; reactions as plain calls; the event queue's rules; the frame's order; and `random_range`'s generator and scaling bit for bit. It doesn't model the ops budget, so the VM run must not warn (a program spreads heavy work over frames with `wait`), nor what the runner doesn't run (paths, animations, music bindings). Fourteen programs cover integer edge arithmetic, booleans and short circuits, loops at the integer limits, recursion, arrays of every kind, fields and property truncation, the body's properties, waits, spawning and killing, `instances()`, reactions on waiting behaviours, random sequences, fixed point and input. The test is skipped unless `SERVAL_LUA32` names such a Lua (`tools/setup-dev.sh --with-lua32`; CI builds one). `LUA_32BITS` makes Lua's floats 32-bit too; the tolerance on fixed values covers that as well.
- **Unit tests** for each stage, including one test per rejected construct, checking the message; golden listings; and compiled programs run on the VM (`svlua_test.py`).
- **`fireflies` in Lua** was compared with the hand-written listing frame by frame on the web build, with scripted input through a whole round, catches, the end and a restart: about a thousand frames, every one pixel-identical, and no warning on the web or the GBA ([examples-roadmap.md](examples-roadmap.md#porting-fireflies-to-lua)).

## Open questions

- Instance fields shared by name across all objects: simple and predictable, but 16 names for a whole program may be tight; a per-object assignment, checked wherever `other.field` is used, is the alternative. The prototype gives slots in order of first appearance.
- Source maps for the debug link (a PC → line table emitted beside the blob) belong with [debug-link.md](debug-link.md). Every statement in the compiler's listing ends in `; file:line`, so `svm.py` could build the table from them.
