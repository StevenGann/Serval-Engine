// The debug link's game side (docs/debug-link.md): the agent's core, in debug
// builds with the agent (SERVAL_LINK_AGENT: SERVAL_DEBUG and the CMake option
// SERVAL_LINK, on by default), and debug_break(). Portable: built for the
// GBA, the web and the host (src/core/link_internal.h says how the agent is
// split and what each platform provides).
//
// The agent answers the debugger in the VM's and the ECS's vocabulary
// through a mailbox in EWRAM (mailbox v1) that the endpoint (the debugger's
// mGBA fork, or the game's web page) reads and writes, and a doorbell the
// agent rings after writing to it. It is dormant (no frame hook, nothing
// written but the presence register) unless an endpoint answered in
// serval_init(). Started, it reads its inbox once a frame at the end of
// frame_end(), and parks the whole game there for PAUSE and frame steps.
// Implemented here (steps E5 and E7 of docs/debug-link.md#implementation):
// AGENT_INFO, PAUSE, RESUME, GLOBALS, SET_GLOBAL, ARRAY_CELLS,
// SET_ARRAY_CELL, ENTITIES, ENTITY, SET_PROPERTY, STATS_SUBSCRIBE, OPTIONS,
// and the events STOPPED, STATS and LOADED. The VM's part (E6, vm_link.c)
// adds BRK's park, breakpoints, stepping, CONTEXTS, FRAMES, SET_LOCAL and
// faults, through serval_link_stop(), which parks the game inside a frame;
// and (E9) hot reload: RELOAD_BEGIN, RELOAD_DATA, RELOAD_PLAN, RELOAD_COMMIT
// and RELOADED, the game parked at a frame boundary.
//
// debug_break() (docs/debug-link.md#script-breakpoints): parks the game while
// a debugger is attached (STOPPED, debug_break, no context); otherwise, and
// in release builds, it does nothing.

#include "serval/debug.h"

#include "link_internal.h"

#ifdef SERVAL_LINK_AGENT
#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "serval/vm.h"
#endif

void debug_break(void) {
#ifdef SERVAL_LINK_AGENT
    serval_link_stop(LINK_STOP_DEBUG_BREAK, 0xFF, 0, 0);
#endif
}

#ifdef SERVAL_LINK_AGENT

#ifndef SERVAL_LINK_ENGINE_VERSION
#error "SERVAL_LINK_ENGINE_VERSION (serval.json's version) is set by CMakeLists.txt"
#endif

// --- State ---------------------------------------------------------------------
//
// Everything in EWRAM: the agent adds no IWRAM (docs/debug-link.md).

SERVAL_EWRAM_BSS ServalLinkMailbox serval_link_mailbox;
SERVAL_EWRAM_BSS void (*serval_link_hook)(void);
SERVAL_EWRAM_BSS const ServalLinkEcs* serval_link_ecs;
SERVAL_EWRAM_BSS const char* serval_link_error_text;

typedef struct {
    bool started;     // serval_link_init() found an endpoint
    bool attached;    // AGENT_INFO answered, the endpoint still holding SVLE
    bool parked;      // parked at a frame boundary (or about to be)
    u8 stop;          // a STOPPED reason + 1 to send after the current reply, else 0
    u8 next_boundary; // RESUME mode 1: a STOPPED reason + 1 for the next boundary, else 0
    u16 stats_every;  // STATS_SUBSCRIBE's frames (0: off)
    u16 stats_count;  // frames since the last STATS
    u32 options;      // OPTIONS' flags (bit 0: stop on fault)
    u32 queue_peak;   // events queued at once, at most, since the last STATS
    u32 dropped;      // frames dropped: the endpoint left no room in the outbox
    bool inside;      // debug link (E9): parked inside a frame (RELOAD_BEGIN needs a boundary)
} Agent;
SERVAL_EWRAM_BSS static Agent agent;

// The VM part, or NULL in a game that doesn't link the VM (link_internal.h).
static const ServalLinkVmPart* vm_part(void) {
    return &serval_link_vm ? serval_link_vm : NULL;
}

