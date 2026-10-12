// The debug link's agent, its hot reload (step E9 of docs/debug-link.md):
// RELOAD_BEGIN, RELOAD_DATA, RELOAD_PLAN, RELOAD_COMMIT and the RELOADED
// event, with the plan's sections (docs/debug-link.md#the-plan). Part of the
// agent's VM part, reached only from vm_link.c, so a game that doesn't link
// the VM links none of it; in debug builds with the agent only.
//
// The debugger, parked at a frame boundary, sends the new blob and a plan it
// computed from both blobs and both .svdbg maps: which functions are
// unchanged (their old and new ranges), what to do with each live context,
// and the globals, RAM arrays, instance fields and objects old to new by
// name. COMMIT checks the bytes and the plan, then applies it at once: this
// file sets the globals and the RAM arrays' pool, vm.c's
// serval_vm_link_reload() the rest (the contexts kept at their new offsets,
// restarted or halted; the bindings renumbered; the blob switched).
//
// The scratch (vm_link.c's) while a reload is under way:
//   [0, size)          the new blob: RELOAD_DATA's bytes, in order
//   [work, ...)        the running blob's RAM arrays (first cell and length
//                      each, recorded at RELOAD_BEGIN, since an earlier
//                      reload's blob lives only here and the new one
//                      overwrites it), then RELOAD_PLAN's sections as they
//                      came
//   [temps, end)       the commit's copies: the new globals and the new RAM
//                      arrays' pool, TEMP_CELLS cells
// `work` is past the new blob and past the running one when that one is in
// the scratch, so only RELOAD_DATA writes over the running blob.

#include "link_internal.h"
#include "serval/vm.h"
#include "vm_internal.h"
#include "warn.h"

#ifdef SERVAL_LINK_AGENT

#ifndef SERVAL_LINK_SCRATCH
#define SERVAL_LINK_SCRATCH 32768
#endif

// The commit's copies: the globals and the RAM arrays' pool, as they will be.
#define TEMP_CELLS (VM_GLOBALS + VM_ARRAY_CELLS)
#define TEMPS_AT (((u32)SERVAL_LINK_SCRATCH - TEMP_CELLS * 4u) & ~3u)
// An old array recorded at RELOAD_BEGIN: its first cell and length (u16 each;
// 0xFFFF, 0xFFFF: a ROM array).
#define ARRAY_RECORD 4u
#define ROM_ARRAY 0xFFFFu

typedef struct {
    bool begun;       // RELOAD_BEGIN taken, no successful RELOAD_COMMIT since
    bool pinned;      // a RELOAD_DATA came: RESUME and STEP wait for a commit
    bool overwritten; // the VM's own blob (an earlier reload's, in the scratch) is written over
    bool report;      // RELOADED to send after the current reply
    u8 result;        // its result: 0, or the commit's error code
    u32 size, hash;   // RELOAD_BEGIN's: the new blob's
    u32 received;     // its bytes received, from 0
    u32 old_size;     // the running blob's size when it lives in the scratch, else 0
    u32 work;         // the work area: the old arrays, then the plan
    u32 arrays;       // old arrays recorded there
    u32 plan_end;     // where the plan ends
} Reload;
SERVAL_EWRAM_BSS static Reload rl;
SERVAL_EWRAM_BSS static u8 outcomes[VM_CONTEXTS]; // the commit's, for RELOADED
// The game's own blob while the VM runs from a copy of it in the scratch
// (breakpoints): RELOAD_BEGIN goes back to it. NULL while the scratch holds
// the VM's only copy (after a reload), or the VM runs no copy.
SERVAL_EWRAM_BSS static const u8* origin;

