#ifndef SERVAL_CORE_LINK_INTERNAL_H
#define SERVAL_CORE_LINK_INTERNAL_H

// Engine-internal: the debug link's agent (docs/debug-link.md: protocol 1.0,
// mailbox v1), in debug builds with the agent only (SERVAL_LINK_AGENT:
// SERVAL_DEBUG and the CMake option SERVAL_LINK). Not part of the public API.
//
// The agent is split so that a game links only what it uses:
//   - src/core/link.c: the core (the mailbox, framing, the handshake's agent
//     side, pause and frame steps, entities, stats). Linked by serval_init()
//     in every debug build, and dormant (no frame hook) unless an endpoint
//     answered.
//   - its ECS part (in link.c): the entity properties, reached only through
//     serval_link_ecs, which entity_create() sets, so a game that never
//     creates an entity keeps none of the pools it doesn't use.
//   - src/core/vm_link.c: the VM part (globals, arrays, instance fields,
//     the blob's size and hash, LOADED, the scratch; and BRK's park,
//     breakpoints with displaced execution, stepping, contexts and frames,
//     faults), reached only through serval_link_vm, which vm.c defines and
//     link.c refers to weakly, so a game that doesn't link the VM links none
//     of it. vm.c calls into it from a few hooks (its debug link blocks).
//   - the platform's end (below): src/gba/link_gba.c (the presence register
//     and the doorbell), src/web/link_web.c with src/web/link.js (the page's
//     end), src/host/link_host.c (a fake endpoint for the tests).

#include "serval/ecs.h"
#include "serval/platform.h"

#include <stddef.h>

_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
               "the mailbox and the frames are little-endian, read in place");

// --- Protocol 1.0 (docs/debug-link.md#messages-protocol-10) -------------------

#define LINK_PROTO_MAJOR 1
#define LINK_PROTO_MINOR 0

// A frame: length u16 (the bytes after it), type u8, flags u8, seq u16, payload.
#define LINK_FRAME_MAX 1024
#define LINK_HEADER 6
#define LINK_PAYLOAD_MAX (LINK_FRAME_MAX - LINK_HEADER)
#define LINK_FLAG_REPLY 1u
#define LINK_FLAG_ERROR 2u

enum {
    // The endpoint's (0x00-0x3F): the agent never sees them.
    LINK_HELLO = 0x01,
    LINK_READ_MEM = 0x02,
    LINK_WRITE_MEM = 0x03,
    LINK_MACHINE_PAUSE = 0x04,
    LINK_MACHINE_RESUME = 0x05,
    LINK_LOG = 0x20,
    LINK_GAME_RESET = 0x21,
    LINK_AGENT_FOUND = 0x22,
    // The agent's requests.
    LINK_AGENT_INFO = 0x40,
    LINK_PAUSE = 0x41,
    LINK_RESUME = 0x42,
    LINK_STEP = 0x43,
    LINK_BREAK_SET = 0x44,
    LINK_BREAK_CLEAR = 0x45,
    LINK_CONTEXTS = 0x46,
    LINK_FRAMES = 0x47,
    LINK_GLOBALS = 0x48,
    LINK_SET_GLOBAL = 0x49,
    LINK_ARRAY_CELLS = 0x4A,
    LINK_SET_ARRAY_CELL = 0x4B,
    LINK_ENTITIES = 0x4C,
    LINK_ENTITY = 0x4D,
    LINK_SET_PROPERTY = 0x4E,
    LINK_SET_LOCAL = 0x4F,
    LINK_RELOAD_BEGIN = 0x50,
    LINK_RELOAD_DATA = 0x51,
    LINK_RELOAD_PLAN = 0x52,
    LINK_RELOAD_COMMIT = 0x53,
    LINK_STATS_SUBSCRIBE = 0x54,
    LINK_OPTIONS = 0x55,
    // The agent's events.
    LINK_STOPPED = 0x60,
    LINK_FAULT = 0x61,
    LINK_STATS = 0x62,
    LINK_RELOADED = 0x63,
    LINK_LOADED = 0x64,
};