static bool endpoint_connected(void) {
    return serval_link_mailbox.endpoint == LINK_ENDPOINT_MAGIC;
}

// --- Reading requests, writing replies --------------------------------------------

u32 serval_link_u8(LinkReader* r) {
    if (r->n - r->at < 1) {
        r->ok = false;
        return 0;
    }
    return r->p[r->at++];
}

u32 serval_link_u16(LinkReader* r) {
    u32 lo = serval_link_u8(r);
    return lo | serval_link_u8(r) << 8;
}

u32 serval_link_u32(LinkReader* r) {
    u32 lo = serval_link_u16(r);
    return lo | serval_link_u16(r) << 16;
}

void serval_link_put8(LinkWriter* w, u32 v) {
    if (w->n < w->room)
        w->p[w->n++] = (u8)v;
    else
        w->full = true;
}

void serval_link_put16(LinkWriter* w, u32 v) {
    serval_link_put8(w, v);
    serval_link_put8(w, v >> 8);
}

void serval_link_put32(LinkWriter* w, u32 v) {
    serval_link_put16(w, v);
    serval_link_put16(w, v >> 16);
}

// A str: a u8 length, then the bytes (cut at 255).
void serval_link_put_str(LinkWriter* w, const char* s) {
    u32 n = 0;
    while (s[n] && n < 255)
        n++;
    serval_link_put8(w, n);
    for (u32 i = 0; i < n; i++)
        serval_link_put8(w, (u8)s[i]);
}

