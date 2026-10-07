// The bytecode VM (vm.h; the format and every rule are in docs/vm.md): the
// blob loader, the interpreter, the scheduler (contexts, waits, behaviours and
// reactions, the event queue) and the bridge to entities and engine calls.
// Portable: the engine calls only the GBA build has (sound, music, text,
// buttons, brightness) go through serval_vm_platform_call (vm_internal.h).
//
// vm_step() runs, in this order: the resume pass, Animation End (queued for
// animations finished since the last check), a drain of the event queue, the
// Step reactions, and a second drain. A binding's Create-pending flag keeps an
// entity's Step reaction from running before its Create was drained.
// vm_events() drains the queue, then runs the collision pass over
// vm_collide's pairs, draining after each overlap it finds.
//
// Behaviours (Create, Room Start, vm_start threads) may wait; reactions (every
// other event) run to completion. A reaction for an entity whose behaviour
// waits runs on top of it, in the same context: the behaviour's registers are
// saved in `below`, the reaction's activation starts at the behaviour's stack
// top, and when the reaction ends (or faults) the registers come back. One
// save slot is enough: reactions never wait, and events are only dispatched
// while no script runs (vm_step and vm_events refuse to nest).
//
// The blob is read in place, byte by byte (little-endian, no alignment), and
// every read is checked against its size first: a bad jump or a handler that
// runs off the end warns and halts that script, never reads past the blob.

#include "serval/vm.h"

#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "serval/random.h"
#include "sprite_internal.h"
#include "vm_internal.h"
#include "warn.h"

// --- State -------------------------------------------------------------------

enum {
    CTX_FREE,        // not in use
    CTX_READY,       // started by vm_start(): runs in the next resume pass
    CTX_RUNNING,     // executing
    CTX_WAIT_FRAMES, // WAIT: resumes when wait_frames counts down to 0
    CTX_WAIT_ANIM,   // WAIT_ANIM: resumes once anim_finished(self)
    CTX_WAIT_MOVE,   // WAIT_MOVE: resumes once self has no C_PATH
};

// What a reaction on top of a waiting behaviour saves and restores.
typedef struct {
    u32 pc;          // blob offset of the next opcode
    Entity other;    // the running handler's other entity, else ENTITY_NONE
    u16 wait_frames; // CTX_WAIT_FRAMES: resume passes left
    u8 state;        // CTX_*
    u8 event;        // the VM_EV_* handler it runs
    u8 sp, fp, cp;   // stack top, frame start, call depth
    u8 base;         // the activation's floor: 0, or the sp of the behaviour beneath
} Registers;

typedef struct {
    Registers r;
    Registers below;       // the waiting behaviour's, while `stacked`
    Entity self;           // bound entity, or ENTITY_NONE for a thread (vm_start)
    bool stacked;          // a reaction runs on top of the behaviour in `below`
    s32 stack[VM_STACK];   // operands, and the locals of every frame
    u32 call_pc[VM_CALLS]; // CALL's return points
    u8 call_fp[VM_CALLS];  // and the callers' frames
} Context;

typedef struct {
    Entity e, other;
    u8 event;
} QueuedEvent;

SERVAL_EWRAM_BSS static Context contexts[VM_CONTEXTS];
SERVAL_EWRAM_BSS static s32 globals[VM_GLOBALS];
SERVAL_EWRAM_BSS static s32 array_cells[VM_ARRAY_CELLS]; // the RAM arrays' pool
SERVAL_EWRAM_BSS static QueuedEvent queue[VM_EVENT_QUEUE];
// Bindings, by entity slot: the attached entity's handle (ENTITY_NONE: none),
// its object, its live context + 1 (0: none), its BIND_* flags and its
// instance fields.
SERVAL_EWRAM_BSS static Entity bound[MAX_ENT];
SERVAL_EWRAM_BSS static u16 bound_object[MAX_ENT];
SERVAL_EWRAM_BSS static u8 bound_context[MAX_ENT];
SERVAL_EWRAM_BSS static u8 bound_flags[MAX_ENT];
SERVAL_EWRAM_BSS static s32 fields[MAX_ENT][VM_FIELDS];

enum {
    BIND_CREATE_PENDING = 1, // its Create is queued: no Step reaction yet
    BIND_ANIM_DONE = 2,      // anim_finished() at the latest Animation End check
};

// Bytes per element of each kind of array (docs/vm.md "Array table"); RAM
// arrays are cells of the pool.
static const u8 element_size[VM_ARRAY_KIND_COUNT] = {
    [VM_ARRAY_RAM] = 0, [VM_ARRAY_S8] = 1,  [VM_ARRAY_U8] = 1,
    [VM_ARRAY_S16] = 2, [VM_ARRAY_U16] = 2, [VM_ARRAY_S32] = 4};

static const u8* blob; // the loaded blob; NULL: none
static u32 blob_size;
static u32 object_count, string_count, global_count, array_count;
static u32 array_table; // blob offset of the array table
static u32 ram_layout;  // fingerprint of the RAM arrays' layout, for vm_reload
static VmBindings bindings;
static u32 queue_head, queue_count;
static u32 ops_frame; // vm_ops_this_frame()
static bool in_phase; // inside vm_step(), vm_events() or vm_kill()'s Destroy logic

#define BEHAVIOUR_EVENTS (1u << VM_EV_CREATE | 1u << VM_EV_ROOM_START)

_Static_assert(VM_CONTEXTS < 255, "bound_context holds a context index + 1 in a u8");
_Static_assert(MAX_ENT <= 256, "an entity slot fits a handle's low byte");
_Static_assert(VM_GLOBALS >= 256, "LDG and STG take any u8 global index");
_Static_assert(VM_STACK <= 255, "sp, fp and base are u8");
_Static_assert(VM_CALLS <= 255, "cp is a u8");

// --- Warnings ----------------------------------------------------------------

#ifdef SERVAL_DEBUG
// Each kind of problem is reported once per loaded blob (vm_load, vm_reload
// and vm_unload start over), not every frame a faulty script runs (docs/vm.md).
enum {
    WARN_ESCAPED,
    WARN_UNKNOWN_OP,
    WARN_OVERFLOW,
    WARN_UNDERFLOW,
    WARN_CALL_DEPTH,
    WARN_DIV_ZERO,
    WARN_BUDGET,
    WARN_REACTION_BUDGET,
    WARN_LOCAL,
    WARN_ARRAY,
    WARN_ARRAY_INDEX,
    WARN_ARRAY_ROM,
    WARN_SELF,
    WARN_WAIT_ANIM,
    WARN_REACTION_WAIT,
    WARN_PROPERTY,
    WARN_PROP_ENTITY,
    WARN_PROP_COMPONENT,
    WARN_FIELD_UNATTACHED,
    WARN_SPAWN_OBJECT,
    WARN_SPAWN_FULL,
    WARN_NEXTI_OBJECT,
    WARN_KILL_NONE,
    WARN_SYS,
    WARN_SONG,
    WARN_PATH,
    WARN_STRING,
    WARN_NO_CONTEXT,
    WARN_QUEUE_FULL,
    WARN_STALE,
    WARN_ATTACH_BLOB,
    WARN_ATTACH_OBJECT,
    WARN_ATTACH_ENTITY,
    WARN_START_BLOB,
    WARN_START_OBJECT,
    WARN_START_HANDLER,
    WARN_EVENT,
    WARN_EVENT_DESTROY_NONE,
    WARN_LOAD_IN_PHASE,
    WARN_NESTED_PHASE,
    WARN_GLOBAL,
    WARN_VM_KILL_NONE,
    WARN_COLLIDE_MASK,
    WARN_COLLIDE_FULL,
    WARN_COUNT
};
static bool warned[WARN_COUNT];

static bool first_warning(u32 problem) {
    if (warned[problem])
        return false;
    warned[problem] = true;
    return true;
}
#define WARN_ONCE(problem, ...)                                                                    \
    do {                                                                                           \
        if (first_warning(problem))                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
    } while (0)

static void reset_warnings(void) {
    for (u32 k = 0; k < WARN_COUNT; k++)
        warned[k] = false;
}

static const char* const event_names[VM_EV_COUNT] = {"Create",    "Step",          "Destroy",
                                                     "Collision", "Animation End", "Room Start"};
#else
#define WARN_ONCE(problem, ...) ((void)0)
static void reset_warnings(void) {}
#endif

// --- Blob access -------------------------------------------------------------

