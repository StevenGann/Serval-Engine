// The debug link's agent, its VM part (step E6 of docs/debug-link.md:
// src/core/vm_link.c and vm.c's debug link hooks) against the fake endpoint
// (link_fake.h): BRK's park, breakpoints with displaced execution, stepping
// (over, into, out, a context ending, other contexts running on), CONTEXTS,
// FRAMES, SET_LOCAL, faults and stop on fault, debug_break() in C and in a
// script, LOADED from the loader, STATS' vm_cycles and queue peak, and
// detaching. The script is link_debug.lua; its sites' pcs come from its maps
// (link_debug_sites.h, written by link_sites.py from the .svmap and .svdbg
// serval_add_script() made), as a debugger's planner picks them. The
// debugger's moves while the game is parked are each test's: the fake
// endpoint's on_wait. Host only.

#include "link_debug_script.h"
#include "link_debug_sites.h"
#include "link_fake.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/vm.h"
#include "test.h"

#ifdef SERVAL_LINK_AGENT

// --- The debugger's requests ----------------------------------------------------------

static u16 sites_request(u32 type, u32 kind, const u32* pcs, u32 count) {
    static u8 p[3 + 4 * 253];
    p[0] = (u8)kind;
    wr16(p + 1, count);
    for (u32 i = 0; i < count; i++)
        wr32(p + 3 + 4 * i, pcs[i]);
    return request(type, p, 3 + 4 * count);
}

static u16 break_set(u32 kind, const u32* pcs, u32 count) {
    return sites_request(LINK_BREAK_SET, kind, pcs, count);
}

static u16 break_clear(u32 kind, const u32* pcs, u32 count) {
    return sites_request(LINK_BREAK_CLEAR, kind, pcs, count);
}

static u16 step_request(u32 k, u32 depth) {
    return request(LINK_STEP, (const u8[]){(u8)k, (u8)depth}, 2);
}

static u16 resume(u32 mode) {
    return request(LINK_RESUME, (const u8[]){(u8)mode}, 1);
}

static u16 set_local(u32 k, u32 index, s32 value) {
    u8 p[6] = {(u8)k, (u8)index};
    wr32(p + 2, (u32)value);
    return request(LINK_SET_LOCAL, p, 6);
}

typedef struct {
    u32 reason, context, pc, frame, fault;
} Stopped;

static bool stopped(u32 nth, Stopped* s) {
    const u8* f = event(LINK_STOPPED, nth);
    if (!f || rd16(f) != 4 + 11)
        return false;
    *s = (Stopped){f[6], f[7], rd32(f + 8), rd32(f + 12), f[16]};
    return true;
}

// FRAMES' reply.
typedef struct {
    u32 sp;
    s32 cells[VM_STACK];
    u32 activations;
    struct {
        u32 base, count;
        u32 pc[VM_CALLS + 1], fp[VM_CALLS + 1];
    } a[2];
} Frames;

static bool frames_of(const u8* f, Frames* out) {
    if (!f || f[3] != LINK_FLAG_REPLY)
        return false;
    const u8* p = f + 6;
    const u8* end = f + 2 + rd16(f);
    out->sp = *p++;
    for (u32 i = 0; i < out->sp; i++, p += 4)
        out->cells[i] = (s32)rd32(p);
    out->activations = *p++;
    for (u32 a = 0; a < out->activations && a < 2; a++) {
        out->a[a].base = *p++;
        out->a[a].count = *p++;
        for (u32 n = 0; n < out->a[a].count && n <= VM_CALLS; n++, p += 5) {
            out->a[a].pc[n] = rd32(p);
            out->a[a].fp[n] = p[4];
        }
    }
    return p == end;
}

// CONTEXTS' entry for context k, 26 bytes, or NULL.
static const u8* context_entry(const u8* f, u32 k) {
    if (!f || f[3] != LINK_FLAG_REPLY)
        return NULL;
    for (u32 n = 0; n < f[6]; n++) {
        const u8* e = f + 7 + n * 26;
        if (e[0] == k)
            return e;
    }
    return NULL;
}

// --- The planner: steps from the maps, as docs/debug-link.md#stepping has them -----------

static const SiteFunction* function_of(u32 pc) {
    for (u32 n = 0; n < FUNCTION_COUNT; n++) {
        if (FUNCTIONS[n].first <= pc && pc <= FUNCTIONS[n].last)
            return &FUNCTIONS[n];
    }
    return NULL;
}

// The statement a pc is in: the last statement start at or before it.
static u32 statement_of(u32 pc) {
    u32 at = 0;
    for (u32 n = 0; n < STATEMENT_COUNT && STATEMENTS[n] <= pc; n++)
        at = STATEMENTS[n];
    return at;
}

// Over: kind 1 at every statement start of the current function and at the
// frame's return point; STEP at the current depth. Into: those, and kind 2
// at the first statement of each function the current statement calls. Out:
// kind 1 at the return point only; STEP a depth less (none at the top).
enum { OVER, INTO, OUT };

static void plan_step(u32 how, u32 k, const Frames* f) {
    u32 depth = f->a[0].count - 1;
    u32 pc = f->a[0].pc[0];
    if (how != OUT) {
        const SiteFunction* fn = function_of(pc);
        if (fn)
            break_set(LINK_SITE_STEP, fn->statements, fn->count);
    }
    if (depth)
        break_set(LINK_SITE_STEP, &f->a[0].pc[1], 1);
    if (how == INTO) {
        u32 callees[CALL_COUNT], n = 0;
        for (u32 c = 0; c < CALL_COUNT; c++) {
            if (CALLS[c][0] == statement_of(pc))
                callees[n++] = CALLS[c][1];
        }
        if (n)
            break_set(LINK_SITE_STEP_ANY, callees, n);
    }
    step_request(k, how == OUT && depth ? depth - 1 : depth);
}

// --- The debugger, while the game is parked ----------------------------------------------
//
// Each stop the test expects: its reason and pc (0: any), the depth its
// context's frames show (-1: any), and the move made there (NULL: RESUME).
// At each stop the debugger asks FRAMES (when the stop has a context), checks
// the stop, then makes the move.

