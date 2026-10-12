// The debug link's agent, its hot reload (step E9 of docs/debug-link.md:
// src/core/link_reload.c and vm.c's serval_vm_link_reload()) against the
// fake endpoint (link_fake.h): RELOAD_BEGIN, RELOAD_DATA, RELOAD_PLAN,
// RELOAD_COMMIT and RELOADED, with real compiled Lua. link_reload_v1.lua is
// the running game; link_reload_v2.lua its edit (an object removed, a
// function moved, a global and an array added in the middle, two handlers
// changed), built without and with KEEP_LAYOUT; the plans between them are
// tools/svlink.py's (link_reload_plans.h, from link_reload_plan.py), all but
// the contexts' section, which the debugger here computes from the running
// game as a debugger computes it from CONTEXTS and FRAMES. Kept contexts go
// on exactly where they were (an undisturbed run of the new blob is the
// reference), restarted ones restart, state follows names, aborts leave the
// game as it was, and RELOADED says what happened. Host only.

#include "link_fake.h"
#include "link_reload_plans.h"
#include "link_reload_v1_script.h"
#include "link_reload_v2_script.h"
#include "link_reload_v2k_script.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/vm.h"
#include "test.h"

#ifdef SERVAL_LINK_AGENT

// --- The game ------------------------------------------------------------------------

// A version of the script: its blob and its objects' numbers (-1: none).
typedef struct {
    const u8* blob;
    u32 size;
    int doomed, walker, counter, clock, pacer;
} Version;

static Version v1(void) {
    return (Version){link_reload_v1_script, link_reload_v1_script_size,
                     R1_OBJ_DOOMED,         R1_OBJ_WALKER,
                     R1_OBJ_COUNTER,        R1_OBJ_CLOCK,
                     R1_OBJ_PACER};
}

static Version v2(void) {
    return (Version){link_reload_v2_script,
                     link_reload_v2_script_size,
                     -1,
                     R2_OBJ_WALKER,
                     R2_OBJ_COUNTER,
                     R2_OBJ_CLOCK,
                     R2_OBJ_PACER};
}

static Version v2k(void) {
    return (Version){link_reload_v2k_script,
                     link_reload_v2k_script_size,
                     -1,
                     R2K_OBJ_WALKER,
                     R2K_OBJ_COUNTER,
                     R2K_OBJ_CLOCK,
                     R2K_OBJ_PACER};
}

// The instances (always the same entities, in the same slots, attached where
// the version has the object) and the threads' contexts.
static struct {
    Entity doomed, walker, counter;
    int clock, pacer;
} game;

static void game_frame(void) {
    vm_step();
    vm_events();
    frame();
}

static void attach_to(Entity e, int object) {
    if (object >= 0)
        vm_attach(e, (u16)object);
}

// The agent started with a debugger attached, a version loaded, its
// instances and threads started, `frames` frames played.
static void play(Version v, u32 frames) {
    start(true);
    attach();
    CHECK(vm_load(v.blob, v.size));
    game.doomed = entity_create(C_POS);
    game.walker = entity_create(C_POS);
    game.counter = entity_create(C_POS);
    attach_to(game.doomed, v.doomed);
    attach_to(game.walker, v.walker);
    attach_to(game.counter, v.counter);
    game.clock = vm_start((u16)v.clock, VM_EV_ROOM_START);
    game.pacer = vm_start((u16)v.pacer, VM_EV_ROOM_START);
    CHECK(game.clock >= 0 && game.pacer >= 0);
    for (u32 f = 0; f < frames; f++)
        game_frame();
}

static s32 x_of(Entity e) {
    return pos_x[entity_index(e)];
}

static int context_of(Entity e) {
    return serval_vm_link_context_of(e);
}

// A cell as runs compare it: the game's entities by their role, since a
// new run's entities have new handles (ecs_reset() moves the generations on).
static s32 normal(s32 cell) {
    Entity es[3] = {game.doomed, game.walker, game.counter};
    for (u32 i = 0; i < 3; i++) {
        if (cell == es[i])
            return -1000 - (s32)i;
    }
    return cell;
}

static const u8* running_blob(void) {
    u32 size;
    return serval_vm_link_blob(&size);
}

// --- The debugger -----------------------------------------------------------------------

// A reload's target: the new blob, its plan's unchanged functions, and the
// plan's other sections as RELOAD_PLAN payloads (each after its length).
typedef struct {
    const u8* blob;
    u32 size;
    const u32 (*functions)[3];
    u32 function_count;
    const u8* plan;
    u32 plan_size;
} Target;

