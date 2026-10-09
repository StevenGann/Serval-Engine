#ifndef SERVAL_VM_H
#define SERVAL_VM_H

// The bytecode VM: objects with event handlers (GameMaker's model), run as
// cooperative scripts with no allocation. A compiler (the Lua subset of
// docs/lua.md, Studio Advance's event editor through it) or tools/svm.py emits
// a script blob in the format specified in docs/vm.md, the source of truth
// for every number below; this header only names them.
//
// Create and Room Start start an instance's behaviour, a script that may wait;
// every other event is a reaction that runs to completion, on top of the
// behaviour if it waits (docs/vm.md "Behaviours and reactions").
//
//     #define C_PLAYER C_GAME(0) // the game's components
//     #define C_COIN C_GAME(1)
//
//     vm_bind(&(VmBindings){.psg_songs = songs, .psg_song_count = SONG_COUNT,
//                           .paths = paths, .path_count = PATH_COUNT});
//     vm_collide(C_PLAYER, C_COIN); // vm_events() raises their Collision events
//     vm_load(game_scripts, sizeof game_scripts);
//     Entity e = entity_create(C_POS | C_VEL | C_SPR | C_BODY | C_PLAYER);
//     vm_attach(e, OBJ_PLAYER); // its Create handler runs in the next phase
//     for (;;) {
//         frame_begin();
//         vm_step();            // waits, queued events, Step reactions
//         sys_path();           // paths the scripts started
//         sys_movement();
//         sys_physics();
//         vm_events();          // queued events, then vm_collide's collisions
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
#define VM_FIELDS 16         // instance fields per attached entity (VM_P_FIELD)
#define VM_ARRAY_CELLS 1024  // the RAM arrays' pool, in cells
#define VM_EVENT_QUEUE 32    // events waiting for dispatch
#define VM_OPS_PER_SLICE 256 // opcodes per handler run; a reaction atop a behaviour counts apart

// --- Blob format (docs/vm.md#blob-format) ------------------------------------

#define VM_FORMAT_VERSION 1
#define VM_CELL_BYTES 4
#define VM_HEADER_SIZE 16
#define VM_OBJECT_SIZE 32      // bytes per object record
#define VM_ARRAY_RECORD_SIZE 8 // bytes per array record

// Header flags. Bit 0: the globals' initial values (global count x s32) follow
// the array table, and vm_load starts the globals at them instead of 0. Bit 1
// is reserved for an extended handler table (events beyond an object record's
// six slots, in a later version). Every bit but bit 0 must be 0: vm_load
// refuses a blob with any other bit set (warns, returns false), so a blob that
// needs a later engine never runs half-understood on this one.
#define VM_FLAG_GLOBAL_VALUES 1

// Array kinds (an array record's kind): cells in the RAM pool, or constant
// ROM data in the blob, little-endian and packed.
enum {
    VM_ARRAY_RAM,
    VM_ARRAY_S8,
    VM_ARRAY_U8,
    VM_ARRAY_S16,
    VM_ARRAY_U16,
    VM_ARRAY_S32,
    VM_ARRAY_KIND_COUNT
};

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
    VM_OP_LDA = 0x0C, // arrays (the blob's array table), 0-based
    VM_OP_STA = 0x0D,
    VM_OP_LEN = 0x0E,

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

    VM_OP_WAIT = 0x30, // waits are for behaviours; 0x33 is unassigned
    VM_OP_WAIT_ANIM = 0x31,
    VM_OP_WAIT_MOVE = 0x32,

    VM_OP_SELF = 0x38,
    VM_OP_OTHER = 0x39,
    VM_OP_GETP = 0x3A,
    VM_OP_SETP = 0x3B,
    VM_OP_SPAWN = 0x3C,
    VM_OP_KILL = 0x3D,
    VM_OP_NEXTI = 0x3E, // the next attached instance of an object

    VM_OP_SYS = 0x40,

    VM_OP_BRK = 0x50,
    VM_OP_TRACE = 0x51,
};

