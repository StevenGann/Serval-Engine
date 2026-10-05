// Save slots (save.h) on a byte-addressed save memory (save_internal.h).
//
// Format (docs/runtime-systems.md#save-data): the 32 KiB memory holds 8
// slots, each two 2048-byte copies, A and B. A copy is a 16-byte header, then
// the data (up to SAVE_SLOT_MAX bytes). The header, little-endian:
//
//   0  4 bytes  magic "SVS1": this copy holds a save
//   4  u32      sequence number: the newer copy has the higher number
//               (compared modulo 2^32)
//   8  u16      the game's save format version
//   10 u16      data size in bytes (1 to SAVE_SLOT_MAX)
//   12 u32      CRC-32 of bytes 4-11 and the data
//
// A copy is *empty* without the magic (never-written memory reads as 0xFF or
// garbage, an erased copy as zeros), *valid* if the size is in range and the
// CRC matches, and *damaged* otherwise. A slot holds the save in its newest
// valid copy; it reads as corrupt only if no copy is valid and one is damaged.
//
// save_write() writes the copy that does not hold the slot's save. It first
// clears that copy's magic, then writes the data and the rest of the header,
// and the magic last, so until the very last byte the copy reads as empty and
// the old save stays the slot's save; then it reads everything back.
// save_erase() clears the magic of the older copy first, then the newer one,
// so it is all or nothing too.

#include "serval/save.h"

#include "save_internal.h"
#include "warn.h"

_Static_assert(SAVE_SLOTS * 2 * SAVE_COPY_SIZE <= SAVE_MEMORY_SIZE, "slots must fit the memory");
_Static_assert(SAVE_HEADER_SIZE + SAVE_SLOT_MAX <= SAVE_COPY_SIZE, "a save must fit its copy");
_Static_assert(SAVE_SLOT_MAX <= 0xFFFF, "the header stores the size in 16 bits");

const SaveDevice* serval_save_device = &serval_platform_save_device;

static const u8 magic[4] = {'S', 'V', 'S', '1'};

// Bytes read or compared at a time (on the stack).
#define CHUNK 64u

// CRC-32, reflected polynomial 0xEDB88320, a byte at a time (the table is
// 1 KiB of ROM).
static const u32 crc_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

u32 serval_save_crc32(u32 crc, const u8* data, u32 count) {
    crc = ~crc;
    for (u32 i = 0; i < count; i++)
        crc = (crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xFF];
    return ~crc;
}

typedef struct {
    u32 seq;
    u32 crc;
    u16 version;
    u16 size;
} Header;

enum { COPY_EMPTY, COPY_DAMAGED, COPY_VALID };

static u32 get16(const u8* p) {
    return (u32)p[0] | (u32)p[1] << 8;
}

static u32 get32(const u8* p) {
    return get16(p) | get16(p + 2) << 16;
}