#define TARGET(name, version)                                                                      \
    ((Target){version().blob, version().size, name##_FUNCTIONS, name##_FUNCTION_COUNT,             \
              name##_PLAN, name##_PLAN_SIZE})

static bool in_unchanged(const Target* t, u32 pc) {
    for (u32 n = 0; n < t->function_count; n++) {
        if (t->functions[n][0] <= pc && pc < t->functions[n][1])
            return true;
    }
    return false;
}

// Section 2, as a debugger computes it from CONTEXTS and FRAMES: each live
// context relocated if its pc and every return point lie in an unchanged
// function, else `otherwise` (LINK_RELOAD_RESTART, LINK_RELOAD_HALT), or
// `every` for all (unless 0xFF). Returns the payload's size.
static u32 contexts_payload(const Target* t, u32 otherwise, u32 every, u8* p) {
    u32 n = 0;
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        ServalVmLinkContext c;
        if (!serval_vm_link_context(k, &c))
            continue;
        bool kept = in_unchanged(t, c.pc);
        for (u32 i = 0; i < c.cp; i++)
            kept = kept && in_unchanged(t, c.call_pc[i]);
        p[2 + 2 * n] = (u8)k;
        p[3 + 2 * n] = (u8)(every != 0xFF ? every : kept ? LINK_RELOAD_RELOCATE : otherwise);
        n++;
    }
    p[0] = 2; // section 2
    p[1] = (u8)n;
    return 2 + 2 * n;
}

static u16 begin(u32 size, u32 hash) {
    u8 p[8];
    wr32(p, size);
    wr32(p + 4, hash);
    return request(LINK_RELOAD_BEGIN, p, 8);
}

// RELOAD_DATA: bytes [from, to) of blob, in frames of 1,014.
static u16 data(const u8* blob, u32 from, u32 to) {
    static u8 p[4 + 1014];
    u16 s = 0;
    for (u32 at = from; at < to; at += 1014) {
        u32 n = to - at < 1014 ? to - at : 1014;
        wr32(p, at);
        for (u32 i = 0; i < n; i++)
            p[4 + i] = blob[at + i];
        s = request(LINK_RELOAD_DATA, p, 4 + n);
    }
    return s;
}

// The plan's payloads (each after its length), every one sent; the last's seq.
static u16 plan(const u8* payloads, u32 size) {
    u16 s = 0;
    for (u32 at = 0; at + 2 <= size; at += 2 + rd16(payloads + at))
        s = request(LINK_RELOAD_PLAN, payloads + at + 2, rd16(payloads + at));
    return s;
}

static u16 resume(void) {
    return request(LINK_RESUME, (const u8[]){0}, 1);
}

typedef struct {
    u16 pause, begin, data, plan, contexts, commit, resume;
} Reload;

// A whole reload, as a debugger sends it: PAUSE (the game parks at the next
// frame boundary, where the poll reads all of it), RELOAD_BEGIN, the blob,
// the plan (its contexts' section from the game as it is now: `otherwise`
// and `every` as contexts_payload's), RELOAD_COMMIT and RESUME. Queued now,
// handled by the next frame().
static Reload send_reload(const Target* t, u32 otherwise, u32 every) {
    Reload r;
    static u8 contexts[2 + 2 * VM_CONTEXTS];
    u32 n = contexts_payload(t, otherwise, every, contexts);
    r.pause = request(LINK_PAUSE, NULL, 0);
    r.begin = begin(t->size, serval_link_fnv1a(t->blob, t->size));
    r.data = data(t->blob, 0, t->size);
    r.plan = plan(t->plan, t->plan_size);
    r.contexts = request(LINK_RELOAD_PLAN, contexts, n);
    r.commit = request(LINK_RELOAD_COMMIT, NULL, 0);
    r.resume = resume();
    return r;
}

static bool reload_ok(const Reload* r) {
    return ok(r->pause, 0) && ok(r->begin, 0) && ok(r->data, 0) && ok(r->plan, 0) &&
           ok(r->contexts, 0) && ok(r->commit, 0) && ok(r->resume, 0);
}

// The nth RELOADED: its result, its count, and each context's outcome
// (LINK_RELOAD_NOT_LIVE for those it doesn't list).
typedef struct {
    u32 result, count;
    u8 outcome[VM_CONTEXTS];
} Reloaded;

static bool reloaded(u32 nth, Reloaded* out) {
    const u8* f = event(LINK_RELOADED, nth);
    if (!f || rd16(f) < 4 + 2)
        return false;
    out->result = f[6];
    out->count = f[7];
    for (u32 k = 0; k < VM_CONTEXTS; k++)
        out->outcome[k] = LINK_RELOAD_NOT_LIVE;
    if (rd16(f) != 4 + 2 + 2 * out->count)
        return false;
    for (u32 i = 0; i < out->count; i++) {
        if (f[8 + 2 * i] >= VM_CONTEXTS)
            return false;
        out->outcome[f[8 + 2 * i]] = f[9 + 2 * i];
    }
    return true;
}

// The game's state, for comparing runs: the globals and the pool's first
// cells, the instances (position, object, fields) and every context.
typedef struct {
    s32 globals[8];
    s32 cells[8];
    s32 x[3];
    int object[3];
    s32 fields[3][VM_FIELDS];
    u32 queued;
    struct {
        bool live;
        u32 pc, state, wait, cp, sp, call0;
        s32 cells[8];
    } ctx[VM_CONTEXTS];
} Snapshot;

static void snapshot(Snapshot* s) {
    *s = (Snapshot){0};
    for (u32 n = 0; n < 8; n++) {
        s->globals[n] = vm_global((u16)n);
        s->cells[n] = serval_vm_array_cell(n);
    }
    Entity es[3] = {game.doomed, game.walker, game.counter};
    for (u32 i = 0; i < 3; i++) {
        s->x[i] = x_of(es[i]);
        s->object[i] = vm_object_of(es[i]);
        for (u32 n = 0; n < VM_FIELDS; n++)
            s->fields[i][n] = serval_vm_field(es[i], n);
    }
    s->queued = serval_vm_link_queued();
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        ServalVmLinkContext c;
        if (!serval_vm_link_context(k, &c))
            continue;
        s->ctx[k].live = true;
        s->ctx[k].pc = c.pc;
        s->ctx[k].state = c.state;
        s->ctx[k].wait = c.wait;
        s->ctx[k].cp = c.cp;
        s->ctx[k].sp = c.sp;
        s->ctx[k].call0 = c.cp ? c.call_pc[0] : 0;
        for (u32 n = 0; n < 8 && n < c.sp; n++)
            s->ctx[k].cells[n] = normal(c.stack[n]);
    }
}

