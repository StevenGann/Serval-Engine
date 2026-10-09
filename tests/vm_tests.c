// Tests for the bytecode VM (src/core/vm.c), written from docs/vm.md alone:
// each case names the rule it checks ("vm.md <section>"). The engine ships no
// assembler, so programs are built here by a small blob builder, one op per
// line. Run natively (ASan/UBSan) and in the test ROM.
//
// Warnings follow vm.md "Exact semantics: Warnings repeat once per problem,
// per loaded blob": a test expects exactly one warning the first time a kind
// of problem happens after a load, and none for repeats until the next load.

#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/math.h"
#include "serval/path.h"
#include "serval/physics.h"
#include "serval/random.h"
#include "serval/sprites.h"
#include "serval/text.h"
#include "serval/vm.h"
#include "test.h"

#include "../src/core/sprite_internal.h"
#include "../src/core/vm_internal.h" // serval_host_vm_calls only in host builds

// Exactly n warnings since `before` in debug builds (both test presets);
// release builds compile warnings out.
#ifdef SERVAL_DEBUG
#define CHECK_WARNED(before, n) CHECK(debug_warning_count() - (before) == (u32)(n))
#else
#define CHECK_WARNED(before, n) CHECK(debug_warning_count() == (before))
#endif

// --- Blob builder ------------------------------------------------------------

enum { BLOB_CAP = 2048, MAX_LABELS = 16, MAX_FIXUPS = 32 };
#define NO_LABEL 0xFFFFFFFFu

typedef struct {
    u8 bytes[BLOB_CAP];
    u32 size;
    u16 objects, strings, arrays;
    u32 labels[MAX_LABELS];
    u32 fixup_at[MAX_FIXUPS]; // operands to fill in once labels are known
    u8 fixup_label[MAX_FIXUPS];
    bool fixup_absolute[MAX_FIXUPS]; // CALL's u32 blob offset, else a rel16
    u32 fixups;
    bool broken; // out of room or a bad object, string or label: CHECKed by blob_end
} Builder;

static SERVAL_EWRAM_BSS Builder bld;

static void emit(u32 byte) {
    if (bld.size < BLOB_CAP)
        bld.bytes[bld.size++] = (u8)byte;
    else
        bld.broken = true;
}

static void emit16(u32 value) {
    emit(value & 0xFF);
    emit(value >> 8 & 0xFF);
}

static void emit32(u32 value) {
    emit16(value & 0xFFFF);
    emit16(value >> 16);
}

static void put16(u32 at, u32 value) {
    bld.bytes[at] = (u8)value;
    bld.bytes[at + 1] = (u8)(value >> 8);
}

static void put32(u32 at, u32 value) {
    put16(at, value & 0xFFFF);
    put16(at + 2, value >> 16);
}

// vm.md "Blob format": the 16-byte header, then the object table (32 bytes
// per object), the string table (4 bytes per string) and the array table (8
// bytes per array), zeroed: no handlers, mask 0, sprite 0, empty RAM arrays
// at cell 0. Strings, ROM array data and code follow.
static void blob_begin_arrays(u16 objects, u16 strings, u16 globals, u16 arrays) {
    bld.size = 0;
    bld.objects = objects;
    bld.strings = strings;
    bld.arrays = arrays;
    bld.fixups = 0;
    bld.broken = false;
    for (u32 l = 0; l < MAX_LABELS; l++)
        bld.labels[l] = NO_LABEL;
    emit('S');
    emit('V');
    emit('M');
    emit('B');
    emit(VM_FORMAT_VERSION);
    emit(VM_CELL_BYTES);
    emit16(0); // flags
    emit16(objects);
    emit16(strings);
    emit16(globals);
    emit16(arrays);
    for (u32 k = 0; k < (u32)objects * VM_OBJECT_SIZE + 4u * strings + 8u * arrays; k++)
        emit(0);
}

// A blob without arrays: laid out exactly as before arrays existed.
static void blob_begin(u16 objects, u16 strings, u16 globals) {
    blob_begin_arrays(objects, strings, globals, 0);
}

// vm.md "Array table": like blob_begin_arrays, with header flag bit 0 and the
// globals' initial values (globals x s32, little-endian) after the array
// table.
static void blob_begin_values(u16 objects, u16 strings, u16 arrays, const s32* values,
                              u16 globals) {
    blob_begin_arrays(objects, strings, globals, arrays);
    put16(6, VM_FLAG_GLOBAL_VALUES);
    for (u32 k = 0; k < globals; k++)
        emit32((u32)values[k]);
}

static u32 object_record(u32 obj) {
    return VM_HEADER_SIZE + obj * VM_OBJECT_SIZE;
}

// Object obj's default component mask and sprite.
static void object(u16 obj, u32 mask, u16 sprite) {
    if (obj >= bld.objects) {
        bld.broken = true;
        return;
    }
    put32(object_record(obj), mask);
    put16(object_record(obj) + 4, sprite);
}

// Makes the code emitted next object obj's handler for `event`.
static void handler(u16 obj, u32 event) {
    if (obj >= bld.objects || event >= VM_EV_COUNT) {
        bld.broken = true;
        return;
    }
    put32(object_record(obj) + 8 + 4 * event, bld.size);
}

// Emits string `index` here (NUL-terminated); returns its blob offset.
static u32 string(u16 index, const char* text) {
    u32 at = bld.size;
    if (index >= bld.strings) {
        bld.broken = true;
        return at;
    }
    put32(object_record(bld.objects) + 4u * index, at);
    for (;; text++) {
        emit((u8)*text);
        if (!*text)
            break;
    }
    return at;
}

// vm.md "Array table": array n's record.
static u32 array_record(u32 n) {
    return object_record(bld.objects) + 4u * bld.strings + 8u * n;
}

enum { ARRAY_RAM, ARRAY_S8, ARRAY_U8, ARRAY_S16, ARRAY_U16, ARRAY_S32 };

// Array n: RAM, `length` cells from pool cell `first`.
static void ram_array(u32 n, u32 length, u32 first) {
    if (n >= bld.arrays) {
        bld.broken = true;
        return;
    }
    put16(array_record(n), length);
    bld.bytes[array_record(n) + 2] = ARRAY_RAM;
    put32(array_record(n) + 4, first);
}

// Array n: ROM data of `kind`, its elements emitted here (little-endian,
// packed). Returns their blob offset.
static u32 rom_array(u32 n, u32 kind, const s32* values, u32 length) {
    static const u8 bytes[] = {0, 1, 1, 2, 2, 4};
    u32 at = bld.size;
    if (n >= bld.arrays || kind == ARRAY_RAM || kind > ARRAY_S32) {
        bld.broken = true;
        return at;
    }
    put16(array_record(n), length);
    bld.bytes[array_record(n) + 2] = (u8)kind;
    put32(array_record(n) + 4, at);
    for (u32 k = 0; k < length; k++)
        for (u32 b = 0; b < bytes[kind]; b++)
            emit((u32)values[k] >> (8 * b) & 0xFF);
    return at;
}

static void op(u32 code) {
    emit(code);
}

static void op8(u32 code, u32 operand) {
    emit(code);
    emit(operand);
}

static void op16(u32 code, u32 operand) {
    emit(code);
    emit16(operand);
}

static void push8(s32 value) {
    op8(VM_OP_PUSH8, (u32)value & 0xFF);
}

static void push16(s32 value) {
    op16(VM_OP_PUSH16, (u32)value & 0xFFFF);
}

static void push32(s32 value) {
    emit(VM_OP_PUSH32);
    emit32((u32)value);
}

// The shortest push for the value.
static void push(s32 value) {
    if (value >= INT8_MIN && value <= INT8_MAX)
        push8(value);
    else if (value >= INT16_MIN && value <= INT16_MAX)
        push16(value);
    else
        push32(value);
}

static void ldg(u32 n) {
    op8(VM_OP_LDG, n);
}

static void stg(u32 n) {
    op8(VM_OP_STG, n);
}

static void ldl(u32 n) {
    op8(VM_OP_LDL, n);
}

static void stl(u32 n) {
    op8(VM_OP_STL, n);
}

static void getp(u32 prop) {
    op8(VM_OP_GETP, prop);
}

static void setp(u32 prop) {
    op8(VM_OP_SETP, prop);
}

static void sys(u32 fn) {
    op8(VM_OP_SYS, fn);
}

static void spawn(u32 obj) {
    op16(VM_OP_SPAWN, obj);
}

static void trace(u32 str) {
    op16(VM_OP_TRACE, str);
}

// ENTER p, n: the top p cells become the frame's first locals, n zeroed ones
// follow.
static void enter(u32 p, u32 n) {
    emit(VM_OP_ENTER);
    emit(p);
    emit(n);
}

static void lda(u32 array) {
    op16(VM_OP_LDA, array);
}

static void sta(u32 array) {
    op16(VM_OP_STA, array);
}

static void len(u32 array) {
    op16(VM_OP_LEN, array);
}

static void nexti(u32 obj) {
    op16(VM_OP_NEXTI, obj);
}

static void label(u32 l) {
    if (l < MAX_LABELS)
        bld.labels[l] = bld.size;
    else
        bld.broken = true;
}

static void fixup(u32 l, bool absolute) {
    if (bld.fixups == MAX_FIXUPS || l >= MAX_LABELS) {
        bld.broken = true;
        return;
    }
    bld.fixup_at[bld.fixups] = bld.size;
    bld.fixup_label[bld.fixups] = (u8)l;
    bld.fixup_absolute[bld.fixups] = absolute;
    bld.fixups++;
}

// JMP, JZ or JNZ to label l.
static void jump(u32 code, u32 l) {
    emit(code);
    fixup(l, false);
    emit16(0);
}

// CALL label l.
static void call(u32 l) {
    emit(VM_OP_CALL);
    fixup(l, true);
    emit32(0);
}

// Fills in jump and call targets; returns the blob's size.
static u32 blob_end(void) {
    for (u32 f = 0; f < bld.fixups; f++) {
        u32 at = bld.fixup_at[f];
        u32 target = bld.labels[bld.fixup_label[f]];
        if (target == NO_LABEL)
            bld.broken = true;
        else if (bld.fixup_absolute[f])
            put32(at, target); // vm.md "Control flow": CALL takes a blob offset
        else
            put16(at, target - (at + 2)); // rel16 counts from just after the operand
    }
    CHECK(!bld.broken);
    return bld.size;
}

// Loaded blobs go at the very end of one of two static buffers, alternately:
// a read past a blob's end is then caught by ASan on the host, and the blob a
// vm_reload replaces stays intact. Nothing needs alignment (vm.md "Blob
// format"), so wherever the end puts them is fine.
static SERVAL_EWRAM_BSS u8 blob_a[BLOB_CAP];
static SERVAL_EWRAM_BSS u8 blob_b[BLOB_CAP];
static const u8* placed; // the latest blob place() copied
static u32 placed_size;

static void place(void) {
    static bool second;
    u8* buffer = second ? blob_b : blob_a;
    second = !second;
    placed_size = blob_end();
    u8* to = buffer + BLOB_CAP - placed_size;
    for (u32 k = 0; k < placed_size; k++)
        to[k] = bld.bytes[k];
    placed = to;
}

static bool load(void) {
    place();
    return vm_load(placed, placed_size);
}

static bool reload(void) {
    place();
    return vm_reload(placed, placed_size);
}

// --- Program pieces and frame helpers ----------------------------------------

// Globals most blobs declare; the witness thread writes the last one.
enum { GLOBALS = 32, W = GLOBALS - 1 };

// glob[g] = value
static void store(u32 g, s32 value) {
    push(value);
    stg(g);
}

// glob[g] += 1
static void count(u32 g) {
    ldg(g);
    push8(1);
    op(VM_OP_ADD);
    stg(g);
}

// glob[g] = glob[g] * 10 + digit: records the order things ran in.
static void append(u32 g, s32 digit) {
    ldg(g);
    push8(10);
    op(VM_OP_MUL);
    push8(digit);
    op(VM_OP_ADD);
    stg(g);
}

// glob[g] = glob[g] * 10 + the value on top of the stack, which is popped.
static void append_top(u32 g) {
    ldg(g);
    push8(10);
    op(VM_OP_MUL);
    op(VM_OP_ADD);
    stg(g);
}

// WAIT n
static void wait_frames(s32 n) {
    push(n);
    op(VM_OP_WAIT);
}

// A thread showing that faults halt only their own context: glob[W] = 1 in
// the phase it first runs, 2 in the next frame's.
static void witness(u16 obj) {
    handler(obj, VM_EV_CREATE);
    store(W, 1);
    wait_frames(1);
    store(W, 2);
    op(VM_OP_HALT);
}

// Every case starts from an empty ECS, no blob and no collision pairs, so
// cases are independent.
static void reset(void) {
    ecs_reset();
    vm_unload();
    vm_collide_clear();
}

static void start(u16 obj) {
    int ctx = vm_start(obj, VM_EV_CREATE);
    CHECK(ctx >= 0 && ctx < VM_CONTEXTS);
}

// One frame of the two-phase loop (vm.md "Scheduling"), without C systems.
static void frame(void) {
    vm_step();
    vm_events();
}

static void frames(u32 n) {
    for (u32 f = 0; f < n; f++)
        frame();
}

// Starts objects 1..faulty as threads, each running into a fault of the same
// kind, then the witness (object 0), and checks vm.md "Exact semantics:
// Halting a context": each fault halts only its own context; the phase goes
// on (the witness runs in it) and so do later frames. One kind of problem in
// one loaded blob warns once ("Warnings repeat once per problem, per loaded
// blob"), however many contexts run into it.
static void expect_faults_halt_only_themselves(u16 faulty) {
    for (u16 obj = 1; obj <= faulty; obj++)
        start(obj);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(W) == 1);
    CHECK_WARNED(before, 1);
    vm_events();
    vm_step();
    CHECK(vm_global(W) == 2);
    CHECK(vm_idle()); // the faulty contexts are free
    CHECK_WARNED(before, 1);
}

// --- Golden example ----------------------------------------------------------

// vm.md "Worked example (golden bytes)", verbatim.
static const u8 golden[63] = {
    0x53, 0x56, 0x4D, 0x42, 0x01, 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x34, 0x00, 0x00, 0x00, 0x48, 0x49, 0x00, 0x02, 0x05, 0x02, 0x07, 0x10, 0x09, 0x00, 0x01,
};

// The worked example's program, through the builder.
static void build_golden(void) {
    blob_begin(1, 1, 1);
    object(0, 0, 0); // mask 0, sprite 0
    string(0, "HI"); // string 0 @ 0x34
    handler(0, VM_EV_CREATE);
    push8(5);       // 5
    push8(7);       // 5 7
    op(VM_OP_ADD);  // 12
    stg(0);         // glob[0] = 12
    op(VM_OP_HALT); //
}

// vm.md "Worked example (golden bytes)": the builder makes those 63 bytes, and
// the VM loads and runs them.
static void golden_example(void) {
    build_golden();
    u32 size = blob_end();
    CHECK(size == sizeof golden);
    u32 differ = 0;
    for (u32 k = 0; k < size && k < sizeof golden; k++)
        differ += bld.bytes[k] != golden[k];
    CHECK(differ == 0);

    reset();
    u32 before = debug_warning_count();
    CHECK(vm_load(golden, sizeof golden));
    int ctx = vm_start(0, VM_EV_CREATE);
    CHECK(ctx >= 0 && ctx < VM_CONTEXTS);
    CHECK(!vm_idle()); // "Starting scripts": vm_start allocates a context at once
    vm_step();
    CHECK(vm_global(0) == 12);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// --- Stack, variables, arithmetic --------------------------------------------

// vm.md "Stack and variables": each op's stack effect, seen through STG; LDG
// sees vm_set_global; ENTER's locals start zeroed in every new context
// ("Frames").
static void stack_and_variable_ops(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 8);    // locals 0-7, zeroed
    op(VM_OP_NOP);  //
    push8(-2);      // -2
    push16(1000);   // -2 1000
    push32(100000); // -2 1000 100000
    stg(0);         // glob[0] = 100000
    stg(1);         // glob[1] = 1000
    stg(2);         // glob[2] = -2
    push8(3);       // 3
    op(VM_OP_DUP);  // 3 3
    stg(3);         // glob[3] = 3
    stg(4);         // glob[4] = 3
    push8(1);       // 1
    push8(2);       // 1 2
    op(VM_OP_SWAP); // 2 1
    stg(5);         // glob[5] = 1
    stg(6);         // glob[6] = 2
    push8(8);       // 8
    push8(9);       // 8 9
    op(VM_OP_DROP); // 8
    stg(7);         // glob[7] = 8
    ldg(0);         // 100000
    stg(8);         // glob[8] = 100000
    ldg(20);        // set from C
    stg(9);         // glob[9] = -77
    ldl(5);         // 0: locals start zeroed
    stg(10);        // glob[10] = 0
    push8(42);      // 42
    stl(7);         // loc[7] = 42
    ldl(7);         // 42
    stg(11);        // glob[11] = 42
    push8(33);      // 33
    stl(0);         // loc[0] = 33: the next context must not see it
    op(VM_OP_HALT); // ends the handler
    store(12, 1);   // never runs
    handler(1, VM_EV_CREATE);
    enter(0, 8);    //
    ldl(0);         // 0
    stg(13);        // glob[13] = 0
    ldl(7);         // 0
    stg(14);        // glob[14] = 0
    op(VM_OP_HALT); //
    CHECK(load());
    vm_set_global(20, -77);
    vm_set_global(10, 99); // so that zeros are results
    vm_set_global(13, 99);
    vm_set_global(14, 99);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    static const s32 expected[] = {100000, 1000, -2, 3, 3, 1, 2, 8, 100000, -77, 0, 42, 0};
    u32 wrong = 0;
    for (u16 g = 0; g < sizeof expected / sizeof expected[0]; g++)
        wrong += vm_global(g) != expected[g];
    CHECK(wrong == 0);
    CHECK(vm_idle());
    start(1);
    vm_step();
    CHECK(vm_global(13) == 0 && vm_global(14) == 0);
    CHECK_WARNED(before, 0);
}

// vm.md "Stack and variables": PUSH8 and PUSH16 sign-extend; PUSH32 is whole.
static void push_sign_extension(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op8(VM_OP_PUSH8, 0x7F);     // 127
    stg(0);                     //
    op8(VM_OP_PUSH8, 0x80);     // -128
    stg(1);                     //
    op8(VM_OP_PUSH8, 0xFF);     // -1
    stg(2);                     //
    op16(VM_OP_PUSH16, 0x7FFF); // 32767
    stg(3);                     //
    op16(VM_OP_PUSH16, 0x8000); // -32768
    stg(4);                     //
    op16(VM_OP_PUSH16, 0xFFFF); // -1
    stg(5);                     //
    op16(VM_OP_PUSH16, 0x0080); // 128: the low byte's top bit doesn't extend
    stg(6);                     //
    push32(INT32_MIN);          // 0x80000000
    stg(7);                     //
    push32(-1);                 // 0xFFFFFFFF
    stg(8);                     //
    push32(0x12345678);         //
    stg(9);                     //
    op(VM_OP_HALT);             //
    CHECK(load());
    start(0);
    vm_step();
    static const s32 expected[] = {127, -128, -1,        32767, -32768,
                                   -1,  128,  INT32_MIN, -1,    0x12345678};
    u32 wrong = 0;
    for (u16 g = 0; g < sizeof expected / sizeof expected[0]; g++)
        wrong += vm_global(g) != expected[g];
    CHECK(wrong == 0);
}

typedef struct {
    u8 op;
    s32 a, b; // b isn't pushed for NEG, BNOT and LNOT
    s32 expected;
    const char* what; // printed if the row fails
} OpRow;

static bool unary(u32 code) {
    return code == VM_OP_NEG || code == VM_OP_BNOT || code == VM_OP_LNOT;
}

// Evaluates each row as `PUSH32 a; PUSH32 b; op; STG row` (vm.md "Arithmetic,
// logic, comparison": a, b -> a op b), in threads of 32 rows (well within a
// slice), and reports every row whose result differs.
static void check_rows(const OpRow* rows, u32 n) {
    reset();
    u16 threads = (u16)((n + 31) / 32);
    blob_begin(threads, 0, (u16)n);
    for (u32 r = 0; r < n; r++) {
        if (r % 32 == 0) {
            if (r > 0)
                op(VM_OP_HALT);
            handler((u16)(r / 32), VM_EV_CREATE);
        }
        push32(rows[r].a);
        if (!unary(rows[r].op))
            push32(rows[r].b);
        op(rows[r].op);
        stg(r);
    }
    op(VM_OP_HALT);
    CHECK(load());
    for (u32 r = 0; r < n; r++)
        vm_set_global((u16)r, 0x5A5A5A5A); // so that a missing result shows
    for (u16 t = 0; t < threads; t++)
        start(t);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_idle());
    for (u32 r = 0; r < n; r++)
        if (vm_global((u16)r) != rows[r].expected)
            test_fail(__FILE__, __LINE__, text_format("%s [row %u]", rows[r].what, r));
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Arithmetic": two's-complement 32-bit wrapping, DIV
// rounding toward zero, INT32_MIN / -1 and % -1. MOD is taken to be C's %
// (the dividend's sign), which DIV rounding toward zero implies.
static const OpRow arithmetic_rows[] = {
    {VM_OP_ADD, 2, 3, 5, "ADD 2 + 3"},
    {VM_OP_ADD, INT32_MAX, 1, INT32_MIN, "ADD wraps: INT32_MAX + 1"},
    {VM_OP_ADD, -1, -1, -2, "ADD -1 + -1"},
    {VM_OP_SUB, 2, 5, -3, "SUB is a - b: 2 - 5"},
    {VM_OP_SUB, INT32_MIN, 1, INT32_MAX, "SUB wraps: INT32_MIN - 1"},
    {VM_OP_MUL, -6, 7, -42, "MUL -6 * 7"},
    {VM_OP_MUL, INT32_MAX, 2, -2, "MUL wraps: INT32_MAX * 2"},
    {VM_OP_MUL, 0x10000, 0x10000, 0, "MUL wraps: 2^16 * 2^16"},
    {VM_OP_MUL, 0x12345678, 0x09ABCDEF, -498937336, "MUL keeps the low 32 bits"},
    {VM_OP_DIV, 100, 7, 14, "DIV is a / b: 100 / 7"},
    {VM_OP_DIV, 7, 2, 3, "DIV 7 / 2 rounds toward zero"},
    {VM_OP_DIV, -7, 2, -3, "DIV -7 / 2 rounds toward zero"},
    {VM_OP_DIV, 7, -2, -3, "DIV 7 / -2 rounds toward zero"},
    {VM_OP_DIV, -7, -2, 3, "DIV -7 / -2 rounds toward zero"},
    {VM_OP_DIV, INT32_MIN, -1, INT32_MIN, "DIV INT32_MIN / -1 is INT32_MIN"},
    {VM_OP_MOD, 7, 3, 1, "MOD 7 % 3"},
    {VM_OP_MOD, -7, 3, -1, "MOD -7 % 3 has the dividend's sign"},
    {VM_OP_MOD, 7, -3, 1, "MOD 7 % -3 has the dividend's sign"},
    {VM_OP_MOD, -7, -3, -1, "MOD -7 % -3"},
    {VM_OP_MOD, INT32_MIN, -1, 0, "MOD INT32_MIN % -1 is 0"},
    {VM_OP_NEG, 5, 0, -5, "NEG 5"},
    {VM_OP_NEG, -7, 0, 7, "NEG -7"},
    {VM_OP_NEG, 0, 0, 0, "NEG 0"},
    {VM_OP_NEG, INT32_MIN, 0, INT32_MIN, "NEG wraps: INT32_MIN"},
};

static void arithmetic_ops(void) {
    check_rows(arithmetic_rows, sizeof arithmetic_rows / sizeof arithmetic_rows[0]);
}

// vm.md "Exact semantics: Arithmetic": FXMUL is the 64-bit product shifted
// right arithmetically by 8 (rounding toward negative infinity), FXDIV
// (s64)a * 256 / b (rounding toward zero), both truncated modulo 2^32.
static const OpRow fixed_point_rows[] = {
    {VM_OP_FXMUL, FX(3), FX(2), FX(6), "FXMUL 3.0 * 2.0"},
    {VM_OP_FXMUL, -384, 512, -768, "FXMUL -1.5 * 2.0"},
    {VM_OP_FXMUL, -384, -640, 960, "FXMUL -1.5 * -2.5"},
    {VM_OP_FXMUL, 1, 1, 0, "FXMUL (1 * 1) >> 8"},
    {VM_OP_FXMUL, -1, 1, -1, "FXMUL (-1 * 1) >> 8 shifts arithmetically"},
    {VM_OP_FXMUL, -3, 100, -2, "FXMUL (-3 * 100) >> 8 rounds down"},
    {VM_OP_FXMUL, FX(30000), FX(2), FX(60000), "FXMUL has a 64-bit intermediate"},
    {VM_OP_FXMUL, FX(30000), FX(30000), -1528233984, "FXMUL truncates to 32 bits"},
    {VM_OP_FXDIV, FX(3), FX(2), 384, "FXDIV 3.0 / 2.0"},
    {VM_OP_FXDIV, -FX(3), FX(2), -384, "FXDIV -3.0 / 2.0"},
    {VM_OP_FXDIV, -FX(1), -FX(4), 64, "FXDIV -1.0 / -4.0"},
    {VM_OP_FXDIV, FX(10), -FX(3), -853, "FXDIV 10.0 / -3.0 rounds toward zero"},
    {VM_OP_FXDIV, -1, 3, -85, "FXDIV -256 / 3 rounds toward zero"},
    {VM_OP_FXDIV, 1, 3, 85, "FXDIV 256 / 3"},
    {VM_OP_FXDIV, FX(65536), FX(2), FX(32768), "FXDIV has a 64-bit intermediate"},
    {VM_OP_FXDIV, INT32_MIN, -1, 0, "FXDIV INT32_MIN * 256 / -1 truncates to 32 bits"},
    {VM_OP_FXDIV, INT32_MAX, 1, -256, "FXDIV INT32_MAX * 256 truncates to 32 bits"},
};

static void fixed_point_ops(void) {
    check_rows(fixed_point_rows, sizeof fixed_point_rows / sizeof fixed_point_rows[0]);
}

// vm.md "Arithmetic, logic, comparison": shift counts are masked to 31, SHR
// is arithmetic, SHL wraps; LNOT is 0 -> 1, else 0.
static const OpRow bitwise_rows[] = {
    {VM_OP_AND, 0x0F0F, 0x00FF, 0x000F, "AND"},
    {VM_OP_AND, -1, 0x1234, 0x1234, "AND with -1"},
    {VM_OP_OR, 0x0F00, 0x00F0, 0x0FF0, "OR"},
    {VM_OP_OR, INT32_MIN, 1, INT32_MIN + 1, "OR keeps the sign bit"},
    {VM_OP_XOR, 0x0FF0, 0x00FF, 0x0F0F, "XOR"},
    {VM_OP_XOR, -1, 0x0F, -16, "XOR with -1"},
    {VM_OP_BNOT, 0, 0, -1, "BNOT 0"},
    {VM_OP_BNOT, 0x0F, 0, -16, "BNOT 15"},
    {VM_OP_LNOT, 0, 0, 1, "LNOT 0 is 1"},
    {VM_OP_LNOT, 5, 0, 0, "LNOT 5 is 0"},
    {VM_OP_LNOT, -1, 0, 0, "LNOT -1 is 0"},
    {VM_OP_LNOT, INT32_MIN, 0, 0, "LNOT INT32_MIN is 0"},
    {VM_OP_SHL, 1, 4, 16, "SHL is a << b: 1 << 4"},
    {VM_OP_SHL, 1, 31, INT32_MIN, "SHL into the sign bit"},
    {VM_OP_SHL, 0x40000000, 1, INT32_MIN, "SHL wraps"},
    {VM_OP_SHL, -1, 4, -16, "SHL of a negative value"},
    {VM_OP_SHL, 3, 32, 3, "SHL count masked to 31: 32 is 0"},
    {VM_OP_SHL, 1, 33, 2, "SHL count masked to 31: 33 is 1"},
    {VM_OP_SHL, 1, -1, INT32_MIN, "SHL count masked to 31: -1 is 31"},
    {VM_OP_SHR, 256, 4, 16, "SHR is a >> b: 256 >> 4"},
    {VM_OP_SHR, -16, 2, -4, "SHR is arithmetic"},
    {VM_OP_SHR, -7, 1, -4, "SHR -7 >> 1 rounds down"},
    {VM_OP_SHR, INT32_MIN, 31, -1, "SHR INT32_MIN >> 31"},
    {VM_OP_SHR, -1, 40, -1, "SHR count masked to 31: 40 is 8"},
    {VM_OP_SHR, 0x40000000, 33, 0x20000000, "SHR count masked to 31: 33 is 1"},
    {VM_OP_SHR, INT32_MAX, 32, INT32_MAX, "SHR count masked to 31: 32 is 0"},
};

static void bitwise_and_shift_ops(void) {
    check_rows(bitwise_rows, sizeof bitwise_rows / sizeof bitwise_rows[0]);
}

