// The debug link's hot reload on the GBA (docs/debug-link.md#hot-reload-as-built):
// what a reload costs the game, measured in mGBA. The test ROM has no
// endpoint, so the agent is dormant; this calls the agent's VM part the way
// the agent does when a debugger's RELOAD_* frames arrive, with a blob of
// fireflies' size and shape (1,235 bytes, six objects, nine globals with
// initial values) and fireflies' nine waiting behaviours (seven instances,
// two threads), reloaded onto itself with a plan that keeps everything: the
// cost of RELOAD_BEGIN, the blob and the plan, then of RELOAD_COMMIT (the
// hash, the checks, the globals and the RAM arrays' pool rebuilt, the
// contexts moved, the bindings recorded again). In debug builds with the
// agent; release builds have none of it.

#include "../../src/core/link_internal.h"
#include "../test.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/text.h"
#include "serval/vm.h"

#include <tonc.h>

#ifdef SERVAL_LINK_AGENT

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

// The blob: six objects whose Create waits a frame, forever (PUSH8 1; WAIT;
// JMP back), the first three components C_POS (instances), the others none
// (threads); nine globals with initial values; NOPs to 1,235 bytes.
#define BLOB_SIZE 1235u
#define CODE (VM_HEADER_SIZE + 6 * VM_OBJECT_SIZE + 9 * 4)
#define HANDLER(k) (CODE + 6 * (k))
#define LE32(v) (u8)(v), (u8)((v) >> 8), (u8)((v) >> 16), (u8)((v) >> 24)
#define OBJECT(mask, k)                                                                            \
    LE32(mask), 0, 0, 0, 0, LE32(HANDLER(k)), LE32(0), LE32(0), LE32(0), LE32(0), LE32(0)
#define WAITS VM_OP_PUSH8, 1, VM_OP_WAIT, VM_OP_JMP, 0xFA, 0xFF

static const u8 blob[BLOB_SIZE] = {'S',
                                   'V',
                                   'M',
                                   'B',
                                   VM_FORMAT_VERSION,
                                   VM_CELL_BYTES,
                                   VM_FLAG_GLOBAL_VALUES,
                                   0,
                                   6,
                                   0,
                                   0,
                                   0,
                                   9,
                                   0,
                                   0,
                                   0,
                                   OBJECT(C_POS, 0),
                                   OBJECT(C_POS, 1),
                                   OBJECT(C_POS, 2),
                                   OBJECT(0, 3),
                                   OBJECT(0, 4),
                                   OBJECT(0, 5),
                                   LE32(10),
                                   LE32(60),
                                   LE32(7),
                                   LE32(1),
                                   LE32(0),
                                   LE32(40),
                                   LE32(90),
                                   LE32(256),
                                   LE32(3),
                                   [HANDLER(0)] = WAITS,
                                   WAITS,
                                   WAITS,
                                   WAITS,
                                   WAITS,
                                   WAITS};

static u32 request(u32 type, const u8* payload, u32 n) {
    u8 reply[16];
    LinkReader r = {.p = payload, .n = n, .at = 0, .ok = true};
    LinkWriter w = {.p = reply, .n = 0, .room = sizeof reply, .full = false};
    return serval_link_reload_request(type, &r, &w);
}

