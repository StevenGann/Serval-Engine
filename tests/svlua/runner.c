// svlua_runner: runs a script blob on the engine's VM, on the host, and
// prints what it did as text. Built by the host preset; tools/svlua_test.py
// and tools/svlua_difftest.py run compiled Lua (docs/lua.md) on it.
//
//   svlua_runner BLOB.bin [--frames N] [--seed S] [--set GLOBAL:VALUE]...
//                [--start OBJ[:EVENT]]... [--attach OBJ[:X:Y]]...
//                [--buttons FRAME:MASK[:LENGTH]]... [--movement] [--physics G]
//                [--collide OBJ:OTHER]... [--print all|last|F,F,...]
//
// The blob is loaded (vm_load, after ecs_reset), the globals --set (after
// the blob's initial values, standing in for game C code), then the threads are
// started (--start: vm_start, EVENT a VM_EV_* number or name, ROOM_START if
// left out) and the entities attached (--attach: entity_create with the
// object's component mask, its sprite if the mask has C_SPR, and the
// position X, Y in whole pixels, then vm_attach), in the order given. Each of
// the N frames (default 1) runs, as a game's loop would without rendering:
//   vm_step(); sys_movement() with --movement; sys_physics() with --physics,
//   which reports contacts (physics_set_contacts) and pulls bodies down by G
//   (256ths of a pixel per frame per frame) inside the default bounds (the
//   screen); the collision pairs of each
//   --collide (every attached instance of OBJ against every one of OTHER, in
//   slot order, OBJ's outermost: body_overlap, then
//   vm_event(obj, other, VM_EV_COLLISION)); vm_events().
// --buttons holds MASK (VM button bits, BUTTON_* in core.h) from frame FRAME
// (frames count from 1) for LENGTH frames (default 1): button_down() sees the
// held buttons, button_pressed() those held this frame and not the last.
// Nothing else runs: no paths, no animation, no sprite tables, so WAIT_MOVE
// continues at once and WAIT_ANIM warns and continues.
//
// Output, one record per line, numbers in decimal:
//   frame F                      at the start of frame F
//   call FN A0 A1 A2 A3 ["TEXT"]  each engine call the platform makes (VM_SYS_*
//                                number and arguments; TEXT_PRINT's string),
//                                as it happens; button queries are not listed
//   global G VALUE               after each printed frame: every global the
//                                blob declares
//   array N V0 V1 ...            every RAM array's cells (ROM arrays: none)
//   entity HANDLE OBJECT P0 ... P19 F0 ... F15
//                                every attached entity, in slot order: its
//                                object (VM_P_OBJECT), its engine properties
//                                (VM_P_X to VM_P_BODY_CONTACT) as GETP reads
//                                them, then its instance fields
//   warnings N                   at the end: the engine's warnings (debug.h)
// Exit status 0, or 2 for bad arguments or a blob that vm_load rejects.

#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/physics.h"
#include "serval/random.h"
#include "serval/vm.h"

#include "../../src/core/vm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_ITEMS = 64, MAX_BLOB = 1 << 20 };

typedef struct {
    u32 object, event;
} Start;

typedef struct {
    u32 object;
    s32 x, y;
} Attach;

typedef struct {
    u32 frame, mask, length;
} Buttons;

typedef struct {
    u32 object, other;
} Collide;

typedef struct {
    u32 global;
    s32 value;
} Set;

static u8 blob[MAX_BLOB];
static u32 blob_size;
static u32 frame;               // the frame running, from 1
static u32 held, held_before;   // buttons this frame and the last
static bool print_every = true; // --print all (the default)
static bool print_last;         // --print last
static u32 print_frames[MAX_ITEMS];
static u32 print_count;

