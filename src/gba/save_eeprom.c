// Save memory on the GBA for SAVE EEPROM8K and SAVE EEPROM512: the
// cartridge's serial EEPROM, for src/core/save.c. Compiled once per type
// (CMakeLists.txt defines SERVAL_SAVE_EEPROM8K or SERVAL_SAVE_EEPROM512);
// serval_add_rom() links the game's one.
//
// Following GBATEK ("GBA Cart Backup EEPROM"):
// - The EEPROM is read and written in 64-bit blocks, one bit per halfword
//   (bit 0) streamed through DMA3 to or from 0x0DFFFF00 (which works for any
//   ROM up to 32 MiB minus 256 bytes), with the cartridge's wait state 2 at 8
//   cycles (WAITCNT; set before every transfer).
// - Read a block: send "11", the block number (6 bits for 512 bytes, 14 bits
//   for 8 KiB, most significant first) and a "0"; then receive 68 bits: 4 to
//   ignore and the block's 64, most significant first.
// - Write a block: send "10", the block number, the 64 bits and a "0"; then
//   read halfwords until bit 0 is 1 (about 6.5 ms), with a timeout (GBATEK:
//   10 ms or longer; here 15 ms).
// - Interrupts are off while DMA3 is set up and runs, so a handler using
//   DMA3 can't interfere.
//
// Byte i of a block is bits 8i to 8i + 7 of the stream, most significant
// first: the order mGBA keeps in its .sav files, so they hold the slots byte
// for byte as on SRAM. Blocks that already hold what save.c writes are
// skipped (a block read costs 0.12-0.19 ms by build, a write 6.5 ms and
// wear).
//
// Timeouts count scanlines (VCOUNT), so no timer is taken from the game.

#include "../core/save_internal.h"

#include <tonc_memmap.h>

#if defined(SERVAL_SAVE_EEPROM8K)
#define ADDRESS_BITS 14u
#elif defined(SERVAL_SAVE_EEPROM512)
#define ADDRESS_BITS 6u
#else
#error "compile save_eeprom.c with SERVAL_SAVE_EEPROM8K or SERVAL_SAVE_EEPROM512"
#endif

#define EEPROM ((volatile u16*)0x0DFFFF00)

#define BLOCK 8u
#define DMA3_START 0x80000000u // 16-bit units, both addresses incrementing, start now
#define WS2_8_CYCLES 0x0300u   // wait state 2: 8 cycles first access, 8 sequential
#define WS2_MASK 0x0700u
#define WRITE_TIMEOUT 205u // scanlines: 15 ms

#define SAVE_DEVICE_SECTION __attribute__((section(".rodata.serval_save_device")))

// The ID string emulators and flash carts look for in the ROM to give the game
// EEPROM (see save_sram.c). The size is told apart by the address width.
SAVE_DEVICE_SECTION __attribute__((aligned(4))) static const char eeprom_id[12] = "EEPROM_V124";

// Streams count halfwords from src to dst through DMA3, interrupts off.
static void dma3(volatile const void* src, volatile void* dst, u32 count) {
    u16 ime = REG_IME;
    REG_IME = 0;
    REG_WAITCNT = (u16)((REG_WAITCNT & ~WS2_MASK) | WS2_8_CYCLES);
    REG_DMA3SAD = (u32)(uintptr_t)src;
    REG_DMA3DAD = (u32)(uintptr_t)dst;
    REG_DMA3CNT = DMA3_START | count;
    while (REG_DMA3CNT & DMA3_START) {
    }
    REG_IME = ime;
}

// Puts the request bits ("1", then `second`, then the block number) in bits.
static u32 request(u16* bits, u32 second, u32 block) {
    u32 n = 0;
    bits[n++] = 1;
    bits[n++] = (u16)second;
    for (u32 b = ADDRESS_BITS; b-- > 0;)
        bits[n++] = (u16)(block >> b & 1);
    return n;
}

static void read_block(u32 block, u8* out) {
    u16 bits[68];
    u32 n = request(bits, 1, block);
    bits[n++] = 0;
    dma3(bits, EEPROM, n);
    dma3(EEPROM, bits, 68);
    for (u32 i = 0; i < BLOCK; i++) {
        u32 byte = 0;
        for (u32 j = 0; j < 8; j++)
            byte = byte << 1 | (bits[4 + i * 8 + j] & 1u);
        out[i] = (u8)byte;
    }
}

static bool write_block(u32 block, const u8* in) {
    u16 bits[2 + ADDRESS_BITS + 64 + 1];
    u32 n = request(bits, 0, block);
    for (u32 i = 0; i < BLOCK; i++)
        for (u32 j = 8; j-- > 0;)
            bits[n++] = (u16)(in[i] >> j & 1);
    bits[n++] = 0;
    dma3(bits, EEPROM, n);
    // Busy (bit 0 clear) until the block is written.
    u32 last = REG_VCOUNT, lines = WRITE_TIMEOUT;
    while (!(*EEPROM & 1)) {
        u32 now = REG_VCOUNT;
        if (now != last) {
            last = now;
            if (!lines--)
                return (*EEPROM & 1) != 0;
        }
    }
    return true;
}

static void eeprom_read(u32 offset, u8* dst, u32 count) {
    u8 block[BLOCK];
    while (count) {
        u32 at = offset % BLOCK, n = BLOCK - at < count ? BLOCK - at : count;
        read_block(offset / BLOCK, block);
        for (u32 i = 0; i < n; i++)
            dst[i] = block[at + i];
        offset += n;
        dst += n;
        count -= n;
    }
}

static bool eeprom_write(u32 offset, const u8* src, u32 count) {
    if (offset % BLOCK || count % BLOCK)
        return false; // save.c writes whole blocks
    for (u32 done = 0; done < count; done += BLOCK) {
        u8 current[BLOCK];
        read_block((offset + done) / BLOCK, current);
        bool same = true;
        for (u32 i = 0; i < BLOCK; i++)
            same &= current[i] == src[done + i];
        if (!same && !write_block((offset + done) / BLOCK, src + done))
            return false;
    }
    return true;
}

SAVE_DEVICE_SECTION const SaveDevice serval_platform_save_device = {
    .read = eeprom_read, .write = eeprom_write, .id = eeprom_id, SAVE_LAYOUT(SERVAL_SAVE_TYPE)};
