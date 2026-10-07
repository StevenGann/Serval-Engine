# Scripting in a Lua subset

Game logic written in a statically checked **subset of Lua 5.4**, compiled ahead of time to the [bytecode VM](vm.md). There is no Lua runtime on the GBA: a script becomes the same kind of script blob the event editor's scripts do, with the same costs.

**Status:** the compiler prototype is implemented: [`tools/svlua.py`](../tools/svlua.py) (`svlua.py compile SCRIPT.lua -o OUT.svm`; tests in `tools/svlua_test.py`, CTest `svlua_tool`) compiles a script to a listing in [vm.md](vm.md#listing-syntax)'s revised language, and [`examples/fireflies/fireflies.lua`](../examples/fireflies/fireflies.lua) is `fireflies` in the subset. Until the VM revision it needs ([vm.md](vm.md#milestones) milestone 6) lands, its output is checked as text (golden listings), assembled by `svm.py` through a test shim for the new opcodes, and run on a model of the revised VM in the tests. Still to come: `serval_add_script()` accepting `.lua`, the tests against real Lua below, and the acceptance test (milestone 7): `fireflies.lua` giving the same screenshots as the hand-written listing.

## The rule

**Every program the compiler accepts means what it means in Lua 5.4 built with 32-bit integers** (`LUA_32BITS`). The subset is a strict subset, never a dialect: no construct changes meaning, and anything whose meaning would differ is a compile error that says why. Two consequences:

- A script can run under real Lua with a stub of the engine API (below), so the compiler is tested by running the same script both ways and comparing the results, and a game's logic can be unit-tested on a PC.
- What doesn't fit is rejected at compile time, never approximated at run time, with one documented exception: fixed-point numbers ([Types](#types)).

`LUA_32BITS` matters: stock Lua 5.4 has 64-bit integers, and the VM's cells are 32-bit. With it, integer overflow wraps the same way in both.

## A script

```lua
-- fireflies.lua (excerpt)
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
    path_start(self, random_range(0, PATH_COUNT - 1), random_range(0, MIRROR_XY))
    wait_move()
    wait(random_range(10, 40))
  end
  self.sprite = SPR_FIREFLY_FADE; self.frame = 0
  wait_anim()
  kill(self)
end

function Firefly:collision(player)   -- a reaction: runs to completion
  score = score + 1
  print(7, 0, score, 3)
  play_sound(SND_CHIME)
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
- **Handlers:** `function Name:create()`, `:step()`, `:destroy()`, `:collision(other)`, `:anim_end()`, `:room_start()`, the six `VM_EV_*` events. `create` and `room_start` are *behaviours* and may wait; the rest are *reactions* and run to completion ([vm.md](vm.md#behaviours-and-reactions)). `self` is the instance; `collision`'s parameter is the other entity. A Room Start handler on an object with no components is a *thread*, started from C with `vm_start`.
- **Globals:** top-level assignments and top-level `local` declarations become VM globals (`VM_GLOBALS` scalars). Their initial values must be constants, and they travel in the blob: `vm_load` sets them (a table the header's flag bit 0 announces; [vm.md](vm.md#header-16-bytes)), so a script starts with `playing = true` already true and C does nothing extra.
- **Arrays:** `name = array(n)` (RAM, n cells, zeroed) or `name = { 3, 5, 8, ... }` (ROM, a constant table of integers, stored in the narrowest kind that holds every element). Top level only.
- **Functions:** `function name(a, b) ... end` and `local function name(...)`, top level only.

**Names C sees** are upper-cased: an object `Firefly` is `OBJ_FIREFLY`, a global `score` is `G_SCORE` in the generated header, as C names its constants (two names that differ only in case are an error). `local NAME <const> = "text"` names a string for `print`, and a constant table of fixed values is a ROM array of their 256ths.

Names in ALL_CAPS that the script doesn't define are **constants from the game's C headers**, passed to the assembler ([`svm.py`](../tools/svm.py) `--header`), which knows the engine's and the game's `#define`s and enumerators. They are integers.

## Types

The compiler infers a static type for every expression and variable; mixing types wrongly is a compile error.

| Type | What it is | In the VM |
| --- | --- | --- |
| integer | Lua integer, 32-bit (`LUA_32BITS`) | a cell |
| fixed | a Lua float, kept as 24.8 fixed point | a cell (`FIXED`) |
| boolean | `true`, `false` | 1 or 0 |
| entity | an instance (`self`, `other`, `spawn(...)`) | its handle; no entity is `none` (0) |
| string | a literal, only as an argument to `print` | a string-table index |
| array | a top-level array | an array number |

- **Integers** wrap on overflow, as Lua's do with `LUA_32BITS`.
- **Fixed** values come from number literals with a decimal point (`1.5`), from the fixed-point properties (`x`, `y`, `vx`, `vy`, and `scale`, whose 8.8 storage has the same 256-is-one scaling: `self.scale = 1.5` is one and a half times the size), and from arithmetic on fixed values. Mixing an integer into fixed arithmetic converts it (`self.x - 4` subtracts four pixels). `/` always produces fixed (`7 / 2` is `3.5`, as in Lua), `math.floor(f)` turns fixed into an integer, and `math.tointeger` is not supported. **This is the one approximation:** a fixed value is a multiple of 1/256. Each literal is rounded to the nearest 1/256 and each operation rounds its result, and later operations can scale those roundings (`0.1 * 10` is 1.015625), so fixed results approximate Lua's floats with no general bound. Integer and boolean results are exact; a script that needs exact arithmetic uses integers (pixels times 256, say). Tests against real Lua compare fixed values within a tolerance each test states.
- **Variables** take the type of their first assignment; **function parameters and results** take the types their uses and call sites agree on, inferred over the whole program (a function called with both an integer and a fixed argument is an error: write two).

**Conditions must be booleans.** Lua treats `0` as true and the VM as false, so an integer in `if`, `while`, `repeat ... until` or `not` is a compile error with the hint `x ~= 0`. `and` and `or` take booleans and short-circuit; the `a and b or c` idiom on other types is an error.

## What compiles to what

| Lua | VM |
| --- | --- |
| `+ - *` on integers; unary `-` | `ADD SUB MUL NEG` |
| `+ - *` on fixed | `ADD SUB FXMUL` (integers converted with a multiply by 256, folded for constants) |
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

**Fields.** The engine's properties are fields by these names: `x y vx vy sprite frame flags angle depth scale body_w body_h tags anim_time anim_step`. Any other field name is an instance field; each distinct name gets one of the `VM_FIELDS` slots for the whole program (so `other.hp` means the same slot whatever `other` is), and more than `VM_FIELDS` distinct names is an error.

**Waits** are allowed only in behaviours and in functions called only from behaviours; the compiler checks the call graph, so a wait can never reach a reaction. The rule is static, so it is stricter than the VM: `wait(0)` in a reaction, which would continue at once, is rejected too.

**Numeric `for`** loops run exactly Lua 5.4's iteration count (the limit and step evaluated once, no overflow at the integer limits). A constant step of 0 is a compile error; one that is 0 at run time, an error in Lua, logs `'for' step is zero` (`TRACE`) and ends the handler. **Header constants** are folded only where the assembler's integers compute what Lua does; anything else runs in code, and where a constant is required (an object's components, an array's length, a global's initial value) it is an error.

## Engine functions

| Lua | SYS call |
| --- | --- |
| `play_sound(id)` | `psg_play` |
| `music_play(song)`, `music_stop()`, `music_pause()`, `music_resume()` | the music calls (songs by binding index) |
| `camera_set(x, y)` | `camera_set` |
| `print(col, row, "text")` | `text_print` |
| `print(col, row, n [, width])` | `text_print_number` (n an integer) |
| `random_range(lo, hi)` | `random_range` (an integer) |
| `button_down(mask)`, `button_pressed(mask)` | the button calls (booleans) |
| `brightness(level)` | `screen_set_brightness` |
| `path_start(e, path, flags)`, `path_stop(e)` | the path calls (paths by binding index) |
| `none` | the entity 0 |

**Not in the subset**, each a compile error naming the construct: tables other than the arrays above (no table constructors with keys, no nested tables, no `pairs`/`ipairs`), metatables, closures over a function's locals, varargs, multiple results, string operations at run time (`..` of two literals is folded), the standard library beyond `math.floor`, `math.abs`, `math.min`, `math.max`, `math.mininteger` and `math.maxinteger` (±2³¹ with 32-bit integers; the literal `-2147483648` is a float in Lua, as in C it overflows before the minus applies), coroutines (handlers already are), `nil` (use `none` for entities), and floats beyond the fixed-point rules.

## The tool

`tools/svlua.py`, Python 3 standard library only like the other tools, MIT like the engine. It compiles a `.lua` script to a `.svm` listing; [`svm.py`](vm.md#tools) assembles that, so `serval_add_script()` accepts a `.lua` file and runs both. Stages: a lexer, a recursive-descent parser for the subset (Lua 5.4's grammar, with anything outside the subset parsed far enough to name it in the error), name resolution, whole-program type inference, the wait and call-graph checks, and stack-machine code generation with constant folding. Errors give `file:line:column`, the construct, and a hint.

Studio Advance's event editor compiles its event blocks through the same path (blocks → this subset → bytecode), so there is one compiler to make correct.

## Testing

- **Against real Lua.** A stub of the engine API in Lua (entities as tables, the engine's RNG and timing reproduced, waits as coroutine yields) runs a script under Lua 5.4 built with `LUA_32BITS`; the same script compiled and run on the host VM must reach the same globals and entity state, frame by frame, for scripted input. Fixed-point values are compared within the documented tolerance.
- **Unit tests** for each stage, including one test per rejected construct, checking the message.
- **`fireflies` in Lua** reproduces the listing's screenshots pixel for pixel.

## Open questions

- Instance fields shared by name across all objects: simple and predictable, but 16 names for a whole program may be tight; a per-object assignment, checked wherever `other.field` is used, is the alternative. The prototype gives slots in order of first appearance.
- Source maps for the debug link (a PC → line table emitted beside the blob) belong with [debug-link.md](debug-link.md). Every line of the compiler's listing ends in `; file:line`, so `svm.py` could build the table from them.
