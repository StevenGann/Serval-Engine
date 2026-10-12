// The debug link's agent, its VM part (docs/debug-link.md; link_internal.h):
// globals, the RAM arrays' pool, instance fields, the entities' objects and
// contexts, the blob's size and FNV-1a hash (AGENT_INFO, LOADED), the VM's
// share of STATS, and the scratch; and (step E6) BRK's park, breakpoints with
// displaced execution, stepping, CONTEXTS, FRAMES, SET_LOCAL and faults. In
// debug builds with the agent only.
//
// A game links this file only if it links the VM: the agent's core (link.c)
// reaches it through serval_link_vm, which vm.c defines and link.c refers
// to weakly. It reads the VM's state through vm.c's accessors
// (vm_internal.h's and vm.c's debug link block), never warning and never
// changing what the game would see; vm.c calls it from a few hooks (a BRK, a
// fault, a stepped context's end, the phases, a load).
//
// Breakpoints (docs/debug-link.md#setting-breakpoints-patching-with-displaced-
// execution): the first BREAK_SET copies the blob into the scratch and points
// the VM at the copy (byte for byte the blob: contexts hold blob offsets, so
// nothing else changes); each site's opcode is then replaced by BRK, and the
// byte it replaced kept. A BRK at a site gives the interpreter that byte
// back, which runs in BRK's place: no unpatching, no single step, and one op
// counted, as without the breakpoint. A site carries kinds (a breakpoint, a
// step site at a depth up to STEP's, a step site at any depth); sites are
// kept sorted by pc, found by a binary search at each hit.
//
// debug link (E9): hot reload is link_reload.c's, reached from here
// (request(), and the part's replied and resuming).

#include "link_internal.h"
#include "serval/debug.h"
#include "serval/text.h"
#include "serval/vm.h"
#include "vm_internal.h"

#ifdef SERVAL_LINK_AGENT

#ifndef SERVAL_LINK_SCRATCH
#define SERVAL_LINK_SCRATCH 32768
#endif

// The scratch: the agent's copy of the blob, which breakpoints patch and
// reloads land in (E9), SERVAL_LINK_SCRATCH bytes of EWRAM (a CMake cache
// variable; 0: none, and no breakpoints or stepping).
#if SERVAL_LINK_SCRATCH > 0
SERVAL_EWRAM_BSS __attribute__((aligned(4))) static u8 scratch[SERVAL_LINK_SCRATCH];
#define SCRATCH scratch
#else
#define SCRATCH ((u8*)NULL)
#endif

// A breakpoint site: its pc, the opcode BRK replaced there, and its kinds
// (bit LINK_SITE_*). 8 bytes.
typedef struct {
    u32 pc;
    u8 original;
    u8 kinds;
    u16 reserved;
} Site;
_Static_assert(sizeof(Site) == 8, "a site is 8 bytes (docs/debug-link.md#what-debug-builds-add)");

#define KIND(k) (1u << (k))
#define STEP_KINDS (KIND(LINK_SITE_STEP) | KIND(LINK_SITE_STEP_ANY))

// STEP: the stepped context, its depth, and whether the step began in a
// reaction running on top of a waiting behaviour (then only that reaction's
// hits count, and its end ends the step) or in the context's own activation.
typedef struct {
    bool active;
    bool stacked;
    u8 context;
    u8 depth;
} Step;

typedef struct {
    u32 sites;        // sites in use, sorted by pc
    Step step;        //
    bool timing;      // inside vm_step() or vm_events(), attached
    u32 phase_start;  // the cycle counter when the phase began (parks excluded)
    u32 phase_cycles; // this frame's, so far
    u32 frame_cycles; // the last frame's (STATS' vm_cycles)
} VmAgent;
SERVAL_EWRAM_BSS static Site sites[LINK_SITES];
SERVAL_EWRAM_BSS static VmAgent vma;
SERVAL_EWRAM_BSS u8 serval_vm_link_stepping;
SERVAL_EWRAM_BSS u8 serval_vm_link_attached; // from AGENT_INFO to the debugger leaving