u32 serval_link_fnv1a(const u8* bytes, u32 size) {
    u32 hash = 0x811C9DC5u;
    for (u32 i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 0x01000193u;
    return hash;
}

// --- The outbox and the doorbell ----------------------------------------------------

// Rings with `state` in the mailbox: what the agent does after the ring.
static void ring(u32 state) {
    serval_link_mailbox.state = (u8)state;
    serval_link_platform_ring(&serval_link_mailbox);
}

// Starts a frame in the outbox: its payload goes in w. Rings first when a
// whole frame might not fit (the endpoint sends and empties the outbox
// during the ring); false (the frame dropped) if it still might not.
// A ring here, while the inbox may still be being read, says "running": an
// endpoint that pauses the machine for a parked agent must not pause it
// before the agent has read its inbox.
static bool frame_open(u32 type, u32 flags, u32 seq, LinkWriter* w) {
    ServalLinkMailbox* m = &serval_link_mailbox;
    if (m->out_len > LINK_OUT_SIZE - LINK_FRAME_MAX) {
        ring(LINK_STATE_RUNNING);
        if (m->out_len > LINK_OUT_SIZE - LINK_FRAME_MAX) {
            agent.dropped++;
            return false;
        }
    }
    u8* f = m->out + m->out_len;
    f[2] = (u8)type;
    f[3] = (u8)flags;
    f[4] = (u8)seq;
    f[5] = (u8)(seq >> 8);
    *w = (LinkWriter){.p = f + LINK_HEADER, .n = 0, .room = LINK_PAYLOAD_MAX, .full = false};
    return true;
}

// Ends the frame w holds: its length, then the outbox's.
static void frame_close(LinkWriter* w) {
    u8* f = w->p - LINK_HEADER;
    u32 length = 4 + w->n;
    f[0] = (u8)length;
    f[1] = (u8)(length >> 8);
    serval_link_mailbox.out_len = (u16)(serval_link_mailbox.out_len + 2 + length);
}

void serval_link_event(u32 type, void (*fill)(LinkWriter* w, const void* arg), const void* arg) {
    LinkWriter w;
    if (!frame_open(type, 0, 0, &w))
        return;
    fill(&w, arg);
    frame_close(&w);
}

// --- Events --------------------------------------------------------------------

// STOPPED: reason, context (0xFF: none, at a frame boundary), pc (0: none),
// the frame, fault (0: none).
typedef struct {
    u32 reason, context, pc, fault;
} Stop;

static void fill_stopped(LinkWriter* w, const void* arg) {
    const Stop* stop = arg;
    ServalLinkFrameStats s;
    serval_link_platform_stats(&s);
    serval_link_put8(w, stop->reason);
    serval_link_put8(w, stop->context);
    serval_link_put32(w, stop->pc);
    serval_link_put32(w, s.frame);
    serval_link_put8(w, stop->fault);
}

// Every stop ends a step: its sites are cleared (the VM's part).
static void send_stopped_at(u32 reason, u32 context, u32 pc, u32 fault) {
    const ServalLinkVmPart* vm = vm_part();
    if (vm)
        vm->stopped();
    Stop stop = {reason, context, pc, fault};
    serval_link_event(LINK_STOPPED, fill_stopped, &stop);
}

static void send_stopped(u32 reason) {
    send_stopped_at(reason, 0xFF, 0, 0);
}

// STATS (docs/debug-link.md#messages-protocol-10). vm_cycles is the cycle
// counter read around the last frame's two phases, and queue the most events
// queued at once since the last STATS, inside the phases or at a boundary.
static void fill_stats(LinkWriter* w, const void* arg) {
    (void)arg;
    ServalLinkFrameStats s;
    serval_link_platform_stats(&s);
    ServalLinkVmStats v = {0};
    const ServalLinkVmPart* vm = vm_part();
    if (vm)
        vm->stats(&v);
    serval_link_put32(w, s.frame);
    serval_link_put32(w, s.cycles);
    serval_link_put32(w, v.vm_cycles);
    serval_link_put32(w, v.ops);
    serval_link_put8(w, v.contexts);
    serval_link_put16(w, agent.queue_peak);
    serval_link_put8(w, MAX_ENT - ecs_free_count());
    serval_link_put8(w, s.drawn);
    serval_link_put8(w, s.matrices);
    serval_link_put16(w, s.dropped);
    serval_link_put32(w, s.mixer_cycles);
    serval_link_put32(w, debug_warning_count());
}

// --- The ECS part ---------------------------------------------------------------
//
// The engine properties, from and to the pools as GETP and SETP read and
// write them (vm.c's get_prop and set_prop, without the VM: C games too),
// without warnings. Only through serval_link_ecs, which entity_create() sets:
// a game that never creates an entity keeps none of the pools through them.

_Static_assert(VM_P_COUNT == 21, "a new property needs its row in link_get and link_set (and "
                                 "SET_PROPERTY's read-only check)");

#define TAG_SHIFT 16
#define TAG_BITS 0x7FFFu

static s32 link_get(u32 i, u32 prop) {
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
    case VM_P_BODY_BOUNCE:
        return body_bounce[i];
    case VM_P_BODY_FRICTION:
        return body_friction[i];
    case VM_P_BODY_MAX_FALL:
        return body_max_fall[i];
    case VM_P_BODY_GRAVITY:
        return body_gravity[i];
    case VM_P_BODY_CONTACT:
        return body_contact[i];
    default: // VM_P_OBJECT: the agent's own (the VM's part)
        return -1;
    }
}

static void link_set(u32 i, u32 prop, s32 value) {
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
    case VM_P_BODY_BOUNCE:
        body_bounce[i] = (u8)value;
        break;
    case VM_P_BODY_FRICTION:
        body_friction[i] = (u8)value;
        break;
    case VM_P_BODY_MAX_FALL:
        body_max_fall[i] = (u16)value;
        break;
    case VM_P_BODY_GRAVITY:
        body_gravity[i] = (s8)value;
        break;
    default: // read-only (VM_P_BODY_CONTACT, VM_P_OBJECT): refused before
        break;
    }
}

const ServalLinkEcs serval_link_ecs_part = {link_get, link_set};

// --- Requests --------------------------------------------------------------------

static u32 fail(u32 code, const char* text) {
    serval_link_error_text = text;
    return code;
}

#define SHORT() fail(LINK_BAD_REQUEST, "the payload is too short")

