@ Startup code for Game Boy Advance cartridge ROMs.
@
@ Holds the ROM header (filled in after linking by tools/gbafix.py), sets up
@ the stacks, copies initialized RAM sections from ROM, zeroes .bss/.sbss,
@ runs static constructors and calls main().

    .section .crt0, "ax", %progbits
    .arm
    .align 2
    .global _start
_start:
    b       .Lreset                 @ 0x00 entry point

    @ 0x04 Nintendo logo: intentionally left empty (see docs/licensing.md).
    .fill   156, 1, 0
    .fill   12, 1, 0                @ 0xA0 game title
    .fill   4, 1, 0                 @ 0xAC game code
    .fill   2, 1, 0                 @ 0xB0 maker code
    .byte   0x96                    @ 0xB2 fixed value
    .byte   0x00                    @ 0xB3 main unit code
    .byte   0x00                    @ 0xB4 device type
    .fill   7, 1, 0                 @ 0xB5 reserved
    .byte   0x00                    @ 0xBC software version
    .byte   0x00                    @ 0xBD header checksum
    .fill   2, 1, 0                 @ 0xBE reserved

.Lreset:
    @ IRQ mode stack, then stay in system mode for main().
    mov     r0, #0x12
    msr     cpsr_c, r0
    ldr     sp, =__sp_irq
    mov     r0, #0x1F
    msr     cpsr_c, r0
    ldr     sp, =__sp_usr

    @ Initialized IWRAM (code and .data) and EWRAM sections.
    ldr     r0, =__iwram_lma
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
    bl      .Lcopy
    ldr     r0, =__ewram_lma
    ldr     r1, =__ewram_start
    ldr     r2, =__ewram_end
    bl      .Lcopy

    @ Zero-initialized sections.
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    bl      .Lzero
    ldr     r0, =__sbss_start
    ldr     r1, =__sbss_end
    bl      .Lzero

    @ Static constructors (none in plain C, but cheap to support).
    ldr     r4, =__init_array_start
    ldr     r5, =__init_array_end
1:  cmp     r4, r5
    bhs     2f
    ldr     r3, [r4], #4
    mov     lr, pc
    bx      r3
    b       1b

    @ main() is Thumb code; bx switches state on the address's low bit.
2:  mov     r0, #0
    mov     r1, #0
    ldr     r3, =main
    mov     lr, pc
    bx      r3

    @ main() should never return; if it does, halt here.
3:  b       3b

@ Copy words from [r0] to [r1] until r1 reaches r2.
.Lcopy:
    cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     .Lcopy
    bx      lr

@ Zero words from r0 until it reaches r1.
.Lzero:
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bx      lr

    .pool