static u32 le16(const u8* p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

static const u8* loaded(u32* size) {
    const u8* b = serval_vm_link_blob(size);
    if (!b)
        *size = 0;
    return b;
}

// Where a blob's code may start: past its header and tables (vm_load checked
// them), as a handler offset must be.
static u32 code_start(const u8* b) {
    u32 globals = le16(b + 6) & VM_FLAG_GLOBAL_VALUES ? le16(b + 12) * VM_CELL_BYTES : 0;
    return VM_HEADER_SIZE + le16(b + 8) * VM_OBJECT_SIZE + le16(b + 10) * 4u +
           le16(b + 14) * VM_ARRAY_RECORD_SIZE + globals;
}

// --- Sites ---------------------------------------------------------------------------

// The first site at or past pc.
static u32 lower_bound(u32 pc) {
    u32 lo = 0, hi = vma.sites;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (sites[mid].pc < pc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static Site* find_site(u32 pc) {
    u32 n = lower_bound(pc);
    return n < vma.sites && sites[n].pc == pc ? &sites[n] : NULL;
}

// The VM's blob, written: the scratch, once the copy is made.
static u8* patchable(void) {
    u32 size;
    const u8* b = loaded(&size);
    return b && b == SCRATCH ? SCRATCH : NULL;
}

// Removes site n, putting back the opcode BRK replaced.
static void remove_site(u32 n) {
    u8* code = patchable();
    if (code)
        code[sites[n].pc] = sites[n].original;
    for (u32 k = n + 1; k < vma.sites; k++)
        sites[k - 1] = sites[k];
    vma.sites--;
}

// Takes the kinds in `kinds` off every site; sites left with none go.
static void clear_kinds(u32 kinds) {
    for (u32 n = vma.sites; n-- > 0;) {
        sites[n].kinds = (u8)(sites[n].kinds & ~kinds);
        if (!sites[n].kinds)
            remove_site(n);
    }
}

// The opcode at pc as the script has it: a site's original, else the blob's.
static u32 opcode_at(u32 pc) {
    u32 size;
    const u8* b = loaded(&size);
    const Site* s = find_site(pc);
    if (s)
        return s->original;
    return b && pc < size ? b[pc] : VM_OP_NOP;
}

// FNV-1a of the blob as the game loaded it: the patched opcodes read as the
// ones they replaced, so AGENT_INFO's hash doesn't change with breakpoints.
static u32 blob_hash(const u8* b, u32 size) {
    u32 hash = 0x811C9DC5u, n = 0;
    for (u32 i = 0; i < size; i++) {
        u32 byte = b[i];
        if (n < vma.sites && sites[n].pc == i)
            byte = sites[n++].original;
        hash = (hash ^ byte) * 0x01000193u;
    }
    return hash;
}

// --- Stepping ----------------------------------------------------------------------

static void end_step(void) {
    vma.step.active = false;
    serval_vm_link_stepping = 0;
}

// Parks the game: STOPPED. The cycles spent parked aren't the VM's.
static void stop(u32 reason, u32 k, u32 pc, u32 fault) {
    u32 parked = vma.timing ? serval_link_platform_cycles() : 0;
    serval_link_stop(reason, k, pc, fault);
    if (vma.timing)
        vma.phase_start += serval_link_platform_cycles() - parked;
}

// The game stopped (any STOPPED, link.c): a step is over, and every step
// site goes (docs/debug-link.md: "every step site is cleared when it stops").
static void stopped(void) {
    end_step();
    clear_kinds(STEP_KINDS);
}

// A hit on a step site of these kinds by context k stops the step: the
// stepped context, in the stepped activation, at a depth up to STEP's for
// kind 1, at any depth for kind 2. Other contexts' hits run on. Out of line:
// a hit pays for the context's registers only when it is the stepped one's.
__attribute__((noinline)) static bool stepped_activation(u32 k, u32 kinds) {
    ServalVmLinkContext c;
    if (!serval_vm_link_context(k, &c) || c.stacked != vma.step.stacked)
        return false;
    return (kinds & KIND(LINK_SITE_STEP_ANY)) || c.cp <= vma.step.depth;
}

static bool step_stops(u32 k, u32 kinds) {
    if (!vma.step.active || k != vma.step.context || !(kinds & STEP_KINDS))
        return false;
    return stepped_activation(k, kinds);
}

bool serval_vm_link_ends(u32 k, bool reaction) {
    return vma.step.active && k == vma.step.context && (!reaction || vma.step.stacked);
}

void serval_vm_link_ended(void) {
    if (!vma.step.active)
        return;
    u32 k = vma.step.context;
    end_step();
    stop(LINK_STOP_ENDED, k, 0, 0);
}

// --- The interpreter's hooks ---------------------------------------------------------

u32 serval_vm_link_brk(u32 k, u32 at) {
    const Site* s = find_site(at);
    u32 original = s ? s->original : VM_OP_BRK;
    u32 kinds = s ? s->kinds : 0;
    if (!serval_vm_link_attached) { // as serval_link_attached(), without a call
        // No debugger, so no sites (detaching restored them): the script's
        // own BRK, logged as in v1.
        debug_log(text_format("vm: BRK at 0x%x", at));
        return original;
    }
    if (kinds & KIND(LINK_SITE_BREAKPOINT))
        stop(LINK_STOP_BREAKPOINT, k, at, 0);
    else if (step_stops(k, kinds))
        stop(LINK_STOP_STEP, k, at, 0);
    else if (original == VM_OP_BRK)
        stop(LINK_STOP_BRK, k, at, 0); // the script's own (Lua's debug_break())
    return original;                   // the site may have gone meanwhile: its opcode was kept
}

// FAULT's text for a fault the VM warned about before (it warns once per
// kind per loaded blob).
static const char* const fault_names[LINK_FAULT_KINDS] = {
    "",
    "stack overflow or underflow",
    "call depth past VM_CALLS",
    "pc escaped the blob",
    "unknown opcode",
    "unknown SYS call",
    "ENTER that doesn't fit",
    "past the budget",
    "a wait in a reaction",
    "a halt",
};

typedef struct {
    u32 kind, k, at;
    const char* text;
} Fault;

static void fill_fault(LinkWriter* w, const void* arg) {
    const Fault* f = arg;
    serval_link_put8(w, f->kind);
    serval_link_put8(w, f->k);
    serval_link_put32(w, f->at);
    serval_link_put_str(w, f->text);
}

void serval_vm_link_fault(u32 k, u32 kind, u32 at, const char* warning) {
    if (!serval_link_attached())
        return;
    if (kind == LINK_FAULT_STACK && opcode_at(at) == VM_OP_ENTER)
        kind = LINK_FAULT_ENTER; // ENTER's arguments or locals don't fit
    bool park = serval_link_options() & 1u;
    if (!warning && !park)
        return; // reported once, as the log is
    Fault f = {kind, k, at, warning};
    if (!warning)
        f.text = text_format("vm: %s at 0x%x (its warning was logged the first time)",
                             fault_names[kind < LINK_FAULT_KINDS ? kind : LINK_FAULT_OTHER], at);
    serval_link_event(LINK_FAULT, fill_fault, &f);
    if (park)
        stop(LINK_STOP_FAULT, k, at, kind);
}

void serval_vm_link_phase(bool begin) {
    if (begin) {
        vma.timing = true;
        vma.phase_start = serval_link_platform_cycles();
    } else if (vma.timing) {
        vma.phase_cycles += serval_link_platform_cycles() - vma.phase_start;
        vma.timing = false;
    }
}

typedef struct {
    u32 size, hash;
} Loaded;

static void fill_loaded(LinkWriter* w, const void* arg) {
    const Loaded* l = arg;
    serval_link_put32(w, l->size);
    serval_link_put32(w, l->hash);
}

// vm_load, vm_reload, vm_unload (install()): the VM runs another blob (or the
// same bytes again, from wherever the game keeps them), so the copy isn't
// its blob any more and every site is gone. A step's context has ended:
// every context has.
void serval_vm_link_loaded(void) {
    vma.sites = 0;
    bool stepping = vma.step.active;
    u32 k = vma.step.context;
    end_step();
    serval_link_reload_loaded(); // debug link (E9): the game's own blob, no reload
    // (serval_vm_link_attached: not while the debugger leaves, whose half-done
    // reload may unload the VM.)
    if (!serval_vm_link_attached)
        return; // AGENT_INFO gives the blob's size and hash
    u32 size;
    const u8* b = loaded(&size);
    Loaded l = {size, b ? serval_link_fnv1a(b, size) : 0};
    serval_link_event(LINK_LOADED, fill_loaded, &l);
    if (stepping)
        stop(LINK_STOP_ENDED, k, 0, 0);
}

// --- Requests ------------------------------------------------------------------------

static u32 fail(u32 code, const char* text) {
    serval_link_error_text = text;
    return code;
}

#define SHORT() fail(LINK_BAD_REQUEST, "the payload is too short")

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
    if (!r->ok)
        return SHORT();
    if (count > 253)
        return fail(LINK_BAD_REQUEST, "at most 253 cells a request");
    if (first >= limit)
        return fail(LINK_NO_SUCH, none);
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
    if (!r->ok)
        return SHORT();
    if (index >= limit)
        return fail(LINK_NO_SUCH, none);
    set(index, value);
    return 0;
}

static void set_global(u32 n, s32 value) {
    vm_set_global((u16)n, value);
}

// A request's context: a live one, or NO_SUCH.
static u32 context_arg(LinkReader* r, ServalVmLinkContext* c, u32* k) {
    *k = serval_link_u8(r);
    if (!r->ok)
        return SHORT();
    if (!serval_vm_link_context(*k, c))
        return fail(LINK_NO_SUCH, "no such context (not live)");
    return 0;
}

// CONTEXTS: every live context, 26 bytes each: context, state, event, self,
// other, object, pc, wait, depth, stacked, then the behaviour beneath (zeros
// if not stacked): event, state, pc, wait, depth.
static u32 contexts(LinkWriter* w) {
    serval_link_put8(w, 0); // count, below
    u32 count = 0;
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        ServalVmLinkContext c;
        if (!serval_vm_link_context(k, &c))
            continue;
        count++;
        serval_link_put8(w, k);
        serval_link_put8(w, c.state);
        serval_link_put8(w, c.event);
        serval_link_put16(w, c.self);
        serval_link_put16(w, c.other);
        serval_link_put16(w, (u32)c.object);
        serval_link_put32(w, c.pc);
        serval_link_put16(w, c.wait);
        serval_link_put8(w, c.cp);
        serval_link_put8(w, c.stacked);
        serval_link_put8(w, c.stacked ? c.below.event : 0);
        serval_link_put8(w, c.stacked ? c.below.state : 0);
        serval_link_put32(w, c.stacked ? c.below.pc : 0);
        serval_link_put16(w, c.stacked ? c.below.wait : 0);
        serval_link_put8(w, c.stacked ? c.below.cp : 0);
    }
    w->p[0] = (u8)count;
    return 0;
}

// An activation's frames, innermost first: where it is, then its return
// points (the pc a RET goes back to, and the caller's frame).
static void activation(LinkWriter* w, u32 base, u32 cp, u32 pc, u32 fp, const u32* call_pc,
                       const u8* call_fp) {
    serval_link_put8(w, base);
    serval_link_put8(w, cp + 1);
    serval_link_put32(w, pc);
    serval_link_put8(w, fp);
    for (u32 n = cp; n-- > 0;) {
        serval_link_put32(w, call_pc[n]);
        serval_link_put8(w, call_fp[n]);
    }
}

// FRAMES: context -> sp, the stack's cells, activations (the reaction's,
// then the behaviour's beneath, when stacked).
static u32 frames(LinkReader* r, LinkWriter* w) {
    ServalVmLinkContext c;
    u32 k;
    u32 code = context_arg(r, &c, &k);
    if (code)
        return code;
    serval_link_put8(w, c.sp);
    for (u32 i = 0; i < c.sp; i++)
        serval_link_put32(w, (u32)c.stack[i]);
    serval_link_put8(w, c.stacked ? 2 : 1);
    activation(w, c.base, c.cp, c.pc, c.fp, c.call_pc, c.call_fp);
    if (c.stacked)
        activation(w, c.below.base, c.below.cp, c.below.pc, c.below.fp, c.below.call_pc,
                   c.below.call_fp);
    return 0;
}

// SET_LOCAL: context, index (a stack cell below sp), cell.
static u32 set_local(LinkReader* r) {
    ServalVmLinkContext c;
    u32 k;
    u32 code = context_arg(r, &c, &k);
    u32 index = serval_link_u8(r);
    s32 value = (s32)serval_link_u32(r);
    if (code)
        return code;
    if (!r->ok)
        return SHORT();
    if (index >= c.sp)
        return fail(LINK_NO_SUCH, "no such stack cell (at or past the context's sp)");
    c.stack[index] = value;
    return 0;
}

// Makes the copy, the first time: the blob into the scratch, the VM pointed
// at it.
static u32 copy_blob(void) {
    u32 size;
    const u8* b = loaded(&size);
    if (b == SCRATCH && b)
        return 0;
    if (!SERVAL_LINK_SCRATCH)
        return fail(LINK_NO_SCRATCH, "no scratch: SERVAL_LINK_SCRATCH is 0");
    if (size > SERVAL_LINK_SCRATCH)
        return fail(LINK_NO_SCRATCH, "the blob is larger than the scratch (SERVAL_LINK_SCRATCH)");
    u8* copy = SCRATCH;
    for (u32 i = 0; i < size; i++)
        copy[i] = b[i];
    serval_vm_link_use_copy(copy);
    serval_link_reload_copied(b); // debug link (E9): a reload goes back to the game's own
    return 0;
}

// BREAK_SET: kind, count, count x pc. All or nothing: FULL when the new
// sites don't fit, and nothing is set.
static u32 break_set(LinkReader* r) {
    u32 kind = serval_link_u8(r);
    u32 count = serval_link_u16(r);
    if (!r->ok || r->n - r->at < count * 4)
        return SHORT();
    if (kind >= LINK_SITE_KINDS)
        return fail(LINK_BAD_REQUEST, "a site's kind is 0 (breakpoint), 1 or 2 (step sites)");
    u32 size;
    const u8* b = loaded(&size);
    u32 first = code_start(b), at = r->at, added = 0;
    for (u32 i = 0; i < count; i++) {
        u32 pc = serval_link_u32(r);
        if (pc < first || pc >= size)
            return fail(LINK_BAD_REQUEST, "a pc outside the blob's code");
        if (find_site(pc))
            continue;
        bool again = false; // the same pc earlier in the request
        for (u32 j = 0; j < i && !again; j++)
            again = (r->p[at + j * 4] | r->p[at + j * 4 + 1] << 8 | r->p[at + j * 4 + 2] << 16 |
                     (u32)r->p[at + j * 4 + 3] << 24) == pc;
        added += !again;
    }
    if (vma.sites + added > LINK_SITES)
        return fail(LINK_FULL, "every breakpoint site is taken");
    u32 code = copy_blob();
    if (code)
        return code;
    u8* copy = SCRATCH;
    r->at = at;
    for (u32 i = 0; i < count; i++) {
        u32 pc = serval_link_u32(r);
        u32 n = lower_bound(pc);
        if (n < vma.sites && sites[n].pc == pc) {
            sites[n].kinds = (u8)(sites[n].kinds | KIND(kind));
            continue;
        }
        for (u32 j = vma.sites; j > n; j--)
            sites[j] = sites[j - 1];
        sites[n] = (Site){.pc = pc, .original = copy[pc], .kinds = (u8)KIND(kind)};
        vma.sites++;
        copy[pc] = VM_OP_BRK;
    }
    return 0;
}

// BREAK_CLEAR: kind, count (0: every site of that kind), count x pc. A pc
// that isn't a site of that kind is no error.
static u32 break_clear(LinkReader* r) {
    u32 kind = serval_link_u8(r);
    u32 count = serval_link_u16(r);
    if (!r->ok || r->n - r->at < count * 4)
        return SHORT();
    if (kind >= LINK_SITE_KINDS)
        return fail(LINK_BAD_REQUEST, "a site's kind is 0 (breakpoint), 1 or 2 (step sites)");
    if (!count) {
        clear_kinds(KIND(kind));
        return 0;
    }
    for (u32 i = 0; i < count; i++) {
        u32 pc = serval_link_u32(r);
        u32 n = lower_bound(pc);
        if (n >= vma.sites || sites[n].pc != pc)
            continue;
        sites[n].kinds = (u8)(sites[n].kinds & ~KIND(kind));
        if (!sites[n].kinds)
            remove_site(n);
    }
    return 0;
}

// STEP: context, depth (link.c checked that the game is parked, and resumes
// it). The step stops where the context first reaches a step site of kind 1
// at a call depth up to `depth`, or of kind 2 at any depth; or where it ends.
static u32 step(LinkReader* r) {
    ServalVmLinkContext c;
    u32 k;
    u32 code = context_arg(r, &c, &k);
    u32 depth = serval_link_u8(r);
    if (code)
        return code;
    if (!r->ok)
        return SHORT();
    if (!SERVAL_LINK_SCRATCH)
        return fail(LINK_NO_SCRATCH, "no scratch: SERVAL_LINK_SCRATCH is 0");
    vma.step = (Step){.active = true, .stacked = c.stacked, .context = (u8)k, .depth = (u8)depth};
    serval_vm_link_stepping = (u8)(k + 1);
    return 0;
}

static u32 request(u32 type, LinkReader* r, LinkWriter* w) {
    u32 size;
    if (!loaded(&size))
        return fail(LINK_NO_VM, "no blob is loaded");
    switch (type) {
    case LINK_GLOBALS:
        return cells(r, w, VM_GLOBALS, global, "no such global");
    case LINK_SET_GLOBAL:
        return set_cell(r, VM_GLOBALS, set_global, "no such global");
    case LINK_ARRAY_CELLS:
        return cells(r, w, VM_ARRAY_CELLS, serval_vm_array_cell, "no such array cell");
    case LINK_SET_ARRAY_CELL:
        return set_cell(r, VM_ARRAY_CELLS, serval_vm_link_set_array_cell, "no such array cell");
    case LINK_CONTEXTS:
        return contexts(w);
    case LINK_FRAMES:
        return frames(r, w);
    case LINK_SET_LOCAL:
        return set_local(r);
    case LINK_BREAK_SET:
    case LINK_BREAK_CLEAR:
        if (serval_link_reload_busy()) // debug link (E9): the scratch is the reload's
            return fail(LINK_RELOADING, "a reload is under way: set breakpoints after RELOADED");
        return type == LINK_BREAK_SET ? break_set(r) : break_clear(r);
    case LINK_RELOAD_BEGIN: // debug link (E9): hot reload (link_reload.c)
    case LINK_RELOAD_DATA:
    case LINK_RELOAD_PLAN:
    case LINK_RELOAD_COMMIT:
        return serval_link_reload_request(type, r, w);
    default: // LINK_STEP
        return step(r);
    }
}

// --- The core's calls ----------------------------------------------------------------

// AGENT_INFO: the debugger attaches.
static void info(ServalLinkVmInfo* out) {
    serval_vm_link_attached = 1;
    u32 size;
    const u8* b = loaded(&size);
    out->size = size;
    out->hash = b ? blob_hash(b, size) : 0;
    out->scratch = SERVAL_LINK_SCRATCH;
    out->sites = SERVAL_LINK_SCRATCH ? LINK_SITES : 0;
    out->capabilities = LINK_CAP_VM | LINK_CAP_STOP_ON_FAULT |
                        (SERVAL_LINK_SCRATCH ? LINK_CAP_BREAKPOINTS | LINK_CAP_STEP : 0) |
                        (SERVAL_LINK_SCRATCH ? LINK_CAP_RELOAD : 0); // debug link (E9)
}

static void poll(bool attached) {
    (void)attached;
    vma.frame_cycles = vma.phase_cycles;
    vma.phase_cycles = 0;
}

static void stats(ServalLinkVmStats* s) {
    s->ops = vm_ops_this_frame();
    s->contexts = serval_vm_link_contexts();
    s->queued = serval_vm_link_queued();
    s->queue_peak = serval_vm_link_queue_peak();
    s->vm_cycles = vma.frame_cycles;
}

// The debugger left: every patched opcode back, so the copy the VM runs
// from is byte for byte the blob again; no step.
static void detached(void) {
    serval_vm_link_attached = 0;
    end_step();
    while (vma.sites)
        remove_site(vma.sites - 1);
    serval_link_reload_detached(); // debug link (E9): a reload half done
}

// debug link (E9): for the hot reload (link_reload.c): the scratch (NULL:
// none), and every site removed, its opcode back, and no step.
u8* serval_vm_link_scratch(void) {
    return SCRATCH;
}

void serval_vm_link_unpatch(void) {
    end_step();
    while (vma.sites)
        remove_site(vma.sites - 1);
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
    .stopped = stopped,
    .detached = detached,
    .replied = serval_link_reload_replied, // debug link (E9): hot reload (link_reload.c)
    .resuming = serval_link_reload_resuming,
};

#endif // SERVAL_LINK_AGENT