static u32 le16(const u8* p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

static u32 le32(const u8* p) {
    return le16(p) | le16(p + 2) << 16;
}

static const u8* object_record(u32 object) {
    return blob + VM_HEADER_SIZE + object * VM_OBJECT_SIZE;
}

// The handler's blob offset, 0 if the object has none.
static u32 handler_of(u32 object, u32 event) {
    return le32(object_record(object) + 8 + event * 4);
}

// String `index` (< string_count). vm_load checked that its NUL is inside the
// blob.
static const char* string_of(u32 index) {
    return (const char*)(blob +
                         le32(blob + VM_HEADER_SIZE + object_count * VM_OBJECT_SIZE + index * 4));
}

// String `index` of the blob, or NULL (warning) if there is no such string.
static const char* string_at(s32 index) {
    if (index < 0 || (u32)index >= string_count) {
        WARN_ONCE(WARN_STRING, "vm: string index %d is not in the blob (%u strings)", (int)index,
                  string_count);
        return NULL;
    }
    return string_of((u32)index);
}

// A cell holding an entity handle. Values that no u16 handle has are no
// entity, rather than another entity's handle once truncated.
static Entity cell_entity(s32 value) {
    return value >= 0 && value <= 0xFFFF ? (Entity)value : ENTITY_NONE;
}

// --- Contexts and bindings ---------------------------------------------------

static u32 context_index(const Context* c) {
    return (u32)(c - contexts);
}

// Frees the context, a behaviour beneath a reaction included, and clears its
// entity's link to it.
static void halt(Context* c) {
    if (c->self != ENTITY_NONE) {
        u32 slot = entity_index(c->self);
        if (slot < MAX_ENT && bound_context[slot] == context_index(c) + 1)
            bound_context[slot] = 0;
    }
    c->r.state = CTX_FREE;
    c->stacked = false;
}

// The running handler has ended (or faulted): a reaction on top of a
// behaviour gives the context back to it, exactly as it was; anything else
// frees the context.
static void finish(Context* c) {
    if (c->stacked) {
        c->r = c->below;
        c->stacked = false;
    } else {
        halt(c);
    }
}

static void clear_fields(u32 slot) {
    for (u32 k = 0; k < VM_FIELDS; k++)
        fields[slot][k] = 0;
}

// Detaches slot's entity, halting its live context and clearing its instance
// fields.
static void unbind(u32 slot) {
    u32 live = bound_context[slot];
    bound_context[slot] = 0;
    bound[slot] = ENTITY_NONE;
    clear_fields(slot);
    if (live)
        halt(&contexts[live - 1]);
}

// True if an entity is attached in slot. A binding whose entity was destroyed
// behind the VM's back (entity_destroy instead of vm_kill) is stale: it is
// cleared and its context halted.
static bool attached(u32 slot) {
    Entity e = bound[slot];
    if (e == ENTITY_NONE)
        return false;
    if (entity_alive(e))
        return true;
    WARN_ONCE(WARN_STALE,
              "vm: entity %u (object %u) was destroyed while attached; its script is halted. "
              "Destroy scripted entities with vm_kill, or vm_detach them first",
              slot, (u32)bound_object[slot]);
    unbind(slot);
    return false;
}

// Takes the first free context (pool index order), or warns and returns NULL.
static Context* start_context(u32 pc, Entity self, Entity other, u32 event, u32 state) {
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        Context* c = &contexts[k];
        if (c->r.state != CTX_FREE)
            continue;
        c->r = (Registers){.pc = pc, .other = other, .state = (u8)state, .event = (u8)event};
        c->self = self;
        c->stacked = false;
        if (self != ENTITY_NONE)
            bound_context[entity_index(self)] = (u8)(k + 1);
        return c;
    }
#ifdef SERVAL_DEBUG
    WARN_ONCE(WARN_NO_CONTEXT,
              "vm: all %d contexts are busy; a %s handler is dropped (too many scripts waiting "
              "at once?)",
              VM_CONTEXTS, event < VM_EV_COUNT ? event_names[event] : "?");
#endif
    return NULL;
}

// Queues an event; false (warning) if the queue is full and it is dropped.
static bool enqueue(Entity e, Entity other, u32 event) {
    if (queue_count == VM_EVENT_QUEUE) {
#ifdef SERVAL_DEBUG
        WARN_ONCE(WARN_QUEUE_FULL,
                  "vm: the event queue is full (%d events); a %s event for entity %u is dropped",
                  VM_EVENT_QUEUE, event_names[event], (u32)entity_index(e));
#endif
        return false;
    }
    QueuedEvent* q = &queue[(queue_head + queue_count) % VM_EVENT_QUEUE];
    q->e = e;
    q->other = other;
    q->event = (u8)event;
    queue_count++;
    return true;
}

// True if a Create event for e is in the queue.
static bool create_queued(Entity e) {
    for (u32 k = 0; k < queue_count; k++) {
        const QueuedEvent* q = &queue[(queue_head + k) % VM_EVENT_QUEUE];
        if (q->e == e && q->event == VM_EV_CREATE)
            return true;
    }
    return false;
}

// Attaches a live entity to a valid object and queues its Create, unless one
// is queued already (attaching twice before a drain queues one Create). Its
// Step reaction waits until that Create is drained; a Create the full queue
// drops is never drained, so it doesn't hold Step back. Its instance fields
// start at zero: an unattached slot's always are (unbind and install clear
// them).
static void bind(Entity e, u32 object) {
    u32 slot = entity_index(e);
    if (attached(slot))
        unbind(slot); // rebinding: its live context is halted first
    bound[slot] = e;
    bound_object[slot] = (u16)object;
    bound_context[slot] = 0;
    bool pending = create_queued(e) || enqueue(e, ENTITY_NONE, VM_EV_CREATE);
    bound_flags[slot] = pending ? BIND_CREATE_PENDING : 0;
}

// --- Entities ----------------------------------------------------------------

// The component each engine property belongs to (0: none, VM_P_TAGS).
static const u32 prop_component[VM_P_COUNT] = {C_POS,  C_POS,  C_VEL, C_VEL,  C_SPR,
                                               C_SPR,  C_SPR,  C_SPR, C_SPR,  C_SPR,
                                               C_BODY, C_BODY, 0,     C_ANIM, C_ANIM};
// A property appended to vm.h without a row here would silently get component 0
// (no warning when it's missing) and fall into set_prop's default case.
_Static_assert(VM_P_COUNT == 15, "add the new property to prop_component, get_prop and set_prop");
_Static_assert(VM_P_FIELD0 >= VM_P_COUNT && VM_P_FIELD0 + VM_FIELDS <= 256,
               "the instance fields follow the engine properties, within a u8 operand");

// VM_P_TAGS: C_GAME(0) to C_GAME(14) of ent_mask, as bits 0 to 14.
#define TAG_SHIFT 16
#define TAG_BITS 0x7FFFu
_Static_assert(C_GAME(0) == 1u << TAG_SHIFT && C_GAME(14) == 1u << (TAG_SHIFT + 14),
               "VM_P_TAGS maps C_GAME(n) to bit n");

// The slot of the entity in `cell` for GETP or SETP of `prop`, or -1 (warning)
// for an unknown property, a dead entity, or a field of an unattached one.
static int prop_slot(u32 prop, s32 cell, u32 at) {
    (void)at; // only for warnings
    bool field = prop >= VM_P_FIELD0 && prop < VM_P_FIELD0 + VM_FIELDS;
    if (prop >= VM_P_COUNT && !field) {
        WARN_ONCE(WARN_PROPERTY,
                  "vm: GETP/SETP at 0x%x: no property %u (VM_P_X to VM_P_ANIM_STEP are 0 to %d, "
                  "the instance fields %d to %d); reads 0, writes nothing",
                  at, prop, VM_P_COUNT - 1, VM_P_FIELD0, VM_P_FIELD0 + VM_FIELDS - 1);
        return -1;
    }
    Entity e = cell_entity(cell);
    if (!entity_alive(e)) {
        WARN_ONCE(WARN_PROP_ENTITY,
                  "vm: GETP/SETP at 0x%x: entity %d is not alive (stale handle?); reads 0, "
                  "writes nothing",
                  at, (int)cell);
        return -1;
    }
    u32 i = entity_index(e);
    if (field) {
        // e is alive in slot i, so a binding there that is still attached is
        // e's.
        if (!attached(i)) {
            WARN_ONCE(WARN_FIELD_UNATTACHED,
                      "vm: GETP/SETP at 0x%x: entity %u is not attached to an object, so it has "
                      "no instance fields (property %u); reads 0, writes nothing",
                      at, i, prop);
            return -1;
        }
        return (int)i;
    }
    if (!ent_has(i, prop_component[prop]))
        WARN_ONCE(WARN_PROP_COMPONENT,
                  "vm: GETP/SETP at 0x%x: entity %u lacks the component of property %u "
                  "(C_POS for X/Y, C_VEL for VX/VY, C_BODY for BODY_W/H, C_ANIM for "
                  "ANIM_TIME/STEP, else C_SPR); its array is used anyway",
                  at, i, prop);
    return (int)i;
}

static s32 get_prop(u32 i, u32 prop) {
    switch (prop) {
    case VM_P_X:
        return pos_x[i];
    case VM_P_Y:
        return pos_y[i];
    case VM_P_VX:
        return vel_x[i];
    case VM_P_VY:
        return vel_y[i];
    case VM_P_SPR:
        return spr_id[i];
    case VM_P_FRAME:
        return spr_frame[i];
    case VM_P_FLAGS:
        return spr_flags[i];
    case VM_P_ANGLE:
        return spr_angle[i];
    case VM_P_DEPTH:
        return spr_depth[i];
    case VM_P_SCALE:
        return spr_scale[i];
    case VM_P_BODY_W:
        return body_w[i];
    case VM_P_BODY_H:
        return body_h[i];
    case VM_P_TAGS:
        return (s32)(ent_mask[i] >> TAG_SHIFT & TAG_BITS);
    case VM_P_ANIM_TIME:
        return spr_anim_time[i];
    case VM_P_ANIM_STEP:
        return spr_anim_step[i];
    default: // an instance field (prop_slot checked)
        return fields[i][prop - VM_P_FIELD0];
    }
}