static void attach(void) {
    agent.attached = true;
}

// Leaves the debugger: no stats, no options, every patched opcode restored
// (the VM's part), the game resumed.
static void detach(void) {
    const ServalLinkVmPart* vm = vm_part();
    if (vm && agent.attached)
        vm->detached();
    agent.attached = false;
    agent.parked = false;
    agent.stop = 0;
    agent.next_boundary = 0;
    agent.stats_every = 0;
    agent.options = 0;
}

static u32 agent_info(LinkWriter* w) {
    const ServalLinkVmPart* vm = vm_part();
    ServalLinkVmInfo info = {0};
    if (vm)
        vm->info(&info);
    char title[13], game_code[5];
    serval_link_platform_rom(title, game_code);
    serval_link_put8(w, LINK_PROTO_MAJOR);
    serval_link_put8(w, LINK_PROTO_MINOR);
    serval_link_put_str(w, SERVAL_LINK_ENGINE_VERSION);
    // The VM's part's: the VM, breakpoints and stepping (with a scratch), stop
    // on fault.
    serval_link_put32(w, info.capabilities | LINK_CAP_STATS | LINK_CAP_ENTITIES);
    serval_link_put8(w, vm ? VM_CONTEXTS : 0);
    serval_link_put8(w, vm ? VM_STACK : 0);
    serval_link_put8(w, vm ? VM_CALLS : 0);
    serval_link_put16(w, vm ? VM_GLOBALS : 0);
    serval_link_put8(w, vm ? VM_FIELDS : 0);
    serval_link_put16(w, vm ? VM_ARRAY_CELLS : 0);
    serval_link_put16(w, MAX_ENT);
    serval_link_put8(w, VM_P_COUNT);
    serval_link_put16(w, info.sites);
    serval_link_put32(w, info.scratch);
    serval_link_put_str(w, title);
    serval_link_put_str(w, game_code);
    serval_link_put32(w, info.size);
    serval_link_put32(w, info.hash);
    attach();
    return 0;
}

// Parks at this frame boundary, which is where requests are read.
static u32 pause_request(void) {
    if (!agent.parked) {
        agent.parked = true;
        agent.stop = LINK_STOP_PAUSE + 1;
    }
    return 0;
}

static u32 resume_request(LinkReader* r) {
    u32 mode = serval_link_u8(r);
    if (!r->ok)
        return SHORT();
    if (mode > 1)
        return fail(LINK_BAD_REQUEST, "RESUME's mode is 0 (run) or 1 (to the next frame boundary)");
    const ServalLinkVmPart* vm = vm_part(); // debug link (E9): a reload pins the game
    if (vm && !vm->resuming(true))
        return fail(LINK_RELOADING,
                    "a reload is under way: the game stays parked until RELOAD_COMMIT");
    agent.parked = false;
    agent.next_boundary = mode == 1 ? LINK_STOP_FRAME + 1 : 0;
    return 0;
}

static u32 stats_subscribe(LinkReader* r) {
    u32 every = serval_link_u16(r);
    if (!r->ok)
        return SHORT();
    agent.stats_every = (u16)every;
    agent.stats_count = 0;
    agent.queue_peak = 0;
    return 0;
}

static u32 options(LinkReader* r) {
    u32 flags = serval_link_u32(r);
    if (!r->ok)
        return SHORT();
    agent.options = flags & 1u; // stop on fault (the VM's part)
    return 0;
}

// STEP (the VM's part sets the step up): resumes the game.
static u32 step_request(const ServalLinkVmPart* vm, LinkReader* r, LinkWriter* w) {
    if (!agent.parked)
        return fail(LINK_NOT_PARKED, "STEP needs the game parked");
    if (!vm->resuming(false)) // debug link (E9): a reload pins the game
        return fail(LINK_RELOADING,
                    "a reload is under way: the game stays parked until RELOAD_COMMIT");
    u32 code = vm->request(LINK_STEP, r, w);
    if (!code) {
        vm->resuming(true);
        agent.parked = false;
        agent.next_boundary = 0;
    }
    return code;
}

