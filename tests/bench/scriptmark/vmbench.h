// The script benchmarks' measuring (vmbench.c), shared by scriptmark's
// bench.c and fireflies_bench.c: the cycle counter, the per-frame sums and
// the report, with a least-squares fit of the VM's cycles against its ops.

#ifndef SCRIPTMARK_VMBENCH_H
#define SCRIPTMARK_VMBENCH_H

#include "serval/serval.h"

// CPU cycles from the engine's free-running counter (timers 2 and 3, which
// serval_init() starts and games may read: docs/core-api.md#timers). Only the
// GBA counts: on the web every reading is the same, and the VM's cycles are 0.
u32 vmbench_cycles(void);

typedef struct {
    u32 frames;
    uint64_t ops, vm, whole;       // sums: ops, VM cycles and whole-frame cycles
    uint64_t ops_sq, vm_sq, cross; // sums of ops squared, VM cycles squared and their products
    u32 peak_ops, peak_vm;
    u32 run; // FNV-1a over every frame's ops, then the end state (vmbench_state)
} VmBench;

void vmbench_start(VmBench* b);

// One measured frame: the ops it ran (vm_ops_this_frame()), the cycles spent
// in vm_step() and vm_events(), and the whole frame's (frame_cpu_cycles()).
void vmbench_frame(VmBench* b, u32 ops, u32 vm, u32 whole);

// Folds the end state into the run's checksum: the first `globals` globals and
// every live entity's components, position and velocity. Two builds that run
// the scene the same way, frame by frame, print the same checksum.
void vmbench_state(VmBench* b, u32 globals);

// Logs "<name>: <frames> frames: VM avg ... cycles a frame (x.x%), peak ...;
// ops avg ..., peak ...; fit ... + ... a op; frame avg ...; run <checksum>".
void vmbench_report(const VmBench* b, const char* name);

#endif // SCRIPTMARK_VMBENCH_H