// Stores the cell in the property's array, truncated to its type.
static void set_prop(u32 i, u32 prop, s32 value) {
    switch (prop) {
    case VM_P_X:
        pos_x[i] = value;
        break;
    case VM_P_Y:
        pos_y[i] = value;
        break;
    case VM_P_VX:
        vel_x[i] = value;
        break;
    case VM_P_VY:
        vel_y[i] = value;
        break;
    case VM_P_SPR:
        spr_id[i] = (u16)value;
        break;
    case VM_P_FRAME:
        spr_frame[i] = (u8)value;
        break;
    case VM_P_FLAGS:
        spr_flags[i] = (u16)value;
        break;
    case VM_P_ANGLE:
        spr_angle[i] = (u16)value;
        break;
    case VM_P_DEPTH:
        spr_depth[i] = (s16)value;
        break;
    case VM_P_SCALE:
        spr_scale[i] = (s16)value;
        break;
    case VM_P_BODY_W:
        body_w[i] = (u8)value;
        break;
    case VM_P_BODY_H:
        body_h[i] = (u8)value;
        break;
    case VM_P_TAGS: // only the game's components: C_ALIVE and the engine's stay
        ent_mask[i] = (ent_mask[i] & ~(TAG_BITS << TAG_SHIFT)) | ((u32)value & TAG_BITS)
                                                                     << TAG_SHIFT;
        break;
    case VM_P_ANIM_TIME:
        spr_anim_time[i] = (u8)value;
        break;
    case VM_P_ANIM_STEP:
        spr_anim_step[i] = (u8)value;
        break;
    default: // an instance field (prop_slot checked)
        fields[i][prop - VM_P_FIELD0] = value;
        break;
    }
}

// SPAWN: creates an entity of the object at (x, y), attaches it (queueing its
// Create) and returns it; ENTITY_NONE (warning) if the object or a free entity
// is missing.
static Entity spawn(u32 object, s32 x, s32 y, u32 at) {
    (void)at; // only for warnings
    if (object >= object_count) {
        WARN_ONCE(WARN_SPAWN_OBJECT, "vm: SPAWN at 0x%x: no object %u (the blob has %u)", at,
                  object, object_count);
        return ENTITY_NONE;
    }
    const u8* record = object_record(object);
    u32 mask = le32(record);
    Entity e = entity_create(mask);
    if (e == ENTITY_NONE) {
        WARN_ONCE(WARN_SPAWN_FULL, "vm: SPAWN at 0x%x: no free entity (all %d in use); pushes 0",
                  at, MAX_ENT);
        return ENTITY_NONE;
    }
    u32 i = entity_index(e);
    pos_x[i] = x;
    pos_y[i] = y;
    if (mask & C_SPR)
        spr_id[i] = (u16)le16(record + 4);
    bind(e, object);
    return e;
}

// NEXTI: the next attached instance of the object after the entity in `cell`,
// in slot order (no entity: from the first slot), or ENTITY_NONE. Slots, not
// handles, are compared, so an instance killed meanwhile still leads on.
static Entity next_instance(u32 object, s32 cell, u32 at) {
    (void)at; // only for warnings
    if (object >= object_count) {
        WARN_ONCE(WARN_NEXTI_OBJECT, "vm: NEXTI at 0x%x: no object %u (the blob has %u); pushes 0",
                  at, object, object_count);
        return ENTITY_NONE;
    }
    Entity e = cell_entity(cell);
    for (u32 slot = e == ENTITY_NONE ? 0 : entity_index(e) + 1u; slot < MAX_ENT; slot++) {
        if (bound_object[slot] == object && attached(slot))
            return bound[slot];
    }
    return ENTITY_NONE;
}

// WAIT_ANIM can wait for e: it has C_SPR | C_ANIM and a one-shot sprite.
// Checked when WAIT_ANIM runs and on every resume pass while it waits.
static bool anim_waitable(Entity e) {
    if (!entity_alive(e))
        return false;
    u32 i = entity_index(e);
    if (!ent_has(i, C_SPR | C_ANIM))
        return false;
    u32 id = spr_id[i];
    const SpriteAsset* sprite = id < serval_sprite_count ? serval_sprite_table[id] : NULL;
    return serval_plausible_pointer(sprite) && (sprite->flags & SPRITE_ASSET_ANIM_ONCE);
}

// --- Arrays ------------------------------------------------------------------

// Array n's 8-byte record, or NULL (warning) if the blob has no array n.
static const u8* array_record(u32 n, u32 at) {
    (void)at; // only for warnings
    if (n >= array_count) {
        WARN_ONCE(WARN_ARRAY,
                  "vm: LDA/STA/LEN at 0x%x: no array %u (the blob has %u); reads 0, writes nothing",
                  at, n, array_count);
        return NULL;
    }
    return blob + array_table + n * VM_ARRAY_RECORD_SIZE;
}

// True if i is an index of the array (warning if not).
static bool array_index_ok(const u8* record, u32 n, s32 i, u32 at) {
    (void)n, (void)at; // only for warnings
    u32 length = le16(record);
    if (i >= 0 && (u32)i < length)
        return true;
    WARN_ONCE(WARN_ARRAY_INDEX,
              "vm: LDA/STA at 0x%x: index %d is outside array %u (length %u; indices start at "
              "0); reads 0, writes nothing",
              at, (int)i, n, length);
    return false;
}

// Element i (in range) of an array.
static s32 array_get(const u8* record, u32 i) {
    u32 kind = record[2];
    u32 offset = le32(record + 4);
    if (kind == VM_ARRAY_RAM)
        return array_cells[offset + i];
    const u8* p = blob + offset + i * element_size[kind];
    switch (kind) {
    case VM_ARRAY_S8:
        return (s8)p[0];
    case VM_ARRAY_U8:
        return p[0];
    case VM_ARRAY_S16:
        return (s16)le16(p);
    case VM_ARRAY_U16:
        return (s32)le16(p);
    default: // VM_ARRAY_S32
        return (s32)le32(p);
    }
}

// --- Engine calls ------------------------------------------------------------

// Arguments per VM_SYS_* call (at most SYS_MAX_ARGS), and the calls that push a
// result. A call appended to vm.h without an entry here would silently take no
// arguments.
#define SYS_MAX_ARGS 4
static const u8 sys_arity[VM_SYS_COUNT] = {1, 1, 0, 0, 0, 2, 3, 2, 1, 1, 1, 3, 4, 1};
_Static_assert(VM_SYS_COUNT == 14, "add the new call to sys_arity, SYS_RETURNS and sys_call");
#define SYS_RETURNS                                                                                \
    (1u << VM_SYS_RANDOM_RANGE | 1u << VM_SYS_BUTTON_DOWN | 1u << VM_SYS_BUTTON_PRESSED)

static s32 sys_call(u32 fn, const s32* args) {
    switch (fn) {
    case VM_SYS_CAMERA_SET:
        camera_set(args[0], args[1]);
        return 0;
    case VM_SYS_RANDOM_RANGE:
        return random_range(args[0], args[1]);
    case VM_SYS_PATH_START: {
        s32 index = args[1];
        const Path* path = NULL;
        if (serval_plausible_pointer(bindings.paths) && index >= 0 &&
            (u32)index < bindings.path_count)
            path = bindings.paths[index];
        if (!serval_plausible_pointer(path)) {
            WARN_ONCE(WARN_PATH,
                      "vm: SYS path_start: path %d is not bound (%u paths; vm_bind sets them); "
                      "not started",
                      (int)index, (u32)bindings.path_count);
            return 0;
        }
        path_start(cell_entity(args[0]), path, (u32)args[2]);
        return 0;
    }
    case VM_SYS_PATH_STOP:
        path_stop(cell_entity(args[0]));
        return 0;
    case VM_SYS_PSG_MUSIC_PLAY: {
        s32 index = args[0];
        const PsgSong* song = NULL;
        if (serval_plausible_pointer(bindings.psg_songs) && index >= 0 &&
            (u32)index < bindings.psg_song_count)
            song = bindings.psg_songs[index];
        if (!serval_plausible_pointer(song)) {
            WARN_ONCE(WARN_SONG,
                      "vm: SYS psg_music_play: song %d is not bound (%u songs; vm_bind's "
                      "psg_songs); not played",
                      (int)index, (u32)bindings.psg_song_count);
            return 0;
        }
        return serval_vm_platform_call(fn, args, song);
    }
    case VM_SYS_TEXT_PRINT: {
        const char* s = string_at(args[2]);
        return s ? serval_vm_platform_call(fn, args, s) : 0;
    }
    default:
        return serval_vm_platform_call(fn, args, NULL);
    }
}

// --- Arithmetic --------------------------------------------------------------