// vm.md "Arithmetic, logic, comparison" (LSH): Lua 5.4's shifts with 32-bit
// integers. The Lua 5.4 manual (3.4.2): "Both right and left shifts fill the
// vacant bits with zeros. Negative displacements shift to the other direction;
// displacements with absolute values equal to or higher than the number of
// bits in an integer result in zero." So for a count n from 0 to 31, a << n is
// a times 2^n, modulo 2^32; from -31 to -1 it is the unsigned value of a
// divided by 2^-n, rounded down; 32 or more either way gives 0. The values
// below were worked out that way, for n = -33 to 33 in order.
enum { SHIFT_MIN = -33, SHIFT_COUNTS = 67 };
static const struct {
    s32 a;
    const char* what;
    u32 expected[SHIFT_COUNTS]; // for n = SHIFT_MIN + index
} shift_sweeps[] = {
    {
        1,
        "LSH 1, n",
        {
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000002,
            0x00000004, 0x00000008, 0x00000010, 0x00000020, 0x00000040, 0x00000080, 0x00000100,
            0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000,
            0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000,
            0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x20000000,
            0x40000000, 0x80000000, 0x00000000, 0x00000000,
        },
    },
    {
        -1,
        "LSH -1, n: right shifts are logical",
        {
            0x00000000, 0x00000000, 0x00000001, 0x00000003, 0x00000007, 0x0000000F, 0x0000001F,
            0x0000003F, 0x0000007F, 0x000000FF, 0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
            0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF, 0x0001FFFF, 0x0003FFFF, 0x0007FFFF,
            0x000FFFFF, 0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF, 0x01FFFFFF, 0x03FFFFFF,
            0x07FFFFFF, 0x0FFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE,
            0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFFF0, 0xFFFFFFE0, 0xFFFFFFC0, 0xFFFFFF80, 0xFFFFFF00,
            0xFFFFFE00, 0xFFFFFC00, 0xFFFFF800, 0xFFFFF000, 0xFFFFE000, 0xFFFFC000, 0xFFFF8000,
            0xFFFF0000, 0xFFFE0000, 0xFFFC0000, 0xFFF80000, 0xFFF00000, 0xFFE00000, 0xFFC00000,
            0xFF800000, 0xFF000000, 0xFE000000, 0xFC000000, 0xF8000000, 0xF0000000, 0xE0000000,
            0xC0000000, 0x80000000, 0x00000000, 0x00000000,
        },
    },
    {
        INT32_MIN,
        "LSH INT32_MIN, n",
        {
            0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010,
            0x00000020, 0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000400, 0x00000800,
            0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000, 0x00040000,
            0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000,
            0x04000000, 0x08000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000,
        },
    },
    {
        0x12345678,
        "LSH 0x12345678, n",
        {
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000002,
            0x00000004, 0x00000009, 0x00000012, 0x00000024, 0x00000048, 0x00000091, 0x00000123,
            0x00000246, 0x0000048D, 0x0000091A, 0x00001234, 0x00002468, 0x000048D1, 0x000091A2,
            0x00012345, 0x0002468A, 0x00048D15, 0x00091A2B, 0x00123456, 0x002468AC, 0x0048D159,
            0x0091A2B3, 0x01234567, 0x02468ACF, 0x048D159E, 0x091A2B3C, 0x12345678, 0x2468ACF0,
            0x48D159E0, 0x91A2B3C0, 0x23456780, 0x468ACF00, 0x8D159E00, 0x1A2B3C00, 0x34567800,
            0x68ACF000, 0xD159E000, 0xA2B3C000, 0x45678000, 0x8ACF0000, 0x159E0000, 0x2B3C0000,
            0x56780000, 0xACF00000, 0x59E00000, 0xB3C00000, 0x67800000, 0xCF000000, 0x9E000000,
            0x3C000000, 0x78000000, 0xF0000000, 0xE0000000, 0xC0000000, 0x80000000, 0x00000000,
            0x00000000, 0x00000000, 0x00000000, 0x00000000,
        },
    },
};

// Counts no sweep reaches: the most negative and positive cells (a >> b
// compiles to LSH a, -b, and -INT32_MIN wraps to INT32_MIN: still 0, as in
// Lua, whose >> negates the count the same way), and zero shifted.
static const OpRow shift_edge_rows[] = {
    {VM_OP_LSH, 1, INT32_MIN, 0, "LSH 1, INT32_MIN"},
    {VM_OP_LSH, -1, INT32_MIN, 0, "LSH -1, INT32_MIN"},
    {VM_OP_LSH, 1, INT32_MAX, 0, "LSH 1, INT32_MAX"},
    {VM_OP_LSH, -1, 1000, 0, "LSH -1, 1000"},
    {VM_OP_LSH, -1, -1000, 0, "LSH -1, -1000"},
    {VM_OP_LSH, 0, 5, 0, "LSH 0, 5"},
    {VM_OP_LSH, 0, -5, 0, "LSH 0, -5"},
    {VM_OP_LSH, 0, 0, 0, "LSH 0, 0"},
    {VM_OP_LSH, -7, -1, 0x7FFFFFFC, "LSH -7, -1: logical"},
    {VM_OP_LSH, -7, 31, INT32_MIN, "LSH -7, 31"},
    {VM_OP_LSH, INT32_MAX, 1, -2, "LSH INT32_MAX, 1 wraps"},
};

static void lua_shift_op(void) {
    // 1.3 KB: in EWRAM, to keep the test ROM's IWRAM for the engine's code.
    static SERVAL_EWRAM_BSS OpRow rows[SHIFT_COUNTS];
    for (u32 t = 0; t < sizeof shift_sweeps / sizeof shift_sweeps[0]; t++) {
        for (u32 k = 0; k < SHIFT_COUNTS; k++)
            rows[k] = (OpRow){VM_OP_LSH, shift_sweeps[t].a, SHIFT_MIN + (s32)k,
                              (s32)shift_sweeps[t].expected[k], shift_sweeps[t].what};
        check_rows(rows, SHIFT_COUNTS);
    }
    check_rows(shift_edge_rows, sizeof shift_edge_rows / sizeof shift_edge_rows[0]);
}

// vm.md "Arithmetic, logic, comparison" (IDIV, IMOD): Lua 5.4's // and % with
// 32-bit integers. The manual (3.4.1): "Floor division (//) is a division
// that rounds the quotient towards minus infinity", and modulo "is defined as
// the remainder of a division that rounds the quotient towards minus
// infinity": a % b = a - (a // b) * b, so a nonzero remainder has b's sign.
// Integer arithmetic wraps around (3.4.1), so INT32_MIN // -1 is INT32_MIN
// (2^31 wrapped) and INT32_MIN % -1 is 0, as vm.md says.
static const OpRow floored_rows[] = {
    {VM_OP_IDIV, 7, 2, 3, "IDIV 7 // 2"},
    {VM_OP_IMOD, 7, 2, 1, "IMOD 7 % 2"},
    {VM_OP_IDIV, -7, 2, -4, "IDIV -7 // 2 rounds down"},
    {VM_OP_IMOD, -7, 2, 1, "IMOD -7 % 2 has the divisor's sign"},
    {VM_OP_IDIV, 7, -2, -4, "IDIV 7 // -2 rounds down"},
    {VM_OP_IMOD, 7, -2, -1, "IMOD 7 % -2 has the divisor's sign"},
    {VM_OP_IDIV, -7, -2, 3, "IDIV -7 // -2"},
    {VM_OP_IMOD, -7, -2, -1, "IMOD -7 % -2"},
    {VM_OP_IDIV, 6, 3, 2, "IDIV 6 // 3"},
    {VM_OP_IMOD, 6, 3, 0, "IMOD 6 % 3"},
    {VM_OP_IDIV, -6, 3, -2, "IDIV -6 // 3: exact, no correction"},
    {VM_OP_IMOD, -6, 3, 0, "IMOD -6 % 3: 0, no correction"},
    {VM_OP_IDIV, 6, -3, -2, "IDIV 6 // -3"},
    {VM_OP_IMOD, 6, -3, 0, "IMOD 6 % -3"},
    {VM_OP_IDIV, -1, 2, -1, "IDIV -1 // 2 is -1"},
    {VM_OP_IMOD, -1, 2, 1, "IMOD -1 % 2 is 1"},
    {VM_OP_IDIV, 1, -2, -1, "IDIV 1 // -2 is -1"},
    {VM_OP_IMOD, 1, -2, -1, "IMOD 1 % -2 is -1"},
    {VM_OP_IDIV, 0, 5, 0, "IDIV 0 // 5"},
    {VM_OP_IMOD, 0, 5, 0, "IMOD 0 % 5"},
    {VM_OP_IDIV, 0, -5, 0, "IDIV 0 // -5"},
    {VM_OP_IMOD, 0, -5, 0, "IMOD 0 % -5"},
    {VM_OP_IDIV, INT32_MIN, -1, INT32_MIN, "IDIV INT32_MIN // -1 wraps"},
    {VM_OP_IMOD, INT32_MIN, -1, 0, "IMOD INT32_MIN % -1"},
    {VM_OP_IDIV, INT32_MIN, 1, INT32_MIN, "IDIV INT32_MIN // 1"},
    {VM_OP_IMOD, INT32_MIN, 1, 0, "IMOD INT32_MIN % 1"},
    {VM_OP_IDIV, INT32_MIN, 2, -1073741824, "IDIV INT32_MIN // 2"},
    {VM_OP_IMOD, INT32_MIN, 2, 0, "IMOD INT32_MIN % 2"},
    {VM_OP_IDIV, INT32_MIN, 3, -715827883, "IDIV INT32_MIN // 3 rounds down"},
    {VM_OP_IMOD, INT32_MIN, 3, 1, "IMOD INT32_MIN % 3"},
    {VM_OP_IDIV, INT32_MIN, INT32_MIN, 1, "IDIV INT32_MIN // INT32_MIN"},
    {VM_OP_IMOD, INT32_MIN, INT32_MIN, 0, "IMOD INT32_MIN % INT32_MIN"},
    {VM_OP_IDIV, INT32_MIN, INT32_MAX, -2, "IDIV INT32_MIN // INT32_MAX rounds down"},
    {VM_OP_IMOD, INT32_MIN, INT32_MAX, 2147483646, "IMOD INT32_MIN % INT32_MAX"},
    {VM_OP_IDIV, INT32_MAX, INT32_MIN, -1, "IDIV INT32_MAX // INT32_MIN"},
    {VM_OP_IMOD, INT32_MAX, INT32_MIN, -1, "IMOD INT32_MAX % INT32_MIN"},
    {VM_OP_IDIV, 1, INT32_MIN, -1, "IDIV 1 // INT32_MIN"},
    {VM_OP_IMOD, 1, INT32_MIN, -2147483647, "IMOD 1 % INT32_MIN"},
    {VM_OP_IDIV, -1, INT32_MIN, 0, "IDIV -1 // INT32_MIN"},
    {VM_OP_IMOD, -1, INT32_MIN, -1, "IMOD -1 % INT32_MIN"},
    {VM_OP_IDIV, INT32_MAX, -1, -INT32_MAX, "IDIV INT32_MAX // -1"},
    {VM_OP_IMOD, INT32_MAX, -1, 0, "IMOD INT32_MAX % -1"},
    {VM_OP_IDIV, INT32_MAX, 2, 1073741823, "IDIV INT32_MAX // 2"},
    {VM_OP_IMOD, INT32_MAX, 2, 1, "IMOD INT32_MAX % 2"},
    {VM_OP_IDIV, -2147483647, 2, -1073741824, "IDIV -INT32_MAX // 2 rounds down"},
    {VM_OP_IMOD, -2147483647, 2, 1, "IMOD -INT32_MAX % 2"},
    {VM_OP_IDIV, -3, INT32_MAX, -1, "IDIV -3 // INT32_MAX"},
    {VM_OP_IMOD, -3, INT32_MAX, 2147483644, "IMOD -3 % INT32_MAX"},
    {VM_OP_IDIV, 100, -7, -15, "IDIV 100 // -7"},
    {VM_OP_IMOD, 100, -7, -5, "IMOD 100 % -7"},
};

static void floored_division_ops(void) {
    check_rows(floored_rows, sizeof floored_rows / sizeof floored_rows[0]);
}

// vm.md "Arithmetic, logic, comparison": signed comparisons, a op b, pushing 1
// or 0.
static const OpRow comparison_rows[] = {
    {VM_OP_EQ, 5, 5, 1, "EQ 5 5"},
    {VM_OP_EQ, 5, 6, 0, "EQ 5 6"},
    {VM_OP_NE, 5, 6, 1, "NE 5 6"},
    {VM_OP_NE, 5, 5, 0, "NE 5 5"},
    {VM_OP_LT, -1, 1, 1, "LT is signed: -1 < 1"},
    {VM_OP_LT, 1, -1, 0, "LT is a < b: 1 < -1"},
    {VM_OP_LT, 3, 3, 0, "LT 3 3"},
    {VM_OP_LT, INT32_MIN, INT32_MAX, 1, "LT INT32_MIN INT32_MAX"},
    {VM_OP_LE, 3, 3, 1, "LE 3 3"},
    {VM_OP_LE, 4, 3, 0, "LE is a <= b: 4 <= 3"},
    {VM_OP_LE, -4, 3, 1, "LE -4 3"},
    {VM_OP_GT, 1, -1, 1, "GT is signed: 1 > -1"},
    {VM_OP_GT, -1, 1, 0, "GT is a > b: -1 > 1"},
    {VM_OP_GT, 3, 3, 0, "GT 3 3"},
    {VM_OP_GE, 3, 3, 1, "GE 3 3"},
    {VM_OP_GE, -5, 2, 0, "GE is a >= b: -5 >= 2"},
    {VM_OP_GE, INT32_MAX, INT32_MIN, 1, "GE INT32_MAX INT32_MIN"},
};

static void comparison_ops(void) {
    check_rows(comparison_rows, sizeof comparison_rows / sizeof comparison_rows[0]);
}

// vm.md "Arithmetic, logic, comparison": DIV, MOD, FXDIV, IDIV and IMOD by
// zero give 0 and warn once (one kind of problem: "Warnings repeat once per
// problem, per loaded blob"); the context goes on.
static void division_by_zero(void) {
    static const u8 divisions[] = {VM_OP_DIV, VM_OP_MOD, VM_OP_FXDIV, VM_OP_IDIV, VM_OP_IMOD};
    enum { N = sizeof divisions };
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    for (u32 round = 0; round < 2; round++) {
        for (u32 k = 0; k < N; k++) {
            push8(k % 2 ? -7 : 7); // a
            push8(0);              // a 0
            op(divisions[k]);      // 0
            stg(N * round + k);    // glob[N * round + k] = 0
        }
    }
    store(2 * N, 1); // carried on
    op(VM_OP_HALT);  //
    CHECK(load());
    for (u16 g = 0; g <= 2 * N; g++)
        vm_set_global(g, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    u32 wrong = 0;
    for (u16 g = 0; g < 2 * N; g++)
        wrong += vm_global(g) != 0;
    CHECK(wrong == 0);
    CHECK(vm_global(2 * N) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// --- Control flow ------------------------------------------------------------

// vm.md "Control flow": JMP both ways, JZ/JNZ taken and not (both pop), CALL
// to a blob offset and RET to just after it, nested.
static void control_flow(void) {
    enum { L_OVER, L_LOOP, L_TAKEN, L_NOT_ZERO, L_ZERO, L_NONZERO, L_SUB1, L_SUB2 };
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 1);             // local 0
    jump(VM_OP_JMP, L_OVER); // forward
    store(0, 99);            // skipped
    label(L_OVER);           //
    store(0, 1);             // glob[0] = 1

    push8(5);                // 5
    stl(0);                  // loc[0] = 5
    label(L_LOOP);           //
    count(1);                // glob[1] += 1
    ldl(0);                  // n
    push8(1);                // n 1
    op(VM_OP_SUB);           // n-1
    op(VM_OP_DUP);           // n-1 n-1
    stl(0);                  // loc[0] = n-1; n-1 stays
    jump(VM_OP_JNZ, L_LOOP); // backward while non-zero: glob[1] = 5

    push8(7);                // 7
    push8(0);                // 7 0
    jump(VM_OP_JZ, L_TAKEN); // taken: pops the 0
    store(2, 99);            // skipped
    label(L_TAKEN);          // 7
    stg(2);                  // glob[2] = 7

    push8(8);                   // 8
    push8(3);                   // 8 3
    jump(VM_OP_JZ, L_NOT_ZERO); // not taken: pops the 3
    stg(3);                     // glob[3] = 8
    label(L_NOT_ZERO);          //

    push8(9);                // 9
    push8(0);                // 9 0
    jump(VM_OP_JNZ, L_ZERO); // not taken: pops the 0
    stg(4);                  // glob[4] = 9
    label(L_ZERO);           //

    push8(10);                  // 10
    push8(-1);                  // 10 -1
    jump(VM_OP_JNZ, L_NONZERO); // taken on any non-zero value: pops it
    store(5, 99);               // skipped
    label(L_NONZERO);           // 10
    stg(5);                     // glob[5] = 10

    call(L_SUB1);   // glob[6] = 1 in L_SUB1, then back here
    count(6);       // glob[6] = 2
    call(L_SUB2);   // calls L_SUB1 (glob[6] = 3), then glob[7] = 1
    store(8, 1);    // back from both
    op(VM_OP_HALT); //
    store(8, 99);   // never runs

    label(L_SUB1); //
    count(6);      //
    op(VM_OP_RET); //

    label(L_SUB2); //
    call(L_SUB1);  //
    count(7);      //
    op(VM_OP_RET); //
    CHECK(load());
    for (u16 g = 2; g <= 5; g++)
        vm_set_global(g, 0x5A5A);
    vm_set_global(8, 0x5A5A);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    static const s32 expected[] = {1, 5, 7, 8, 9, 10, 3, 1, 1};
    u32 wrong = 0;
    for (u16 g = 0; g < sizeof expected / sizeof expected[0]; g++)
        wrong += vm_global(g) != expected[g];
    CHECK(wrong == 0);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Control flow": RET with an empty call stack ends the handler like
// HALT, without a warning (handlers may end that way, "Load-time validation").
static void ret_with_empty_call_stack_halts(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    // glob[0] = 1
    op(VM_OP_RET);  // nothing to return to: ends here
    store(0, 2);    // never runs
    op(VM_OP_HALT); //
    CHECK(load());
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Frames", "Exact semantics: Frames": ENTER p, n makes the top p
// cells (the caller's arguments) the frame's first locals and pushes n zeroed
// ones; RETV pops a value, drops the callee's whole frame (arguments, locals,
// temporaries) and pushes the value; RET drops the frame. A callee without
// ENTER has a frame that starts at the stack top, so RET leaves the caller's
// arguments to the caller. The caller's locals and temporaries come through
// untouched.
static void frames_hold_arguments_and_locals(void) {
    enum { L_ADD3, L_DROPS, L_KEEPS };
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 1);    // c0
    push8(77);      //
    stl(0);         // c0 = 77
    push8(100);     // c0 100: a temporary
    push8(3);       // c0 100 3
    push8(4);       // c0 100 3 4: arguments
    call(L_ADD3);   // c0 100 17
    stg(0);         // glob[0] = 17
    stg(1);         // glob[1] = 100
    ldl(0);         //
    stg(2);         // glob[2] = 77
    push8(5);       // c0 5: an argument
    call(L_DROPS);  // c0: RET dropped it with the frame
    push8(9);       // c0 9
    ldl(1);         // c0 9 9 (5 if the argument were still there)
    stg(3);         // glob[3] = 9
    op(VM_OP_DROP); // c0
    push8(6);       // c0 6
    call(L_KEEPS);  // c0 6: no ENTER, so the 6 was never the callee's
    stg(4);         // glob[4] = 6
    ldl(0);         //
    stg(5);         // glob[5] = 77
    op(VM_OP_HALT); //
    label(L_ADD3);  // (a, b): a + b + 10
    enter(2, 1);    // a b l2
    ldl(2);         // a b l2 0
    stg(6);         // glob[6] = 0: zeroed
    push8(10);      //
    stl(2);         // l2 = 10
    ldl(0);         //
    ldl(1);         //
    op(VM_OP_ADD);  //
    ldl(2);         //
    op(VM_OP_ADD);  // a b l2 17
    push8(55);      // a b l2 17 55: a temporary left behind
    op(VM_OP_SWAP); // a b l2 55 17
    op(VM_OP_RETV); //
    label(L_DROPS); // (n)
    enter(1, 2);    // n l1 l2
    push8(1);       //
    push8(2);       // n l1 l2 1 2
    op(VM_OP_RET);  //
    label(L_KEEPS); // a frame starting at the stack top
    push8(1);       //
    push8(2);       //
    op(VM_OP_RET);  //
    CHECK(load());
    vm_set_global(6, 99);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    static const s32 expected[] = {17, 100, 77, 9, 6, 77, 0};
    u32 wrong = 0;
    for (u16 g = 0; g < sizeof expected / sizeof expected[0]; g++)
        wrong += vm_global(g) != expected[g];
    CHECK(wrong == 0);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Frames": recursion works to the depth VM_CALLS and VM_STACK allow.
// sum(n) = n + sum(n - 1), sum(0) = 0, is VM_CALLS calls deep for sum(VM_CALLS
// - 1) and fits; one deeper overflows the call stack. A function whose frame
// holds 8 locals runs out of stack after VM_STACK / 8 levels instead. Each
// warns and halts only its own context.
static void recursion_to_the_limits(void) {
    enum { L_SUM, L_MORE, L_DEEP };
    reset();
    blob_begin(3, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(VM_CALLS - 1); // n
    call(L_SUM);         // sum(n)
    stg(0);              // glob[0]
    op(VM_OP_HALT);      //
    handler(1, VM_EV_CREATE);
    store(1, 1);     //
    push8(VM_CALLS); // one call deeper
    call(L_SUM);     // overflows the call stack: warns, halts
    store(1, 2);     // never runs
    op(VM_OP_HALT);  //
    handler(2, VM_EV_CREATE);
    call(L_DEEP);   // overflows the stack: warns, halts
    store(3, 1);    // never runs
    op(VM_OP_HALT); //
    label(L_SUM);   // (n)
    enter(1, 0);    // n
    ldl(0);         //
    jump(VM_OP_JNZ, L_MORE);
    push8(0);       // n 0
    op(VM_OP_RETV); //
    label(L_MORE);  //
    ldl(0);         // n n
    ldl(0);         //
    push8(1);       //
    op(VM_OP_SUB);  // n n n-1
    call(L_SUM);    // n n sum(n-1)
    op(VM_OP_ADD);  // n n+sum(n-1)
    op(VM_OP_RETV); //
    label(L_DEEP);  //
    count(2);       // glob[2]: levels reached
    enter(0, 8);    // 8 cells a level
    call(L_DEEP);   //
    op(VM_OP_RET);  //
    CHECK(load());
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == (VM_CALLS - 1) * VM_CALLS / 2);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    start(1);
    vm_step();
    CHECK(vm_global(1) == 1 && vm_idle());
    CHECK_WARNED(before, 1);
    start(2);
    vm_step();
    CHECK(vm_global(2) == VM_STACK / 8); // the next level has no room left
    CHECK(vm_global(3) == 0 && vm_idle());
    CHECK_WARNED(before, 2);
}

// vm.md "Exact semantics: Frames": RET and RETV at the handler's own level
// end the handler, whatever its stack holds, without a warning (RETV's value
// is discarded, and it needs none).
static void handler_level_returns_end_the_handler(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    push8(5);       //
    push8(6);       // 5 6
    op(VM_OP_RETV); // ends the handler
    store(0, 2);    // never runs
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    store(1, 1);    //
    op(VM_OP_RETV); // an empty stack: ends the handler all the same
    store(1, 2);    // never runs
    op(VM_OP_HALT); //
    CHECK(load());
    start(0);
    start(1);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 1 && vm_global(1) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Control flow" (ENTER): fewer than p cells in the activation warns
// and halts (a stack underflow), only that context.
static void enter_needs_its_arguments(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);    //
    push8(1);       // one cell
    enter(2, 0);    // two arguments: warns, halts
    store(1, 2);    // never runs
    op(VM_OP_HALT); //
    CHECK(load());
    expect_faults_halt_only_themselves(1);
    CHECK(vm_global(1) == 1);
}

// --- Runtime safety ----------------------------------------------------------

// vm.md "Opcode reference": stack overflow on any op warns and halts the
// context; VM_STACK cells fit (ENTER's included).
static void stack_overflow_halts_only_its_context(void) {
    reset();
    blob_begin(4, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);                        //
    for (s32 v = 1; v <= VM_STACK; v++) //
        push8(v);                       // a full stack: 1 .. VM_STACK
    stg(3);                             // glob[3] = VM_STACK: they all fit
    push8(9);                           // full again
    push8(10);                          // one too many: warns, halts
    store(1, 2);                        // never runs
    op(VM_OP_HALT);                     //
    handler(2, VM_EV_CREATE);
    store(2, 1);                        //
    for (s32 v = 1; v <= VM_STACK; v++) //
        push8(v);                       // full
    op(VM_OP_DUP);                      // overflows too
    store(2, 2);                        // never runs
    op(VM_OP_HALT);                     //
    handler(3, VM_EV_CREATE);
    store(4, 1);            //
    enter(0, VM_STACK - 1); // all but one cell: fits
    enter(0, 2);            // one cell left: overflows
    store(4, 2);            // never runs
    op(VM_OP_HALT);         //
    CHECK(load());
    expect_faults_halt_only_themselves(3);
    CHECK(vm_global(1) == 1 && vm_global(2) == 1 && vm_global(4) == 1);
    CHECK(vm_global(3) == VM_STACK);
}

// vm.md "Opcode reference": stack underflow on any op (a binary op, DROP, a
// SYS short of arguments) warns and halts the context.
static void stack_underflow_halts_only_its_context(void) {
    reset();
    blob_begin(4, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);    //
    push8(3);       // 3
    op(VM_OP_ADD);  // one operand short: warns, halts
    store(1, 2);    // never runs
    op(VM_OP_HALT); //
    handler(2, VM_EV_CREATE);
    store(2, 1);    //
    op(VM_OP_DROP); // empty stack
    store(2, 2);    // never runs
    op(VM_OP_HALT); //
    handler(3, VM_EV_CREATE);
    store(3, 1);              //
    push8(1);                 // 1
    sys(VM_SYS_RANDOM_RANGE); // takes two
    store(3, 2);              // never runs
    op(VM_OP_HALT);           //
    CHECK(load());
    expect_faults_halt_only_themselves(3);
    CHECK(vm_global(1) == 1 && vm_global(2) == 1 && vm_global(3) == 1);
}

// vm.md "Control flow": CALL deeper than VM_CALLS overflows the call stack:
// warns, halts.
static void call_depth_is_limited(void) {
    enum { L_F };
    reset();
    blob_begin(2, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);    //
    call(L_F);      // depth 1
    store(1, 2);    // never runs: the context halts inside
    op(VM_OP_HALT); //
    label(L_F);     //
    count(2);       // glob[2] = the depth reached
    call(L_F);      // nested call VM_CALLS + 1 overflows
    op(VM_OP_RET);  //
    CHECK(load());
    expect_faults_halt_only_themselves(1);
    CHECK(vm_global(1) == 1);
    CHECK(vm_global(2) == VM_CALLS);
}

// vm.md "Opcode reference": an unknown opcode warns and halts the context.
// 0x33 held INTERRUPTIBLE before v1 was released: unassigned now ("Waits").
static void unknown_opcode_halts_only_its_context(void) {
    static const u8 unknown[] = {0x0F, 0x2F, 0x33, 0x3F, 0x41, 0xFF};
    reset();
    blob_begin(1 + sizeof unknown, 0, GLOBALS);
    witness(0);
    for (u16 k = 0; k < sizeof unknown; k++) {
        handler(k + 1, VM_EV_CREATE);
        store(k + 1, 1); //
        op(unknown[k]);  // warns, halts
        store(k + 1, 2); // never runs
        op(VM_OP_HALT);  //
    }
    CHECK(load());
    expect_faults_halt_only_themselves(sizeof unknown);
    u32 wrong = 0;
    for (u16 k = 1; k <= sizeof unknown; k++)
        wrong += vm_global(k) != 1;
    CHECK(wrong == 0);
}

// vm.md "Stack and variables", "Exact semantics: Frames": LDL and STL n
// need fp + n < sp (after STL's pop): the frame is ENTER's locals and anything
// above them. Outside it, LDL warns and pushes 0, STL warns and drops the
// value. Neither halts: the context goes on. Both are one kind of problem: one
// warning.
static void locals_out_of_range(void) {
    enum { L_F };
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 2);    // l0 l1: sp 2
    push8(5);       // l0 l1 5
    ldl(3);         // fp + 3 is sp: warns, l0 l1 5 0
    stg(0);         // glob[0] = 0
    stg(1);         // glob[1] = 5
    push8(7);       // l0 l1 7
    push8(9);       // l0 l1 7 9
    stl(3);         // pops the 9; fp + 3 is sp again: warns (no repeat), dropped
    stg(2);         // glob[2] = 7
    push8(42);      // l0 l1 42
    stl(1);         // local 1 = 42
    ldl(1);         // l0 l1 42
    stg(3);         // glob[3] = 42
    push8(11);      // l0 l1 11: a cell above the locals is in the frame too
    ldl(2);         // l0 l1 11 11
    stg(4);         // glob[4] = 11
    op(VM_OP_DROP); // l0 l1
    push8(3);       // l0 l1 3: an argument
    call(L_F);      //
    store(8, 1);    // carried on
    op(VM_OP_HALT); //
    label(L_F);     // the callee's frame starts at the stack top
    ldl(0);         // l0 l1 3 0: outside its frame (warns, no repeat)
    stg(6);         // glob[6] = 0
    enter(1, 0);    // the argument becomes local 0
    ldl(0);         // l0 l1 3 3
    stg(7);         // glob[7] = 3
    op(VM_OP_RET);  // drops the frame, argument and all
    CHECK(load());
    vm_set_global(0, 99);
    vm_set_global(6, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 0 && vm_global(1) == 5 && vm_global(2) == 7);
    CHECK(vm_global(3) == 42 && vm_global(4) == 11);
    CHECK(vm_global(6) == 0 && vm_global(7) == 3 && vm_global(8) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// vm.md "Load-time validation": pc leaving the blob (a bad jump or CALL, or
// running off the end) warns and halts the context.
static void pc_escaping_the_blob_halts_its_context(void) {
    reset();
    blob_begin(5, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);           //
    op16(VM_OP_JMP, 2000); // far past the end
    store(1, 2);           // never runs
    op(VM_OP_HALT);        //
    handler(2, VM_EV_CREATE);
    store(2, 1);                             //
    op16(VM_OP_JMP, (u32)(-30000) & 0xFFFF); // far before the start
    store(2, 2);                             // never runs
    op(VM_OP_HALT);                          //
    handler(3, VM_EV_CREATE);
    store(3, 1);              //
    emit(VM_OP_CALL);         //
    emit32(0x00FFFFFF);       // CALL past the end
    store(3, 2);              // never runs
    op(VM_OP_HALT);           //
    handler(4, VM_EV_CREATE); // last in the blob, no HALT:
    store(4, 1);              // runs off the end
    CHECK(load());
    expect_faults_halt_only_themselves(4);
    u32 wrong = 0;
    for (u16 g = 1; g <= 4; g++)
        wrong += vm_global(g) != 1;
    CHECK(wrong == 0);
}

// vm.md "Load-time validation" (the interpreter doesn't trust the code): an
// op whose operand is cut off by the end of the blob warns and halts, without
// reading past the end (ASan, on the host).
static void truncated_operand_halts_its_context(void) {
    for (u32 variant = 0; variant < 3; variant++) {
        reset();
        blob_begin(2, 0, GLOBALS);
        witness(0);
        handler(1, VM_EV_CREATE);
        store(1, 1);
        if (variant == 0) {
            emit(VM_OP_PUSH32); // two of its four operand bytes
            emit(0x01);
            emit(0x02);
        } else if (variant == 1) {
            emit(VM_OP_JMP); // one of its two
            emit(0x00);
        } else {
            emit(VM_OP_STG); // none of its one
        }
        CHECK(load());
        expect_faults_halt_only_themselves(1);
        CHECK(vm_global(1) == 1);
    }
}

// vm.md "Entities": an unknown property warns (once, for GETP and SETP
// alike); GETP pushes 0 and SETP drops the value (both pop as usual) and the
// context goes on.
static void unknown_property_warns(void) {
    // The first engine property not assigned, the last one reserved for the
    // engine, the first past the instance fields, the last a u8 can name.
    static const u8 unknown[] = {VM_P_COUNT, VM_P_FIELD0 - 1, VM_P_FIELD(VM_FIELDS), 255};
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    for (u32 k = 0; k < sizeof unknown; k++) {
        ldg(0);           // e
        getp(unknown[k]); // warns (once): 0
        stg(4 + k);       // glob[4 + k] = 0
    }
    ldg(0);           // e
    getp(VM_P_COUNT); // warns: 0
    stg(1);           // glob[1] = 0
    push8(66);        // 66
    ldg(0);           // 66 e
    push8(5);         // 66 e 5
    setp(VM_P_COUNT); // warns, pops two: 66
    stg(2);           // glob[2] = 66
    store(3, 1);      // carried on
    op(VM_OP_HALT);   //
    CHECK(load());
    Entity e = entity_create(C_POS | C_VEL | C_SPR);
    u32 i = entity_index(e);
    vm_attach(e, 0); // attached: instance fields would be readable
    vm_set_global(0, e);
    vm_set_global(1, 99);
    for (u16 k = 0; k < sizeof unknown; k++)
        vm_set_global(4 + k, 99);
    u32 before = debug_warning_count();
    vm_step();
    u32 wrong = 0;
    for (u16 k = 0; k < sizeof unknown; k++)
        wrong += vm_global(4 + k) != 0;
    CHECK(wrong == 0);
    CHECK(vm_global(1) == 0 && vm_global(2) == 66 && vm_global(3) == 1);
    CHECK(vm_idle());
    CHECK(pos_x[i] == 0 && pos_y[i] == 0 && vel_x[i] == 0 && vel_y[i] == 0);
    CHECK(spr_id[i] == 0 && spr_frame[i] == 0 && spr_flags[i] == 0);
    CHECK(spr_angle[i] == 0 && spr_depth[i] == 0 && spr_scale[i] == 0);
    CHECK_WARNED(before, 1);
}

// vm.md "Test plan" (runtime safety): an unknown SYS number warns and halts
// the context.
static void unknown_sys_halts_only_its_context(void) {
    reset();
    blob_begin(3, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);       //
    sys(VM_SYS_COUNT); // the first unassigned number
    store(1, 2);       // never runs
    op(VM_OP_HALT);    //
    handler(2, VM_EV_CREATE);
    store(2, 1);    //
    sys(255);       //
    store(2, 2);    // never runs
    op(VM_OP_HALT); //
    CHECK(load());
    expect_faults_halt_only_themselves(2);
    CHECK(vm_global(1) == 1 && vm_global(2) == 1);
}

// --- Scheduling --------------------------------------------------------------

// vm.md "Waits", "Exact semantics: The resume pass": WAIT n resumes in the
// pass n frames later (vm_events counts nothing), WAIT 0 or less goes on at
// once, and a context waiting again during the pass runs once per frame.
static void wait_counts_frames(void) {
    enum { L_AGAIN };
    reset();
    blob_begin(3, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(3); //
    store(0, 2);    // three frames later
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    label(L_AGAIN);           //
    count(1);                 // once per frame
    wait_frames(1);           //
    jump(VM_OP_JMP, L_AGAIN); //
    handler(2, VM_EV_CREATE);
    wait_frames(0);    // goes on at once
    store(2, 1);       //
    wait_frames(-5);   // so does a negative count
    store(2, 2);       //
    push32(INT32_MIN); //
    op(VM_OP_WAIT);    // and the most negative
    store(2, 3);       //
    op(VM_OP_HALT);    //
    CHECK(load());
    start(0);
    start(1);
    start(2);
    u32 before = debug_warning_count();
    vm_step(); // frame 1
    CHECK(vm_global(0) == 1 && vm_global(1) == 1 && vm_global(2) == 3);
    for (u32 k = 0; k < 3; k++)
        vm_events(); // never resumes contexts
    CHECK(vm_global(0) == 1 && vm_global(1) == 1);
    for (s32 f = 2; f <= 10; f++) {
        vm_step();
        CHECK(vm_global(0) == (f < 4 ? 1 : 2)); // WAIT 3 from frame 1: frame 4
        CHECK(vm_global(1) == f);
        vm_events();
    }
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: The resume pass": WAIT counters are clamped to
// 65535, not truncated to 16 bits. The whole wait runs on the host only.
static void wait_counter_is_clamped(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push32(65536 + 5); // 5 if truncated to 16 bits
    op(VM_OP_WAIT);    // 65535
    store(0, 1);       //
    op(VM_OP_HALT);    //
    CHECK(load());
    start(0);
    frames(10); // frames 1 to 10; the wait began in frame 1
    CHECK(vm_global(0) == 0);
#ifndef SERVAL_GBA
    for (u32 f = 11; f <= 65535; f++)
        frame();
    CHECK(vm_global(0) == 0);
    vm_step(); // frame 1 + 65535
    CHECK(vm_global(0) == 1);
#endif
}

// vm.md "Exact semantics: Starting scripts": vm_start allocates a context at
// once, which first runs in the next vm_step(), never in vm_events().
static void vm_start_first_runs_in_the_next_vm_step(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);
    op(VM_OP_HALT);
    CHECK(load());
    start(0);
    CHECK(!vm_idle());
    vm_events();
    vm_events();
    CHECK(vm_global(0) == 0);
    vm_step();
    CHECK(vm_global(0) == 1);
    CHECK(vm_idle());
}

// vm.h: vm_start returns -1 and warns if there is no such handler (or blob):
// once per kind of problem (no handler, no object, no blob).
static void vm_start_without_a_handler_fails(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_HALT);
    CHECK(load());
    u32 before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_STEP) == -1);   // object 0 has no Step handler
    CHECK(vm_start(1, VM_EV_CREATE) == -1); // no object 1
    CHECK(vm_start(0, VM_EV_COUNT) == -1);  // no such event: no handler either
    CHECK(vm_idle());
    CHECK_WARNED(before, 2);
    vm_unload();
    before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_CREATE) == -1); // no blob
    CHECK_WARNED(before, 1);
}

// The digits of contexts ctx[0..n), in context index order.
static s32 in_pool_order(const int* ctx, const s32* digit, u32 n) {
    s32 value = 0;
    for (int c = 0; c < VM_CONTEXTS; c++)
        for (u32 k = 0; k < n; k++)
            if (ctx[k] == c)
                value = value * 10 + digit[k];
    return value;
}

// vm.md "Exact semantics: The resume pass": one pass in pool index order,
// ready and waiting contexts alike (whatever indices vm_start hands out).
static void contexts_resume_in_pool_order(void) {
    reset();
    blob_begin(4, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_HALT); // frees its context at once
    for (u16 obj = 1; obj <= 2; obj++) {
        handler(obj, VM_EV_CREATE);
        append(0, obj); // frame 1: glob[0]
        wait_frames(1); //
        append(1, obj); // frame 2: glob[1]
        op(VM_OP_HALT); //
    }
    handler(3, VM_EV_CREATE);
    append(1, 3);   // frame 2: glob[1]
    op(VM_OP_HALT); //
    CHECK(load());
    static const s32 digits[] = {1, 2, 3};
    int ctx[3];
    start(0);
    ctx[0] = vm_start(1, VM_EV_CREATE);
    ctx[1] = vm_start(2, VM_EV_CREATE);
    CHECK(ctx[0] >= 0 && ctx[1] >= 0 && ctx[0] != ctx[1]);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == in_pool_order(ctx, digits, 2));
    vm_events();
    ctx[2] = vm_start(3, VM_EV_CREATE); // may reuse object 0's context
    CHECK(ctx[2] >= 0 && ctx[2] != ctx[0] && ctx[2] != ctx[1]);
    vm_step();
    CHECK(vm_global(1) == in_pool_order(ctx, digits, 3));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Behaviours and reactions", "Exact semantics: Step reactions": every
// frame after the first drain, in entity index order, for attached entities
// with a Step handler, also while their behaviour waits (on top of it).
static void step_reactions_run_every_frame(void) {
    enum { L_AGAIN };
    reset();
    blob_begin(4, 0, GLOBALS);
    handler(0, VM_EV_STEP);
    append(0, 1);   //
    op(VM_OP_HALT); //
    handler(1, VM_EV_STEP);
    append(0, 2);   //
    op(VM_OP_HALT); //
    handler(2, VM_EV_CREATE);
    label(L_AGAIN);           // a behaviour waiting most of the time:
    count(2);                 // glob[2]: its rounds
    wait_frames(3);           //
    jump(VM_OP_JMP, L_AGAIN); //
    handler(2, VM_EV_STEP);
    count(1);       // glob[1]: Step runs, every frame
    op(VM_OP_HALT); //
    // Object 3 has no handlers.
    CHECK(load());
    Entity e0 = entity_create(C_POS);
    Entity e1 = entity_create(C_POS);
    Entity e2 = entity_create(C_POS);
    Entity e3 = entity_create(C_POS);
    entity_create(C_POS); // not attached
    vm_attach(e1, 1);     // out of index order
    vm_attach(e0, 0);
    vm_attach(e2, 2);
    vm_attach(e3, 3);
    vm_events(); // drains the Create events: e2's behaviour starts and waits
    CHECK(vm_global(2) == 1);
    u32 before = debug_warning_count();
    static const s32 rounds[] = {1, 1, 2, 2, 2, 3, 3}; // glob[2] after frames 1-7
    for (s32 f = 1; f <= 7; f++) {
        vm_set_global(0, 0);
        vm_step();
        CHECK(vm_global(0) == 12); // e0 then e1, once each
        CHECK(vm_global(1) == f);  // e2's Step, on top of its waiting behaviour
        CHECK(vm_global(2) == rounds[f - 1]);
        vm_events();
    }
    CHECK_WARNED(before, 0);
}

// vm.md "Scheduling", "Exact semantics: Step reactions": an entity attached
// before vm_step() runs its Create in the first drain and its first Step in
// the same vm_step(), on top of the Create if that waits.
static void create_runs_before_the_first_step(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    append(0, 1);   // glob[0]: Create
    op(VM_OP_HALT); //
    handler(0, VM_EV_STEP);
    append(0, 2);   // glob[0]: Step
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    append(1, 1);   // glob[1]: Create...
    wait_frames(1); //
    append(1, 2);   // ...ending next frame
    op(VM_OP_HALT); //
    handler(1, VM_EV_STEP);
    append(1, 3);   // glob[1]: Step
    op(VM_OP_HALT); //
    CHECK(load());
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    vm_attach(a, 0);
    vm_attach(b, 1);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 12); // Create, then Step, in the same vm_step()
    CHECK(vm_global(1) == 13); // Create waits; Step runs on top of it
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 122);
    CHECK(vm_global(1) == 1323); // Create ends in the resume pass, then Step
    vm_events();
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Step reactions": an entity a Step reaction spawns
// runs its Create in vm_step()'s second drain and its first Step in the next
// frame, even from a slot the Step reactions have yet to reach.
static void entity_spawned_by_step_steps_next_frame(void) {
    enum { L_DONE };
    reset();
    blob_begin(2, 0, GLOBALS);
    object(1, C_POS, 0);
    handler(0, VM_EV_STEP);
    ldg(2);                  // spawned already?
    jump(VM_OP_JNZ, L_DONE); //
    store(2, 1);             //
    push8(0);                // x
    push8(0);                // x y
    spawn(1);                // e, in a higher slot than the spawner's
    stg(3);                  // glob[3] = e
    label(L_DONE);           //
    op(VM_OP_HALT);          //
    handler(1, VM_EV_CREATE);
    append(0, 1);   // glob[0]: e's Create
    op(VM_OP_HALT); //
    handler(1, VM_EV_STEP);
    append(0, 2);   // glob[0]: e's Step
    op(VM_OP_HALT); //
    CHECK(load());
    Entity spawner = entity_create(C_POS);
    vm_attach(spawner, 0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 1); // Create in the second drain, no Step
    Entity e = (Entity)vm_global(3);
    CHECK(entity_alive(e) && entity_index(e) > entity_index(spawner));
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 12); // the first Step, next frame
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 122);
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Step reactions": Step never runs before Create.
// Spawned and C-attached entities' Step handlers check that their Create has
// run (it sets their depth to 1). The Create-pending flag clears when the
// Create is drained even if the object has no Create handler: such an entity
// attached between the phases runs Step from the next frame.
static void step_never_runs_before_create(void) {
    reset();
    blob_begin(3, 0, GLOBALS);
    object(1, C_POS | C_SPR, 0);
    handler(0, VM_EV_STEP); // spawns one entity of object 1 a frame
    push8(0);               // x
    push8(0);               // x y
    spawn(1);               // e
    op(VM_OP_DROP);         //
    op(VM_OP_HALT);         //
    handler(1, VM_EV_CREATE);
    op(VM_OP_SELF);   // self
    push8(1);         // self 1
    setp(VM_P_DEPTH); // depth = 1: Create ran
    count(1);         // glob[1]: Creates
    op(VM_OP_HALT);   //
    handler(1, VM_EV_STEP);
    op(VM_OP_SELF);         // self
    getp(VM_P_DEPTH);       // depth
    op(VM_OP_LNOT);         // 1 if Create hasn't run
    ldg(0);                 // early g
    op(VM_OP_ADD);          //
    stg(0);                 // glob[0]: Steps before Create
    count(2);               // glob[2]: Steps
    op(VM_OP_HALT);         //
    handler(2, VM_EV_STEP); // object 2 has no Create handler
    count(3);               // glob[3]: its Steps
    op(VM_OP_HALT);         //
    CHECK(load());
    vm_attach(entity_create(C_POS), 0);
    u32 before = debug_warning_count();
    for (u32 f = 1; f <= 5; f++) {
        vm_step();
        if (f == 1) { // attached between the phases: Create in vm_events()
            vm_attach(entity_create(C_POS | C_SPR), 1);
            vm_attach(entity_create(C_POS), 2);
        }
        vm_events();
    }
    // Spawned in frames 1-5, each Steps from the frame after: 0+1+2+3+4.
    // The two attached after frame 1's vm_step() Step in frames 2-5.
    CHECK(vm_global(0) == 0);
    CHECK(vm_global(1) == 6);
    CHECK(vm_global(2) == 10 + 4);
    CHECK(vm_global(3) == 4);
    CHECK_WARNED(before, 0);
}