// ENTITIES: every live entity from slot `first` on, as many as fit.
static u32 entities(LinkReader* r, LinkWriter* w) {
    u32 first = serval_link_u8(r);
    if (!r->ok)
        return SHORT();
    if (first > MAX_ENT)
        return fail(LINK_BAD_REQUEST, "ENTITIES' first slot is past MAX_ENT");
    const ServalLinkVmPart* vm = vm_part();
    serval_link_put8(w, 0); // next, below
    u32 slot = first;
    for (; slot < MAX_ENT; slot++) {
        Entity e = entity_at(slot);
        if (e == ENTITY_NONE)
            continue;
        if (w->room - w->n < 9)
            break;
        int object = vm ? vm->object_of(e) : -1;
        int context = vm ? vm->context_of(e) : -1;
        serval_link_put16(w, e);
        serval_link_put32(w, ent_mask[slot]);
        serval_link_put16(w, (u32)object);
        serval_link_put8(w, (u32)context);
    }
    w->p[0] = (u8)slot;
    return 0;
}

static u32 entity(LinkReader* r, LinkWriter* w) {
    Entity e = (Entity)serval_link_u16(r);
    if (!r->ok)
        return SHORT();
    const ServalLinkEcs* ecs = serval_link_ecs;
    if (!ecs || !entity_alive(e))
        return fail(LINK_NO_SUCH, "no such entity (dead, or never created)");
    const ServalLinkVmPart* vm = vm_part();
    int object = vm ? vm->object_of(e) : -1;
    u32 slot = entity_index(e);
    serval_link_put16(w, e);
    serval_link_put16(w, (u32)object);
    serval_link_put8(w, VM_P_COUNT);
    for (u32 p = 0; p < VM_P_COUNT; p++)
        serval_link_put32(w, (u32)(p == VM_P_OBJECT ? object : ecs->get(slot, p)));
    u32 fields = object >= 0 ? VM_FIELDS : 0;
    serval_link_put8(w, fields);
    for (u32 n = 0; n < fields; n++)
        serval_link_put32(w, (u32)vm->field(e, n));
    return 0;
}

// SET_PROPERTY: SETP's rules (truncated to the pool's type; the read-only
// properties refused), without its warnings.
static u32 set_property(LinkReader* r) {
    Entity e = (Entity)serval_link_u16(r);
    u32 prop = serval_link_u8(r);
    s32 value = (s32)serval_link_u32(r);
    if (!r->ok)
        return SHORT();
    const ServalLinkEcs* ecs = serval_link_ecs;
    if (!ecs || !entity_alive(e))
        return fail(LINK_NO_SUCH, "no such entity (dead, or never created)");
    if (prop == VM_P_BODY_CONTACT || prop == VM_P_OBJECT)
        return fail(LINK_READ_ONLY, "the property is read-only");
    if (prop < VM_P_COUNT) {
        ecs->set(entity_index(e), prop, value);
        return 0;
    }
    const ServalLinkVmPart* vm = vm_part();
    if (prop >= VM_P_FIELD0 && prop < VM_P_FIELD0 + VM_FIELDS) {
        if (!vm || vm->object_of(e) < 0)
            return fail(LINK_NO_SUCH, "the entity isn't attached to an object: no instance fields");
        vm->set_field(e, prop - VM_P_FIELD0, value);
        return 0;
    }
    return fail(LINK_NO_SUCH, "no such property");
}