// LSH: Lua's shift. Left by b, or logically right by -b; 32 or more either way
// shifts everything out.
static s32 lua_shift(s32 a, s32 b) {
    if (b >= 32 || b <= -32)
        return 0;
    return b >= 0 ? (s32)((u32)a << b) : (s32)((u32)a >> (u32)-b);
}

// IDIV and IMOD: Lua's floored // and % (b not 0). The truncating results,
// corrected toward negative infinity when the signs differ and the remainder
// isn't 0.
static s32 floored(u32 op, s32 a, s32 b) {
    if (b == -1) // INT32_MIN / -1 overflows in C: wrap it here
        return op == VM_OP_IDIV ? (s32)(0u - (u32)a) : 0;
    s32 q = a / b, m = a % b;
    if (m != 0 && (m ^ b) < 0) {
        q -= 1;
        m += b;
    }
    return op == VM_OP_IDIV ? q : m;
}

// --- Interpreter -------------------------------------------------------------

// Runs the context's handler until it ends or waits, or until it has run
// VM_OPS_PER_SLICE ops. `reaction`: a reaction, which must not wait (a wait,
// or running past the budget, warns and ends it). Returns the number of ops
// run.
static u32 execute(Context* c, bool reaction) {
    const u8* const code = blob;
    const u32 size = blob_size;
    s32* const st = c->stack;
    const u32 base = c->r.base;
    u32 pc = c->r.pc;
    u32 sp = c->r.sp;
    u32 fp = c->r.fp;
    u32 ops = 0;
    u32 at = pc; // the current op's offset, for warnings
    (void)at;    // (release builds have none)
    c->r.state = CTX_RUNNING;

// pc is at most size here (the opcode at pc - 1 was inside the blob).
#define OPERAND(n)                                                                                 \
    if ((n) > size - pc)                                                                           \
    goto escaped
#define NEED(n)                                                                                    \
    if (sp < base + (n))                                                                           \
    goto underflow
#define ROOM(n)                                                                                    \
    if (sp + (n) > VM_STACK)                                                                       \
    goto overflow
#define BINARY(expr)                                                                               \
    do {                                                                                           \
        NEED(2);                                                                                   \
        s32 b = st[--sp], a = st[sp - 1];                                                          \
        st[sp - 1] = (expr);                                                                       \
    } while (0)

    for (;;) {
        at = pc;
        if (pc >= size)
            goto escaped;
        if (ops == VM_OPS_PER_SLICE)
            goto budget;
        ops++;
        u32 op = code[pc++];
        switch (op) {
        case VM_OP_NOP:
            break;
        case VM_OP_HALT:
            goto ended;
        case VM_OP_PUSH8:
            OPERAND(1);
            ROOM(1);
            st[sp++] = (s8)code[pc];
            pc += 1;
            break;
        case VM_OP_PUSH16:
            OPERAND(2);
            ROOM(1);
            st[sp++] = (s16)le16(code + pc);
            pc += 2;
            break;
        case VM_OP_PUSH32:
            OPERAND(4);
            ROOM(1);
            st[sp++] = (s32)le32(code + pc);
            pc += 4;
            break;
        case VM_OP_DUP:
            NEED(1);
            ROOM(1);
            st[sp] = st[sp - 1];
            sp++;
            break;
        case VM_OP_DROP:
            NEED(1);
            sp--;
            break;
        case VM_OP_SWAP: {
            NEED(2);
            s32 t = st[sp - 1];
            st[sp - 1] = st[sp - 2];
            st[sp - 2] = t;
            break;
        }
        case VM_OP_LDG:
            OPERAND(1);
            ROOM(1);
            st[sp++] = globals[code[pc]];
            pc += 1;
            break;
        case VM_OP_STG:
            OPERAND(1);
            NEED(1);
            globals[code[pc]] = st[--sp];
            pc += 1;
            break;
        // Locals are the frame's cells: fp + n, below the stack top.
        case VM_OP_LDL: {
            OPERAND(1);
            ROOM(1);
            u32 n = code[pc];
            pc += 1;
            if (fp + n < sp) {
                st[sp] = st[fp + n];
            } else {
                WARN_ONCE(WARN_LOCAL,
                          "vm: LDL/STL at 0x%x: local %u is outside the frame (%d cells; ENTER "
                          "makes locals); reads 0, writes nothing",
                          at, n, (int)sp - (int)fp);
                st[sp] = 0;
            }
            sp++;
            break;
        }
        case VM_OP_STL: {
            OPERAND(1);
            NEED(1);
            u32 n = code[pc];
            pc += 1;
            s32 value = st[--sp];
            if (fp + n < sp)
                st[fp + n] = value;
            else
                WARN_ONCE(WARN_LOCAL,
                          "vm: LDL/STL at 0x%x: local %u is outside the frame (%d cells; ENTER "
                          "makes locals); reads 0, writes nothing",
                          at, n, (int)sp - (int)fp);
            break;
        }
        case VM_OP_LDA:
        case VM_OP_STA:
        case VM_OP_LEN: {
            OPERAND(2);
            u32 n = le16(code + pc);
            pc += 2;
            if (op == VM_OP_LEN) {
                ROOM(1);
                const u8* record = array_record(n, at);
                st[sp++] = record ? (s32)le16(record) : 0;
            } else if (op == VM_OP_LDA) {
                NEED(1);
                const u8* record = array_record(n, at);
                s32 i = st[sp - 1];
                st[sp - 1] =
                    record && array_index_ok(record, n, i, at) ? array_get(record, (u32)i) : 0;
            } else {
                NEED(2);
                s32 value = st[--sp];
                s32 i = st[--sp];
                const u8* record = array_record(n, at);
                if (!record)
                    break;
                if (record[2] != VM_ARRAY_RAM) {
                    WARN_ONCE(WARN_ARRAY_ROM,
                              "vm: STA at 0x%x: array %u is ROM data, which scripts can't change; "
                              "nothing written",
                              at, n);
                    break;
                }
                if (array_index_ok(record, n, i, at))
                    array_cells[le32(record + 4) + (u32)i] = value;
            }
            break;
        }

        // Arithmetic wraps (two's complement, done in u32: no C undefined
        // behaviour), and dividing by zero gives 0.
        case VM_OP_ADD:
            BINARY((s32)((u32)a + (u32)b));
            break;
        case VM_OP_SUB:
            BINARY((s32)((u32)a - (u32)b));
            break;
        case VM_OP_MUL:
            BINARY((s32)((u32)a * (u32)b));
            break;
        case VM_OP_DIV:
        case VM_OP_MOD:
        case VM_OP_FXDIV:
        case VM_OP_IDIV:
        case VM_OP_IMOD: {
            NEED(2);
            s32 b = st[--sp], a = st[sp - 1];
            s32 r;
            if (b == 0) {
                WARN_ONCE(WARN_DIV_ZERO,
                          "vm: division by zero at 0x%x (DIV, MOD, FXDIV, IDIV or IMOD); gives 0",
                          at);
                r = 0;
            } else if (op == VM_OP_FXDIV) {
                r = (s32)(u32)(uint64_t)((int64_t)a * 256 / b);
            } else if (op == VM_OP_IDIV || op == VM_OP_IMOD) {
                r = floored(op, a, b);
            } else if (b == -1) { // INT32_MIN / -1 overflows in C: wrap it here
                r = op == VM_OP_DIV ? (s32)(0u - (u32)a) : 0;
            } else {
                r = op == VM_OP_DIV ? a / b : a % b;
            }
            st[sp - 1] = r;
            break;
        }
        case VM_OP_NEG:
            NEED(1);
            st[sp - 1] = (s32)(0u - (u32)st[sp - 1]);
            break;
        case VM_OP_FXMUL:
            BINARY((s32)(u32)(uint64_t)(((int64_t)a * b) >> 8));
            break;
        case VM_OP_AND:
            BINARY(a & b);
            break;
        case VM_OP_OR:
            BINARY(a | b);
            break;
        case VM_OP_XOR:
            BINARY(a ^ b);
            break;
        case VM_OP_BNOT:
            NEED(1);
            st[sp - 1] = ~st[sp - 1];
            break;
        case VM_OP_SHL:
            BINARY((s32)((u32)a << ((u32)b & 31)));
            break;
        case VM_OP_SHR: // arithmetic: written so negative values need no
                        // implementation-defined shift
            BINARY(a < 0 ? ~(~a >> ((u32)b & 31)) : a >> ((u32)b & 31));
            break;
        case VM_OP_LNOT:
            NEED(1);
            st[sp - 1] = st[sp - 1] == 0;
            break;
        case VM_OP_LSH:
            BINARY(lua_shift(a, b));
            break;
        case VM_OP_EQ:
            BINARY(a == b);
            break;
        case VM_OP_NE:
            BINARY(a != b);
            break;
        case VM_OP_LT:
            BINARY(a < b);
            break;
        case VM_OP_LE:
            BINARY(a <= b);
            break;
        case VM_OP_GT:
            BINARY(a > b);
            break;
        case VM_OP_GE:
            BINARY(a >= b);
            break;

        // Jumps: rel16 counts from the end of the operand. A target outside
        // the blob is caught by the next fetch (u32 wraparound included).
        case VM_OP_JMP:
            OPERAND(2);
            pc += 2 + (u32)(s32)(s16)le16(code + pc);
            break;
        case VM_OP_JZ:
        case VM_OP_JNZ: {
            OPERAND(2);
            NEED(1);
            u32 target = pc + 2 + (u32)(s32)(s16)le16(code + pc);
            bool zero = st[--sp] == 0;
            bool jump = op == VM_OP_JZ ? zero : !zero;
            pc = jump ? target : pc + 2;
            break;
        }
        // Frames: CALL saves the return point and the caller's frame and
        // starts the callee's at the stack top (its arguments just below);
        // RET drops the whole frame. At the handler's own level (cp 0), RET
        // and RETV end the handler, whatever the stack holds.
        case VM_OP_CALL:
            OPERAND(4);
            if (c->r.cp == VM_CALLS)
                goto call_depth;
            c->call_pc[c->r.cp] = pc + 4;
            c->call_fp[c->r.cp] = (u8)fp;
            c->r.cp++;
            fp = sp;
            pc = le32(code + pc);
            break;
        case VM_OP_RET:
        case VM_OP_RETV: {
            if (c->r.cp == 0)
                goto ended;
            s32 value = 0;
            if (op == VM_OP_RETV) {
                NEED(1);
                value = st[--sp];
            }
            sp = fp;
            c->r.cp--;
            pc = c->call_pc[c->r.cp];
            fp = c->call_fp[c->r.cp];
            if (op == VM_OP_RETV) {
                ROOM(1);
                st[sp++] = value;
            }
            break;
        }
        case VM_OP_ENTER: {
            OPERAND(2);
            u32 p = code[pc], n = code[pc + 1];
            pc += 2;
            NEED(p); // the arguments: in this activation
            ROOM(n);
            fp = sp - p;
            for (u32 k = 0; k < n; k++)
                st[sp++] = 0;
            break;
        }

        case VM_OP_WAIT: {
            NEED(1);
            s32 n = st[--sp];
            if (n <= 0)
                break;
            if (reaction)
                goto reaction_wait;
            c->r.wait_frames = n > 0xFFFF ? 0xFFFF : (u16)n;
            c->r.state = CTX_WAIT_FRAMES;
            goto suspend;
        }
        case VM_OP_WAIT_ANIM:
            if (!anim_waitable(c->self)) {
                WARN_ONCE(WARN_WAIT_ANIM,
                          "vm: WAIT_ANIM at 0x%x: self has no one-shot animation to wait for "
                          "(no C_SPR | C_ANIM, a sprite without SPRITE_ASSET_ANIM_ONCE, or no "
                          "self); not waiting",
                          at);
                break;
            }
            if (anim_finished(c->self))
                break;
            if (reaction)
                goto reaction_wait;
            c->r.state = CTX_WAIT_ANIM;
            goto suspend;
        case VM_OP_WAIT_MOVE:
            if (!path_active(c->self))
                break;
            if (reaction)
                goto reaction_wait;
            c->r.state = CTX_WAIT_MOVE;
            goto suspend;

        case VM_OP_SELF:
            ROOM(1);
            if (c->self == ENTITY_NONE)
                WARN_ONCE(WARN_SELF,
                          "vm: SELF at 0x%x in a thread (vm_start), which has no entity; pushes 0",
                          at);
            st[sp++] = c->self;
            break;
        case VM_OP_OTHER:
            ROOM(1);
            st[sp++] = c->r.other;
            break;
        case VM_OP_GETP: {
            OPERAND(1);
            NEED(1);
            u32 prop = code[pc];
            pc += 1;
            int i = prop_slot(prop, st[sp - 1], at);
            st[sp - 1] = i < 0 ? 0 : get_prop((u32)i, prop);
            break;
        }
        case VM_OP_SETP: {
            OPERAND(1);
            NEED(2);
            u32 prop = code[pc];
            pc += 1;
            s32 value = st[--sp];
            int i = prop_slot(prop, st[--sp], at);
            if (i >= 0)
                set_prop((u32)i, prop, value);
            break;
        }
        case VM_OP_SPAWN: {
            OPERAND(2);
            NEED(2);
            u32 object = le16(code + pc);
            pc += 2;
            s32 y = st[--sp], x = st[--sp];
            st[sp++] = spawn(object, x, y, at);
            if (c->r.state != CTX_RUNNING)
                return ops; // halted from outside (cannot happen today)
            break;
        }
        case VM_OP_KILL: {
            NEED(1);
            s32 cell = st[--sp];
            Entity e = cell_entity(cell);
            // A dead entity is skipped when its Destroy is drained, silently:
            // two scripts killing the same thing is normal. No entity at all
            // is a script bug.
            if (e == ENTITY_NONE)
                WARN_ONCE(WARN_KILL_NONE,
                          "vm: KILL at 0x%x of %d, which is no entity (a failed SPAWN, or a "
                          "variable never set?); ignored",
                          at, (int)cell);
            else
                enqueue(e, ENTITY_NONE, VM_EV_DESTROY);
            break;
        }
        case VM_OP_NEXTI: {
            OPERAND(2);
            NEED(1);
            u32 object = le16(code + pc);
            pc += 2;
            st[sp - 1] = next_instance(object, st[sp - 1], at);
            if (c->r.state != CTX_RUNNING)
                return ops; // halted from outside (cannot happen today)
            break;
        }

        case VM_OP_SYS: {
            OPERAND(1);
            u32 fn = code[pc];
            pc += 1;
            if (fn >= VM_SYS_COUNT) {
                WARN_ONCE(WARN_SYS, "vm: SYS at 0x%x: no engine call %u (0 to %d); halted", at, fn,
                          VM_SYS_COUNT - 1);
                goto ended;
            }
            u32 n = sys_arity[fn];
            NEED(n);
            s32 args[SYS_MAX_ARGS] = {0, 0, 0, 0};
            sp -= n;
            for (u32 k = 0; k < n; k++)
                args[k] = st[sp + k];
            s32 result = sys_call(fn, args);
            if (c->r.state != CTX_RUNNING)
                return ops; // halted from outside (cannot happen today)
            if (SYS_RETURNS & 1u << fn)
                st[sp++] = result; // room: every call with a result pops an argument
            break;
        }

        case VM_OP_BRK:
#ifdef SERVAL_DEBUG
            debug_log(text_format("vm: BRK at 0x%x", at));
#endif
            break;
        case VM_OP_TRACE: {
            OPERAND(2);
#ifdef SERVAL_DEBUG
            u32 index = le16(code + pc);
            const char* s = index < string_count ? string_of(index) : "?";
            if (sp > base)
                debug_log(text_format("vm: %s %d", s, (int)st[sp - 1]));
            else
                debug_log(text_format("vm: %s", s));
#endif
            pc += 2;
            break;
        }

        default:
            WARN_ONCE(WARN_UNKNOWN_OP, "vm: unknown opcode 0x%x at 0x%x; halted", op, at);
            goto ended;
        }
    }

#undef OPERAND
#undef NEED
#undef ROOM
#undef BINARY

escaped:
    WARN_ONCE(WARN_ESCAPED,
              "vm: a script ran out of the blob at 0x%x (%u bytes): a bad jump or call, or a "
              "handler without HALT at its end; halted",
              at, size);
    goto ended;
overflow:
    WARN_ONCE(WARN_OVERFLOW, "vm: stack overflow at 0x%x (more than %d cells); halted", at,
              VM_STACK);
    goto ended;
underflow:
    WARN_ONCE(WARN_UNDERFLOW, "vm: stack underflow at 0x%x (opcode 0x%x needs more cells); halted",
              at, (u32)code[at]);
    goto ended;
call_depth:
    WARN_ONCE(WARN_CALL_DEPTH, "vm: CALL at 0x%x is nested more than %d deep; halted", at,
              VM_CALLS);
    goto ended;
reaction_wait:
#ifdef SERVAL_DEBUG
    WARN_ONCE(WARN_REACTION_WAIT,
              "vm: a %s reaction waits at 0x%x; only Create and Room Start may wait, so it is "
              "halted",
              event_names[c->r.event], at);
#endif
    goto ended;
budget: // the op at `at` is not run
#ifdef SERVAL_DEBUG
    if (first_warning(reaction ? WARN_REACTION_BUDGET : WARN_BUDGET))
        SERVAL_WARN("vm: a %s %s ran %d ops in one phase (an endless loop?); it %s at 0x%x",
                    event_names[c->r.event], reaction ? "reaction" : "behaviour", VM_OPS_PER_SLICE,
                    reaction ? "is halted (reactions can't wait)" : "waits a frame", at);
#endif
    if (reaction)
        goto ended;
    c->r.wait_frames = 1;
    c->r.state = CTX_WAIT_FRAMES;
    goto suspend;
suspend:
    c->r.pc = pc;
    c->r.sp = (u8)sp;
    c->r.fp = (u8)fp;
    return ops;
ended:
    finish(c);
    return ops;
}