// Entity properties for GETP and SETP: the ECS arrays of the same names,
// then the instance fields (VM_P_FIELD0 on). Append-only; 21 to 63 are
// reserved for the engine. VM_P_BODY_CONTACT and VM_P_OBJECT are read-only:
// SETP of them warns and writes nothing.
enum {
    VM_P_X,         // pos_x (FIXED)
    VM_P_Y,         // pos_y (FIXED)
    VM_P_VX,        // vel_x (FIXED)
    VM_P_VY,        // vel_y (FIXED)
    VM_P_SPR,       // spr_id
    VM_P_FRAME,     // spr_frame
    VM_P_FLAGS,     // spr_flags
    VM_P_ANGLE,     // spr_angle
    VM_P_DEPTH,     // spr_depth
    VM_P_SCALE,     // spr_scale
    VM_P_BODY_W,    // body_w (C_BODY; physics.h)
    VM_P_BODY_H,    // body_h (C_BODY)
    VM_P_TAGS,      // the game components C_GAME(0) to C_GAME(14), as bits 0 to 14
    VM_P_ANIM_TIME, // spr_anim_time (C_ANIM)
    VM_P_ANIM_STEP, // spr_anim_step (C_ANIM)
    // The body's tuning and contacts (C_BODY; physics.h), as C has them:
    VM_P_BODY_BOUNCE,   // body_bounce: 0-254 the 256ths a floor bounce keeps, 255 perfect
    VM_P_BODY_FRICTION, // body_friction: 256ths of the speed lost per frame on a floor
    VM_P_BODY_MAX_FALL, // body_max_fall: FIXED pixels per frame in a u16; 0 no limit
    VM_P_BODY_GRAVITY,  // body_gravity: the scale minus 16, as BODY_GRAVITY(sixteenths)
    VM_P_BODY_CONTACT,  // body_contact, read-only: BODY_SIDE_*, MAP_CONTACT_* bits
    VM_P_OBJECT,        // read-only: the object the entity is attached to, else -1 (vm_object_of)
    VM_P_COUNT          // the engine properties: 0 to VM_P_COUNT - 1
};
// Instance fields: VM_FIELDS cells of each attached entity, zeroed when it is
// attached. VM_P_FIELD(n) is field n, 0 to VM_FIELDS - 1.
#define VM_P_FIELD0 64
#define VM_P_FIELD(n) (VM_P_FIELD0 + (n))

// Engine calls for SYS, each named after the C function it calls (the Lua
// subset's builtins have the same names in lower case; docs/lua.md).
// Arguments are pushed left to right (the last on top). Append-only: the
// numbers are part of the blob format. Tracker music and sampled sound
// (audio.h's music_* and sfx_*) have no calls yet; they arrive with their
// implementations, appended.
enum {
    VM_SYS_PSG_PLAY,              // sound id -> psg_play(id)
    VM_SYS_PSG_MUSIC_PLAY,        // song index (VmBindings.psg_songs) -> psg_music_play
    VM_SYS_PSG_MUSIC_STOP,        // psg_music_stop()
    VM_SYS_PSG_MUSIC_PAUSE,       // psg_music_pause()
    VM_SYS_PSG_MUSIC_RESUME,      // psg_music_resume()
    VM_SYS_CAMERA_SET,            // x, y (whole pixels) -> camera_set(x, y)
    VM_SYS_TEXT_PRINT,            // col, row, string index -> text_print
    VM_SYS_RANDOM_RANGE,          // lo, hi -> random_range(lo, hi)
    VM_SYS_BUTTON_DOWN,           // buttons -> 1 if button_down(buttons), else 0
    VM_SYS_BUTTON_PRESSED,        // buttons -> 1 if button_pressed(buttons), else 0
    VM_SYS_SCREEN_SET_BRIGHTNESS, // level -> screen_set_brightness(level)
    VM_SYS_PATH_START,            // entity, path index (VmBindings.paths), flags
    VM_SYS_TEXT_PRINT_NUMBER,     // col, row, value, width: the value in decimal; width >= 1
                                  // right-aligns it in that many columns, spaces in front
                                  // (no C function: C prints numbers with text_format)
    VM_SYS_PATH_STOP,             // entity -> path_stop(entity)
    VM_SYS_COUNT
};