static void put16(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put32(u8* p, u32 v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

static u32 copy_offset(u32 copy) {
    return copy * SAVE_COPY_SIZE;
}

static bool has_magic(const u8* bytes) {
    return bytes[0] == magic[0] && bytes[1] == magic[1] && bytes[2] == magic[2] &&
           bytes[3] == magic[3];
}

// Reads a copy's header. False if it has no magic (an empty copy).
static bool read_header(u32 copy, Header* h) {
    u8 bytes[SAVE_HEADER_SIZE];
    serval_save_device->read(copy_offset(copy), bytes, SAVE_HEADER_SIZE);
    if (!has_magic(bytes))
        return false;
    h->seq = get32(bytes + 4);
    h->version = (u16)get16(bytes + 8);
    h->size = (u16)get16(bytes + 10);
    h->crc = get32(bytes + 12);
    return true;
}

// Whether a copy with this header (and magic) is intact: its size is in range
// and the CRC over the header fields and the data in memory matches.
static bool copy_intact(u32 copy, const Header* h) {
    if (h->size == 0 || h->size > SAVE_SLOT_MAX)
        return false;
    u8 bytes[CHUNK];
    put32(bytes, h->seq);
    put16(bytes + 4, h->version);
    put16(bytes + 6, h->size);
    u32 crc = serval_save_crc32(0, bytes, 8);
    u32 offset = copy_offset(copy) + SAVE_HEADER_SIZE;
    for (u32 done = 0; done < h->size; done += CHUNK) {
        u32 n = h->size - done < CHUNK ? h->size - done : CHUNK;
        serval_save_device->read(offset + done, bytes, n);
        crc = serval_save_crc32(crc, bytes, n);
    }
    return crc == h->crc;
}

// Finds a slot's save: its newest valid copy (*copy and *h, returns
// COPY_VALID), or returns COPY_DAMAGED if no copy is valid but one has the
// magic, else COPY_EMPTY.
static int find_save(u32 slot, u32* copy, Header* h) {
    u32 first = slot * 2;
    Header headers[2] = {{0}, {0}};
    bool present[2] = {read_header(first, &headers[0]), read_header(first + 1, &headers[1])};
    if (!present[0] && !present[1])
        return COPY_EMPTY;
    // Newest first: B if its sequence number is ahead of A's (modulo 2^32).
    u32 order[2] = {0, 1};
    if (present[0] && present[1] && (s32)(headers[1].seq - headers[0].seq) > 0) {
        order[0] = 1;
        order[1] = 0;
    }
    for (u32 i = 0; i < 2; i++) {
        u32 c = order[i];
        if (present[c] && copy_intact(first + c, &headers[c])) {
            *copy = first + c;
            *h = headers[c];
            return COPY_VALID;
        }
    }
    return COPY_DAMAGED;
}

// Debug warnings, once per problem and function.
enum { FN_WRITE, FN_READ, FN_VERSION, FN_SIZE, FN_ERASE };
enum { BAD_SLOT, BAD_SIZE, BAD_DATA, NOT_WRITTEN, PROBLEMS };
#ifdef SERVAL_DEBUG
static const char* const fn_names[] = {"save_write", "save_read", "save_slot_version",
                                       "save_slot_size", "save_erase"};
static u32 warned;

static bool first_warning(u32 fn, u32 problem) {
    u32 bit = 1u << (fn * PROBLEMS + problem);
    if (warned & bit)
        return false;
    warned |= bit;
    return true;
}
#endif

static bool slot_ok(u32 fn, u32 slot) {
    if (slot < SAVE_SLOTS)
        return true;
#ifdef SERVAL_DEBUG
    if (first_warning(fn, BAD_SLOT))
        SERVAL_WARN("%s: there is no slot %u; slots are 0 to %u (SAVE_SLOTS - 1)", fn_names[fn],
                    slot, SAVE_SLOTS - 1);
#endif
    (void)fn;
    return false;
}

static bool data_ok(u32 fn, u32 slot, const void* data) {
    if (serval_plausible_pointer(data))
        return true;
#ifdef SERVAL_DEBUG
    if (first_warning(fn, BAD_DATA))
        SERVAL_WARN("%s(slot %u): data (0x%x) is not a pointer to the game's save struct",
                    fn_names[fn], slot, (u32)(uintptr_t)data);
#endif
    (void)fn;
    (void)slot;
    return false;
}

static bool size_ok(u32 fn, u32 slot, u32 size) {
    if (size != 0 && size <= SAVE_SLOT_MAX)
        return true;
#ifdef SERVAL_DEBUG
    if (first_warning(fn, BAD_SIZE))
        SERVAL_WARN("%s(slot %u): size %u; it must be 1 to %u bytes (SAVE_SLOT_MAX)%s",
                    fn_names[fn], slot, size, SAVE_SLOT_MAX,
                    fn == FN_WRITE ? ", nothing saved" : "");
#endif
    (void)fn;
    (void)slot;
    return false;
}

static void warn_not_written(u32 fn, u32 slot) {
#ifdef SERVAL_DEBUG
    if (first_warning(fn, NOT_WRITTEN))
        SERVAL_WARN("%s(slot %u): the save memory didn't keep what was written (no save RAM?)%s",
                    fn_names[fn], slot, fn == FN_WRITE ? "; the old save is kept" : "");
#endif
    (void)fn;
    (void)slot;
}

static void clear_magic(u32 copy) {
    static const u8 zeros[sizeof magic] = {0};
    serval_save_device->write(copy_offset(copy), zeros, sizeof zeros);
}

static void flush(void) {
    if (serval_save_device->flush)
        serval_save_device->flush();
}

// Whether memory at offset holds exactly bytes[0..count).
static bool memory_matches(u32 offset, const u8* bytes, u32 count) {
    u8 chunk[CHUNK];
    for (u32 done = 0; done < count; done += CHUNK) {
        u32 n = count - done < CHUNK ? count - done : CHUNK;
        serval_save_device->read(offset + done, chunk, n);
        for (u32 i = 0; i < n; i++)
            if (chunk[i] != bytes[done + i])
                return false;
    }
    return true;
}

bool save_write(u32 slot, const void* data, u32 size, u16 version) {
    if (!slot_ok(FN_WRITE, slot) || !size_ok(FN_WRITE, slot, size) ||
        !data_ok(FN_WRITE, slot, data))
        return false;

    u32 current;
    Header old;
    u32 target, seq;
    if (find_save(slot, &current, &old) == COPY_VALID) {
        target = current ^ 1;
        seq = old.seq + 1;
    } else {
        target = slot * 2;
        seq = 1;
    }

    u8 header[SAVE_HEADER_SIZE];
    for (u32 i = 0; i < sizeof magic; i++)
        header[i] = magic[i];
    put32(header + 4, seq);
    put16(header + 8, version);
    put16(header + 10, size);
    put32(header + 12, serval_save_crc32(serval_save_crc32(0, header + 4, 8), data, size));

    u32 offset = copy_offset(target);
    clear_magic(target);
    serval_save_device->write(offset + SAVE_HEADER_SIZE, data, size);
    serval_save_device->write(offset + sizeof magic, header + sizeof magic,
                              SAVE_HEADER_SIZE - sizeof magic);
    serval_save_device->write(offset, header, sizeof magic);

    bool ok = memory_matches(offset, header, SAVE_HEADER_SIZE) &&
              memory_matches(offset + SAVE_HEADER_SIZE, data, size);
    if (!ok) {
        clear_magic(target); // the old save stays the slot's save
        warn_not_written(FN_WRITE, slot);
    }
    flush();
    return ok;
}

int save_read(u32 slot, void* data, u32 size, u16 version) {
    if (!slot_ok(FN_READ, slot) || !data_ok(FN_READ, slot, data))
        return SAVE_EMPTY;
    size_ok(FN_READ, slot, size); // a bad size just never matches

    u32 copy;
    Header h;
    int found = find_save(slot, &copy, &h);
    if (found == COPY_EMPTY)
        return SAVE_EMPTY;
    if (found == COPY_DAMAGED)
        return SAVE_CORRUPT;
    if (h.version != version || h.size != size)
        return SAVE_OTHER_VERSION;
    serval_save_device->read(copy_offset(copy) + SAVE_HEADER_SIZE, data, size);
    return SAVE_OK;
}

u16 save_slot_version(u32 slot) {
    u32 copy;
    Header h;
    if (!slot_ok(FN_VERSION, slot) || find_save(slot, &copy, &h) != COPY_VALID)
        return 0;
    return h.version;
}

u32 save_slot_size(u32 slot) {
    u32 copy;
    Header h;
    if (!slot_ok(FN_SIZE, slot) || find_save(slot, &copy, &h) != COPY_VALID)
        return 0;
    return h.size;
}

void save_erase(u32 slot) {
    if (!slot_ok(FN_ERASE, slot))
        return;
    u32 first = slot * 2;
    Header a = {0}, b = {0};
    bool has_a = read_header(first, &a), has_b = read_header(first + 1, &b);
    if (!has_a && !has_b)
        return;
    // The older copy first: if power fails in between, the slot still reads
    // as before the erase.
    if (has_a && has_b && (s32)(b.seq - a.seq) > 0) {
        clear_magic(first);
        clear_magic(first + 1);
    } else {
        if (has_b)
            clear_magic(first + 1);
        if (has_a)
            clear_magic(first);
    }
    if (read_header(first, &a) || read_header(first + 1, &b))
        warn_not_written(FN_ERASE, slot);
    flush();
}