static void run(Context* c, bool reaction) {
    ops_frame += execute(c, reaction);
}

// --- Scheduler ---------------------------------------------------------------

// The return points of a behaviour that waits inside a CALL, while a reaction
// runs on top of it: the reaction's own calls start at depth 0, in the same
// slots. One set is enough, as for `below`: reactions don't nest.
SERVAL_EWRAM_BSS static u32 below_call_pc[VM_CALLS];
SERVAL_EWRAM_BSS static u8 below_call_fp[VM_CALLS];

// Runs a reaction of slot's attached entity to completion: on top of its
// behaviour if one waits (it always does, if it has one: no script runs while
// events are dispatched), else in a context of its own, freed when it ends.
static void react(u32 slot, u32 handler, Entity other, u32 event) {
    u32 live = bound_context[slot];
    if (!live) {
        Context* c = start_context(handler, bound[slot], other, event, CTX_RUNNING);
        if (c)
            run(c, true);
        return;
    }
    Context* c = &contexts[live - 1];
    u32 depth = c->r.cp;
    for (u32 k = 0; k < depth; k++) {
        below_call_pc[k] = c->call_pc[k];
        below_call_fp[k] = c->call_fp[k];
    }
    c->below = c->r;
    c->stacked = true;
    u8 top = c->r.sp;
    c->r = (Registers){.pc = handler,
                       .other = other,
                       .state = CTX_RUNNING,
                       .event = (u8)event,
                       .sp = top,
                       .fp = top,
                       .base = top};
    run(c, true);
    if (c->r.state < CTX_WAIT_FRAMES)
        return; // the behaviour was halted meanwhile (cannot happen today)
    for (u32 k = 0; k < depth; k++) {
        c->call_pc[k] = below_call_pc[k];
        c->call_fp[k] = below_call_fp[k];
    }
}

