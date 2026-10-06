// The bytecode VM (vm.h; the format and every rule are in docs/vm.md): the
// blob loader, the interpreter, the scheduler (contexts, waits, Step
// handlers, the event queue) and the bridge to entities and engine calls.
// Portable: the engine calls only the GBA build has (sound, music, text,
// buttons, brightness) go through serval_vm_platform_call (vm_internal.h).
//
// vm_step() runs, in this order: the resume pass, Animation End (queued for
// animations finished since the last check), a drain of the event queue, the
// Step handlers, and a second drain. A binding's Create-pending flag keeps an
// entity's Step handler from running before its Create was drained.
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

typedef struct {
    u32 pc;              // blob offset of the next opcode
    Entity self;         // bound entity, or ENTITY_NONE for a thread (vm_start)
    Entity other;        // the event's other entity, else ENTITY_NONE
    u8 state;            // CTX_*
    u8 event;            // the VM_EV_* handler it runs
    u16 wait_frames;     // CTX_WAIT_FRAMES: resume passes left
    u8 sp, cp;           // value and call stack depths
    s32 stack[VM_STACK]; // value stack
    u32 calls[VM_CALLS]; // return offsets
    s32 loc[VM_LOCALS];  // zeroed when the context starts
} Context;

typedef struct {
    Entity e, other;
    u8 event;
} QueuedEvent;

SERVAL_EWRAM_BSS static Context contexts[VM_CONTEXTS];
SERVAL_EWRAM_BSS static s32 globals[VM_GLOBALS];
SERVAL_EWRAM_BSS static QueuedEvent queue[VM_EVENT_QUEUE];
// Bindings, by entity slot: the attached entity's handle (ENTITY_NONE: none),
// its object, its live context + 1 (0: none) and its BIND_* flags.
SERVAL_EWRAM_BSS static Entity bound[MAX_ENT];
SERVAL_EWRAM_BSS static u16 bound_object[MAX_ENT];
SERVAL_EWRAM_BSS static u8 bound_context[MAX_ENT];
SERVAL_EWRAM_BSS static u8 bound_flags[MAX_ENT];

enum {
    BIND_CREATE_PENDING = 1, // its Create is queued: no Step handler yet
    BIND_ANIM_DONE = 2,      // anim_finished() at the latest Animation End check
};

static const u8* blob; // the loaded blob; NULL: none
static u32 blob_size;
static u32 object_count, string_count, global_count;
static VmBindings bindings;
static u32 queue_head, queue_count;
static u32 ops_frame; // vm_ops_this_frame()
static bool in_phase; // inside vm_step() or vm_events()

_Static_assert(VM_CONTEXTS < 255, "bound_context holds a context index + 1 in a u8");
_Static_assert(MAX_ENT <= 256, "an entity slot fits a handle's low byte");
_Static_assert(VM_GLOBALS >= 256, "LDG and STG take any u8 global index");

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
    WARN_DESTROY_BUDGET,
    WARN_LOCAL,
    WARN_SELF,
    WARN_WAIT_ANIM,
    WARN_DESTROY_WAIT,
    WARN_PROPERTY,
    WARN_PROP_ENTITY,
    WARN_PROP_COMPONENT,
    WARN_SPAWN_OBJECT,
    WARN_SPAWN_FULL,
    WARN_KILL_NONE,
    WARN_SYS,
    WARN_SONG,
    WARN_PATH,
    WARN_STRING,
    WARN_NO_CONTEXT,
    WARN_QUEUE_FULL,
    WARN_DROPPED,
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
    WARN_GLOBAL,
    WARN_VM_KILL_NONE,
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

// Frees the context and clears its entity's link to it.
static void halt(Context* c) {
    if (c->self != ENTITY_NONE) {
        u32 slot = entity_index(c->self);
        if (slot < MAX_ENT && bound_context[slot] == context_index(c) + 1)
            bound_context[slot] = 0;
    }
    c->state = CTX_FREE;
}

