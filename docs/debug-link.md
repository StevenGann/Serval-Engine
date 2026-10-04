# Debug link (runtime side)

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