// Slots whose entity destroy() destroyed since the collision pass listed the
// current pair's sets, as bits: the pass skips them even if a spawn has
// reused the slot meanwhile (that entity isn't the one it listed). destroy()
// is the only way an entity dies while the VM runs: no SYS call destroys.
SERVAL_EWRAM_BSS static u32 destroyed_slots[MAX_ENT / 32];

// The Destroy logic (KILL, vm_kill): runs e's Destroy reaction if it is
// attached (on top of its behaviour if that waits), then halts the behaviour,
// unbinds e and destroys it.
static void destroy(Entity e) {
    if (!entity_alive(e))
        return;
    u32 slot = entity_index(e);
    if (attached(slot)) {
        u32 handler = handler_of(bound_object[slot], VM_EV_DESTROY);
        if (handler)
            react(slot, handler, ENTITY_NONE, VM_EV_DESTROY);
        unbind(slot); // halts the behaviour
    }
    entity_destroy(e);
    destroyed_slots[slot / 32] |= 1u << (slot % 32);
}

// Runs a queued event's handler for e (any event but Destroy): a behaviour
// replaces e's live one, a reaction runs to completion.
static void dispatch(Entity e, Entity other, u32 event) {
    if (!entity_alive(e))
        return;
    u32 slot = entity_index(e);
    if (!attached(slot))
        return;
    // Drained, whether it runs, has no handler or is dropped below: the
    // entity's Step reaction may run from now on.
    if (event == VM_EV_CREATE)
        bound_flags[slot] &= (u8)~BIND_CREATE_PENDING;
    u32 handler = handler_of(bound_object[slot], event);
    if (!handler)
        return;
    if (!(BEHAVIOUR_EVENTS & 1u << event)) {
        react(slot, handler, other, event);
        return;
    }
    if (bound_context[slot])
        halt(&contexts[bound_context[slot] - 1]); // at most one behaviour per instance
    Context* c = start_context(handler, e, other, event, CTX_RUNNING);
    if (c)
        run(c, false);
}

// Runs queued events, oldest first, until the queue is empty (including
// events queued meanwhile).
static void drain(void) {
    while (queue_count) {
        QueuedEvent q = queue[queue_head];
        queue_head = (queue_head + 1) % VM_EVENT_QUEUE;
        queue_count--;
        if (q.event == VM_EV_DESTROY)
            destroy(q.e);
        else
            dispatch(q.e, q.other, q.event);
    }
}

// Queues Animation End for each attached entity whose object has a handler
// for it and whose animation finished since the previous check (an edge, so
// an animation the game restarts can raise it again).
static void queue_anim_ends(void) {
    for (u32 slot = 0; slot < MAX_ENT; slot++) {
        // The handler first: most objects have none, and it costs no call.
        if (bound[slot] == ENTITY_NONE || !handler_of(bound_object[slot], VM_EV_ANIM_END) ||
            !attached(slot))
            continue;
        u32 flags = bound_flags[slot];
        if (anim_finished(bound[slot])) {
            if (!(flags & BIND_ANIM_DONE))
                enqueue(bound[slot], ENTITY_NONE, VM_EV_ANIM_END);
            flags |= BIND_ANIM_DONE;
        } else {
            flags &= ~(u32)BIND_ANIM_DONE;
        }
        bound_flags[slot] = (u8)flags;
    }
}

// True (warning) if a phase, or vm_kill's Destroy logic, is running: a phase
// started from inside one (by C code a script reached) would dispatch events
// to a context whose script is running.
static bool nested_phase(const char* who) {
    (void)who; // only for warnings
    if (!in_phase)
        return false;
    WARN_ONCE(WARN_NESTED_PHASE, "%s: called during vm_step, vm_events or vm_kill; ignored", who);
    return true;
}

void vm_step(void) {
    if (nested_phase("vm_step"))
        return;
    ops_frame = 0;
    in_phase = true;
    // Resume pass: each context once, in pool order.
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        Context* c = &contexts[k];
        if (c->r.state == CTX_FREE)
            continue;
        if (c->self != ENTITY_NONE && !entity_alive(c->self)) {
            u32 slot = entity_index(c->self);
            if (slot < MAX_ENT && bound[slot] == c->self)
                attached(slot); // a stale binding: warns, halts its context
            halt(c);
            continue;
        }
        bool resume;
        switch (c->r.state) {
        case CTX_READY:
            resume = true;
            break;
        case CTX_WAIT_FRAMES:
            resume = --c->r.wait_frames == 0;
            break;
        case CTX_WAIT_ANIM:
            // The game may have switched self to a sprite without a one-shot
            // animation since: then nothing would end the wait.
            if (anim_waitable(c->self)) {
                resume = anim_finished(c->self);
            } else {
                WARN_ONCE(WARN_WAIT_ANIM,
                          "vm: WAIT_ANIM at 0x%x: self no longer has a one-shot animation to "
                          "wait for (its sprite or components changed); not waiting",
                          c->r.pc - 1);
                resume = true;
            }
            break;
        case CTX_WAIT_MOVE:
            resume = !path_active(c->self);
            break;
        default:
            resume = false;
            break;
        }
        if (resume)
            run(c, false);
    }
    queue_anim_ends();
    drain(); // Creates queued since the last phase run before their first Step
    // Step reactions, in entity order, for attached entities whose Create has
    // been drained: on top of their behaviour if it waits.
    for (u32 slot = 0; slot < MAX_ENT; slot++) {
        if (!attached(slot) || (bound_flags[slot] & BIND_CREATE_PENDING))
            continue;
        u32 handler = handler_of(bound_object[slot], VM_EV_STEP);
        if (handler)
            react(slot, handler, ENTITY_NONE, VM_EV_STEP);
    }
    drain(); // events the Step reactions queued, e.g. the Create of an entity they spawned
    in_phase = false;
}

// --- Collisions --------------------------------------------------------------

// vm_collide's pairs, in the order they were set: component masks, never 0.
SERVAL_EWRAM_BSS static u32 collide_a[VM_COLLIDE_PAIRS], collide_b[VM_COLLIDE_PAIRS];
static u32 collide_count;
// The pass's lists of the current pair's sets (slot indices, ascending), and
// the slots listed in both, as bits.
SERVAL_EWRAM_BSS static u8 set_a[MAX_ENT], set_b[MAX_ENT];
SERVAL_EWRAM_BSS static u32 in_both[MAX_ENT / 32];

static bool slot_bit(const u32* bits, u32 slot) {
    return bits[slot / 32] >> (slot % 32) & 1;
}

// Marks the slots both sorted lists hold in in_both; true if there are any.
static bool mark_both(const u8* a, u32 na, const u8* b, u32 nb) {
    for (u32 k = 0; k < MAX_ENT / 32; k++)
        in_both[k] = 0;
    bool any = false;
    for (u32 j = 0, k = 0; j < na && k < nb;) {
        if (a[j] < b[k]) {
            j++;
        } else if (a[j] > b[k]) {
            k++;
        } else {
            in_both[a[j] / 32] |= 1u << (a[j] % 32);
            any = true;
            j++;
            k++;
        }
    }
    return any;
}

// True if listed slot i is still in its set: alive, with the set's
// components (a reaction may have changed its tags), and not destroyed since
// the pair's sets were listed (the slot may hold a newer entity).
static bool still_in_set(u32 i, u32 mask) {
    return ent_has(i, mask) && !slot_bit(destroyed_slots, i);
}

