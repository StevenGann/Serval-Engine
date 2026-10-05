// Save memory on the GBA for SAVE FLASH64K and SAVE FLASH128K: the
// cartridge's Flash chip (64 KiB, or 128 KiB in two 64 KiB banks, at
// 0x0E000000), for src/core/save.c. Compiled once per type (CMakeLists.txt
// defines SERVAL_SAVE_FLASH64K or SERVAL_SAVE_FLASH128K); serval_add_rom()
// links the game's one.
//
// Following GBATEK ("GBA Cart Backup Flash ROM"):
// - Commands are byte writes of 0xAA to 0x5555 and 0x55 to 0x2AAA, then the
//   command to 0x5555: 0x90 chip ID, 0xF0 leave ID mode, 0xA0 program a byte,
//   0x80 then 0x30 to a sector's first byte erase a 4 KiB sector, 0xB0 then
//   the bank number to 0x0000 select a bank (128 KiB chips).
// - Programming only clears bits (erased bytes read 0xFF); save.c erases a
//   copy's sector before writing it, and clears magic bytes by programming
//   zeros, which needs no erase.
// - A program or erase is done when the byte reads back as written (0xFF for
//   an erase; the chip returns status bits until then), or failed after a
//   timeout: 10 ms per byte, 40 ms to 2 s per sector by chip. Macronix chips
//   then need 0xF0 written to 0x5555 to end the command.
// - "Reading anything (data or status) can be done only by opcodes executed
//   in WRAM, not from opcodes in ROM": every routine that reads the chip runs
//   from EWRAM (FLASH_RAM_CODE: about 300 bytes, copied there at startup by
//   crt0.s, in ROMs that use Flash saves only). Writes may run from ROM.
// - The chip's ID (manufacturer, device) picks the timeouts. Atmel chips
//   (ID 0x3D1F) write 128-byte pages instead of bytes and are not supported:
//   saves fail with a warning. Unknown IDs are used like the known ones, with
//   the longest timeouts (warning in debug builds).
// - The ID is read once, on the first save call: 20 ms in ID mode and 20 ms
//   after leaving it (Atmel's datasheet asks for the waits; harmless for the
//   others), and leaving ID mode is repeated on Sanyo chips, which need it.
//
// Timeouts count scanlines (VCOUNT, 1232 cycles each) rather than use a
// timer: no timer or interrupt is taken from the game. Wait states: SRAM
// region at 8 cycles (serval_init()), which every chip supports.

#include "../core/save_internal.h"
#include "../core/warn.h"

#include <tonc_memmap.h>

#if defined(SERVAL_SAVE_FLASH128K)
#define BANKS 2u
#define FLASH_ID_STRING "FLASH1M_V103"
#elif defined(SERVAL_SAVE_FLASH64K)
#define BANKS 1u
#define FLASH_ID_STRING "FLASH512_V131"
#else
#error "compile save_flash.c with SERVAL_SAVE_FLASH64K or SERVAL_SAVE_FLASH128K"
#endif

#define FLASH ((volatile u8*)0x0E000000)

#define BANK_SIZE 0x10000u
#define SECTOR_SIZE 0x1000u

// Scanlines (73.4 us each) for a number of milliseconds, rounded up.
#define LINES(ms) ((ms) * 14u)
#define PROGRAM_TIMEOUT LINES(10u)
#define ID_MODE_WAIT LINES(20u)
#define ERASE_RETRIES 3u

#ifdef SERVAL_GBA
#define FLASH_RAM_CODE __attribute__((section(".ewram.text"), long_call, noinline))
#else
#define FLASH_RAM_CODE
#endif

#define SAVE_DEVICE_SECTION __attribute__((section(".rodata.serval_save_device")))

// The ID string emulators and flash carts look for in the ROM to give the game
// Flash of this size (see save_sram.c).
SAVE_DEVICE_SECTION __attribute__((aligned(4))) static const char flash_id[16] = FLASH_ID_STRING;

enum { CHIP_MACRONIX = 1, CHIP_ATMEL = 2 };