static void put32(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void reload_cost(void) {
    CHECK(vm_load(blob, BLOB_SIZE));
    Entity e[7];
    for (u32 i = 0; i < 7; i++) {
        e[i] = entity_create(C_POS);
        vm_attach(e[i], (u16)(i % 3));
    }
    CHECK(vm_start(3, VM_EV_CREATE) >= 0 && vm_start(4, VM_EV_CREATE) >= 0);
    vm_step();
    vm_events();
    CHECK(serval_vm_link_contexts() == 9);
    u32 pcs[VM_CONTEXTS];
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        ServalVmLinkContext c;
        pcs[k] = serval_vm_link_context(k, &c) ? c.pc : 0;
    }
    // The plan: every function unchanged, every context relocated, the
    // globals and objects as they are.
    u8 plan[3 + 12 * 6], contexts[2 + 2 * 9], globals[3 + 4 * 9], objects[3 + 4 * 6];
    plan[0] = 1, plan[1] = 6, plan[2] = 0;
    for (u32 k = 0; k < 6; k++) {
        put32(plan + 3 + 12 * k, HANDLER(k));
        put32(plan + 7 + 12 * k, HANDLER(k) + 6);
        put32(plan + 11 + 12 * k, HANDLER(k));
    }
    contexts[0] = 2, contexts[1] = 9;
    for (u32 k = 0; k < 9; k++)
        contexts[2 + 2 * k] = (u8)k, contexts[3 + 2 * k] = LINK_RELOAD_RELOCATE;
    globals[0] = 3, globals[1] = 9, globals[2] = 0;
    for (u32 n = 0; n < 9; n++)
        globals[3 + 4 * n] = (u8)n, globals[4 + 4 * n] = 0, globals[5 + 4 * n] = (u8)n,
                        globals[6 + 4 * n] = 0;
    objects[0] = 6, objects[1] = 6, objects[2] = 0;
    for (u32 n = 0; n < 6; n++)
        objects[3 + 4 * n] = (u8)n, objects[4 + 4 * n] = 0, objects[5 + 4 * n] = (u8)n,
                        objects[6 + 4 * n] = 0;
    vm_set_global(0, 12345);

    u32 t0 = cycles();
    u8 p[8];
    put32(p, BLOB_SIZE);
    put32(p + 4, serval_link_fnv1a(blob, BLOB_SIZE));
    u32 begun = request(LINK_RELOAD_BEGIN, p, 8);
    // In chunks of 128 bytes here (a debugger sends up to 1,014 a frame).
    u8 chunk[4 + 128];
    u32 sent = 0;
    for (u32 at = 0; at < BLOB_SIZE; at += 128) {
        u32 n = BLOB_SIZE - at < 128 ? BLOB_SIZE - at : 128;
        put32(chunk, at);
        for (u32 i = 0; i < n; i++)
            chunk[4 + i] = blob[at + i];
        sent |= request(LINK_RELOAD_DATA, chunk, 4 + n);
    }
    sent |= request(LINK_RELOAD_PLAN, plan, sizeof plan);
    sent |= request(LINK_RELOAD_PLAN, contexts, sizeof contexts);
    sent |= request(LINK_RELOAD_PLAN, globals, sizeof globals);
    sent |= request(LINK_RELOAD_PLAN, objects, sizeof objects);
    u32 t1 = cycles();
    u32 committed = request(LINK_RELOAD_COMMIT, NULL, 0);
    u32 t2 = cycles();
    u32 hash = serval_link_fnv1a(blob, BLOB_SIZE);
    u32 t3 = cycles();
    (void)hash;
    CHECK(begun == 0 && sent == 0 && committed == 0);
    // Everything kept where it was, from the scratch now, the globals too.
    u32 size, moved = 0;
    CHECK(serval_vm_link_blob(&size) != blob && size == BLOB_SIZE);
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        ServalVmLinkContext c;
        moved += (serval_vm_link_context(k, &c) ? c.pc : 0) != pcs[k];
    }
    CHECK(moved == 0 && serval_vm_link_contexts() == 9 && vm_global(0) == 12345);
    CHECK(vm_object_of(e[0]) == 0 && vm_object_of(e[6]) == 0);
    u32 warnings = debug_warning_count();
    vm_step();
    vm_events();
    CHECK(serval_vm_link_contexts() == 9 && debug_warning_count() == warnings);
    u32 transfer = t1 - t0, commit = t2 - t1, fnv = t3 - t2;
    debug_log(text_format("link: reload, %u-byte blob, 9 contexts kept: BEGIN, the blob (128-byte "
                          "chunks) and the plan %u cycles",
                          BLOB_SIZE, transfer));
    debug_log(text_format("link: reload: COMMIT %u cycles (%u.%u%% of a frame; the hash %u)",
                          commit, commit * 100 / 280896u, commit * 1000 / 280896u % 10, fnv));
    CHECK_TIMING(commit < 280896u); // under a frame
    vm_unload();
    for (u32 i = 0; i < 7; i++)
        entity_destroy(e[i]);
}

TEST_SUITE(gba_link_reload_tests, "gba_link_reload", {"a reload's cost", reload_cost});

#else

static void no_agent(void) {
    CHECK(true);
}

TEST_SUITE(gba_link_reload_tests, "gba_link_reload", {"no agent in this build", no_agent});

#endif // SERVAL_LINK_AGENT