static bool same(const Snapshot* a, const Snapshot* b) {
    const u8* p = (const u8*)a;
    const u8* q = (const u8*)b;
    for (u32 i = 0; i < sizeof *a; i++) {
        if (p[i] != q[i])
            return false;
    }
    return true;
}

// --- Relocate, restart, halt -------------------------------------------------------------

// v1 -> v2, built without KEEP_LAYOUT: the Walker, waiting inside stroll()
// (moved, unchanged), is kept at its new offsets, its return point in
// Walker:create too; the Clock (its `ticks` renumbered by the global added
// before it), the Pacer and the Counter (edited) restart, the Doomed's
// instance (its object gone) is detached. Every global, array cell and
// field follows its name; the new global starts at its initial value.
// RELOADED says so, context by context.
static void reload_relocates_restarts_and_detaches(void) {
    play(v1(), 10);
    int walker = context_of(game.walker), counter = context_of(game.counter);
    int doomed = context_of(game.doomed);
    CHECK(walker >= 0 && counter >= 0 && doomed >= 0);
    ServalVmLinkContext before;
    CHECK(serval_vm_link_context((u32)walker, &before) && before.cp == 1);
    u32 pc = before.pc, ret = before.call_pc[0], sp = before.sp;
    s32 cells[8];
    for (u32 n = 0; n < 8 && n < sp; n++)
        cells[n] = before.stack[n];
    CHECK(pc >= LINK_RELOAD_V1_FN_STROLL_START && pc < LINK_RELOAD_V1_FN_STROLL_END);
    s32 score = vm_global(R1_G_SCORE), ticks = vm_global(R1_G_TICKS);
    s32 history = serval_vm_array_cell(LINK_RELOAD_V1_CELLS_HISTORY);
    s32 seen = serval_vm_field(game.walker, LINK_RELOAD_V1_FIELD_SEEN);
    s32 walker_x = x_of(game.walker), counter_x = x_of(game.counter);
    CHECK(vm_global(R1_G_STARTS) == 2 && score > 10 && history == score && seen == 6 * 10);
    CHECK(serval_vm_field(game.doomed, LINK_RELOAD_V1_FIELD_FATE) == 3);

    Target t = TARGET(V2, v2);
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    CHECK(reload_ok(&r));
    Reloaded done;
    CHECK(events(LINK_RELOADED) == 1 && reloaded(0, &done) && done.result == 0 && done.count == 5);
    CHECK(done.outcome[walker] == LINK_RELOAD_KEPT);
    CHECK(done.outcome[game.clock] == LINK_RELOAD_RESTARTED &&
          done.outcome[game.pacer] == LINK_RELOAD_RESTARTED);
    CHECK(done.outcome[counter] == LINK_RELOAD_RESTARTED &&
          done.outcome[doomed] == LINK_RELOAD_HALTED);
    CHECK(events(LINK_LOADED) == 1); // vm_load's at the start: a reload sends none
    CHECK(running_blob() != link_reload_v1_script && running_blob() != link_reload_v2_script);

    // At the frame boundary, before the game goes on. The Walker: moved, its
    // stack as it was.
    ServalVmLinkContext after;
    CHECK(serval_vm_link_context((u32)walker, &after) && after.cp == 1 && after.sp == sp);
    CHECK(after.pc == pc - LINK_RELOAD_V1_FN_STROLL_START + LINK_RELOAD_V2_FN_STROLL_START);
    CHECK(after.call_pc[0] ==
          ret - LINK_RELOAD_V1_FN_WALKER_CREATE_START + LINK_RELOAD_V2_FN_WALKER_CREATE_START);
    CHECK(after.state == 3 && after.wait == before.wait);
    u32 differ = 0;
    for (u32 n = 0; n < 8 && n < sp; n++)
        differ += after.stack[n] != cells[n];
    CHECK(differ == 0);
    // The threads: restarted in place, ready at their handlers' starts.
    ServalVmLinkContext clock, pacer;
    CHECK(serval_vm_link_context((u32)game.clock, &clock) && clock.state == 1 && clock.sp == 0);
    CHECK(clock.pc == LINK_RELOAD_V2_FN_CLOCK_ROOM_START_START);
    CHECK(serval_vm_link_context((u32)game.pacer, &pacer) && pacer.state == 1);
    CHECK(pacer.pc == LINK_RELOAD_V2_FN_PACER_ROOM_START_START);
    // The Counter: its behaviour halted, its Create queued; renumbered.
    CHECK(context_of(game.counter) < 0 && serval_vm_link_queued() == 1);
    CHECK(vm_object_of(game.counter) == R2_OBJ_COUNTER &&
          vm_object_of(game.walker) == R2_OBJ_WALKER);
    // The Doomed: detached (no Destroy), alive, its fields gone.
    CHECK(entity_alive(game.doomed) && vm_object_of(game.doomed) == -1);
    CHECK(serval_vm_field(game.doomed, 0) == 0 && context_of(game.doomed) < 0);
    // State, by name.
    CHECK(vm_global(R2_G_SCORE) == score && vm_global(R2_G_BONUS) == 7);
    CHECK(vm_global(R2_G_LIVES) == 3 && vm_global(R2_G_TICKS) == ticks);
    CHECK(vm_global(R2_G_STARTS) == 2 && vm_global(R2_G_DESTROYED) == 0);
    CHECK(serval_vm_array_cell(LINK_RELOAD_V2_CELLS_HISTORY) == history);
    CHECK(serval_vm_array_cell(LINK_RELOAD_V2_CELLS_EXTRA) == 0 &&
          serval_vm_array_cell(LINK_RELOAD_V2_CELLS_EXTRA + 1) == 0);
    CHECK(serval_vm_field(game.walker, LINK_RELOAD_V2_FIELD_SEEN) == seen);
    CHECK(serval_vm_field(game.counter, LINK_RELOAD_V2_FIELD_RUNS) == 1);

    // Then the game goes on: the Walker a pixel a frame, as before; the
    // restarted handlers' side effects repeat (starts, runs), the edits show
    // (the Counter 3 pixels a frame, the Pacer 5 points a pass).
    for (u32 f = 0; f < 6; f++)
        game_frame();
    CHECK(x_of(game.walker) == walker_x + 6 * 256);
    CHECK(vm_global(R2_G_STARTS) == 4 &&
          serval_vm_field(game.counter, LINK_RELOAD_V2_FIELD_RUNS) == 2);
    CHECK(x_of(game.counter) == counter_x + 5 * 3 * 256); // its Create ran in the first frame
    CHECK(vm_global(R2_G_SCORE) == score + 2 * 5 && vm_global(R2_G_TICKS) == ticks + 5);
    CHECK(serval_vm_field(game.walker, LINK_RELOAD_V2_FIELD_SEEN) == seen + 6 * 6);
    CHECK(vm_global(R2_G_DESTROYED) == 0 && vm_global(R2_G_BONUS) == 7);
    CHECK(serval_vm_array_cell(LINK_RELOAD_V2_CELLS_EXTRA + 1) == 3 + 7);
}