static u32 request(u32 type, LinkReader* r, LinkWriter* w) {
    switch (type) {
    case LINK_AGENT_INFO:
        return agent_info(w);
    case LINK_PAUSE:
    case LINK_RESUME:
    case LINK_GLOBALS:
    case LINK_SET_GLOBAL:
    case LINK_ARRAY_CELLS:
    case LINK_SET_ARRAY_CELL:
    case LINK_ENTITIES:
    case LINK_ENTITY:
    case LINK_SET_PROPERTY:
    case LINK_STATS_SUBSCRIBE:
    case LINK_OPTIONS:
    case LINK_STEP:        // the VM's part (E6)
    case LINK_BREAK_SET:   //
    case LINK_BREAK_CLEAR: //
    case LINK_CONTEXTS:    //
    case LINK_FRAMES:      //
    case LINK_SET_LOCAL:   //
    // debug link (E9): the VM's part, hot reload
    case LINK_RELOAD_BEGIN:
    case LINK_RELOAD_DATA:
    case LINK_RELOAD_PLAN:
    case LINK_RELOAD_COMMIT:
        break;
    default:
        return fail(LINK_UNSUPPORTED, "this agent doesn't support the request");
    }
    if (!agent.attached)
        return fail(LINK_BAD_REQUEST, "not attached: send AGENT_INFO first");
    switch (type) {
    case LINK_PAUSE:
        return pause_request();
    case LINK_RESUME:
        return resume_request(r);
    case LINK_ENTITIES:
        return entities(r, w);
    case LINK_ENTITY:
        return entity(r, w);
    case LINK_SET_PROPERTY:
        return set_property(r);
    case LINK_STATS_SUBSCRIBE:
        return stats_subscribe(r);
    case LINK_OPTIONS:
        return options(r);
    default: { // the VM's: GLOBALS to SET_ARRAY_CELL, and CONTEXTS to STEP
        const ServalLinkVmPart* vm = vm_part();
        if (!vm)
            return fail(LINK_NO_VM, "the game doesn't link the VM");
        if (type == LINK_STEP)
            return step_request(vm, r, w);
        if (type == LINK_SET_LOCAL && !agent.parked)
            return fail(LINK_NOT_PARKED, "SET_LOCAL needs the game parked");
        if (type == LINK_RELOAD_BEGIN && (!agent.parked || agent.inside)) // debug link (E9)
            return agent.parked
                       ? fail(LINK_NOT_AT_BOUNDARY, "RELOAD_BEGIN needs the game parked at "
                                                    "a frame boundary (PAUSE)")
                       : fail(LINK_NOT_PARKED, "RELOAD_BEGIN needs the game parked at a "
                                               "frame boundary (PAUSE)");
        return vm->request(type, r, w);
    }
    }
}

// One frame from the inbox: a request gets exactly one reply. Replies and
// events from the debugger (it sends none in 1.0) are ignored, as are flag
// bits this protocol version doesn't define.
static void handle(const u8* frame, u32 size) {
    u32 type = frame[2], flags = frame[3], seq = frame[4] | (u32)frame[5] << 8;
    if ((flags & LINK_FLAG_REPLY) || seq == 0)
        return;
    LinkReader r = {.p = frame + LINK_HEADER, .n = size - LINK_HEADER, .at = 0, .ok = true};
    LinkWriter w;
    if (!frame_open(type, LINK_FLAG_REPLY, seq, &w))
        return;
    u32 code = request(type, &r, &w);
    if (!code && w.full)
        code = fail(LINK_BAD_REQUEST, "the reply doesn't fit a frame");
    if (code) {
        w.p[-3] = LINK_FLAG_REPLY | LINK_FLAG_ERROR;
        w.n = 0;
        w.full = false;
        serval_link_put8(&w, code);
        serval_link_put_str(&w, serval_link_error_text);
    }
    frame_close(&w);
    const ServalLinkVmPart* vm = vm_part(); // debug link (E9): RELOADED after RELOAD_COMMIT's reply
    if (vm)
        vm->replied();
    if (agent.stop) {
        send_stopped(agent.stop - 1u);
        agent.stop = 0;
    }
}

// Handles every whole frame in the inbox, then empties it. A frame whose
// length is impossible (under 4, or past in_len) ends the reading: the rest
// is dropped, since the next frame's start is unknown.
static void read_inbox(void) {
    ServalLinkMailbox* m = &serval_link_mailbox;
    u32 n = m->in_len;
    if (n > LINK_IN_SIZE)
        n = 0;
    for (u32 at = 0; at + 2 <= n;) {
        u32 length = m->in[at] | (u32)m->in[at + 1] << 8;
        if (length < 4 || length > n - at - 2)
            break;
        handle(m->in + at, 2 + length);
        at += 2 + length;
    }
    m->in_len = 0;
}