// Error codes (an error reply's code u8).
enum {
    LINK_UNSUPPORTED = 1,
    LINK_BAD_REQUEST = 2,
    LINK_NO_AGENT = 3,
    LINK_NOT_PARKED = 4,
    LINK_NOT_AT_BOUNDARY = 5,
    LINK_NO_VM = 6,
    LINK_NO_SCRATCH = 7,
    LINK_NO_SUCH = 8,
    LINK_READ_ONLY = 9,
    LINK_FULL = 10,
    LINK_RELOADING = 11,
    LINK_MISMATCH = 12,
};

// The agent's capabilities (AGENT_INFO).
#define LINK_CAP_VM (1u << 0)
#define LINK_CAP_BREAKPOINTS (1u << 1)
#define LINK_CAP_STEP (1u << 2)
#define LINK_CAP_RELOAD (1u << 3)
#define LINK_CAP_STATS (1u << 4)
#define LINK_CAP_ENTITIES (1u << 5)
#define LINK_CAP_STOP_ON_FAULT (1u << 6)

// STOPPED's reasons.
enum {
    LINK_STOP_PAUSE = 0,
    LINK_STOP_FRAME = 1,
    LINK_STOP_BRK = 2,
    LINK_STOP_BREAKPOINT = 3,
    LINK_STOP_STEP = 4,
    LINK_STOP_ENDED = 5,
    LINK_STOP_FAULT = 6,
    LINK_STOP_DEBUG_BREAK = 7,
};

// FAULT's kinds (and STOPPED's fault): the VM's faults (docs/vm.md).
enum {
    LINK_FAULT_STACK = 1,   // stack overflow or underflow
    LINK_FAULT_CALLS = 2,   // call depth past VM_CALLS
    LINK_FAULT_ESCAPED = 3, // pc escaped the blob
    LINK_FAULT_OPCODE = 4,  // unknown opcode
    LINK_FAULT_SYS = 5,     // unknown SYS call
    LINK_FAULT_ENTER = 6,   // ENTER that doesn't fit
    LINK_FAULT_BUDGET = 7,  // a behaviour throttled, a reaction halted
    LINK_FAULT_WAIT = 8,    // a wait in a reaction
    LINK_FAULT_OTHER = 9,   // another halt
    LINK_FAULT_KINDS = 10,
};

// BREAK_SET's and BREAK_CLEAR's kinds.
enum {
    LINK_SITE_BREAKPOINT = 0, // a breakpoint
    LINK_SITE_STEP = 1,       // a step site at a call depth up to STEP's
    LINK_SITE_STEP_ANY = 2,   // a step site at any depth
    LINK_SITE_KINDS = 3,
};

// The breakpoint sites the agent can patch at once (AGENT_INFO's sites).
#define LINK_SITES 256

// --- Mailbox v1 (docs/debug-link.md#mailbox-v1) -------------------------------

#define LINK_MAILBOX_VERSION 1
#define LINK_IN_SIZE 1024
#define LINK_OUT_SIZE 2048
// The bytes SVLK and SVLE, read as little-endian words.
#define LINK_MAGIC 0x4B4C5653u
#define LINK_ENDPOINT_MAGIC 0x454C5653u

enum {
    LINK_STATE_RUNNING = 0,
    LINK_STATE_BOUNDARY = 1, // parked at a frame boundary
    LINK_STATE_INSIDE = 2,   // parked inside a frame (BRK, breakpoints, steps, faults, debug_break)
};

typedef struct {
    u32 magic;    // SVLK (agent)
    u16 version;  // 1 (agent)
    u16 size;     // 3,092 (agent)
    u32 endpoint; // SVLE while a debugger is connected (endpoint)
    u16 in_len;   // bytes of frames in `in` (the endpoint sets it, the agent zeroes it)
    u16 out_len;  // bytes of frames in `out` (the agent adds, the endpoint zeroes it)
    u8 state;     // LINK_STATE_* (agent, before every ring)
    u8 reserved[3];
    u8 in[LINK_IN_SIZE];   // whole frames for the agent
    u8 out[LINK_OUT_SIZE]; // whole frames from the agent
} ServalLinkMailbox;