typedef struct {
    u16 id; // device << 8 | manufacturer, as GBATEK lists them
    u8 banks;
    u8 flags;
    u16 erase_ms; // sector erase timeout
} Chip;

// GBATEK's chips. Its sector erase timeouts for the 128 KiB chips are unknown:
// they get the longest.
static const Chip chips[] = {
    {0xD4BF, 1, 0, 40},               // SST (and Sanyo's 64 KiB chip, same ID)
    {0x1CC2, 1, CHIP_MACRONIX, 2000}, // Macronix
    {0x1B32, 1, 0, 500},              // Panasonic
    {0x3D1F, 1, CHIP_ATMEL, 40},      // Atmel: 128-byte pages, unsupported
    {0x1362, 2, 0, 2000},             // Sanyo
    {0x09C2, 2, CHIP_MACRONIX, 2000}, // Macronix
};
static const Chip unknown_chip = {0, BANKS, 0, 2000};

enum { STATE_UNKNOWN, STATE_READY, STATE_UNUSABLE };
static SERVAL_EWRAM_BSS struct {
    const Chip* chip;
    u8 state;
    u8 bank; // the selected bank (128 KiB chips)
} flash;

// Inlined: the EWRAM routines must not call into ROM.
__attribute__((always_inline)) static inline void command(u8 cmd) {
    FLASH[0x5555] = 0xAA;
    FLASH[0x2AAA] = 0x55;
    FLASH[0x5555] = cmd;
}

static void select_bank(u32 bank) {
    command(0xB0);
    FLASH[0] = (u8)bank;
    flash.bank = (u8)bank;
}

// Waits for `lines` scanlines.
__attribute__((always_inline)) static inline void wait_lines(u32 lines) {
    u32 last = REG_VCOUNT;
    while (lines) {
        u32 now = REG_VCOUNT;
        if (now != last) {
            last = now;
            lines--;
        }
    }
}

// Waits until *p reads `value`, for at most `lines` scanlines. False on
// timeout.
__attribute__((always_inline)) static inline bool wait_for(volatile u8* p, u8 value, u32 lines) {
    u32 last = REG_VCOUNT;
    while (*p != value) {
        u32 now = REG_VCOUNT;
        if (now != last) {
            last = now;
            if (!lines--)
                return *p == value;
        }
    }
    return true;
}

// The chip's ID: device << 8 | manufacturer.
FLASH_RAM_CODE static u32 flash_read_id(void) {
    command(0x90);
    wait_lines(ID_MODE_WAIT);
    u32 id = (u32)FLASH[1] << 8 | FLASH[0];
    command(0xF0);
    if ((id & 0xFF) == 0x62) // Sanyo: leave ID mode twice
        FLASH[0x5555] = 0xF0;
    wait_lines(ID_MODE_WAIT);
    return id;
}

FLASH_RAM_CODE static void flash_copy(u8* dst, const volatile u8* src, u32 count) {
    for (u32 i = 0; i < count; i++)
        dst[i] = src[i];
}

// Programs one byte (in the selected bank). False on timeout.
FLASH_RAM_CODE static bool flash_program(volatile u8* p, u8 value) {
    command(0xA0);
    *p = value;
    return wait_for(p, value, PROGRAM_TIMEOUT);
}

// Erases the sector starting at p (in the selected bank) and checks that it
// reads 0xFF throughout. False on timeout or if a byte stayed programmed.
FLASH_RAM_CODE static bool flash_erase_sector(volatile u8* p, u32 timeout) {
    command(0x80);
    FLASH[0x5555] = 0xAA;
    FLASH[0x2AAA] = 0x55;
    *p = 0x30;
    if (!wait_for(p, 0xFF, timeout))
        return false;
    for (u32 i = 0; i < SECTOR_SIZE; i++)
        if (p[i] != 0xFF)
            return false;
    return true;
}

static const Chip* find_chip(u32 id) {
    for (u32 i = 0; i < sizeof chips / sizeof chips[0]; i++)
        if (chips[i].id == id)
            return &chips[i];
    return &unknown_chip;
}

