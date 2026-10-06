// The mini assembler (asm.h): writes a format v1 script blob into an EWRAM
// buffer, byte by byte, little-endian, as docs/vm.md lays it out.

#include "asm.h"

#define BLOB_CAP 4096 // fireflies' blob is about 1.1 KB
#define MAX_LABELS 96
#define MAX_FIXUPS 160
#define MAX_STRINGS 32
#define UNBOUND 0xFFFFFFFFu

static u8 bytes[BLOB_CAP] SERVAL_EWRAM_BSS;
static u32 size;
static u16 object_count, string_count;

static u32 labels[MAX_LABELS]; // blob offsets, or UNBOUND
static u32 label_count;

// Operands to fill in once every label is known: a rel16 (JMP, JZ, JNZ) or a
// u32 blob offset (CALL).
static struct {
    u32 at;
    u16 label;
    bool absolute;
} fixups[MAX_FIXUPS] SERVAL_EWRAM_BSS;
static u32 fixup_count;

static const char* strings[MAX_STRINGS];
static const char* error;

// Remembers the first problem; asm_end then fails.
static void fail(const char* what) {
    if (!error)
        error = what;
}

static void emit(u32 byte) {
    if (size < BLOB_CAP)
        bytes[size++] = (u8)byte;
    else
        fail("the blob is bigger than its buffer (BLOB_CAP)");
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
    bytes[at] = (u8)value;
    bytes[at + 1] = (u8)(value >> 8);
}

static void put32(u32 at, u32 value) {
    put16(at, value & 0xFFFF);
    put16(at + 2, value >> 16);
}

static u32 object_record(u32 object) {
    return VM_HEADER_SIZE + object * VM_OBJECT_SIZE;
}

static u32 string_entry(u32 index) {
    return object_record(object_count) + index * 4;
}

void asm_begin(u16 objects, u16 string_total, u16 globals) {
    size = 0;
    label_count = fixup_count = 0;
    error = NULL;
    object_count = objects;
    string_count = string_total;
    if (string_total > MAX_STRINGS)
        fail("too many strings (MAX_STRINGS)");
    for (u32 k = 0; k < MAX_STRINGS; k++)
        strings[k] = NULL;
    // Header: magic, version, cell width, flags, counts, reserved.
    emit('S');
    emit('V');
    emit('M');
    emit('B');
    emit(VM_FORMAT_VERSION);
    emit(VM_CELL_BYTES);
    emit16(0);
    emit16(objects);
    emit16(string_total);
    emit16(globals);
    emit16(0);
    // The object and string tables, zeroed; filled in by asm_object,
    // asm_handler and asm_end.
    for (u32 k = 0; k < (u32)objects * VM_OBJECT_SIZE + 4u * string_total; k++)
        emit(0);
}

void asm_object(u16 object, u32 components, u16 sprite) {
    if (object >= object_count) {
        fail("asm_object: no such object");
        return;
    }
    put32(object_record(object), components);
    put16(object_record(object) + 4, sprite);
}

void asm_handler(u16 object, u8 event) {
    if (object >= object_count || event >= VM_EV_COUNT) {
        fail("asm_handler: no such object or event");
        return;
    }
    put32(object_record(object) + 8 + 4u * event, size);
}

void asm_string(u16 index, const char* text) {
    if (index >= string_count || index >= MAX_STRINGS)
        fail("asm_string: no such string");
    else
        strings[index] = text;
}

AsmLabel asm_label(void) {
    if (label_count == MAX_LABELS) {
        fail("too many labels (MAX_LABELS)");
        return 0;
    }
    labels[label_count] = UNBOUND;
    return (AsmLabel)label_count++;
}

void asm_bind(AsmLabel label) {
    if (label >= label_count)
        fail("asm_bind: no such label");
    else if (labels[label] != UNBOUND)
        fail("a label is bound twice");
    else
        labels[label] = size;
}

void asm_op(u32 op) {
    emit(op);
}

void asm_op8(u32 op, u32 operand) {
    emit(op);
    emit(operand);
}

void asm_op16(u32 op, u32 operand) {
    emit(op);
    emit16(operand);
}

void asm_push(s32 value) {
    if (value >= INT8_MIN && value <= INT8_MAX) {
        asm_op8(VM_OP_PUSH8, (u32)value & 0xFF);
    } else if (value >= INT16_MIN && value <= INT16_MAX) {
        asm_op16(VM_OP_PUSH16, (u32)value & 0xFFFF);
    } else {
        emit(VM_OP_PUSH32);
        emit32((u32)value);
    }
}

// Records that the operand about to be emitted refers to `label`.
static void fixup(AsmLabel label, bool absolute) {
    if (fixup_count == MAX_FIXUPS) {
        fail("too many jumps and calls (MAX_FIXUPS)");
        return;
    }
    fixups[fixup_count].at = size;
    fixups[fixup_count].label = label;
    fixups[fixup_count].absolute = absolute;
    fixup_count++;
}

void asm_jump(u32 op, AsmLabel label) {
    emit(op);
    fixup(label, false);
    emit16(0);
}

void asm_call(AsmLabel label) {
    emit(VM_OP_CALL);
    fixup(label, true);
    emit32(0);
}

u32 asm_end(const u8** blob) {
    // Jumps and calls: a CALL takes a blob offset, a jump an s16 counted from
    // just after its operand.
    for (u32 f = 0; f < fixup_count; f++) {
        u32 at = fixups[f].at;
        u32 target = fixups[f].label < label_count ? labels[fixups[f].label] : UNBOUND;
        if (target == UNBOUND) {
            fail("a jump or call goes to a label that was never bound");
        } else if (fixups[f].absolute) {
            put32(at, target);
        } else {
            s32 offset = (s32)target - (s32)(at + 2);
            if (offset < INT16_MIN || offset > INT16_MAX)
                fail("a jump is too far for its rel16 offset");
            put16(at, (u32)offset & 0xFFFF);
        }
    }
    // The strings, after all the code.
    for (u32 s = 0; s < string_count && s < MAX_STRINGS; s++) {
        if (!strings[s]) {
            fail("a string was never set (asm_string)");
            continue;
        }
        put32(string_entry(s), size);
        for (const char* c = strings[s];; c++) {
            emit((u8)*c);
            if (!*c)
                break;
        }
    }
    if (error) {
        *blob = NULL;
        return 0;
    }
    *blob = bytes;
    return size;
}

const char* asm_error(void) {
    return error ? error : "";
}