// Kept contexts go on exactly where they were: v1 for 10 frames, a reload
// to v2 built with KEEP_LAYOUT (its globals', fields' and arrays' numbers
// v1's, so the Clock's and the Walker's Step code are unchanged too), 7
// frames, against v2 played undisturbed for 17 frames: the Walker's and the
// Clock's contexts (pc, return point, wait, stack) and what they move and
// count are the same, cell for cell.
static void kept_contexts_match_an_undisturbed_run(void) {
    play(v1(), 10);
    int walker = context_of(game.walker);
    Target t = TARGET(V2K, v2k);
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    Reloaded done;
    CHECK(reload_ok(&r) && reloaded(0, &done) && done.result == 0);
    CHECK(done.outcome[walker] == LINK_RELOAD_KEPT && done.outcome[game.clock] == LINK_RELOAD_KEPT);
    CHECK(done.outcome[game.pacer] == LINK_RELOAD_RESTARTED);
    CHECK(vm_global(R2K_G_LIVES) == 3 &&
          vm_global(R2K_G_BONUS) == 7); // appended, its initial value
    for (u32 f = 0; f < 7; f++)
        game_frame();
    ServalVmLinkContext a;
    CHECK(serval_vm_link_context((u32)context_of(game.walker), &a));
    ServalVmLinkContext ca;
    CHECK(serval_vm_link_context((u32)game.clock, &ca));
    s32 walker_x = x_of(game.walker),
        seen = serval_vm_field(game.walker, LINK_RELOAD_V2K_FIELD_SEEN);
    s32 ticks = vm_global(R2K_G_TICKS);
    u32 pc = a.pc, ret = a.call_pc[0], wait = a.wait, sp = a.sp, clock_pc = ca.pc;
    s32 cells[8];
    for (u32 n = 0; n < 8 && n < sp; n++)
        cells[n] = normal(a.stack[n]);

    play(v2k(), 17); // the reference: the new blob, undisturbed
    ServalVmLinkContext b, cb;
    CHECK(serval_vm_link_context((u32)context_of(game.walker), &b));
    CHECK(serval_vm_link_context((u32)game.clock, &cb));
    CHECK(b.pc == pc && b.cp == 1 && b.call_pc[0] == ret && b.wait == wait && b.sp == sp);
    u32 differ = 0;
    for (u32 n = 0; n < 8 && n < sp; n++)
        differ += normal(b.stack[n]) != cells[n];
    CHECK(differ == 0 && cb.pc == clock_pc);
    CHECK(x_of(game.walker) == walker_x && vm_global(R2K_G_TICKS) == ticks);
    CHECK(serval_vm_field(game.walker, LINK_RELOAD_V2K_FIELD_SEEN) == seen);
}

// The "halt instead" mode (action 3 for every context): what can be kept is,
// the rest halts: nothing restarts, no side effect repeats.
static void halt_instead(void) {
    play(v1(), 10);
    int walker = context_of(game.walker), counter = context_of(game.counter);
    s32 counter_x = x_of(game.counter), ticks = vm_global(R1_G_TICKS);
    Target t = TARGET(V2, v2);
    Reload r = send_reload(&t, 0, LINK_RELOAD_RELOCATE_OR_HALT);
    frame();
    Reloaded done;
    CHECK(reload_ok(&r) && reloaded(0, &done) && done.result == 0 && done.count == 5);
    CHECK(done.outcome[walker] == LINK_RELOAD_KEPT && done.outcome[counter] == LINK_RELOAD_HALTED);
    CHECK(done.outcome[game.clock] == LINK_RELOAD_HALTED &&
          done.outcome[game.pacer] == LINK_RELOAD_HALTED);
    CHECK(serval_vm_link_queued() == 0 && serval_vm_link_contexts() == 1);
    for (u32 f = 0; f < 4; f++)
        game_frame();
    CHECK(vm_global(R2_G_STARTS) == 2 && x_of(game.counter) == counter_x);
    CHECK(vm_global(R2_G_TICKS) == ticks && serval_vm_link_contexts() == 1);
    CHECK(vm_object_of(game.counter) == R2_OBJ_COUNTER); // halted, still attached
}

