// The PSG music sequencer: keeps a song's time and steps its tracks through
// their notes, once per frame. See psg_sequencer.h.

#include "psg_sequencer.h"

#include "warn.h"

// Notes as frequency register values, generated from the equal-tempered
// scale (A4 = 440 Hz) with the formulas in psg_sequencer.h. Index 0 (a rest)
// is unused. tests/psg_sequencer_tests.c checks them against the formulas.
const u16 serval_psg_square_rates[SERVAL_PSG_NOTE_MAX + 1] = {
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    44,   157,  263,  363,  457,  547,  631,  711,  786,  856,  923,  986,
    1046, 1102, 1155, 1205, 1253, 1297, 1339, 1379, 1417, 1452, 1486, 1517, 1547, 1575, 1602, 1627,
    1650, 1673, 1694, 1714, 1732, 1750, 1767, 1783, 1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881,
    1890, 1899, 1907, 1915, 1923, 1930, 1936, 1943, 1949, 1954, 1959, 1964, 1969, 1974, 1978, 1982,
    1985, 1989, 1992, 1995, 1998, 2001, 2004, 2006, 2009, 2011, 2013, 2015, 2017, 2018, 2020, 2022,
    2023, 2025, 2026, 2027, 2028, 2029, 2030, 2031, 2032, 2033, 2034, 2035, 2036, 2036, 2037, 2038,
    2038, 2039, 2039, 2040, 2040, 2041, 2041, 2041, 2042, 2042, 2042, 2043, 2043, 2043, 2044, 2044,
};
const u8 serval_psg_noise_rates[SERVAL_PSG_NOTE_MAX + 1] = {
    0,   199, 199, 198, 198, 197, 197, 197, 197, 196, 196, 196, 196, 183, 183, 183, 182, 182,
    181, 181, 181, 181, 180, 180, 180, 167, 167, 166, 166, 166, 166, 165, 165, 165, 164, 164,
    164, 151, 151, 151, 150, 150, 150, 149, 149, 149, 148, 148, 148, 135, 135, 135, 134, 134,
    134, 133, 133, 133, 132, 132, 132, 119, 119, 119, 118, 118, 118, 117, 117, 117, 116, 116,
    116, 103, 103, 103, 102, 102, 102, 101, 101, 101, 100, 100, 100, 87,  87,  87,  86,  86,
    86,  85,  85,  85,  84,  84,  84,  71,  71,  71,  70,  70,  70,  69,  69,  69,  68,  68,
    68,  55,  55,  55,  54,  54,  54,  53,  53,  53,  52,  52,  52,  39,  39,  39,  38,  38,
    38,  37,  37,  37,  36,  36,  36,  23,  23,  23,  22,  22,  22,  21,  21,  21,  20,  20,
};

#ifdef SERVAL_DEBUG
enum {
    WARN_SONG = 1 << 0,
    WARN_TRACKS = 1 << 1,
    WARN_CHANNEL = 1 << 2,
    WARN_DUPLICATE = 1 << 3,
    WARN_EMPTY = 1 << 4,
    WARN_NOTES = 1 << 5,
    WARN_LOOP = 1 << 6,
    WARN_LOW_NOTE = 1 << 7,
    WARN_HIGH_NOTE = 1 << 8,
    WARN_TEMPO = 1 << 9,
    WARN_FIELDS = 1 << 10,
    WARN_WAVE = 1 << 11,
};
static u32 warned;

static bool first_warning(u32 problem) {
    if (warned & problem)
        return false;
    warned |= problem;
    return true;
}
#define WARN_ONCE(problem, ...)                                                                    \
    do {                                                                                           \
        if (first_warning(problem))                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
    } while (0)

static const char* const channel_names[SERVAL_PSG_CHANNELS] = {"PSG_SQUARE1", "PSG_SQUARE2",
                                                               "PSG_NOISE"};
#else
#define WARN_ONCE(problem, ...) ((void)0)
#endif

void serval_psg_seq_reset_warnings(void) {
#ifdef SERVAL_DEBUG
    warned = 0;
#endif
}