// vm.md "Behaviours and reactions", "Exact semantics: Reactions on top of a
// behaviour": reactions (Step, Collision, Animation End) for an entity whose
// behaviour waits run on top of it, on the same context: their activation
// starts at the behaviour's stack top (its zeroed locals don't touch the
// behaviour's cells, and it can't pop them: underflow), and when they end -
// HALT, RET at their own level, or a fault - the behaviour's pc, stack,
// frame, call depth and wait are back exactly as they were. Here it waits
// inside a CALL, with locals and a temporary on its stack, and resumes in the
// frame its WAIT 3 named, then returns and finds everything in place.
static void reactions_run_on_top_of_a_waiting_behaviour(void) {
    enum { L_SUB, L_SUB2 };
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 2);    // l0 l1
    push8(11);      //
    stl(0);         // l0 = 11
    push8(22);      //
    stl(1);         // l1 = 22
    push8(33);      // l0 l1 33: a temporary
    call(L_SUB);    // waits in there; returns 44: l0 l1 33 44
    stg(3);         // glob[3] = 44
    stg(4);         // glob[4] = 33
    ldl(0);         //
    stg(5);         // glob[5] = 11
    ldl(1);         //
    stg(6);         // glob[6] = 22
    store(0, 2);    // glob[0] = 2: the behaviour has ended
    op(VM_OP_HALT); //
    label(L_SUB);   // a frame of its own
    enter(0, 1);    // l0 l1 33 | s0
    push8(44);      //
    stl(0);         // s0 = 44
    store(0, 1);    // glob[0] = 1: waiting
    wait_frames(3); //
    ldl(0);         // 44
    op(VM_OP_RETV); // drops the frame, pushes 44

    handler(0, VM_EV_STEP);
    count(7);       // glob[7]: Step runs, every frame
    op(VM_OP_HALT); //

    handler(0, VM_EV_COLLISION);
    enter(0, 3);     // three zeroed locals, above the behaviour's cells
    ldl(0);          //
    ldl(1);          //
    op(VM_OP_ADD);   //
    ldl(2);          //
    op(VM_OP_ADD);   // 0
    stg(8);          // glob[8] = 0
    push8(-1);       //
    stl(0);          // writes its own local, not the behaviour's
    op(VM_OP_OTHER); //
    stg(9);          // glob[9] = other
    push8(1);        // temporaries left on the stack
    push8(2);        //
    call(L_SUB2);    // a call of its own
    count(10);       // glob[10]: Collisions that ran to their end
    op(VM_OP_RET);   // at its own level: the reaction ends
    label(L_SUB2);   //
    push8(5);        //
    op(VM_OP_RET);   //

    handler(0, VM_EV_ANIM_END);
    count(11);      // glob[11]: it ran
    op(VM_OP_DROP); // nothing in its own activation: underflow, halts it
    store(11, 99);  // never runs
    op(VM_OP_HALT); //
    CHECK(load());
    vm_set_global(8, 99);
    Entity e = entity_create(C_POS);
    Entity o = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // Create runs and waits (3 frames)
    CHECK(vm_global(0) == 1);
    u32 before = debug_warning_count();
    vm_step(); // frame 1: 2 frames left; Step on top
    CHECK(vm_global(7) == 1);
    vm_event(e, o, VM_EV_COLLISION);
    vm_event(e, o, VM_EV_ANIM_END);
    vm_events();
    CHECK(vm_global(8) == 0 && vm_global(9) == o && vm_global(10) == 1);
    CHECK(vm_global(11) == 1);
    CHECK_WARNED(before, 1); // the underflow
    vm_step();               // frame 2: 1 frame left
    CHECK(vm_global(0) == 1 && vm_global(7) == 2);
    vm_event(e, o, VM_EV_COLLISION); // again, while it still waits
    vm_events();
    CHECK(vm_global(10) == 2);
    vm_step(); // frame 3: the behaviour resumes, returns and ends; then Step
    CHECK(vm_global(0) == 2);
    CHECK(vm_global(3) == 44 && vm_global(4) == 33);
    CHECK(vm_global(5) == 11 && vm_global(6) == 22);
    CHECK(vm_global(7) == 3);
    CHECK(vm_global(11) == 1);
    vm_events();
    CHECK(vm_idle());                // Step's own context is freed
    vm_event(e, o, VM_EV_COLLISION); // no behaviour now: a context of its own
    vm_events();
    CHECK(vm_global(10) == 3);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: Frames": a reaction on top of a behaviour has an
// activation of its own, from the behaviour's stack top: ENTER can't take the
// behaviour's cells as arguments, nor SWAP or DROP reach them (stack
// underflow: warns once, halts the reaction), and LDL and STL see no local
// below its frame. The behaviour's cells come through untouched.
static void reactions_cannot_reach_below_their_activation(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(7);       //
    push8(8);       //
    push8(9);       // 7 8 9
    wait_frames(2); //
    stg(2);         // glob[2] = 9
    stg(1);         // glob[1] = 8
    stg(0);         // glob[0] = 7
    op(VM_OP_HALT); //
    handler(0, VM_EV_COLLISION);
    count(3);       // glob[3]: it ran
    enter(1, 0);    // no cell of its own to take: halted
    store(3, 99);   // never runs
    op(VM_OP_HALT); //
    handler(0, VM_EV_ANIM_END);
    count(4);       // glob[4]
    push8(1);       // one cell of its own
    op(VM_OP_SWAP); // needs two: halted
    store(4, 99);   // never runs
    op(VM_OP_HALT); //
    handler(0, VM_EV_STEP);
    count(5);       // glob[5]
    push8(-1);      //
    stl(1);         // outside its frame: dropped (warns)
    ldl(1);         // 0 (no repeat)
    stg(6);         // glob[6] = 0
    op(VM_OP_HALT); //
    CHECK(load());
    vm_set_global(6, 99);
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // the behaviour waits on 7 8 9
    u32 before = debug_warning_count();
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    vm_event(e, ENTITY_NONE, VM_EV_ANIM_END);
    vm_events();
    CHECK(vm_global(3) == 1 && vm_global(4) == 1);
    CHECK_WARNED(before, 1); // underflow
    vm_step();               // Step, on top of the behaviour's last frame of waiting
    CHECK(vm_global(5) == 1 && vm_global(6) == 0);
    CHECK_WARNED(before, 2); // the local
    vm_events();
    vm_step(); // the behaviour resumes and ends; then Step, alone
    CHECK(vm_global(0) == 7 && vm_global(1) == 8 && vm_global(2) == 9);
    CHECK(vm_global(5) == 2);
    CHECK(vm_idle());
    CHECK_WARNED(before, 2);
}

static const PathStep move_steps[] = {{.frames = 3, .speed = FX(1)}};
static const Path move_path = {PATH_STEPS(move_steps)};

// vm.md "Exact semantics: Reactions on top of a behaviour": a reaction leaves
// the behaviour's kind of wait alone too: a WAIT_MOVE resumes when the path
// ends, not before, though a Step and a Collision run on top of it every
// frame.
static void reactions_keep_a_behaviours_wait(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_WAIT_MOVE); //
    store(0, 1);         //
    op(VM_OP_HALT);      //
    handler(0, VM_EV_STEP);
    count(1);       //
    op(VM_OP_HALT); //
    handler(0, VM_EV_COLLISION);
    count(2);       //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS | C_VEL);
    path_start(e, &move_path, 0);
    vm_attach(e, 0);
    u32 before = debug_warning_count();
    for (s32 f = 1; f <= 4; f++) {
        vm_step();
        CHECK(vm_global(0) == (f == 4)); // the 3-frame path ends in frame 3's sys_path
        CHECK(vm_global(1) == f);
        sys_path();
        sys_movement();
        vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
        vm_events();
        CHECK(vm_global(2) == f);
    }
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Behaviours and reactions", "Exact semantics: Budget": a wait that
// would suspend a reaction, or a reaction running past its budget, warns and
// halts the reaction, not the behaviour beneath it (which goes on counting
// frames). A wait that continues at once (WAIT 0, WAIT_MOVE with no path) is
// fine. A reaction with no behaviour beneath it (object 1) is halted the same
// way, and its context freed.
static void reaction_waits_and_overruns_halt_only_the_reaction(void) {
    enum { L_AGAIN, L_LOOP };
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    label(L_AGAIN);           // the behaviour: counts frames forever
    count(0);                 //
    wait_frames(1);           //
    jump(VM_OP_JMP, L_AGAIN); //
    handler(0, VM_EV_STEP);
    count(1);            // glob[1]: Step runs
    wait_frames(0);      // continues at once
    op(VM_OP_WAIT_MOVE); // no path: continues at once
    count(2);            // glob[2]
    wait_frames(2);      // would suspend: warns, halts the reaction
    store(3, 99);        // never runs
    op(VM_OP_HALT);      //
    handler(0, VM_EV_COLLISION);
    label(L_LOOP);           // an endless loop
    count(4);                // 5 ops a round
    jump(VM_OP_JMP, L_LOOP); //
    handler(1, VM_EV_STEP);
    count(5);       // glob[5]
    wait_frames(1); // warns (the same kind), halts
    store(6, 99);   // never runs
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    Entity alone = entity_create(C_POS);
    vm_attach(e, 0);
    vm_attach(alone, 1);
    vm_events(); // e's behaviour counts frame 0
    u32 before = debug_warning_count();
    for (s32 f = 1; f <= 3; f++) {
        vm_step();
        vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
        vm_events();
        CHECK(vm_global(0) == 1 + f); // the behaviour, every frame
        CHECK(vm_global(1) == f && vm_global(2) == f && vm_global(3) == 0);
        CHECK(vm_global(4) == 51 * f); // 51 rounds and an LDG, then halted
        CHECK(vm_global(5) == f && vm_global(6) == 0);
    }
    CHECK_WARNED(before, 2); // a wait in a reaction; a reaction over budget
    vm_detach(e);
    CHECK(vm_idle()); // object 1's reactions left nothing behind
}

// vm.md "Exact semantics: Draining": FIFO, from vm_events() and from
// vm_step()'s drain alike.
static void events_drain_in_fifo_order(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_COLLISION);
    ldg(0);         // g
    push8(10);      // g 10
    op(VM_OP_MUL);  // g*10
    op(VM_OP_SELF); // g*10 self
    getp(VM_P_X);   // g*10 x
    op(VM_OP_ADD);  // g*10+x
    stg(0);         // glob[0] = glob[0] * 10 + this entity's pos_x
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e[3];
    for (u32 k = 0; k < 3; k++) {
        e[k] = entity_create(C_POS);
        pos_x[entity_index(e[k])] = (FIXED)(k + 1);
        vm_attach(e[k], 0);
    }
    vm_events();
    vm_event(e[2], ENTITY_NONE, VM_EV_COLLISION);
    vm_event(e[0], ENTITY_NONE, VM_EV_COLLISION);
    vm_event(e[1], ENTITY_NONE, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_global(0) == 312);
    vm_set_global(0, 0);
    vm_event(e[1], ENTITY_NONE, VM_EV_COLLISION);
    vm_event(e[0], ENTITY_NONE, VM_EV_COLLISION);
    vm_step();
    CHECK(vm_global(0) == 21);
}

// vm.md "Scheduling", "Exact semantics: Draining": events queued during a
// drain (SPAWN's Create, KILL's Destroy) are drained in the same phase.
static void events_queued_while_draining_run_in_the_same_phase(void) {
    reset();
    blob_begin(4, 0, GLOBALS);
    object(1, C_POS, 0);
    handler(0, VM_EV_CREATE);
    push8(0);       // x
    push8(0);       // x y
    spawn(1);       // e
    op(VM_OP_DROP); //
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    count(3);       // glob[3]: object 1 Creates
    op(VM_OP_HALT); //
    handler(2, VM_EV_COLLISION);
    op(VM_OP_OTHER); // other
    op(VM_OP_KILL);  // queues its Destroy
    op(VM_OP_HALT);  //
    handler(3, VM_EV_DESTROY);
    store(4, 1);    //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity spawner = entity_create(C_POS);
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    u32 before = debug_warning_count();
    vm_attach(spawner, 0);
    vm_events(); // spawner's Create spawns; the spawn's Create runs in this drain
    CHECK(vm_global(3) == 1);
    vm_attach(a, 2);
    vm_attach(b, 3);
    vm_events();
    vm_event(a, b, VM_EV_COLLISION);
    vm_events(); // a's Collision kills b; b's Destroy runs in this drain
    CHECK(vm_global(4) == 1);
    CHECK(!entity_alive(b) && entity_alive(a));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Entities" (KILL), "Exact semantics: Draining": Destroy (queued by
// KILL) runs the Destroy reaction on top of the entity's waiting behaviour,
// then halts the behaviour, then destroys the entity. The reaction sees the
// entity alive, and its activation starts above the behaviour's cells.
static void kill_runs_destroy_then_halts_the_behaviour(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 3);    // three cells on the behaviour's stack
    store(0, 1);    //
    wait_frames(5); //
    store(0, 2);    // never runs: killed while waiting
    op(VM_OP_HALT); //
    handler(0, VM_EV_DESTROY);
    op(VM_OP_SELF);         // self
    getp(VM_P_X);           // still alive: its x
    stg(2);                 // glob[2] = 6.0
    enter(0, VM_STACK - 3); // exactly what is left above the behaviour's cells
    store(1, 1);            // one more cell: overflows (warns), so never runs
    op(VM_OP_HALT);         //
    handler(1, VM_EV_CREATE);
    ldg(5);         // e
    op(VM_OP_KILL); // queues e's Destroy
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    pos_x[entity_index(e)] = FX(6);
    vm_attach(e, 0);
    vm_events(); // Create runs and waits
    vm_set_global(5, e);
    u32 before = debug_warning_count();
    start(1);
    vm_step(); // the thread queues Destroy; the drain runs it, then halts e's wait
    CHECK(vm_global(2) == FX(6));
    CHECK(vm_global(1) == 0);
    CHECK_WARNED(before, 1); // the overflow
    CHECK(!entity_alive(e));
    CHECK(vm_idle());
    vm_events();
    frames(6);
    CHECK(vm_global(0) == 1);
    CHECK_WARNED(before, 1);
}