typedef struct {
    u32 reason, pc;
    int depth;
    void (*move)(const Stopped* s, const Frames* f);
} Expect;

static struct {
    const Expect* script;
    u32 count, at; // the stops expected, and how many came
    u32 handled;   // STOPPED events handled
    u16 frames;    // FRAMES' seq, while its reply is awaited
    Stopped stop;  // the stop being handled
    Frames f;      // its context's frames
    u32 wrong;     // stops not as expected
    u32 hits;      // stops of any kind (for scripts of none)
    bool any;      // every stop resumed, none checked
} dbg;

static void debugger(u32 n) {
    (void)n;
    if (dbg.frames) {
        const u8* f = reply(dbg.frames);
        if (!f)
            return;
        dbg.frames = 0;
        if (!frames_of(f, &dbg.f))
            dbg.wrong++;
        const Expect* e = dbg.at < dbg.count ? &dbg.script[dbg.at] : NULL;
        dbg.at++;
        if (!e || (e->reason != dbg.stop.reason) || (e->pc && e->pc != dbg.stop.pc) ||
            (e->depth >= 0 && (u32)e->depth + 1 != dbg.f.a[0].count)) {
            dbg.wrong++;
            resume(0);
            return;
        }
        if (e->move)
            e->move(&dbg.stop, &dbg.f);
        else
            resume(0);
        return;
    }
    Stopped s;
    if (!stopped(dbg.handled, &s))
        return;
    dbg.handled++;
    dbg.hits++;
    if (dbg.any) {
        resume(0);
        return;
    }
    dbg.stop = s;
    if (s.context == 0xFF || s.reason == LINK_STOP_ENDED) { // nothing to look at
        const Expect* e = dbg.at < dbg.count ? &dbg.script[dbg.at] : NULL;
        dbg.at++;
        if (!e || e->reason != s.reason) {
            dbg.wrong++;
            resume(0);
        } else if (e->move) {
            e->move(&s, NULL);
        } else {
            resume(0);
        }
        return;
    }
    u8 k = (u8)s.context;
    dbg.frames = request(LINK_FRAMES, &k, 1);
}

// The debugger handles the stops from now on.
static void debug_with(const Expect* script, u32 count) {
    dbg = (typeof(dbg)){.script = script, .count = count, .handled = events(LINK_STOPPED)};
    fake.on_wait = debugger;
    fake.max_waits = 100000;
}

static void over(const Stopped* s, const Frames* f) {
    plan_step(OVER, s->context, f);
}

static void into(const Stopped* s, const Frames* f) {
    plan_step(INTO, s->context, f);
}

static void out(const Stopped* s, const Frames* f) {
    plan_step(OUT, s->context, f);
}

// --- The game --------------------------------------------------------------------------

// Frame boundaries until the debugger's queued requests are all read (the
// inbox holds 1,024 bytes; the agent reads it once a poll).
static void polls(void) {
    for (u32 n = 0; n < 16 && (fake.queued || mailbox()->in_len); n++)
        frame();
}

// A frame of the game: the VM's two phases, then frame_end()'s poll.
static void game_frame(void) {
    vm_step();
    vm_events();
    frame();
}

// The agent started with a debugger attached, link_debug.lua loaded.
static void attached(void) {
    start(true);
    attach();
    CHECK(vm_load(link_debug_script, link_debug_script_size));
    serval_host_link.cycles_step = 0;
}

static int start_thread(u16 object) {
    int k = vm_start(object, VM_EV_CREATE);
    CHECK(k >= 0);
    return k;
}

// The VM's blob now, and how many of its bytes differ from the script's
// (the patched sites).
static u32 patched_bytes(void) {
    u32 size;
    const u8* b = serval_vm_link_blob(&size);
    u32 n = 0;
    for (u32 i = 0; b && i < size && i < link_debug_script_size; i++)
        n += b[i] != link_debug_script[i];
    return n;
}

static bool running_from_copy(void) {
    u32 size;
    return serval_vm_link_blob(&size) != link_debug_script;
}

// --- Capabilities ------------------------------------------------------------------------

static void capabilities_and_sites(void) {
    attached();
    u16 s = attach();
    const u8* f = reply(s);
    CHECK(f != NULL);
    if (!f)
        return;
    const u8* p = f + 6 + 3 + f[8];
    CHECK(rd32(p) == (LINK_CAP_VM | LINK_CAP_BREAKPOINTS | LINK_CAP_STEP | LINK_CAP_STATS |
                      LINK_CAP_ENTITIES | LINK_CAP_STOP_ON_FAULT));
    CHECK(rd16(p + 15) == LINK_SITES && rd32(p + 17) == SERVAL_LINK_SCRATCH);
    // Breakpoints change neither AGENT_INFO's size nor its hash: the blob as
    // the game loaded it.
    u32 hash = serval_link_fnv1a(link_debug_script, link_debug_script_size);
    u16 set = break_set(LINK_SITE_BREAKPOINT, STATEMENTS, STATEMENT_COUNT);
    u16 again = request(LINK_AGENT_INFO, NULL, 0);
    frame();
    CHECK(ok(set, 0) && running_from_copy() && patched_bytes() > 0);
    f = reply(again);
    CHECK(f && rd32(f + 2 + rd16(f) - 8) == link_debug_script_size &&
          rd32(f + 2 + rd16(f) - 4) == hash);
}

// --- Breakpoints ---------------------------------------------------------------------------

// A breakpoint in a loop stops it every pass (no unpatching, no re-arming):
// ten stops, each where the site is, in the context running it, parked
// inside the frame; then the script's own BRK (debug_break()). The loop's
// result is the one it has without the breakpoint.
static void breakpoint_in_a_loop_hits_every_time(void) {
    attached();
    int k = start_thread(DEBUG_OBJ_LOOPER);
    u32 site = SITE_LOOP_BODY;
    u16 set = break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    CHECK(ok(set, 0));
    static Expect script[11];
    for (u32 n = 0; n < 10; n++)
        script[n] = (Expect){LINK_STOP_BREAKPOINT, SITE_LOOP_BODY, 0, NULL};
    script[10] = (Expect){LINK_STOP_BRK, SITE_LOOPER_BREAK, 0, NULL};
    debug_with(script, 11);
    game_frame();
    CHECK(dbg.at == 11 && dbg.wrong == 0);
    Stopped s;
    CHECK(stopped(0, &s) && s.context == (u32)k && s.fault == 0);
    CHECK(s.frame == serval_host_link.stats.frame - 1); // the frame before frame_end() counts it
    CHECK(fake.inside_rings >= 11 && fake.parked_rings == 0);
    CHECK(vm_global(DEBUG_G_TOTAL) == 56 && vm_global(DEBUG_G_RESULT) == 225);
    CHECK(vm_idle() && !serval_link_parked());
}