// Queues a Collision event for slot i's entity, with slot o's as OTHER, if it
// is attached to an object with a Collision handler.
static void collision(u32 i, u32 o) {
    if (attached(i) && handler_of(bound_object[i], VM_EV_COLLISION))
        enqueue(bound[i], entity_at(o), VM_EV_COLLISION);
}

// Tests one pair: every listed entity of set a against every one of set b,
// in slot order, running the Collision reactions of each overlap before the
// next test (vm.h, vm_collide). The queue is empty at each overlap (drained
// before the pass and after each overlap), so its two events always fit.
static void collide_pair(u32 ma, u32 mb) {
    for (u32 k = 0; k < MAX_ENT / 32; k++)
        destroyed_slots[k] = 0;
    u32 na = ecs_gather(ma, set_a);
    if (!na)
        return;
    const u8* list_b = set_a; // the same set: listed once
    u32 nb = na;
    if (mb != ma) {
        nb = ecs_gather(mb, set_b);
        list_b = set_b;
    }
    // Entities in both sets meet twice, as (x, y) and (y, x): only the first,
    // with the lower slot as a, is tested. With the same set, that is every
    // pair after the entity in the list.
    bool shared = ma != mb && mark_both(set_a, na, list_b, nb);
    for (u32 j = 0; j < na; j++) {
        u32 x = set_a[j];
        if (!still_in_set(x, ma))
            continue;
        for (u32 k = ma == mb ? j + 1 : 0; k < nb; k++) {
            u32 y = list_b[k];
            // The overlap first: it is the common reason to go on, and the
            // only cost most tests have. The rest is checked on a hit.
            if (!body_overlap(x, y) || y == x || !still_in_set(y, mb))
                continue;
            if (shared && y < x && slot_bit(in_both, x) && slot_bit(in_both, y))
                continue; // tested as (y, x)
            collision(x, y);
            collision(y, x);
            drain();
            if (!still_in_set(x, ma))
                break; // a reaction killed x or took it out of the set
        }
    }
}

bool vm_collide(u32 a, u32 b) {
    if (!a || !b) {
        WARN_ONCE(WARN_COLLIDE_MASK,
                  "vm_collide: a mask of 0 would test every live entity (C_BODY means every "
                  "body); not set");
        return false;
    }
    for (u32 p = 0; p < collide_count; p++) {
        if ((collide_a[p] == a && collide_b[p] == b) || (collide_a[p] == b && collide_b[p] == a))
            return true; // set already: setting it again would raise its events twice
    }
    if (collide_count == VM_COLLIDE_PAIRS) {
        WARN_ONCE(WARN_COLLIDE_FULL,
                  "vm_collide: %d pairs are set already (VM_COLLIDE_PAIRS); not set "
                  "(vm_collide_clear removes them)",
                  VM_COLLIDE_PAIRS);
        return false;
    }
    collide_a[collide_count] = a;
    collide_b[collide_count] = b;
    collide_count++;
    return true;
}

void vm_collide_clear(void) {
    collide_count = 0;
}

void vm_events(void) {
    if (nested_phase("vm_events"))
        return;
    in_phase = true;
    drain(); // what game code queued since vm_step
    // No blob, no attached entity: nothing could react to a collision.
    for (u32 p = 0; blob && p < collide_count; p++)
        collide_pair(collide_a[p], collide_b[p]);
    in_phase = false;
}

// --- Entities and events -----------------------------------------------------

void vm_attach(Entity e, u16 object) {
    if (!blob) {
        WARN_ONCE(WARN_ATTACH_BLOB, "vm_attach: no scripts are loaded (vm_load); ignored");
        return;
    }
    if (object >= object_count) {
        WARN_ONCE(WARN_ATTACH_OBJECT, "vm_attach: no object %u (the blob has %u); ignored",
                  (u32)object, object_count);
        return;
    }
    if (!entity_alive(e)) {
        WARN_ONCE(WARN_ATTACH_ENTITY,
                  "vm_attach: entity %u is not alive (stale handle or ENTITY_NONE); ignored",
                  (u32)e);
        return;
    }
    bind(e, object);
}

void vm_detach(Entity e) {
    u32 slot = entity_index(e);
    if (e != ENTITY_NONE && slot < MAX_ENT && bound[slot] == e && attached(slot))
        unbind(slot);
}

void vm_kill(Entity e) {
    if (e == ENTITY_NONE) { // as KILL: misuse; a dead entity is just skipped
        WARN_ONCE(WARN_VM_KILL_NONE, "vm_kill: ENTITY_NONE; ignored");
        return;
    }
    if (in_phase) {
        enqueue(e, ENTITY_NONE, VM_EV_DESTROY);
        return;
    }
    // The Destroy reaction runs now; while it does, this counts as a phase
    // (loads are refused, vm_kill queues, vm_step and vm_events don't nest).
    in_phase = true;
    destroy(e);
    in_phase = false;
}

int vm_start(u16 object, u8 event) {
    if (!blob) {
        WARN_ONCE(WARN_START_BLOB, "vm_start: no scripts are loaded (vm_load); returns -1");
        return -1;
    }
    if (object >= object_count) {
        WARN_ONCE(WARN_START_OBJECT, "vm_start: no object %u (the blob has %u); returns -1",
                  (u32)object, object_count);
        return -1;
    }
    u32 handler = event < VM_EV_COUNT ? handler_of(object, event) : 0;
    if (!handler) {
        WARN_ONCE(WARN_START_HANDLER, "vm_start: object %u has no handler for event %u; returns -1",
                  (u32)object, (u32)event);
        return -1;
    }
    Context* c = start_context(handler, ENTITY_NONE, ENTITY_NONE, event, CTX_READY);
    return c ? (int)context_index(c) : -1;
}

void vm_event(Entity e, Entity other, u8 event) {
    if (event >= VM_EV_COUNT) {
        WARN_ONCE(WARN_EVENT, "vm_event: no event %u (VM_EV_CREATE to VM_EV_ROOM_START); ignored",
                  (u32)event);
        return;
    }
    // Destroy is exactly KILL: queued, `other` unused (the reaction's OTHER is
    // 0), and no entity at all warns.
    if (event == VM_EV_DESTROY && e == ENTITY_NONE) {
        WARN_ONCE(WARN_EVENT_DESTROY_NONE, "vm_event: Destroy for ENTITY_NONE; ignored");
        return;
    }
    enqueue(e, other, event);
}

// --- Loading -----------------------------------------------------------------

// Checks the header and tables; warns about the first problem found.
static bool valid_blob(const u8* b, u32 size, const char* who) {
    (void)who; // only for warnings
    if (!serval_plausible_pointer(b) || size < VM_HEADER_SIZE) {
        SERVAL_WARN("%s: the blob is %s; nothing is loaded", who,
                    !serval_plausible_pointer(b) ? "not a valid pointer"
                                                 : "shorter than its 16-byte header");
        return false;
    }
    if (b[0] != 'S' || b[1] != 'V' || b[2] != 'M' || b[3] != 'B') {
        SERVAL_WARN("%s: not a script blob (no \"SVMB\" at its start); nothing is loaded", who);
        return false;
    }
    if (b[4] != VM_FORMAT_VERSION) {
        SERVAL_WARN("%s: the blob is format version %u, but this engine runs version %d; nothing "
                    "is loaded",
                    who, (u32)b[4], VM_FORMAT_VERSION);
        return false;
    }
    if (b[5] != VM_CELL_BYTES) {
        SERVAL_WARN("%s: the blob has %u-byte cells, but this engine's are %d bytes; nothing is "
                    "loaded",
                    who, (u32)b[5], VM_CELL_BYTES);
        return false;
    }
    u32 flags = le16(b + 6);
    if (flags & ~(u32)VM_FLAG_GLOBAL_VALUES) {
        SERVAL_WARN("%s: the blob's header flags are 0x%x, but this engine knows only bit 0 "
                    "(the globals' initial values; the others must be 0); nothing is loaded",
                    who, flags);
        return false;
    }
    u32 objects = le16(b + 8), strings = le16(b + 10), used_globals = le16(b + 12);
    u32 arrays = le16(b + 14);
    if (used_globals > VM_GLOBALS) {
        SERVAL_WARN("%s: the blob uses %u globals; the most is %d; nothing is loaded", who,
                    used_globals, VM_GLOBALS);
        return false;
    }
    u32 arrays_at = VM_HEADER_SIZE + objects * VM_OBJECT_SIZE + strings * 4;
    // The globals' initial values, if the flag says they are there, end the tables.
    u32 values = flags & VM_FLAG_GLOBAL_VALUES ? used_globals * VM_CELL_BYTES : 0;
    u32 tables_end = arrays_at + arrays * VM_ARRAY_RECORD_SIZE + values;
    if (tables_end > size) {
        SERVAL_WARN("%s: the blob's tables (%u objects, %u strings, %u arrays%s) need %u bytes, "
                    "but it has %u; nothing is loaded",
                    who, objects, strings, arrays, values ? ", the globals' initial values" : "",
                    tables_end, size);
        return false;
    }
    for (u32 object = 0; object < objects; object++) {
        const u8* record = b + VM_HEADER_SIZE + object * VM_OBJECT_SIZE;
        if (le16(record + 6)) {
            SERVAL_WARN("%s: object %u's reserved field is not 0; nothing is loaded", who, object);
            return false;
        }
        for (u32 event = 0; event < VM_EV_COUNT; event++) {
            u32 offset = le32(record + 8 + event * 4);
            if (offset && (offset < tables_end || offset >= size)) {
                SERVAL_WARN("%s: object %u's handler %u is at 0x%x, outside the code (0x%x to "
                            "0x%x); nothing is loaded",
                            who, object, event, offset, tables_end, size);
                return false;
            }
        }
    }
    for (u32 n = 0; n < arrays; n++) {
        const u8* record = b + arrays_at + n * VM_ARRAY_RECORD_SIZE;
        u32 length = le16(record), kind = record[2], where = le32(record + 4);
        if (kind >= VM_ARRAY_KIND_COUNT || record[3]) {
            SERVAL_WARN("%s: array %u has kind %u and reserved byte %u (kinds: 0 RAM, 1 s8, 2 u8, "
                        "3 s16, 4 u16, 5 s32; the reserved byte must be 0); nothing is loaded",
                        who, n, kind, (u32)record[3]);
            return false;
        }
        if (kind == VM_ARRAY_RAM ? where > VM_ARRAY_CELLS || length > VM_ARRAY_CELLS - where
                                 : where < tables_end || where > size ||
                                       length * element_size[kind] > size - where) {
            SERVAL_WARN("%s: array %u (%u elements at %u) is outside %s; nothing is loaded", who, n,
                        length, where,
                        kind == VM_ARRAY_RAM ? "the RAM arrays' pool (VM_ARRAY_CELLS cells)"
                                             : "the blob's data past its tables");
            return false;
        }
    }
    const u8* string_table = b + VM_HEADER_SIZE + objects * VM_OBJECT_SIZE;
    // A string ends inside the blob if it starts before `ended`, just past the
    // blob's last NUL (tables_end if it has none past the tables).
    u32 ended = size;
    while (strings && ended > tables_end && b[ended - 1] != 0)
        ended--;
    for (u32 index = 0; index < strings; index++) {
        u32 offset = le32(string_table + index * 4);
        if (offset < tables_end || offset >= size) {
            SERVAL_WARN("%s: string %u is at 0x%x, outside the blob's data (0x%x to 0x%x); "
                        "nothing is loaded",
                        who, index, offset, tables_end, size);
            return false;
        }
        if (offset >= ended) {
            SERVAL_WARN("%s: string %u (at 0x%x) has no NUL before the end of the blob; nothing "
                        "is loaded",
                        who, index, offset);
            return false;
        }
    }
    return true;
}