// vm.md "Entities" (KILL), "Exact semantics: Draining": KILL queues Destroy
// (the killing script goes on); the Destroy handler runs while the entity is
// still alive, then the entity is destroyed - in the same phase.
static void kill_runs_destroy_before_destroying(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_SELF); // e
    op(VM_OP_KILL); // queues Destroy
    store(0, 1);    // this handler goes on
    op(VM_OP_HALT); //
    handler(0, VM_EV_DESTROY);
    op(VM_OP_SELF); // e
    getp(VM_P_X);   // still alive: its x
    stg(1);         // glob[1] = 3.0
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    pos_x[entity_index(e)] = FX(3);
    u32 before = debug_warning_count();
    vm_attach(e, 0);
    vm_step();
    CHECK(vm_global(0) == 1 && vm_global(1) == FX(3));
    CHECK(!entity_alive(e));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Draining": KILL of an unattached live entity just
// destroys it; a second Destroy for it, now dead, is skipped silently.
static void kill_destroys_an_unattached_entity(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    ldg(5);         // e
    op(VM_OP_DUP);  // e e
    op(VM_OP_KILL); // e
    op(VM_OP_KILL); //
    store(0, 1);    //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    Entity other = entity_create(C_POS);
    vm_set_global(5, e);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    CHECK(vm_global(0) == 1);
    CHECK(!entity_alive(e) && entity_alive(other));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Behaviours and reactions": a wait inside a Destroy reaction warns and
// halts it; the entity is destroyed all the same.
static void wait_inside_destroy_warns_and_halts(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_DESTROY);
    store(1, 1);    //
    wait_frames(2); // warns; Destroy stops here
    store(1, 2);    // never runs
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    ldg(5);         // e
    op(VM_OP_KILL); //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    vm_set_global(5, e);
    start(1);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(1) == 1);
    CHECK(!entity_alive(e));
    CHECK(vm_idle());
    frames(3);
    CHECK(vm_global(1) == 1);
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: Destroy details", "Entity cells": KILL of
// ENTITY_NONE, or of a cell no handle has (never truncated into one), warns
// and the script goes on; KILL of an entity that is already dead (destroyed,
// or a stale handle to a reused slot) is skipped silently.
static void kill_of_no_entity_warns_but_of_a_dead_one_is_silent(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    ldg(5);         // destroyed
    op(VM_OP_KILL); // silently skipped
    ldg(6);         // stale: live's slot, another generation
    op(VM_OP_KILL); // silently skipped
    store(0, 1);    // carried on
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    push8(0);       // ENTITY_NONE
    op(VM_OP_KILL); // warns
    ldg(7);         // 0x10000 | live: no handle, not live once truncated
    op(VM_OP_KILL); // the same problem: no second warning
    store(1, 1);    // carried on
    op(VM_OP_HALT); //
    CHECK(load());
    Entity live = entity_create(C_POS);
    Entity gone = entity_create(C_POS);
    entity_destroy(gone);
    u32 generation = entity_generation(live) == 255 ? 1u : entity_generation(live) + 1u;
    Entity stale = (Entity)(generation << 8 | entity_index(live));
    CHECK(!entity_alive(stale));
    vm_set_global(5, gone);
    vm_set_global(6, stale);
    vm_set_global(7, (s32)(0x10000u | live));
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    CHECK(vm_global(0) == 1);
    CHECK(entity_alive(live));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    start(1);
    vm_step();
    CHECK(vm_global(1) == 1);
    CHECK(entity_alive(live));
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: Destroy details": vm_event(e, x, VM_EV_DESTROY)
// behaves exactly like KILL: queued until a phase drains it, then the Destroy
// logic. A Destroy handler sees OTHER = 0, whatever the event or the killing
// handler had. vm_event of Destroy for ENTITY_NONE warns, as KILL does.
static void destroy_event_behaves_like_kill(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_DESTROY);
    op(VM_OP_OTHER); // 0
    ldg(0);          //
    op(VM_OP_ADD);   //
    stg(0);          // glob[0] += OTHER
    count(1);        // glob[1]: Destroys run
    op(VM_OP_HALT);  //
    handler(1, VM_EV_COLLISION);
    op(VM_OP_OTHER); // its other entity
    op(VM_OP_KILL);  // queues its Destroy
    op(VM_OP_HALT);  //
    CHECK(load());
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    Entity killer = entity_create(C_POS);
    vm_attach(a, 0);
    vm_attach(b, 0);
    vm_attach(killer, 1);
    vm_events();
    u32 before = debug_warning_count();
    vm_event(a, killer, VM_EV_DESTROY);   // from C, with an other
    CHECK(entity_alive(a) && !vm_idle()); // queued, not run
    vm_events();
    CHECK(!entity_alive(a));
    CHECK(vm_global(1) == 1 && vm_global(0) == 0);
    vm_event(killer, b, VM_EV_COLLISION); // its handler KILLs b
    vm_events();
    CHECK(!entity_alive(b) && entity_alive(killer));
    CHECK(vm_global(1) == 2 && vm_global(0) == 0);
    CHECK_WARNED(before, 0);
    vm_event(ENTITY_NONE, killer, VM_EV_DESTROY);
    CHECK(vm_idle()); // nothing queued
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: Destroy details": vm_event with an event number of
// VM_EV_COUNT or more warns and queues nothing.
static void vm_event_rejects_unknown_events(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_COLLISION);
    count(0);
    op(VM_OP_HALT);
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    u32 before = debug_warning_count();
    vm_event(e, ENTITY_NONE, VM_EV_COUNT);
    vm_event(e, ENTITY_NONE, 255);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
    vm_events();
    CHECK(vm_global(0) == 0);
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION); // e's handler is fine
    vm_events();
    CHECK(vm_global(0) == 1);
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: vm_kill": from C, outside the phases, the Destroy
// logic runs at once: the Destroy reaction (if any, on top of the waiting
// behaviour), the behaviour halted, entity_destroy; an unattached entity is
// just destroyed.
static void vm_kill_runs_destroy_at_once(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(5); //
    store(0, 2);    // never runs
    op(VM_OP_HALT); //
    handler(0, VM_EV_DESTROY);
    op(VM_OP_SELF); // e
    getp(VM_P_X);   // its x
    stg(1);         // glob[1] = 4.0
    op(VM_OP_HALT); //
    // Object 1 has no handlers.
    CHECK(load());
    Entity e0 = entity_create(C_POS);
    Entity e1 = entity_create(C_POS);
    Entity e2 = entity_create(C_POS);
    pos_x[entity_index(e0)] = FX(4);
    vm_attach(e0, 0);
    vm_attach(e1, 1);
    vm_events();
    CHECK(vm_global(0) == 1);
    u32 before = debug_warning_count();
    vm_kill(e0);
    CHECK(vm_global(1) == FX(4));
    CHECK(!entity_alive(e0));
    vm_kill(e1); // attached, no Destroy handler
    CHECK(!entity_alive(e1));
    vm_kill(e2); // never attached
    CHECK(!entity_alive(e2));
    CHECK(vm_idle());
    frames(6);
    CHECK(vm_global(0) == 1);
    CHECK_WARNED(before, 0);
    // vm.md "Destroy details": like KILL, ENTITY_NONE is misuse and warns
    // (once per loaded blob); an entity that is already dead is silent.
    vm_kill(ENTITY_NONE);
    vm_kill(ENTITY_NONE);
    CHECK_WARNED(before, 1);
    vm_kill(e0);
    CHECK_WARNED(before, 1);
}

// vm.h: vm_detach halts the entity's script and unbinds it, without Destroy;
// the entity lives on, without its Step handler.
static void detach_stops_the_script_without_destroy(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(2); //
    store(0, 2);    // never runs
    op(VM_OP_HALT); //
    handler(0, VM_EV_STEP);
    count(2);       //
    op(VM_OP_HALT); //
    handler(0, VM_EV_DESTROY);
    store(1, 1);    //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // Create runs and waits
    CHECK(vm_global(0) == 1);
    u32 before = debug_warning_count();
    vm_detach(e);
    CHECK(vm_idle());
    frames(4);
    CHECK(vm_global(0) == 1); // never resumed
    CHECK(vm_global(1) == 0); // no Destroy
    CHECK(vm_global(2) == 0); // no Step
    CHECK(entity_alive(e));
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Starting scripts": attaching an attached entity
// rebinds it, halting its live context first.
static void attach_rebinds_an_attached_entity(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(3); //
    store(0, 2);    // never runs: halted by the rebind
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    store(1, 1);    //
    op(VM_OP_HALT); //
    handler(1, VM_EV_STEP);
    count(2);       //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    CHECK(vm_global(0) == 1);
    u32 before = debug_warning_count();
    vm_attach(e, 1);
    vm_events(); // object 1's Create (not dropped: the old context was halted)
    CHECK(vm_global(1) == 1);
    frames(4);
    CHECK(vm_global(0) == 1);
    CHECK(vm_global(2) == 4); // object 1's Step, every frame
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Starting scripts": attaching twice before a drain
// queues one Create, whether the entity is attached to the same object again,
// rebound to another (then the other's Create runs), or detached and attached
// again in between.
static void double_attach_queues_one_create(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    count(0);       // glob[0]: object 0's Creates
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    count(1);       // glob[1]: object 1's Creates
    op(VM_OP_HALT); //
    CHECK(load());
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    Entity c = entity_create(C_POS);
    u32 before = debug_warning_count();
    vm_attach(a, 0);
    vm_attach(a, 0); // the same object again
    vm_attach(b, 0);
    vm_attach(b, 1); // rebound before its Create ran
    vm_attach(c, 1);
    vm_detach(c);
    vm_attach(c, 1); // detached and attached again
    vm_events();
    CHECK(vm_global(0) == 1); // a's
    CHECK(vm_global(1) == 2); // b's (object 1's) and c's
    vm_attach(a, 0);          // after the drain: a new Create
    vm_events();
    CHECK(vm_global(0) == 2);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Behaviours and reactions", "Exact semantics: Draining": an instance
// has at most one behaviour. A Room Start event for an instance whose Create
// waits halts it and starts the Room Start behaviour; one its object has no
// handler for leaves the behaviour alone.
static void a_behaviour_event_replaces_the_live_behaviour(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(2); //
    store(0, 2);    // never runs: replaced
    op(VM_OP_HALT); //
    handler(0, VM_EV_ROOM_START);
    store(1, 1);    //
    wait_frames(2); //
    store(1, 2);    // runs
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    store(2, 1);    //
    wait_frames(2); //
    store(2, 2);    // runs: object 1 has no Room Start handler
    op(VM_OP_HALT); //
    CHECK(load());
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    vm_attach(a, 0);
    vm_attach(b, 1);
    vm_events(); // both Creates run and wait
    u32 before = debug_warning_count();
    vm_event(a, ENTITY_NONE, VM_EV_ROOM_START);
    vm_event(b, ENTITY_NONE, VM_EV_ROOM_START);
    vm_events();
    CHECK(vm_global(1) == 1);
    frames(3);
    CHECK(vm_global(0) == 1 && vm_global(1) == 2 && vm_global(2) == 2);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Starting scripts": vm_attach with no blob, a dead
// entity or an object out of range warns and is ignored.
static void attach_misuse_is_ignored(void) {
    reset();
    Entity e = entity_create(C_POS);
    u32 before = debug_warning_count();
    vm_attach(e, 0); // no blob
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);
    op(VM_OP_HALT);
    CHECK(load());
    Entity dead = entity_create(C_POS);
    entity_destroy(dead);
    before = debug_warning_count();
    vm_attach(dead, 0);
    vm_attach(e, 1);  // no object 1
    CHECK(vm_idle()); // nothing queued
    vm_events();
    CHECK(vm_global(0) == 0);
    CHECK_WARNED(before, 2);
}

// vm.md "Exact semantics: Spawning": SPAWN pops y then x, creates the entity
// with the object's mask, sets its position (and sprite, with C_SPR),
// attaches it and pushes it; its Create is queued, running in this phase's
// drain.
static void spawn_creates_an_attached_entity(void) {
    reset();
    blob_begin(3, 0, GLOBALS);
    object(1, C_POS | C_SPR | C_GAME(3), 7);
    object(2, C_POS, 9);
    handler(0, VM_EV_CREATE);
    push32(FX(10));  // x
    push32(-FX(20)); // x y
    spawn(1);        // a
    stg(0);          // glob[0] = a
    ldg(3);          // a's Create hasn't run yet: 0
    stg(4);          // glob[4] = 0
    push8(1);        // x
    push8(2);        // x y
    spawn(2);        // b
    stg(1);          // glob[1] = b
    op(VM_OP_HALT);  //
    handler(1, VM_EV_CREATE);
    op(VM_OP_SELF); // a
    getp(VM_P_X);   // its x, set before Create runs
    stg(2);         // glob[2] = 10.0
    store(3, 1);    //
    op(VM_OP_HALT); //
    handler(1, VM_EV_COLLISION);
    store(5, 1);    //
    op(VM_OP_HALT); //
    CHECK(load());
    vm_set_global(4, 99);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    Entity a = (Entity)vm_global(0);
    Entity b = (Entity)vm_global(1);
    CHECK(a != ENTITY_NONE && entity_alive(a));
    CHECK(b != ENTITY_NONE && entity_alive(b) && b != a);
    u32 i = entity_index(a), j = entity_index(b);
    CHECK(ent_mask[i] == (C_POS | C_SPR | C_GAME(3) | C_ALIVE));
    CHECK(pos_x[i] == FX(10) && pos_y[i] == -FX(20));
    CHECK(spr_id[i] == 7);
    CHECK(ent_mask[j] == (C_POS | C_ALIVE));
    CHECK(pos_x[j] == 1 && pos_y[j] == 2);
    CHECK(spr_id[j] == 0); // no C_SPR: the sprite isn't set
    CHECK(vm_global(4) == 0);
    CHECK(vm_global(3) == 1 && vm_global(2) == FX(10));
    CHECK(vm_idle());
    vm_event(a, ENTITY_NONE, VM_EV_COLLISION); // a is attached to object 1
    vm_events();
    CHECK(vm_global(5) == 1);
    CHECK_WARNED(before, 0);
}

// vm.md "Exact semantics: Spawning": an object out of range, or a full pool,
// warns and pushes 0; the script goes on.
static void spawn_failures_push_zero(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(0);       // x
    push8(0);       // x y
    spawn(2);       // no object 2: warns, 0
    stg(0);         // glob[0] = 0
    store(1, 1);    // carried on
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    push8(0);       // x
    push8(0);       // x y
    spawn(0);       // the pool is full: warns, 0
    stg(2);         // glob[2] = 0
    store(3, 1);    // carried on
    op(VM_OP_HALT); //
    CHECK(load());
    vm_set_global(0, 99);
    vm_set_global(2, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 0 && vm_global(1) == 1);
    CHECK(ecs_count(0) == 0);
    CHECK_WARNED(before, 1);
    for (u32 k = 0; k < MAX_ENT; k++)
        entity_create(0);
    CHECK(ecs_free_count() == 0);
    start(1);
    before = debug_warning_count();
    vm_step();
    CHECK(vm_global(2) == 0 && vm_global(3) == 1);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() > before); // the VM's (entity_create may add its own)
#endif
}

// vm.md "Scheduling": a full event queue drops the event with a warning.
static void full_event_queue_drops_with_a_warning(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_COLLISION);
    count(0);
    op(VM_OP_HALT);
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // the queue is empty
    u32 before = debug_warning_count();
    for (u32 k = 0; k < VM_EVENT_QUEUE + 1; k++)
        vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    CHECK_WARNED(before, 1);
    vm_events(); // each handler ends before the next event: all run
    CHECK(vm_global(0) == VM_EVENT_QUEUE);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// vm.h VM_EVENT_QUEUE: a room's whole load fits the queue. Every entity the
// pool holds attached (a Create each) and given a Room Start, with no drain
// in between, warns nothing; the next vm_step runs them in order, every
// Create before the first Room Start, so each Create sees the whole room
// (here: the room's last instance, after the one in glob[5]), and Room Start
// sees every Create done.
static void a_full_room_fits_the_queue(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);     // glob[0] += 1; glob[3] += 1 if the last instance is there
    count(0);                     //
    ldg(5);                       // the second-to-last instance
    nexti(0);                     // the next one: the last, once it is attached
    ldg(6);                       // next, last
    op(VM_OP_EQ);                 // 0 or 1
    ldg(3);                       //
    op(VM_OP_ADD);                //
    stg(3);                       //
    op(VM_OP_HALT);               //
    handler(0, VM_EV_ROOM_START); // glob[4] += 1 if every Create ran before it
    ldg(0);                       // creates
    push16(MAX_ENT);              // creates 128
    op(VM_OP_EQ);                 // 0 or 1
    ldg(4);                       //
    op(VM_OP_ADD);                //
    stg(4);                       //
    op(VM_OP_HALT);               //
    CHECK(load());
    _Static_assert(2 * MAX_ENT <= VM_EVENT_QUEUE, "the room fits");
    Entity room[MAX_ENT];
    for (u32 k = 0; k < MAX_ENT; k++)
        room[k] = entity_create(C_POS);
    vm_set_global(5, room[MAX_ENT - 2]);
    vm_set_global(6, room[MAX_ENT - 1]);
    u32 before = debug_warning_count();
    for (u32 k = 0; k < MAX_ENT; k++)
        vm_attach(room[k], 0);
    for (u32 k = 0; k < MAX_ENT; k++)
        vm_event(room[k], ENTITY_NONE, VM_EV_ROOM_START);
    CHECK(!vm_idle());
    vm_step();
    CHECK(vm_global(0) == MAX_ENT && vm_global(3) == MAX_ENT && vm_global(4) == MAX_ENT);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.h, vm.md "Exact semantics: Draining": with every context in use,
// vm_start returns -1 and a drained event is dropped: one kind of problem (no
// free context), so one warning.
static void context_pool_exhaustion_warns(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    wait_frames(2);
    op(VM_OP_HALT);
    handler(1, VM_EV_CREATE);
    store(1, 1);
    op(VM_OP_HALT);
    CHECK(load());
    for (u32 k = 0; k < VM_CONTEXTS; k++)
        start(0);
    u32 before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_CREATE) == -1);
    CHECK_WARNED(before, 1);
    Entity e = entity_create(C_POS);
    vm_attach(e, 1);
    vm_events(); // no context for its Create: dropped
    CHECK(vm_global(1) == 0);
    CHECK_WARNED(before, 1);
    frames(3); // the 32 threads end
    CHECK(vm_idle());
    start(1); // room again
    vm_step();
    CHECK(vm_global(1) == 1);
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: Budget": a context's op that would exceed
// VM_OPS_PER_SLICE in a phase isn't run; the context waits 1 frame there and
// warns (once per loaded blob, not every frame). Other contexts still run, and
// the endless loop gets exactly one slice per frame.
static void budget_throttles_an_endless_loop(void) {
    enum { L_LOOP };
    reset();
    blob_begin(2, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    label(L_LOOP);           //
    count(0);                // LDG, PUSH8, ADD, STG
    jump(VM_OP_JMP, L_LOOP); // 5 ops a round, forever
    CHECK(load());
    start(1);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    // Ops 1-256 are 51 rounds and the 52nd round's LDG; its PUSH8 isn't run.
    CHECK(vm_global(0) == 51);
    CHECK(vm_global(W) == 1);
    CHECK(vm_ops_this_frame() == VM_OPS_PER_SLICE + 4); // + the witness's 4
    CHECK_WARNED(before, 1);
    vm_events();
    CHECK(vm_ops_this_frame() == VM_OPS_PER_SLICE + 4); // vm_events resumes nothing
    vm_step();
    // From that PUSH8: the 52nd round ends, then 50 more rounds.
    CHECK(vm_global(0) == 102);
    CHECK(vm_global(W) == 2);
    CHECK(vm_ops_this_frame() == VM_OPS_PER_SLICE + 3); // + the witness's last 3
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 153);
    CHECK(vm_ops_this_frame() == VM_OPS_PER_SLICE);
    CHECK(!vm_idle());
    CHECK_WARNED(before, 1); // not again in later frames
}

// An endless loop: glob[g] += 1 forever (5 ops a round).
static void endless(u32 g, u32 l) {
    label(l);
    count(g);
    jump(VM_OP_JMP, l);
}

// vm.md "Exact semantics: Warnings repeat once per problem, per loaded blob":
// any number of contexts over budget, in any number of frames, warn once;
// after a reload, the first one warns again.
static void budget_warns_once_per_loaded_blob(void) {
    enum { L_LOOP_1, L_LOOP_2 };
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    endless(0, L_LOOP_1);
    handler(1, VM_EV_CREATE);
    endless(1, L_LOOP_2);
    CHECK(load());
    start(0);
    start(1);
    u32 before = debug_warning_count();
    frames(3);
    CHECK(vm_global(0) > 0 && vm_global(1) > 0); // both run, throttled
    CHECK_WARNED(before, 1);
    CHECK(reload()); // the same blob: halts both
    start(1);
    frames(2);
    CHECK_WARNED(before, 2);
}

// vm.md "Exact semantics: Budget": inside a Destroy reaction, which cannot
// wait, the op over the budget halts the reaction instead (a loop that would
// end after 60 rounds stops in round 32); the entity is destroyed all the
// same.
static void budget_halts_a_destroy_handler(void) {
    enum { L_LOOP };
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_DESTROY);
    count(1);                // 4 ops
    label(L_LOOP);           //
    count(0);                // round n: glob[0] = n
    ldg(0);                  //
    push8(60);               //
    op(VM_OP_LT);            //
    jump(VM_OP_JNZ, L_LOOP); // 8 ops a round: round 32's STG is op 256
    store(2, 1);             // never runs
    op(VM_OP_HALT);          //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    u32 before = debug_warning_count();
    vm_kill(e);
    CHECK(!entity_alive(e));
    CHECK(vm_global(1) == 1 && vm_global(0) == 32 && vm_global(2) == 0);
    CHECK(vm_idle()); // halted, not waiting
    frames(2);
    CHECK(vm_global(0) == 32);
    CHECK_WARNED(before, 1);
}

// vm.md "Exact semantics: Budget": vm_ops_this_frame() counts the ops run in
// both phases since the latest vm_step() began.
static void ops_this_frame_counts_both_phases(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    // 2 ops
    op(VM_OP_HALT); // 3
    handler(1, VM_EV_COLLISION);
    store(1, 1);    // 2 ops
    op(VM_OP_NOP);  // 3
    op(VM_OP_HALT); // 4
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 1);
    vm_events();
    start(0);
    vm_step();
    CHECK(vm_ops_this_frame() == 3);
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_ops_this_frame() == 7);
    vm_step(); // nothing to run
    CHECK(vm_ops_this_frame() == 0);
    vm_events();
    CHECK(vm_ops_this_frame() == 0);
}

// vm.md "Exact semantics: Stale bindings": an entity destroyed behind the
// VM's back counts as unbound, with one warning, even when a new entity has
// taken its slot.
static void stale_binding_counts_as_unbound(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_STEP);
    count(0);
    op(VM_OP_HALT);
    CHECK(load());
    Entity e = entity_create(C_POS);
    u32 i = entity_index(e);
    vm_attach(e, 0);
    vm_events();
    frame();
    CHECK(vm_global(0) == 1);
    entity_destroy(e); // not vm_kill
    Entity reused;     // the ECS reuses the slot last: fill all the others
    do {
        reused = entity_create(C_POS);
    } while (reused != ENTITY_NONE && entity_index(reused) != i);
    CHECK(reused != ENTITY_NONE && entity_index(reused) == i && reused != e);
    u32 before = debug_warning_count();
    frames(3);
    CHECK(vm_global(0) == 1); // no Step for e, nor for the entity in its slot
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// --- Entity bridge -----------------------------------------------------------

// vm.md "Entities": SELF is the bound entity, OTHER the event's other (else
// 0); in a detached thread OTHER is 0 and SELF warns and pushes 0.
static void self_and_other(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_OTHER); // no other entity: 0
    stg(2);          //
    op(VM_OP_HALT);  //
    handler(0, VM_EV_COLLISION);
    op(VM_OP_SELF);  // e
    stg(0);          //
    op(VM_OP_OTHER); // b
    stg(1);          //
    op(VM_OP_HALT);  //
    handler(1, VM_EV_CREATE);
    op(VM_OP_OTHER); // 0, silently
    stg(3);          //
    op(VM_OP_SELF);  // warns: 0
    stg(4);          //
    store(5, 1);     // carried on
    op(VM_OP_HALT);  //
    CHECK(load());
    for (u16 g = 2; g <= 4; g++)
        vm_set_global(g, 99);
    Entity e = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    u32 before = debug_warning_count();
    vm_attach(e, 0);
    vm_events();
    CHECK(vm_global(2) == 0);
    vm_event(e, b, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_global(0) == e && vm_global(1) == b);
    CHECK_WARNED(before, 0);
    start(1);
    vm_step();
    CHECK(vm_global(3) == 0 && vm_global(4) == 0 && vm_global(5) == 1);
    CHECK_WARNED(before, 1);
}

// vm.md "Entities": GETP and SETP reach the ECS arrays of the same names.
// Writes truncate to the array's type; reads extend it back to a cell (sign-
// extending s8 and s16, zero-extending u8 and u16). VM_P_TAGS is C_GAME(0) to
// C_GAME(14) of ent_mask as bits 0 to 14; SETP changes only those bits.
// VM_P_BODY_CONTACT and VM_P_OBJECT are read-only: not written here
// (read_only_properties, objects_of_entities).
static const struct {
    s32 written;
    s32 read; // what GETP then gives
    const char* what;
    bool read_only;
} prop_rows[VM_P_COUNT] = {
    [VM_P_X] = {-FX(12) - 5, -FX(12) - 5, "VM_P_X"},
    [VM_P_Y] = {INT32_MAX, INT32_MAX, "VM_P_Y"},
    [VM_P_VX] = {-FX(1) / 2, -FX(1) / 2, "VM_P_VX"},
    [VM_P_VY] = {FX(3), FX(3), "VM_P_VY"},
    [VM_P_SPR] = {0x12345, 0x2345, "VM_P_SPR truncates to u16"},
    [VM_P_FRAME] = {0x1FF, 0xFF, "VM_P_FRAME truncates to u8, reads unsigned"},
    [VM_P_FLAGS] = {-1, 0xFFFF, "VM_P_FLAGS truncates to u16, reads unsigned"},
    [VM_P_ANGLE] = {0x18000, 0x8000, "VM_P_ANGLE truncates to u16, reads unsigned"},
    [VM_P_DEPTH] = {0x18000, -32768, "VM_P_DEPTH truncates to s16, reads sign-extended"},
    [VM_P_SCALE] = {0xFFFF, -1, "VM_P_SCALE truncates to s16, reads sign-extended"},
    [VM_P_BODY_W] = {0x1FE, 0xFE, "VM_P_BODY_W truncates to u8, reads unsigned"},
    [VM_P_BODY_H] = {-1, 0xFF, "VM_P_BODY_H truncates to u8, reads unsigned"},
    [VM_P_TAGS] = {-1, 0x7FFF, "VM_P_TAGS keeps bits 0 to 14"},
    [VM_P_ANIM_TIME] = {0x1FF, 0xFF, "VM_P_ANIM_TIME truncates to u8, reads unsigned"},
    [VM_P_ANIM_STEP] = {-2, 0xFE, "VM_P_ANIM_STEP truncates to u8, reads unsigned"},
    [VM_P_BODY_BOUNCE] = {0x1FF, 0xFF, "VM_P_BODY_BOUNCE truncates to u8, reads unsigned"},
    [VM_P_BODY_FRICTION] = {-1, 0xFF, "VM_P_BODY_FRICTION truncates to u8, reads unsigned"},
    [VM_P_BODY_MAX_FALL] = {-FX(1), 0xFF00, "VM_P_BODY_MAX_FALL truncates to u16, reads unsigned"},
    [VM_P_BODY_GRAVITY] = {0x180, -128, "VM_P_BODY_GRAVITY truncates to s8, reads sign-extended"},
    [VM_P_BODY_CONTACT] = {0, 0, "VM_P_BODY_CONTACT reads body_contact", true},
    [VM_P_OBJECT] = {0, -1, "VM_P_OBJECT reads -1 for an entity that isn't attached", true},
};

// Where properties_read_and_write_the_ecs stores what it reads: GETP of
// property p goes to glob[READ_BY_SCRIPT + p], then glob[READ_FROM_C + p]
// (LDG and STG take any global below VM_GLOBALS, past the declared count).
enum { READ_BY_SCRIPT = GLOBALS, READ_FROM_C = READ_BY_SCRIPT + VM_P_COUNT };
_Static_assert(READ_FROM_C + VM_P_COUNT <= VM_GLOBALS, "the property rows fit the globals");

