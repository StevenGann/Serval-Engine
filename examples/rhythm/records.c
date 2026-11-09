// The records, saved: one small struct in one save slot, as racer keeps its
// bests.
//
// Records (records.h) is fixed-size fields only, no pointers, so it means
// the same after a power-off. It is read once at boot and written only when
// something changes: a best falls on the results screen, the calibration is
// saved, or the records are erased. A write takes a millisecond or two, and
// the save memory is not worn for nothing.
//
// Versioning: RECORDS_VERSION is a number this game picks for the layout of
// Records. Raise it whenever the struct changes, and save_read() reports a
// save in the old layout as SAVE_OTHER_VERSION instead of reading it as
// garbage (a game that wanted to keep old saves would then read the slot
// with its old struct and convert). This is version 1, so there is nothing
// older to convert: anything else starts with no records.

#include "records.h"

// --- The rules ------------------------------------------------------------------------------

void records_clear(Records* r) {
    *r = (Records){0};
}

// Grades by rank, from 0 (never played) to 5 ('S'); -1 for no grade at all.
static int grade_rank(u8 g) {
    switch (g) {
    case 0:
        return 0;
    case 'D':
        return 1;
    case 'C':
        return 2;
    case 'B':
        return 3;
    case 'A':
        return 4;
    case 'S':
        return 5;
    default:
        return -1;
    }
}

u32 records_add(Records* r, u8 song, u8 difficulty, const Score* s, bool autoplay) {
    if (autoplay || song >= SONG_COUNT || difficulty >= DIFF_COUNT)
        return 0;
    if (s->perfect + s->good + s->miss == 0)
        return 0; // nothing was played
    Best* b = &r->best[song][difficulty];
    u8 grade = (u8)grade_of(score_accuracy_permille(s));
    u32 fell = 0;
    if (b->grade == 0 || s->score > b->score) {
        b->score = s->score;
        fell |= RECORD_SCORE;
    }
    if (grade_rank(grade) > grade_rank(b->grade)) {
        b->grade = grade;
        fell |= RECORD_GRADE;
    }
    if (s->miss == 0 && !b->full_combo) {
        b->full_combo = 1;
        fell |= RECORD_FULL_COMBO;
    }
    return fell;
}

bool records_valid(const Records* r) {
    for (int s = 0; s < SONG_COUNT; s++) {
        for (int d = 0; d < DIFF_COUNT; d++) {
            const Best* b = &r->best[s][d];
            if (grade_rank(b->grade) < 0 || b->full_combo > 1 || (b->full_combo && !b->grade) ||
                b->reserved[0] || b->reserved[1])
                return false;
        }
    }
    return r->reserved == 0 && r->calibration_ms >= -CALIBRATION_MAX_MS &&
           r->calibration_ms <= CALIBRATION_MAX_MS;
}

// --- The game's records, saved --------------------------------------------------------------

static Records records;
static bool save_failed; // the last save_write() didn't succeed

void records_load(void) {
    switch (save_read(RECORDS_SLOT, &records, sizeof records, RECORDS_VERSION)) {
    case SAVE_OK:
        if (records_valid(&records))
            break;
        // Intact, but holding values this game never writes: not its save.
        records_clear(&records);
        break;
    case SAVE_EMPTY:         // first boot, or the slot was erased
    case SAVE_CORRUPT:       // damaged in storage (bad checksum): nothing to rescue
    case SAVE_OTHER_VERSION: // another layout or size: see RECORDS_VERSION above
    default:
        // No records yet. The slot isn't written now: the first record or
        // calibration saves over it.
        records_clear(&records);
        break;
    }
}

const Records* records_get(void) {
    return &records;
}

static void save(void) {
    save_failed = !save_write(RECORDS_SLOT, &records, sizeof records, RECORDS_VERSION);
}

u32 records_take(u8 song, u8 difficulty, const Score* s, bool autoplay) {
    u32 fell = records_add(&records, song, difficulty, s, autoplay);
    if (fell)
        save();
    return fell;
}

void records_save_calibration(s16 ms) {
    records.calibration_ms = (s16)int_clamp(ms, -CALIBRATION_MAX_MS, CALIBRATION_MAX_MS);
    save();
}

void records_erase(void) {
    s16 calibration = records.calibration_ms;
    records_clear(&records);
    records.calibration_ms = calibration;
    save();
}

bool records_save_failed(void) {
    return save_failed;
}