// A fingerprint of a valid blob's RAM array layout (the array count, each
// array's kind, and each RAM array's length and first cell): vm_reload keeps
// the pool's cells when it is unchanged. Kept as a number, not read back from
// the old blob, which a hot reload may already have overwritten.
static u32 layout_of(const u8* b) {
    u32 arrays = le16(b + 14);
    const u8* table = b + VM_HEADER_SIZE + le16(b + 8) * VM_OBJECT_SIZE + le16(b + 10) * 4;
    u32 hash = 2166136261u; // FNV-1a, over the fields' bytes
#define MIX(byte) hash = (hash ^ (u8)(byte)) * 16777619u
    MIX(arrays);
    MIX(arrays >> 8);
    for (u32 n = 0; n < arrays; n++) {
        const u8* record = table + n * VM_ARRAY_RECORD_SIZE;
        MIX(record[2]); // the kind; a RAM array's whole record (length, first cell)
        for (u32 k = 0; record[2] == VM_ARRAY_RAM && k < VM_ARRAY_RECORD_SIZE; k++)
            MIX(record[k]);
    }
#undef MIX
    return hash;
}

// Makes b (NULL: none) the loaded blob: halts every context, empties the
// queue and detaches every entity, except, when hot reloading (keep), those
// attached to objects b still has, which keep their instance fields. Leaves
// the globals and the RAM arrays alone.
static void install(const u8* b, u32 size, bool keep) {
    u32 objects = b ? le16(b + 8) : 0;
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        contexts[k].r.state = CTX_FREE;
        contexts[k].stacked = false;
    }
    queue_head = queue_count = 0;
    for (u32 slot = 0; slot < MAX_ENT; slot++) {
        bound_context[slot] = 0;
        // A binding kept by a hot reload lost its queued Create with the queue:
        // its Step reaction must not wait for it.
        bound_flags[slot] &= (u8)~BIND_CREATE_PENDING;
        if (!(keep && attached(slot) && bound_object[slot] < objects)) {
            bound[slot] = ENTITY_NONE;
            clear_fields(slot);
        }
    }
    reset_warnings();
    blob = b;
    blob_size = b ? size : 0;
    object_count = objects;
    string_count = b ? le16(b + 10) : 0;
    global_count = b ? le16(b + 12) : 0;
    array_count = b ? le16(b + 14) : 0;
    array_table = VM_HEADER_SIZE + object_count * VM_OBJECT_SIZE + string_count * 4;
}

// True (warning) if a phase (or vm_kill's Destroy logic) is running: vm_load,
// vm_reload and vm_unload would pull the blob, contexts and queue from under
// the running script. Scripts can't call them in v1 (no SYS call leads there),
// so only game C code run from inside vm_step, vm_events or vm_kill could; it
// is refused.
static bool loading_in_phase(const char* who) {
    (void)who; // only for warnings
    if (!in_phase)
        return false;
    WARN_ONCE(WARN_LOAD_IN_PHASE,
              "%s: called during vm_step, vm_events or vm_kill; ignored (load between frames)",
              who);
    return true;
}

// vm_load and vm_reload (keep: hot reload). An invalid blob unloads.
static bool load(const u8* b, u32 size, bool keep, const char* who) {
    if (loading_in_phase(who))
        return false;
    if (!valid_blob(b, size, who)) {
        install(NULL, 0, false);
        return false;
    }
    u32 used_globals = le16(b + 12);
    if (!(keep && blob && used_globals == global_count)) {
        if (keep && blob)
            SERVAL_WARN("%s: the new blob uses %u globals, the old one %u; globals start again "
                        "(at their initial values, else 0)",
                        who, used_globals, global_count);
        // The initial values follow the array table (valid_blob checked
        // they are inside the blob).
        const u8* values = b + VM_HEADER_SIZE + le16(b + 8) * VM_OBJECT_SIZE + le16(b + 10) * 4 +
                           le16(b + 14) * VM_ARRAY_RECORD_SIZE;
        bool initial = le16(b + 6) & VM_FLAG_GLOBAL_VALUES;
        for (u32 k = 0; k < VM_GLOBALS; k++)
            globals[k] = initial && k < used_globals ? (s32)le32(values + k * VM_CELL_BYTES) : 0;
    }
    u32 layout = layout_of(b);
    if (!(keep && blob && layout == ram_layout)) {
        if (keep && blob)
            SERVAL_WARN("%s: the new blob's RAM arrays are laid out differently; their cells are "
                        "zeroed",
                        who);
        for (u32 k = 0; k < VM_ARRAY_CELLS; k++)
            array_cells[k] = 0;
    }
    install(b, size, keep);
    ram_layout = layout;
    return true;
}

bool vm_load(const u8* b, u32 size) {
    return load(b, size, false, "vm_load");
}

bool vm_reload(const u8* b, u32 size) {
    return load(b, size, true, "vm_reload");
}

void vm_unload(void) {
    if (!loading_in_phase("vm_unload"))
        install(NULL, 0, false);
}

void vm_bind(const VmBindings* b) {
    if (b) {
        bindings = *b;
    } else {
        bindings = (VmBindings){0};
    }
}

// --- Inspecting --------------------------------------------------------------

s32 vm_global(u16 index) {
    if (index >= VM_GLOBALS) {
        WARN_ONCE(WARN_GLOBAL, "vm_global: no global %u (0 to %d); returns 0", (u32)index,
                  VM_GLOBALS - 1);
        return 0;
    }
    return globals[index];
}

void vm_set_global(u16 index, s32 value) {
    if (index >= VM_GLOBALS) {
        WARN_ONCE(WARN_GLOBAL, "vm_set_global: no global %u (0 to %d); ignored", (u32)index,
                  VM_GLOBALS - 1);
        return;
    }
    globals[index] = value;
}

int serval_vm_attached_object(Entity e) {
    u32 slot = entity_index(e);
    if (e == ENTITY_NONE || slot >= MAX_ENT || bound[slot] != e || !entity_alive(e))
        return -1;
    return bound_object[slot];
}

s32 serval_vm_field(Entity e, u32 n) {
    if (n >= VM_FIELDS || serval_vm_attached_object(e) < 0)
        return 0;
    return fields[entity_index(e)][n];
}

s32 serval_vm_array_cell(u32 n) {
    return n < VM_ARRAY_CELLS ? array_cells[n] : 0;
}

u32 vm_ops_this_frame(void) {
    return ops_frame;
}

bool vm_idle(void) {
    if (queue_count)
        return false;
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        if (contexts[k].r.state != CTX_FREE)
            return false;
    }
    return true;
}