static void properties_read_and_write_the_ecs(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    for (u32 p = 0; p < VM_P_COUNT; p++) {
        if (prop_rows[p].read_only)
            continue;
        ldg(0);                       // e
        push32(prop_rows[p].written); // e v
        setp(p);                      //
    }
    for (u32 p = 0; p < VM_P_COUNT; p++) {
        ldg(0);                  // e
        getp(p);                 // value
        stg(READ_BY_SCRIPT + p); // glob[READ_BY_SCRIPT + p]
    }
    op(VM_OP_HALT);
    handler(1, VM_EV_CREATE); // reads what C set
    for (u32 p = 0; p < VM_P_COUNT; p++) {
        ldg(0);               // e
        getp(p);              // value
        stg(READ_FROM_C + p); // glob[READ_FROM_C + p]
    }
    op(VM_OP_HALT);
    CHECK(load());
    u32 components = C_POS | C_VEL | C_SPR | C_BODY | C_ANIM;
    Entity e = entity_create(components);
    u32 i = entity_index(e);
    vm_set_global(0, e);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    CHECK(pos_x[i] == -FX(12) - 5 && pos_y[i] == INT32_MAX);
    CHECK(vel_x[i] == -FX(1) / 2 && vel_y[i] == FX(3));
    CHECK(spr_id[i] == 0x2345 && spr_frame[i] == 0xFF && spr_flags[i] == 0xFFFF);
    CHECK(spr_angle[i] == 0x8000 && spr_depth[i] == -32768 && spr_scale[i] == -1);
    CHECK(body_w[i] == 0xFE && body_h[i] == 0xFF);
    CHECK(spr_anim_time[i] == 0xFF && spr_anim_step[i] == 0xFE);
    CHECK(body_bounce[i] == 0xFF && body_friction[i] == 0xFF && body_max_fall[i] == 0xFF00);
    CHECK(body_gravity[i] == -128 && body_contact[i] == 0);
    // Every game component set; the engine's and C_ALIVE as they were.
    CHECK(ent_mask[i] == (components | C_ALIVE | 0x7FFFu << 16));
    CHECK(ent_has(i, C_GAME(0) | C_GAME(14)));
    for (u32 p = 0; p < VM_P_COUNT; p++)
        if (vm_global((u16)(READ_BY_SCRIPT + p)) != prop_rows[p].read)
            test_fail(__FILE__, __LINE__, prop_rows[p].what);

    pos_x[i] = -FX(7);
    pos_y[i] = 3;
    vel_x[i] = 1;
    vel_y[i] = -1;
    spr_id[i] = 0xFFFF;
    spr_frame[i] = 200;
    spr_flags[i] = 0x8001;
    spr_angle[i] = 0xFFFF;
    spr_depth[i] = -2;
    spr_scale[i] = -32768;
    body_w[i] = 200;
    body_h[i] = 7;
    ent_mask[i] = components | C_ALIVE | C_GAME(1) | C_GAME(13);
    spr_anim_time[i] = 250;
    spr_anim_step[i] = 3;
    body_bounce[i] = 255;
    body_friction[i] = 7;
    body_max_fall[i] = 0xFFFF;
    body_gravity[i] = BODY_GRAVITY(-112); // -128
    body_contact[i] = BODY_SIDE_BOTTOM | BODY_CONTACT_EXIT;
    static const s32 from_c[VM_P_COUNT] = {
        [VM_P_X] = -FX(7),
        [VM_P_Y] = 3,
        [VM_P_VX] = 1,
        [VM_P_VY] = -1,
        [VM_P_SPR] = 0xFFFF,
        [VM_P_FRAME] = 200,
        [VM_P_FLAGS] = 0x8001,
        [VM_P_ANGLE] = 0xFFFF,
        [VM_P_DEPTH] = -2,
        [VM_P_SCALE] = -32768,
        [VM_P_BODY_W] = 200,
        [VM_P_BODY_H] = 7,
        [VM_P_TAGS] = 1 << 1 | 1 << 13,
        [VM_P_ANIM_TIME] = 250,
        [VM_P_ANIM_STEP] = 3,
        [VM_P_BODY_BOUNCE] = 255,
        [VM_P_BODY_FRICTION] = 7,
        [VM_P_BODY_MAX_FALL] = 0xFFFF,
        [VM_P_BODY_GRAVITY] = -128,
        [VM_P_BODY_CONTACT] = BODY_SIDE_BOTTOM | BODY_CONTACT_EXIT,
        [VM_P_OBJECT] = -1, // not attached
    };
    start(1);
    vm_step();
    u32 wrong = 0;
    for (u32 p = 0; p < VM_P_COUNT; p++)
        wrong += vm_global((u16)(READ_FROM_C + p)) != from_c[p];
    CHECK(wrong == 0);
    CHECK_WARNED(before, 0);
}

// vm.md "Entities": a dead (stale, destroyed) or ENTITY_NONE entity warns
// (once: one kind of problem): GETP pushes 0, SETP is dropped - even when a
// live entity has the slot.
static void properties_of_dead_entities(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    ldg(0);         // stale
    getp(VM_P_X);   // warns: 0
    stg(1);         // glob[1] = 0
    ldg(0);         // stale
    push8(5);       // stale 5
    setp(VM_P_X);   // warns, dropped
    ldg(2);         // destroyed
    getp(VM_P_Y);   // warns: 0
    stg(3);         // glob[3] = 0
    push8(0);       // ENTITY_NONE
    getp(VM_P_X);   // warns: 0
    stg(4);         // glob[4] = 0
    push8(0);       // ENTITY_NONE
    push8(5);       // ENTITY_NONE 5
    setp(VM_P_Y);   // warns, dropped
    store(5, 1);    // carried on
    op(VM_OP_HALT); //
    CHECK(load());
    Entity live = entity_create(C_POS | C_VEL | C_SPR); // slot 0, as ENTITY_NONE's index
    Entity gone = entity_create(C_POS);
    entity_destroy(gone);
    u32 generation = entity_generation(live) == 255 ? 1u : entity_generation(live) + 1u;
    Entity stale = (Entity)(generation << 8 | entity_index(live)); // same slot, other generation
    CHECK(stale != ENTITY_NONE && !entity_alive(stale));
    u32 i = entity_index(live);
    pos_x[i] = FX(9);
    vm_set_global(0, stale);
    vm_set_global(2, gone);
    vm_set_global(1, 99);
    vm_set_global(3, 99);
    vm_set_global(4, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(1) == 0 && vm_global(3) == 0 && vm_global(4) == 0);
    CHECK(vm_global(5) == 1);
    CHECK(pos_x[i] == FX(9) && pos_y[i] == 0); // the live entity is untouched
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// vm.md "Entities": a property whose component the entity lacks warns (once,
// for SETP and GETP alike), but still reads and writes the array.
// vm.md "Entity cells": a cell is an entity only within 0..0xFFFF, and a handle
// whose low byte names a slot past MAX_ENT (128) is no entity either:
// entity_alive() bounds-checks the slot, so GETP and SETP never index the
// pools out of range (ASan would catch it on the host).
static void properties_of_handles_past_the_pool(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_ROOM_START);
    push32(0x01FF);    // generation 1, slot 255
    getp(VM_P_X);      // warns: 0
    stg(0);            // glob[0] = 0
    push32(0x01FF);    // the handle again
    push8(5);          // handle 5
    setp(VM_P_X);      // dropped
    push32(0x7F80);    // generation 127, slot 128: the first past the pool
    getp(VM_P_BODY_W); // 0
    stg(1);            // glob[1] = 0
    store(2, 1);       // carried on
    op(VM_OP_HALT);    //
    CHECK(load());
    vm_set_global(0, 99);
    vm_set_global(1, 99);
    u32 before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_ROOM_START) >= 0);
    vm_step();
    CHECK(vm_global(0) == 0 && vm_global(1) == 0 && vm_global(2) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

static void property_without_its_component_warns(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    ldg(0);         // e
    push8(5);       // e 5
    setp(VM_P_VX);  // warns, writes
    ldg(0);         // e
    getp(VM_P_VX);  // warns, reads: 5
    stg(1);         // glob[1] = 5
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS); // no C_VEL
    vm_set_global(0, e);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vel_x[entity_index(e)] == 5);
    CHECK(vm_global(1) == 5);
    CHECK_WARNED(before, 1);
}

// vm.md "Entities": the body size, VM_P_BODY_W and VM_P_BODY_H, is what
// body_overlap() tests, so a script can size an entity it spawned. Its
// component is C_BODY: without it, GETP and SETP warn (once: the same kind of
// problem as any other property's missing component) but still use the
// arrays.
static void body_size_properties(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE); // sizes self 6 x 300 (a u8: 44), reads it back
    op(VM_OP_SELF);           // self
    push8(6);                 // self 6
    setp(VM_P_BODY_W);        //
    op(VM_OP_SELF);           // self
    push16(300);              // self 300
    setp(VM_P_BODY_H);        // 300 & 0xFF = 44
    op(VM_OP_SELF);           // self
    getp(VM_P_BODY_W);        // 6
    stg(0);                   // glob[0] = 6
    op(VM_OP_SELF);           // self
    getp(VM_P_BODY_H);        // 44
    stg(1);                   // glob[1] = 44
    op(VM_OP_HALT);           //
    handler(1, VM_EV_CREATE); // the same on an entity without C_BODY
    op(VM_OP_SELF);           // self
    push8(9);                 // self 9
    setp(VM_P_BODY_W);        // warns, writes
    op(VM_OP_SELF);           // self
    getp(VM_P_BODY_W);        // warns (no repeat), reads: 9
    stg(2);                   // glob[2] = 9
    op(VM_OP_HALT);           //
    CHECK(load());
    Entity a = entity_create(C_POS | C_BODY);
    Entity b = entity_create(C_POS | C_BODY);
    Entity c = entity_create(C_POS);
    u32 ia = entity_index(a), ib = entity_index(b);
    pos_x[ib] = FX(5); // 1 pixel inside a once a is 6 wide
    body_w[ib] = body_h[ib] = 4;
    CHECK(!body_overlap(ia, ib)); // a is 0 x 0 so far
    vm_attach(a, 0);
    u32 before = debug_warning_count();
    vm_events();
    CHECK(body_w[ia] == 6 && body_h[ia] == 44);
    CHECK(vm_global(0) == 6 && vm_global(1) == 44);
    CHECK(body_overlap(ia, ib));
    CHECK_WARNED(before, 0);
    vm_attach(c, 1);
    vm_events();
    CHECK(body_w[entity_index(c)] == 9 && vm_global(2) == 9);
    CHECK_WARNED(before, 1);
}

// vm.md "Entities": VM_P_BODY_CONTACT is read-only. SETP of it warns (once:
// one kind of problem) and writes nothing, whatever the entity (ENTITY_NONE
// gets the read-only warning, not the dead entity's; a live body), and pops
// both its operands; the script carries on. GETP reads it as usual.
static void read_only_properties(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(42);               // 42: below the rest, to show the stack stays balanced
    push8(0);                // 42 ENTITY_NONE
    push8(1);                // 42 0 1
    setp(VM_P_BODY_CONTACT); // 42: warns (read-only), dropped
    op(VM_OP_SELF);          // 42 self
    push8(BODY_SIDE_TOP);    // 42 self 2
    setp(VM_P_BODY_CONTACT); // 42: dropped (no repeat)
    push8(0);                // 42 ENTITY_NONE
    getp(VM_P_BODY_CONTACT); // 42 0: warns (a dead entity), another kind
    op(VM_OP_DROP);          // 42
    op(VM_OP_SELF);          // 42 self
    getp(VM_P_BODY_CONTACT); // 42 contact
    stg(0);                  // 42: glob[0] = what C set
    stg(1);                  // glob[1] = 42
    op(VM_OP_HALT);          //
    CHECK(load());
    Entity e = entity_create(C_POS | C_BODY);
    u32 i = entity_index(e);
    body_contact[i] = BODY_SIDE_LEFT;
    u32 before = debug_warning_count();
    vm_attach(e, 0);
    vm_events();
    CHECK(body_contact[i] == BODY_SIDE_LEFT);
    CHECK(vm_global(0) == BODY_SIDE_LEFT && vm_global(1) == 42);
    CHECK(vm_idle());
    CHECK_WARNED(before, 2);
}

// vm.md "Entities": VM_P_OBJECT is the object an entity is attached to, and
// -1 for one that isn't: never attached, detached, killed (and so dead: that
// warns, as any property of a dead entity, but reads -1, not 0, which is an
// object's number), or ENTITY_NONE. It is read-only. vm_object_of() gives C
// the same, without warnings for entities that simply aren't attached; an
// entity destroyed behind the VM's back is a stale binding (warns once).
static void objects_of_entities(void) {
    reset();
    blob_begin(3, 0, GLOBALS);
    handler(0, VM_EV_ROOM_START); // glob[k] = the object of the entity in glob[10 + k]
    for (u32 k = 0; k < 5; k++) {
        ldg(10 + k);       // e
        getp(VM_P_OBJECT); // its object
        stg(k);            //
    }
    ldg(10);                     // e
    push8(2);                    // e 2
    setp(VM_P_OBJECT);           // warns: read-only
    op(VM_OP_HALT);              //
    handler(2, VM_EV_COLLISION); // glob[5] = 1 if OTHER is attached to object 1
    op(VM_OP_OTHER);             // other
    getp(VM_P_OBJECT);           // its object
    push8(1);                    // its object, 1
    op(VM_OP_EQ);                // 0 or 1
    stg(5);                      //
    op(VM_OP_HALT);              //
    CHECK(load());
    CHECK(vm_object_of(ENTITY_NONE) == -1);
    Entity first = entity_create(C_POS);  // attached to object 0
    Entity second = entity_create(C_POS); // attached to object 1
    Entity loose = entity_create(C_POS);  // never attached
    Entity killed = entity_create(C_POS); // attached, then killed
    vm_attach(first, 0);
    vm_attach(second, 1);
    vm_attach(killed, 1);
    vm_events();
    vm_kill(killed);
    CHECK(vm_object_of(first) == 0 && vm_object_of(second) == 1);
    CHECK(vm_object_of(loose) == -1 && vm_object_of(killed) == -1);
    vm_set_global(10, first);
    vm_set_global(11, second);
    vm_set_global(12, loose);
    vm_set_global(13, killed);
    vm_set_global(14, ENTITY_NONE);
    u32 before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_ROOM_START) >= 0);
    vm_step();
    CHECK(vm_global(0) == 0 && vm_global(1) == 1 && vm_global(2) == -1);
    CHECK(vm_global(3) == -1 && vm_global(4) == -1); // a dead entity and none: no object 0
    CHECK(vm_object_of(first) == 0);                 // SETP wrote nothing
    CHECK_WARNED(before, 2);                         // a dead entity's property; read-only
    // A Collision reaction tells what it met by its object.
    Entity target = entity_create(C_POS);
    vm_attach(target, 2);
    vm_event(target, second, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_global(5) == 1);
    vm_event(target, first, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_global(5) == 0);
    // Detached, or destroyed behind the VM's back (a stale binding: warns).
    vm_detach(first);
    CHECK(vm_object_of(first) == -1);
    before = debug_warning_count();
    entity_destroy(second);
    CHECK(vm_object_of(second) == -1);
    CHECK_WARNED(before, 1);
    // Nothing is attached without a blob.
    vm_unload();
    CHECK(vm_object_of(target) == -1);
}

// vm.md "Entities": VM_P_BODY_CONTACT is body_contact, what the body touched
// in the last sys_physics() (with contacts on) or sys_map_movement(). A Step
// reaction (vm_step, before the systems) reads what the previous frame's
// physics reported: nothing on the first frame, then the floor of the bounds
// the body rests on. The tuning properties a script set are what the systems
// use: body_gravity BODY_GRAVITY(0) keeps a second body from falling.
static void body_properties_meet_physics(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_STEP);   // the contacts, a digit per frame
    count(0);                 //
    op(VM_OP_SELF);           // self
    getp(VM_P_BODY_CONTACT);  // contact (BODY_SIDE_BOTTOM is 1)
    append_top(1);            // glob[1] = glob[1] * 10 + contact
    op(VM_OP_HALT);           //
    handler(1, VM_EV_CREATE); // no gravity for this one
    op(VM_OP_SELF);           // self
    push(BODY_GRAVITY(0));    // self -16
    setp(VM_P_BODY_GRAVITY);  //
    op(VM_OP_HALT);           //
    CHECK(load());
    physics_set_gravity(0, FX_ONE / 4);
    physics_set_contacts(true);
    Entity e = entity_create(C_POS | C_VEL | C_BODY);
    Entity floating = entity_create(C_POS | C_VEL | C_BODY);
    u32 i = entity_index(e), j = entity_index(floating);
    body_w[i] = body_h[i] = body_w[j] = body_h[j] = 8;
    pos_x[i] = FX(20);
    pos_y[i] = FX(SCREEN_H - 8); // resting on the default bounds' floor
    pos_x[j] = FX(60);
    pos_y[j] = FX(20);
    vm_attach(e, 0);
    vm_attach(floating, 1);
    u32 before = debug_warning_count();
    for (u32 f = 0; f < 3; f++) {
        vm_step();
        sys_movement();
        sys_physics();
        vm_events();
    }
    _Static_assert(BODY_SIDE_BOTTOM == 1, "a digit per frame");
    CHECK(vm_global(0) == 3);
    CHECK(vm_global(1) == 11); // 0, then the floor twice
    CHECK(body_gravity[j] == BODY_GRAVITY(0) && vel_y[j] == 0 && pos_y[j] == FX(20));
    CHECK_WARNED(before, 0);
    physics_set_contacts(false);
    physics_set_gravity(0, 0);
}

// vm.md "Exact semantics: Spawning": SPAWN creates the entity with its
// object's component mask, C_KINEMATIC (physics.h) included: a shot that
// moves only by its velocity, which sys_physics() leaves alone (no gravity),
// and whose body vm_collide() tests like any other.
static void spawned_kinematic_bodies(void) {
    reset();
    blob_begin(3, 0, GLOBALS);
    object(1, C_POS | C_VEL | C_BODY | C_KINEMATIC | C_GAME(0), 0);
    object(2, C_POS | C_BODY | C_GAME(1), 0);
    handler(0, VM_EV_ROOM_START); // the gun
    push32(FX(10));               // x
    push32(FX(20));               // x y
    spawn(1);                     // shot
    stg(0);                       // glob[0] = shot
    op(VM_OP_HALT);               //
    handler(1, VM_EV_CREATE);     // the shot
    op(VM_OP_SELF);               // self
    push32(FX(2));                // self 2.0
    setp(VM_P_VX);                //
    op(VM_OP_SELF);               // self
    push8(4);                     // self 4
    setp(VM_P_BODY_W);            //
    op(VM_OP_SELF);               // self
    push8(4);                     // self 4
    setp(VM_P_BODY_H);            //
    op(VM_OP_HALT);               //
    handler(2, VM_EV_COLLISION);  // the target: glob[1] = the frame it is hit
    ldg(2);                       // frame
    stg(1);                       //
    op(VM_OP_HALT);               //
    CHECK(load());
    CHECK(vm_collide(C_GAME(1), C_GAME(0)));
    physics_set_gravity(0, FX_ONE / 4);
    Entity target = entity_create(C_POS | C_BODY | C_GAME(1));
    u32 t = entity_index(target);
    pos_x[t] = FX(30);
    pos_y[t] = FX(20);
    body_w[t] = body_h[t] = 4;
    vm_attach(target, 2);
    CHECK(vm_start(0, VM_EV_ROOM_START) >= 0);
    u32 before = debug_warning_count();
    for (s32 f = 1; f <= 9; f++) {
        vm_set_global(2, f);
        vm_step();
        sys_movement();
        sys_physics();
        vm_events();
    }
    Entity shot = (Entity)vm_global(0);
    u32 i = entity_index(shot);
    CHECK(entity_alive(shot) &&
          ent_mask[i] == (C_POS | C_VEL | C_BODY | C_KINEMATIC | C_GAME(0) | C_ALIVE));
    CHECK(pos_x[i] == FX(10 + 2 * 9) && pos_y[i] == FX(20) && vel_y[i] == 0); // straight on
    CHECK(vm_global(1) == 9); // x 28: its 4 pixels reach the target at 30
    CHECK_WARNED(before, 0);
    physics_set_gravity(0, 0);
}

// vm.md "Entities": VM_P_TAGS reads C_GAME(0) to C_GAME(14) as bits 0 to 14,
// and SETP changes only those components: the engine's, and C_ALIVE, stay
// (bit 15 and up of the value are ignored). Systems see the change at once.
// No component is needed: no warning.
static void tags_are_the_game_components(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_SELF);  // self
    getp(VM_P_TAGS); // tags
    stg(0);          // glob[0]
    op(VM_OP_SELF);  // self
    push32(0x18001); // self 0x18001: bits 0 and 15 (no C_GAME(15)), and 16
    setp(VM_P_TAGS); //
    op(VM_OP_SELF);  // self
    getp(VM_P_TAGS); // 1
    stg(1);          // glob[1]
    op(VM_OP_HALT);  //
    CHECK(load());
    Entity e = entity_create(C_POS | C_VEL | C_GAME(2) | C_GAME(14));
    u32 i = entity_index(e);
    u32 before = debug_warning_count();
    vm_attach(e, 0);
    vm_events();
    CHECK(vm_global(0) == (1 << 2 | 1 << 14));
    CHECK(vm_global(1) == 1);
    CHECK(ent_mask[i] == (C_POS | C_VEL | C_GAME(0) | C_ALIVE));
    CHECK(ecs_count(C_GAME(0)) == 1 && ecs_count(C_GAME(2)) == 0);
    CHECK(entity_alive(e));
    CHECK_WARNED(before, 0);
}

// vm.md "Entities": instance fields (VM_P_FIELD0 on) are VM_FIELDS cells of
// each attached entity: zeroed when it is attached (again, too), its own
// (another instance's are readable by handle, from any script), and not there
// for an unattached entity (warns once; reads 0, writes nothing).
static void instance_fields(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_SELF);                  // self
    getp(VM_P_FIELD0);               // 0: zeroed at attach
    ldg(0);                          //
    op(VM_OP_ADD);                   //
    stg(0);                          // glob[0] += field 0
    op(VM_OP_SELF);                  // self
    op(VM_OP_SELF);                  // self self
    getp(VM_P_X);                    // self x
    setp(VM_P_FIELD0);               // field 0 = x
    op(VM_OP_SELF);                  // self
    push8(6);                        // self 6
    setp(VM_P_FIELD(VM_FIELDS - 1)); // the last field = 6
    op(VM_OP_HALT);                  //
    handler(0, VM_EV_COLLISION);
    op(VM_OP_OTHER);                 // other
    getp(VM_P_FIELD(VM_FIELDS - 1)); // 6
    stg(1);                          // glob[1]: another instance's field
    op(VM_OP_SELF);                  // self
    getp(VM_P_FIELD0);               // its x
    stg(2);                          // glob[2]
    op(VM_OP_HALT);                  //
    handler(1, VM_EV_CREATE);        // a thread
    ldg(5);                          // an unattached entity
    getp(VM_P_FIELD0);               // warns: 0
    stg(3);                          // glob[3] = 0
    ldg(5);                          //
    push8(1);                        //
    setp(VM_P_FIELD(1));             // warns (no repeat), nothing written
    ldg(6);                          // an attached one
    getp(VM_P_FIELD0);               // its x
    stg(4);                          // glob[4]
    op(VM_OP_HALT);                  //
    CHECK(load());
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    Entity loose = entity_create(C_POS);
    pos_x[entity_index(a)] = 1;
    pos_x[entity_index(b)] = 2;
    u32 before = debug_warning_count();
    vm_attach(a, 0);
    vm_attach(b, 0);
    vm_events();
    CHECK(vm_global(0) == 0);
    vm_event(a, b, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_global(1) == 6 && vm_global(2) == 1);
    vm_event(b, a, VM_EV_COLLISION);
    vm_events();
    CHECK(vm_global(1) == 6 && vm_global(2) == 2);
    vm_attach(a, 0); // attached again: zeroed, then its Create sets them
    vm_events();
    CHECK(vm_global(0) == 0);
    CHECK_WARNED(before, 0);
    vm_set_global(3, 99);
    vm_set_global(5, loose);
    vm_set_global(6, b);
    start(1);
    vm_step();
    CHECK(vm_global(3) == 0 && vm_global(4) == 2);
    CHECK_WARNED(before, 1);
    vm_detach(b); // b's fields go with its binding
    vm_set_global(5, b);
    start(1);
    vm_step();
    CHECK(vm_global(3) == 0);
    CHECK_WARNED(before, 1);
}

// vm.md "Entities" (NEXTI), "Exact semantics: Arrays, fields, instances":
// NEXTI e gives the next attached instance of the object after e in slot
// order (0: from the first), and 0 when none is left: a loop over every
// instance, skipping other objects' and unattached entities. Slots are
// compared, so it goes on from an entity that is dead by now, and a loop that
// KILLs each instance it visits (queued) still visits them all. An object the
// blob doesn't have warns and gives 0.
static void nexti_loops_over_an_objects_instances(void) {
    enum { L_NEXT, L_DONE };
    reset();
    blob_begin(4, 0, GLOBALS);
    handler(2, VM_EV_CREATE);
    push8(0);                // 0: from the first
    label(L_NEXT);           //
    nexti(0);                // e
    op(VM_OP_DUP);           // e e
    jump(VM_OP_JZ, L_DONE);  // e
    op(VM_OP_DUP);           // e e
    getp(VM_P_X);            // e x
    append_top(0);           // e: glob[0] = glob[0] * 10 + x
    op(VM_OP_DUP);           // e e
    op(VM_OP_KILL);          // e: its Destroy is queued
    jump(VM_OP_JMP, L_NEXT); //
    label(L_DONE);           // 0
    op(VM_OP_DROP);          //
    op(VM_OP_HALT);          //
    handler(3, VM_EV_CREATE);
    ldg(5);         // a dead entity's handle
    nexti(0);       // the next instance after its slot
    stg(1);         // glob[1]
    push8(0);       //
    nexti(7);       // no object 7: warns, 0
    stg(2);         // glob[2] = 0
    op(VM_OP_HALT); //
    CHECK(load());
    static const u16 objects[] = {0, 1, 0, 0xFFFF, 0, 0}; // 0xFFFF: not attached
    Entity e[6];
    for (u32 k = 0; k < 6; k++) {
        e[k] = entity_create(C_POS);
        pos_x[entity_index(e[k])] = (FIXED)(k + 1);
    }
    for (u32 k = 6; k-- > 0;) // attached out of slot order
        if (objects[k] != 0xFFFF)
            vm_attach(e[k], objects[k]);
    vm_events();
    vm_kill(e[2]); // dead now
    u32 before = debug_warning_count();
    vm_set_global(5, e[2]);
    vm_set_global(2, 99);
    start(3);
    vm_step();
    CHECK(vm_global(1) == e[4] && vm_global(2) == 0);
    CHECK_WARNED(before, 1);
    start(2);
    vm_step(); // e[0], e[4] and e[5], then their Destroys
    CHECK(vm_global(0) == 156);
    CHECK(!entity_alive(e[0]) && !entity_alive(e[4]) && !entity_alive(e[5]));
    CHECK(entity_alive(e[1]) && entity_alive(e[3]));
    vm_set_global(0, 0);
    start(2);
    vm_step(); // none left
    CHECK(vm_global(0) == 0);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
}

// --- Waits on the engine -----------------------------------------------------

// vm.md "Waits": WAIT_MOVE resumes once self has no C_PATH (sys_path removes
// it when the path ends); a pathless entity goes on at once.
static void wait_move_resumes_when_the_path_ends(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    for (u16 obj = 0; obj < 2; obj++) {
        handler(obj, VM_EV_CREATE);
        op(VM_OP_WAIT_MOVE); //
        store(obj, 1);       //
        op(VM_OP_HALT);      //
    }
    CHECK(load());
    Entity mover = entity_create(C_POS | C_VEL);
    Entity still = entity_create(C_POS | C_VEL);
    path_start(mover, &move_path, 0);
    vm_attach(mover, 0);
    vm_attach(still, 1);
    u32 before = debug_warning_count();
    for (s32 f = 1; f <= 4; f++) {
        vm_step();
        CHECK(vm_global(0) == (f == 4)); // the 3-frame path ends in frame 3's sys_path
        CHECK(vm_global(1) == 1);
        sys_path();
        sys_movement();
        vm_events();
    }
    CHECK(!path_active(mover));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

static const u32 anim_tiles[8 * 3];
static const SpriteAsset anim_once = {
    .size = SPRITE_8x8, .tiles = anim_tiles, .frame_count = 3, .flags = SPRITE_ASSET_ANIM_ONCE};
static const SpriteAsset anim_looping = {.size = SPRITE_8x8, .tiles = anim_tiles, .frame_count = 3};
enum { SPR_ONCE, SPR_LOOPING, SPR_COUNT };
static const SpriteAsset* const anim_sprites[SPR_COUNT] = {
    [SPR_ONCE] = &anim_once,
    [SPR_LOOPING] = &anim_looping,
};

static const SpriteAsset* const* saved_sprite_table;
static u16 saved_sprite_count;

// Installs this file's sprite table (sys_animate and anim_finished read it),
// as anim_tests does; restore_sprites puts back the previous one.
static void use_anim_sprites(void) {
    saved_sprite_table = serval_sprite_table;
    saved_sprite_count = serval_sprite_count;
    serval_sprite_table = anim_sprites;
    serval_sprite_count = SPR_COUNT;
}

static void restore_sprites(void) {
    serval_sprite_table = saved_sprite_table;
    serval_sprite_count = saved_sprite_count;
}

// vm.md "Waits", "Public API" (anim_finished): WAIT_ANIM resumes in the
// first pass after sys_animate leaves a one-shot animation on its last frame.
static void wait_anim_resumes_on_the_last_frame(void) {
    reset();
    use_anim_sprites();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_WAIT_ANIM); //
    store(0, 1);         //
    op(VM_OP_HALT);      //
    CHECK(load());
    Entity e = entity_create(C_SPR | C_ANIM);
    u32 i = entity_index(e);
    spr_id[i] = SPR_ONCE;
    vm_attach(e, 0);
    u32 before = debug_warning_count();
    for (s32 f = 1; f <= 3; f++) {
        vm_step();
        CHECK(vm_global(0) == (f == 3)); // frames 0, 1, 2: the last from frame 2's sys_animate
        vm_events();
        sys_animate();
    }
    CHECK(spr_frame[i] == 2);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    restore_sprites();
}