static u32 le16(const u8* p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

static u32 le32(const u8* p) {
    return le16(p) | le16(p + 2) << 16;
}

static void put16(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u32 fail(u32 code, const char* text) {
    serval_link_error_text = text;
    return code;
}

#define SHORT() fail(LINK_BAD_REQUEST, "the payload is too short")

static u32 align4(u32 n) {
    return (n + 3u) & ~3u;
}

// A valid blob's array table, and where its code may start (past the
// tables, the globals' initial values included).
static u32 array_table(const u8* b) {
    return VM_HEADER_SIZE + le16(b + 8) * VM_OBJECT_SIZE + le16(b + 10) * 4u;
}

static u32 tables_end(const u8* b) {
    u32 values = le16(b + 6) & VM_FLAG_GLOBAL_VALUES ? le16(b + 12) * VM_CELL_BYTES : 0;
    return array_table(b) + le16(b + 14) * VM_ARRAY_RECORD_SIZE + values;
}

// --- The plan ----------------------------------------------------------------------
//
// Each RELOAD_PLAN frame taken is kept in the work area as it came: section
// u8, count u16 (the frame's u8 counts widened), then its entries.

enum {
    SECTION_FUNCTIONS = 1,
    SECTION_CONTEXTS = 2,
    SECTION_GLOBALS = 3,
    SECTION_ARRAYS = 4,
    SECTION_FIELDS = 5,
    SECTION_OBJECTS = 6,
    SECTIONS = 7,
};

// Each section's entry size, and whether its count is a u8 (else a u16).
static const u8 entry_size[SECTIONS] = {0, 12, 2, 4, 4, 2, 4};
#define U8_COUNTS (1u << SECTION_CONTEXTS | 1u << SECTION_FIELDS)

// Walks the plan's entries of one section: entry() gives the next, or NULL.
typedef struct {
    const u8* scratch;
    u32 section;
    u32 at;   // the next frame's offset
    u32 left; // entries left in the current frame
    const u8* p;
} Entries;

static Entries entries(const u8* scratch, u32 section) {
    return (Entries){
        .scratch = scratch, .section = section, .at = rl.work + rl.arrays * ARRAY_RECORD};
}

static const u8* entry(Entries* it) {
    while (!it->left) {
        if (it->at >= rl.plan_end)
            return NULL;
        const u8* frame = it->scratch + it->at;
        u32 section = frame[0], count = le16(frame + 1);
        it->at += 3 + count * entry_size[section];
        if (section == it->section) {
            it->left = count;
            it->p = frame + 3;
        }
    }
    const u8* e = it->p;
    it->p += entry_size[it->section];
    it->left--;
    return e;
}

// The plan's maps, for vm.c (ServalVmLinkReload): an old pc's new one, in
// the first unchanged function that holds it (0: none); an old object's new
// number (the last pair for it; -1: gone, or not listed).
static u32 relocate(const void* plan, u32 pc) {
    Entries it = entries(plan, SECTION_FUNCTIONS);
    for (const u8* e; (e = entry(&it));) {
        u32 old_start = le32(e), old_end = le32(e + 4);
        if (pc >= old_start && pc < old_end)
            return le32(e + 8) + (pc - old_start);
    }
    return 0;
}

static int object_of(const void* plan, u32 old) {
    if (old >= serval_vm_link_objects())
        return -1;
    int found = -1;
    Entries it = entries(plan, SECTION_OBJECTS);
    for (const u8* e; (e = entry(&it));) {
        if (le16(e) == old)
            found = le16(e + 2) == 0xFFFFu ? -1 : (int)le16(e + 2);
    }
    return found;
}

// RELOAD_PLAN: section u8, then its count and entries. The values that need
// no blob are checked here, the rest at RELOAD_COMMIT.
static u32 plan(LinkReader* r, u8* scratch) {
    u32 section = serval_link_u8(r);
    if (!r->ok)
        return SHORT();
    if (section == 0 || section >= SECTIONS)
        return fail(LINK_BAD_REQUEST, "a plan's sections are 1 to 6");
    u32 count = U8_COUNTS >> section & 1u ? serval_link_u8(r) : serval_link_u16(r);
    u32 bytes = count * entry_size[section];
    if (!r->ok || r->n - r->at < bytes)
        return SHORT();
    const u8* e = r->p + r->at;
    for (u32 i = 0; i < count; i++, e += entry_size[section]) {
        bool bad = false;
        if (section == SECTION_CONTEXTS)
            bad = e[0] >= VM_CONTEXTS || e[1] >= LINK_RELOAD_ACTIONS;
        else if (section == SECTION_GLOBALS)
            bad = le16(e) >= VM_GLOBALS || le16(e + 2) >= VM_GLOBALS;
        else if (section == SECTION_FIELDS)
            bad = e[0] >= VM_FIELDS || e[1] >= VM_FIELDS;
        if (bad)
            return fail(LINK_BAD_REQUEST, section == SECTION_CONTEXTS
                                              ? "a context past VM_CONTEXTS, or an action past 3"
                                          : section == SECTION_GLOBALS ? "a global past VM_GLOBALS"
                                                                       : "a field past VM_FIELDS");
    }
    if (rl.plan_end + 3 + bytes > TEMPS_AT)
        return fail(LINK_NO_SCRATCH, "the plan doesn't fit the scratch beside the new blob");
    u8* to = scratch + rl.plan_end;
    to[0] = (u8)section;
    put16(to + 1, count);
    for (u32 i = 0; i < bytes; i++)
        to[3 + i] = r->p[r->at + i];
    rl.plan_end += 3 + bytes;
    return 0;
}

// The plan against both blobs (b, the new one, passed vm_load's checks):
// the unchanged functions' new ranges inside its code, the arrays and
// objects inside both blobs' tables.
static bool plan_fits(const u8* scratch, const u8* b) {
    u32 code = tables_end(b);
    Entries it = entries(scratch, SECTION_FUNCTIONS);
    for (const u8* e; (e = entry(&it));) {
        u32 old_start = le32(e), old_end = le32(e + 4), new_start = le32(e + 8);
        if (old_start >= old_end || new_start < code || new_start > rl.size ||
            old_end - old_start > rl.size - new_start)
            return false;
    }
    it = entries(scratch, SECTION_ARRAYS);
    for (const u8* e; (e = entry(&it));) {
        if (le16(e) >= rl.arrays || le16(e + 2) >= le16(b + 14))
            return false;
    }
    it = entries(scratch, SECTION_OBJECTS);
    for (const u8* e; (e = entry(&it));) {
        u32 to = le16(e + 2);
        if (le16(e) >= serval_vm_link_objects() || (to >= le16(b + 8) && to != 0xFFFFu))
            return false;
    }
    return true;
}

// --- The messages ------------------------------------------------------------------

// Records the running blob's RAM arrays at `at`: a reload's arrays follow
// them by name, and the blob may be overwritten before the commit.
static void record_arrays(u8* scratch, u32 at, const u8* b) {
    u32 arrays = le16(b + 14);
    const u8* table = b + array_table(b);
    for (u32 n = 0; n < arrays; n++) {
        const u8* record = table + n * VM_ARRAY_RECORD_SIZE;
        bool ram = record[2] == VM_ARRAY_RAM;
        put16(scratch + at + n * ARRAY_RECORD, ram ? le32(record + 4) : ROM_ARRAY);
        put16(scratch + at + n * ARRAY_RECORD + 2, ram ? le16(record) : ROM_ARRAY);
    }
    rl.arrays = arrays;
}

// Moves n bytes within the scratch, the ranges possibly overlapping.
static void move(u8* scratch, u32 to, u32 from, u32 n) {
    if (to < from) {
        for (u32 i = 0; i < n; i++)
            scratch[to + i] = scratch[from + i];
    } else {
        for (u32 i = n; i-- > 0;)
            scratch[to + i] = scratch[from + i];
    }
}

// RELOAD_BEGIN: size u32, hash u32 (the new blob's). link.c checked that the
// game is parked at a frame boundary, vm_link.c that a blob is loaded. Every
// site goes (their pcs are moving, and the scratch is about to be written),
// and the VM goes back to the game's own blob if it ran a copy of it.
static u32 begin(LinkReader* r, u8* scratch) {
    u32 size = serval_link_u32(r);
    u32 hash = serval_link_u32(r);
    if (!r->ok)
        return SHORT();
    if (!scratch)
        return fail(LINK_NO_SCRATCH, "no scratch: SERVAL_LINK_SCRATCH is 0");
    if (size < VM_HEADER_SIZE)
        return fail(LINK_BAD_REQUEST, "a blob is at least its 16-byte header");
    u32 running;
    const u8* b = serval_vm_link_blob(&running);
    // The running blob: the game's (in ROM, or wherever the game keeps it),
    // a copy of it in the scratch, or an earlier reload's, only there.
    bool copy = b == scratch && origin;
    u32 old_size = rl.overwritten ? rl.old_size : b == scratch && !origin ? running : 0;
    u32 arrays = rl.overwritten ? rl.arrays : le16(b + 14);
    u32 work = align4(size > old_size ? size : old_size);
    if (size > SERVAL_LINK_SCRATCH || work + arrays * ARRAY_RECORD > TEMPS_AT ||
        TEMPS_AT > SERVAL_LINK_SCRATCH)
        return fail(LINK_NO_SCRATCH, "the blob and the reload's working room don't fit the scratch "
                                     "(SERVAL_LINK_SCRATCH)");
    serval_vm_link_unpatch();
    if (copy) {
        serval_vm_link_use_copy(origin); // byte for byte the copy, now unpatched
        b = origin;
    }
    if (rl.overwritten)
        move(scratch, work, rl.work, arrays * ARRAY_RECORD); // an earlier BEGIN's record
    else
        record_arrays(scratch, work, b);
    rl.begun = true;
    rl.size = size;
    rl.hash = hash;
    rl.received = 0;
    rl.old_size = old_size;
    rl.work = work;
    rl.plan_end = work + arrays * ARRAY_RECORD;
    return 0;
}

// RELOAD_DATA: offset u32, then bytes of the new blob, in order (a chunk may
// repeat bytes already received, not skip any).
static u32 data(LinkReader* r, u8* scratch) {
    u32 offset = serval_link_u32(r);
    if (!r->ok)
        return SHORT();
    u32 n = r->n - r->at;
    if (offset > rl.received || n > rl.size - offset)
        return fail(LINK_BAD_REQUEST, "RELOAD_DATA's chunks come in order, inside the blob");
    u32 running;
    if (serval_vm_link_blob(&running) == scratch)
        rl.overwritten = true; // the VM's only copy of its blob: from now on it is gone
    for (u32 i = 0; i < n; i++)
        scratch[offset + i] = r->p[r->at + i];
    if (offset + n > rl.received)
        rl.received = offset + n;
    rl.pinned = true;
    return 0;
}

// The new globals (the new blob's initial values, then the plan's copies of
// the values from before) and RAM arrays' pool (zero, then the plan's
// copies, as many cells as the shorter array has), into the temps.
static void new_values(u8* scratch, const u8* b, s32* globals, s32* cells) {
    bool initial = le16(b + 6) & VM_FLAG_GLOBAL_VALUES;
    u32 count = le16(b + 12);
    const u8* values = b + array_table(b) + le16(b + 14) * VM_ARRAY_RECORD_SIZE;
    for (u32 n = 0; n < VM_GLOBALS; n++)
        globals[n] = initial && n < count ? (s32)le32(values + n * VM_CELL_BYTES) : 0;
    Entries it = entries(scratch, SECTION_GLOBALS);
    for (const u8* e; (e = entry(&it));)
        globals[le16(e + 2)] = vm_global((u16)le16(e));
    for (u32 n = 0; n < VM_ARRAY_CELLS; n++)
        cells[n] = 0;
    it = entries(scratch, SECTION_ARRAYS);
    for (const u8* e; (e = entry(&it));) {
        const u8* old = scratch + rl.work + le16(e) * ARRAY_RECORD;
        const u8* now = b + array_table(b) + le16(e + 2) * VM_ARRAY_RECORD_SIZE;
        if (le16(old + 2) == ROM_ARRAY || now[2] != VM_ARRAY_RAM)
            continue;
        u32 from = le16(old), to = le32(now + 4), n = le16(old + 2);
        if (le16(now) < n)
            n = le16(now);
        for (u32 i = 0; i < n; i++)
            cells[to + i] = serval_vm_array_cell(from + i);
    }
}

// RELOAD_COMMIT: every byte arrived, the hash is BEGIN's, the blob passes
// vm_load's checks and the plan fits both blobs; then it is applied, all at
// once. Any error leaves everything as it was, the reload still under way.
static u32 commit(u8* scratch) {
    if (rl.received < rl.size)
        return fail(LINK_MISMATCH, "the new blob is incomplete (RELOAD_DATA missing)");
    if (serval_link_fnv1a(scratch, rl.size) != rl.hash)
        return fail(LINK_MISMATCH, "the new blob's hash isn't RELOAD_BEGIN's");
    const u8* b = scratch;
    if (!serval_vm_link_valid(b, rl.size))
        return fail(LINK_BAD_REQUEST, "the new blob fails the loader's checks (its warning is "
                                      "in the log)");
    if (!plan_fits(scratch, b))
        return fail(LINK_BAD_REQUEST, "the plan doesn't fit the blobs (a function's range, an "
                                      "array or an object out of range)");
    u8 actions[VM_CONTEXTS], field_from[VM_FIELDS];
    for (u32 k = 0; k < VM_CONTEXTS; k++)
        actions[k] = LINK_RELOAD_HALT; // a live context the plan doesn't list
    Entries it = entries(scratch, SECTION_CONTEXTS);
    for (const u8* e; (e = entry(&it));)
        actions[e[0]] = e[1];
    for (u32 n = 0; n < VM_FIELDS; n++)
        field_from[n] = 0xFF;
    it = entries(scratch, SECTION_FIELDS);
    for (const u8* e; (e = entry(&it));)
        field_from[e[1]] = e[0];
    s32* globals = (s32*)(void*)(scratch + TEMPS_AT);
    s32* cells = globals + VM_GLOBALS;
    new_values(scratch, b, globals, cells);
    serval_vm_link_reload(&(ServalVmLinkReload){.blob = b,
                                                .size = rl.size,
                                                .actions = actions,
                                                .field_from = field_from,
                                                .plan = scratch,
                                                .relocate = relocate,
                                                .object = object_of,
                                                .outcomes = outcomes});
    for (u32 n = 0; n < VM_GLOBALS; n++)
        vm_set_global((u16)n, globals[n]);
    for (u32 n = 0; n < VM_ARRAY_CELLS; n++)
        serval_vm_link_set_array_cell(n, cells[n]);
    origin = NULL; // the scratch is the VM's only copy of its blob now
    rl = (Reload){0};
    return 0;
}

u32 serval_link_reload_request(u32 type, LinkReader* r, LinkWriter* w) {
    (void)w;
    u8* scratch = serval_vm_link_scratch();
    if (type == LINK_RELOAD_BEGIN)
        return begin(r, scratch);
    u32 code = 0;
    if (!rl.begun)
        code = fail(LINK_BAD_REQUEST, "no reload is under way: RELOAD_BEGIN first");
    else if (type == LINK_RELOAD_DATA)
        code = data(r, scratch);
    else if (type == LINK_RELOAD_PLAN)
        code = plan(r, scratch);
    else
        code = commit(scratch);
    if (type == LINK_RELOAD_COMMIT) { // RELOADED follows every commit, done or not
        rl.report = true;
        rl.result = (u8)code;
    }
    return code;
}

typedef struct {
    u32 result;
} Reloaded;

static void fill_reloaded(LinkWriter* w, const void* arg) {
    const Reloaded* reloaded = arg;
    serval_link_put8(w, reloaded->result);
    u32 count = 0;
    for (u32 k = 0; reloaded->result == 0 && k < VM_CONTEXTS; k++)
        count += outcomes[k] != LINK_RELOAD_NOT_LIVE;
    serval_link_put8(w, count);
    for (u32 k = 0; reloaded->result == 0 && k < VM_CONTEXTS; k++) {
        if (outcomes[k] != LINK_RELOAD_NOT_LIVE) {
            serval_link_put8(w, k);
            serval_link_put8(w, outcomes[k]);
        }
    }
}

void serval_link_reload_replied(void) {
    if (!rl.report)
        return;
    rl.report = false;
    Reloaded reloaded = {rl.result};
    serval_link_event(LINK_RELOADED, fill_reloaded, &reloaded);
}

bool serval_link_reload_resuming(bool go) {
    if (rl.pinned)
        return false;
    if (go)
        rl = (Reload){0}; // a reload begun, no data yet: dropped
    return true;
}

bool serval_link_reload_busy(void) {
    return rl.begun;
}

void serval_link_reload_copied(const u8* game_blob) {
    origin = game_blob;
}

void serval_link_reload_loaded(void) {
    origin = NULL; // the game runs a blob of its own
    rl = (Reload){0};
}

void serval_link_reload_detached(void) {
    bool overwritten = rl.overwritten;
    rl = (Reload){0};
    if (overwritten) {
        // Half a blob can't run: the VM's own was overwritten by one never
        // committed.
        SERVAL_WARN("debug link: the debugger left during a hot reload that had overwritten the "
                    "running scripts; they are unloaded (every context halted)");
        vm_unload();
    }
}

#endif // SERVAL_LINK_AGENT