// --- Parking and the frame hook ------------------------------------------------------

// The game stays here, at the end of frame_end() (state: at a frame
// boundary) or inside a frame (serval_link_stop), until RESUME, RESUME to the
// next boundary, STEP, or the debugger leaving. Each pass rings (with the
// replies, saying "parked": the fork pauses the machine until it delivers a
// frame), reads the inbox, or waits for the endpoint.
static void park(u32 state) {
    ServalLinkMailbox* m = &serval_link_mailbox;
    serval_link_platform_park(true);
    while (agent.parked) {
        ring(state);
        if (!endpoint_connected()) {
            detach();
            break;
        }
        if (m->in_len)
            read_inbox();
        else
            serval_link_platform_wait();
    }
    ring(LINK_STATE_RUNNING);
    serval_link_platform_park(false);
}

void serval_link_poll(void) {
    ServalLinkMailbox* m = &serval_link_mailbox;
    if (agent.attached && !endpoint_connected())
        detach();
    const ServalLinkVmPart* vm = vm_part();
    if (vm) {
        vm->poll(agent.attached);
        ServalLinkVmStats v;
        vm->stats(&v);
        u32 peak = v.queue_peak > v.queued ? v.queue_peak : v.queued;
        if (peak > agent.queue_peak)
            agent.queue_peak = peak;
    }
    if (agent.attached && agent.stats_every && ++agent.stats_count >= agent.stats_every) {
        agent.stats_count = 0;
        serval_link_event(LINK_STATS, fill_stats, NULL);
        agent.queue_peak = 0;
    }
    if (agent.next_boundary) {
        u32 reason = agent.next_boundary - 1u;
        agent.next_boundary = 0;
        if (agent.attached && !agent.parked) {
            agent.parked = true;
            send_stopped(reason);
        }
    }
    read_inbox();
    if (agent.parked)
        park(LINK_STATE_BOUNDARY);
    else if (m->out_len)
        ring(LINK_STATE_RUNNING);
}

void serval_link_stop(u32 reason, u32 context, u32 pc, u32 fault) {
    if (!agent.attached)
        return;
    if (!endpoint_connected()) {
        detach();
        return;
    }
    agent.parked = true;
    agent.next_boundary = 0; // a frame step is over too
    send_stopped_at(reason, context, pc, fault);
    agent.inside = true; // debug link (E9)
    park(LINK_STATE_INSIDE);
    agent.inside = false;
}

u32 serval_link_options(void) {
    return agent.attached ? agent.options : 0;
}

void serval_link_init(void) {
    serval_link_hook = NULL;
    agent = (Agent){0};
    const ServalLinkVmPart* vm = vm_part();
    if (vm)
        vm->detached(); // (re)started: no debugger, so no patched opcode and no step
    if (!serval_link_platform_present())
        return;
    // A fresh mailbox, endpoint field included: an endpoint that finds its
    // SVLE gone at a ring knows the agent started again (GAME_RESET).
    ServalLinkMailbox* m = &serval_link_mailbox;
    u8* bytes = (u8*)m;
    for (u32 i = 0; i < sizeof(*m); i++)
        bytes[i] = 0;
    m->magic = LINK_MAGIC;
    m->version = LINK_MAILBOX_VERSION;
    m->size = (u16)sizeof(ServalLinkMailbox);
    agent.started = true;
    if (vm)
        vm->poll(false);
    serval_link_hook = serval_link_poll;
    ring(LINK_STATE_RUNNING);
}

bool serval_link_started(void) {
    return agent.started;
}

bool serval_link_attached(void) {
    return agent.attached;
}

bool serval_link_parked(void) {
    return agent.parked;
}

#endif // SERVAL_LINK_AGENT