_Static_assert(sizeof(ServalLinkMailbox) == 3092, "mailbox v1 is 3,092 bytes");
_Static_assert(offsetof(ServalLinkMailbox, endpoint) == 8 &&
                   offsetof(ServalLinkMailbox, in_len) == 12 &&
                   offsetof(ServalLinkMailbox, out_len) == 14 &&
                   offsetof(ServalLinkMailbox, state) == 16 &&
                   offsetof(ServalLinkMailbox, in) == 20 &&
                   offsetof(ServalLinkMailbox, out) == 1044,
               "mailbox v1's layout (docs/debug-link.md#mailbox-v1)");

#ifdef SERVAL_LINK_AGENT

// --- The core (link.c) ---------------------------------------------------------

// The frame hook: frame_end() calls it, after frames++, while it is set; it
// is set when an endpoint answered (serval_link_init), else NULL. In EWRAM.
extern void (*serval_link_hook)(void);

// Called by serval_init() (GBA, web): asks the platform whether an endpoint
// answered (serval_link_platform_present); if so, (re)starts the agent: the
// mailbox zeroed and its header written, the hook set, a ring. Otherwise the
// agent stays dormant: nothing else is written.
void serval_link_init(void);

// The frame hook's function: handles the inbox, sends stats and LOADED,
// parks for PAUSE and frame steps. Tests call it in place of frame_end().
void serval_link_poll(void);

// The mailbox, for the platforms and the tests.
extern ServalLinkMailbox serval_link_mailbox;

// For tests: whether the agent was started, is attached (AGENT_INFO
// answered) and is parked.
bool serval_link_started(void);
bool serval_link_attached(void);
bool serval_link_parked(void);

// debug link (E6): parks the game inside a frame (mailbox state 2) while a
// debugger is attached: STOPPED with the reason, context (0xFF: none), pc (0:
// none) and fault kind (0: none), then the park loop, until RESUME, STEP or
// the debugger leaving. Returns at once when no debugger is attached. For the
// VM's part (BRK, breakpoints, steps, faults, a stepped context's end) and
// debug_break().
void serval_link_stop(u32 reason, u32 context, u32 pc, u32 fault);

// OPTIONS' flags (bit 0: stop on fault); 0 when no debugger is attached.
u32 serval_link_options(void);

// --- The ECS part (link.c, through entity_create) --------------------------------

// The entity properties (VM_P_X to VM_P_BODY_CONTACT; vm.h) as GETP reads
// them and SETP writes them, from and to the ECS's pools. entity_create()
// sets serval_link_ecs to &serval_link_ecs_part, so only games that create
// entities keep the pools through it.
typedef struct {
    s32 (*get)(u32 slot, u32 prop);
    void (*set)(u32 slot, u32 prop, s32 value);
} ServalLinkEcs;
extern const ServalLinkEcs serval_link_ecs_part;
extern const ServalLinkEcs* serval_link_ecs;

// --- The VM part (vm_link.c, through vm.c) -------------------------------------

// A reply under construction: the payload, written in place in the outbox.
typedef struct {
    u8* p;     // the payload's first byte
    u32 n;     // bytes written
    u32 room;  // the most it may hold
    bool full; // a write didn't fit (and was dropped)
} LinkWriter;

// A request's payload.
typedef struct {
    const u8* p;
    u32 n;   // bytes in the payload
    u32 at;  // bytes read
    bool ok; // false once a read ran past the end
} LinkReader;

u32 serval_link_u8(LinkReader* r);
u32 serval_link_u16(LinkReader* r);
u32 serval_link_u32(LinkReader* r);
void serval_link_put8(LinkWriter* w, u32 v);
void serval_link_put16(LinkWriter* w, u32 v);
void serval_link_put32(LinkWriter* w, u32 v);
void serval_link_put_str(LinkWriter* w, const char* s);
// Appends an event frame (seq 0) to the outbox, ringing first if it might
// not fit; its payload is written by `fill`. Dropped if the endpoint leaves
// no room.
void serval_link_event(u32 type, void (*fill)(LinkWriter* w, const void* arg), const void* arg);
// The error text a handler gives with its code (static text).
extern const char* serval_link_error_text;

