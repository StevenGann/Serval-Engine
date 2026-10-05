# Object/event model and bytecode VM

Game logic uses GameMaker's mental model, compiled to a compact custom bytecode VM in the style of GB Studio's GBVM.

**Status:** planned; nothing here is implemented. Games are written in C against the [core API](core-api.md) and [ECS](ecs.md) today.

**Why not Lua:** its RAM footprint, interpretive overhead and garbage collection pauses are too costly at 16.78 MHz.

## Mapping GameMaker concepts

- **Object:** a prefab = default component set + a table of event → bytecode script.
- **Events:** Create, Step, Collision with X, Animation End, Destroy, Room Start.
- **Room:** tilemap layers + placed instances + required asset groups + camera spec.
- **Event generation:** engine systems push events into a queue, e.g. the collision system emits `(entity, other, EV_COLLISION)`. The VM runs matching handlers. See [frame-loop.md](frame-loop.md) for when dispatch happens.

## VM requirements

- Cooperative threads: scripts yield per frame, like coroutines.
- Fixed cost per opcode and no garbage collection.
- Scripts live in memory as bytecode so the emulator can hot-patch them ([debug-link.md](debug-link.md)).
- Heavy per-frame math stays in C systems; scripts decide *what* happens, not *how*.
- Opcodes are platform-neutral: no hardware addresses, and no hard dependency on 32-bit values, so the same bytecode can later target GB's 8-bit CPU ([platforms.md](platforms.md)).
- Sounds are ordinary script ops ([audio.md](audio.md)).

## Opcode set and encoding

Not yet defined. See [open-questions.md](open-questions.md). The script compiler that produces bytecode lives in the editor; the bytecode format itself is specified here so that the engine, the compiler and the debugger agree on it.

## Escape hatches for advanced users

- Plain C modules written against the engine API.
- Optional: a Lua-like language transpiled to C (e.g. Nelua) for native-speed scripting. Not required for 1.0.