// Each action as asked: the Walker restarted (its Create queued again, the
// instance's fields kept), the Clock halted, the Pacer not listed (halted),
// the Counter relocated where it can't be (restarted instead), the Doomed
// relocated though its object is gone (halted), and a context that isn't
// live ignored.
static void actions_as_asked(void) {
    play(v1(), 10);
    int walker = context_of(game.walker), counter = context_of(game.counter);
    int doomed = context_of(game.doomed);
    s32 seen = serval_vm_field(game.walker, LINK_RELOAD_V1_FIELD_SEEN);
    s32 walker_x = x_of(game.walker);
    u8 p[2 + 2 * 5] = {2,
                       5,
                       (u8)walker,
                       LINK_RELOAD_RESTART,
                       (u8)game.clock,
                       LINK_RELOAD_HALT,
                       (u8)counter,
                       LINK_RELOAD_RELOCATE,
                       (u8)doomed,
                       LINK_RELOAD_RELOCATE,
                       30,
                       LINK_RELOAD_RELOCATE};
    Target t = TARGET(V2, v2);
    u16 pause = request(LINK_PAUSE, NULL, 0);
    begin(t.size, serval_link_fnv1a(t.blob, t.size));
    data(t.blob, 0, t.size);
    plan(t.plan, t.plan_size);
    u16 contexts = request(LINK_RELOAD_PLAN, p, sizeof p);
    u16 commit = request(LINK_RELOAD_COMMIT, NULL, 0);
    resume();
    frame();
    Reloaded done;
    CHECK(ok(pause, 0) && ok(contexts, 0) && ok(commit, 0));
    CHECK(reloaded(0, &done) && done.result == 0 && done.count == 5);
    CHECK(done.outcome[walker] == LINK_RELOAD_RESTARTED &&
          done.outcome[game.clock] == LINK_RELOAD_HALTED);
    CHECK(done.outcome[game.pacer] == LINK_RELOAD_HALTED);
    CHECK(done.outcome[counter] == LINK_RELOAD_RESTARTED &&
          done.outcome[doomed] == LINK_RELOAD_HALTED);
    CHECK(done.outcome[30] == LINK_RELOAD_NOT_LIVE);
    CHECK(serval_vm_link_queued() == 2); // the Walker's and the Counter's Creates
    game_frame();
    // The Walker's create ran again: back in stroll(), a step behind (its
    // first frame waits), its field kept.
    CHECK(context_of(game.walker) >= 0 && x_of(game.walker) == walker_x);
    CHECK(serval_vm_field(game.walker, LINK_RELOAD_V2_FIELD_SEEN) == seen + 6);
    game_frame();
    CHECK(x_of(game.walker) == walker_x + 256);
}

// --- Aborts ------------------------------------------------------------------------------

// The debugger leaves without a commit (RESUME was refused meanwhile: the
// data pinned the game): the game, which ran its blob from ROM all along,
// goes on exactly as a run that never saw the reload. Errors at COMMIT (a
// hash that isn't BEGIN's, bytes missing, a plan that doesn't fit the
// blobs, a blob the loader refuses) change nothing: RELOADED says why.
static void wrong_hash(u32 n) {
    (void)n;
    mailbox()->endpoint = 0; // the debugger leaves
}

static void aborts_leave_the_game_as_it_was(void) {
    play(v1(), 15);
    Snapshot reference;
    snapshot(&reference); // a run that never saw the reload, 15 frames in
    play(v1(), 10);
    Snapshot was;
    snapshot(&was);
    Target t = TARGET(V2, v2);
    u32 hash = serval_link_fnv1a(t.blob, t.size);
    u16 pause = request(LINK_PAUSE, NULL, 0);
    u16 b = begin(t.size, hash + 1);
    data(t.blob, 0, t.size);
    plan(t.plan, t.plan_size);
    u16 bad_hash = request(LINK_RELOAD_COMMIT, NULL, 0);
    u16 go = resume();
    u16 step = request(LINK_STEP, (const u8[]){(u8)game.clock, 0}, 2);
    u16 brk = request(LINK_BREAK_SET, (const u8[]){0, 1, 0, 0, 1, 0, 0}, 7);
    // A start over: the blob in part only.
    u16 again = begin(t.size, hash);
    data(t.blob, 0, t.size / 2);
    u16 incomplete = request(LINK_RELOAD_COMMIT, NULL, 0);
    // The rest, but an object mapped past the new blob's.
    data(t.blob, t.size / 2, t.size);
    u16 past = request(LINK_RELOAD_PLAN, (const u8[]){6, 1, 0, 1, 0, 99, 0}, 7);
    u16 unfit = request(LINK_RELOAD_COMMIT, NULL, 0);
    fake.on_wait = wrong_hash;
    frame();
    CHECK(ok(pause, 0) && ok(b, 0) && error(bad_hash, LINK_MISMATCH));
    CHECK(error(go, LINK_RELOADING) && error(step, LINK_RELOADING) && error(brk, LINK_RELOADING));
    CHECK(ok(again, 0) && error(incomplete, LINK_MISMATCH));
    CHECK(ok(past, 0) && error(unfit, LINK_BAD_REQUEST));
    Reloaded done;
    CHECK(events(LINK_RELOADED) == 3 && reloaded(0, &done) && done.result == LINK_MISMATCH &&
          done.count == 0);
    CHECK(reloaded(1, &done) && done.result == LINK_MISMATCH);
    CHECK(reloaded(2, &done) && done.result == LINK_BAD_REQUEST);
    CHECK(!serval_link_attached() && !serval_link_parked());
    Snapshot now;
    snapshot(&now);
    CHECK(same(&now, &was) && running_blob() == link_reload_v1_script);
    for (u32 f = 0; f < 5; f++)
        game_frame();
    snapshot(&now);
    CHECK(same(&now, &reference));
}