// vm.md "Waits": WAIT_ANIM without C_ANIM, on a looping sprite, or in a
// thread with no entity, warns (once: one kind of problem) and goes on at
// once.
static void wait_anim_without_a_one_shot_animation_continues(void) {
    reset();
    use_anim_sprites();
    blob_begin(3, 0, GLOBALS);
    for (u16 obj = 0; obj < 3; obj++) {
        handler(obj, VM_EV_CREATE);
        op(VM_OP_WAIT_ANIM); // warns, goes on
        store(obj, 1);       //
        op(VM_OP_HALT);      //
    }
    CHECK(load());
    Entity no_anim = entity_create(C_SPR);
    Entity looping = entity_create(C_SPR | C_ANIM);
    spr_id[entity_index(no_anim)] = SPR_ONCE;
    spr_id[entity_index(looping)] = SPR_LOOPING;
    vm_attach(no_anim, 0);
    vm_attach(looping, 1);
    u32 before = debug_warning_count();
    vm_events();
    CHECK(vm_global(0) == 1 && vm_global(1) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
    start(2); // a thread
    vm_step();
    CHECK(vm_global(2) == 1);
    CHECK(vm_idle());
    restore_sprites();
}

// vm.md "Waits": WAIT_ANIM checks for a one-shot animation on every resume
// pass while it waits. A game switching the sprite to a looping one mid-wait
// makes the script warn and go on, instead of waiting forever.
static void wait_anim_continues_if_the_sprite_stops_being_one_shot(void) {
    reset();
    use_anim_sprites();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_WAIT_ANIM); // waits: frame 0 of 3
    store(0, 1);         //
    op(VM_OP_HALT);      //
    CHECK(load());
    Entity e = entity_create(C_SPR | C_ANIM);
    u32 i = entity_index(e);
    spr_id[i] = SPR_ONCE;
    vm_attach(e, 0);
    u32 before = debug_warning_count();
    vm_step();
    vm_events();
    CHECK(vm_global(0) == 0 && !vm_idle());
    CHECK_WARNED(before, 0);
    spr_id[i] = SPR_LOOPING; // the game switches sprites mid-wait
    vm_step();
    CHECK(vm_global(0) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
    restore_sprites();
}

// vm.md "Exact semantics: Animation End": vm_step() queues Animation End,
// after the resume pass, when anim_finished() turns true for an attached
// entity whose object has a handler: once per finish (an edge), again after
// the game restarts the animation. The last value starts false at attach, so
// an entity attached on its last frame raises it at once. A looping sprite
// never finishes.
static void animation_end_is_raised_when_an_animation_finishes(void) {
    reset();
    use_anim_sprites();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_ANIM_END);
    count(0);       // glob[0]: the one-shot entity's Animation End runs
    op(VM_OP_HALT); //
    handler(1, VM_EV_ANIM_END);
    count(1);       // glob[1]: the looping entity's
    op(VM_OP_HALT); //
    CHECK(load());
    Entity once = entity_create(C_SPR | C_ANIM);
    Entity looping = entity_create(C_SPR | C_ANIM);
    u32 i = entity_index(once);
    spr_id[i] = SPR_ONCE;
    spr_id[entity_index(looping)] = SPR_LOOPING;
    vm_attach(once, 0);
    vm_attach(looping, 1);
    u32 before = debug_warning_count();
    // glob[0] after each frame's vm_step(): frames 0, 1, 2 are shown from
    // frames 1, 2, 3, so the animation has finished in frame 3's; the game
    // restarts it before frame 6, so it finishes again in frame 8's.
    static const s32 ends[] = {0, 0, 1, 1, 1, 1, 1, 2, 2};
    for (u32 f = 0; f < sizeof ends / sizeof ends[0]; f++) {
        if (f == 5) {
            spr_frame[i] = 0;
            spr_anim_time[i] = 0;
        }
        vm_step();
        CHECK(vm_global(0) == ends[f]);
        vm_events();
        sys_animate();
    }
    CHECK(spr_frame[i] == 2);
    vm_attach(once, 0); // re-attached, still on its last frame
    vm_step();
    CHECK(vm_global(0) == 3);
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 3);
    CHECK(vm_global(1) == 0);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    restore_sprites();
}

// vm.md "Entities": VM_P_ANIM_TIME and VM_P_ANIM_STEP are spr_anim_time and
// spr_anim_step; with VM_P_FRAME, what restarting an animation sets. A script
// that restarts its one-shot animation in its Animation End reaction gets
// another Animation End when it finishes again.
static void animation_properties_restart_an_animation(void) {
    reset();
    use_anim_sprites();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_ANIM_END);
    count(0);             // glob[0]: Animation Ends
    op(VM_OP_SELF);       // self
    getp(VM_P_ANIM_TIME); // 0: sys_animate leaves it there on the last frame
    stg(1);               // glob[1]
    op(VM_OP_SELF);       //
    push8(0);             //
    setp(VM_P_FRAME);     // restart: frame 0...
    op(VM_OP_SELF);       //
    push8(0);             //
    setp(VM_P_ANIM_TIME); // ...shown for no frames yet
    op(VM_OP_SELF);       //
    push8(0);             //
    setp(VM_P_ANIM_STEP); // (no frame_order: unused)
    op(VM_OP_HALT);       //
    CHECK(load());
    Entity e = entity_create(C_SPR | C_ANIM);
    u32 i = entity_index(e);
    spr_id[i] = SPR_ONCE;
    vm_attach(e, 0);
    u32 before = debug_warning_count();
    // As in animation_end_is_raised_when_an_animation_finishes: it finishes
    // in frame 3's vm_step(); restarted there, it finishes again two frames
    // later, and so on.
    static const s32 ends[] = {0, 0, 1, 1, 2, 2, 3, 3, 4};
    vm_set_global(1, 99);
    for (u32 f = 0; f < sizeof ends / sizeof ends[0]; f++) {
        vm_step();
        CHECK(vm_global(0) == ends[f]);
        vm_events();
        sys_animate();
    }
    CHECK(vm_global(1) == 0);
    CHECK_WARNED(before, 0);
    restore_sprites();
}

// --- SYS ---------------------------------------------------------------------

// vm.md "Engine calls": the SYS page's numbers are part of the format and
// never change; a rename (VM_SYS_PSG_MUSIC_*, VM_SYS_SCREEN_SET_BRIGHTNESS)
// keeps them.
static void sys_page_numbers_are_fixed(void) {
    static const u32 page[] = {VM_SYS_PSG_PLAY,
                               VM_SYS_PSG_MUSIC_PLAY,
                               VM_SYS_PSG_MUSIC_STOP,
                               VM_SYS_PSG_MUSIC_PAUSE,
                               VM_SYS_PSG_MUSIC_RESUME,
                               VM_SYS_CAMERA_SET,
                               VM_SYS_TEXT_PRINT,
                               VM_SYS_RANDOM_RANGE,
                               VM_SYS_BUTTON_DOWN,
                               VM_SYS_BUTTON_PRESSED,
                               VM_SYS_SCREEN_SET_BRIGHTNESS,
                               VM_SYS_PATH_START,
                               VM_SYS_TEXT_PRINT_NUMBER,
                               VM_SYS_PATH_STOP,
                               VM_SYS_SCREEN_SET_BLEND};
    u32 moved = 0;
    for (u32 k = 0; k < sizeof page / sizeof page[0]; k++)
        moved += page[k] != k;
    CHECK(moved == 0);
    CHECK(VM_SYS_SCREEN_SET_BLEND == 14);
    CHECK(VM_SYS_SPRITE_SET_COLORS == 15 && VM_SYS_TILESET_SET_COLORS == 16); // palettes
    CHECK(VM_SYS_COUNT == 17);
}

// vm.md "Engine calls": SYS random_range(lo, hi) is the engine's (the same
// sequence from the same seed), pops two and pushes one.
static void sys_random_range(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    for (u32 k = 0; k < 8; k++) {
        push8(-3);                // lo
        push8(4);                 // lo hi
        sys(VM_SYS_RANDOM_RANGE); // r
        stg(k);                   // glob[k] = r
    }
    push8(55);                // 55
    push8(0);                 // 55 0
    push8(1);                 // 55 0 1
    sys(VM_SYS_RANDOM_RANGE); // 55 r
    op(VM_OP_DROP);           // 55
    stg(8);                   // glob[8] = 55
    op(VM_OP_HALT);           //
    CHECK(load());
    start(0);
    u32 before = debug_warning_count();
    random_seed(1234);
    vm_step();
    random_seed(1234);
    u32 wrong = 0, outside = 0;
    for (u16 k = 0; k < 8; k++) {
        s32 r = vm_global(k);
        wrong += r != random_range(-3, 4);
        outside += r < -3 || r > 4;
    }
    CHECK(wrong == 0 && outside == 0);
    CHECK(vm_global(8) == 55);
    CHECK_WARNED(before, 0);
}

// vm.md "Engine calls": SYS camera_set(x, y) sets the camera (map.h), pops
// two, pushes nothing.
static void sys_camera_set(void) {
    reset();
    map_unload(2); // no playfield: the camera isn't clamped
    camera_set(0, 0);
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(55);              // 55
    push16(-50);            // 55 x
    push16(1000);           // 55 x y
    sys(VM_SYS_CAMERA_SET); // 55
    stg(0);                 // glob[0] = 55
    op(VM_OP_HALT);         //
    CHECK(load());
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(camera_x() == -50 && camera_y() == 1000);
    CHECK(vm_global(0) == 55);
    CHECK_WARNED(before, 0);
    camera_set(0, 0);
}

static const PathStep sys_path_steps[] = {{.frames = 10, .speed = FX(1)}};
static const Path sys_path_down = {PATH_STEPS(sys_path_steps), .heading = ANGLE_DEG(90)};
static const Path sys_path_right = {PATH_STEPS(sys_path_steps)};
static const Path* const sys_paths[] = {&sys_path_down, &sys_path_right};

// vm.md "Engine calls": SYS path_start(entity, path index, flags) starts
// VmBindings.paths[index]; a bad index or no paths bound warns (once: one
// kind of problem) and does nothing. Pops three, pushes nothing.
static void sys_path_start_uses_bindings(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(55);              // 55
    ldg(0);                 // 55 e
    ldg(1);                 // 55 e index
    push8(PATH_MIRROR_X);   // 55 e index flags
    sys(VM_SYS_PATH_START); // 55
    stg(2);                 // glob[2] = 55
    op(VM_OP_HALT);         //
    CHECK(load());
    vm_bind(&(VmBindings){.paths = sys_paths, .path_count = 2});
    Entity e = entity_create(C_POS | C_VEL);
    vm_set_global(0, e);
    vm_set_global(1, 1);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    CHECK(path_active(e));
    CHECK(path_heading[entity_index(e)] == ANGLE_DEG(180)); // path 1 (right), mirrored
    CHECK(vm_global(2) == 55);
    CHECK_WARNED(before, 0);

    Entity f = entity_create(C_POS | C_VEL);
    vm_set_global(0, f);
    vm_set_global(1, 2); // past the bindings
    vm_set_global(2, 0);
    start(0);
    vm_step();
    CHECK(!path_active(f) && vm_global(2) == 55);
    CHECK_WARNED(before, 1);

    vm_bind(&(VmBindings){.path_count = 0}); // no paths bound
    vm_set_global(1, 0);
    vm_set_global(2, 0);
    start(0);
    vm_step();
    CHECK(!path_active(f) && vm_global(2) == 55);
    CHECK_WARNED(before, 1);
}

static const PsgSong song_a = {.tempo = 120};
static const PsgSong song_b = {.tempo = 90};
static const PsgSong* const songs[] = {&song_a, &song_b};

// vm.md "Engine calls", vm_internal.h: a bad string or song index, or no
// songs bound, warns and makes no call (none reaches the platform); the
// arguments are popped as usual. A bad string and a song that isn't bound are
// two kinds of problem: one warning each, none for the repeats.
static void sys_bad_string_or_song_index(void) {
    reset();
    blob_begin(1, 2, GLOBALS);
    string(0, "A");
    string(1, "B");
    handler(0, VM_EV_CREATE);
    push8(55);                  // 55
    push8(3);                   // 55 col
    push8(4);                   // 55 col row
    ldg(0);                     // 55 col row index
    sys(VM_SYS_TEXT_PRINT);     // 55
    stg(1);                     // glob[1] = 55
    push8(56);                  // 56
    ldg(2);                     // 56 index
    sys(VM_SYS_PSG_MUSIC_PLAY); // 56
    stg(3);                     // glob[3] = 56
    op(VM_OP_HALT);             //
    CHECK(load());
    vm_bind(&(VmBindings){.psg_songs = songs, .psg_song_count = 2});
#ifndef SERVAL_GBA
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
#endif
    vm_set_global(0, 2); // one past the last string
    vm_set_global(2, 2); // one past the last song
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    CHECK(vm_global(1) == 55 && vm_global(3) == 56);
    CHECK_WARNED(before, 2);

    vm_bind(&(VmBindings){.psg_song_count = 0}); // no songs bound
    vm_set_global(0, -1);                        // a negative string index
    vm_set_global(2, 0);
    vm_set_global(1, 0);
    vm_set_global(3, 0);
    start(0);
    vm_step();
    CHECK(vm_global(1) == 55 && vm_global(3) == 56);
    CHECK_WARNED(before, 2);
#ifndef SERVAL_GBA
    CHECK(serval_host_vm_calls.calls == 0);
#endif
}

#ifndef SERVAL_GBA
// The platform's latest call, as src/host/platform.c recorded it.
static bool last_call4(u32 calls, u32 fn, s32 a0, s32 a1, s32 a2, s32 a3, const void* ptr) {
    const ServalHostVmCalls* r = &serval_host_vm_calls;
    return r->calls == calls && r->fn == fn && r->args[0] == a0 && r->args[1] == a1 &&
           r->args[2] == a2 && r->args[3] == a3 && r->ptr == ptr;
}

static bool last_call(u32 calls, u32 fn, s32 a0, s32 a1, s32 a2, const void* ptr) {
    return last_call4(calls, fn, a0, a1, a2, 0, ptr);
}
#endif

// vm.md "Implementation notes: Platform calls", vm_internal.h (host only):
// the platform SYS calls reach serval_vm_platform_call with their arguments
// in push order (unused ones 0), the resolved string or song, and the button
// calls return what the platform does.
static void platform_sys_calls_reach_the_platform(void) {
#ifndef SERVAL_GBA
    reset();
    blob_begin(1, 2, GLOBALS);
    string(0, "HI");
    u32 hello = string(1, "HELLO");
    handler(0, VM_EV_CREATE);
    push8(5);                          // sound 5
    sys(VM_SYS_PSG_PLAY);              // frame 1
    wait_frames(1);                    //
    push8(1);                          // song 1
    sys(VM_SYS_PSG_MUSIC_PLAY);        // frame 2
    wait_frames(1);                    //
    sys(VM_SYS_PSG_MUSIC_STOP);        // frame 3
    wait_frames(1);                    //
    sys(VM_SYS_PSG_MUSIC_PAUSE);       // frame 4
    wait_frames(1);                    //
    sys(VM_SYS_PSG_MUSIC_RESUME);      // frame 5
    wait_frames(1);                    //
    push8(3);                          // col
    push8(4);                          // col row
    push8(1);                          // col row string
    sys(VM_SYS_TEXT_PRINT);            // frame 6
    wait_frames(1);                    //
    push16(BUTTON_A | BUTTON_L);       // buttons
    sys(VM_SYS_BUTTON_DOWN);           // frame 7: 1
    stg(0);                            // glob[0] = 1
    wait_frames(1);                    //
    push16(BUTTON_START);              // buttons
    sys(VM_SYS_BUTTON_PRESSED);        // frame 8: 0
    stg(1);                            // glob[1] = 0
    wait_frames(1);                    //
    push8(-16);                        // level
    sys(VM_SYS_SCREEN_SET_BRIGHTNESS); // frame 9
    op(VM_OP_HALT);                    //
    CHECK(load());
    vm_bind(&(VmBindings){.psg_songs = songs, .psg_song_count = 2});
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
    vm_set_global(1, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(last_call(1, VM_SYS_PSG_PLAY, 5, 0, 0, NULL));
    frame();
    CHECK(last_call(2, VM_SYS_PSG_MUSIC_PLAY, 1, 0, 0, &song_b));
    frame();
    CHECK(last_call(3, VM_SYS_PSG_MUSIC_STOP, 0, 0, 0, NULL));
    frame();
    CHECK(last_call(4, VM_SYS_PSG_MUSIC_PAUSE, 0, 0, 0, NULL));
    frame();
    CHECK(last_call(5, VM_SYS_PSG_MUSIC_RESUME, 0, 0, 0, NULL));
    frame();
    CHECK(last_call(6, VM_SYS_TEXT_PRINT, 3, 4, 1, placed + hello));
    const char* text = serval_host_vm_calls.ptr;
    CHECK(text && text[0] == 'H' && text[4] == 'O' && text[5] == '\0');
    serval_host_vm_calls.button_value = 1;
    frame();
    CHECK(last_call(7, VM_SYS_BUTTON_DOWN, BUTTON_A | BUTTON_L, 0, 0, NULL));
    CHECK(vm_global(0) == 1);
    serval_host_vm_calls.button_value = 0;
    frame();
    CHECK(last_call(8, VM_SYS_BUTTON_PRESSED, BUTTON_START, 0, 0, NULL));
    CHECK(vm_global(1) == 0);
    frame();
    CHECK(last_call(9, VM_SYS_SCREEN_SET_BRIGHTNESS, -16, 0, 0, NULL));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    vm_bind(&(VmBindings){.psg_song_count = 0});
#endif
}

// vm.md "Engine calls": SYS text_print_number(col, row, value, width) pops
// four and pushes nothing; it is a platform call (vm_internal.h), so on the
// host the recorder sees its arguments in push order, with no string or song.
// tests/rom/vm_platform_tests.c checks what the GBA prints.
static void sys_text_print_number(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(55);                     // 55
    push8(3);                      // 55 col
    push8(4);                      // 55 col row
    push16(-1234);                 // 55 col row value
    push8(0);                      // 55 col row value width
    sys(VM_SYS_TEXT_PRINT_NUMBER); // 55: frame 1
    stg(0);                        // glob[0] = 55
    wait_frames(1);                //
    push8(5);                      // col
    push8(6);                      // col row
    push32(INT32_MIN);             // col row value
    push8(12);                     // col row value width
    sys(VM_SYS_TEXT_PRINT_NUMBER); // frame 2
    store(1, 1);                   // carried on
    op(VM_OP_HALT);                //
    CHECK(load());
#ifndef SERVAL_GBA
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
#endif
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 55);
#ifndef SERVAL_GBA
    CHECK(last_call4(1, VM_SYS_TEXT_PRINT_NUMBER, 3, 4, -1234, 0, NULL));
#endif
    frame();
    CHECK(vm_global(1) == 1);
#ifndef SERVAL_GBA
    CHECK(last_call4(2, VM_SYS_TEXT_PRINT_NUMBER, 5, 6, INT32_MIN, 12, NULL));
#endif
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Engine calls": SYS screen_set_blend(top, bottom, top_weight,
// bottom_weight) pops four and pushes nothing; it is a platform call
// (vm_internal.h), so on the host the recorder sees its arguments in push
// order. tests/rom/vm_platform_tests.c checks the registers it sets on the
// GBA. The last call turns blending off again (on the GBA, at the next
// frame_end()).
static void sys_screen_set_blend(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(55);                    // 55
    push8(LAYER_FOREGROUND);      // 55 top
    push8(LAYER_ALL);             // 55 top bottom
    push8(6);                     // 55 top bottom 6
    push8(10);                    // 55 top bottom 6 10
    sys(VM_SYS_SCREEN_SET_BLEND); // 55: frame 1
    stg(0);                       // glob[0] = 55
    wait_frames(1);               //
    push8(0);                     // 0
    push8(0);                     // 0 0
    push8(0);                     // 0 0 0
    push8(0);                     // 0 0 0 0
    sys(VM_SYS_SCREEN_SET_BLEND); // frame 2: off
    store(1, 1);                  // carried on
    op(VM_OP_HALT);               //
    CHECK(load());
#ifndef SERVAL_GBA
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
#endif
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 55);
#ifndef SERVAL_GBA
    CHECK(last_call4(1, VM_SYS_SCREEN_SET_BLEND, LAYER_FOREGROUND, LAYER_ALL, 6, 10, NULL));
#endif
    frame();
    CHECK(vm_global(1) == 1);
#ifndef SERVAL_GBA
    CHECK(last_call4(2, VM_SYS_SCREEN_SET_BLEND, 0, 0, 0, 0, NULL));
#endif
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
}

// vm.md "Engine calls": SYS path_stop(entity) is path.h's path_stop: the path
// ends where it is (a WAIT_MOVE on it resumes in the next pass). A dead or no
// entity is ignored, as path_stop does. Pops one, pushes nothing.
static void sys_path_stop(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    op(VM_OP_WAIT_MOVE); // until the path ends
    store(0, 1);         //
    op(VM_OP_HALT);      //
    handler(1, VM_EV_CREATE);
    push8(55);             // 55
    ldg(5);                // 55 e
    sys(VM_SYS_PATH_STOP); // 55
    push8(0);              // 55 0: no entity
    sys(VM_SYS_PATH_STOP); // 55
    stg(1);                // glob[1] = 55
    op(VM_OP_HALT);        //
    CHECK(load());
    vm_bind(&(VmBindings){.paths = sys_paths, .path_count = 2});
    Entity e = entity_create(C_POS | C_VEL);
    path_start(e, &sys_path_down, 0); // 10 frames
    vm_attach(e, 0);
    vm_set_global(5, e);
    u32 before = debug_warning_count();
    frame(); // e waits on its path
    sys_path();
    CHECK(path_active(e) && vm_global(0) == 0);
    start(1);
    frame(); // the thread stops the path
    CHECK(!path_active(e) && vm_global(1) == 55);
    CHECK(vm_global(0) == 0);
    vm_step(); // the wait ends
    CHECK(vm_global(0) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    vm_bind(NULL);
}

#ifndef SERVAL_GBA
// The colors the platform's latest call received: `count` of them, equal to
// `want`.
static bool call_colors(const u16* want, u32 count) {
    const Color* colors = serval_host_vm_calls.ptr;
    if (!colors)
        return false;
    for (u32 k = 0; k < count; k++)
        if (colors[k] != want[k])
            return false;
    return true;
}
#endif

// vm.md "Engine calls": SYS sprite_set_colors(sprite, index, array, count) and
// tileset_set_colors(index, array, count) read `count` elements of the array,
// ROM or RAM, from its first, each as a Color (the cell's low 16 bits), and
// pass them to the platform call (vm_internal.h) with the arguments in push
// order. They pop their arguments and push nothing. tests/rom/palette_tests.c
// checks what the GBA does with them.
static void sys_set_colors_read_their_array(void) {
#ifndef SERVAL_GBA
    static const s32 rom_colors[] = {0x001F, 0x03E0, 0x7C00, 0x7FFF};
    reset();
    blob_begin_arrays(1, 0, GLOBALS, 2);
    ram_array(1, 3, 0);
    handler(0, VM_EV_CREATE);
    push8(0);                       // i
    push32(0x18001);                // i value: its low 16 bits are the color
    sta(1);                         // ram[0]
    push8(1);                       // i
    push8(-1);                      // i -1: 0xFFFF
    sta(1);                         // ram[1]
    push8(55);                      // 55
    push16(300);                    // 55 sprite
    push8(17);                      // 55 sprite index
    push8(0);                       // 55 sprite index array (ROM)
    push8(3);                       // 55 sprite index array count
    sys(VM_SYS_SPRITE_SET_COLORS);  // 55: frame 1
    stg(0);                         // glob[0] = 55
    wait_frames(1);                 //
    push8(5);                       // index
    push8(1);                       // index array (RAM)
    push8(2);                       // index array count
    sys(VM_SYS_TILESET_SET_COLORS); // frame 2
    wait_frames(1);                 //
    push8(9);                       // index
    push8(0);                       // index array
    push8(0);                       // index array 0: no colors
    sys(VM_SYS_TILESET_SET_COLORS); // frame 3
    store(1, 1);                    // carried on
    op(VM_OP_HALT);                 //
    rom_array(0, ARRAY_U16, rom_colors, 4);
    CHECK(load());
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 55);
    const ServalHostVmCalls* r = &serval_host_vm_calls;
    CHECK(r->calls == 1 && r->fn == VM_SYS_SPRITE_SET_COLORS);
    CHECK(r->args[0] == 300 && r->args[1] == 17 && r->args[2] == 0 && r->args[3] == 3);
    CHECK(call_colors((const u16[]){0x001F, 0x03E0, 0x7C00}, 3));
    frame();
    CHECK(r->calls == 2 && r->fn == VM_SYS_TILESET_SET_COLORS);
    CHECK(r->args[0] == 5 && r->args[1] == 1 && r->args[2] == 2 && r->args[3] == 0);
    CHECK(call_colors((const u16[]){0x8001, 0xFFFF}, 2));
    frame();
    CHECK(r->calls == 3 && r->fn == VM_SYS_TILESET_SET_COLORS && r->ptr != NULL);
    CHECK(r->args[0] == 9 && r->args[1] == 0 && r->args[2] == 0);
    CHECK(vm_global(1) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
#endif
}

// vm.md "Engine calls": a palette call naming an array the blob doesn't
// have, or a count that is negative, past the array's length or past 256
// (more than any call can write), warns (once per kind) and makes no call;
// its arguments are popped and the script carries on.
static void sys_set_colors_refuse_a_bad_array_or_count(void) {
    static const s32 rom_colors[] = {0x001F, 0x03E0};
    reset();
    blob_begin_arrays(1, 0, GLOBALS, 2);
    ram_array(1, 300, 0);
    handler(0, VM_EV_CREATE);
    push8(55);                      // 55
    push8(0);                       // 55 sprite
    push8(0);                       // 55 sprite index
    push8(2);                       // 55 sprite index array: there is no array 2
    push8(1);                       // 55 sprite index array count
    sys(VM_SYS_SPRITE_SET_COLORS);  // 55
    push8(0);                       // 55 index
    push8(-1);                      // 55 index array: none either
    push8(1);                       // 55 index array count
    sys(VM_SYS_TILESET_SET_COLORS); // 55
    push8(0);                       // 55 index
    push8(0);                       // 55 index array
    push8(3);                       // 55 index array count: the array has 2
    sys(VM_SYS_TILESET_SET_COLORS); // 55
    push8(0);                       // 55 sprite
    push8(0);                       // 55 sprite index
    push8(0);                       // 55 sprite index array
    push8(-1);                      // 55 sprite index array count: negative
    sys(VM_SYS_SPRITE_SET_COLORS);  // 55
    push8(0);                       // 55 index
    push8(1);                       // 55 index array: 300 cells
    push16(257);                    // 55 index array count: past 256
    sys(VM_SYS_TILESET_SET_COLORS); // 55
    stg(0);                         // glob[0] = 55
    op(VM_OP_HALT);                 //
    rom_array(0, ARRAY_U16, rom_colors, 2);
    CHECK(load());
#ifndef SERVAL_GBA
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
#endif
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 55);
    CHECK(vm_idle());
    CHECK_WARNED(before, 2);
#ifndef SERVAL_GBA
    CHECK(serval_host_vm_calls.calls == 0);
#endif
}

// --- Arrays ------------------------------------------------------------------