// Ticks per minute at one tick per frame: 60 * 59.7275 frames per second.
#define MAX_TICKS_PER_MINUTE 3583u

u32 serval_psg_tick_step(u16 tempo, u8 ticks_per_beat) {
    u32 per_minute = (u32)(tempo ? tempo : 120) * (ticks_per_beat ? ticks_per_beat : 4);
    if (per_minute > MAX_TICKS_PER_MINUTE) {
        WARN_ONCE(WARN_TEMPO,
                  "PSG music: tempo %u at %u ticks per beat is faster than a tick per frame "
                  "(%u ticks a minute at most); it plays at that speed. Lower the tempo "
                  "(PsgSong.tempo, psg_music_set_tempo) or .ticks_per_beat",
                  tempo, ticks_per_beat ? ticks_per_beat : 4u, MAX_TICKS_PER_MINUTE);
        return SERVAL_PSG_TICK;
    }
    // A frame is 280896 CPU cycles at 2^24 Hz, so ticks per frame are
    // per_minute * 280896 / (60 * 2^24), or per_minute * 1463 / 80 in 16.16.
    return (per_minute * 1463 + 40) / 80;
}

// Reports what can't play as written (debug builds): notes out of range.
static void check_notes(const PsgTrack* t, u32 id) {
#ifdef SERVAL_DEBUG
    for (u32 i = 0; i < t->note_count; i++) {
        u32 note = t->notes[i].note;
        if (note > SERVAL_PSG_NOTE_MAX)
            WARN_ONCE(WARN_HIGH_NOTE,
                      "psg_music_play: track %u (%s) has note %u at %u, above PSG_B10; it plays "
                      "as PSG_B10",
                      id, channel_names[t->channel], note, i);
        else if (note != PSG_REST && note < PSG_C2 && t->channel != PSG_NOISE)
            WARN_ONCE(WARN_LOW_NOTE,
                      "psg_music_play: track %u (%s) has note %u at %u, below PSG_C2, the "
                      "lowest a square channel plays (it plays as 64 Hz); raise it an octave "
                      "(+ 12)",
                      id, channel_names[t->channel], note, i);
    }
#else
    (void)t;
    (void)id;
#endif
}

static void load_note(PsgSeqTrack* t) {
    const PsgNote* n = &t->track->notes[t->index];
    t->note = n->note > SERVAL_PSG_NOTE_MAX ? SERVAL_PSG_NOTE_MAX : n->note;
    t->ticks_left = n->length ? n->length : t->default_length;
}