// A blob the loader refuses: BAD_REQUEST at COMMIT, its warning logged,
// nothing changed; then a RELOAD_BEGIN starts over and a good one commits.
static void a_blob_the_loader_refuses(void) {
    play(v1(), 5);
    Snapshot was;
    snapshot(&was);
    Target t = TARGET(V2, v2);
    static u8 bad[1024];
    for (u32 i = 0; i < t.size; i++)
        bad[i] = t.blob[i];
    bad[6] = 0x80; // a header flag no engine knows
    u32 warnings = debug_warning_count();
    request(LINK_PAUSE, NULL, 0);
    begin(t.size, serval_link_fnv1a(bad, t.size));
    data(bad, 0, t.size);
    plan(t.plan, t.plan_size);
    u16 refused = request(LINK_RELOAD_COMMIT, NULL, 0);
    fake.on_wait = NULL;
    frame(); // the debugger leaves at the first wait
    CHECK(error(refused, LINK_BAD_REQUEST) && debug_warning_count() == warnings + 1);
    Snapshot now;
    snapshot(&now);
    CHECK(same(&now, &was) && running_blob() == link_reload_v1_script);
    // Attached again: a reload from the start.
    mailbox()->endpoint = LINK_ENDPOINT_MAGIC;
    attach();
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    CHECK(reload_ok(&r) && vm_global(R2_G_BONUS) == 7);
}

// RELOAD_BEGIN needs the game parked at a frame boundary and a blob that
// fits the scratch with the reload's room; DATA comes in order, inside the
// blob; PLAN's values that need no blob are checked as they come; with no
// reload begun, DATA, PLAN and COMMIT are refused (and RELOADED follows the
// COMMIT). RESUME after BEGIN, before any data, drops the reload.
static u16 inside_begin;
static void begin_inside(u32 n) {
    if (n == 1) {
        inside_begin = begin(64, 0);
        resume();
    } else {
        mailbox()->endpoint = 0;
    }
}

static void errors(void) {
    play(v1(), 3);
    u16 running = begin(64, 0);
    u16 data_first = request(LINK_RELOAD_DATA, (const u8[]){0, 0, 0, 0, 1}, 5);
    u16 plan_first = request(LINK_RELOAD_PLAN, (const u8[]){5, 0}, 2);
    u16 commit_first = request(LINK_RELOAD_COMMIT, NULL, 0);
    frame();
    CHECK(error(running, LINK_NOT_PARKED) && error(data_first, LINK_BAD_REQUEST));
    CHECK(error(plan_first, LINK_BAD_REQUEST) && error(commit_first, LINK_BAD_REQUEST));
    Reloaded done;
    CHECK(events(LINK_RELOADED) == 1 && reloaded(0, &done) && done.result == LINK_BAD_REQUEST &&
          done.count == 0);
    // Parked inside a frame (debug_break() from C): NOT_AT_BOUNDARY.
    fake.on_wait = begin_inside;
    fake.max_waits = 1000;
    debug_break();
    CHECK(error(inside_begin, LINK_NOT_AT_BOUNDARY) && serval_link_attached());
    fake.on_wait = NULL;
    // Parked at the boundary: what BEGIN refuses.
    Target t = TARGET(V2, v2);
    u32 hash = serval_link_fnv1a(t.blob, t.size);
    u16 pause = request(LINK_PAUSE, NULL, 0);
    u16 tiny = begin(15, 0);
    u16 huge = begin(SERVAL_LINK_SCRATCH + 1, 0);
    u16 roomless = begin(SERVAL_LINK_SCRATCH - 1024, 0); // the blob fits, the room doesn't
    u16 short_begin = request(LINK_RELOAD_BEGIN, (const u8[]){1, 0, 0}, 3);
    // Begun: RESUME before any data drops it (DATA then has no reload).
    u16 dropped = begin(t.size, hash);
    u16 go = resume();
    u16 no_reload = request(LINK_RELOAD_DATA, (const u8[]){0, 0, 0, 0, 'S'}, 5);
    frame();
    CHECK(ok(pause, 0) && error(tiny, LINK_BAD_REQUEST) && error(huge, LINK_NO_SCRATCH));
    CHECK(error(roomless, LINK_NO_SCRATCH) && error(short_begin, LINK_BAD_REQUEST));
    CHECK(ok(dropped, 0) && ok(go, 0) && error(no_reload, LINK_BAD_REQUEST));
    CHECK(!serval_link_parked());
    // DATA in order and inside the blob; PLAN's sections and values.
    pause = request(LINK_PAUSE, NULL, 0);
    u16 good = begin(t.size, hash);
    u16 gap = data(t.blob + 10, 10, 20);
    u16 first = data(t.blob, 0, t.size);
    u16 past = request(LINK_RELOAD_DATA, (const u8[]){(u8)t.size, (u8)(t.size >> 8), 0, 0, 1}, 5);
    u16 again = data(t.blob, 0, 8); // bytes received already: fine
    u16 section0 = request(LINK_RELOAD_PLAN, (const u8[]){0, 0, 0}, 3);
    u16 section7 = request(LINK_RELOAD_PLAN, (const u8[]){7, 0, 0}, 3);
    u16 context = request(LINK_RELOAD_PLAN, (const u8[]){2, 1, VM_CONTEXTS, 0}, 4);
    u16 action = request(LINK_RELOAD_PLAN, (const u8[]){2, 1, 0, LINK_RELOAD_ACTIONS}, 4);
    u16 global = request(LINK_RELOAD_PLAN, (const u8[]){3, 1, 0, 0, 0, 0, 1}, 7);
    u16 field = request(LINK_RELOAD_PLAN, (const u8[]){5, 1, 0, VM_FIELDS}, 4);
    u16 short_plan = request(LINK_RELOAD_PLAN, (const u8[]){1, 1, 0, 1, 2, 3}, 6);
    u16 trailing = request(LINK_RELOAD_PLAN, (const u8[]){5, 0, 9, 9}, 4); // ignored bytes
    u16 brk = request(LINK_BREAK_CLEAR, (const u8[]){0, 0, 0}, 3);
    u16 step = request(LINK_STEP, (const u8[]){(u8)game.clock, 0}, 2);
    fake.on_wait = NULL;
    frame(); // the debugger leaves at the first wait: the reload is dropped
    CHECK(ok(pause, 0) && ok(good, 0) && error(gap, LINK_BAD_REQUEST) && ok(first, 0));
    CHECK(error(past, LINK_BAD_REQUEST) && ok(again, 0));
    CHECK(error(section0, LINK_BAD_REQUEST) && error(section7, LINK_BAD_REQUEST));
    CHECK(error(context, LINK_BAD_REQUEST) && error(action, LINK_BAD_REQUEST));
    CHECK(error(global, LINK_BAD_REQUEST) && error(field, LINK_BAD_REQUEST));
    CHECK(error(short_plan, LINK_BAD_REQUEST) && ok(trailing, 0));
    CHECK(error(brk, LINK_RELOADING) && error(step, LINK_RELOADING));
    CHECK(!serval_link_attached() && running_blob() == link_reload_v1_script);
    // No blob: NO_VM.
    mailbox()->endpoint = LINK_ENDPOINT_MAGIC;
    attach();
    vm_unload();
    pause = request(LINK_PAUSE, NULL, 0);
    u16 no_vm = begin(t.size, hash);
    resume();
    frame();
    CHECK(error(no_vm, LINK_NO_VM));
}