// vm.md "Stack and variables" (LDA, STA, LEN), "Array table": RAM arrays are
// cells of the pool from their first cell (ranges may overlap), 0-based. An
// index outside the array warns (once, for LDA and STA alike): LDA pushes 0,
// STA drops the value. An array the blob doesn't have warns (once): LDA and
// LEN push 0, STA drops. Cells outlast the handler that wrote them.
static void ram_arrays(void) {
    reset();
    blob_begin_arrays(2, 0, GLOBALS, 2);
    ram_array(0, 4, 10); // cells 10-13
    ram_array(1, 3, 12); // cells 12-14: overlaps the last two
    handler(0, VM_EV_CREATE);
    for (s32 k = 0; k < 4; k++) {
        push8(k);      // i
        push8(10 * k); // i v
        sta(0);        // array 0 [i] = 10i
    }
    push8(0);       // array 1's 0 is array 0's 2
    lda(1);         // 20
    stg(0);         // glob[0] = 20
    len(0);         //
    stg(1);         // glob[1] = 4
    len(1);         //
    stg(2);         // glob[2] = 3
    push8(4);       // one past the end
    lda(0);         // warns: 0
    stg(3);         // glob[3] = 0
    push8(-1);      //
    lda(0);         // 0 (no repeat)
    stg(4);         // glob[4] = 0
    push8(4);       //
    push8(99);      //
    sta(0);         // dropped
    push8(55);      // 55
    push8(0);       // 55 0
    lda(2);         // no array 2: warns, 55 0
    stg(5);         // glob[5] = 0
    push8(0);       // 55 0
    push8(1);       // 55 0 1
    sta(2);         // dropped: 55
    len(2);         // 55 0
    stg(6);         // glob[6] = 0
    stg(7);         // glob[7] = 55
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    push8(3);       //
    lda(0);         // 30, from the other handler
    stg(8);         //
    push8(2);       //
    lda(1);         // cell 14: never written, 0
    stg(9);         //
    op(VM_OP_HALT); //
    CHECK(load());
    for (u16 g = 3; g <= 9; g++)
        vm_set_global(g, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    static const s32 expected[] = {20, 4, 3, 0, 0, 0, 0, 55};
    for (u16 g = 0; g < sizeof expected / sizeof expected[0]; g++)
        if (vm_global(g) != expected[g])
            test_fail(__FILE__, __LINE__, text_format("glob[%u] is %d", g, vm_global(g)));
    CHECK_WARNED(before, 2);
    start(1);
    vm_step();
    CHECK(vm_global(8) == 30 && vm_global(9) == 0);
    CHECK(vm_idle());
    CHECK_WARNED(before, 2);
}

// The ROM arrays' elements, one array per kind, in the narrowest kind's
// extremes.
static const s32 rom_s8[] = {-128, 127, -1};
static const s32 rom_u8[] = {0, 255, 128};
static const s32 rom_s16[] = {-32768, 32767, -2};
static const s32 rom_u16[] = {65535, 0, 32768};
static const s32 rom_s32[] = {INT32_MIN, INT32_MAX, -3};

// vm.md "Array table": ROM arrays are constant data read in place, in each
// kind (s8 and s16 sign-extended, u8 and u16 zero-extended). STA to one warns
// and writes nothing; an index outside it warns and reads 0. The last array
// ends the blob, so reading its last element reads the blob's last bytes and
// no further (ASan, on the host).
static void rom_arrays_of_every_kind(void) {
    static const s32* const values[] = {rom_s8, rom_u8, rom_s16, rom_u16, rom_s32};
    static const u8 kinds[] = {ARRAY_S8, ARRAY_U8, ARRAY_S16, ARRAY_U16, ARRAY_S32};
    reset();
    blob_begin_arrays(1, 0, GLOBALS, 5);
    for (u32 n = 0; n < 4; n++)
        rom_array(n, kinds[n], values[n], 3);
    handler(0, VM_EV_CREATE);
    for (u32 n = 0; n < 5; n++) {
        for (s32 i = 0; i < 3; i++) {
            push8(i);            // i
            lda(n);              // element i
            stg(3 * n + (u32)i); // glob[3n + i]
        }
        len(n);      //
        stg(16 + n); // glob[16 + n] = 3
    }
    push8(1);                            // i
    push8(7);                            // i 7
    sta(0);                              // a ROM array: warns, nothing written
    push8(1);                            //
    lda(0);                              // still 127
    stg(21);                             //
    push8(3);                            // one past the end
    lda(4);                              // warns: 0
    stg(22);                             //
    op(VM_OP_HALT);                      //
    rom_array(4, ARRAY_S32, rom_s32, 3); // the blob's last 12 bytes
    CHECK(load());
    vm_set_global(22, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    u32 wrong = 0;
    for (u32 n = 0; n < 5; n++) {
        for (u32 i = 0; i < 3; i++)
            wrong += vm_global((u16)(3 * n + i)) != values[n][i];
        wrong += vm_global((u16)(16 + n)) != 3;
    }
    CHECK(wrong == 0);
    CHECK(vm_global(21) == 127 && vm_global(22) == 0);
    CHECK(vm_idle());
    CHECK_WARNED(before, 2);
}

// A blob for the array reload cases: object 0's Create adds 1 to cell 0 of
// its RAM array and copies it to glob[0]. `length` and `first` lay the RAM
// array out; `rom` puts a ROM array of that many elements before it (array 0,
// making the RAM array number 1), or after it with `rom_after`.
static void build_counter_array_with(u16 length, u16 first, u16 rom, bool rom_after) {
    static const s32 data[] = {1, 2, 3};
    u32 n = rom && !rom_after ? 1 : 0;
    blob_begin_arrays(1, 0, GLOBALS, rom ? 2 : 1);
    if (rom)
        rom_array(1 - n, ARRAY_U8, data, rom);
    ram_array(n, length, first);
    handler(0, VM_EV_CREATE);
    push8(0);       // 0
    push8(0);       // 0 0
    lda(n);         // 0 a[0]
    push8(1);       //
    op(VM_OP_ADD);  // 0 a[0]+1
    sta(n);         // a[0] += 1
    push8(0);       //
    lda(n);         // a[0]
    stg(0);         // glob[0]
    op(VM_OP_HALT); //
}

static void build_counter_array(u16 length, u16 first, u16 rom) {
    build_counter_array_with(length, first, rom, false);
}

// Runs object 0's Create `times` times; returns glob[0], the counter.
static s32 count_up(u32 times) {
    for (u32 k = 0; k < times; k++)
        start(0);
    vm_step();
    return vm_global(0);
}

// vm.md "Array table", "Hot reload": vm_load zeroes the RAM arrays' cells;
// vm_reload keeps them when the new blob's RAM arrays are laid out the same
// (the array count, each array's kind, each RAM array's length and first
// cell; ROM data may change), and zeroes them with a warning otherwise.
static void ram_arrays_across_loads(void) {
    reset();
    build_counter_array(4, 8, 0);
    CHECK(load());
    u32 before = debug_warning_count();
    CHECK(count_up(2) == 2);
    build_counter_array(4, 8, 0);
    CHECK(reload()); // the same layout: kept
    CHECK(count_up(1) == 3);
    CHECK_WARNED(before, 0);
    build_counter_array(5, 8, 0); // longer: zeroed
    CHECK(reload());
    CHECK_WARNED(before, 1);
    CHECK(count_up(1) == 1);
    build_counter_array(5, 9, 0); // moved: zeroed
    CHECK(reload());
    CHECK_WARNED(before, 2);
    CHECK(count_up(2) == 2);
    build_counter_array(5, 9, 2); // a ROM array before it: zeroed
    CHECK(reload());
    CHECK_WARNED(before, 3);
    CHECK(count_up(1) == 1);
    build_counter_array(5, 9, 3); // only the ROM array changed: kept
    CHECK(reload());
    CHECK(count_up(1) == 2);
    CHECK_WARNED(before, 3);
    build_counter_array_with(5, 9, 3, true); // as many arrays, but array 0 is RAM now
    CHECK(reload());
    CHECK_WARNED(before, 4);
    CHECK(count_up(1) == 1);
    build_counter_array_with(5, 9, 3, true);
    CHECK(load()); // vm_load: zeroed, no warning
    CHECK(count_up(1) == 1);
    CHECK_WARNED(before, 4);
}

// --- Hot reload --------------------------------------------------------------

// A blob for the reload cases: the Step handler of object 0 appends
// step_digit to glob[0]; object 1's (if any) appends 2 to glob[1].
static void build_steppers(u16 globals, s32 step_digit, u16 objects) {
    blob_begin(objects, 0, globals);
    handler(0, VM_EV_STEP);
    append(0, step_digit);
    op(VM_OP_HALT);
    if (objects > 1) {
        handler(1, VM_EV_STEP);
        append(1, 2);
        op(VM_OP_HALT);
    }
}

// vm.md "Hot reload": vm_reload keeps the globals when the global count
// matches, else zeroes them and warns.
static void reload_keeps_globals_if_their_count_matches(void) {
    reset();
    build_steppers(4, 1, 2);
    CHECK(load());
    for (u16 g = 0; g < 4; g++)
        vm_set_global(g, 10 * (g + 1));
    u32 before = debug_warning_count();
    build_steppers(4, 3, 2);
    CHECK(reload());
    CHECK(vm_global(0) == 10 && vm_global(1) == 20 && vm_global(2) == 30 && vm_global(3) == 40);
    CHECK_WARNED(before, 0);
    build_steppers(5, 3, 2);
    CHECK(reload());
    u32 nonzero = 0;
    for (u16 g = 0; g < 5; g++)
        nonzero += vm_global(g) != 0;
    CHECK(nonzero == 0);
    CHECK_WARNED(before, 1);
}

// Object 0's Create adds 1 to glob[0]; the blob starts its globals at
// `values` (header flag bit 0), or at 0 when values is NULL.
static void build_initial_values(const s32* values, u16 globals) {
    if (values)
        blob_begin_values(1, 0, 0, values, globals);
    else
        blob_begin(1, 0, globals);
    handler(0, VM_EV_CREATE);
    count(0);
    op(VM_OP_HALT);
}

// vm.md "Array table": with header flag bit 0, vm_load starts the globals at
// the blob's initial values (those past its global count at 0), so a script's
// first run sees them; vm_reload keeps the old values when the global count
// matches, and otherwise starts them again at the new blob's initial values,
// with a warning. Without the flag, globals start at 0.
static void globals_start_at_their_initial_values(void) {
    static const s32 values[] = {5, -1, INT32_MIN, 0, INT32_MAX};
    static const s32 others[] = {1, 2, 3};
    reset();
    vm_set_global(7, 9);
    build_initial_values(values, 5);
    CHECK(load());
    CHECK(vm_global(0) == 5 && vm_global(1) == -1 && vm_global(2) == INT32_MIN);
    CHECK(vm_global(3) == 0 && vm_global(4) == INT32_MAX && vm_global(7) == 0);
    start(0);
    vm_step();
    CHECK(vm_global(0) == 6);
    u32 before = debug_warning_count();
    build_initial_values(values, 5);
    CHECK(reload()); // the same count: the values are kept
    CHECK(vm_global(0) == 6 && vm_global(4) == INT32_MAX);
    CHECK(load()); // vm_load starts them again
    CHECK(vm_global(0) == 5);
    CHECK_WARNED(before, 0);
    vm_set_global(0, 50);
    vm_set_global(4, 50);
    build_initial_values(others, 3);
    CHECK(reload()); // another count: the new blob's values, and a warning
    CHECK_WARNED(before, 1);
    CHECK(vm_global(0) == 1 && vm_global(1) == 2 && vm_global(2) == 3);
    CHECK(vm_global(3) == 0 && vm_global(4) == 0);
    build_initial_values(NULL, 3);
    CHECK(load()); // no initial values: 0
    CHECK(vm_global(0) == 0 && vm_global(1) == 0 && vm_global(2) == 0);
    start(0);
    vm_step();
    CHECK(vm_global(0) == 1);
}

// vm.md "Load-time validation": the initial values are part of the tables:
// they must fit in the blob, and handlers, strings and ROM data must lie past
// them. With no globals the table is empty. One warning per rejected blob.
static void loader_checks_initial_values(void) {
    static const s32 values[] = {1, 2};
    static const s32 data[] = {7};
    enum { HANDLER = 0x18, STRING = 0x30, ROM_AT = 0x38, CODE = 0x44 };
    reset();
    u32 before = debug_warning_count();
    // The tables: 0x10 header, 0x20 object, 4 string, 8 array, 8 initial values.
    blob_begin_values(1, 1, 1, values, 2);
    CHECK(bld.size == CODE);
    handler(0, VM_EV_CREATE);
    op(VM_OP_HALT);
    rom_array(0, ARRAY_S8, data, 1);
    string(0, "S");
    CHECK(load());
    CHECK(vm_global(0) == 1 && vm_global(1) == 2);
    put32(HANDLER, CODE - 1); // the handler in the values
    CHECK(!load());
    put32(HANDLER, CODE);
    put32(STRING, CODE - 4); // the string in the values
    CHECK(!load());
    put32(STRING, CODE + 2);
    put32(ROM_AT, CODE - 1); // the ROM data in the values
    CHECK(!load());
    put32(ROM_AT, CODE + 1);
    CHECK(load());
    u32 size = bld.size;
    bld.size = CODE - 1; // the values cut off by the blob's end
    CHECK(!load());
    bld.size = size;
    CHECK(load());
    CHECK_WARNED(before, 4);
    blob_begin_values(1, 0, 0, values, 0); // the flag with no globals
    handler(0, VM_EV_CREATE);
    op(VM_OP_HALT);
    CHECK(load());
    CHECK_WARNED(before, 4);
    vm_unload();
}

// vm.h: vm_reload keeps entities attached to objects the new blob still has
// (running its code); others are no longer attached (but stay alive). The
// emptied queue loses a kept entity's pending Create, so its Step handler
// runs without it (vm.md "Exact semantics: Step handlers").
static void reload_keeps_attachments_to_objects_that_remain(void) {
    reset();
    build_steppers(4, 1, 2);
    CHECK(load());
    Entity e0 = entity_create(C_POS);
    Entity e1 = entity_create(C_POS);
    vm_attach(e0, 0);
    vm_attach(e1, 1);
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 1 && vm_global(1) == 2);
    vm_events();
    Entity e2 = entity_create(C_POS);
    vm_attach(e2, 0);        // its Create is still queued at the reload
    build_steppers(4, 3, 1); // object 1 is gone; object 0's Step appends 3 now
    CHECK(reload());
    vm_step();
    CHECK(vm_global(0) == 133); // e0, then e2: no Create to wait for any more
    CHECK(vm_global(1) == 2);
    CHECK(entity_alive(e0) && entity_alive(e1) && entity_alive(e2));
}

// Objects whose Create sets the instance's field 3 to 7; object 0's Room
// Start, run as a thread, reads that field of the entities in glob[5] and
// glob[6] into glob[0] and glob[1].
static void build_fielders(u16 objects) {
    blob_begin(objects, 0, GLOBALS);
    for (u16 obj = 0; obj < objects; obj++) {
        handler(obj, VM_EV_CREATE);
        op(VM_OP_SELF);
        push8(7);
        setp(VM_P_FIELD(3));
        op(VM_OP_HALT);
    }
    handler(0, VM_EV_ROOM_START);
    ldg(5);
    getp(VM_P_FIELD(3));
    stg(0);
    ldg(6);
    getp(VM_P_FIELD(3));
    stg(1);
    op(VM_OP_HALT);
}

// vm.md "Hot reload": instance fields are kept for the entities the reload
// keeps attached; an entity the reload detaches has none any more.
static void reload_keeps_the_kept_instances_fields(void) {
    reset();
    build_fielders(2);
    CHECK(load());
    Entity a = entity_create(C_POS);
    Entity b = entity_create(C_POS);
    vm_attach(a, 0);
    vm_attach(b, 1);
    vm_events();
    vm_set_global(5, a);
    vm_set_global(6, b);
    build_fielders(1); // object 1 is gone: b is detached
    CHECK(reload());
    vm_set_global(1, 99);
    u32 before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_ROOM_START) >= 0);
    vm_step();
    CHECK(vm_global(0) == 7); // a's, kept
    CHECK(vm_global(1) == 0); // b is unattached: warns
    CHECK_WARNED(before, 1);
}

// A Collision handler for object 0 (glob[1] = 1) and a thread, object 1,
// that sets glob[0] to 1, waits a frame, then sets it to 2.
static void build_collider_and_waiter(void) {
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_COLLISION);
    store(1, 1);
    op(VM_OP_HALT);
    handler(1, VM_EV_CREATE);
    store(0, 1);
    wait_frames(1);
    store(0, 2);
    op(VM_OP_HALT);
}

// vm.h: vm_reload halts every context and empties the event queue.
static void reload_halts_contexts_and_empties_the_queue(void) {
    reset();
    build_collider_and_waiter();
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    start(1);
    vm_step();
    CHECK(vm_global(0) == 1);                  // the thread waits
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION); // queued
    build_collider_and_waiter();
    CHECK(reload()); // the same program
    CHECK(vm_idle());
    frames(2);
    CHECK(vm_global(0) == 1 && vm_global(1) == 0);
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION); // e is still attached
    vm_events();
    CHECK(vm_global(1) == 1);
}

// Object 0 counts its Step runs in glob[0]; object 1 is a thread counting
// frames in glob[1] forever.
static void build_counters(void) {
    enum { L_AGAIN };
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_STEP);
    count(0);
    op(VM_OP_HALT);
    handler(1, VM_EV_CREATE);
    label(L_AGAIN);
    count(1);
    wait_frames(1);
    jump(VM_OP_JMP, L_AGAIN);
}

// vm.md "Hot reload", vm.h: vm_load halts every context, detaches every
// entity, empties the queue and zeroes the globals.
static void load_resets_everything(void) {
    reset();
    build_counters();
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    start(1);
    vm_step();
    CHECK(vm_global(0) == 1 && vm_global(1) == 1);
    vm_events();
    vm_set_global(5, 77);
    vm_attach(entity_create(C_POS), 0); // leaves a Create queued
    build_counters();
    CHECK(load()); // the same program again
    CHECK(vm_global(0) == 0 && vm_global(1) == 0 && vm_global(5) == 0);
    CHECK(vm_idle());
    frames(2);
    CHECK(vm_global(0) == 0 && vm_global(1) == 0); // e detached, the thread gone
}

#ifndef SERVAL_GBA
static u32 loads_refused;

// Stands in for game C code run during a phase (none can be, in v1).
static void load_during_the_phase(void) {
    loads_refused += !vm_load(placed, placed_size);
    loads_refused += !vm_reload(placed, placed_size);
    vm_unload();
}
#endif

// vm.md "Exact semantics: Loading during a phase": vm_load, vm_reload and
// vm_unload called from inside vm_step or vm_events warn (once: one kind of
// problem), do nothing and return false. Scripts can't reach them in v1; on
// the host, the platform-call recorder's hook runs C code in the middle of a
// script's SYS call.
static void loading_during_a_phase_is_refused(void) {
#ifndef SERVAL_GBA
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);          //
    push8(5);             // sound 5
    sys(VM_SYS_PSG_PLAY); // the hook tries to load, reload and unload
    store(0, 2);          // the same blob runs on
    wait_frames(1);       // still waiting after the hook's vm_unload
    store(0, 3);          //
    op(VM_OP_HALT);       //
    CHECK(load());
    vm_set_global(5, 77); // vm_load would zero it
    start(0);
    serval_host_vm_calls = (ServalHostVmCalls){.during = load_during_the_phase};
    loads_refused = 0;
    u32 before = debug_warning_count();
    vm_step();
    serval_host_vm_calls.during = NULL;
    CHECK(loads_refused == 2);
    CHECK(vm_global(0) == 2 && vm_global(5) == 77);
    CHECK(!vm_idle());
    CHECK_WARNED(before, 1);
    vm_events();
    vm_step();
    CHECK(vm_global(0) == 3);
    CHECK(vm_load(placed, placed_size)); // between frames: fine
    CHECK(vm_global(5) == 0);
    CHECK_WARNED(before, 1);
#endif
}

// vm.md "Exact semantics: vm_kill": called outside the phases, vm_kill runs
// the Destroy reaction at once; while it runs, loading is refused just as in
// a phase (the blob would be pulled from under it), with one warning.
static void loading_during_vm_kill_is_refused(void) {
#ifndef SERVAL_GBA
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_DESTROY);
    store(0, 1);          //
    push8(5);             // sound 5
    sys(VM_SYS_PSG_PLAY); // the hook tries to load, reload and unload
    store(0, 2);          // the same blob runs on
    op(VM_OP_HALT);       //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    vm_set_global(5, 77); // vm_load would zero it
    serval_host_vm_calls = (ServalHostVmCalls){.during = load_during_the_phase};
    loads_refused = 0;
    u32 before = debug_warning_count();
    vm_kill(e);
    serval_host_vm_calls.during = NULL;
    CHECK(loads_refused == 2);
    CHECK(vm_global(0) == 2 && vm_global(5) == 77);
    CHECK(!entity_alive(e) && vm_idle());
    CHECK_WARNED(before, 1);
    CHECK(vm_load(placed, placed_size)); // afterwards: fine
    CHECK_WARNED(before, 1);
#endif
}

#ifndef SERVAL_GBA
// Stands in for game C code run during a phase that starts another (none can
// in v1).
static void phase_during_the_phase(void) {
    vm_events();
    vm_step();
}
#endif

// vm.md "Exact semantics: Reactions on top of a behaviour" saves one
// behaviour per context, which is enough because events are never dispatched
// while a script runs. So vm_step and vm_events called from inside a phase
// (by C code a script reached, as the recorder's hook does here on the host)
// warn once and do nothing: the second Collision, queued while the first one
// runs on top of the entity's behaviour, runs after it, not on top of it. The
// behaviour beneath is untouched and still resumes.
static void phases_do_not_nest(void) {
#ifndef SERVAL_GBA
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    enter(0, 2);    // two cells on its stack
    wait_frames(2); //
    store(0, 1);    // resumes
    op(VM_OP_HALT); //
    handler(0, VM_EV_COLLISION);
    count(1);             // glob[1]: Collisions begun
    push8(5);             // sound 5
    sys(VM_SYS_PSG_PLAY); // the hook tries to run a phase
    count(2);             // glob[2]: Collisions ended
    op(VM_OP_HALT);       //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // the behaviour waits
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    serval_host_vm_calls = (ServalHostVmCalls){.during = phase_during_the_phase};
    u32 before = debug_warning_count();
    vm_events();
    serval_host_vm_calls.during = NULL;
    CHECK(vm_global(1) == 2 && vm_global(2) == 2);
    CHECK(serval_host_vm_calls.calls == 2);
    CHECK_WARNED(before, 1);
    frames(2);
    CHECK(vm_global(0) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
#endif
}

// --- Loader ------------------------------------------------------------------

typedef struct {
    u32 at, value, bytes; // replaces golden[at .. at + bytes), little-endian
    const char* what;
} Patch;

static SERVAL_EWRAM_BSS u8 patched[sizeof golden];

static const u8* golden_with(const Patch* patch) {
    for (u32 k = 0; k < sizeof golden; k++)
        patched[k] = golden[k];
    for (u32 k = 0; k < patch->bytes; k++)
        patched[patch->at + k] = (u8)(patch->value >> (8 * k));
    return patched;
}

// vm.md "Load-time validation". The golden blob's tables end at 0x34; its
// Create handler offset is at 0x18, Room Start's at 0x2C, string 0's at 0x30;
// the header's flags are at 6, its array count at 14, object 0's reserved
// field at 0x16.
static const Patch bad_patches[] = {
    {0, 'X', 1, "bad magic"},
    {6, 2, 2, "header flag bit 1"},
    {6, 3, 2, "header flag bits 0 and 1"},
    {6, 0x8000, 2, "header flags not 0 (top bit)"},
    {6, 1, 2, "initial values (flag bit 0) over the string and the code"},
    {0x16, 1, 2, "object reserved field not 0"},
    {14, 1, 2, "an array table over the code and the string"},
    {14, 0xFFFF, 2, "array table past the end"},
    {3, 'b', 1, "bad magic (last byte)"},
    {4, 0, 1, "version 0"},
    {4, 2, 1, "version 2"},
    {5, 2, 1, "2-byte cells"},
    {5, 8, 1, "8-byte cells"},
    {8, 2, 2, "object table past the end"},
    {8, 0xFFFF, 2, "65535 objects"},
    {10, 4, 2, "string table past the end"},
    {10, 0xFFFF, 2, "65535 strings"},
    {12, VM_GLOBALS + 1, 2, "more globals than VM_GLOBALS"},
    {0x18, sizeof golden, 4, "handler at the blob's size"},
    {0x18, 0x1000, 4, "handler past the end"},
    {0x18, 0xFFFFFFFF, 4, "handler at 2^32 - 1"},
    {0x18, 0x08, 4, "handler in the header"},
    {0x18, 0x10, 4, "handler in the object table"},
    {0x18, 0x33, 4, "handler in the string table"},
    {0x2C, sizeof golden, 4, "Room Start handler past the end"},
    {0x2C, 0x20, 4, "Room Start handler in the object table"},
    {0x30, sizeof golden, 4, "string at the blob's size"},
    {0x30, 0xFFFFFFFF, 4, "string at 2^32 - 1"},
    {0x30, 0x10, 4, "string in the object table"},
    {0x30, 0x30, 4, "string in the string table"},
    {0x30, 0x3E, 4, "string with no NUL before the end"},
};

static const Patch good_patches[] = {
    {12, VM_GLOBALS, 2, "VM_GLOBALS globals rejected"},
    {0x18, 0x34, 4, "handler right after the tables rejected"},
    {0x18, sizeof golden - 1, 4, "handler at the last byte rejected"},
    {0x2C, 0x37, 4, "Room Start handler rejected"},
    {0x30, 0x37, 4, "string past the tables rejected"},
    {0x30, 0x3D, 4, "empty string at the blob's last NUL rejected"},
};

// vm.md "Load-time validation": vm_load returns false and warns (once per
// call) for a bad header, tables past the end, globals over VM_GLOBALS,
// handler or string offsets outside the blob or inside its tables, and a
// string whose NUL isn't in the blob; offsets anywhere past the tables are
// fine.
static void loader_rejects_bad_blobs(void) {
    reset();
    u32 before = debug_warning_count();
    u32 rejected = 0;
    CHECK(!vm_load(NULL, sizeof golden));
    CHECK(!vm_load(golden, 0));
    CHECK(!vm_load(golden, VM_HEADER_SIZE - 1));
    CHECK(!vm_load(golden, 40)); // a whole header, cut in its object table
    rejected += 4;
    for (u32 k = 0; k < sizeof bad_patches / sizeof bad_patches[0]; k++) {
        if (vm_load(golden_with(&bad_patches[k]), sizeof golden)) {
            test_fail(__FILE__, __LINE__, bad_patches[k].what);
            vm_unload();
        }
        rejected++;
    }
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() - before == rejected);
#else
    (void)rejected;
    CHECK(debug_warning_count() == before);
#endif
    before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_CREATE) == -1); // nothing loaded: nothing runs
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
    before = debug_warning_count();
    for (u32 k = 0; k < sizeof good_patches / sizeof good_patches[0]; k++) {
        if (!vm_load(golden_with(&good_patches[k]), sizeof golden))
            test_fail(__FILE__, __LINE__, good_patches[k].what);
        vm_unload();
    }
    CHECK_WARNED(before, 0);
}

// vm.md "Load-time validation": every string's terminating NUL must be inside
// the blob. The last string cut off before its NUL (the blob ends at the end
// of its buffer, so ASan would catch a read past it) is rejected; the same
// blob with its NUL loads.
static void loader_rejects_a_string_without_its_nul(void) {
    reset();
    blob_begin(1, 2, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);
    op(VM_OP_HALT);
    string(0, "FINE");
    string(1, "CUT"); // the blob's last bytes
    CHECK(load());
    vm_unload();
    u32 before = debug_warning_count();
    bld.size--; // without the NUL
    CHECK(!load());
    CHECK_WARNED(before, 1);
    CHECK(vm_start(0, VM_EV_CREATE) == -1); // nothing loaded
}

// One array record, and whether vm_load must accept it, in a blob whose
// tables end at 0x38 and whose last byte is at 0x3F (size 0x40).
static const struct {
    u32 length, kind, reserved, where;
    bool good;
    const char* what;
} array_cases[] = {
    {4, ARRAY_RAM, 0, 0, true, "a RAM array at cell 0"},
    {24, ARRAY_RAM, 0, VM_ARRAY_CELLS - 24, true, "a RAM array ending at the pool's end"},
    {0, ARRAY_RAM, 0, VM_ARRAY_CELLS, true, "an empty RAM array at the pool's end"},
    {VM_ARRAY_CELLS, ARRAY_RAM, 0, 0, true, "the whole pool"},
    {25, ARRAY_RAM, 0, VM_ARRAY_CELLS - 24, false, "a RAM array past the pool's end"},
    {0, ARRAY_RAM, 0, VM_ARRAY_CELLS + 1, false, "an empty RAM array past the pool"},
    {1, ARRAY_RAM, 0, 0xFFFFFFFF, false, "a RAM array at cell 2^32 - 1"},
    {0xFFFF, ARRAY_RAM, 0, 0, false, "a RAM array longer than the pool"},
    {8, ARRAY_U8, 0, 0x38, true, "u8 data filling the rest of the blob"},
    {2, ARRAY_S32, 0, 0x38, true, "s32 data filling the rest of the blob"},
    {4, ARRAY_S16, 0, 0x38, true, "s16 data filling the rest of the blob"},
    {0, ARRAY_S8, 0, 0x40, true, "empty data at the blob's end"},
    {9, ARRAY_U8, 0, 0x38, false, "u8 data one byte past the end"},
    {3, ARRAY_S32, 0, 0x38, false, "s32 data past the end"},
    {5, ARRAY_U16, 0, 0x38, false, "u16 data past the end"},
    {1, ARRAY_S8, 0, 0x37, false, "data in the array table"},
    {1, ARRAY_S8, 0, 0x10, false, "data in the object table"},
    {0, ARRAY_S8, 0, 0x41, false, "empty data past the blob's end"},
    {1, ARRAY_S8, 0, 0xFFFFFFFF, false, "data at 2^32 - 1"},
    {0xFFFF, ARRAY_S32, 0, 0x38, false, "65535 s32s"},
    {1, ARRAY_S32 + 1, 0, 0x38, false, "an unknown kind"},
    {1, 255, 0, 0x38, false, "kind 255"},
    {1, ARRAY_RAM, 1, 0, false, "a RAM array's reserved byte not 0"},
    {1, ARRAY_U8, 0x80, 0x38, false, "a ROM array's reserved byte not 0"},
};

// vm.md "Load-time validation": every array record must be valid: a known
// kind, its reserved byte 0, a RAM range inside VM_ARRAY_CELLS, ROM data
// inside the blob past the tables. One warning per rejected blob.
static void loader_checks_array_records(void) {
    reset();
    u32 before = debug_warning_count();
    u32 rejected = 0;
    for (u32 k = 0; k < sizeof array_cases / sizeof array_cases[0]; k++) {
        blob_begin_arrays(1, 0, GLOBALS, 1); // tables: 0x10 + 0x20 + 8 = 0x38
        handler(0, VM_EV_CREATE);
        for (u32 b = 0; b < 7; b++)
            op(VM_OP_NOP);
        op(VM_OP_HALT); // 0x38 to 0x3F
        put16(array_record(0), array_cases[k].length);
        bld.bytes[array_record(0) + 2] = (u8)array_cases[k].kind;
        bld.bytes[array_record(0) + 3] = (u8)array_cases[k].reserved;
        put32(array_record(0) + 4, array_cases[k].where);
        if (load() != array_cases[k].good)
            test_fail(__FILE__, __LINE__, array_cases[k].what);
        rejected += !array_cases[k].good;
        vm_unload();
    }
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() - before == rejected);
#else
    (void)rejected;
    CHECK(debug_warning_count() == before);
#endif
}

// --- Collisions (vm_collide) ---------------------------------------------------

// The sets of the collision tests, by game component.
#define TAG_A C_GAME(0)
#define TAG_B C_GAME(1)
#define TAG_C C_GAME(2)
#define TAG_D C_GAME(3)

// Game component n, for a run-time n (C_GAME checks a constant one).
static u32 game_tag(u32 n) {
    return 1u << (16 + n);
}

// The log of Collision reactions: array 0, its length in glob[LOG]. An entry
// is self * 100 + other, each entity named by its spr_depth.
enum { LOG = 0, LOG_CELLS = 64 };

static void collide_blob_begin(u16 objects) {
    blob_begin_arrays(objects, 0, GLOBALS, 1);
    ram_array(0, LOG_CELLS, 0);
}

