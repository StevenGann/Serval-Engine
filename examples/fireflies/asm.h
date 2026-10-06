// A mini assembler for Serval Engine script blobs (format v1, docs/vm.md),
// local to this example. The engine ships no assembler: a real project's blob
// is ROM data that Studio Advance's script compiler emits. This one builds the
// blob once at boot into an EWRAM buffer, from the listing in script.c.
//
// What it does: the 16-byte header, the object table (component mask,
// sprite, six handler offsets per object) and the string table; labels that
// may be used before they are bound (forward jumps), patched at the end as
// rel16 offsets for JMP/JZ/JNZ and as absolute blob offsets for CALL; the
// string bytes, placed after all the code (so adding a string never moves
// code); and a final check that every label was bound and every string set.
// Anything wrong (a full buffer, a jump too far, an unbound label, an object
// or string out of range) makes asm_end return 0, with asm_error() saying
// what.

#ifndef FIREFLIES_ASM_H
#define FIREFLIES_ASM_H

#include "serval/serval.h"

typedef u16 AsmLabel;

// Starts a blob with the given numbers of objects, strings and globals; the
// object and string tables start out zeroed (no handlers, mask 0, sprite 0).
void asm_begin(u16 objects, u16 strings, u16 globals);
// Object `object`'s default component mask and sprite (SPAWN uses them).
void asm_object(u16 object, u32 components, u16 sprite);
// The code emitted next is object `object`'s handler for `event` (VM_EV_*).
void asm_handler(u16 object, u8 event);
// String `index`'s text, emitted after the code by asm_end. Must stay valid
// until then.
void asm_string(u16 index, const char* text);

// A new label, bound later with asm_bind (or never: asm_end reports it).
AsmLabel asm_label(void);
// Binds the label to the next byte emitted.
void asm_bind(AsmLabel label);

void asm_op(u32 op);                   // an opcode without operands
void asm_op8(u32 op, u32 operand);     // with a u8 operand (LDG, GETP, SYS, ...)
void asm_op16(u32 op, u32 operand);    // with a u16 operand (SPAWN, TRACE)
void asm_push(s32 value);              // the shortest of PUSH8, PUSH16 and PUSH32
void asm_jump(u32 op, AsmLabel label); // JMP, JZ or JNZ to the label
void asm_call(AsmLabel label);         // CALL the label

// Patches the jumps and calls, emits the strings and checks the blob. Returns
// its size and sets *blob, or returns 0 (see asm_error) if it is broken.
u32 asm_end(const u8** blob);
// What made asm_end fail (the first problem), or "" if nothing did.
const char* asm_error(void);

// The listing's mnemonics (script.c), one per opcode form: OP(ADD), PUSH(3),
// LDG(G_SCORE), GETP(X), SETP(BODY_W), SYS(TEXT_PRINT), SPAWN(OBJ_SPARKLE),
// JZ(label), CALL(label), LABEL(label) and HANDLER(OBJ_PLAYER, STEP).
#define OP(name) asm_op(VM_OP_##name)
#define PUSH(value) asm_push(value)
#define LDG(g) asm_op8(VM_OP_LDG, (g))
#define STG(g) asm_op8(VM_OP_STG, (g))
#define LDL(n) asm_op8(VM_OP_LDL, (n))
#define STL(n) asm_op8(VM_OP_STL, (n))
#define GETP(prop) asm_op8(VM_OP_GETP, VM_P_##prop)
#define SETP(prop) asm_op8(VM_OP_SETP, VM_P_##prop)
#define SYS(fn) asm_op8(VM_OP_SYS, VM_SYS_##fn)
#define SPAWN(object) asm_op16(VM_OP_SPAWN, (object))
#define JMP(label) asm_jump(VM_OP_JMP, (label))
#define JZ(label) asm_jump(VM_OP_JZ, (label))
#define JNZ(label) asm_jump(VM_OP_JNZ, (label))
#define CALL(label) asm_call(label)
#define LABEL(label) asm_bind(label)
#define HANDLER(object, event) asm_handler((object), VM_EV_##event)

#endif // FIREFLIES_ASM_H