u32 serval_psg_seq_start(PsgSequencer* seq, const PsgSong* song) {
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        seq->tracks[c] = (PsgSeqTrack){.track = NULL, .note = PSG_REST};
    seq->step = 0;
    seq->phase = 0;
    seq->tempo = 0;
    seq->ticks_per_beat = 0;
    if (!serval_plausible_pointer(song)) {
        WARN_ONCE(WARN_SONG, "psg_music_play: the song is NULL or not a valid pointer; nothing "
                             "plays");
        return 0;
    }
    if (song->track_count && !serval_plausible_pointer(song->tracks)) {
        WARN_ONCE(WARN_TRACKS,
                  "psg_music_play: the song has .track_count %u but .tracks is not set; "
                  "nothing plays",
                  song->track_count);
        return 0;
    }
    seq->tempo = song->tempo;
    seq->ticks_per_beat = song->ticks_per_beat;
    seq->step = serval_psg_tick_step(song->tempo, song->ticks_per_beat);
    // Half a frame ahead: each tick falls on the frame closest to its time.
    seq->phase = (s32)(seq->step / 2);
    u32 beat = song->ticks_per_beat ? song->ticks_per_beat : 4u;
    u32 channels = 0;
    for (u32 i = 0; i < song->track_count; i++) {
        const PsgTrack* t = &song->tracks[i];
        if (t->channel == PSG_WAVE) {
            // Planned (audio.h): left out like an invalid channel until the
            // wave channel is implemented, but reported as planned, and apart
            // from invalid channels, so that each is reported once.
            WARN_ONCE(WARN_WAVE,
                      "psg_music_play: the PSG wave channel is planned, not implemented in this "
                      "engine version; track %u (PSG_WAVE) is left out",
                      i);
            continue;
        }
        if (t->channel >= SERVAL_PSG_CHANNELS) {
            WARN_ONCE(WARN_CHANNEL,
                      "psg_music_play: track %u has an invalid channel (%u); use PSG_SQUARE1, "
                      "PSG_SQUARE2 or PSG_NOISE. It is left out",
                      i, t->channel);
            continue;
        }
        if (channels & 1u << t->channel) {
            WARN_ONCE(WARN_DUPLICATE,
                      "psg_music_play: track %u plays on %s, like an earlier track; a channel "
                      "plays one track. It is left out",
                      i, channel_names[t->channel]);
            continue;
        }
        if (t->note_count == 0) {
            WARN_ONCE(WARN_EMPTY, "psg_music_play: track %u (%s) has no notes (.note_count 0)", i,
                      channel_names[t->channel]);
            continue;
        }
        if (!serval_plausible_pointer(t->notes)) {
            WARN_ONCE(WARN_NOTES,
                      "psg_music_play: track %u (%s) has .note_count %u but .notes is not set; "
                      "it is left out",
                      i, channel_names[t->channel], t->note_count);
            continue;
        }
        u16 loop = t->loop;
        if (loop != PSG_NO_LOOP && loop >= t->note_count) {
            WARN_ONCE(WARN_LOOP,
                      "psg_music_play: track %u (%s) has .loop %u, but only %u notes; it loops "
                      "from the start. Use a note index, or PSG_NO_LOOP to play it once",
                      i, channel_names[t->channel], loop, t->note_count);
            loop = 0;
        }
        check_notes(t, i);
        if ((t->duty > PSG_DUTY_75 && t->channel != PSG_NOISE) || t->volume > 15 || t->fade < -7 ||
            t->fade > 7)
            WARN_ONCE(WARN_FIELDS,
                      "psg_music_play: track %u (%s) has .duty %u, .volume %u, .fade %d; use "
                      "PSG_DUTY_12 to PSG_DUTY_75, 0 to 15 and -7 to 7 (clamped)",
                      i, channel_names[t->channel], t->duty, t->volume, t->fade);
        PsgSeqTrack* s = &seq->tracks[t->channel];
        *s = (PsgSeqTrack){
            .track = t, .loop = loop, .default_length = (u8)(t->length ? t->length : beat)};
        load_note(s);
        channels |= 1u << t->channel;
    }
    return channels;
}

u32 serval_psg_seq_advance(PsgSequencer* seq) {
    seq->phase += (s32)seq->step;
    if (seq->phase < (s32)SERVAL_PSG_TICK)
        return 0;
    seq->phase -= (s32)SERVAL_PSG_TICK;
    u32 changed = 0;
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++) {
        PsgSeqTrack* t = &seq->tracks[c];
        if (!t->track || --t->ticks_left > 0)
            continue;
        changed |= 1u << c;
        if (++t->index >= t->track->note_count) {
            if (t->loop == PSG_NO_LOOP) {
                t->track = NULL;
                t->note = PSG_REST;
                continue;
            }
            t->index = t->loop;
        }
        load_note(t);
    }
    return changed;
}

void serval_psg_seq_set_tempo(PsgSequencer* seq, u16 tempo) {
    u32 step = serval_psg_tick_step(tempo ? tempo : seq->tempo, seq->ticks_per_beat);
    // The phase is the song's position within its tick plus half a frame at
    // the old tempo: keep the position, and lead by half a frame at the new
    // one. Below 0, the tick that just played fell early by the new rounding;
    // at a tick or more, the next one is due now and plays next frame (a
    // frame late, once). Either way the song keeps its time.
    seq->phase += (s32)(step / 2) - (s32)(seq->step / 2);
    seq->step = step;
}

u32 serval_psg_seq_channels(const PsgSequencer* seq) {
    u32 channels = 0;
    for (u32 c = 0; c < SERVAL_PSG_CHANNELS; c++)
        if (seq->tracks[c].track)
            channels |= 1u << c;
    return channels;
}
