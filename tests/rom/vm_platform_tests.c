// Tests for the VM's GBA engine calls (src/gba/vm_platform.c): what a script's
// SYS text calls leave in the text layer's map. The blob is hand-assembled
// (docs/vm.md "Blob format"); the shared suite (tests/vm_tests.c) covers the
// interpreter's side of the calls.

#include "../test.h"
#include "serval/debug.h"
#include "serval/text.h"
#include "serval/vm.h"

#include <tonc.h>

#define TEXT_MAP se_mem[31]
#define GLYPH(c) ((c) - ' ')
#define ENTRY(col, row) (TEXT_MAP[(row) * 32 + (col)] & SE_ID_MASK)

// One object whose Create prints -907 at (2, 3) with text_print_number and
// string 0, "HI", at (2, 4) with text_print.
static const u8 printer[] = {
    'S', 'V', 'M', 'B', VM_FORMAT_VERSION, VM_CELL_BYTES, 0, 0, // magic, version, cells, flags
    1, 0, 1, 0, 0, 0, 0, 0, // 1 object, 1 string, no globals, reserved
    // 0x10: object 0: mask 0, sprite 0, Create @ 0x34, no other handler
    0, 0, 0, 0, 0, 0, 0, 0, 0x34, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //
    0x46, 0, 0, 0,                                     // 0x30: string 0 @ 0x46
    VM_OP_PUSH8, 2,                                    // 0x34: col
    VM_OP_PUSH8, 3,                                    //       col row
    VM_OP_PUSH16, 0x75, 0xFC,                          //       col row -907
    VM_OP_SYS, VM_SYS_TEXT_PRINT_NUMBER,               //
    VM_OP_PUSH8, 2,                                    //       col
    VM_OP_PUSH8, 4,                                    //       col row
    VM_OP_PUSH8, 0,                                    //       col row string
    VM_OP_SYS, VM_SYS_TEXT_PRINT,                      //
    VM_OP_HALT,                                        //
    'H', 'I', 0,                                       // 0x46: string 0
};

static void text_calls_print(void) {
    text_clear();
    CHECK(vm_load(printer, sizeof printer));
    u32 before = debug_warning_count();
    CHECK(vm_start(0, VM_EV_CREATE) >= 0);
    vm_step();
    CHECK(vm_idle());
    CHECK(debug_warning_count() == before);
    CHECK(ENTRY(2, 3) == GLYPH('-'));
    CHECK(ENTRY(3, 3) == GLYPH('9'));
    CHECK(ENTRY(4, 3) == GLYPH('0'));
    CHECK(ENTRY(5, 3) == GLYPH('7'));
    CHECK(TEXT_MAP[3 * 32 + 6] == 0); // nothing after the number
    CHECK(ENTRY(2, 4) == GLYPH('H'));
    CHECK(ENTRY(3, 4) == GLYPH('I'));
    vm_unload();
    text_clear();
}

TEST_SUITE(gba_vm_tests, "gba_vm", {"text_calls_print", text_calls_print});