// Data that SYS calls reach by index, since scripts hold no pointers: the
// songs VM_SYS_PSG_MUSIC_PLAY plays and the paths VM_SYS_PATH_START starts.
// The arrays must stay valid while scripts run; vm_bind copies this struct.
// An index past a count, or a NULL entry, warns and does nothing.
typedef struct {
    const PsgSong* const* psg_songs;
    const Path* const* paths;
    u16 psg_song_count;
    u16 path_count;
} VmBindings;

// --- Loading -----------------------------------------------------------------

// Validates the blob and makes it the running scripts: halts every context,
// detaches every entity, empties the event queue, sets the globals to the
// blob's initial values (VM_FLAG_GLOBAL_VALUES; 0 without them) and zeroes
// the RAM arrays.
// Returns false (and warns) if the blob is not valid; nothing runs then. The
// blob is read in place and must stay valid while it is loaded. Call it (or
// vm_unload) after ecs_reset().
bool vm_load(const u8* blob, u32 size);

// Like vm_load, for the debug link's hot reload: keeps the globals' values if
// the new blob declares the same global count (otherwise they start as
// vm_load starts them, with a warning), the RAM arrays' cells if its
// RAM arrays are laid out the same, and entities (with their instance fields)
// attached to objects the new blob still has. Contexts are halted and the
// queue emptied.
bool vm_reload(const u8* blob, u32 size);

// Halts every context, detaches every entity and empties the queue: no
// scripts run until the next vm_load. Keeps vm_bind's bindings and
// vm_collide's pairs.
void vm_unload(void);
// Sets the data SYS calls reach by index (copied; the arrays it points to are
// not). NULL clears it. Loading a blob keeps it.
void vm_bind(const VmBindings* bindings);

// --- Entities and events -----------------------------------------------------

// Binds an entity to an object, zeroes its instance fields and queues its
// Create event (one, however often it is attached before that is drained).
// Its Step reaction first runs once that Create has been dispatched.
void vm_attach(Entity e, u16 object);
// Halts the entity's script and unbinds it; no Destroy event.
void vm_detach(Entity e);
// Runs the entity's Destroy reaction (if any), then halts its behaviour and
// destroys it. Use this, or vm_detach, instead of entity_destroy for attached
// entities.
void vm_kill(Entity e);
// Starts an object's handler as a thread with no entity (self is
// ENTITY_NONE), a behaviour. It first runs in the next vm_step(). Returns the
// context index, or -1 (and warns) if no blob is loaded, the blob has no such
// object, the object has no handler for the event, or no context is free.
int vm_start(u16 object, u8 event);
// Queues an event for an entity, with `other` as the handler's OTHER; it runs
// in the next drain (vm_step or vm_events). Game code can report collisions
// this way, e.g. vm_event(a, b, VM_EV_COLLISION) after its own test, where
// vm_collide's pairs don't fit. An event number of VM_EV_COUNT or more warns
// and queues nothing; a full queue (VM_EVENT_QUEUE) drops the event and warns.
void vm_event(Entity e, Entity other, u8 event);

// --- Collisions ----------------------------------------------------------------

#define VM_COLLIDE_PAIRS 8 // pairs vm_collide() keeps at once

