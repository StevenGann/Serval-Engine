# Scripting in a Lua subset

Game logic written in a statically checked **subset of Lua 5.4**, compiled ahead of time to the [bytecode VM](vm.md). There is no Lua runtime on the GBA: a script becomes the same kind of script blob the event editor's scripts do, with the same costs.

**Status:** the compiler prototype is implemented: [`tools/svlua.py`](../tools/svlua.py) (`svlua.py compile SCRIPT.lua -o OUT.svm`; tests in `tools/svlua_test.py`, CTest `svlua_tool`) compiles a script to a listing in [vm.md](vm.md#listing-syntax)'s revised language, and [`tests/svlua/fireflies.lua`](../tests/svlua/fireflies.lua) is `fireflies` in the subset. Until the VM revision it needs ([vm.md](vm.md#milestones) milestone 6) lands, its output is checked as text (golden listings), assembled by `svm.py` through a test shim for the new opcodes, and run on a model of the revised VM in the tests. Still to come: `serval_add_script()` accepting `.lua`, the tests against real Lua below, and the acceptance test (milestone 7): `fireflies.lua` giving the same screenshots as the hand-written listing.

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
- **Globals:** top-level assignments and top-level `local` declarations become VM globals (`VM_GLOBALS` scalars). Their initial values must be constants; the compiler emits a hidden Room Start initializer only if a game asks for one (an object named `Init` with a `room_start`, say), so C code still controls when scripts start.
- **Arrays:** `name = array(n)` (RAM, n cells, zeroed) or `name = { 3, 5, 8, ... }` (ROM, a constant table of integers, stored in the narrowest kind that holds every element). Top level only.
- **Functions:** `function name(a, b) ... end` and `local function name(...)`, top level only.

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
- **Fixed** values come from number literals with a decimal point (`1.5`), from the fixed-point properties (`x`, `y`, `vx`, `vy`), and from arithmetic on fixed values. Mixing an integer into fixed arithmetic converts it (`self.x - 4` subtracts four pixels). `/` always produces fixed (`7 / 2` is `3.5`, as in Lua), `math.floor(f)` turns fixed into an integer, and `math.tointeger` is not supported. **This is the one approximation:** a fixed value has 1/256 precision where Lua's float has more, so arithmetic on fixed values agrees with Lua to within 1/256 per operation, not exactly. Integer and boolean results are exact.
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

**Waits** are allowed only in behaviours and in functions called only from behaviours; the compiler checks the call graph, so a wait can never reach a reaction.

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

**Not in the subset**, each a compile error naming the construct: tables other than the arrays above (no table constructors with keys, no nested tables, no `pairs`/`ipairs`), metatables, closures over a function's locals, varargs, multiple results, string operations at run time (`..` of two literals is folded), the standard library beyond `math.floor`, `math.abs`, `math.min`, `math.max`, coroutines (handlers already are), `nil` (use `none` for entities), and floats beyond the fixed-point rules.

## The tool

`tools/svlua.py`, Python 3 standard library only like the other tools, MIT like the engine. It compiles a `.lua` script to a `.svm` listing; [`svm.py`](vm.md#tools) assembles that, so `serval_add_script()` accepts a `.lua` file and runs both. Stages: a lexer, a recursive-descent parser for the subset (Lua 5.4's grammar, with anything outside the subset parsed far enough to name it in the error), name resolution, whole-program type inference, the wait and call-graph checks, and stack-machine code generation with constant folding. Errors give `file:line:column`, the construct, and a hint.

Studio Advance's event editor compiles its event blocks through the same path (blocks → this subset → bytecode), so there is one compiler to make correct.

## Testing

- **Against real Lua.** A stub of the engine API in Lua (entities as tables, the engine's RNG and timing reproduced, waits as coroutine yields) runs a script under Lua 5.4 built with `LUA_32BITS`; the same script compiled and run on the host VM must reach the same globals and entity state, frame by frame, for scripted input. Fixed-point values are compared within the documented tolerance.
- **Unit tests** for each stage, including one test per rejected construct, checking the message.
- **`fireflies` in Lua** reproduces the listing's screenshots pixel for pixel.

## Open questions

- Whether globals' initial values come from a generated initializer or stay the game's job. The prototype: a global must start at 0, false or none (what `vm_load` leaves) unless the script declares an object named `Init`; then `Init:room_start` (the script's, or a generated one) first sets every global to its initial value, and C starts it with `vm_start`. RAM arrays are left to `vm_load`'s zeroing.
- Instance fields shared by name across all objects: simple and predictable, but 16 names for a whole program may be tight; a per-object assignment checked at `other.field` uses is the alternative. The prototype gives slots in order of first appearance; `fireflies.lua` uses none.
- Source maps for the debug link (a PC → line table emitted beside the blob) belong with [debug-link.md](debug-link.md). Every line of the prototype's listing ends in `; file:line`, so `svm.py` could build the table from them.
- The names C sees: the prototype upper-cases objects, globals and arrays in the listing (`Firefly` is `OBJ_FIREFLY`, `score` is `G_SCORE`), as C names its constants and as the hand-written `fireflies` listing does, so `main.c` runs either; two names that differ only in case are an error. The alternative is the script's names as they are (`OBJ_Firefly`).
- `scale` is 8.8 fixed point in the engine (256 is normal size), but the field list above makes it an integer; the prototype follows the list (`self.scale = 384`, not `1.5`).
- The compiler doesn't read the C headers, so it folds header constants only where the assembler's integers compute what Lua does (`+ - * & | ~`, `<<` by a constant, `//` and `%` by powers of two); `>>`, `//` and `%` by other numbers and comparisons of header constants run in code, and in places that need a constant (an object's components, an array's length, a global's initial value) they are errors.
- Rounding a float literal to 1/256 is an error that operations scale: `0.1 * 10` is 1.015625. "Within 1/256 per operation" holds for each operation on the values as stored, not against Lua's exact decimals.
- A `for` step of 0 at run time: Lua raises an error; the prototype's loop logs `'for' step is zero` (`TRACE`, debug builds) and ends the handler (`HALT`), the nearest the VM has. A constant step of 0 is a compile error.
- The wait rule is static and stricter than the VM: `wait(0)` in a reaction, which the VM allows, is rejected.
- `-2147483648` is a float in Lua (the literal overflows before the minus applies), so it doesn't fit fixed point; a script writes `-2147483647 - 1`. `math.mininteger` and `math.maxinteger` would help, but the standard library stops at `floor`, `abs`, `min` and `max`.
- The prototype accepts two things this document doesn't list: `<const>` strings (literals by another name, for `print`) and constant tables of fixed values (stored scaled by 256, in the narrowest kind).