// FNV-1a over 32 bits (docs/debug-link.md#handshake).
u32 serval_link_fnv1a(const u8* bytes, u32 size);

// AGENT_INFO's share of the VM part.
typedef struct {
    u32 size, hash;   // the loaded blob's (0, 0: none), its hash computed now
    u32 scratch;      // SERVAL_LINK_SCRATCH
    u32 sites;        // breakpoint sites (0 without a scratch)
    u32 capabilities; // the VM part's: the VM, breakpoints and stepping, stop on fault
} ServalLinkVmInfo;

// STATS' share of the VM part.
typedef struct {
    u32 ops;        // vm_ops_this_frame()
    u32 contexts;   // live contexts
    u32 queued;     // events queued now
    u32 queue_peak; // the most queued inside the phases since the last poll
    u32 vm_cycles;  // the last frame's cycles in vm_step() and vm_events()
} ServalLinkVmStats;

typedef struct {
    void (*info)(ServalLinkVmInfo* info);
    // Each poll (frame boundary): the frame's VM cycles are latched for STATS.
    void (*poll)(bool attached);
    // GLOBALS, SET_GLOBAL, ARRAY_CELLS, SET_ARRAY_CELL, and (E6) CONTEXTS,
    // FRAMES, SET_LOCAL, BREAK_SET, BREAK_CLEAR and STEP (link.c checks that
    // the game is parked for the last two): returns 0 or an error code (with
    // serval_link_error_text), the reply's payload in w.
    u32 (*request)(u32 type, LinkReader* r, LinkWriter* w);
    // The object entity e is attached to, or -1; its instance field n.
    int (*object_of)(Entity e);
    s32 (*field)(Entity e, u32 n);
    void (*set_field)(Entity e, u32 n, s32 value);
    // The context of e's behaviour, or -1.
    int (*context_of)(Entity e);
    void (*stats)(ServalLinkVmStats* s);
    // The game stopped (any STOPPED): a step is over, its sites cleared.
    void (*stopped)(void);
    // The debugger left: every patched opcode restored, no step.
    void (*detached)(void);
} ServalLinkVmPart;

// Defined in vm.c (its debug link block), so it exists only in games that
// link the VM. Weak: a game without the VM doesn't link vm.c, and link.c
// finds &serval_link_vm NULL (a weak reference doesn't pull vm.c in). It
// points at vm_link.c's part, which a game links only through it.
extern const ServalLinkVmPart* const serval_link_vm __attribute__((weak));
extern const ServalLinkVmPart serval_link_vm_part;

// vm.c's state, for vm_link.c (vm.c's debug link block).
const u8* serval_vm_link_blob(u32* size);
u32 serval_vm_link_contexts(void);
u32 serval_vm_link_queued(void);
int serval_vm_link_context_of(Entity e);
void serval_vm_link_set_field(Entity e, u32 n, s32 value);
void serval_vm_link_set_array_cell(u32 n, s32 value);

// debug link (E6): the rest of vm.c's state the VM part reads and writes.

// Points the VM at a byte-identical copy of its blob (the agent's scratch):
// contexts hold blob offsets, so nothing else changes, mid-phase too.
void serval_vm_link_use_copy(const u8* copy);
// The most events queued inside the phases since the last call.
u32 serval_vm_link_queue_peak(void);