// Makes the VM find collisions itself, so a scripted game needs no collision
// loop in C: once vm_events() has drained the queue, it tests every pair set
// here and runs the Collision reactions of the bodies that overlap
// (docs/vm.md#collisions).
//
// A pair names two sets of entities by component mask: the live entities
// with every component in `a` (as ent_has(i, a)), and those with every
// component in `b`; game components are the usual choice:
// vm_collide(C_PLAYER, C_COIN), vm_collide(C_SHOT, C_ENEMY). Attached or not:
// an entity C code runs can be the other side of a scripted one's collision.
// Two entities overlap as body_overlap() (physics.h) says: their body_w x
// body_h rectangles at pos_x, pos_y, touching edges not counting, and a
// SPRITE_SCREEN entity compared with a world one in the world. An entity with
// a 0 x 0 body (body_w and body_h are 0 until set) is a point.
//
// For each overlapping pair, each of the two that is attached to an object
// with a Collision handler gets the event, with the other as OTHER: a's first,
// then b's. Both are queued and the queue drained before the next test, so the
// reactions run in this vm_events(), and the tests after them see what they
// did: an entity a reaction killed (KILL) is tested no more this frame, one it
// moved is tested where it is now, and one it took out of a set (VM_P_TAGS) is
// tested no more as a member of that set. The queue never fills this way: it
// holds an overlap's two events only until they run. Bodies that stay
// overlapped collide again every frame, as in GameMaker; a reaction meant for
// the first touch keeps its own state (a field, or a tag it clears).
//
// Order, deterministic: the pairs in the order they were set; in a pair, the
// entities of `a` in slot order, each against the entities of `b` in slot
// order. Each pair lists its sets (ecs_gather) when its turn comes, so an
// entity spawned meanwhile is tested from the next pair, or the next frame.
// An entity is never paired with itself, and two entities that are both in
// both sets are tested once, as (lower slot, higher slot).
//
// Returns true once the pair is set, also when it already was (either way
// round: (a, b) and (b, a) raise the same events). Returns false and warns if
// a or b is 0 (that set would be every live entity; C_BODY means every body)
// or VM_COLLIDE_PAIRS pairs are set. Pairs are the game's configuration, not
// script state: vm_load, vm_reload and vm_unload keep them; while no blob is
// loaded nothing is tested.
//
// Cost per pair and vm_events(), on the GBA: listing the sets, about 1,000
// cycles each (one list when a == b), and about 75 cycles per test of an
// entity of a against one of b, plus the reactions: about 3,000 for a player
// against 8 fireflies, 17,000 for 10 shots against 20 enemies (6% of a frame).
// Keep the sets small and apart: shots against enemies, not every body
// against every body.
bool vm_collide(u32 a, u32 b);
// Removes every pair: vm_events() tests no collisions until vm_collide()
// sets one again.
void vm_collide_clear(void);

// --- Running -----------------------------------------------------------------

// Phase 1, after input and before movement: resumes waiting scripts, queues
// Animation End events, drains the queue, runs the Step reactions, drains
// again (docs/vm.md "Scheduling: two phases per frame").
void vm_step(void);
// Phase 2, after movement and physics: drains the queue (events game code
// queued since vm_step), then tests vm_collide's pairs, running their
// Collision reactions as it finds them.
void vm_events(void);

// --- Inspecting --------------------------------------------------------------

s32 vm_global(u16 index);
void vm_set_global(u16 index, s32 value);
// The object entity e is attached to (vm_attach, SPAWN; an OBJ_* number), or
// -1 if it isn't attached: a dead entity, ENTITY_NONE, one never attached,
// detached, or attached before the latest vm_load. Scripts read the same as
// VM_P_OBJECT (Lua's e.object). E.g. the instance a camera follows:
//     for (u32 i = 0; i < MAX_ENT; i++)
//         if (vm_object_of(entity_at(i)) == OBJ_PLAYER) ...
// An entity destroyed behind the VM's back (entity_destroy rather than
// vm_kill) is not attached: its binding is cleared, its script halted, with a
// warning, as wherever the VM finds one.
int vm_object_of(Entity e);
u32 vm_ops_this_frame(void); // opcodes run since the latest vm_step() began
bool vm_idle(void);          // no context live and no event queued

#endif // SERVAL_VM_H
