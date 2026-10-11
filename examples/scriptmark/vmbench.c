// The script benchmarks' measuring: see vmbench.h.

#include "vmbench.h"

// Timers 2 and 3's counters. The engine cascades them into a 32-bit cycle
// counter; games may read them, never write them (docs/core-api.md#timers).
#define TIMER2_COUNT (*(volatile const u16*)(uintptr_t)0x04000108u)
#define TIMER3_COUNT (*(volatile const u16*)(uintptr_t)0x0400010Cu)

u32 vmbench_cycles(void) {
    // Read again if the low half wrapped between reading the two halves.
    u32 hi, lo;
    do {
        hi = TIMER3_COUNT;
        lo = TIMER2_COUNT;
    } while (hi != TIMER3_COUNT);
    return hi << 16 | lo;
}

static u32 mix(u32 hash, u32 value) {
    for (u32 k = 0; k < 4; k++)
        hash = (hash ^ (value >> (8 * k) & 0xFF)) * 16777619u; // FNV-1a
    return hash;
}

void vmbench_start(VmBench* b) {
    *b = (VmBench){.run = 2166136261u};
}

void vmbench_frame(VmBench* b, u32 ops, u32 vm, u32 whole) {
    b->frames++;
    b->ops += ops;
    b->vm += vm;
    b->whole += whole;
    b->ops_sq += (uint64_t)ops * ops;
    b->vm_sq += (uint64_t)vm * vm;
    b->cross += (uint64_t)ops * vm;
    b->peak_ops = ops > b->peak_ops ? ops : b->peak_ops;
    b->peak_vm = vm > b->peak_vm ? vm : b->peak_vm;
    b->run = mix(b->run, ops);
}

void vmbench_state(VmBench* b, u32 globals) {
    for (u32 g = 0; g < globals; g++)
        b->run = mix(b->run, (u32)vm_global((u16)g));
    ECS_FOR_EACH(i, 0) {
        b->run = mix(b->run, i);
        b->run = mix(b->run, ent_mask[i]);
        b->run = mix(b->run, (u32)pos_x[i]);
        b->run = mix(b->run, (u32)pos_y[i]);
        b->run = mix(b->run, (u32)vel_x[i]);
        b->run = mix(b->run, (u32)vel_y[i]);
    }
}

void vmbench_report(const VmBench* b, const char* name) {
    uint64_t n = b->frames;
    if (!n)
        return;
    u32 vm = (u32)(b->vm / n);
    u32 permille = vm * 1000 / frame_budget_cycles();
    debug_log(text_format("%s: %u frames: VM avg %u cycles a frame (%u.%u%%), peak %u; ops avg %u, "
                          "peak %u",
                          name, b->frames, vm, permille / 10, permille % 10, b->peak_vm,
                          (u32)(b->ops / n), b->peak_ops));
    // The least-squares line through (ops, VM cycles): fixed + per_op * ops.
    // In integers: per_op in thousandths, r² in thousandths (r² = per_op *
    // spread / vm_spread).
    uint64_t spread = n * b->ops_sq - b->ops * b->ops; // n² times the variance of ops
    uint64_t vm_spread = n * b->vm_sq - b->vm * b->vm;
    int64_t together = (int64_t)(n * b->cross) - (int64_t)(b->ops * b->vm);
    if (!spread || !vm_spread || together <= 0) {
        debug_log(text_format("%s: no fit (the ops or the cycles never change); frame avg %u; run "
                              "%x",
                              name, (u32)(b->whole / n), b->run));
        return;
    }
    uint64_t per_op = (uint64_t)together * 1000 / spread;
    s32 fixed = (s32)(((int64_t)b->vm * 1000 - (int64_t)(per_op * b->ops)) / (int64_t)(n * 1000));
    uint64_t a = (uint64_t)together, c = vm_spread;
    while (per_op && a > UINT64_MAX / per_op) { // keep a * per_op in 64 bits
        a >>= 1;
        c >>= 1;
    }
    u32 r2 = c ? (u32)(a * per_op / c) : 0;
    debug_log(text_format("%s: fit %d + %u.%u a op (r2 0.%03u); frame avg %u; run %x", name, fixed,
                          (u32)(per_op / 1000), (u32)(per_op % 1000 / 100), r2 > 999 ? 999 : r2,
                          (u32)(b->whole / n), b->run));
}
