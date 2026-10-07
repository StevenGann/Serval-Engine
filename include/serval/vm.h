#ifndef SERVAL_VM_H
#define SERVAL_VM_H

// The bytecode VM: objects with event handlers (GameMaker's model), run as
// cooperative scripts with no allocation. The editor's script compiler emits
// a script blob in the format specified in docs/vm.md, the source of truth
// for every number below; this header only names them.
//
//     vm_load(game_scripts, sizeof game_scripts);
//     Entity e = entity_create(C_POS | C_SPR);
//     vm_attach(e, OBJ_PLAYER); // its Create handler runs in the next phase
//     for (;;) {
//         frame_begin();
//         vm_step();            // waits, queued events, Step handlers
//         sys_movement();
//         sys_physics();
//         vm_events();          // collision handlers (vm_event from game code)
//         sys_animate();
//         sys_render();
//         frame_end();
//     }

#include "serval/audio.h"
#include "serval/ecs.h"
#include "serval/path.h"

// --- Limits ------------------------------------------------------------------

#define VM_CONTEXTS 32       // scripts running or waiting at once
#define VM_STACK 64          // cells per context: operands and every frame's locals
#define VM_CALLS 16          // CALL depth per context
#define VM_GLOBALS 256       // global cells shared by all scripts
#define VM_EVENT_QUEUE 32    // events waiting for dispatch
#define VM_OPS_PER_SLICE 256 // opcodes a context may run in one phase

// --- Blob format (docs/vm.md#blob-format) ------------------------------------

#define VM_FORMAT_VERSION 1
#define VM_CELL_BYTES 4
#define VM_HEADER_SIZE 16
#define VM_OBJECT_SIZE 32

// Events: the handler slots of an object record, in order.
enum {
    VM_EV_CREATE,
    VM_EV_STEP,
    VM_EV_DESTROY,
    VM_EV_COLLISION,
    VM_EV_ANIM_END,
    VM_EV_ROOM_START,
    VM_EV_COUNT
};

// Opcodes (docs/vm.md#opcode-reference).
enum {
    VM_OP_NOP = 0x00,
    VM_OP_HALT = 0x01,
    VM_OP_PUSH8 = 0x02,
    VM_OP_PUSH16 = 0x03,
    VM_OP_PUSH32 = 0x04,
    VM_OP_DUP = 0x05,
    VM_OP_DROP = 0x06,
    VM_OP_SWAP = 0x07,
    VM_OP_LDG = 0x08,
    VM_OP_STG = 0x09,
    VM_OP_LDL = 0x0A, // the frame's local n
    VM_OP_STL = 0x0B,

    VM_OP_ADD = 0x10,
    VM_OP_SUB = 0x11,
    VM_OP_MUL = 0x12,
    VM_OP_DIV = 0x13,
    VM_OP_MOD = 0x14,
    VM_OP_NEG = 0x15,
    VM_OP_FXMUL = 0x16,
    VM_OP_FXDIV = 0x17,
    VM_OP_AND = 0x18,
    VM_OP_OR = 0x19,
    VM_OP_XOR = 0x1A,
    VM_OP_BNOT = 0x1B,
    VM_OP_SHL = 0x1C,
    VM_OP_SHR = 0x1D,
    VM_OP_LNOT = 0x1E,
    VM_OP_LSH = 0x1F, // Lua's << (and >> as LSH a, -b)

    VM_OP_EQ = 0x20,
    VM_OP_NE = 0x21,
    VM_OP_LT = 0x22,
    VM_OP_LE = 0x23,
    VM_OP_GT = 0x24,
    VM_OP_GE = 0x25,
    VM_OP_IDIV = 0x26, // floored, as Lua's //
    VM_OP_IMOD = 0x27, // floored, as Lua's %

    VM_OP_JMP = 0x28,
    VM_OP_JZ = 0x29,
    VM_OP_JNZ = 0x2A,
    VM_OP_CALL = 0x2B,
    VM_OP_RET = 0x2C,
    VM_OP_RETV = 0x2D,
    VM_OP_ENTER = 0x2E, // u8 p, u8 n: the frame's arguments and locals

    VM_OP_WAIT = 0x30,
    VM_OP_WAIT_ANIM = 0x31,
    VM_OP_WAIT_MOVE = 0x32,
    VM_OP_INTERRUPTIBLE = 0x33,

    VM_OP_SELF = 0x38,
    VM_OP_OTHER = 0x39,
    VM_OP_GETP = 0x3A,
    VM_OP_SETP = 0x3B,
    VM_OP_SPAWN = 0x3C,
    VM_OP_KILL = 0x3D,

