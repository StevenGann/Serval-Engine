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
#include "serval/random.h"
#include "serval/sprites.h"
#include "serval/vm.h"
#include "test.h"

#include "../src/core/sprite_internal.h"
#ifndef SERVAL_GBA
#include "../src/core/vm_internal.h"
#endif

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
    u16 objects, strings;
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
// per object) and the string table (4 bytes per string), zeroed: no
// handlers, mask 0, sprite 0. Strings and code follow.
static void blob_begin(u16 objects, u16 strings, u16 globals) {
    bld.size = 0;
    bld.objects = objects;
    bld.strings = strings;
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
    emit16(0); // reserved
    for (u32 k = 0; k < (u32)objects * VM_OBJECT_SIZE + 4u * strings; k++)
        emit(0);
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

// Every case starts from an empty ECS and no blob, so cases are independent.
static void reset(void) {
    ecs_reset();
    vm_unload();
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
// sees vm_set_global; locals start zeroed in every new context ("Contexts").
static void stack_and_variable_ops(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
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
            test_fail(__FILE__, __LINE__, rows[r].what);
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

// vm.md "Exact semantics: Arithmetic": FXMUL is (s32)(((s64)a * b) >> 8) (an
// arithmetic shift: rounds down), FXDIV (s64)a * 256 / b (rounds toward zero),
// both truncated to 32 bits.
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

// vm.md "Arithmetic, logic, comparison": DIV, MOD and FXDIV by zero give 0
// and warn once (one kind of problem: "Warnings repeat once per problem, per
// loaded blob"); the context goes on.
static void division_by_zero(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    for (u32 round = 0; round < 2; round++) {
        push8(7);       // 7
        push8(0);       // 7 0
        op(VM_OP_DIV);  // 0
        stg(3 * round); // glob[0 or 3] = 0
        push8(-7);      // -7
        push8(0);       // -7 0
        op(VM_OP_MOD);  // 0
        stg(3 * round + 1);
        push32(FX(3));   // 3.0
        push8(0);        // 3.0 0
        op(VM_OP_FXDIV); // 0
        stg(3 * round + 2);
    }
    store(6, 1);    // carried on
    op(VM_OP_HALT); //
    CHECK(load());
    for (u16 g = 0; g < 7; g++)
        vm_set_global(g, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    u32 wrong = 0;
    for (u16 g = 0; g < 6; g++)
        wrong += vm_global(g) != 0;
    CHECK(wrong == 0);
    CHECK(vm_global(6) == 1);
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

// --- Runtime safety ----------------------------------------------------------

// vm.md "Opcode reference": stack overflow on any op warns and halts the
// context; VM_STACK cells fit.
static void stack_overflow_halts_only_its_context(void) {
    reset();
    blob_begin(3, 0, GLOBALS);
    witness(0);
    handler(1, VM_EV_CREATE);
    store(1, 1);                        //
    for (s32 v = 1; v <= VM_STACK; v++) //
        push8(v);                       // a full stack: 1 .. 8
    stg(3);                             // glob[3] = 8: they all fit
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
    CHECK(load());
    expect_faults_halt_only_themselves(2);
    CHECK(vm_global(1) == 1 && vm_global(2) == 1);
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

// vm.md "Opcode reference": CALL deeper than VM_CALLS overflows the call
// stack: warns, halts.
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
    call(L_F);      // the fifth nested CALL overflows
    op(VM_OP_RET);  //
    CHECK(load());
    expect_faults_halt_only_themselves(1);
    CHECK(vm_global(1) == 1);
    CHECK(vm_global(2) == VM_CALLS);
}

// vm.md "Opcode reference": an unknown opcode warns and halts the context.
static void unknown_opcode_halts_only_its_context(void) {
    static const u8 unknown[] = {0x0C, 0x1F, 0x41, 0xFF};
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

// vm.md "Stack and variables": LDL n >= VM_LOCALS warns and pushes 0, STL
// warns and drops the value. Neither halts: the context goes on. Both are one
// kind of problem (no such local): one warning.
static void locals_out_of_range(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    push8(5);           // 5
    ldl(VM_LOCALS);     // warns: 5 0
    stg(0);             // glob[0] = 0
    stg(1);             // glob[1] = 5
    push8(7);           // 7
    push8(9);           // 7 9
    stl(VM_LOCALS);     // warns, pops the 9: 7
    stg(2);             // glob[2] = 7
    push8(42);          // 42
    stl(VM_LOCALS - 1); // the last local is fine
    ldl(VM_LOCALS - 1); // 42
    stg(3);             // glob[3] = 42
    store(4, 1);        // carried on
    op(VM_OP_HALT);     //
    CHECK(load());
    vm_set_global(0, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(vm_global(0) == 0 && vm_global(1) == 5 && vm_global(2) == 7);
    CHECK(vm_global(3) == 42 && vm_global(4) == 1);
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
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
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
    vm_set_global(0, e);
    vm_set_global(1, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
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

// vm.md "Exact semantics: Step handlers": every frame after the resume pass,
// in entity index order, for attached entities with a Step handler and no
// live context; skipped while one waits.
static void step_handlers_run_every_frame_unless_live(void) {
    reset();
    blob_begin(4, 0, GLOBALS);
    handler(0, VM_EV_STEP);
    append(0, 1);   //
    op(VM_OP_HALT); //
    handler(1, VM_EV_STEP);
    append(0, 2);   //
    op(VM_OP_HALT); //
    handler(2, VM_EV_STEP);
    count(1);       // glob[1]: Step runs
    wait_frames(3); // live for three frames: Step skipped meanwhile
    count(2);       // glob[2]: resumes
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
    vm_events(); // drains the Create events (no Create handlers)
    u32 before = debug_warning_count();
    static const s32 runs[] = {1, 1, 1, 2, 2, 2, 3};    // glob[1] after frames 1-7
    static const s32 resumes[] = {0, 0, 0, 1, 1, 1, 2}; // glob[2]
    for (u32 f = 0; f < 7; f++) {
        vm_set_global(0, 0);
        vm_step();
        CHECK(vm_global(0) == 12); // e0 then e1, once each
        CHECK(vm_global(1) == runs[f] && vm_global(2) == resumes[f]);
        vm_events();
    }
    CHECK_WARNED(before, 0);
}

// vm.md "Contexts" (one script per entity), "Exact semantics: Draining": an
// event for an entity whose context is live is dropped with a warning (not
// deferred); one its object has no handler for is skipped silently.
static void event_for_a_live_entity_is_dropped(void) {
    reset();
    blob_begin(1, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(5); //
    store(0, 2);    //
    op(VM_OP_HALT); //
    handler(0, VM_EV_COLLISION);
    count(1);       //
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // Create runs and waits
    CHECK(vm_global(0) == 1);
    u32 before = debug_warning_count();
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    vm_events(); // dropped, with a warning
    CHECK(vm_global(1) == 0);
    CHECK_WARNED(before, 1);
    vm_event(e, ENTITY_NONE, VM_EV_ANIM_END);
    vm_events(); // no handler: skipped silently
    CHECK_WARNED(before, 1);
    frames(5);
    CHECK(vm_global(0) == 2); // Create finished...
    CHECK(vm_global(1) == 0); // ...and the dropped event stayed dropped
    vm_event(e, ENTITY_NONE, VM_EV_COLLISION);
    vm_events(); // free again: it runs
    CHECK(vm_global(1) == 1);
    CHECK_WARNED(before, 1);
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

// vm.md "Contexts": Destroy (queued by KILL) force-halts the entity's live
// context first, then runs the Destroy handler, then destroys the entity.
static void kill_force_halts_a_waiting_script(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    store(0, 1);    //
    wait_frames(5); //
    store(0, 2);    // never runs: killed while waiting
    op(VM_OP_HALT); //
    handler(0, VM_EV_DESTROY);
    store(1, 1);    //
    op(VM_OP_HALT); //
    handler(1, VM_EV_CREATE);
    ldg(5);         // e
    op(VM_OP_KILL); // queues e's Destroy
    op(VM_OP_HALT); //
    CHECK(load());
    Entity e = entity_create(C_POS);
    vm_attach(e, 0);
    vm_events(); // Create runs and waits
    vm_set_global(5, e);
    u32 before = debug_warning_count();
    start(1);
    vm_step(); // the thread queues Destroy; the drain halts e's wait, runs Destroy
    CHECK(vm_global(1) == 1);
    CHECK(!entity_alive(e));
    CHECK(vm_idle());
    vm_events();
    frames(6);
    CHECK(vm_global(0) == 1);
    CHECK_WARNED(before, 0);
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

// vm.md "Exact semantics: Draining": a wait inside Destroy warns and halts
// the handler; the entity is destroyed all the same.
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

// vm.md "Exact semantics: vm_kill": from C, outside the phases, the Destroy
// logic runs at once: force-halt, Destroy handler (if any), entity_destroy;
// an unattached entity is just destroyed.
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
// warns once in its lifetime. Other contexts still run, and the endless loop
// gets exactly one slice per frame.
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
    CHECK_WARNED(before, 1); // once per context
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
// extending s16, zero-extending u8 and u16).
static const struct {
    s32 written;
    s32 read; // what GETP then gives
    const char* what;
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
};

static void properties_read_and_write_the_ecs(void) {
    reset();
    blob_begin(2, 0, GLOBALS);
    handler(0, VM_EV_CREATE);
    for (u32 p = 0; p < VM_P_COUNT; p++) {
        ldg(0);                       // e
        push32(prop_rows[p].written); // e v
        setp(p);                      //
    }
    for (u32 p = 0; p < VM_P_COUNT; p++) {
        ldg(0);     // e
        getp(p);    // value
        stg(1 + p); // glob[1 + p]
    }
    op(VM_OP_HALT);
    handler(1, VM_EV_CREATE); // reads what C set
    for (u32 p = 0; p < VM_P_COUNT; p++) {
        ldg(0);      // e
        getp(p);     // value
        stg(11 + p); // glob[11 + p]
    }
    op(VM_OP_HALT);
    CHECK(load());
    Entity e = entity_create(C_POS | C_VEL | C_SPR);
    u32 i = entity_index(e);
    vm_set_global(0, e);
    u32 before = debug_warning_count();
    start(0);
    vm_step();
    CHECK(pos_x[i] == -FX(12) - 5 && pos_y[i] == INT32_MAX);
    CHECK(vel_x[i] == -FX(1) / 2 && vel_y[i] == FX(3));
    CHECK(spr_id[i] == 0x2345 && spr_frame[i] == 0xFF && spr_flags[i] == 0xFFFF);
    CHECK(spr_angle[i] == 0x8000 && spr_depth[i] == -32768 && spr_scale[i] == -1);
    for (u32 p = 0; p < VM_P_COUNT; p++)
        if (vm_global((u16)(1 + p)) != prop_rows[p].read)
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
    static const s32 from_c[VM_P_COUNT] = {-FX(7), 3,      1,      -1, 0xFFFF,
                                           200,    0x8001, 0xFFFF, -2, -32768};
    start(1);
    vm_step();
    u32 wrong = 0;
    for (u32 p = 0; p < VM_P_COUNT; p++)
        wrong += vm_global((u16)(11 + p)) != from_c[p];
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

// --- Waits on the engine -----------------------------------------------------

static const PathStep move_steps[] = {{.frames = 3, .speed = FX(1)}};
static const Path move_path = {PATH_STEPS(move_steps)};

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

// vm.md "Waits": WAIT_ANIM without C_ANIM, or on a looping sprite, warns
// (once: one kind of problem) and goes on at once.
static void wait_anim_without_a_one_shot_animation_continues(void) {
    reset();
    use_anim_sprites();
    blob_begin(2, 0, GLOBALS);
    for (u16 obj = 0; obj < 2; obj++) {
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
    restore_sprites();
}

// --- SYS ---------------------------------------------------------------------

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
    push8(55);              // 55
    push8(3);               // 55 col
    push8(4);               // 55 col row
    ldg(0);                 // 55 col row index
    sys(VM_SYS_TEXT_PRINT); // 55
    stg(1);                 // glob[1] = 55
    push8(56);              // 56
    ldg(2);                 // 56 index
    sys(VM_SYS_MUSIC_PLAY); // 56
    stg(3);                 // glob[3] = 56
    op(VM_OP_HALT);         //
    CHECK(load());
    vm_bind(&(VmBindings){.songs = songs, .song_count = 2});
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

    vm_bind(&(VmBindings){.song_count = 0}); // no songs bound
    vm_set_global(0, -1);                    // a negative string index
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
static bool last_call(u32 calls, u32 fn, s32 a0, s32 a1, s32 a2, const void* ptr) {
    const ServalHostVmCalls* r = &serval_host_vm_calls;
    return r->calls == calls && r->fn == fn && r->args[0] == a0 && r->args[1] == a1 &&
           r->args[2] == a2 && r->ptr == ptr;
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
    push8(5);                    // sound 5
    sys(VM_SYS_PSG_PLAY);        // frame 1
    wait_frames(1);              //
    push8(1);                    // song 1
    sys(VM_SYS_MUSIC_PLAY);      // frame 2
    wait_frames(1);              //
    sys(VM_SYS_MUSIC_STOP);      // frame 3
    wait_frames(1);              //
    sys(VM_SYS_MUSIC_PAUSE);     // frame 4
    wait_frames(1);              //
    sys(VM_SYS_MUSIC_RESUME);    // frame 5
    wait_frames(1);              //
    push8(3);                    // col
    push8(4);                    // col row
    push8(1);                    // col row string
    sys(VM_SYS_TEXT_PRINT);      // frame 6
    wait_frames(1);              //
    push16(BUTTON_A | BUTTON_L); // buttons
    sys(VM_SYS_BUTTON_DOWN);     // frame 7: 1
    stg(0);                      // glob[0] = 1
    wait_frames(1);              //
    push16(BUTTON_START);        // buttons
    sys(VM_SYS_BUTTON_PRESSED);  // frame 8: 0
    stg(1);                      // glob[1] = 0
    wait_frames(1);              //
    push8(-16);                  // level
    sys(VM_SYS_BRIGHTNESS);      // frame 9
    op(VM_OP_HALT);              //
    CHECK(load());
    vm_bind(&(VmBindings){.songs = songs, .song_count = 2});
    serval_host_vm_calls = (ServalHostVmCalls){.calls = 0};
    vm_set_global(1, 99);
    start(0);
    u32 before = debug_warning_count();
    vm_step();
    CHECK(last_call(1, VM_SYS_PSG_PLAY, 5, 0, 0, NULL));
    frame();
    CHECK(last_call(2, VM_SYS_MUSIC_PLAY, 1, 0, 0, &song_b));
    frame();
    CHECK(last_call(3, VM_SYS_MUSIC_STOP, 0, 0, 0, NULL));
    frame();
    CHECK(last_call(4, VM_SYS_MUSIC_PAUSE, 0, 0, 0, NULL));
    frame();
    CHECK(last_call(5, VM_SYS_MUSIC_RESUME, 0, 0, 0, NULL));
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
    CHECK(last_call(9, VM_SYS_BRIGHTNESS, -16, 0, 0, NULL));
    CHECK(vm_idle());
    CHECK_WARNED(before, 0);
    vm_bind(&(VmBindings){.song_count = 0});
#endif
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

// vm.h: vm_reload keeps entities attached to objects the new blob still has
// (running its code); others are no longer attached (but stay alive).
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
    build_steppers(4, 3, 1); // object 1 is gone; object 0's Step appends 3 now
    CHECK(reload());
    vm_step();
    CHECK(vm_global(0) == 13);
    CHECK(vm_global(1) == 2);
    CHECK(entity_alive(e0) && entity_alive(e1));
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
// Create handler offset is at 0x18, Room Start's at 0x2C, string 0's at 0x30.
static const Patch bad_patches[] = {
    {0, 'X', 1, "bad magic"},
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
};

static const Patch good_patches[] = {
    {12, VM_GLOBALS, 2, "VM_GLOBALS globals rejected"},
    {0x18, 0x34, 4, "handler right after the tables rejected"},
    {0x18, sizeof golden - 1, 4, "handler at the last byte rejected"},
    {0x2C, 0x37, 4, "Room Start handler rejected"},
    {0x30, 0x37, 4, "string past the tables rejected"},
};

// vm.md "Load-time validation": vm_load returns false and warns (once per
// call) for a bad header, tables past the end, globals over VM_GLOBALS, and
// handler or string offsets outside the blob or inside its tables; offsets
// anywhere past the tables are fine.
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
    static s32 first[GLOBALS], second[GLOBALS];
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

TEST_SUITE(vm_tests, "vm", {"golden_example", golden_example},
           {"stack_and_variable_ops", stack_and_variable_ops},
           {"push_sign_extension", push_sign_extension}, {"arithmetic_ops", arithmetic_ops},
           {"fixed_point_ops", fixed_point_ops}, {"bitwise_and_shift_ops", bitwise_and_shift_ops},
           {"comparison_ops", comparison_ops}, {"division_by_zero", division_by_zero},
           {"control_flow", control_flow},
           {"ret_with_empty_call_stack_halts", ret_with_empty_call_stack_halts},
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
           {"step_handlers_run_every_frame_unless_live", step_handlers_run_every_frame_unless_live},
           {"event_for_a_live_entity_is_dropped", event_for_a_live_entity_is_dropped},
           {"events_drain_in_fifo_order", events_drain_in_fifo_order},
           {"events_queued_while_draining_run_in_the_same_phase",
            events_queued_while_draining_run_in_the_same_phase},
           {"kill_force_halts_a_waiting_script", kill_force_halts_a_waiting_script},
           {"kill_runs_destroy_before_destroying", kill_runs_destroy_before_destroying},
           {"kill_destroys_an_unattached_entity", kill_destroys_an_unattached_entity},
           {"wait_inside_destroy_warns_and_halts", wait_inside_destroy_warns_and_halts},
           {"vm_kill_runs_destroy_at_once", vm_kill_runs_destroy_at_once},
           {"detach_stops_the_script_without_destroy", detach_stops_the_script_without_destroy},
           {"attach_rebinds_an_attached_entity", attach_rebinds_an_attached_entity},
           {"attach_misuse_is_ignored", attach_misuse_is_ignored},
           {"spawn_creates_an_attached_entity", spawn_creates_an_attached_entity},
           {"spawn_failures_push_zero", spawn_failures_push_zero},
           {"full_event_queue_drops_with_a_warning", full_event_queue_drops_with_a_warning},
           {"context_pool_exhaustion_warns", context_pool_exhaustion_warns},
           {"budget_throttles_an_endless_loop", budget_throttles_an_endless_loop},
           {"ops_this_frame_counts_both_phases", ops_this_frame_counts_both_phases},
           {"stale_binding_counts_as_unbound", stale_binding_counts_as_unbound},
           {"self_and_other", self_and_other},
           {"properties_read_and_write_the_ecs", properties_read_and_write_the_ecs},
           {"properties_of_dead_entities", properties_of_dead_entities},
           {"property_without_its_component_warns", property_without_its_component_warns},
           {"wait_move_resumes_when_the_path_ends", wait_move_resumes_when_the_path_ends},
           {"wait_anim_resumes_on_the_last_frame", wait_anim_resumes_on_the_last_frame},
           {"wait_anim_without_a_one_shot_animation_continues",
            wait_anim_without_a_one_shot_animation_continues},
           {"sys_random_range", sys_random_range}, {"sys_camera_set", sys_camera_set},
           {"sys_path_start_uses_bindings", sys_path_start_uses_bindings},
           {"sys_bad_string_or_song_index", sys_bad_string_or_song_index},
           {"platform_sys_calls_reach_the_platform", platform_sys_calls_reach_the_platform},
           {"reload_keeps_globals_if_their_count_matches",
            reload_keeps_globals_if_their_count_matches},
           {"reload_keeps_attachments_to_objects_that_remain",
            reload_keeps_attachments_to_objects_that_remain},
           {"reload_halts_contexts_and_empties_the_queue",
            reload_halts_contexts_and_empties_the_queue},
           {"load_resets_everything", load_resets_everything},
           {"loader_rejects_bad_blobs", loader_rejects_bad_blobs},
           {"runs_are_deterministic", runs_are_deterministic},
           {"debug_ops_log_and_continue", debug_ops_log_and_continue},
           {"unload_stops_everything", unload_stops_everything});
