// The GBA's clock: the Seiko S-3511A some cartridges carry, on the cartridge's
// GPIO port, for src/core/rtc.c (which finds it and checks what it says).
//
// The port is three 16-bit registers in the ROM's address space: data (the
// pins' levels), direction (1 = the GBA drives the pin) and control (1 = the
// registers can be read; 0 = reads there return the ROM's own bytes, which
// crt0 keeps at zero). Only 16-bit stores reach it. The clock's pins are 0
// (SCK, the serial clock), 1 (SIO, data both ways) and 2 (CS, select); pin 3
// belongs to whatever else the cartridge has there (rumble, a light sensor),
// so each transaction keeps its direction and level as it found them.
//
// A transaction: CS rises while SCK is high; then each bit is SIO set while
// SCK is low and taken by the chip as SCK rises (the GBA's bits), or put on
// SIO by the chip as SCK rises and read after (the chip's: SIO is then an
// input); CS falls to end it. The command byte goes most significant bit
// first, `0110 ccc r` (r = 1 to read); data bytes least significant bit first.
//
// Timing (the datasheet's figures are not at hand, so these copy margins that
// work on hardware): each SCK-low store is repeated, 3 stores (4 when the chip
// answers, so SIO has turned around), about 1 microsecond at 3/1 wait states;
// after a write the chip is left alone for about 200 microseconds. Interrupts
// are off for each transaction (REG_IME, restored after), so a handler can't
// stretch a bit. Not hot: a read is a few thousand cycles (a date-time read
// 3,600 to 4,700, interrupts off for most of it), so this stays Thumb code in ROM.
//
// Without a clock the stores go nowhere and every bit reads as the ROM's zero:
// the status reads 0 and src/core/rtc.c finds no clock.

#include "../core/rtc_internal.h"

#include <tonc.h>

#define GPIO_DATA (*(volatile u16*)0x080000C4)
#define GPIO_DIR (*(volatile u16*)0x080000C6)
#define GPIO_CTRL (*(volatile u16*)0x080000C8)

#define PIN_SCK 1
#define PIN_SIO 2
#define PIN_CS 4
#define PIN_OTHER 8 // pin 3: not the clock's

#define CMD_RESET 0x60
#define CMD_STATUS_WRITE 0x62
#define CMD_STATUS_READ 0x63
#define CMD_DATETIME_READ 0x65

// About 200 microseconds, in CPU cycles (16.78 MHz).
#define SETTLE_CYCLES 3400
// The wait's bound if timer 2 isn't running: each iteration takes several
// cycles, so the timer ends the wait first when it runs.
#define SETTLE_MAX_ITERATIONS 3400

// The ID string emulators look for in the ROM to give the game a clock
// (Nintendo's library put "SIIRTC_V" and its version there; scanners match it
// on a 4-byte boundary). It shares one section with the device below, which
// src/core/rtc.c references: --gc-sections keeps both or neither, so only
// games that use the clock carry it. tools/check-rom.py looks for this section
// in the link map.
#define RTC_SECTION __attribute__((section(".rodata.serval_rtc")))

RTC_SECTION __attribute__((aligned(4))) static const char rtc_id[12] = "SIIRTC_V001";

// One transaction's state: interrupts as it found them, and pin 3's direction
// bit and, if the GBA drives it, its level. On the stack: the clock keeps
// nothing in IWRAM.
typedef struct Port {
    u16 ime;
    u16 other_dir;
    u16 other_level;
} Port;

static void begin(Port* p) {
    p->ime = REG_IME;
    REG_IME = 0;
    GPIO_CTRL = 1;
    p->other_dir = GPIO_DIR & PIN_OTHER;
    p->other_level = GPIO_DATA & p->other_dir;
    GPIO_DIR = p->other_dir | PIN_CS | PIN_SIO | PIN_SCK;
    // CS low, then high, with SCK high.
    GPIO_DATA = p->other_level | PIN_SCK;
    GPIO_DATA = p->other_level | PIN_SCK;
    GPIO_DATA = p->other_level | PIN_CS | PIN_SCK;
}

static void send_bit(const Port* p, u32 bit) {
    u16 low = p->other_level | PIN_CS | (u16)(bit << 1);
    GPIO_DATA = low;
    GPIO_DATA = low;
    GPIO_DATA = low;
    GPIO_DATA = low | PIN_SCK;
}

static void send_command(const Port* p, u8 command) {
    for (int i = 7; i >= 0; i--)
        send_bit(p, (command >> i) & 1);
}

static void send_byte(const Port* p, u8 byte) {
    for (int i = 0; i < 8; i++)
        send_bit(p, (byte >> i) & 1);
}

// The chip answers on SIO: the GBA stops driving it.
static void turn_around(const Port* p) {
    GPIO_DIR = p->other_dir | PIN_CS | PIN_SCK;
}

static u8 receive_byte(const Port* p) {
    u16 low = p->other_level | PIN_CS;
    u32 byte = 0;
    for (int i = 0; i < 8; i++) {
        GPIO_DATA = low;
        GPIO_DATA = low;
        GPIO_DATA = low;
        GPIO_DATA = low;
        GPIO_DATA = low | PIN_SCK;
        byte |= (u32)((GPIO_DATA >> 1) & 1) << i;
    }
    return (u8)byte;
}

// CS low with SCK high ends the transaction; the port closes (control 0), so
// reads at 0x080000C4-C9 are the ROM's again.
static void end(const Port* p) {
    GPIO_DIR = p->other_dir | PIN_CS | PIN_SCK;
    GPIO_DATA = p->other_level | PIN_SCK;
    GPIO_DATA = p->other_level | PIN_SCK;
    GPIO_CTRL = 0;
    REG_IME = p->ime;
}

// Leaves the chip alone for about 200 microseconds after a write. Timer 2 is
// the low half of the engine's cycle counter (serval_init()); the iteration
// bound ends the wait even if it isn't running.
static void settle(void) {
    u16 start = REG_TM2D;
    for (u32 n = 0; n < SETTLE_MAX_ITERATIONS; n++)
        if ((u16)(REG_TM2D - start) >= SETTLE_CYCLES)
            break;
}

static u8 gba_read_status(void) {
    Port p;
    begin(&p);
    send_command(&p, CMD_STATUS_READ);
    turn_around(&p);
    u8 status = receive_byte(&p);
    end(&p);
    return status;
}

static void gba_write_status(u8 status) {
    // The interrupt bits stay 0: the cartridge interrupt is the game's.
    Port p;
    begin(&p);
    send_command(&p, CMD_STATUS_WRITE);
    send_byte(&p, status);
    end(&p);
    settle();
}

static void gba_reset(void) {
    Port p;
    begin(&p);
    send_command(&p, CMD_RESET);
    end(&p);
    settle();
}

static void gba_read_datetime(u8 raw[7]) {
    Port p;
    begin(&p);
    send_command(&p, CMD_DATETIME_READ);
    turn_around(&p);
    for (int i = 0; i < 7; i++)
        raw[i] = receive_byte(&p);
    end(&p);
}

RTC_SECTION const RtcDevice serval_platform_rtc_device = {.read_status = gba_read_status,
                                                          .write_status = gba_write_status,
                                                          .reset = gba_reset,
                                                          .read_datetime = gba_read_datetime,
                                                          .id = rtc_id};