static void usage(const char* problem) {
    fprintf(stderr, "svlua_runner: %s\n", problem);
    fprintf(stderr, "usage: svlua_runner BLOB.bin [--frames N] [--seed S] [--set GLOBAL:VALUE]... "
                    "[--start OBJ[:EVENT]]... [--attach OBJ[:X:Y]]... "
                    "[--buttons FRAME:MASK[:LENGTH]]... [--movement] [--physics G] "
                    "[--collide OBJ:OTHER]... "
                    "[--print all|last|F,F,...]\n");
    exit(2);
}

static u32 le16(const u8* p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

static u32 le32(const u8* p) {
    return le16(p) | le16(p + 2) << 16;
}

// A whole number in decimal or 0x hex, or usage().
static long number(const char* text, const char* what) {
    char* end;
    long value = strtol(text, &end, 0);
    if (end == text || *end)
        usage(what);
    return value;
}

// Splits text at ':' into at most n parts (in place); returns how many.
static u32 split(char* text, char** parts, u32 n) {
    u32 count = 0;
    while (count < n) {
        parts[count++] = text;
        char* colon = strchr(text, ':');
        if (!colon)
            break;
        *colon = 0;
        text = colon + 1;
    }
    return count;
}

static u32 event_number(const char* text) {
    static const char* const names[VM_EV_COUNT] = {"CREATE",    "STEP",     "DESTROY",
                                                   "COLLISION", "ANIM_END", "ROOM_START"};
    for (u32 k = 0; k < VM_EV_COUNT; k++)
        if (!strcmp(text, names[k]))
            return k;
    return (u32)number(text, "an event is a VM_EV_* number or name (ROOM_START)");
}

static void read_blob(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    blob_size = (u32)fread(blob, 1, sizeof blob, f);
    bool more = fgetc(f) != EOF;
    fclose(f);
    if (more)
        usage("the blob is larger than 1 MiB");
}

// --- Engine calls ------------------------------------------------------------

// Called by the host platform for each engine call the VM makes there
// (vm_internal.h): answers the button queries from the held buttons, prints
// every other call.
static void on_call(void) {
    ServalHostVmCalls* r = &serval_host_vm_calls;
    if (r->fn == VM_SYS_BUTTON_DOWN || r->fn == VM_SYS_BUTTON_PRESSED) {
        u32 buttons = r->fn == VM_SYS_BUTTON_DOWN ? held : held & ~held_before;
        r->button_value = (buttons & (u32)r->args[0]) != 0;
        return;
    }
    printf("call %u %d %d %d %d", r->fn, r->args[0], r->args[1], r->args[2], r->args[3]);
    if (r->fn == VM_SYS_TEXT_PRINT && r->ptr)
        printf(" \"%s\"", (const char*)r->ptr);
    printf("\n");
}

// --- The state ---------------------------------------------------------------

static void print_state(void) {
    u32 objects = le16(blob + 8), strings = le16(blob + 10), globals = le16(blob + 12);
    u32 arrays = le16(blob + 14);
    for (u32 g = 0; g < globals; g++)
        printf("global %u %d\n", g, vm_global((u16)g));
    const u8* table = blob + VM_HEADER_SIZE + objects * VM_OBJECT_SIZE + strings * 4;
    for (u32 n = 0; n < arrays; n++) {
        const u8* record = table + n * VM_ARRAY_RECORD_SIZE;
        if (record[2] != VM_ARRAY_RAM)
            continue;
        u32 length = le16(record), first = le32(record + 4);
        printf("array %u", n);
        for (u32 k = 0; k < length; k++)
            printf(" %d", serval_vm_array_cell(first + k));
        printf("\n");
    }
    for (u32 slot = 0; slot < MAX_ENT; slot++) {
        Entity e = entity_at(slot);
        int object = serval_vm_attached_object(e);
        if (object < 0)
            continue;
        printf("entity %u %d %d %d %d %d %u %u %u %u %d %d %u %u %u %u %u %u %u %u %d %u", (u32)e,
               object, pos_x[slot], pos_y[slot], vel_x[slot], vel_y[slot], (u32)spr_id[slot],
               (u32)spr_frame[slot], (u32)spr_flags[slot], (u32)spr_angle[slot],
               (s32)spr_depth[slot], (s32)spr_scale[slot], (u32)body_w[slot], (u32)body_h[slot],
               ent_mask[slot] >> 16 & 0x7FFFu, (u32)spr_anim_time[slot], (u32)spr_anim_step[slot],
               (u32)body_bounce[slot], (u32)body_friction[slot], (u32)body_max_fall[slot],
               (s32)body_gravity[slot], (u32)body_contact[slot]);
        for (u32 n = 0; n < VM_FIELDS; n++)
            printf(" %d", serval_vm_field(e, n));
        printf("\n");
    }
}

static bool printed(u32 f, u32 frames) {
    if (print_every || (print_last && f == frames))
        return true;
    for (u32 k = 0; k < print_count; k++)
        if (print_frames[k] == f)
            return true;
    return false;
}

// --- Collisions --------------------------------------------------------------

static void collide(const Collide* c) {
    for (u32 a = 0; a < MAX_ENT; a++) {
        Entity ea = entity_at(a);
        if (serval_vm_attached_object(ea) != (int)c->object)
            continue;
        for (u32 b = 0; b < MAX_ENT; b++) {
            Entity eb = entity_at(b);
            if (a != b && serval_vm_attached_object(eb) == (int)c->other && body_overlap(a, b))
                vm_event(ea, eb, VM_EV_COLLISION);
        }
    }
}

int main(int argc, char** argv) {
    static Start starts[MAX_ITEMS];
    static Attach attaches[MAX_ITEMS];
    static Buttons buttons[MAX_ITEMS];
    static Collide collides[MAX_ITEMS];
    static Set sets[MAX_ITEMS];
    u32 start_count = 0, attach_count = 0, button_count = 0, collide_count = 0, set_count = 0;
    u32 frames = 1;
    bool movement = false, physics = false;
    s32 gravity = 0;
    const char* path = NULL;
    for (int k = 1; k < argc; k++) {
        const char* arg = argv[k];
        bool has_value = k + 1 < argc;
        char* parts[3];
        if (!strcmp(arg, "--movement")) {
            movement = true;
            continue;
        }
        if (arg[0] != '-') {
            if (path)
                usage("one blob only");
            path = arg;
            continue;
        }
        if (!has_value)
            usage("an option without its value");
        char* value = argv[++k];
        if (!strcmp(arg, "--physics")) {
            physics = true;
            gravity = (s32)number(value, "--physics takes a gravity in 256ths");
        } else if (!strcmp(arg, "--frames")) {
            frames = (u32)number(value, "--frames takes a number");
        } else if (!strcmp(arg, "--seed")) {
            random_seed((u32)number(value, "--seed takes a number"));
        } else if (!strcmp(arg, "--set")) {
            if (set_count == MAX_ITEMS)
                usage("too many --set");
            if (split(value, parts, 2) != 2)
                usage("--set GLOBAL:VALUE");
            sets[set_count].global = (u32)number(parts[0], "--set GLOBAL:VALUE");
            sets[set_count].value = (s32)number(parts[1], "--set GLOBAL:VALUE");
            set_count++;
        } else if (!strcmp(arg, "--start")) {
            if (start_count == MAX_ITEMS)
                usage("too many --start");
            u32 n = split(value, parts, 2);
            starts[start_count].object = (u32)number(parts[0], "--start OBJ[:EVENT]");
            starts[start_count].event = n > 1 ? event_number(parts[1]) : VM_EV_ROOM_START;
            start_count++;
        } else if (!strcmp(arg, "--attach")) {
            if (attach_count == MAX_ITEMS)
                usage("too many --attach");
            u32 n = split(value, parts, 3);
            if (n == 2)
                usage("--attach OBJ[:X:Y]");
            Attach* a = &attaches[attach_count++];
            a->object = (u32)number(parts[0], "--attach OBJ[:X:Y]");
            a->x = n > 1 ? (s32)number(parts[1], "--attach OBJ[:X:Y]") : 0;
            a->y = n > 1 ? (s32)number(parts[2], "--attach OBJ[:X:Y]") : 0;
        } else if (!strcmp(arg, "--buttons")) {
            if (button_count == MAX_ITEMS)
                usage("too many --buttons");
            u32 n = split(value, parts, 3);
            if (n < 2)
                usage("--buttons FRAME:MASK[:LENGTH]");
            Buttons* b = &buttons[button_count++];
            b->frame = (u32)number(parts[0], "--buttons FRAME:MASK[:LENGTH]");
            b->mask = (u32)number(parts[1], "--buttons FRAME:MASK[:LENGTH]");
            b->length = n > 2 ? (u32)number(parts[2], "--buttons FRAME:MASK[:LENGTH]") : 1;
        } else if (!strcmp(arg, "--collide")) {
            if (collide_count == MAX_ITEMS)
                usage("too many --collide");
            if (split(value, parts, 2) != 2)
                usage("--collide OBJ:OTHER");
            collides[collide_count].object = (u32)number(parts[0], "--collide OBJ:OTHER");
            collides[collide_count].other = (u32)number(parts[1], "--collide OBJ:OTHER");
            collide_count++;
        } else if (!strcmp(arg, "--print")) {
            print_every = !strcmp(value, "all");
            print_last = !strcmp(value, "last");
            if (!print_every && !print_last) {
                for (char* item = strtok(value, ","); item; item = strtok(NULL, ",")) {
                    if (print_count == MAX_ITEMS)
                        usage("too many frames to --print");
                    print_frames[print_count++] = (u32)number(item, "--print all|last|F,F,...");
                }
            }
        } else {
            usage("unknown option");
        }
    }
    if (!path)
        usage("no blob");
    read_blob(path);

    ecs_reset();
    if (physics) {
        physics_set_contacts(true);
        physics_set_gravity(0, gravity);
    }
    if (!vm_load(blob, blob_size)) {
        fprintf(stderr, "svlua_runner: %s: vm_load rejects it\n", path);
        return 2;
    }
    serval_host_vm_calls.during = on_call;
    for (u32 k = 0; k < set_count; k++)
        vm_set_global((u16)sets[k].global, sets[k].value);
    for (u32 k = 0; k < start_count; k++)
        vm_start((u16)starts[k].object, (u8)starts[k].event);
    u32 objects = le16(blob + 8);
    for (u32 k = 0; k < attach_count; k++) {
        const Attach* a = &attaches[k];
        if (a->object >= objects)
            usage("--attach: the blob has no such object");
        const u8* record = blob + VM_HEADER_SIZE + a->object * VM_OBJECT_SIZE;
        Entity e = entity_create(le32(record));
        u32 slot = entity_index(e);
        pos_x[slot] = FX(a->x);
        pos_y[slot] = FX(a->y);
        if (le32(record) & C_SPR)
            spr_id[slot] = (u16)le16(record + 4);
        vm_attach(e, (u16)a->object);
    }

    for (frame = 1; frame <= frames; frame++) {
        printf("frame %u\n", frame);
        held_before = held;
        held = 0;
        for (u32 k = 0; k < button_count; k++)
            if (frame >= buttons[k].frame && frame - buttons[k].frame < buttons[k].length)
                held |= buttons[k].mask;
        vm_step();
        if (movement)
            sys_movement();
        if (physics)
            sys_physics();
        for (u32 k = 0; k < collide_count; k++)
            collide(&collides[k]);
        vm_events();
        if (printed(frame, frames))
            print_state();
    }
    printf("warnings %u\n", debug_warning_count());
    return 0;
}