    VM_OP_SYS = 0x40,

    VM_OP_BRK = 0x50,
    VM_OP_TRACE = 0x51,
};

// Entity properties for GETP and SETP: the ECS arrays of the same names.
// Append-only.
enum {
    VM_P_X,      // pos_x (FIXED)
    VM_P_Y,      // pos_y (FIXED)
    VM_P_VX,     // vel_x (FIXED)
    VM_P_VY,     // vel_y (FIXED)
    VM_P_SPR,    // spr_id
    VM_P_FRAME,  // spr_frame
    VM_P_FLAGS,  // spr_flags
    VM_P_ANGLE,  // spr_angle
    VM_P_DEPTH,  // spr_depth
    VM_P_SCALE,  // spr_scale
    VM_P_BODY_W, // body_w (C_BODY; physics.h)
    VM_P_BODY_H, // body_h (C_BODY)
    VM_P_COUNT
};

// Engine calls for SYS. Arguments are pushed left to right (the last on
// top). Append-only.
enum {
    VM_SYS_PSG_PLAY,          // sound id
    VM_SYS_MUSIC_PLAY,        // song index (VmBindings.songs)
    VM_SYS_MUSIC_STOP,        //
    VM_SYS_MUSIC_PAUSE,       //
    VM_SYS_MUSIC_RESUME,      //
    VM_SYS_CAMERA_SET,        // x, y (whole pixels)
    VM_SYS_TEXT_PRINT,        // col, row, string index
    VM_SYS_RANDOM_RANGE,      // lo, hi -> random_range(lo, hi)
    VM_SYS_BUTTON_DOWN,       // buttons -> 1 if button_down(buttons), else 0
    VM_SYS_BUTTON_PRESSED,    // buttons -> 1 if button_pressed(buttons), else 0
    VM_SYS_BRIGHTNESS,        // level -> screen_set_brightness(level)
    VM_SYS_PATH_START,        // entity, path index (VmBindings.paths), flags
    VM_SYS_TEXT_PRINT_NUMBER, // col, row, value: prints the value in decimal
    VM_SYS_COUNT
};

// Data that SYS calls reach by index, since scripts hold no pointers. The
// arrays must stay valid while scripts run; vm_bind copies this struct.
typedef struct {
    const PsgSong* const* songs;
    const Path* const* paths;
    u16 song_count;
    u16 path_count;
} VmBindings;

// --- Loading -----------------------------------------------------------------

// Validates the blob and makes it the running scripts: halts every context,
// detaches every entity, empties the event queue and zeroes the globals.
// Returns false (and warns) if the blob is not valid; nothing runs then. The
// blob is read in place and must stay valid while it is loaded. Call it (or
// vm_unload) after ecs_reset().
bool vm_load(const u8* blob, u32 size);

// Like vm_load, for the debug link's hot reload: keeps the globals' values if
// the new blob declares the same global count, and keeps entities attached to
// objects the new blob still has. Contexts are halted and the queue emptied.
bool vm_reload(const u8* blob, u32 size);

void vm_unload(void);
void vm_bind(const VmBindings* bindings);

// --- Entities and events -----------------------------------------------------

// Binds an entity to an object and queues its Create event. Its Step handler
// first runs once that Create has been dispatched.
void vm_attach(Entity e, u16 object);
// Halts the entity's script and unbinds it; no Destroy event.
void vm_detach(Entity e);
// Runs the entity's Destroy handler (if any) to completion, then destroys it.
// Use this, or vm_detach, instead of entity_destroy for attached entities.
void vm_kill(Entity e);
// Starts an object's handler as a thread with no entity (self is
// ENTITY_NONE). It first runs in the next vm_step(). Returns the context
// index, or -1 (and warns) if there is no such handler or no free context.
int vm_start(u16 object, u8 event);
// Queues an event for an entity: game code reports collisions this way,
// e.g. vm_event(a, b, VM_EV_COLLISION) after body_overlap(a, b).
void vm_event(Entity e, Entity other, u8 event);

// --- Running -----------------------------------------------------------------

void vm_step(void);   // phase 1: after input, before movement
void vm_events(void); // phase 2: after movement and physics

// --- Inspecting --------------------------------------------------------------

s32 vm_global(u16 index);
void vm_set_global(u16 index, s32 value);
u32 vm_ops_this_frame(void); // opcodes run since the latest vm_step() began
bool vm_idle(void);          // no context live and no event queued

#endif // SERVAL_VM_H
