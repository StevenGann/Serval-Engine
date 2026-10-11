// The debug link's agent, its VM part (docs/debug-link.md; link_internal.h):
// globals, the RAM arrays' pool, instance fields, the entities' objects and
// contexts, the blob's size and FNV-1a hash (AGENT_INFO, LOADED), the VM's
// share of STATS, and the scratch. In debug builds with the agent only.
//
// A game links this file only if it links the VM: the agent's core (link.c)
// reaches it through serval_link_vm, which vm.c defines and link.c refers
// to weakly. It reads the VM's state through vm.c's accessors
// (vm_internal.h's and vm.c's debug link block), never warning and never
// changing what the game would see.
//
// The VM's part of breakpoints, stepping, contexts and frames (E6 of
// docs/debug-link.md#implementation) is still to come: it adds BRK's park
// and displaced execution, the copy into the scratch, and a hook in vm.c's
// loader for LOADED (polled at frame boundaries until then, which misses a
// reload of the same blob at the same address: the same bytes, so the same
// maps).

#include "link_internal.h"
#include "serval/vm.h"
#include "vm_internal.h"

#ifdef SERVAL_LINK_AGENT

#ifndef SERVAL_LINK_SCRATCH
#define SERVAL_LINK_SCRATCH 32768
#endif

// The scratch: the agent's copy of the blob, which breakpoints patch and
// reloads land in (the VM's part, E6 and E9), SERVAL_LINK_SCRATCH bytes of
// EWRAM (a CMake cache variable; 0: none). Reserved now, so that a debug
// build's memory is what it will be, and AGENT_INFO reports its size. Kept
// (used) though nothing reads it yet.
#if SERVAL_LINK_SCRATCH > 0
SERVAL_EWRAM_BSS __attribute__((used, aligned(4))) static u8 scratch[SERVAL_LINK_SCRATCH];
#endif

// The blob the agent last saw loaded, and its hash (0 until computed).
typedef struct {
    const u8* blob;
    u32 size;
    u32 hash;
} Seen;
SERVAL_EWRAM_BSS static Seen seen;

static const u8* loaded(u32* size) {
    const u8* b = serval_vm_link_blob(size);
    if (!b)
        *size = 0;
    return b;
}

static void info(u32* size, u32* hash, u32* scratch_size) {
    seen.blob = loaded(&seen.size);
    seen.hash = seen.blob ? serval_link_fnv1a(seen.blob, seen.size) : 0;
    *size = seen.size;
    *hash = seen.hash;
    *scratch_size = SERVAL_LINK_SCRATCH;
}

static void fill_loaded(LinkWriter* w, const void* arg) {
    (void)arg;
    serval_link_put32(w, seen.size);
    serval_link_put32(w, seen.hash);
}

static void poll(bool attached) {
    u32 size;
    const u8* b = loaded(&size);
    if (b == seen.blob && size == seen.size)
        return;
    seen.blob = b;
    seen.size = size;
    seen.hash = 0;
    if (!attached)
        return; // AGENT_INFO hashes it
    seen.hash = b ? serval_link_fnv1a(b, size) : 0;
    serval_link_event(LINK_LOADED, fill_loaded, NULL);
}

static s32 global(u32 n) {
    return vm_global((u16)n);
}

// GLOBALS and ARRAY_CELLS: first u16, count u16 (at most 253, so that the
// reply fits a frame) -> first, count, the cells. A count reaching past the
// pool's end is cut there (the reply's count says how many); a first past
// it is NO_SUCH.
static u32 cells(LinkReader* r, LinkWriter* w, u32 limit, s32 (*get)(u32), const char* none) {
    u32 first = serval_link_u16(r);
    u32 count = serval_link_u16(r);
    if (!r->ok) {
        serval_link_error_text = "the payload is too short";
        return LINK_BAD_REQUEST;
    }
    if (count > 253) {
        serval_link_error_text = "at most 253 cells a request";
        return LINK_BAD_REQUEST;
    }
    if (first >= limit) {
        serval_link_error_text = none;
        return LINK_NO_SUCH;
    }
    if (count > limit - first)
        count = limit - first;
    serval_link_put16(w, first);
    serval_link_put16(w, count);
    for (u32 i = 0; i < count; i++)
        serval_link_put32(w, (u32)get(first + i));
    return 0;
}

// SET_GLOBAL and SET_ARRAY_CELL: index u16, cell.
static u32 set_cell(LinkReader* r, u32 limit, void (*set)(u32, s32), const char* none) {
    u32 index = serval_link_u16(r);
    s32 value = (s32)serval_link_u32(r);
    if (!r->ok) {
        serval_link_error_text = "the payload is too short";
        return LINK_BAD_REQUEST;
    }
    if (index >= limit) {
        serval_link_error_text = none;
        return LINK_NO_SUCH;
    }
    set(index, value);
    return 0;
}

static void set_global(u32 n, s32 value) {
    vm_set_global((u16)n, value);
}

static u32 request(u32 type, LinkReader* r, LinkWriter* w) {
    u32 size;
    if (!loaded(&size)) {
        serval_link_error_text = "no blob is loaded";
        return LINK_NO_VM;
    }
    switch (type) {
    case LINK_GLOBALS:
        return cells(r, w, VM_GLOBALS, global, "no such global");
    case LINK_SET_GLOBAL:
        return set_cell(r, VM_GLOBALS, set_global, "no such global");
    case LINK_ARRAY_CELLS:
        return cells(r, w, VM_ARRAY_CELLS, serval_vm_array_cell, "no such array cell");
    default: // LINK_SET_ARRAY_CELL
        return set_cell(r, VM_ARRAY_CELLS, serval_vm_link_set_array_cell, "no such array cell");
    }
}

static void stats(u32* ops, u32* contexts, u32* queued) {
    *ops = vm_ops_this_frame();
    *contexts = serval_vm_link_contexts();
    *queued = serval_vm_link_queued();
}

const ServalLinkVmPart serval_link_vm_part = {
    .info = info,
    .poll = poll,
    .request = request,
    .object_of = serval_vm_attached_object,
    .field = serval_vm_field,
    .set_field = serval_vm_link_set_field,
    .context_of = serval_vm_link_context_of,
    .stats = stats,
};

#endif // SERVAL_LINK_AGENT