// Detaches slot's entity, halting its live context.
static void unbind(u32 slot) {
    u32 live = bound_context[slot];
    bound_context[slot] = 0;
    bound[slot] = ENTITY_NONE;
    if (live)
        contexts[live - 1].state = CTX_FREE;
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
        if (c->state != CTX_FREE)
            continue;
        c->pc = pc;
        c->self = self;
        c->other = other;
        c->state = (u8)state;
        c->event = (u8)event;
        c->wait_frames = 0;
        c->sp = c->cp = 0;
        for (u32 n = 0; n < VM_LOCALS; n++)
            c->loc[n] = 0;
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

// Attaches a live entity to a valid object and queues its Create. Its Step
// handler waits until that Create is drained; a Create the full queue drops
// is never drained, so it doesn't hold Step back.
static void bind(Entity e, u32 object) {
    u32 slot = entity_index(e);
    if (attached(slot))
        unbind(slot); // rebinding: its live context is halted first
    bound[slot] = e;
    bound_object[slot] = (u16)object;
    bound_context[slot] = 0;
    bound_flags[slot] = enqueue(e, ENTITY_NONE, VM_EV_CREATE) ? BIND_CREATE_PENDING : 0;
}

// --- Entities ----------------------------------------------------------------

static const u32 prop_component[VM_P_COUNT] = {C_POS, C_POS, C_VEL, C_VEL, C_SPR,  C_SPR,
                                               C_SPR, C_SPR, C_SPR, C_SPR, C_BODY, C_BODY};

// The slot of the entity in `cell` for GETP or SETP of `prop`, or -1 (warning)
// for an unknown property or a dead entity.
static int prop_slot(u32 prop, s32 cell, u32 at) {
    (void)at; // only for warnings
    if (prop >= VM_P_COUNT) {
        WARN_ONCE(WARN_PROPERTY,
                  "vm: GETP/SETP at 0x%x: no property %u (VM_P_X to VM_P_BODY_H, 0 to %d); "
                  "reads 0, writes nothing",
                  at, prop, VM_P_COUNT - 1);
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
    if (!ent_has(i, prop_component[prop]))
        WARN_ONCE(WARN_PROP_COMPONENT,
                  "vm: GETP/SETP at 0x%x: entity %u lacks the component of property %u "
                  "(C_POS for X/Y, C_VEL for VX/VY, C_BODY for BODY_W/H, else C_SPR); its "
                  "array is used anyway",
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
    default: // VM_P_BODY_H
        return body_h[i];
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
    default: // VM_P_BODY_H
        body_h[i] = (u8)value;
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

// --- Engine calls ------------------------------------------------------------

// Arguments per VM_SYS_* call, and the calls that push a result.
static const u8 sys_arity[VM_SYS_COUNT] = {1, 1, 0, 0, 0, 2, 3, 2, 1, 1, 1, 3, 3};
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
    case VM_SYS_MUSIC_PLAY: {
        s32 index = args[0];
        const PsgSong* song = NULL;
        if (serval_plausible_pointer(bindings.songs) && index >= 0 &&
            (u32)index < bindings.song_count)
            song = bindings.songs[index];
        if (!serval_plausible_pointer(song)) {
            WARN_ONCE(WARN_SONG,
                      "vm: SYS music_play: song %d is not bound (%u songs; vm_bind sets them); "
                      "not played",
                      (int)index, (u32)bindings.song_count);
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

// --- Interpreter -------------------------------------------------------------

// Runs the context until it halts or waits, or until it has run
// VM_OPS_PER_SLICE ops. `must_finish`: a Destroy handler run by the Destroy
// logic, which must not wait (a wait warns and halts it). Returns the number
// of ops run.
static u32 execute(Context* c, bool must_finish) {
    const u8* const code = blob;
    const u32 size = blob_size;
    s32* const st = c->stack;
    u32 pc = c->pc;
    u32 sp = c->sp;
    u32 ops = 0;
    u32 at = pc; // the current op's offset, for warnings
    (void)at;    // (release builds have none)
    c->state = CTX_RUNNING;

// pc is at most size here (the opcode at pc - 1 was inside the blob).
#define OPERAND(n)                                                                                 \
    if ((n) > size - pc)                                                                           \
    goto escaped
#define NEED(n)                                                                                    \
    if (sp < (n))                                                                                  \
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
            goto halted;
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
        case VM_OP_LDL: {
            OPERAND(1);
            ROOM(1);
            u32 n = code[pc];
            pc += 1;
            if (n >= VM_LOCALS) {
                WARN_ONCE(WARN_LOCAL, "vm: LDL at 0x%x: no local %u (0 to %d); pushes 0", at, n,
                          VM_LOCALS - 1);
                st[sp++] = 0;
            } else {
                st[sp++] = c->loc[n];
            }
            break;
        }
        case VM_OP_STL: {
            OPERAND(1);
            NEED(1);
            u32 n = code[pc];
            pc += 1;
            s32 value = st[--sp];
            if (n >= VM_LOCALS)
                WARN_ONCE(WARN_LOCAL, "vm: STL at 0x%x: no local %u (0 to %d); value dropped", at,
                          n, VM_LOCALS - 1);
            else
                c->loc[n] = value;
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
        case VM_OP_FXDIV: {
            NEED(2);
            s32 b = st[--sp], a = st[sp - 1];
            s32 r;
            if (b == 0) {
                WARN_ONCE(WARN_DIV_ZERO,
                          "vm: division by zero at 0x%x (DIV, MOD or FXDIV); gives 0", at);
                r = 0;
            } else if (op == VM_OP_FXDIV) {
                r = (s32)(u32)(uint64_t)((int64_t)a * 256 / b);
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
        case VM_OP_CALL:
            OPERAND(4);
            if (c->cp == VM_CALLS)
                goto call_depth;
            c->calls[c->cp++] = pc + 4;
            pc = le32(code + pc);
            break;
        case VM_OP_RET:
            if (c->cp == 0)
                goto halted; // returning from the handler itself
            pc = c->calls[--c->cp];
            break;

        case VM_OP_WAIT: {
            NEED(1);
            s32 n = st[--sp];
            if (n <= 0)
                break;
            if (must_finish)
                goto destroy_wait;
            c->wait_frames = n > 0xFFFF ? 0xFFFF : (u16)n;
            c->state = CTX_WAIT_FRAMES;
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
            if (must_finish)
                goto destroy_wait;
            c->state = CTX_WAIT_ANIM;
            goto suspend;
        case VM_OP_WAIT_MOVE:
            if (!path_active(c->self))
                break;
            if (must_finish)
                goto destroy_wait;
            c->state = CTX_WAIT_MOVE;
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
            st[sp++] = c->other;
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
            if (c->state != CTX_RUNNING)
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

        case VM_OP_SYS: {
            OPERAND(1);
            u32 fn = code[pc];
            pc += 1;
            if (fn >= VM_SYS_COUNT) {
                WARN_ONCE(WARN_SYS, "vm: SYS at 0x%x: no engine call %u (0 to %d); halted", at, fn,
                          VM_SYS_COUNT - 1);
                goto halted;
            }
            u32 n = sys_arity[fn];
            NEED(n);
            s32 args[3] = {0, 0, 0};
            sp -= n;
            for (u32 k = 0; k < n; k++)
                args[k] = st[sp + k];
            s32 result = sys_call(fn, args);
            if (c->state != CTX_RUNNING)
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
            if (sp)
                debug_log(text_format("vm: %s %d", s, (int)st[sp - 1]));
            else
                debug_log(text_format("vm: %s", s));
#endif
            pc += 2;
            break;
        }

        default:
            WARN_ONCE(WARN_UNKNOWN_OP, "vm: unknown opcode 0x%x at 0x%x; halted", op, at);
            goto halted;
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
    goto halted;
overflow:
    WARN_ONCE(WARN_OVERFLOW, "vm: stack overflow at 0x%x (more than %d cells); halted", at,
              VM_STACK);
    goto halted;
underflow:
    WARN_ONCE(WARN_UNDERFLOW, "vm: stack underflow at 0x%x (opcode 0x%x needs more cells); halted",
              at, (u32)code[at]);
    goto halted;
call_depth:
    WARN_ONCE(WARN_CALL_DEPTH, "vm: CALL at 0x%x is nested more than %d deep; halted", at,
              VM_CALLS);
    goto halted;
destroy_wait:
    WARN_ONCE(WARN_DESTROY_WAIT,
              "vm: a Destroy handler waits at 0x%x; it must run to completion, so it is halted",
              at);
    goto halted;
budget: // the op at `at` is not run
#ifdef SERVAL_DEBUG
    if (first_warning(must_finish ? WARN_DESTROY_BUDGET : WARN_BUDGET))
        SERVAL_WARN("vm: a %s handler ran %d ops in one phase (an endless loop?); it %s at 0x%x",
                    event_names[c->event], VM_OPS_PER_SLICE,
                    must_finish ? "is halted (Destroy can't wait)" : "waits a frame", at);
#endif
    if (must_finish)
        goto halted;
    c->wait_frames = 1;
    c->state = CTX_WAIT_FRAMES;
    goto suspend;
suspend:
    c->pc = pc;
    c->sp = (u8)sp;
    return ops;
halted:
    halt(c);
    return ops;
}

static void run(Context* c, bool must_finish) {
    ops_frame += execute(c, must_finish);
}

// --- Scheduler ---------------------------------------------------------------

// The Destroy logic (KILL, vm_kill): runs e's Destroy handler to completion
// if it is attached, then destroys it.
static void destroy(Entity e) {
    if (!entity_alive(e))
        return;
    u32 slot = entity_index(e);
    if (attached(slot)) {
        u32 live = bound_context[slot];
        if (live)
            halt(&contexts[live - 1]);
        u32 handler = handler_of(bound_object[slot], VM_EV_DESTROY);
        if (handler) {
            Context* c = start_context(handler, e, ENTITY_NONE, VM_EV_DESTROY, CTX_RUNNING);
            if (c)
                run(c, true);
        }
        unbind(slot);
    }
    entity_destroy(e);
}

// Runs a queued event's handler for e (any event but Destroy).
static void dispatch(Entity e, Entity other, u32 event) {
    if (!entity_alive(e))
        return;
    u32 slot = entity_index(e);
    if (!attached(slot))
        return;
    // Drained, whether it runs, has no handler or is dropped below: the
    // entity's Step handler may run from now on.
    if (event == VM_EV_CREATE)
        bound_flags[slot] &= (u8)~BIND_CREATE_PENDING;
    u32 handler = handler_of(bound_object[slot], event);
    if (!handler)
        return;
    if (bound_context[slot]) {
#ifdef SERVAL_DEBUG
        WARN_ONCE(WARN_DROPPED,
                  "vm: entity %u's script is still running (or waiting), so its %s event is "
                  "dropped (one script per entity)",
                  slot, event_names[event]);
#endif
        return;
    }
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

void vm_step(void) {
    ops_frame = 0;
    in_phase = true;
    // Resume pass: each context once, in pool order.
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        Context* c = &contexts[k];
        if (c->state == CTX_FREE)
            continue;
        if (c->self != ENTITY_NONE && !entity_alive(c->self)) {
            u32 slot = entity_index(c->self);
            if (slot < MAX_ENT && bound[slot] == c->self)
                attached(slot); // a stale binding: warns, halts its context
            halt(c);
            continue;
        }
        bool resume;
        switch (c->state) {
        case CTX_READY:
            resume = true;
            break;
        case CTX_WAIT_FRAMES:
            resume = --c->wait_frames == 0;
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
                          c->pc - 1);
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
    // Step handlers, in entity order, for attached entities with no live
    // context whose Create has been drained.
    for (u32 slot = 0; slot < MAX_ENT; slot++) {
        if (!attached(slot) || bound_context[slot] || (bound_flags[slot] & BIND_CREATE_PENDING))
            continue;
        u32 handler = handler_of(bound_object[slot], VM_EV_STEP);
        if (!handler)
            continue;
        Context* c = start_context(handler, bound[slot], ENTITY_NONE, VM_EV_STEP, CTX_RUNNING);
        if (c)
            run(c, false);
    }
    drain(); // events the Step handlers queued, e.g. the Create of an entity they spawned
    in_phase = false;
}

void vm_events(void) {
    in_phase = true;
    drain();
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
    if (in_phase)
        enqueue(e, ENTITY_NONE, VM_EV_DESTROY);
    else
        destroy(e);
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
    // Destroy is exactly KILL: queued, `other` unused (the handler's OTHER is
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
    u32 objects = le16(b + 8), strings = le16(b + 10), used_globals = le16(b + 12);
    if (used_globals > VM_GLOBALS) {
        SERVAL_WARN("%s: the blob uses %u globals; the most is %d; nothing is loaded", who,
                    used_globals, VM_GLOBALS);
        return false;
    }
    u32 tables_end = VM_HEADER_SIZE + objects * VM_OBJECT_SIZE + strings * 4;
    if (tables_end > size) {
        SERVAL_WARN("%s: the blob's tables (%u objects, %u strings) need %u bytes, but it has %u; "
                    "nothing is loaded",
                    who, objects, strings, tables_end, size);
        return false;
    }
    for (u32 object = 0; object < objects; object++) {
        const u8* record = b + VM_HEADER_SIZE + object * VM_OBJECT_SIZE;
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

// Makes b (NULL: none) the loaded blob: halts every context, empties the
// queue and detaches every entity, except, when hot reloading (keep), those
// attached to objects b still has. Leaves the globals alone.
static void install(const u8* b, u32 size, bool keep) {
    u32 objects = b ? le16(b + 8) : 0;
    for (u32 k = 0; k < VM_CONTEXTS; k++)
        contexts[k].state = CTX_FREE;
    queue_head = queue_count = 0;
    for (u32 slot = 0; slot < MAX_ENT; slot++) {
        bound_context[slot] = 0;
        // A binding kept by a hot reload lost its queued Create with the queue:
        // its Step handler must not wait for it.
        bound_flags[slot] &= (u8)~BIND_CREATE_PENDING;
        if (!(keep && attached(slot) && bound_object[slot] < objects))
            bound[slot] = ENTITY_NONE;
    }
    reset_warnings();
    blob = b;
    blob_size = b ? size : 0;
    object_count = objects;
    string_count = b ? le16(b + 10) : 0;
    global_count = b ? le16(b + 12) : 0;
}

// True (warning) if a phase is running: vm_load, vm_reload and vm_unload
// would pull the blob, contexts and queue from under the running script.
// Scripts can't call them in v1 (no SYS call leads there), so only game C code
// run from inside vm_step or vm_events could; it is refused.
static bool loading_in_phase(const char* who) {
    (void)who; // only for warnings
    if (!in_phase)
        return false;
    WARN_ONCE(WARN_LOAD_IN_PHASE,
              "%s: called during vm_step or vm_events; ignored (load between frames)", who);
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
            SERVAL_WARN("%s: the new blob uses %u globals, the old one %u; globals are zeroed", who,
                        used_globals, global_count);
        for (u32 k = 0; k < VM_GLOBALS; k++)
            globals[k] = 0;
    }
    install(b, size, keep);
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

u32 vm_ops_this_frame(void) {
    return ops_frame;
}

bool vm_idle(void) {
    if (queue_count)
        return false;
    for (u32 k = 0; k < VM_CONTEXTS; k++) {
        if (contexts[k].state != CTX_FREE)
            return false;
    }
    return true;
}