// Displaced execution: a run with a breakpoint at every statement start of
// the script, each hit stopping and resuming, and a run with step sites
// there (which stop nothing, no step running), end in exactly the state of a
// run without them, frame for frame: the ops each frame ran (one op counted
// per hit, as without the breakpoint), the globals and the entities.
typedef struct {
    s32 globals[DEBUG_G_COUNT];
    s32 x[2];
    u32 ops;
} RunState;

static void displaced_run(int sites, RunState* out) {
    attached();
    Entity w[2] = {entity_create(C_POS), entity_create(C_POS)};
    vm_attach(w[0], DEBUG_OBJ_WALKER);
    vm_attach(w[1], DEBUG_OBJ_WALKER);
    start_thread(DEBUG_OBJ_LOOPER);
    start_thread(DEBUG_OBJ_BURST);
    if (sites >= 0)
        break_set((u32)sites, STATEMENTS, STATEMENT_COUNT);
    frame();
    debug_with(NULL, 0);
    dbg.any = true;
    out->ops = 0;
    for (u32 f = 0; f < 60; f++) {
        vm_step();
        vm_events();
        out->ops = out->ops * 31u + vm_ops_this_frame();
        frame();
    }
    for (u32 g = 0; g < DEBUG_G_COUNT; g++)
        out->globals[g] = vm_global((u16)g);
    out->x[0] = pos_x[entity_index(w[0])];
    out->x[1] = pos_x[entity_index(w[1])];
}

static void displaced_execution_changes_nothing(void) {
    RunState plain, breakpoints, step_sites;
    displaced_run(-1, &plain);
    u32 plain_hits = dbg.hits; // the script's own BRK, once
    displaced_run(LINK_SITE_BREAKPOINT, &breakpoints);
    u32 hits = dbg.hits;
    displaced_run(LINK_SITE_STEP, &step_sites);
    u32 step_hits = dbg.hits;
    CHECK(plain_hits == 1 && step_hits == 1); // step sites stop nothing without a step
    CHECK(hits > 600);                        // every statement, every pass
    u32 differ = 0;
    for (u32 g = 0; g < DEBUG_G_COUNT; g++)
        differ +=
            plain.globals[g] != breakpoints.globals[g] || plain.globals[g] != step_sites.globals[g];
    CHECK(differ == 0);
    CHECK(plain.ops == breakpoints.ops && plain.ops == step_sites.ops);
    CHECK(plain.x[0] == breakpoints.x[0] && plain.x[1] == breakpoints.x[1]);
    CHECK(plain.globals[DEBUG_G_MOVED] == 120 && plain.globals[DEBUG_G_TOTAL] == 56);
    CHECK(plain.globals[DEBUG_G_WAITED] == 58 && plain.globals[DEBUG_G_STEPS] == 120);
}

// A breakpoint set while parked at the script's own BRK (the first: the
// copy is made then, mid-run) stops the same run further on: execute()
// reads the blob's address again after a park.
static void set_while_parked(const Stopped* s, const Frames* f) {
    (void)s, (void)f;
    u32 site = SITE_LOOPER_LAST;
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    resume(0);
}

static void breakpoint_set_while_parked_hits_in_the_same_run(void) {
    attached();
    start_thread(DEBUG_OBJ_LOOPER);
    static const Expect script[] = {
        {LINK_STOP_BRK, SITE_LOOPER_BREAK, 0, set_while_parked},
        {LINK_STOP_BREAKPOINT, SITE_LOOPER_LAST, 0, NULL},
    };
    debug_with(script, 2);
    CHECK(!running_from_copy());
    game_frame();
    CHECK(dbg.at == 2 && dbg.wrong == 0 && running_from_copy());
    CHECK(vm_global(DEBUG_G_TOTAL) == 56);
}

// --- BREAK_SET and BREAK_CLEAR -----------------------------------------------------------------

// A blob of `size` bytes: one object whose Create is NOPs, then HALT.
static u8 big_blob[SERVAL_LINK_SCRATCH + 64];