// Starts obj's Collision handler with code that logs the event; the caller
// adds the rest (at least a HALT).
static void log_collision(u16 obj) {
    handler(obj, VM_EV_COLLISION);
    ldg(LOG);         // n
    op(VM_OP_SELF);   // n self
    getp(VM_P_DEPTH); // n id
    push8(100);       // n id 100
    op(VM_OP_MUL);    // n id*100
    op(VM_OP_OTHER);  // n id*100 other
    getp(VM_P_DEPTH); // n id*100 other_id
    op(VM_OP_ADD);    // n entry
    sta(0);           // log[n] = entry
    count(LOG);       // n + 1
}

// An entity with the components `tags`, a w x h body at (x, y) in whole
// pixels, named `id` (its spr_depth) in the log.
static Entity collider(u32 tags, s32 x, s32 y, u32 w, u32 h, s16 id) {
    Entity e = entity_create(C_POS | C_SPR | C_BODY | tags);
    u32 i = entity_index(e);
    pos_x[i] = FX(x);
    pos_y[i] = FX(y);
    body_w[i] = (u8)w;
    body_h[i] = (u8)h;
    spr_depth[i] = id;
    return e;
}

// True if the log holds exactly these n entries, in order.
static bool logged(const s32* entries, u32 n) {
    if (vm_global(LOG) != (s32)n)
        return false;
    for (u32 k = 0; k < n; k++)
        if (serval_vm_array_cell(k) != entries[k])
            return false;
    return true;
}

// vm.h vm_collide: vm_events() (not vm_step) tests the pair, and two
// entities whose bodies overlap (body_overlap) both get a Collision event,
// with the other as OTHER: a's first, then b's. They collide again on every
// frame they overlap, and not once apart. Touching edges don't count; a
// SPRITE_SCREEN entity is compared with a world one in the world (the camera
// added). No warnings.
static void collide_raises_collisions_on_both_sides(void) {
    reset();
    map_unload(2); // no playfield: the camera isn't clamped
    camera_set(0, 0);
    collide_blob_begin(2);
    log_collision(0);
    op(VM_OP_HALT);
    log_collision(1);
    op(VM_OP_HALT);
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    Entity a = collider(TAG_A, 10, 10, 8, 8, 1);
    Entity b = collider(TAG_B, 14, 14, 8, 8, 2);    // overlaps a
    Entity edge = collider(TAG_B, 18, 10, 8, 8, 3); // touches a's right edge
    vm_attach(a, 0);
    vm_attach(b, 1);
    vm_attach(edge, 1);
    u32 before = debug_warning_count();
    vm_step(); // the Creates; no collision in this phase
    CHECK(vm_global(LOG) == 0);
    vm_events();
    static const s32 once[] = {102, 201};
    CHECK(logged(once, 2));
    frame(); // still overlapping: again
    static const s32 twice[] = {102, 201, 102, 201};
    CHECK(logged(twice, 4));
    u32 bi = entity_index(b);
    pos_x[bi] = FX(30); // apart
    frame();
    CHECK(logged(twice, 4));
    // On the screen at x -86 with the camera at x 100: x 14 in the world.
    spr_flags[bi] |= SPRITE_SCREEN;
    pos_x[bi] = FX(14 - 100);
    camera_set(100, 0);
    vm_set_global(LOG, 0);
    frame();
    CHECK(logged(once, 2));
    camera_set(0, 0); // now at x -86 in the world as well: apart
    frame();
    CHECK(logged(once, 2));
    CHECK_WARNED(before, 0);
}

// vm.h vm_collide: the sets hold every live entity with their components,
// attached or not. Only an entity attached to an object with a Collision
// handler gets the event; an unattached one can still be OTHER.
static void collide_events_go_to_collision_handlers_only(void) {
    reset();
    collide_blob_begin(2);
    log_collision(0);
    op(VM_OP_HALT);
    handler(1, VM_EV_CREATE); // object 1: no Collision handler
    op(VM_OP_HALT);
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    Entity a = collider(TAG_A, 0, 0, 8, 8, 1);
    collider(TAG_B, 4, 4, 8, 8, 2); // unattached, overlapping a
    Entity c = collider(TAG_A, 50, 0, 8, 8, 3);
    Entity d = collider(TAG_B, 54, 4, 8, 8, 4); // overlapping c
    vm_attach(a, 0);
    vm_attach(c, 1);
    vm_attach(d, 0);
    u32 before = debug_warning_count();
    frame();
    static const s32 expected[] = {102, 403};
    CHECK(logged(expected, 2));
    CHECK_WARNED(before, 0);
}

// vm.h vm_collide "Order": the pairs in the order they were set; in a pair,
// the entities of a in slot order, each against those of b in slot order.
static void collide_order_is_pairs_then_slots(void) {
    reset();
    collide_blob_begin(1);
    log_collision(0);
    op(VM_OP_HALT);
    CHECK(load());
    CHECK(vm_collide(TAG_C, TAG_D));
    CHECK(vm_collide(TAG_A, TAG_B));
    // All on the same spot, created in slot order.
    Entity e[6] = {collider(TAG_A, 0, 0, 8, 8, 1), collider(TAG_B, 1, 1, 8, 8, 2),
                   collider(TAG_A, 2, 2, 8, 8, 3), collider(TAG_B, 3, 3, 8, 8, 4),
                   collider(TAG_C, 4, 4, 8, 8, 5), collider(TAG_D, 5, 5, 8, 8, 6)};
    for (u32 k = 0; k < 6; k++)
        vm_attach(e[k], 0);
    CHECK(entity_index(e[0]) < entity_index(e[1]) && entity_index(e[1]) < entity_index(e[2]));
    CHECK(entity_index(e[2]) < entity_index(e[3]));
    frame();
    static const s32 expected[] = {506, 605, 102, 201, 104, 401, 302, 203, 304, 403};
    CHECK(logged(expected, 10));
}

// vm.h vm_collide: an entity is never paired with itself; two entities in
// both sets are tested once, with the lower slot as a; a pair set again
// (either way round) changes nothing.
static void collide_tests_each_pair_of_entities_once(void) {
    reset();
    collide_blob_begin(1);
    log_collision(0);
    op(VM_OP_HALT);
    CHECK(load());
    // One set against itself.
    CHECK(vm_collide(TAG_A, TAG_A));
    CHECK(vm_collide(TAG_A, TAG_A)); // set already: nothing changes
    Entity e[3] = {collider(TAG_A, 0, 0, 8, 8, 1), collider(TAG_A, 2, 2, 8, 8, 2),
                   collider(TAG_A, 4, 4, 8, 8, 3)};
    for (u32 k = 0; k < 3; k++)
        vm_attach(e[k], 0);
    u32 before = debug_warning_count();
    frame();
    static const s32 same[] = {102, 201, 103, 301, 203, 302};
    CHECK(logged(same, 6));

    // Sets that share entities: p and q are in both.
    reset();
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    CHECK(vm_collide(TAG_B, TAG_A)); // the same pair the other way round
    Entity p = collider(TAG_A | TAG_B, 0, 0, 8, 8, 1);
    Entity q = collider(TAG_A | TAG_B, 2, 2, 8, 8, 2);
    Entity r = collider(TAG_B, 4, 4, 8, 8, 3);
    vm_attach(p, 0);
    vm_attach(q, 0);
    vm_attach(r, 0);
    frame();
    static const s32 shared[] = {102, 201, 103, 301, 203, 302};
    CHECK(logged(shared, 6));
    CHECK_WARNED(before, 0);
}

// vm.h vm_collide: an overlap's reactions run (and the queue is drained)
// before the next test, so later tests see what they did. A shot that kills
// itself on the first enemy doesn't reach the second, and its Destroy has run
// by the end of vm_events(); an entity that takes itself out of the set by
// its tags (VM_P_TAGS) isn't tested as a member of it again.
static void collide_reactions_run_before_the_next_test(void) {
    reset();
    collide_blob_begin(4);
    log_collision(0); // the shot: logs, then kills itself
    op(VM_OP_SELF);
    op(VM_OP_KILL);
    op(VM_OP_HALT);
    handler(0, VM_EV_DESTROY);
    store(1, 1); // glob[1]: its Destroy ran
    op(VM_OP_HALT);
    log_collision(1); // an enemy: logs
    op(VM_OP_HALT);
    log_collision(2); // a decoy: logs, then leaves the sets (tags 0)
    op(VM_OP_SELF);
    push8(0);
    setp(VM_P_TAGS);
    op(VM_OP_HALT);
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    Entity shot = collider(TAG_A, 0, 0, 8, 8, 1);
    Entity e1 = collider(TAG_B, 2, 2, 8, 8, 2);
    Entity e2 = collider(TAG_B, 4, 4, 8, 8, 3);
    vm_attach(shot, 0);
    vm_attach(e1, 1);
    vm_attach(e2, 1);
    u32 before = debug_warning_count();
    frame();
    static const s32 shot_log[] = {102, 201};
    CHECK(logged(shot_log, 2));
    CHECK(!entity_alive(shot) && vm_global(1) == 1);

    Entity decoy = collider(TAG_A, 0, 0, 8, 8, 4);
    vm_attach(decoy, 2);
    vm_set_global(LOG, 0);
    frame();
    static const s32 decoy_log[] = {402, 204};
    CHECK(logged(decoy_log, 2));
    CHECK(!ent_has(entity_index(decoy), TAG_A));
    frame(); // no longer in set a: nothing
    CHECK(logged(decoy_log, 2));
    CHECK_WARNED(before, 0);
}

// vm.h vm_collide: an entity destroyed during the pass is not tested again,
// even when a spawn reuses its slot in the same pass: the new entity, though
// it has the set's components and overlaps an entity of b, is tested from the
// next frame. The pool is full, so the spawn must take the freed slot.
static void collide_skips_a_slot_reused_during_the_pass(void) {
    reset();
    collide_blob_begin(4);
    object(2, C_POS | C_SPR | C_BODY | TAG_A, 0); // the replacement
    log_collision(0);                             // x: logs, then kills itself and z
    op(VM_OP_SELF);
    op(VM_OP_KILL);
    ldg(2); // z
    op(VM_OP_KILL);
    op(VM_OP_HALT);
    log_collision(1); // y and y2: log
    op(VM_OP_HALT);
    log_collision(2); // the replacement: logs; its Create sizes it, names it 9
    op(VM_OP_HALT);
    handler(2, VM_EV_CREATE);
    op(VM_OP_SELF);
    push8(8);
    setp(VM_P_BODY_W);
    op(VM_OP_SELF);
    push8(8);
    setp(VM_P_BODY_H);
    op(VM_OP_SELF);
    push8(9);
    setp(VM_P_DEPTH);
    op(VM_OP_HALT);
    handler(3, VM_EV_DESTROY); // z: spawns the replacement on y2
    push32(FX(40));            // x
    push32(FX(10));            // x y
    spawn(2);                  // e
    op(VM_OP_DROP);            //
    op(VM_OP_HALT);            //
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    Entity x = collider(TAG_A, 10, 10, 8, 8, 1);
    Entity y = collider(TAG_B, 12, 12, 8, 8, 2);
    Entity y2 = collider(TAG_B, 40, 10, 8, 8, 3);
    Entity z = entity_create(C_POS);
    vm_attach(x, 0);
    vm_attach(y, 1);
    vm_attach(y2, 1);
    vm_attach(z, 3);
    vm_set_global(2, z);
    while (entity_create(C_POS) != ENTITY_NONE) {
    }
    CHECK(ecs_free_count() == 0);
    u32 before = debug_warning_count();
    frame();
    static const s32 first[] = {102, 201};
    CHECK(logged(first, 2));
    CHECK(!entity_alive(x) && !entity_alive(z));
    Entity spawned = entity_at(entity_index(x)); // the slot x had
    CHECK(spawned != ENTITY_NONE && ent_has(entity_index(spawned), TAG_A));
    CHECK(spr_depth[entity_index(spawned)] == 9);
    frame();
    static const s32 next[] = {102, 201, 903, 309};
    CHECK(logged(next, 4));
    CHECK_WARNED(before, 0);
}

// vm.h vm_events: it drains the events game code queued first, then runs
// the pass, so the pass sees what their reactions did.
static void collide_runs_after_the_queued_events(void) {
    reset();
    collide_blob_begin(2);
    log_collision(0); // logs, then kills itself
    op(VM_OP_SELF);
    op(VM_OP_KILL);
    op(VM_OP_HALT);
    log_collision(1);
    op(VM_OP_HALT);
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    Entity a = collider(TAG_A, 0, 0, 8, 8, 1);
    Entity b = collider(TAG_B, 4, 4, 8, 8, 2);
    vm_attach(a, 0);
    vm_attach(b, 1);
    vm_step();
    vm_event(a, b, VM_EV_COLLISION); // as the game's own collision code would
    vm_events();
    static const s32 expected[] = {102};
    CHECK(logged(expected, 1));
    CHECK(!entity_alive(a));
}

// vm.h vm_collide: the queue holds each overlap's events only until they
// run, so a pass with more events than VM_EVENT_QUEUE drops none: 12 x 12
// overlapping entities raise 288 events, and nothing warns.
static void collide_never_fills_the_queue(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_COLLISION);
    count(1);
    op(VM_OP_HALT);
    CHECK(load());
    CHECK(vm_collide(TAG_A, TAG_B));
    for (u32 k = 0; k < 24; k++) // all within 11 pixels of each other: every pair overlaps
        vm_attach(collider(k < 12 ? TAG_A : TAG_B, (s32)(k % 12), (s32)(k % 12), 16, 16, (s16)k),
                  0);
    u32 before = debug_warning_count();
    frame();
    CHECK(12 * 12 * 2 > VM_EVENT_QUEUE);
    CHECK(vm_global(1) == 12 * 12 * 2);
    CHECK_WARNED(before, 0);
}

// vm.h vm_collide: a mask of 0 and a pair past VM_COLLIDE_PAIRS are refused
// (false, one warning per kind); vm_collide_clear removes every pair; pairs
// outlast vm_load, vm_reload and vm_unload; without a blob nothing is tested.
static void vm_collide_limits_and_lifetime(void) {
    reset();
    u32 before = debug_warning_count();
    CHECK(!vm_collide(0, TAG_B));
    CHECK(!vm_collide(TAG_A, 0));
    CHECK_WARNED(before, 1);
    for (u32 k = 0; k < VM_COLLIDE_PAIRS; k++)
        CHECK(vm_collide(TAG_A, game_tag(k + 1)));
    CHECK(vm_collide(C_GAME(1), TAG_A)); // set already: true, takes no room
    CHECK(!vm_collide(TAG_A, C_GAME(VM_COLLIDE_PAIRS + 1)));
    CHECK(!vm_collide(TAG_B, TAG_C));
    CHECK_WARNED(before, 2);
    vm_collide_clear();
    for (u32 k = 0; k < VM_COLLIDE_PAIRS; k++)
        CHECK(vm_collide(TAG_B, game_tag(k + 2)));
    CHECK_WARNED(before, 2);

    vm_collide_clear();
    collide_blob_begin(1);
    log_collision(0);
    op(VM_OP_HALT);
    CHECK(vm_collide(TAG_A, TAG_B)); // before any blob is loaded
    Entity a = collider(TAG_A, 0, 0, 8, 8, 1);
    Entity b = collider(TAG_B, 4, 4, 8, 8, 2);
    frame(); // no blob: nothing tested, nothing warns
    CHECK(load());
    vm_attach(a, 0);
    vm_attach(b, 0);
    frame();
    static const s32 once[] = {102, 201};
    CHECK(logged(once, 2));
    CHECK(reload()); // kept: a and b stay attached, and still collide
    vm_set_global(LOG, 0);
    frame();
    CHECK(logged(once, 2));
    vm_unload();
    frame();
    CHECK(load()); // kept through the unload too
    vm_attach(a, 0);
    vm_attach(b, 0);
    frame();
    CHECK(logged(once, 2));
    vm_collide_clear();
    vm_set_global(LOG, 0);
    frame();
    CHECK(vm_global(LOG) == 0);
    CHECK_WARNED(before, 2);
}

// --- Determinism -------------------------------------------------------------

// Runs a program that spawns movers at random places, a frame apart, from a
// fresh ECS and VM, and copies out its globals.
static void run_spawners(s32* out) {
    enum { L_NEXT };
    reset();
    random_seed(1234); // random_range is the engine's: seeded like the game's
    blob_begin(2, 0, GLOBALS);
    object(1, C_POS | C_VEL, 0);
    handler(0, VM_EV_CREATE); // spawns 6 movers, a frame apart
    enter(0, 1);              // loc[0]
    push8(6);                 // 6
    stl(0);                   // loc[0] = movers left
    label(L_NEXT);            //
    push8(1);                 // 1
    push8(99);                // 1 99
    sys(VM_SYS_RANDOM_RANGE); // x
    ldl(0);                   // x y
    spawn(1);                 // mover
    stg(1);                   // glob[1] = the latest mover
    wait_frames(1);           //
    ldl(0);                   // n
    push8(1);                 // n 1
    op(VM_OP_SUB);            // n-1
    op(VM_OP_DUP);            // n-1 n-1
    stl(0);                   // n-1
    jump(VM_OP_JNZ, L_NEXT);  //
    op(VM_OP_HALT);           //
    handler(1, VM_EV_CREATE); // a mover adds its x to glob[0] and moves right
    op(VM_OP_SELF);           // self
    getp(VM_P_X);             // x
    ldg(0);                   // x g
    op(VM_OP_ADD);            // x+g
    stg(0);                   // glob[0] += x
    op(VM_OP_SELF);           // self
    push16(FX(1));            // self 1.0
    setp(VM_P_VX);            //
    op(VM_OP_HALT);           //
    handler(1, VM_EV_STEP);   // every mover adds its x to glob[2] every frame
    op(VM_OP_SELF);           // self
    getp(VM_P_X);             // x
    ldg(2);                   // x g
    op(VM_OP_ADD);            // x+g
    stg(2);                   // glob[2] += x
    op(VM_OP_HALT);           //
    CHECK(load());
    start(0);
    for (u32 f = 0; f < 12; f++) {
        vm_step();
        sys_movement();
        vm_events();
    }
    for (u16 g = 0; g < GLOBALS; g++)
        out[g] = vm_global(g);
    // glob[1] holds an entity handle, whose generation byte depends on what
    // earlier cases did with the slot (ecs_reset bumps it); its slot is what
    // the run decided.
    out[1] = entity_index((Entity)out[1]);
}

// vm.md "Determinism": two identical runs (scripts, inputs, seed) leave
// identical globals.
static void runs_are_deterministic(void) {
    static SERVAL_EWRAM_BSS s32 first[GLOBALS], second[GLOBALS];
    run_spawners(first);
    run_spawners(second);
    u32 differ = 0;
    for (u32 g = 0; g < GLOBALS; g++)
        differ += first[g] != second[g];
    CHECK(differ == 0);
    CHECK(first[0] >= 6 && first[1] != 0 && first[2] != 0); // it did something
}

// --- Debug ops ---------------------------------------------------------------

// vm.md "Debug", "Exact semantics: Debug ops": BRK logs and goes on; TRACE
// logs its string and the top of the stack without popping, logs only the
// string on an empty stack, and "?" for an invalid string index. No warnings.
static void debug_ops_log_and_continue(void) {
    reset();
    blob_begin(2, 1, GLOBALS);
    string(0, "TRACE TEST");
    handler(0, VM_EV_CREATE);
    op(VM_OP_BRK);  // logs
    store(0, 1);    //
    push8(42);      // 42
    trace(0);       // logs the string and 42: 42
    stg(1);         // glob[1] = 42
    trace(0);       // empty stack: logs the string
    store(2, 1);    //
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    trace(7);       // no string 7: logs "?"
    store(3, 1);    //
    op(VM_OP_HALT); //
    CHECK(load());
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 1 && vm_global(1) == 42 && vm_global(2) == 1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    start(1);
    vm_step();
    CHECK(vm_global(3) == 1);
    CHECK(vm_idle());
}

// vm.h: vm_unload halts everything, and with no blob vm_start fails. The last
// case: leaves the VM unloaded for the suites after this one.
static void unload_stops_everything(void) {
    reset();
    build_counters();
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events();
    start(1);
    vm_step();
    CHECK(vm_global(0) == 1 && vm_global(1) == 1);
    vm_unload();
    CHECK(vm_idle());
    frames(2); // nothing to run
    u32 before = debug_warning_count();
    CHECK(vm_start(1, VM_EV_CREATE) == -1);
    CHECK(vm_idle());
    CHECK_WARNED(before, 1);
    ecs_reset();
}

TEST_SUITE(
    vm_tests, "vm", {"golden_example", golden_example},
    {"stack_and_variable_ops", stack_and_variable_ops},
    {"push_sign_extension", push_sign_extension}, {"arithmetic_ops", arithmetic_ops},
    {"fixed_point_ops", fixed_point_ops}, {"bitwise_and_shift_ops", bitwise_and_shift_ops},
    {"lua_shift_op", lua_shift_op}, {"floored_division_ops", floored_division_ops},
    {"comparison_ops", comparison_ops}, {"division_by_zero", division_by_zero},
    {"control_flow", control_flow},
    {"ret_with_empty_call_stack_halts", ret_with_empty_call_stack_halts},
    {"frames_hold_arguments_and_locals", frames_hold_arguments_and_locals},
    {"recursion_to_the_limits", recursion_to_the_limits},
    {"handler_level_returns_end_the_handler", handler_level_returns_end_the_handler},
    {"enter_needs_its_arguments", enter_needs_its_arguments},
    {"stack_overflow_halts_only_its_context", stack_overflow_halts_only_its_context},
    {"stack_underflow_halts_only_its_context", stack_underflow_halts_only_its_context},
    {"call_depth_is_limited", call_depth_is_limited},
    {"unknown_opcode_halts_only_its_context", unknown_opcode_halts_only_its_context},
    {"locals_out_of_range", locals_out_of_range},
    {"pc_escaping_the_blob_halts_its_context", pc_escaping_the_blob_halts_its_context},
    {"truncated_operand_halts_its_context", truncated_operand_halts_its_context},
    {"unknown_property_warns", unknown_property_warns},
    {"unknown_sys_halts_only_its_context", unknown_sys_halts_only_its_context},
    {"wait_counts_frames", wait_counts_frames},
    {"wait_counter_is_clamped", wait_counter_is_clamped},
    {"vm_start_first_runs_in_the_next_vm_step", vm_start_first_runs_in_the_next_vm_step},
    {"vm_start_without_a_handler_fails", vm_start_without_a_handler_fails},
    {"contexts_resume_in_pool_order", contexts_resume_in_pool_order},
    {"step_reactions_run_every_frame", step_reactions_run_every_frame},
    {"create_runs_before_the_first_step", create_runs_before_the_first_step},
    {"entity_spawned_by_step_steps_next_frame", entity_spawned_by_step_steps_next_frame},
    {"step_never_runs_before_create", step_never_runs_before_create},
    {"reactions_run_on_top_of_a_waiting_behaviour", reactions_run_on_top_of_a_waiting_behaviour},
    {"reactions_keep_a_behaviours_wait", reactions_keep_a_behaviours_wait},
    {"reactions_cannot_reach_below_their_activation",
     reactions_cannot_reach_below_their_activation},
    {"reaction_waits_and_overruns_halt_only_the_reaction",
     reaction_waits_and_overruns_halt_only_the_reaction},
    {"events_drain_in_fifo_order", events_drain_in_fifo_order},
    {"events_queued_while_draining_run_in_the_same_phase",
     events_queued_while_draining_run_in_the_same_phase},
    {"kill_runs_destroy_then_halts_the_behaviour", kill_runs_destroy_then_halts_the_behaviour},
    {"kill_runs_destroy_before_destroying", kill_runs_destroy_before_destroying},
    {"kill_destroys_an_unattached_entity", kill_destroys_an_unattached_entity},
    {"wait_inside_destroy_warns_and_halts", wait_inside_destroy_warns_and_halts},
    {"kill_of_no_entity_warns_but_of_a_dead_one_is_silent",
     kill_of_no_entity_warns_but_of_a_dead_one_is_silent},
    {"destroy_event_behaves_like_kill", destroy_event_behaves_like_kill},
    {"vm_event_rejects_unknown_events", vm_event_rejects_unknown_events},
    {"vm_kill_runs_destroy_at_once", vm_kill_runs_destroy_at_once},
    {"detach_stops_the_script_without_destroy", detach_stops_the_script_without_destroy},
    {"attach_rebinds_an_attached_entity", attach_rebinds_an_attached_entity},
    {"double_attach_queues_one_create", double_attach_queues_one_create},
    {"a_behaviour_event_replaces_the_live_behaviour",
     a_behaviour_event_replaces_the_live_behaviour},
    {"attach_misuse_is_ignored", attach_misuse_is_ignored},
    {"spawn_creates_an_attached_entity", spawn_creates_an_attached_entity},
    {"spawn_failures_push_zero", spawn_failures_push_zero},
    {"full_event_queue_drops_with_a_warning", full_event_queue_drops_with_a_warning},
    {"a_full_room_fits_the_queue", a_full_room_fits_the_queue},
    {"context_pool_exhaustion_warns", context_pool_exhaustion_warns},
    {"budget_throttles_an_endless_loop", budget_throttles_an_endless_loop},
    {"budget_warns_once_per_loaded_blob", budget_warns_once_per_loaded_blob},
    {"budget_halts_a_destroy_handler", budget_halts_a_destroy_handler},
    {"ops_this_frame_counts_both_phases", ops_this_frame_counts_both_phases},
    {"stale_binding_counts_as_unbound", stale_binding_counts_as_unbound},
    {"self_and_other", self_and_other},
    {"properties_read_and_write_the_ecs", properties_read_and_write_the_ecs},
    {"properties_of_dead_entities", properties_of_dead_entities},
    {"properties_of_handles_past_the_pool", properties_of_handles_past_the_pool},
    {"property_without_its_component_warns", property_without_its_component_warns},
    {"body_size_properties", body_size_properties}, {"read_only_properties", read_only_properties},
    {"objects_of_entities", objects_of_entities},
    {"body_properties_meet_physics", body_properties_meet_physics},
    {"spawned_kinematic_bodies", spawned_kinematic_bodies},
    {"tags_are_the_game_components", tags_are_the_game_components},
    {"instance_fields", instance_fields},
    {"nexti_loops_over_an_objects_instances", nexti_loops_over_an_objects_instances},
    {"wait_move_resumes_when_the_path_ends", wait_move_resumes_when_the_path_ends},
    {"wait_anim_resumes_on_the_last_frame", wait_anim_resumes_on_the_last_frame},
    {"wait_anim_without_a_one_shot_animation_continues",
     wait_anim_without_a_one_shot_animation_continues},
    {"wait_anim_continues_if_the_sprite_stops_being_one_shot",
     wait_anim_continues_if_the_sprite_stops_being_one_shot},
    {"animation_end_is_raised_when_an_animation_finishes",
     animation_end_is_raised_when_an_animation_finishes},
    {"animation_properties_restart_an_animation", animation_properties_restart_an_animation},
    {"sys_page_numbers_are_fixed", sys_page_numbers_are_fixed},
    {"sys_random_range", sys_random_range}, {"sys_camera_set", sys_camera_set},
    {"sys_path_start_uses_bindings", sys_path_start_uses_bindings},
    {"sys_bad_string_or_song_index", sys_bad_string_or_song_index},
    {"platform_sys_calls_reach_the_platform", platform_sys_calls_reach_the_platform},
    {"sys_text_print_number", sys_text_print_number}, {"sys_path_stop", sys_path_stop},
    {"sys_screen_set_blend", sys_screen_set_blend},
    {"sys_set_colors_read_their_array", sys_set_colors_read_their_array},
    {"sys_set_colors_refuse_a_bad_array_or_count", sys_set_colors_refuse_a_bad_array_or_count},
    {"ram_arrays", ram_arrays}, {"rom_arrays_of_every_kind", rom_arrays_of_every_kind},
    {"ram_arrays_across_loads", ram_arrays_across_loads},
    {"reload_keeps_globals_if_their_count_matches", reload_keeps_globals_if_their_count_matches},
    {"globals_start_at_their_initial_values", globals_start_at_their_initial_values},
    {"reload_keeps_attachments_to_objects_that_remain",
     reload_keeps_attachments_to_objects_that_remain},
    {"reload_keeps_the_kept_instances_fields", reload_keeps_the_kept_instances_fields},
    {"reload_halts_contexts_and_empties_the_queue", reload_halts_contexts_and_empties_the_queue},
    {"load_resets_everything", load_resets_everything},
    {"loading_during_a_phase_is_refused", loading_during_a_phase_is_refused},
    {"loading_during_vm_kill_is_refused", loading_during_vm_kill_is_refused},
    {"phases_do_not_nest", phases_do_not_nest},
    {"loader_rejects_bad_blobs", loader_rejects_bad_blobs},
    {"loader_rejects_a_string_without_its_nul", loader_rejects_a_string_without_its_nul},
    {"loader_checks_array_records", loader_checks_array_records},
    {"loader_checks_initial_values", loader_checks_initial_values},
    {"collide_raises_collisions_on_both_sides", collide_raises_collisions_on_both_sides},
    {"collide_events_go_to_collision_handlers_only", collide_events_go_to_collision_handlers_only},
    {"collide_order_is_pairs_then_slots", collide_order_is_pairs_then_slots},
    {"collide_tests_each_pair_of_entities_once", collide_tests_each_pair_of_entities_once},
    {"collide_reactions_run_before_the_next_test", collide_reactions_run_before_the_next_test},
    {"collide_skips_a_slot_reused_during_the_pass", collide_skips_a_slot_reused_during_the_pass},
    {"collide_runs_after_the_queued_events", collide_runs_after_the_queued_events},
    {"collide_never_fills_the_queue", collide_never_fills_the_queue},
    {"vm_collide_limits_and_lifetime", vm_collide_limits_and_lifetime},
    {"runs_are_deterministic", runs_are_deterministic},
    {"debug_ops_log_and_continue", debug_ops_log_and_continue},
    {"unload_stops_everything", unload_stops_everything});
