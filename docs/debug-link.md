# Debug link (runtime side)

**Status:** planned; the protocol is not defined and nothing of it is implemented. One name is declared for it as planned API, [`debug_break()`](#script-breakpoints). Two runtime pieces it will use exist: `vm_reload()`, which swaps a running game's scripts and keeps their state ([vm.md](vm.md#hot-reload)), and the `BRK` opcode, which today only logs, in debug builds ([vm.md](vm.md#debug)). Today the engine writes warnings and `debug_log()` lines to mGBA's debug log, or to the browser's console on the web ([api-reference.md](api-reference.md#debugh)).

The debug link connects the editor to a running game inside the emulator. The protocol is a shared contract between the engine, the emulator fork (mGBA, MPL 2.0) and the editor, so it is specified in this open repository.

## Transport

Process-to-process (socket or pipe) from the start, even while the emulator is linked into the editor. This keeps the door open for GPL emulators (e.g. for DS) that must run as separate processes.

Transport and message format are **not yet defined**; see [open-questions.md](open-questions.md).

## What the runtime must guarantee

The debugger features depend on these runtime properties:

| Feature | Runtime requirement |
| --- | --- |
| Hot reload of scripts | Scripts are bytecode in memory, so edited scripts can be patched into the running game without a rebuild ([vm.md](vm.md)) |
| Script-level debugging | Breakpoints and stepping on VM opcodes, mappable back to source events (not just ARM instructions) |
| Entity inspector | ECS component arrays sit at known addresses, so any entity's data can be displayed and edited live ([ecs.md](ecs.md)) |
| Viewers | VRAM, palettes, OAM, affine matrix usage and raster effects are inspectable |
| Profiler | Per-frame CPU breakdown, including measured audio mixer cost |

The engine should export the symbol table / address map the debugger needs as part of the build.

## Script breakpoints

`void debug_break(void)` (`debug.h`) stops the whole game where it is called (every script, every system, the frame) while a debugger is attached over the debug link, until the debugger resumes it; with no debugger attached, and in release builds, it does nothing. Scripts get a builtin of the same name, with no arguments and no result, compiling to the VM's `BRK` opcode; the event editor's "pause here" block is one.

It is **planned API** ([releases.md](releases.md#planned-api)), declared before 1.0.0 so that its name is reserved in scripts from 1.0 ([lua.md](lua.md#planned-functions)): in this version the C function does nothing and the game runs on (it warns once in debug builds), and a script that calls it, or declares its name at the top level, doesn't compile. It is implemented with the debug link's agent, in a 1.x minor version.