static u32 nop_blob(u32 size) {
    static const u8 header[16] = {'S', 'V', 'M', 'B', 1, 4, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    for (u32 i = 0; i < size; i++)
        big_blob[i] = i < 16 ? header[i] : 0;
    wr32(big_blob + 16 + 8, 48); // Create at 48, past the object table
    big_blob[size - 1] = VM_OP_HALT;
    return size;
}

static void break_set_and_clear(void) {
    attached();
    // Errors: a kind past 2, a pc in the tables or past the blob, a short
    // payload: nothing is set.
    u32 tables = FUNCTIONS[0].first; // add's ENTER: the blob's first op
    u32 bad_pcs[3] = {16, link_debug_script_size, SITE_ADD_SUM};
    u16 kind = break_set(3, &bad_pcs[2], 1);
    u16 in_tables = break_set(0, &bad_pcs[0], 1);
    u16 past = break_set(0, &bad_pcs[1], 1);
    u16 shrt = request(LINK_BREAK_SET, (const u8[]){0, 2, 0, 1, 2, 3, 4}, 7);
    frame();
    CHECK(error(kind, LINK_BAD_REQUEST) && error(in_tables, LINK_BAD_REQUEST));
    CHECK(error(past, LINK_BAD_REQUEST) && error(shrt, LINK_BAD_REQUEST));
    CHECK(!running_from_copy() && patched_bytes() == 0);
    // The first site copies the blob; sites of several kinds share a pc.
    u32 pcs[2] = {SITE_ADD_SUM, tables};
    u16 a = break_set(LINK_SITE_BREAKPOINT, pcs, 2);
    u16 b = break_set(LINK_SITE_STEP, pcs, 1);
    frame();
    CHECK(ok(a, 0) && ok(b, 0) && running_from_copy() && patched_bytes() == 2);
    u32 size;
    const u8* copy = serval_vm_link_blob(&size);
    CHECK(size == link_debug_script_size && copy[SITE_ADD_SUM] == VM_OP_BRK);
    // Clearing one kind keeps a site its other kinds hold; a pc that isn't a
    // site of the kind is no error; count 0 clears every site of a kind.
    u16 c = break_clear(LINK_SITE_BREAKPOINT, pcs, 2);
    u16 none = break_clear(LINK_SITE_STEP_ANY, pcs, 2);
    frame();
    CHECK(ok(c, 0) && ok(none, 0) && patched_bytes() == 1 && copy[SITE_ADD_SUM] == VM_OP_BRK);
    u16 all = break_clear(LINK_SITE_STEP, NULL, 0);
    frame();
    CHECK(ok(all, 0) && patched_bytes() == 0 && running_from_copy());
    // FULL: past LINK_SITES sites, and then nothing of the request is set.
    vm_unload();
    CHECK(vm_load(big_blob, nop_blob(600)));
    static u32 many[LINK_SITES + 1];
    for (u32 i = 0; i <= LINK_SITES; i++)
        many[i] = 48 + i;
    u16 most = break_set(LINK_SITE_BREAKPOINT, many, 200);
    u16 rest = break_set(LINK_SITE_STEP, many + 100, 156);   // 56 new: 256 in all
    u16 full = break_set(LINK_SITE_STEP_ANY, many + 250, 7); // 1 new, one too many
    polls();
    CHECK(ok(most, 0) && ok(rest, 0) && error(full, LINK_FULL));
    const u8* b2 = serval_vm_link_blob(&size);
    u32 brks = 0;
    for (u32 i = 0; i < size; i++)
        brks += b2[i] == VM_OP_BRK;
    CHECK(brks == LINK_SITES && b2[48 + LINK_SITES] == VM_OP_NOP);
    // A blob larger than the scratch: NO_SCRATCH.
    vm_unload();
    CHECK(vm_load(big_blob, nop_blob(SERVAL_LINK_SCRATCH + 64)));
    u16 large = break_set(LINK_SITE_BREAKPOINT, many, 1);
    polls();
    CHECK(error(large, LINK_NO_SCRATCH));
    // Requests on the VM with no blob loaded: NO_VM.
    vm_unload();
    u16 no_vm = break_set(LINK_SITE_BREAKPOINT, many, 1);
    u16 no_ctx = request(LINK_CONTEXTS, NULL, 0);
    polls();
    CHECK(error(no_vm, LINK_NO_VM) && error(no_ctx, LINK_NO_VM));
}

// --- Stepping --------------------------------------------------------------------------------

// Over, into and out through Looper:create, twice() and add(), each step's
// sites picked from the maps as a debugger does; the script's own BRK and a
// breakpoint stop a step on the way; the context ending stops the last.
static void stepping_over_into_and_out(void) {
    attached();
    int k = start_thread(DEBUG_OBJ_LOOPER);
    u32 site = SITE_LOOPER_TWICE;
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    // The return points, as FRAMES gives them: after each CALL.
    static const Expect script[] = {
        {LINK_STOP_BREAKPOINT, SITE_LOOPER_TWICE, 0, into}, // twice(total)
        {LINK_STOP_STEP, SITE_TWICE_ONCE, 1, over},         // into twice()
        {LINK_STOP_STEP, SITE_TWICE_RETURN, 1, into},       // over add(n, n)
        {LINK_STOP_STEP, SITE_ADD_SUM, 2, out},             // into add(once, once)
        {LINK_STOP_STEP, 0, 1, out},                        // out: back in twice()
        {LINK_STOP_STEP, 0, 0, over},                       // out: back in Looper:create
        {LINK_STOP_STEP, SITE_LOOPER_EXTRA, 0, out},        // over: the next line
        {LINK_STOP_BRK, SITE_LOOPER_BREAK, 0, over},        // out at the top: the BRK stops it
        {LINK_STOP_STEP, SITE_LOOPER_LAST, 0, over},        // a script's BRK goes on after itself
        {LINK_STOP_STEP, 0, 0, over},                       // the implicit return at its end
        {LINK_STOP_ENDED, 0, -1, NULL},                     // the handler ended
    };
    debug_with(script, sizeof script / sizeof script[0]);
    game_frame();
    CHECK(dbg.at == sizeof script / sizeof script[0] && dbg.wrong == 0);
    // Where the out steps stopped: the return points, inside the statements
    // that made the calls.
    Stopped s;
    CHECK(stopped(4, &s) && statement_of(s.pc) == SITE_TWICE_RETURN && s.pc > SITE_TWICE_RETURN);
    CHECK(stopped(5, &s) && statement_of(s.pc) == SITE_LOOPER_TWICE && s.pc > SITE_LOOPER_TWICE);
    CHECK(stopped(10, &s) && s.context == (u32)k && s.pc == 0);
    CHECK(vm_global(DEBUG_G_TOTAL) == 56 && vm_global(DEBUG_G_RESULT) == 225 && vm_idle());
    // Each stop cleared the step sites: the breakpoint alone is left.
    CHECK(patched_bytes() == 1);
}

// Over a recursive call: the function's own step sites are hit deeper down
// (count(2), count(1), count(0)), deeper than STEP's depth, and run on; the
// step stops at the next statement of the frame it began in, its local
// holding what the recursion returned. Into stops a level down; out, back up.
static Frames at_count_return;
static void over_and_look(const Stopped* s, const Frames* f) {
    at_count_return = *f;
    plan_step(OVER, s->context, f);
}

static void clear_then_over(const Stopped* s, const Frames* f) {
    break_clear(LINK_SITE_BREAKPOINT, NULL, 0);
    plan_step(OVER, s->context, f);
}

static void clear_then_into(const Stopped* s, const Frames* f) {
    break_clear(LINK_SITE_BREAKPOINT, NULL, 0);
    plan_step(INTO, s->context, f);
}

static void step_over_a_recursive_call(void) {
    attached();
    start_thread(DEBUG_OBJ_COUNTER);
    u32 site = SITE_COUNT_CALL;
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    static const Expect script[] = {
        {LINK_STOP_BREAKPOINT, SITE_COUNT_CALL, 1, clear_then_over}, // count(3)
        {LINK_STOP_STEP, SITE_COUNT_RETURN, 1, over_and_look},       // not count(2)'s
        {LINK_STOP_STEP, 0, 0, NULL},                                // count(3) returned
    };
    debug_with(script, sizeof script / sizeof script[0]);
    game_frame();
    CHECK(dbg.at == sizeof script / sizeof script[0] && dbg.wrong == 0);
    const Frames* f = &at_count_return;
    CHECK(f->cells[f->a[0].fp[0] + LOCAL_COUNT_BELOW] == 2 &&
          f->cells[f->a[0].fp[0] + LOCAL_COUNT_N] == 3);
    CHECK(vm_global(DEBUG_G_COUNTED) == 3 && vm_idle());
    // Into: a level down, at count(2)'s first statement; out: back in count(3).
    start_thread(DEBUG_OBJ_COUNTER);
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    static const Expect into_out[] = {
        {LINK_STOP_BREAKPOINT, SITE_COUNT_CALL, 1, clear_then_into},
        {LINK_STOP_STEP, SITE_COUNT_BASE, 2, out},
        {LINK_STOP_STEP, 0, 1, NULL},
    };
    debug_with(into_out, sizeof into_out / sizeof into_out[0]);
    game_frame();
    CHECK(dbg.at == sizeof into_out / sizeof into_out[0] && dbg.wrong == 0);
    Stopped st;
    CHECK(stopped(events(LINK_STOPPED) - 1, &st) && statement_of(st.pc) == SITE_COUNT_CALL);
}

// A step stops in the stepped activation only. Stepping a Walker's
// behaviour (its create, waiting), from a frame boundary: the reactions on
// top of it (its step, calling add() at the step sites of kind 2) and the
// other Walker (the same code) run on without a round trip; it stops where
// the create resumes, two frames on.
static int walker_context(Entity e) {
    int k = serval_vm_link_context_of(e);
    CHECK(k >= 0);
    return k;
}

static void step_stops_in_its_own_activation(void) {
    attached();
    Entity w[2] = {entity_create(C_POS), entity_create(C_POS)};
    vm_attach(w[0], DEBUG_OBJ_WALKER);
    vm_attach(w[1], DEBUG_OBJ_WALKER);
    game_frame(); // the creates wait (2 frames); the steps ran once
    int k = walker_context(w[1]);
    // At a frame boundary, parked: the sites, then STEP the second Walker's
    // create, which resumes the game.
    u16 pause = request(LINK_PAUSE, NULL, 0);
    break_set(LINK_SITE_STEP, FN_WALKER_CREATE, FN_WALKER_CREATE_COUNT);
    break_set(LINK_SITE_STEP, FN_WALKER_STEP, FN_WALKER_STEP_COUNT);
    break_set(LINK_SITE_STEP_ANY, FN_ADD, FN_ADD_COUNT);
    u16 s = step_request((u32)k, 0);
    frame();
    CHECK(ok(pause, 0) && ok(s, 0) && !serval_link_parked());
    static const Expect script[] = {{LINK_STOP_STEP, SITE_WALKER_COUNT, 0, NULL}};
    debug_with(script, 1);
    u32 stops = events(LINK_STOPPED);
    game_frame(); // the steps hit every site, and run on
    CHECK(events(LINK_STOPPED) == stops && fake.inside_rings == 0);
    game_frame(); // the creates resume: the second one stops
    CHECK(dbg.at == 1 && dbg.wrong == 0 && events(LINK_STOPPED) == stops + 1);
    Stopped stop;
    CHECK(stopped(stops, &stop) && stop.context == (u32)k);
    CHECK(vm_global(DEBUG_G_STEPS) == 6 && vm_global(DEBUG_G_MOVED) == 6);
    CHECK(vm_global(DEBUG_G_WAITED) == 2);
    CHECK(patched_bytes() == 0); // the stop cleared every step site
}

// A reaction running on top of a waiting behaviour: CONTEXTS shows both
// (stacked, the behaviour beneath waiting), FRAMES two activations; a step
// through the reaction stops at its end (STOPPED, ended) with the behaviour
// beneath as it was, waiting on.
static u16 contexts_seq;
static u8 ctx_entry[26];
static bool ctx_ok;

static void look_at_contexts(const Stopped* s, const Frames* f) {
    (void)s, (void)f;
    contexts_seq = request(LINK_CONTEXTS, NULL, 0);
    plan_step(OVER, s->context, f);
}

static void step_into_add(const Stopped* s, const Frames* f) {
    const u8* e = context_entry(reply(contexts_seq), s->context);
    ctx_ok = e != NULL;
    for (u32 i = 0; e && i < 26; i++)
        ctx_entry[i] = e[i];
    plan_step(INTO, s->context, f);
}

static Frames inside_add;
static void out_of_add(const Stopped* s, const Frames* f) {
    inside_add = *f;
    plan_step(OUT, s->context, f);
}

static void stacked_reaction_over_a_waiting_behaviour(void) {
    attached();
    Entity w = entity_create(C_POS);
    vm_attach(w, DEBUG_OBJ_WALKER);
    game_frame(); // its create waits, its step ran
    int k = walker_context(w);
    u32 site = SITE_WALKER_STEP;
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    static const Expect script[] = {
        {LINK_STOP_BREAKPOINT, SITE_WALKER_STEP, 0, look_at_contexts},
        {LINK_STOP_STEP, SITE_WALKER_MOVE, 0, step_into_add},
        {LINK_STOP_STEP, SITE_ADD_SUM, 1, out_of_add},
        {LINK_STOP_STEP, 0, 0, over},   // back in Walker:step
        {LINK_STOP_STEP, 0, 0, over},   // its implicit return
        {LINK_STOP_ENDED, 0, -1, NULL}, // the reaction ended
    };
    debug_with(script, sizeof script / sizeof script[0]);
    game_frame();
    CHECK(dbg.at == sizeof script / sizeof script[0] && dbg.wrong == 0);
    // CONTEXTS at the breakpoint: the reaction, running, on top of the
    // create, waiting.
    const u8* e = ctx_entry;
    CHECK(ctx_ok && e[0] == (u32)k && e[1] == 2 && e[2] == VM_EV_STEP);
    CHECK(rd16(e + 3) == w && rd16(e + 5) == ENTITY_NONE && (s16)rd16(e + 7) == DEBUG_OBJ_WALKER);
    CHECK(rd32(e + 9) == SITE_WALKER_STEP && e[15] == 0 && e[16] == 1);
    CHECK(e[17] == VM_EV_CREATE && e[18] == 3 && rd32(e + 19) == SITE_WALKER_COUNT &&
          rd16(e + 23) == 1 && e[25] == 0);
    // FRAMES inside add(): the reaction's activation (add's frame, then the
    // return point in Walker:step), then the behaviour's beneath it.
    const Frames* f = &inside_add;
    CHECK(f->activations == 2 && f->a[0].count == 2 && f->a[1].count == 1);
    CHECK(f->a[1].base == 0 && f->a[1].pc[0] == SITE_WALKER_COUNT);
    CHECK(statement_of(f->a[0].pc[1]) == SITE_WALKER_MOVE);
    CHECK(f->a[0].base == 1 && f->cells[LOCAL_WALKER_CREATE_PACE] == 2); // pace, beneath
    CHECK(f->sp > f->a[0].base && f->a[0].fp[0] >= f->a[0].base);
    CHECK(f->cells[f->a[0].fp[0] + LOCAL_ADD_B] == 1); // add(moved, 1)
    // After: the behaviour is as it was, waiting; its wait ran out next frame.
    Stopped s;
    CHECK(stopped(5, &s) && s.context == (u32)k);
    CHECK(serval_vm_link_context_of(w) == k && vm_global(DEBUG_G_WAITED) == 0);
    break_clear(LINK_SITE_BREAKPOINT, NULL, 0);
    game_frame();
    CHECK(vm_global(DEBUG_G_WAITED) == 1 && vm_global(DEBUG_G_STEPS) == 3);
}

// The stepped context halted from outside: vm_detach() of the entity whose
// behaviour is stepped parks the game at once (STOPPED, ended).
static void stepped_context_halted_from_outside(void) {
    attached();
    Entity w = entity_create(C_POS);
    vm_attach(w, DEBUG_OBJ_WALKER);
    game_frame();
    int k = walker_context(w);
    request(LINK_PAUSE, NULL, 0);
    u16 s = step_request((u32)k, 0);
    frame();
    CHECK(ok(s, 0) && serval_vm_link_stepping == k + 1);
    static const Expect script[] = {{LINK_STOP_ENDED, 0, -1, NULL}};
    debug_with(script, 1);
    vm_detach(w); // the game's C code
    CHECK(dbg.at == 1 && dbg.wrong == 0);
    Stopped stop;
    CHECK(stopped(events(LINK_STOPPED) - 1, &stop) && stop.reason == LINK_STOP_ENDED &&
          stop.context == (u32)k && stop.pc == 0);
    CHECK(serval_vm_link_stepping == 0 && !serval_link_parked());
}

// STEP and SET_LOCAL need the game parked; STEP's context must be live.
static void step_needs_a_parked_game(void) {
    attached();
    int k = start_thread(DEBUG_OBJ_LOOPER);
    u16 s = step_request((u32)k, 0);
    u16 l = set_local((u32)k, 0, 1);
    frame();
    CHECK(error(s, LINK_NOT_PARKED) && error(l, LINK_NOT_PARKED));
    request(LINK_PAUSE, NULL, 0);
    fake.on_wait = NULL;
    u16 dead = step_request(31, 0);
    u16 none = step_request(0xFF, 0);
    u16 shrt = request(LINK_STEP, (const u8[]){(u8)k}, 1);
    u16 go = resume(0);
    frame();
    CHECK(error(dead, LINK_NO_SUCH) && error(none, LINK_NO_SUCH) && error(shrt, LINK_BAD_REQUEST));
    CHECK(ok(go, 0));
}

// --- CONTEXTS, FRAMES, SET_LOCAL ---------------------------------------------------------------

// SET_LOCAL while parked at a breakpoint: `extra` becomes 37, and the
// script goes on with it. Its errors: a cell at or past sp, no context.
static u16 set_seq, past_seq, nobody_seq;
static void set_extra(const Stopped* s, const Frames* f) {
    set_seq = set_local(s->context, f->a[0].fp[0] + LOCAL_LOOPER_CREATE_EXTRA, 37);
    past_seq = set_local(s->context, f->sp, 1);
    nobody_seq = set_local(30, 0, 1);
    resume(0);
}

static void set_local_writes_a_cell(void) {
    attached();
    start_thread(DEBUG_OBJ_LOOPER);
    u32 site = SITE_LOOPER_ADD;
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    static const Expect script[] = {
        {LINK_STOP_BREAKPOINT, SITE_LOOPER_ADD, 0, set_extra},
        {LINK_STOP_BRK, SITE_LOOPER_BREAK, 0, NULL},
    };
    debug_with(script, 2);
    game_frame();
    CHECK(dbg.at == 2 && dbg.wrong == 0);
    CHECK(ok(set_seq, 0) && error(past_seq, LINK_NO_SUCH) && error(nobody_seq, LINK_NO_SUCH));
    CHECK(vm_global(DEBUG_G_RESULT) == 220 + 37);
}

// CONTEXTS and FRAMES at a frame boundary: every live context, its
// registers as it saved them at its wait.
static void contexts_and_frames_at_a_boundary(void) {
    attached();
    Entity w = entity_create(C_POS);
    vm_attach(w, DEBUG_OBJ_WALKER);
    game_frame();
    int k = walker_context(w);
    u16 c = request(LINK_CONTEXTS, NULL, 0);
    u16 f = request(LINK_FRAMES, (const u8[]){(u8)k}, 1);
    u16 dead = request(LINK_FRAMES, (const u8[]){(u8)(k + 1)}, 1);
    frame();
    const u8* e = context_entry(reply(c), (u32)k);
    CHECK(reply(c) && reply(c)[6] == 1 && rd16(reply(c)) == 4 + 1 + 26);
    CHECK(e && e[1] == 3 && e[2] == VM_EV_CREATE && rd16(e + 3) == w);
    CHECK(rd32(e + 9) == SITE_WALKER_COUNT && rd16(e + 13) == 2 && e[15] == 0 && e[16] == 0);
    Frames fr;
    CHECK(frames_of(reply(f), &fr) && fr.activations == 1 && fr.a[0].count == 1);
    CHECK(fr.a[0].pc[0] == SITE_WALKER_COUNT && fr.a[0].base == 0 && fr.sp == 1);
    CHECK(fr.cells[LOCAL_WALKER_CREATE_PACE] == 2);
    CHECK(error(dead, LINK_NO_SUCH));
}

// --- Faults ----------------------------------------------------------------------------------

typedef struct {
    u32 kind, context, pc;
    const u8* text;
} FaultEvent;

static bool fault_event(u32 nth, FaultEvent* out) {
    const u8* f = event(LINK_FAULT, nth);
    if (!f)
        return false;
    *out = (FaultEvent){f[6], f[7], rd32(f + 8), f + 12};
    return true;
}

static bool text_starts(const u8* str, const char* text) {
    u32 n = 0;
    while (text[n])
        n++;
    if (str[0] < n)
        return false;
    for (u32 i = 0; i < n; i++) {
        if ((char)str[1 + i] != text[i])
            return false;
    }
    return true;
}

static Frames at_fault;
static void look_at_the_fault(const Stopped* s, const Frames* f) {
    (void)s;
    at_fault = *f;
    resume(0);
}

// With stop on fault, the game parks before the VM halts the faulting
// context: FAULT (its kind, the context, the pc, the VM's warning), then
// STOPPED (fault), FRAMES showing the state that faulted (VM_CALLS calls
// deep); resumed, the context is halted. Again in the same blob: FAULT and
// the stop again (stop on fault: every fault), its text saying the warning
// came before. Without stop on fault: FAULT only when the VM warns, once
// per kind per loaded blob.
static void fault_parks_before_the_halt(void) {
    attached();
    u16 o = request(LINK_OPTIONS, (const u8[]){1, 0, 0, 0}, 4);
    frame();
    CHECK(ok(o, 0));
    int k = start_thread(DEBUG_OBJ_DEEP);
    static const Expect script[] = {
        {LINK_STOP_FAULT, 0, VM_CALLS, look_at_the_fault},
        {LINK_STOP_FAULT, 0, VM_CALLS, NULL},
    };
    debug_with(script, 2);
    u32 warnings = debug_warning_count();
    game_frame();
    CHECK(dbg.at == 1 && dbg.wrong == 0 && debug_warning_count() == warnings + 1);
    FaultEvent fe;
    CHECK(fault_event(0, &fe) && fe.kind == LINK_FAULT_CALLS && fe.context == (u32)k);
    CHECK(fe.pc == dbg.stop.pc && link_debug_script[fe.pc] == VM_OP_CALL);
    CHECK(statement_of(fe.pc) == SITE_DEEP_CALL && text_starts(fe.text, "vm: CALL at 0x"));
    Stopped s;
    CHECK(stopped(0, &s) && s.fault == LINK_FAULT_CALLS && s.context == (u32)k);
    CHECK(at_fault.a[0].count == VM_CALLS + 1 && vm_idle()); // halted once resumed
    // Again: stopped again, and a FAULT saying the warning came before.
    start_thread(DEBUG_OBJ_DEEP);
    game_frame();
    CHECK(dbg.at == 2 && dbg.wrong == 0 && debug_warning_count() == warnings + 1);
    CHECK(fault_event(1, &fe) && text_starts(fe.text, "vm: call depth past VM_CALLS at 0x"));
    // Stop on fault off: no FAULT for a repeat; after a load the warning
    // comes again, and FAULT with it, without a stop.
    request(LINK_OPTIONS, (const u8[]){0, 0, 0, 0}, 4);
    frame();
    start_thread(DEBUG_OBJ_DEEP);
    game_frame();
    CHECK(events(LINK_FAULT) == 2 && events(LINK_STOPPED) == 2);
    CHECK(vm_load(link_debug_script, link_debug_script_size));
    start_thread(DEBUG_OBJ_DEEP);
    game_frame();
    CHECK(events(LINK_FAULT) == 3 && events(LINK_STOPPED) == 2 && vm_idle());
    CHECK(fault_event(2, &fe) && text_starts(fe.text, "vm: CALL at 0x"));
}

// ENTER that doesn't fit (heavy()'s eight locals, VM_STACK deep) is kind 6;
// a behaviour past its budget, kind 7, parks before it is throttled, and
// waits a frame afterwards.
static void fault_kinds_enter_and_budget(void) {
    attached();
    request(LINK_OPTIONS, (const u8[]){1, 0, 0, 0}, 4);
    frame();
    start_thread(DEBUG_OBJ_HEAVY);
    static const Expect script[] = {
        {LINK_STOP_FAULT, 0, -1, NULL},
        {LINK_STOP_FAULT, 0, 0, look_at_the_fault},
    };
    debug_with(script, 2);
    game_frame();
    FaultEvent fe;
    CHECK(fault_event(0, &fe) && fe.kind == LINK_FAULT_ENTER);
    CHECK(function_of(fe.pc) == &FUNCTIONS[6] && fe.pc == FUNCTIONS[6].first); // heavy's ENTER
    int k = start_thread(DEBUG_OBJ_SPINNER);
    game_frame();
    CHECK(dbg.at == 2 && dbg.wrong == 0);
    CHECK(fault_event(1, &fe) && fe.kind == LINK_FAULT_BUDGET && fe.context == (u32)k);
    Stopped s;
    CHECK(stopped(1, &s) && s.fault == LINK_FAULT_BUDGET && function_of(s.pc) == &FUNCTIONS[9]);
    CHECK(vm_ops_this_frame() == VM_OPS_PER_SLICE);
    ServalVmLinkContext c;
    CHECK(serval_vm_link_context((u32)k, &c) && c.state == 3 && c.wait == 1 && c.pc == s.pc);
}

// --- debug_break() ---------------------------------------------------------------------------

// In C: parks the game while attached (STOPPED, debug_break, no context, no
// pc), parked inside the frame; with no debugger it does nothing, and warns
// nothing.
static void debug_break_in_c(void) {
    attached();
    static const Expect script[] = {{LINK_STOP_DEBUG_BREAK, 0, -1, NULL}};
    debug_with(script, 1);
    debug_break();
    CHECK(dbg.at == 1 && dbg.wrong == 0 && fake.inside_rings >= 1);
    Stopped s;
    CHECK(stopped(0, &s) && s.context == 0xFF && s.pc == 0 && s.fault == 0);
    CHECK(!serval_link_parked());
    // Detached: nothing, at once.
    mailbox()->endpoint = 0;
    frame();
    u32 rings = fake.rings, warnings = debug_warning_count();
    debug_break();
    CHECK(fake.rings == rings && debug_warning_count() == warnings && events(LINK_STOPPED) == 1);
}

// --- LOADED, STATS -------------------------------------------------------------------------

// LOADED at every load, the same blob at the same address included (the
// loader's hook); every site goes with it: the VM runs the game's blob, and
// a site set again copies it again.
static void loaded_from_the_loader(void) {
    attached();
    u32 site = SITE_ADD_SUM;
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    u32 loads = events(LINK_LOADED); // attached(): AGENT_INFO, then vm_load
    CHECK(running_from_copy() && loads == 1);
    CHECK(vm_load(link_debug_script, link_debug_script_size)); // fireflies' restart
    frame();                                                   // the next ring sends it
    CHECK(events(LINK_LOADED) == loads + 1);
    const u8* f = event(LINK_LOADED, loads);
    CHECK(f && rd32(f + 6) == link_debug_script_size &&
          rd32(f + 10) == serval_link_fnv1a(link_debug_script, link_debug_script_size));
    CHECK(!running_from_copy());
    start_thread(DEBUG_OBJ_LOOPER);
    debug_with(NULL, 0);
    dbg.any = true;
    game_frame();
    CHECK(dbg.hits == 1); // the script's own BRK: no breakpoint is left
    break_set(LINK_SITE_BREAKPOINT, &site, 1);
    frame();
    CHECK(running_from_copy() && patched_bytes() == 1);
}

// STATS' vm_cycles: the cycle counter read around the last frame's two
// phases (each read here adds 1,000); its queue: the most events queued at
// once, inside the phases (Burst's six Creates, drained in the same phase).
static void stats_vm_cycles_and_queue_peak(void) {
    attached();
    request(LINK_STATS_SUBSCRIBE, (const u8[]){1, 0}, 2);
    frame();
    serval_host_link.cycles_step = 1000;
    start_thread(DEBUG_OBJ_BURST);
    game_frame();
    const u8* f = event(LINK_STATS, events(LINK_STATS) - 1);
    CHECK(f && rd32(f + 6 + 8) == 2000);
    CHECK(f && rd16(f + 6 + 17) == 6);
    CHECK(vm_idle());
    game_frame();
    f = event(LINK_STATS, events(LINK_STATS) - 1);
    CHECK(f && rd32(f + 6 + 8) == 2000 && rd16(f + 6 + 17) == 0);
}

// --- Detaching -------------------------------------------------------------------------------

// The debugger leaving while the game is parked at a breakpoint: every
// patched opcode is restored (the copy byte for byte the blob again), and
// the game goes on, the breakpoint's opcode run in its place.
static void leave(u32 n) {
    (void)n;
    mailbox()->endpoint = 0;
}

static void detaching_while_parked_inside(void) {
    attached();
    start_thread(DEBUG_OBJ_LOOPER);
    break_set(LINK_SITE_BREAKPOINT, STATEMENTS, STATEMENT_COUNT);
    frame();
    fake.on_wait = leave;
    game_frame();
    CHECK(!serval_link_attached() && !serval_link_parked());
    CHECK(running_from_copy() && patched_bytes() == 0);
    CHECK(vm_global(DEBUG_G_TOTAL) == 56 && vm_global(DEBUG_G_RESULT) == 225 && vm_idle());
    CHECK(events(LINK_STOPPED) == 1);
}

// Last: the agent left dormant for the suites after this one.
static void leave_dormant(void) {
    start(false);
    CHECK(!serval_link_started() && !serval_link_attached());
}

TEST_SUITE(link_vm_tests, "link_vm", {"capabilities and sites", capabilities_and_sites},
           {"a breakpoint in a loop", breakpoint_in_a_loop_hits_every_time},
           {"displaced execution", displaced_execution_changes_nothing},
           {"set while parked", breakpoint_set_while_parked_hits_in_the_same_run},
           {"BREAK_SET and BREAK_CLEAR", break_set_and_clear},
           {"over, into, out", stepping_over_into_and_out},
           {"over a recursive call", step_over_a_recursive_call},
           {"a step's own activation", step_stops_in_its_own_activation},
           {"a stacked reaction", stacked_reaction_over_a_waiting_behaviour},
           {"halted from outside", stepped_context_halted_from_outside},
           {"STEP needs a park", step_needs_a_parked_game}, {"SET_LOCAL", set_local_writes_a_cell},
           {"CONTEXTS and FRAMES", contexts_and_frames_at_a_boundary},
           {"a fault parks", fault_parks_before_the_halt},
           {"ENTER and budget faults", fault_kinds_enter_and_budget},
           {"debug_break() in C", debug_break_in_c}, {"LOADED", loaded_from_the_loader},
           {"STATS", stats_vm_cycles_and_queue_peak},
           {"detaching while parked", detaching_while_parked_inside},
           {"left dormant", leave_dormant});

#else

static void no_agent(void) {
    CHECK(true);
}

TEST_SUITE(link_vm_tests, "link_vm", {"no agent in this build", no_agent});

#endif // SERVAL_LINK_AGENT