// --- The scratch, after a reload --------------------------------------------------------

// After a reload the VM runs the new blob from the scratch, its only copy.
// Reloading the same blob again keeps everything where it is (every
// function unchanged); a RELOAD_BEGIN that starts over after RELOAD_DATA has
// overwritten the running blob still carries its RAM arrays by name; and a
// debugger that leaves with the running blob overwritten unloads the VM,
// with a warning, rather than run half a blob.
static void reloads_over_a_reload(void) {
    play(v1(), 5);
    Target t = TARGET(V2, v2);
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    CHECK(reload_ok(&r));
    const u8* scratch = running_blob();
    CHECK(scratch != link_reload_v1_script && scratch != link_reload_v2_script);
    for (u32 f = 0; f < 3; f++)
        game_frame();
    // AGENT_INFO: the new blob's size and hash.
    u16 info = attach();
    const u8* f = reply(info);
    CHECK(f && rd32(f + 2 + rd16(f) - 8) == t.size &&
          rd32(f + 2 + rd16(f) - 4) == serval_link_fnv1a(t.blob, t.size));
    // The same blob again: everything kept, nothing changed.
    Snapshot was, now;
    snapshot(&was);
    Target again = TARGET(SAME, v2);
    r = send_reload(&again, LINK_RELOAD_RESTART, 0xFF);
    frame();
    Reloaded done;
    CHECK(reload_ok(&r) && reloaded(1, &done) && done.result == 0 && done.count == 4);
    u32 kept = 0;
    for (u32 k = 0; k < VM_CONTEXTS; k++)
        kept += done.outcome[k] == LINK_RELOAD_KEPT;
    CHECK(kept == 4);
    snapshot(&now);
    CHECK(same(&now, &was) && running_blob() == scratch);
    // A start over, the running blob overwritten in between: its arrays
    // still follow their names.
    s32 history = serval_vm_array_cell(LINK_RELOAD_V2_CELLS_HISTORY);
    CHECK(history != 0);
    static u8 contexts[2 + 2 * VM_CONTEXTS];
    u32 n = contexts_payload(&again, LINK_RELOAD_RESTART, 0xFF, contexts);
    u32 hash = serval_link_fnv1a(t.blob, t.size);
    static u8 junk[1024];
    for (u32 i = 0; i < sizeof junk; i++)
        junk[i] = 0xEE;
    request(LINK_PAUSE, NULL, 0);
    begin(t.size + 64, hash);   // a bigger blob: the arrays are recorded further on
    data(junk, 0, t.size + 64); // over the running blob, and where they would be now
    u16 over = begin(t.size, hash);
    data(t.blob, 0, t.size);
    plan(again.plan, again.plan_size);
    request(LINK_RELOAD_PLAN, contexts, n);
    u16 commit = request(LINK_RELOAD_COMMIT, NULL, 0);
    resume();
    frame();
    CHECK(ok(over, 0) && ok(commit, 0) && reloaded(2, &done) && done.result == 0);
    CHECK(serval_vm_array_cell(LINK_RELOAD_V2_CELLS_HISTORY) == history);
    snapshot(&now);
    CHECK(same(&now, &was));
    // The debugger leaves after RELOAD_DATA overwrote the running blob: the
    // VM is unloaded, with a warning; the entities stay.
    u32 warnings = debug_warning_count();
    request(LINK_PAUSE, NULL, 0);
    begin(t.size, hash);
    data(t.blob, 0, 32);
    fake.on_wait = NULL;
    frame();
    CHECK(!serval_link_attached() && !running_blob() && vm_idle());
    CHECK(debug_warning_count() == warnings + 1 && vm_object_of(game.walker) == -1);
    CHECK(entity_alive(game.walker) && events(LINK_LOADED) == 1); // no LOADED to a debugger gone
}

