// The album: see album.h.

#include "album.h"

#include <string.h>

static AlbumEntry entries[ALBUM_MAX] SERVAL_EWRAM_BSS; // 448 bytes: not in IWRAM
static u8 count;

void album_add(const AlbumEntry* e) {
    int keep = count < ALBUM_MAX ? count : ALBUM_MAX - 1;
    memmove(&entries[1], &entries[0], keep * sizeof entries[0]);
    entries[0] = *e;
    count = (u8)(keep + 1);
}

u8 album_count(void) {
    return count;
}

const AlbumEntry* album_get(u8 i) {
    return i < count ? &entries[i] : 0;
}

void album_set(const AlbumEntry* in, u8 n) {
    if (n > ALBUM_MAX)
        n = ALBUM_MAX;
    if (n)
        memcpy(entries, in, n * sizeof entries[0]);
    count = n;
}

u8 album_export(AlbumEntry* out) {
    if (count)
        memcpy(out, entries, count * sizeof entries[0]);
    return count;
}