// A live context, as CONTEXTS and FRAMES report it: its running activation
// (a reaction's, while one runs on top of a waiting behaviour), and the
// behaviour beneath while stacked. The registers are the ones the context
// saved: at a wait, or where the agent parked it.
typedef struct {
    u32 pc;
    Entity self, other;
    int object; // self's object, or -1 (a thread, or not attached)
    u16 wait;   // frames left (state 3)
    u8 state;   // 1 ready, 2 running, 3 waiting for frames, 4 for its animation, 5 for its path
    u8 event;   // VM_EV_*
    u8 sp, fp, base, cp;
    bool stacked;
    s32* stack;         // VM_STACK cells (the behaviour's below `base` while stacked)
    const u32* call_pc; // the activation's return points, [0, cp)
    const u8* call_fp;  // and the callers' frames
    struct {            // the behaviour beneath, while stacked
        u32 pc;
        u16 wait;
        u8 state, event, sp, fp, base, cp;
        const u32* call_pc; // its return points, set aside while the reaction runs
        const u8* call_fp;
    } below;
} ServalVmLinkContext;
// Fills *c for context k; false if k isn't live.
bool serval_vm_link_context(u32 k, ServalVmLinkContext* c);

// vm.c calls these, in debug builds with the agent only (vm_link.c):
//   - a BRK at `at` in context k, its registers saved: the opcode to run in
//     BRK's place, a patched site's original (displaced execution), or
//     VM_OP_BRK for a script's own, which goes on after itself (it parks while
//     a debugger is attached, and logs "vm: BRK at ..." otherwise).
u32 serval_vm_link_brk(u32 k, u32 at);
//   - a fault of context k at `at`, its registers saved, before the VM halts
//     or throttles it: `warning` is the VM's warning when it just logged it
//     (once per kind per loaded blob), else NULL. Sends FAULT, and parks with
//     stop on fault.
void serval_vm_link_fault(u32 k, u32 kind, u32 at, const char* warning);
//   - the stepped context + 1 while a step runs, else 0: vm.c asks
//     serval_vm_link_ends() when that context halts, or a reaction on it
//     ends, and calls serval_vm_link_ended() (which parks: STOPPED, ended)
//     once the context's state is whole again.
extern u8 serval_vm_link_stepping;
bool serval_vm_link_ends(u32 k, bool reaction);
void serval_vm_link_ended(void);
//   - vm_step() and vm_events() begin (true) and end (false), while a
//     debugger is attached (serval_vm_link_attached: 1): STATS' vm_cycles.
extern u8 serval_vm_link_attached;
void serval_vm_link_phase(bool begin);
//   - the game loaded, reloaded or unloaded a blob (install()): LOADED, and
//     every breakpoint site is gone.
void serval_vm_link_loaded(void);

// --- The platform's end ---------------------------------------------------------

// Whether an endpoint answered: GBA, the presence register (0x5E41 written,
// 0x1A5E read back); web, the page called serval_link_attach(); host, the
// tests' choice.
bool serval_link_platform_present(void);
// The doorbell: the endpoint sends `out`'s frames and zeroes out_len during
// it, and may write `in`. Returns once it has.
void serval_link_platform_ring(ServalLinkMailbox* mailbox);
// Parked with an empty inbox: wait for the endpoint (GBA: VBlankIntrWait();
// web: the page's event loop runs until a frame arrives; host: the tests').
void serval_link_platform_wait(void);
// The game parks (true) or goes on (false): GBA, the sound muted while
// parked.
void serval_link_platform_park(bool parked);
// The ROM header's title (up to 12 characters) and game code (4), as
// NUL-terminated text; on the web, the page's.
void serval_link_platform_rom(char title[13], char game_code[5]);

// What STATS reports from outside the agent (frame_end's counts).
typedef struct {
    u32 frame;        // frame_count()
    u32 cycles;       // frame_cpu_cycles()
    u32 drawn;        // sprite_stats()
    u32 matrices;     //
    u32 dropped;      //
    u32 mixer_cycles; // the sound bank's mixer this frame (0 without one)
} ServalLinkFrameStats;
void serval_link_platform_stats(ServalLinkFrameStats* s);

// debug link (E6): the CPU cycle counter frame_cpu_cycles() is counted with
// (GBA: timers 2 and 3; web: the browser's clock; host: the tests'), read
// around vm_step() and vm_events() for STATS' vm_cycles.
u32 serval_link_platform_cycles(void);

#endif // SERVAL_LINK_AGENT

#endif // SERVAL_CORE_LINK_INTERNAL_H
