// Tests for the VM's GBA engine calls (src/gba/vm_platform.c): what a script's
// SYS text calls leave in the text layer's map. The blob is hand-assembled
// (docs/vm.md "Blob format"); the shared suite (tests/vm_tests.c) covers the
// interpreter's side of the calls, and vm_collide's rules. Also the cost of
// vm_collide's pass on the hardware, logged.

#include "../test.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/physics.h"
#include "serval/text.h"
#include "serval/vm.h"

#include <tonc.h>

#define TEXT_MAP se_mem[31]
#define GLYPH(c) ((c) - ' ')
#define ENTRY(col, row) (TEXT_MAP[(row) * 32 + (col)] & SE_ID_MASK)

// One object whose Create prints -907 at (2, 3) with text_print_number (width
// 0: just the digits) and string 0, "HI", at (2, 4) with text_print.
static const u8 printer[] = {
    'S', 'V', 'M', 'B', VM_FORMAT_VERSION, VM_CELL_BYTES, 0, 0, // magic, version, cells, flags
    1, 0, 1, 0, 0, 0, 0, 0, // 1 object, 1 string, no globals, no arrays
    // 0x10: object 0: mask 0, sprite 0, Create @ 0x34, no other handler
    0, 0, 0, 0, 0, 0, 0, 0, 0x34, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //
    0x48, 0, 0, 0,                                     // 0x30: string 0 @ 0x48
    VM_OP_PUSH8, 2,                                    // 0x34: col
    VM_OP_PUSH8, 3,                                    //       col row
    VM_OP_PUSH16, 0x75, 0xFC,                          //       col row -907
    VM_OP_PUSH8, 0,                                    //       col row -907 0
    VM_OP_SYS, VM_SYS_TEXT_PRINT_NUMBER,               //
    VM_OP_PUSH8, 2,                                    //       col
    VM_OP_PUSH8, 4,                                    //       col row
    VM_OP_PUSH8, 0,                                    //       col row string
    VM_OP_SYS, VM_SYS_TEXT_PRINT,                      //
    VM_OP_HALT,                                        //
    'H', 'I', 0,                                       // 0x48: string 0
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

// A blob whose Create prints the cells of global 0 (value) at (glob[1],
// glob[2]) with text_print_number, width glob[3].
static const u8 number_printer[] = {
    'S', 'V', 'M', 'B', VM_FORMAT_VERSION, VM_CELL_BYTES, 0, 0, // magic, version, cells, flags
    1, 0, 0, 0, 4, 0, 0, 0, // 1 object, no strings, 4 globals, no arrays
    // 0x10: object 0: mask 0, sprite 0, Create @ 0x30
    0, 0, 0, 0, 0, 0, 0, 0, 0x30, 0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //
    VM_OP_LDG, 1,                                      // 0x30: col
    VM_OP_LDG, 2,                                      //       col row
    VM_OP_LDG, 0,                                      //       col row value
    VM_OP_LDG, 3,                                      //       col row value width
    VM_OP_SYS, VM_SYS_TEXT_PRINT_NUMBER,               //
    VM_OP_HALT,                                        //
};

// Prints value at (col, row) in `width` columns through a script.
static void print_number(s32 col, s32 row, s32 value, s32 width) {
    vm_set_global(0, value);
    vm_set_global(1, col);
    vm_set_global(2, row);
    vm_set_global(3, width);
    CHECK(vm_start(0, VM_EV_CREATE) >= 0);
    vm_step();
}

// True if row's cells from col on read `text` (' ': a blank cell, tile 0).
static bool row_reads(int col, int row, const char* text) {
    for (; *text; text++, col++)
        if (ENTRY(col, row) != (u32)GLYPH(*text))
            return false;
    return true;
}

// vm.md "Engine calls": with a width of 1 or more, text_print_number right-
// aligns the number in that many columns, blanks in front, so a number that
// got shorter leaves nothing behind; a number wider than the width prints in
// full; a width of 0 or less prints just the digits. Columns off the screen
// are clipped, as text_print clips.
static void text_print_number_width(void) {
    text_clear();
    CHECK(vm_load(number_printer, sizeof number_printer));
    u32 before = debug_warning_count();
    print_number(1, 2, 10, 4); // "  10"
    CHECK(row_reads(0, 2, "   10 "));
    print_number(1, 2, 9, 4); // "   9": the 1 is gone
    CHECK(row_reads(0, 2, "    9 "));
    print_number(1, 3, -12345, 3); // wider than 3: in full
    CHECK(row_reads(0, 3, " -12345 "));
    print_number(1, 4, 7, 1); // exactly as wide
    CHECK(row_reads(0, 4, " 7 "));
    print_number(5, 5, 88, 2);
    print_number(5, 5, 3, 0); // width 0: just the digits, nothing blanked
    CHECK(row_reads(4, 5, " 38 "));
    print_number(5, 5, 4, -7); // negative: the same
    CHECK(row_reads(4, 5, " 48 "));
    print_number(-3, 6, 123, 6); // starts off the left edge: "   123" from -3
    CHECK(row_reads(0, 6, "123 "));
    print_number(-2, 6, 5, 3); // only the digit is on the screen
    CHECK(row_reads(0, 6, "523 "));
    print_number(TEXT_COLS - 5, 7, 99999, 0); // the row's last five columns
    print_number(TEXT_COLS - 2, 7, 4567, 6);  // blanks two; the digits are off the edge
    CHECK(row_reads(TEXT_COLS - 6, 7, " 999"));
    CHECK(ENTRY(TEXT_COLS - 2, 7) == 0 && ENTRY(TEXT_COLS - 1, 7) == 0);
    print_number(INT32_MIN, 8, 1, INT32_MAX); // ends at INT32_MIN + INT32_MAX: off the left
    print_number(INT32_MAX, 8, 1, INT32_MAX); // off the right
    print_number(-2147483600, 8, 42, 2147483600 + 10); // ends at column 10: "42" at 8
    CHECK(row_reads(0, 8, "        42 "));
    CHECK(vm_idle());
    CHECK(debug_warning_count() == before);
    vm_unload();
    text_clear();
}

// CPU cycles, from the cascaded timers serval_init() starts.
static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Cycle budgets only hold for optimized code: Debug builds (-O0) check
// correctness but not timing.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

#define C_SHOT C_GAME(0)
#define C_ENEMY C_GAME(1)

// `count` 8x8 bodies with the component `tag`, 10 pixels apart in a row at y:
// none touches another.
static void make_row(u32 tag, u32 count, s32 y) {
    for (u32 k = 0; k < count; k++) {
        u32 i = entity_index(entity_create(C_POS | C_BODY | tag));
        pos_x[i] = FX((s32)k * 10);
        pos_y[i] = FX(y);
        body_w[i] = body_h[i] = 8;
    }
}

// vm_events() with its queue empty and the pairs set: the cycles the pass
// costs, without reactions (nothing overlaps).
static u32 events_cycles(void) {
    u32 t0 = cycles();
    vm_events();
    return cycles() - t0;
}

// vm.h vm_collide "Cost": what the collision pass costs on the GBA, logged.
// A pass over one pair lists both sets and tests every entity of one against
// every one of the other: measured with 1 x 8 bodies (fireflies' player and
// fireflies) and 10 x 20 (a shooter's shots and enemies), in a pool filled
// with 64 other entities, none overlapping.
static void collide_costs(void) {
    ecs_reset();
    CHECK(vm_load(printer, sizeof printer)); // any blob: no blob, no pass
    for (u32 k = 0; k < 64; k++)
        entity_create(C_POS);
    make_row(C_SHOT, 1, 0);
    make_row(C_ENEMY, 8, 20);
    u32 none = events_cycles();
    CHECK(vm_collide(C_SHOT, C_ENEMY));
    u32 small = events_cycles();
    make_row(C_SHOT, 9, 40);
    make_row(C_ENEMY, 12, 60);
    u32 large = events_cycles();
    u32 before = debug_warning_count();
    // Per test: the difference between 200 and 8 tests, the same two lists.
    u32 per_test = (large - small) / (200 - 8);
    debug_log(text_format("vm_collide: vm_events with no pair %u cycles; one pair, 1 x 8 bodies "
                          "%u, 10 x 20 %u: about %u per test",
                          none, small, large, per_test));
    CHECK(debug_warning_count() == before);
    // About 3,000 and 17,100 more than with no pair when this was written
    // (gba-ci, mGBA): two lists of about 1,000 cycles each, then about 75
    // cycles per test.
    CHECK_TIMING(small - none < 4000);
    CHECK_TIMING(per_test < 100);
    vm_collide_clear();
    vm_unload();
    ecs_reset();
}

TEST_SUITE(gba_vm_tests, "gba_vm", {"text_calls_print", text_calls_print},
           {"text_print_number_width", text_print_number_width}, {"collide_costs", collide_costs});
