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

Names in ALL_CAPS that the script doesn't define are **constants from the game's C headers**, passed to the assembler ([`svm.py`](../tools/svm.py) `--header`), which knows the engine's and the game's `#define`s and enumerators. They are integers. Three of the engine's macros can be called, with constant arguments: `C_GAME(n)` (a game component), `BODY_GRAVITY(n)` (a body's gravity scale) and `COLOR_RGB(r, g, b)` (`screen.h`: a color from 8-bit red, green and blue, 0-255 each, as C makes it; for the palette calls, [below](#engine-functions)).

## Types

The compiler infers a static type for every expression and variable; mixing types wrongly is a compile error.

| Type | What it is | In the VM |
| --- | --- | --- |
| integer | Lua integer, 32-bit (`LUA_32BITS`) | a cell |
| fixed | a Lua float, kept as 24.8 fixed point | a cell (`FIXED`) |
| boolean | `true`, `false` | 1 or 0 |
| entity | an instance (`self`, `other`, `spawn(...)`) | its handle; no entity is `none` (0) |
| string | a literal, only as an argument to `text_print` | a string-table index |
| array | a top-level array, used by its name: indexed, `#`, or passed to a palette call ([below](#engine-functions)) | an array number |
| object | an object's name (`Coin`), an entity's object (`e.object`) | its number; −1 for no object |

- **Integers** wrap on overflow, as Lua's do with `LUA_32BITS`.
- **Fixed** values come from number literals with a decimal point (`1.5`), from the fixed-point properties (`x`, `y`, `vx`, `vy`; `scale`, whose 8.8 storage has the same 256-is-one scaling: `self.scale = 1.5` is one and a half times the size; and `body_max_fall`, a speed like `vy`), and from arithmetic on fixed values. Mixing an integer into fixed arithmetic converts it (`self.x - 4` subtracts four pixels). `/` always produces fixed (`7 / 2` is `3.5`, as in Lua), `math.floor(f)` turns fixed into an integer, and `math.tointeger` is not supported. **This is the one approximation:** a fixed value is a multiple of 1/256. Each literal is rounded to the nearest 1/256 and each operation rounds its result, and later operations can scale those roundings (`0.1 * 10` is 1.015625), so fixed results approximate Lua's floats with no general bound. Integer and boolean results are exact; a script that needs exact arithmetic uses integers (pixels times 256, say). Tests against real Lua compare fixed values within a tolerance each test states.
- **Variables** take the type of their first assignment; **function parameters and results** take the types their uses and call sites agree on, inferred over the whole program (a function called with both an integer and a fixed argument is an error: write two).

**Objects are compared, nothing else.** An object's name is a value only where it names an object (`spawn(Coin, x, y)`, `instances(Coin)`) and as an operand of `==` and `~=`, against an entity's object: `if other.object == Coin then ... end`, `a.object ~= b.object`. Kept in a variable, passed, or compared with a number or an entity, it is a compile error (in Lua an object is a table, never equal to a number). An entity that isn't attached to an object has none: its `object` equals no object's name.

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
| `self.hp` (any other field name, except the [reserved ones](#reserved-names)) | an instance field, `VM_P_FIELD0 + n` |
| `a[i]`, `#a` | `LDA` / `STA` with `i - 1`; `LEN` |
| `for e in instances(Firefly) do ... end` | a `NEXTI` loop |
| `spawn(Obj, x, y)`, `kill(e)` | `SPAWN`, `KILL` |
| `wait(n)`, `wait_anim()`, `wait_move()` | `WAIT`, `WAIT_ANIM`, `WAIT_MOVE` |
| `debug_break()` | `BRK` |
| the engine functions below | `SYS` |

**Fields.** The engine's properties are fields by these names: `x y vx vy sprite frame flags angle depth scale body_w body_h tags anim_time anim_step body_bounce body_friction body_max_fall body_gravity body_contact object`. Each means what its C pool means (`object`, what `vm_object_of()` returns) and is stored in the pool's type, wrapping as C's would (`self.frame = 300` is 44; [vm.md](vm.md#entities)). Any other field name is an instance field, unless it starts with a prefix reserved for later properties ([below](#reserved-names)); each distinct name gets one of the `VM_FIELDS` slots for the whole program (so `other.hp` means the same slot whatever `other` is), and more than `VM_FIELDS` distinct names is an error.

`body_contact` and `object` are **read-only**: the engine sets them, and assigning one is a compile error. `object` is the object the instance was spawned or attached as, for telling what a collision met without spending a game component on each kind: `function Hero:collision(other) if other.object == Coin then ... end end`.

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

The contact bits are the C headers' constants: `MAP_CONTACT_FLOOR`, `_CEILING`, `_LEFT` and `_RIGHT` from `map.h` (the same bits as `physics.h`'s `BODY_SIDE_BOTTOM`, `_TOP`, `_LEFT` and `_RIGHT`) and `BODY_CONTACT_EXIT` from `physics.h`, so `serval_add_script()` names those headers (`HEADERS serval/map.h serval/physics.h`). So is `MAP_CONTACT_LADDER` (bit 6, from `map.h`): `self.body_contact & MAP_CONTACT_LADDER ~= 0` while a map body touches a ladder, and a script climbs with `vy` and `body_gravity` ([tilemaps.md](tilemaps.md#ladders); `tests/ladder_climber.lua` climbs one). It was planned in 1.0.0-rc.1, when a script naming it failed to assemble; the version that implemented ladders made it work, with no change to scripts.

**Waits** are allowed only in behaviours and in functions called only from behaviours; the compiler checks the call graph, so a wait can never reach a reaction. The rule is static, so it is stricter than the VM: `wait(0)` in a reaction, which would continue at once, is rejected too.

**Numeric `for`** loops run exactly Lua 5.4's iteration count (the limit and step evaluated once, no overflow at the integer limits). A constant step of 0 is a compile error; one that is 0 at run time, an error in Lua, logs `'for' step is zero` (`TRACE`) and ends the handler. **Header constants** are folded only where the assembler's integers compute what Lua does; anything else runs in code, and where a constant is required (an object's components, an array's length, a global's initial value) it is an error.

## Reserved names

Two kinds of names are kept for what later engine versions add: instance fields can't start with the prefixes of later properties ([below](#instance-fields)), and a script can't declare, at the top level, the name of any function of the engine's C API, which may become a builtin: the planned functions ([below](#planned-functions)) and all the others ([C functions](#c-functions)).

### Instance fields

An instance field's name can't start with **`anim_`, `body_`, `ent_`, `map_`, `path_`, `pos_`, `spr_`, `vel_` or `vm_`**: those names belong to the engine's properties, today's and the ones later versions add. `self.body_speed = 2` is a compile error:

```
game.lua:12:3: error: self.body_speed: body_ is reserved for engine properties, and body_speed isn't one: an instance field can't take the name of a property a later engine version may add
  hint: rename the field, e.g. my_body_speed or bodyspeed (instance fields can't start with anim_, body_, ent_, map_, path_, pos_, spr_, vel_ or vm_)
```

Only the start of the name counts, as written (properties are lower case): `my_body_x`, `nobody_w`, `bodyx`, `sprite_x`, `entry` and `Body_x` are ordinary fields. Today's properties keep their names, prefixed (`body_w`, `anim_time`) or not (`x`, `angle`, `object`). The hint names the property when the field looks like a misspelt one (`body_bouce`: did you mean `body_bounce`?) or is its C pool's name (`spr_angle` is `angle` in a script, `pos_x` is `x`).

**Why.** A field that isn't a property is the script's own instance field, so a property added later would take a name some script may already use for a field, and silently change what that script means: a breaking change, on a property page that is otherwise append-only ([vm.md](vm.md#entities)). So **every property a later engine version adds has a name that starts with one of these prefixes, and no other name will ever become a property**. No script can be using such a name, and names without a reserved prefix (`speed`, `layer`, `palette`, `visible`) stay the scripts' for good. Reserving a list of likely short names instead would have been guesswork, and would have taken names scripts want. Decided before 1.0.0, when no released script could have used one ([api-freeze.md](api-freeze.md#decisions), D8).

The prefixes are those of the engine's per-entity data, in C and in Lua:

| Prefix | Why |
| --- | --- |
| `body_` | The body's pools ([physics.h](../include/serval/physics.h), `body_w` to `body_contact`) and their properties. Later body features (swept tests, body-to-body response, moving platforms, a platformer controller) may add more |
| `spr_` | The sprite's pools in C (`spr_id`, `spr_angle`, `spr_scale`, ...). Today's sprite properties dropped the prefix (`sprite`, `angle`, `scale`); later ones keep their C names, so a new sprite property can't take a short name a script already uses |
| `anim_` | The animation's properties (`anim_time`, `anim_step`; `spr_anim_*` in C), and later animation state |
| `path_` | The path follower's pools ([path.h](../include/serval/path.h): `path_heading`, `path_speed`, `path_step`, `path_time`), which scripts can't read yet: likely properties, with the path extras [api-freeze.md](api-freeze.md#later-additively-no-api-now) lists |
| `pos_`, `vel_` | The position and velocity pools in C (`pos_x`, `vel_y`; `x` and `vy` in scripts), and later state of the same kind (a previous position for swept tests, say) |
| `map_` | Map bodies (`C_MAPBODY`, `sys_map_movement()`) and their map features (ladders, slopes): per-entity map state would take the map module's prefix |
| `ent_` | The entity itself (`ent_mask`, `ent_has()` in C): properties of the entity rather than of one component (a second word of game components, beside `tags`), and any later property that fits no other prefix |
| `vm_` | The VM's state for an instance (`object` is what `vm_object_of()` returns), and later VM state a script may read |

Not reserved: `sys_` (C's systems are functions, never per-entity data), the engine's other function prefixes (`sprite_`, `camera_`, `text_`, `psg_`: global state, not an entity's) and `engine_` or `serval_` (`ent_` already holds what fits nowhere else).

### Planned functions

A script can't declare, at the top level, the name of one of the engine's **planned functions**: every function its headers mark `SERVAL_PLANNED` ([releases.md](releases.md#planned-api); the list is [api-freeze.md](api-freeze.md#planned-in-1x-declared-now)'s). This version has none. `debug_break`, the debug link's [script breakpoint](debug-link.md#script-breakpoints), was declared before 1.0.0 so that no script could take its name, and is a builtin now, with no arguments and no result, compiling to the VM's `BRK` opcode, not to a SYS call ([Engine functions](#engine-functions)). (`sprite_set_colors` and `tileset_set_colors` were planned too, and are builtins now: [Engine functions](#engine-functions). So were `raster_scroll`, `raster_backdrop` and `raster_clear`, implemented C-only: they take per-line tables, which scripts can't pass; and `psg_waves_set`, C-only too: it takes a table of waveforms, and scripts play the wave channel through sounds and songs on it; and tracker music's and sampled effects' `music_*` and `sfx_*`, builtins now ([Engine functions](#engine-functions)), and `audio_bank_set`, C-only: it takes the bank's pointer. Like every engine function's, their names stay reserved: [below](#c-functions).) The rules below hold for any function a later version declares planned; `music_play` shows them, as they applied to it until tracker music was implemented. `function music_play() ... end` was a compile error:

```
game.lua:3:10: error: function music_play: music_play is reserved: it names a planned engine function, which a later engine version may make a builtin
  hint: rename it, e.g. my_music_play (music_play is planned: tracker music, docs/audio.md#tracker-music)
```

The hint quotes the marker: the feature and the doc that describes it. Using a planned function is an error too, saying it is planned, where any other undefined name would be "not defined":

```
game.lua:12:3: error: music_play is planned, not implemented in this engine version (tracker music, docs/audio.md#tracker-music): scripts can't use it yet
  hint: planned API reaches scripts in the engine version that implements it, named as in C (docs/releases.md#planned-api)
```

A planned name is treated as a builtin's is, case by case:

| The name as | A builtin's (`psg_play`) | A planned function's (`music_play` until 1.x) |
| --- | --- | --- |
| A top-level declaration: `function name`, a global, object or array (`name = ...`), `local function name`, a top-level `local name` (`<const>` too) | Error: an engine function | Error: reserved |
| A local in a function or handler, a parameter (a collision handler's too), a `for` loop's variable | Allowed: in its scope the name is the local | Allowed, the same way |
| A use: a call, a value, an assignment in a function, a global's initial value | The builtin (assigning it is an error) | Error: planned |

**Why each case.**

- **Top-level declarations** are refused for a builtin's name: a script can't redefine an engine function. So the day a planned function became a builtin, a script that had declared its name would stop compiling, and implementing planned API, a minor version, would break it. Refused now, no script can have one. Top-level locals are VM globals in the subset ([Program structure](#program-structure)), refused like globals.
- **Locals, parameters and loop variables** may shadow a builtin, as in Lua: within its scope the name means the local, whatever the engine has. Its meaning can't change when the builtin arrives, so there is nothing to refuse, and a script keeps short local names like `sfx_play` for a sound it is about to play.
- **Uses** can't break anything: no script can use a planned function now. Saying "planned" instead of "not defined" tells the author it exists and why it doesn't work yet.

Field names (`self.music_play`) and labels are namespaces of their own, untouched. Names are compared as written: `Music_play` and `MUSIC_PLAY` are ordinary names.

**The set is read, not listed.** `svlua.py` reads the engine's headers beside it (`tools/../include/serval/`, in the repository and in the release archive) by the rule `tools/check-planned.py` checks (both use `svlua.py`'s `planned_api()`), and takes every function marked `SERVAL_PLANNED`; without the headers it stops with an error. A version that implements one drops its marker and, if scripts can call it, adds its SYS call and builtin by the same name ([Engine functions](#engine-functions)): the name goes from reserved to builtin with no list to update. A planned function that stays C-only once implemented (`audio_bank_set` takes a pointer to the sound bank) loses its marker and keeps its reservation, as every engine C function does ([below](#c-functions)).

**Planned constants** need nothing more. A script that names one (none is planned in this version; `MAP_CONTACT_LADDER` and `TILESET_LZ77` were, until ladders and LZ77 tilesets were implemented, and the tests use their own, `tests/planned/fake.h`) passes it to the assembler, which refuses it as planned, given the header (without it, as for any constant, the name is unknown; [Bodies](#what-compiles-to-what)). And a script that declares a name a header also defines (`local PSG_WAVE <const> = 3`, `MAP_LADDER = 0`) uses its own, for an implemented constant as for a planned one, so implementing a constant changes no script.

Decided before 1.0.0, when no released script could have used one of these names ([api-freeze.md](api-freeze.md#decisions), D9).

### C functions

A script can't declare, at the top level, the name of **any function the engine's public headers declare** (`include/serval/*.h`), implemented or planned, `static inline` ones included: `sprite_draw`, `map_load`, `entity_create`, `psg_music_set_tempo`, `frame_count`, `fx_mul`, `ent_has` and the rest, about 160 names. Builtins keep their own error ([Engine functions](#engine-functions)) and planned functions theirs ([above](#planned-functions)); for the others, `function sprite_draw() ... end` is:

```
game.lua:3:10: error: function sprite_draw: sprite_draw is reserved: it is an engine C function, which scripts may get as a builtin in a later version
  hint: rename it, e.g. my_sprite_draw (scripts can't take the engine's C function names at the top level, docs/lua.md#c-functions)
```

The cases are the planned functions' ([the table above](#planned-functions)): refused as top-level declarations (a function, a global, an object, an array, a `local function`, a top-level `local`); allowed as locals in functions and handlers, parameters and loop variables, which may shadow a builtin, so a builtin arriving can't change what they mean. Using one, which can't break anything, says what it is rather than "not defined":

```
game.lua:12:3: error: map_load is an engine C function, which scripts can't call: this engine version has no builtin for it
  hint: scripts call the engine through builtins, named after the C functions they call (docs/lua.md#engine-functions)
```

**Why.** Builtins are named after the C functions they call ([api-freeze.md](api-freeze.md#decisions), D6), and any function may get one in a later version: one implemented today without a builtin (`sprite_draw`, `map_load`, `psg_music_set_tempo`), or a planned one that stays C-only when implemented (`raster_scroll` and `sprite_set_tiles` take tables, which scripts can't pass) until a later version finds it a builtin. Reserving only the planned functions would free such a name the day its marker went, and a builtin added after that would stop a script that had declared it. Reserving every function makes any builtin an addition: no script can have the name. It costs scripts names they rarely want at the top level, and none as locals: nearly all carry an engine module's prefix (`sprite_`, `map_`, `fx_`, `vm_`, ...).

**The set is read, not listed**, as the planned functions are: `svlua.py` reads every function declaration and definition in the headers beside it, at file scope, leaving out comments, macros (`FX`, `ECS_FOR_EACH`, `C_GAME`), typedefs (of function types too), variables (`pos_x`, `ent_mask`, a pointer to a function) and the members of structs, and reads both branches of an `#if`: a function declared for one platform is reserved on all. A test compares what it reads with what GCC finds (`-aux-info`) in every header.

**Functions a later version adds** reserve their names in scripts from the version that declares them, and a builtin for one, then or later, takes nothing more. A script that had declared such a name at the top level must rename it, as a C game whose own function had the name must: a minor change, as adding any function to a C library is ([releases.md](releases.md#versioning), [development.md](development.md#planned-api)). New engine functions keep module prefixes (`psg_`, `sprite_`, `map_`, `raster_`, ...), which makes collisions with a script's own names unlikely.

Decided before 1.0.0, when no released script could have used one of these names ([api-freeze.md](api-freeze.md#decisions), D9).

## Engine functions

Each is named after the C function it calls, and its SYS call ([vm.md](vm.md#engine-calls)) is that name in capitals: `psg_music_play` is `VM_SYS_PSG_MUSIC_PLAY`, which calls `psg_music_play()`.

| Lua | SYS call | Notes |
| --- | --- | --- |
| `psg_play(id)` | `PSG_PLAY` | a sound of the game's `psg_table_set` table |
| `psg_music_play(song)`, `psg_music_stop()`, `psg_music_pause()`, `psg_music_resume()` | `PSG_MUSIC_PLAY`, `_STOP`, `_PAUSE`, `_RESUME` | PSG music; `song` is an index into `VmBindings.psg_songs` |
| `camera_set(x, y)` | `CAMERA_SET` | whole pixels |
| `text_print(col, row, "text")` | `TEXT_PRINT` | a literal string or a `<const>` one, printable ASCII |
| `text_print_number(col, row, n [, width])` | `TEXT_PRINT_NUMBER` | n an integer; with a width of 1 or more, right-aligned in that many columns, blank cells in front (no width: just the digits); no zero padding |
| `text_print_centered(row, "text")` | `TEXT_PRINT_CENTERED` | blanks the row and centers the text in it; the text as `text_print`'s |
| `text_clear()`, `text_set_shadow(on)` | `TEXT_CLEAR`, `TEXT_SET_SHADOW` | every cell blank; the font's drop shadow, `on` a boolean |
| `random_range(lo, hi)` | `RANDOM_RANGE` | an integer |
| `button_down(mask)`, `button_pressed(mask)` | `BUTTON_DOWN`, `BUTTON_PRESSED` | booleans |
| `screen_set_brightness(level)` | `SCREEN_SET_BRIGHTNESS` | |
| `screen_set_blend(top, bottom, top_weight, bottom_weight)` | `SCREEN_SET_BLEND` | integers: the `LAYER_*` masks (from `screen.h`) and the weights, 0-16 ([alpha blending](runtime-systems.md#alpha-blending)) |
| `path_start(e, path, flags)`, `path_stop(e)` | `PATH_START`, `PATH_STOP` | `path` is an index into `VmBindings.paths` |
| `sprite_set_colors(sprite, index, colors, count)`, `tileset_set_colors(index, colors, count)` | `SPRITE_SET_COLORS`, `TILESET_SET_COLORS` | palette writes ([sprites.md](sprites.md#palettes), [tilemaps.md](tilemaps.md#palette-writes)): `colors` is a top-level array of integers, by its name, and the call takes its first `count` elements, each a color's low 16 bits; `index` is C's (palette × 16 + color, from 0) |
| `music_play(id, loop)`, `music_stop()`, `music_pause()`, `music_resume()` | `MUSIC_PLAY`, `_STOP`, `_PAUSE`, `_RESUME` | tracker music ([audio.md](audio.md#tracker-music)): `id` a `MOD_*` of the sound bank, `loop` a boolean |
| `music_playing()`, `music_paused()` | `MUSIC_PLAYING`, `MUSIC_PAUSED` | booleans |
| `music_set_volume(volume)`, `music_set_speed(percent)` | `MUSIC_SET_VOLUME`, `MUSIC_SET_SPEED` | integers: 0-255; 50-200 percent of the module's tempo, 0 meaning 100 |
| `sfx_play(id)`, `sfx_play_ex(id, volume, pan, pitch, priority)` | `SFX_PLAY`, `SFX_PLAY_EX` | sampled effects ([audio.md](audio.md#sampled-sound-effects)): `id` an `SFX_*` of the sound bank; each returns the effect's handle, an integer (0 when nothing played). `pitch` is fixed (`1.0` as recorded, `2` an octave up, `0.5` down), the rest integers: volume 0-255, pan −128 (left) to 127, priority 0-255 |
| `sfx_stop(sfx)`, `sfx_playing(sfx)`, `sfx_stop_all()`, `sfx_set_volume(volume)` | `SFX_STOP`, `SFX_PLAYING`, `SFX_STOP_ALL`, `SFX_SET_VOLUME` | `sfx` a handle from `sfx_play`; `sfx_playing` a boolean; the volume 0-255 |
| `map_cell(mx, my)`, `map_set_cell(mx, my, metatile)` | `MAP_CELL`, `MAP_SET_CELL` | the playfield's cells ([tilemaps.md](tilemaps.md#runtime-changes)), in metatiles: `map_cell` an integer, the metatile (0 outside the map); `map_set_cell` a broken brick, a collected gem |
| `map_collision_at(x, y)`, `map_tags_in(x, y, w, h)` | `MAP_COLLISION_AT`, `MAP_TAGS_IN` | integers, in world pixels: one pixel's collision byte (`& 15` its type, `MAP_SOLID` 1), and the tags a rectangle overlaps, ORed ([tilemaps.md](tilemaps.md#tags)) |
| `debug_break()` | (`BRK`) | the debug link's [script breakpoint](debug-link.md#script-breakpoints): stops the whole game here while a debugger is attached (`STOPPED`, brk), and goes on after itself when resumed; with none, debug builds log `vm: BRK at 0x...` and go on, release builds do nothing. No SYS call |
| `none` | | the entity 0 |

**Palette writes** take their colors from an array: a constant table (`{ COLOR_RGB(0, 64, 160), 0x7FFF }`) or a RAM array the script fills, as a palette cycle does:

```lua
water = { COLOR_RGB(0, 64, 160), COLOR_RGB(0, 96, 200), COLOR_RGB(32, 160, 248) }
shown = array(3)

function Pool:room_start()
  local t = 0
  while true do
    for i = 1, 3 do shown[i] = water[(i + t) % 3 + 1] end
    tileset_set_colors(1 * 16 + 1, shown, 3) -- palette 1, colors 1 to 3
    t = t + 1
    wait(8)
  end
end
```

The VM reads the elements at the call (the array may change right after) and the colors reach the screen at the next `frame_end()`, as from C. A count of 0 does nothing; an array the blob doesn't have, or a count that is negative, past the array's length or past 256, warns and makes no call ([vm.md](vm.md#engine-calls)). There is no `color_mix` builtin: a fade mixes a color's channels with integer arithmetic (`c & 31`, `(c >> 5) & 31` and `(c >> 10) & 31` are its red, green and blue, 0-31).

**Tracker music and sampled effects** take their IDs from the sound bank's header (`MOD_*` for modules, `SFX_*` for samples), which `serval_add_soundbank()` writes with the bank ([audio.md](audio.md#sound-bank); mmutil's own, for a bank Studio Advance builds): name it in `serval_add_script()`'s `HEADERS`, as any game header. An effect's handle is an integer, kept in a local, a global or an instance field and passed back to `sfx_stop` and `sfx_playing`; `0`, C's `SFX_NONE`, when nothing played, and stale once the effect ends. Values outside C's ranges don't wrap as a C cast would: a volume, pan or priority is clamped (warns), an ID past 65535 plays nothing (the call warns; [vm.md](vm.md#engine-calls)).

```lua
Ship = object { components = C_POS | C_SPR, sprite = SPR_SHIP }

function Ship:create()
  music_play(MOD_THEME, true)
  self.hum = sfx_play_ex(SFX_ENGINE, 96, 0, 1.0, 1) -- a sample that loops
end

function Ship:step()
  if button_pressed(BUTTON_A) then sfx_play(SFX_LASER) end
  if button_pressed(BUTTON_START) then
    if music_paused() then music_resume() else music_pause() end
  end
end

function Ship:destroy()
  sfx_stop(self.hum)
  sfx_play_ex(SFX_BOOM, 255, 0, 0.5, 200) -- an octave down, over the others
end
```

The bank itself is C's: `audio_bank_set` has no builtin, as it takes a pointer; the game registers the bank at boot, as it binds songs and paths.

**The map** is the playfield the game's C code loaded (background 2), read by metatile (`map_cell`) or by world pixel (`map_collision_at`, `map_tags_in`), and changed by `map_set_cell`; the tags' meanings are the game's (`#define TAG_GEM MAP_TAG(1)` in a header the script names), and so are the metatile numbers. A platformer's player, a 16 x 16 body, breaks the brick it bumps into from below, collects gems and is hurt by spikes:

```lua
Player = object { components = C_POS | C_VEL | C_BODY | C_MAPBODY }

gems = 0

function Player:step()
  local x, y = math.floor(self.x), math.floor(self.y)
  local above = (y - 1) // 16
  if self.body_contact & MAP_CONTACT_CEILING ~= 0 and map_cell((x + 8) // 16, above) == MT_BRICK then
    map_set_cell((x + 8) // 16, above, MT_EMPTY)
  end
  if map_tags_in(x, y, 16, 16) & TAG_GEM ~= 0 then
    map_set_cell((x + 8) // 16, (y + 8) // 16, MT_EMPTY)
    gems = gems + 1
    text_print_number(5, 0, gems, 3)
  end
  if map_tags_in(x, y + 16, 16, 1) & TAG_SPIKES ~= 0 then self.hp = self.hp - 1 end
end
```

A metatile outside 0-65535 doesn't wrap: it is one the layer lacks, and `map_set_cell` warns and changes nothing ([vm.md](vm.md#engine-calls)).

Lua's `print` is not one of them: it is Lua's console output, which the subset doesn't have (a compile error whose hint names `text_print` and `text_print_number`). The text layer's other setup (`text_set_color`, `text_set_style`, `text_set_style_color`, `text_print_centered_in`, `text_clear_area`, `text_print_line`) has no builtin in this version; their names are [reserved](#c-functions).

**C-only.** `vm_collide`, the collision pairs ([vm.md](vm.md#collisions)), has no builtin: like `vm_bind`'s songs and paths it is the game's configuration, set once from C at boot, and it outlasts every `vm_load`. The other C setup calls (loading assets, `vm_load`, `vm_start`) are C-only too. Their names, as every C function's, are [reserved](#c-functions).

**Planned functions** have no SYS calls and so no builtins: the SYS page is append-only, and a function scripts can call gets both in the version that implements it, named after the same C function, as tracker music and sampled effects did (`debug_break`'s builtin needed none: it compiles to `BRK`). Until then its name is [reserved](#planned-functions).

**Not in the subset**, each a compile error naming the construct: tables other than the arrays above (no table constructors with keys, no nested tables, no `pairs`/`ipairs`), metatables, closures over a function's locals, varargs, multiple results, string operations at run time (`..` of two literals is folded), the standard library (`print` included) beyond `math.floor`, `math.abs`, `math.min`, `math.max`, `math.mininteger` and `math.maxinteger` (±2³¹ with 32-bit integers; the literal `-2147483648` is a float in Lua, as in C it overflows before the minus applies), coroutines (handlers already are), `nil` (use `none` for entities), `^` except between constants (folded: the VM has no power operation), and floats beyond the fixed-point rules.

## The tool

`tools/svlua.py`, Python 3 standard library only like the other tools (3.11 or later; `serval_add_script()` stops with an error on an older Python), MIT like the engine. It compiles a `.lua` script to a `.svm` listing; [`svm.py`](vm.md#tools) assembles that, so `serval_add_script()` accepts a `.lua` file and runs both. Stages: a lexer, a recursive-descent parser for the subset (Lua 5.4's grammar, with anything outside the subset parsed far enough to name it in the error), name resolution, whole-program type inference, the wait and call-graph checks, and stack-machine code generation with constant folding. Errors give `file:line:column`, the construct, and a hint. It reads the engine's headers beside it (`include/serval/`) for the engine's functions, planned and implemented, whose names it [reserves](#c-functions).

Studio Advance's event editor compiles its event blocks through the same path (blocks → this subset → bytecode), so there is one compiler to make correct.

### Debug information

The maps from pcs to source lines a debugger needs ([debug-link.md](debug-link.md#maps)), decided 2026-10-10 and **implemented** (1.x: the Implementation table's E3 and E4). They are **side files** the build writes beside the blob, **never in the blob**, one from each tool, since each knows one half:

- `svm.py asm LISTING --map OUT.svmap`: each op's pc and the listing line it came from, with the blob's size and FNV-1a hash, so the debugger knows which blob a map is for. It knows nothing about Lua, so it serves hand-written listings too.
- `svlua.py compile SCRIPT.lua --debug-info OUT.svdbg`: listing lines to `file:line:column` and statement starts, functions and handlers, locals with their slots and scopes, and the globals, fields, arrays, objects and strings by name, number and type.
- `svlua.py compile SCRIPT.lua --keep-layout OLD.svdbg`: the globals, fields and arrays an earlier build's `.svdbg` has keep their numbers, and new ones are appended, so a hot reload never scrambles state (or the numbers C was built with) when an edit adds a global in the middle.
- `serval_add_script()` writes `<basename>.svmap` and, for a Lua script, `<basename>.svdbg` beside `<basename>.svm` and `<basename>_script.c` in the target's binary directory, rebuilt with the blob ([development.md](development.md#building-a-game)). Its arguments didn't change for them.
- `serval_add_script(<target> <script.lua> KEEP_LAYOUT <old.svdbg> ...)` (1.x, added with the debug link's hot reload, [debug-link.md](debug-link.md#hot-reload)) passes `--keep-layout <old.svdbg>`, and rebuilds the script when that file changes. It names an earlier build's `.svdbg`, a copy (the call's own `<basename>.svdbg`, which the build rewrites, is refused at configure time), and may be a file the same project generates (the host tests keep `link_reload_v1.svdbg`'s layout for a build of `link_reload_v2.lua`); an empty value is none, so a variable that may be unset can be passed (`KEEP_LAYOUT "${GAME_KEEP_LAYOUT}"`). Lua scripts only. Given right after the script, before `SYMBOL`, `PREFIX` and `HEADERS`, it is ignored by an engine from before it (two unparsed words), which a build for a debugger that reloads can rely on.

All are additions: new options, and files no build reads but a debugger's. Neither tool's listing syntax changes, and without `--keep-layout` the listing is what it was, but for two comments: a statement's first op names the statement's own line (`local x =` on one line and its value on the next: the first op is the statement's), and a script's text copied into a comment loses the characters Python's `str.splitlines()` ends a line at (a form feed, say), so both tools count the listing's lines alike. `tools/check-debug-maps.py BASE...` joins a build's two files the way a debugger does and checks them against each other, the blob and the listing (CTest `debug_maps`).

#### The files

Both are JSON objects, with `"format"` and `"version"` (1) first, written one key and one list entry a line; the same input gives the same bytes. A reader ignores keys it doesn't know; a change an older reader would misread raises the version. Line numbers start at 1, the listing's counted as `svm.py` counts them; columns start at 1.

**`.svmap`**, from `svm.py`:

| Key | Holds |
| --- | --- |
| `format`, `version` | `"svmap"`, 1 |
| `listing` | The listing's path, as given to `svm.py` (for a hand-written one, what a debugger shows) |
| `blob` | `{"size", "fnv1a32"}`: the blob's size and FNV-1a 32 hash (a number), as `AGENT_INFO` and `LOADED` report them |
| `pcs` | `[pc, listing line]` for every op, in pc order. A pc is a blob offset, as the agent reports it. A listing line holds one op, so the line changes at every op; a `.byte` line counts as one (its bytes may be code: `svm.py dis` writes what it can't name so). The tables, and the strings and ROM data the assembler places, have none. The op at a pc is the last pair at or before it |

**`.svdbg`**, from `svlua.py`; lists in the order given, the program's names by number:

| Key | Each entry |
| --- | --- |
| `format`, `version` | `"svdbg"`, 1 |
| `sources` | A script's path, as given to `svlua.py` (one file today) |
| `lines` | `[listing line, source, line, column, statement start]` (`source` indexes `sources`; the start 0 or 1) for every listing line holding an op: exactly the `.svmap`'s lines |
| `functions` | `{"name", "object", "event", "other", "first", "last", "params"}` per handler and function, in listing order: `name` as `Firefly:create` or `bounce`; a handler's object, event (`create`, `step`, `destroy`, `collision`, `anim_end`, `room_start`: `VM_EV_*` 0 to 5) and, for a collision, `other`, its parameter's name (the context's other entity); each `null` for a function. `first` is its `.handler` or label line, `last` its last op's line: the ranges are disjoint and every op is in one. `params`: `ENTER`'s p, the frame cells its parameters take (0 for a handler: `self` and `other` come with the context). A function nothing calls isn't compiled, nor listed |
| `locals` | `{"name", "function", "slot", "type", "scope"}` per named local and parameter: `function` indexes `functions`; `slot` as `LDL` numbers it, so its value is the stack cell at the frame's `fp` plus `slot`; `scope`, `[first, last]`, the listing lines of the first and last ops where it holds its value |
| `globals`, `fields` | `{"name", "number", "type"}`: a field's number is n in `VM_P_FIELD0 + n` |
| `arrays` | `{"name", "number", "kind", "type"}`: `kind` is `ram`, or a ROM array's element kind (`s8`, `u8`, `s16`, `u16`, `s32`); `type` the elements'. Its length and its cells or data are in the blob's array table, at its number |
| `objects` | `{"name", "number"}` |
| `strings` | `{"name", "number", "text"}`: `name` as the generated header gives it to C (`STR_TIME_UP` is `TIME_UP`) |

A `type` is `integer`, `fixed`, `boolean` or `entity`: the debugger shows a fixed cell as `1.5`, a boolean as `true`, an entity as a handle.

- **Statement starts** are where a breakpoint on a line goes and where a step stops: the first op of each statement, with the statement's own line and column (its first token), even when that op computes something on a later line; an `elseif`'s test and an `until`'s test (lines the code comes back to); and the implicit `HALT` or `RET` at a function's `end`, when the code can reach it. Not: `ENTER`, a loop's per-iteration code, jumps, or statements that make no code (`local K <const> = 4`; a `repeat` starts at its body's first statement). A line can hold several (`if not playing then break end`: the `if`'s and the `break`'s).
- **Scopes**: a local holds its value from the op after the `STL` that declares it to the last op of its block; a parameter or a loop's variable from the first op of its body; a function's top-level locals and parameters through the `HALT` or `RET` at its `end`; a `repeat`'s body's locals through its `until`. A local no op sees (`do local unused = 5 end`) isn't listed, nor are the compiler's hidden cells (a loop's limit and step). When two locals in scope share a name, the one whose scope starts later shadows the other. To name a frame's locals, a debugger takes the frame's pc, and for a return point (`FRAMES`' later pcs) the pc before it, inside the `CALL`.
- **Mapping back**: a pc's listing line is the last `pcs` pair at or before it, and its place in the script that line's `lines` entry; a breakpoint on a line of the script is the pcs of its statement starts.
- **`--keep-layout`**: a global, field or array the old file names keeps its number; a new one takes the next number after every number the old file used, in declaration order (fields: first use); a number an edit freed stays free, held in the blob by an unnamed global or an empty RAM array, until the limit (`VM_GLOBALS`, `VM_FIELDS`) is reached, and then new names take the free numbers, lowest first. Objects keep their declaration order, as always. An old file that isn't a `.svdbg` version 1, or names a number twice or one past the limit, is an error.

An example, `coin.lua`:

```lua
Coin = object {}
score = 0

function Coin:collision(hero)
  local bonus = 2
  if hero.y < self.y then
    bonus = bonus * 2
  end
  score = score + bonus
  kill(self)
end
```

compiles (`svlua.py compile coin.lua -o coin.svm --debug-info coin.svdbg`) to a listing whose lines 14 to 43 are the handler (line 16 `ENTER 0, 1`, 18 `PUSH 2`, 19 `STL 0`, 21 `OTHER`, ..., 43 `HALT`), and to:

```json
{
  "format": "svdbg",
  "version": 1,
  "sources": [
    "coin.lua"
  ],
  "lines": [
    [16, 0, 4, 1, 0],
    [18, 0, 5, 3, 1],
    [19, 0, 5, 3, 0],
    [21, 0, 6, 3, 1],
    [22, 0, 6, 6, 0],
    [23, 0, 6, 15, 0],
    [24, 0, 6, 15, 0],
    [25, 0, 6, 13, 0],
    [26, 0, 6, 13, 0],
    [28, 0, 7, 5, 1],
    [29, 0, 7, 21, 0],
    [30, 0, 7, 19, 0],
    [31, 0, 7, 5, 0],
    [35, 0, 9, 3, 1],
    [36, 0, 9, 19, 0],
    [37, 0, 9, 17, 0],
    [38, 0, 9, 3, 0],
    [40, 0, 10, 3, 1],
    [41, 0, 10, 3, 0],
    [43, 0, 11, 1, 1]
  ],
  "functions": [
    {"name": "Coin:collision", "object": "Coin", "event": "collision", "other": "hero", "first": 14, "last": 43, "params": 0}
  ],
  "locals": [
    {"name": "bonus", "function": 0, "slot": 0, "type": "integer", "scope": [21, 43]}
  ],
  "globals": [
    {"name": "score", "number": 0, "type": "integer"}
  ],
  "fields": [],
  "arrays": [],
  "objects": [
    {"name": "Coin", "number": 0}
  ],
  "strings": []
}
```

and `svm.py asm coin.svm --map coin.svmap` writes (the code starts at 48, after the header, one object and one global):

```json
{
  "format": "svmap",
  "version": 1,
  "listing": "coin.svm",
  "blob": {"size": 82, "fnv1a32": 3462200235},
  "pcs": [
    [48, 16],
    [51, 18],
    [53, 19],
    [55, 21],
    [56, 22],
    [58, 23],
    [59, 24],
    [61, 25],
    [62, 26],
    [65, 28],
    [67, 29],
    [69, 30],
    [70, 31],
    [72, 35],
    [74, 36],
    [76, 37],
    [77, 38],
    [79, 40],
    [80, 41],
    [81, 43]
  ]
}
```

A breakpoint on line 7 is pc 65; a context stopped at pc 70 is on listing line 31, `coin.lua:7`, with `bonus` (in scope from 21 to 43) in its frame's cell `fp + 0`; the `if` on line 6 starts at pc 55.

For a sense of size: fireflies' `.svmap` is 7.5 KB and its `.svdbg` 15 KB (a 1,201-byte blob of 451 ops); the jukebox's `pads.lua`, 2.1 KB and 4.3 KB (359 bytes, 125 ops).

## Testing

- **Against real Lua** (`tools/svlua_difftest.py`, CTest `svlua_difftest`). Each program in [`tests/svlua/diff/`](../tests/svlua/diff) runs under Lua 5.4.8 built with `LUA_32BITS`, with [`tests/svlua/stub.lua`](../tests/svlua/stub.lua) as the engine's API, and compiled on the VM by `svlua_runner` ([`tests/svlua/runner.c`](../tests/svlua/runner.c)), from the same start with the same scripted input. After every printed frame the two must agree on every global, RAM array cell, attached instance's properties and fields, and the frame's engine calls (text, numbers, sounds, brightness, blending, palette writes with the colors they read, tracker music and sampled effects with their arguments as C gets them): integers, booleans and entities exactly, fixed values within the tolerance each program states (default 1/256). The stub reproduces what a script can observe: instances as tables whose properties are truncated to their arrays' types and whose unset fields read 0, 0.0, false or none by type; entity handles from the ECS's FIFO of free slots and per-slot generations; behaviours as coroutines in a pool of contexts taken lowest first and resumed in pool order; reactions as plain calls; the event queue's rules; the frame's order; and `random_range`'s generator and scaling bit for bit; the sound queries meet the same stand-in for a mixer on both sides (music plays from `music_play` until `music_stop`, paused from `music_pause` until `music_resume`; effects' handles count from 1 and play until stopped). It doesn't model the ops budget, so the VM run must not warn (a program spreads heavy work over frames with `wait`), nor what the runner doesn't run (paths, animations, music bindings). Seventeen programs cover integer edge arithmetic, booleans and short circuits, loops at the integer limits, recursion, arrays of every kind, fields and property truncation, the body's properties, objects compared, waits, spawning and killing, `instances()`, reactions on waiting behaviours, random sequences, fixed point, input, palette writes and sound. The test is skipped unless `SERVAL_LUA32` names such a Lua (`tools/setup-dev.sh --with-lua32`; CI builds one). `LUA_32BITS` makes Lua's floats 32-bit too; the tolerance on fixed values covers that as well.
- **Unit tests** for each stage, including one test per rejected construct, checking the message; golden listings; and compiled programs run on the VM (`svlua_test.py`).
- **`fireflies` in Lua** was compared with the hand-written listing frame by frame on the web build, with scripted input through a whole round, catches, the end and a restart: about a thousand frames, every one pixel-identical, and no warning on the web or the GBA ([examples-roadmap.md](examples-roadmap.md#porting-fireflies-to-lua)).

## Open questions

- Instance fields shared by name across all objects: simple and predictable, but 16 names for a whole program may be tight; a per-object assignment, checked wherever `other.field` is used, is the alternative. The prototype gives slots in order of first appearance.
- ~~Source maps for the debug link~~: decided 2026-10-10, two side files, never in the blob; implemented ([Debug information](#debug-information)).
