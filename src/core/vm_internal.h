#ifndef SERVAL_CORE_VM_INTERNAL_H
#define SERVAL_CORE_VM_INTERNAL_H

// Engine-internal: the VM's engine calls that only the GBA build has (sound,
// music, text, buttons, brightness; the web build compiles the GBA files).
// vm.c makes the portable SYS calls itself (VM_SYS_CAMERA_SET,
// VM_SYS_RANDOM_RANGE, VM_SYS_PATH_START, VM_SYS_PATH_STOP) and hands every
// other one to serval_vm_platform_call, implemented in src/gba/vm_platform.c
// (GBA and web) and in src/host/platform.c (host: records the call for the
// tests).

#include "serval/vm.h"

// fn: a VM_SYS_* number other than the four above. args: the call's
// arguments in the order they were pushed (args[0] first), as many as the
// SYS table in docs/vm.md gives it. ptr: what vm.c resolved from an index
// argument (VM_SYS_TEXT_PRINT: the string; VM_SYS_PSG_MUSIC_PLAY: the PsgSong),
// otherwise NULL; vm.c has already warned about and skipped calls whose
// index was bad. Returns the call's result (button calls: 0 or 1), 0 for
// calls without one.
s32 serval_vm_platform_call(u32 fn, const s32* args, const void* ptr);

// For tools that inspect a running VM from C (the host's script runner,
// tests/svlua/runner.c; the engine never calls them): the object entity e is
// attached to, or -1 if it is not attached; its instance field n (0 if it is
// not attached or n >= VM_FIELDS); cell n of the RAM arrays' pool (0 if n >=
// VM_ARRAY_CELLS). Read only: no warnings, no stale-binding cleanup.
int serval_vm_attached_object(Entity e);
s32 serval_vm_field(Entity e, u32 n);
s32 serval_vm_array_cell(u32 n);

// Host build only (src/host/platform.c): what serval_vm_platform_call saw,
// for the tests. Not defined in GBA or web builds.
typedef struct {
    u32 calls;            // platform calls since the test last zeroed this
    u32 fn;               // the latest call's VM_SYS_* number
    s32 args[4];          // its arguments (unused ones 0)
    const void* ptr;      // its resolved string or song
    s32 button_value;     // what VM_SYS_BUTTON_DOWN and VM_SYS_BUTTON_PRESSED return
    void (*during)(void); // if set, called by each call: stands in for game code
                          // that runs during vm_step or vm_events
} ServalHostVmCalls;
extern ServalHostVmCalls serval_host_vm_calls;

#endif // SERVAL_CORE_VM_INTERNAL_H