// Identifies the chip on first use. False if saves can't use it.
static bool flash_ready(void) {
    if (flash.state != STATE_UNKNOWN)
        return flash.state == STATE_READY;
    flash.state = STATE_UNUSABLE;
    if (BANKS == 2) {
        // Leaves bank 0 selected. mGBA (0.10) reports a 64 KiB chip until
        // bank 1 has been selected once; real 64 KiB chips ignore this.
        select_bank(1);
        select_bank(0);
    }
    u32 id = flash_read_id();
    const Chip* chip = find_chip(id);
    if (chip->flags & CHIP_ATMEL) {
        SERVAL_WARN("save: the cartridge's Flash chip is an Atmel one (ID 0x%x), which Serval "
                    "doesn't support; saves are off (use another save type or cartridge)",
                    id);
        return false;
    }
    if (chip->banks < BANKS) {
        SERVAL_WARN("save: the cartridge's Flash chip (ID 0x%x) holds 64 KiB, but the game was "
                    "built with SAVE FLASH128K; saves are off (use SAVE FLASH64K)",
                    id);
        return false;
    }
    if (chip == &unknown_chip)
        SERVAL_WARN("save: unknown Flash chip ID 0x%x; using it like the known ones", id);
    if (chip->banks == 2 && BANKS == 1)
        select_bank(0); // a 128 KiB chip in a 64 KiB game
    flash.chip = chip;
    flash.state = STATE_READY;
    return true;
}

// Ends a timed-out command (Macronix chips need it).
static void flash_timed_out(void) {
    if (flash.chip->flags & CHIP_MACRONIX)
        FLASH[0x5555] = 0xF0;
}

static void flash_read(u32 offset, u8* dst, u32 count) {
    if (!flash_ready()) {
        for (u32 i = 0; i < count; i++)
            dst[i] = 0xFF; // reads as never written
        return;
    }
    while (count) {
        u32 bank = offset / BANK_SIZE, at = offset % BANK_SIZE;
        u32 n = BANK_SIZE - at < count ? BANK_SIZE - at : count;
        if (BANKS == 2 && bank != flash.bank)
            select_bank(bank);
        flash_copy(dst, FLASH + at, n);
        offset += n;
        dst += n;
        count -= n;
    }
}

static bool flash_write(u32 offset, const u8* src, u32 count) {
    if (!flash_ready())
        return false;
    for (u32 i = 0; i < count; i++) {
        if (src[i] == 0xFF)
            continue; // erased bytes already read 0xFF
        u32 bank = (offset + i) / BANK_SIZE;
        if (BANKS == 2 && bank != flash.bank)
            select_bank(bank);
        if (!flash_program(FLASH + (offset + i) % BANK_SIZE, src[i])) {
            flash_timed_out();
            return false;
        }
    }
    return true;
}

static bool flash_erase(u32 offset, u32 count) {
    if (!flash_ready())
        return false;
    for (u32 sector = offset / SECTOR_SIZE; sector * SECTOR_SIZE < offset + count; sector++) {
        u32 bank = sector * SECTOR_SIZE / BANK_SIZE;
        if (BANKS == 2 && bank != flash.bank)
            select_bank(bank);
        volatile u8* p = FLASH + sector * SECTOR_SIZE % BANK_SIZE;
        bool erased = false;
        for (u32 attempt = 0; attempt < ERASE_RETRIES && !erased; attempt++) {
            erased = flash_erase_sector(p, LINES(flash.chip->erase_ms));
            if (!erased)
                flash_timed_out();
        }
        if (!erased)
            return false;
    }
    return true;
}

SAVE_DEVICE_SECTION const SaveDevice serval_platform_save_device = {.read = flash_read,
                                                                    .write = flash_write,
                                                                    .erase = flash_erase,
                                                                    .id = flash_id,
                                                                    SAVE_LAYOUT(SERVAL_SAVE_TYPE)};
