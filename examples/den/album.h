#ifndef DEN_ALBUM_H
#define DEN_ALBUM_H

// The album: the serval a player has raised, newest first, at most 16. The
// album keeps its own copy of the entries (album.c); the save carries it:
// album_set() after den_save_load(), album_export() before den_save_store().

#include "litter.h"

typedef enum { END_LITTER, END_TOMBSTONE } AlbumEnd;

typedef struct AlbumEntry {
    char name[9];
    Coat coat;
    u8 generation;
    u8 end; // AlbumEnd
    u32 age_minutes;
    u16 best[3]; // the pet's best scores of the three games
} AlbumEntry;

#define ALBUM_MAX 16

// Adds as the newest; the oldest is dropped past 16.
void album_add(const AlbumEntry* e);
u8 album_count(void);
const AlbumEntry* album_get(u8 i); // 0 = newest; NULL past the count

// Replaces the album (a loaded save's): up to 16 entries, newest first.
void album_set(const AlbumEntry* entries, u8 count);
// Copies the album out (room for ALBUM_MAX); returns the count.
u8 album_export(AlbumEntry* out);

#endif // DEN_ALBUM_H