// A behaviour whose entity C code destroyed behind the VM's back (no
// vm_kill) can't restart: it is halted, and the next vm_step() finds the
// stale binding as it always does.
static void a_dead_entity_halts(void) {
    play(v1(), 5);
    int counter = context_of(game.counter);
    entity_destroy(game.counter);
    Target t = TARGET(V2, v2);
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    Reloaded done;
    CHECK(reload_ok(&r) && reloaded(0, &done) && done.outcome[counter] == LINK_RELOAD_HALTED);
    CHECK(serval_vm_link_queued() == 0); // no Create queued for it
    game_frame();
    CHECK(vm_object_of(game.counter) == -1 && vm_global(R2_G_STARTS) == 3);
}

// A kept thread's object is renumbered with it: v1 -> v2k keeps the Clock
// (object 3 in v1, 2 now), then v2k -> v2 (`ticks` renumbered) restarts it,
// from its own handler, not from what v1's number names now (the Pacer).
static void a_kept_thread_follows_its_object(void) {
    play(v1(), 4);
    Target t = TARGET(V2K, v2k);
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    Reloaded done;
    CHECK(reload_ok(&r) && reloaded(0, &done) && done.outcome[game.clock] == LINK_RELOAD_KEPT);
    game_frame();
    Target back = TARGET(K2, v2);
    r = send_reload(&back, LINK_RELOAD_RESTART, 0xFF);
    frame();
    CHECK(reload_ok(&r) && reloaded(1, &done) && done.outcome[game.clock] == LINK_RELOAD_RESTARTED);
    ServalVmLinkContext clock;
    CHECK(serval_vm_link_context((u32)game.clock, &clock) && clock.state == 1);
    CHECK(clock.pc == LINK_RELOAD_V2_FN_CLOCK_ROOM_START_START);
    s32 ticks = vm_global(R2_G_TICKS);
    for (u32 f = 0; f < 3; f++)
        game_frame();
    CHECK(vm_global(R2_G_TICKS) == ticks + 2);
}

// Breakpoints: RELOAD_BEGIN clears every site (their pcs move), the game
// running its own blob again meanwhile; after RELOADED the debugger sets
// them again at the new blob's pcs, patched in place in the scratch, and
// they stop the game there.
static void go_on(u32 n) {
    (void)n;
    resume();
}

static void breakpoints_after_a_reload(void) {
    play(v1(), 3);
    u8 p[7] = {LINK_SITE_BREAKPOINT, 1, 0};
    wr32(p + 3, LINK_RELOAD_V1_FN_WALKER_STEP_START); // Walker:step's first op: every frame
    u16 set = request(LINK_BREAK_SET, p, 7);
    frame();
    CHECK(ok(set, 0) && running_blob() != link_reload_v1_script);
    Target t = TARGET(V2, v2);
    Reload r = send_reload(&t, LINK_RELOAD_RESTART, 0xFF);
    frame();
    CHECK(reload_ok(&r));
    // No site is left: none patched, none to restore (clearing every one
    // writes nothing back), and AGENT_INFO's hash is the new blob's.
    u16 clear = request(LINK_BREAK_CLEAR, (const u8[]){LINK_SITE_BREAKPOINT, 0, 0}, 3);
    u16 info = attach();
    const u8* b = running_blob();
    u32 differ = 0;
    for (u32 i = 0; i < t.size; i++)
        differ += b[i] != t.blob[i];
    const u8* f = reply(info);
    CHECK(ok(clear, 0) && differ == 0);
    CHECK(f && rd32(f + 2 + rd16(f) - 4) == serval_link_fnv1a(t.blob, t.size));
    u32 stops = events(LINK_STOPPED);
    game_frame();
    CHECK(events(LINK_STOPPED) == stops);
    wr32(p + 3, LINK_RELOAD_V2_FN_WALKER_STEP_START);
    set = request(LINK_BREAK_SET, p, 7);
    frame();
    CHECK(ok(set, 0) && running_blob() == b && b[LINK_RELOAD_V2_FN_WALKER_STEP_START] == VM_OP_BRK);
    fake.on_wait = go_on;
    fake.max_waits = 1000;
    game_frame();
    const u8* stopped = event(LINK_STOPPED, stops);
    CHECK(stopped && stopped[6] == LINK_STOP_BREAKPOINT &&
          rd32(stopped + 8) == LINK_RELOAD_V2_FN_WALKER_STEP_START);
    CHECK(!serval_link_parked());
}

// Last: the agent left dormant for the suites after this one.
static void leave_dormant(void) {
    start(false);
    CHECK(!serval_link_started() && !serval_link_attached());
}

TEST_SUITE(link_reload_tests, "link_reload",
           {"relocate, restart, detach", reload_relocates_restarts_and_detaches},
           {"kept contexts match an undisturbed run", kept_contexts_match_an_undisturbed_run},
           {"halt instead", halt_instead}, {"actions as asked", actions_as_asked},
           {"aborts leave the game as it was", aborts_leave_the_game_as_it_was},
           {"a blob the loader refuses", a_blob_the_loader_refuses}, {"errors", errors},
           {"reloads over a reload", reloads_over_a_reload},
           {"a kept thread follows its object", a_kept_thread_follows_its_object},
           {"a dead entity halts", a_dead_entity_halts},
           {"breakpoints after a reload", breakpoints_after_a_reload},
           {"left dormant", leave_dormant});

#else

static void no_agent(void) {
    CHECK(true);
}

TEST_SUITE(link_reload_tests, "link_reload", {"no agent in this build", no_agent});

#endif // SERVAL_LINK_AGENT
