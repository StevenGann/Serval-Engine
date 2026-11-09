// rhythm: Beat Paws' judging, scoring and calibration arithmetic
// (examples/rhythm/judge.c), natively. Nothing of it is on screen here, so
// these tests are what shows a press is judged against the right note, in
// the right window, once; that a missed note costs its combo; and that the
// score, the grade and the calibration median come out as the rules say.

#include "test.h"

#include "../examples/rhythm/judge.h"
#include "../examples/rhythm/records.h"

// --- Helpers ----------------------------------------------------------------

// Windows used where the sample rate does not matter: 35 and 80 ms at 314
// samples a tick (the slowest song).
#define PERFECT 450
#define GOOD 1028

#define T(ticks) ((FIXED)((ticks) << 8))

static Play play;

static void start(const RhythmNote* notes, u16 count) {
    play_start(&play, notes, count, PERFECT, GOOD);
}

// --- Tests ------------------------------------------------------------------

static void judge_windows(void) {
    static const RhythmNote one[] = {{100, 0}};
    static const struct {
        FIXED error;
        Judgement expect;
    } cases[] = {
        {0, JUDGE_PERFECT},        {PERFECT, JUDGE_PERFECT},   {-PERFECT, JUDGE_PERFECT},
        {PERFECT + 1, JUDGE_GOOD}, {-PERFECT - 1, JUDGE_GOOD}, {GOOD, JUDGE_GOOD},
        {-GOOD, JUDGE_GOOD},       {GOOD + 1, JUDGE_NONE},     {-GOOD - 1, JUDGE_NONE},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        start(one, 1);
        CHECK(play_press(&play, 0, T(100) + cases[i].error) == cases[i].expect);
        // A press that found nothing changes nothing.
        if (cases[i].expect == JUDGE_NONE) {
            CHECK(!play_done(&play));
            CHECK(play_score(&play)->score == 0);
            CHECK(play_score(&play)->miss == 0);
        }
    }
    // Pressed too late for GOOD: the note expires as a MISS instead.
    start(one, 1);
    CHECK(play_press(&play, 0, T(100) + GOOD + 1) == JUDGE_NONE);
    CHECK(play_expire(&play, T(100) + GOOD) == 0); // still inside the window
    CHECK(play_expire(&play, T(100) + GOOD + 1) == 1);
    CHECK(play_score(&play)->miss == 1);
    CHECK(play_done(&play));
    // Wrong lane never matches.
    start(one, 1);
    CHECK(play_press(&play, 1, T(100)) == JUDGE_NONE);
    CHECK(!play_done(&play));
}

static void before_row_zero(void) {
    static const RhythmNote notes[] = {{12, 2}};
    // Presses before the module starts have negative ticks.
    start(notes, 1);
    CHECK(play_press(&play, 2, -T(5)) == JUDGE_NONE);
    CHECK(play_press(&play, 2, T(12) - GOOD - 1) == JUDGE_NONE);
    CHECK(play_press(&play, 2, T(12) - PERFECT - 1) == JUDGE_GOOD);
    // Expiring before row zero marks nothing.
    start(notes, 1);
    CHECK(play_expire(&play, -T(100)) == 0);
    CHECK(play_expire(&play, 0) == 0);
    start(notes, 1);
    CHECK(play_press(&play, 2, -GOOD) == JUDGE_NONE);
}

static void mashing(void) {
    // A chart of notes in all lanes; every lane pressed every quarter tick.
    static RhythmNote notes[400];
    unsigned n = 0;
    for (unsigned t = 6; n < 400; t += 3)
        for (u8 l = 0; l < 4 && n < 400; l++)
            if ((t / 3 + l) % 3 != 0)
                notes[n++] = (RhythmNote){t, l};
    start(notes, 400);
    u16 hits = 0;
    unsigned seen[400] = {0};
    for (FIXED now = -T(2); now < T(notes[399].tick + 3); now += 64) {
        for (u8 l = 0; l < 4; l++) {
            u16 before = play_score(&play)->perfect + play_score(&play)->good;
            Judgement j = play_press(&play, l, now);
            u16 after = play_score(&play)->perfect + play_score(&play)->good;
            if (j == JUDGE_NONE) {
                CHECK(after == before);
            } else {
                CHECK(after == before + 1);
                hits++;
                // A note of this lane nobody judged yet lies in the window:
                // mark the earliest such note as taken.
                unsigned found = 0;
                for (unsigned i = 0; i < 400; i++)
                    if (notes[i].lane == l && T(notes[i].tick) - GOOD <= now &&
                        now <= T(notes[i].tick) + GOOD)
                        if (seen[i] == 0 && found == 0) {
                            seen[i] = 1;
                            found = 1;
                        }
                CHECK(found == 1);
            }
        }
        play_expire(&play, now);
    }
    CHECK(hits == play_score(&play)->perfect + play_score(&play)->good);
    CHECK(hits <= 400);
    CHECK(play_done(&play));
    CHECK(play_score(&play)->perfect + play_score(&play)->good + play_score(&play)->miss == 400);
}

static void close_notes(void) {
    // Two notes 2 rows (12 ticks) apart: the earlier is judged first, though
    // the press is nearer the later one only after the midpoint.
    static const RhythmNote notes[] = {{100, 1}, {112, 1}};
    start(notes, 2);
    CHECK(play_press(&play, 1, T(100)) == JUDGE_PERFECT);
    CHECK(play_press(&play, 1, T(100)) == JUDGE_NONE); // nothing left in the window
    CHECK(play_press(&play, 1, T(112)) == JUDGE_PERFECT);
    CHECK(play_done(&play));
    // Close enough that both are in the window: the earlier first.
    static const RhythmNote tight[] = {{100, 0}, {102, 0}};
    start(tight, 2);
    CHECK(play_press(&play, 0, T(102)) == JUDGE_GOOD);    // note 100 (error 512 > PERFECT)
    CHECK(play_press(&play, 0, T(102)) == JUDGE_PERFECT); // then note 102
    CHECK(play_done(&play));
    // The earlier note too old to hit is left to expire; the next is judged.
    static const RhythmNote late[] = {{100, 0}, {109, 0}};
    start(late, 2);
    FIXED now = T(100) + GOOD + 1;
    CHECK(play_press(&play, 0, now) == JUDGE_NONE); // 109 is 9 ticks away
    now = T(109) - 10;
    CHECK(play_press(&play, 0, now) == JUDGE_PERFECT);
    CHECK(play_score(&play)->perfect == 1);
    CHECK(play_expire(&play, T(109)) == 1); // the first one
    CHECK(play_score(&play)->miss == 1);
    CHECK(play_done(&play));
}

static void expire_counts(void) {
    static const RhythmNote notes[] = {{10, 0}, {20, 1}, {30, 2}, {40, 3}, {50, 0}};
    start(notes, 5);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);
    CHECK(play_press(&play, 1, T(20)) == JUDGE_PERFECT);
    CHECK(play_score(&play)->combo == 2);
    CHECK(play_expire(&play, T(10)) == 0);
    // Notes 30 and 40 pass their windows together.
    CHECK(play_expire(&play, T(45)) == 2);
    CHECK(play_score(&play)->miss == 2);
    CHECK(play_score(&play)->combo == 0);
    CHECK(play_score(&play)->max_combo == 2);
    CHECK(play_expire(&play, T(45)) == 0); // not counted twice
    CHECK(!play_done(&play));
    CHECK(play_press(&play, 0, T(50)) == JUDGE_PERFECT);
    CHECK(play_score(&play)->combo == 1);
    CHECK(play_done(&play));
    // A missed note can no longer be hit.
    CHECK(play_press(&play, 2, T(30) + 1) == JUDGE_NONE);
}

static void scoring(void) {
    // 60 notes in one lane, hit perfectly: the multiplier grows with the
    // combo before each hit, and stops at 2 once it reaches 50.
    static RhythmNote notes[60];
    for (unsigned i = 0; i < 60; i++)
        notes[i] = (RhythmNote){10 + 10 * i, 0};
    start(notes, 60);
    u32 expect = 0;
    for (unsigned i = 0; i < 60; i++) {
        u32 c = i < 50 ? i : 50;
        expect += 300 * (256 + c * 256 / 50) / 256;
        CHECK(play_press(&play, 0, T(notes[i].tick)) == JUDGE_PERFECT);
        CHECK(play_score(&play)->score == expect);
    }
    CHECK(play_score(&play)->combo == 60);
    CHECK(play_score(&play)->max_combo == 60);
    // First hit 300, second 300 * 261 / 256 = 305 (rounded down), the 51st: 600.
    start(notes, 60);
    play_press(&play, 0, T(10));
    CHECK(play_score(&play)->score == 300);
    play_press(&play, 0, T(20));
    CHECK(play_score(&play)->score == 300 + 305);
    // 100 for a GOOD, times the multiplier; a MISS then resets it.
    static const RhythmNote seq[] = {{10, 0}, {20, 0}, {30, 0}, {40, 0}};
    start(seq, 4);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);            // 300 * 1.00
    CHECK(play_press(&play, 0, T(20) + PERFECT + 1) == JUDGE_GOOD); // 100 * 261 / 256 = 101
    CHECK(play_score(&play)->score == 401);
    CHECK(play_expire(&play, T(30) + GOOD + 1) == 1);
    CHECK(play_score(&play)->combo == 0);
    CHECK(play_press(&play, 0, T(40)) == JUDGE_PERFECT); // multiplier back to 1
    CHECK(play_score(&play)->score == 701);
    CHECK(play_score(&play)->perfect == 2);
    CHECK(play_score(&play)->good == 1);
    CHECK(play_score(&play)->miss == 1);
    CHECK(play_score(&play)->max_combo == 2);
    // Accuracy: (2*3 + 1) * 1000 / 12 = 583.
    CHECK(score_accuracy_permille(play_score(&play)) == 583);
    Score none = {0};
    CHECK(score_accuracy_permille(&none) == 0);
    Score all = {.perfect = 7};
    CHECK(score_accuracy_permille(&all) == 1000);
}

static void grades(void) {
    CHECK(grade_of(1000) == 'S');
    CHECK(grade_of(950) == 'S');
    CHECK(grade_of(949) == 'A');
    CHECK(grade_of(900) == 'A');
    CHECK(grade_of(899) == 'B');
    CHECK(grade_of(800) == 'B');
    CHECK(grade_of(799) == 'C');
    CHECK(grade_of(700) == 'C');
    CHECK(grade_of(699) == 'D');
    CHECK(grade_of(0) == 'D');
}

static void ms_to_ticks_values(void) {
    // ms * 15768 * 256 / (1000 * samples), to the nearest.
    CHECK(ms_to_ticks(35, 314) == 450);  // 449.94
    CHECK(ms_to_ticks(80, 314) == 1028); // 1028.44
    CHECK(ms_to_ticks(35, 188) == 751);  // 751.496
    CHECK(ms_to_ticks(80, 188) == 1718); // 1717.71
    CHECK(ms_to_ticks(0, 188) == 0);
    CHECK(ms_to_ticks(-35, 314) == -450);
    CHECK(ms_to_ticks(-80, 188) == -1718);
    CHECK(ms_to_ticks(1000, 314) == 12855); // 12855.1
    // Exact halves round away from zero: 125 ms at 512 samples a tick is
    // 125 * 15768 * 256 / 512000 = 985.5 ticks of 1/256 exactly.
    CHECK(ms_to_ticks(125, 512) == 986);
    CHECK(ms_to_ticks(-125, 512) == -986);
    CHECK(ms_to_ticks(375, 512) == 2957); // 2956.5
    CHECK(ms_to_ticks(-375, 512) == -2957);
}

static void calibration_extremes(void) {
    s16 one[] = {-37};
    CHECK(calibration_median_ms(one, 1) == -37);
    s16 odd[] = {50, -20, 10, 300, -5};
    CHECK(calibration_median_ms(odd, 5) == 10);
    s16 even[] = {4, 1, 3, 2};
    CHECK(calibration_median_ms(even, 4) == 2); // (2 + 3) / 2, toward zero
    s16 evenneg[] = {-4, -1, -3, -2};
    CHECK(calibration_median_ms(evenneg, 4) == -2);
    s16 low[] = {-200, -200, -200};
    CHECK(calibration_median_ms(low, 3) == -200);
    s16 high[] = {200, 200, 200, 200};
    CHECK(calibration_median_ms(high, 4) == 200);
    // One outlier hardly moves it.
    s16 outlier[] = {20, 22, 21, 19, 2000, 20, 23};
    CHECK(calibration_median_ms(outlier, 7) == 21);
    // The caller's array is left as it was.
    CHECK(odd[0] == 50 && odd[1] == -20 && odd[4] == -5);
    CHECK(calibration_median_ms(odd, 0) == 0);
}

static void play_start_resets(void) {
    static const RhythmNote notes[] = {{10, 0}, {20, 1}, {30, 2}};
    start(notes, 3);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);
    CHECK(play_press(&play, 1, T(20)) == JUDGE_PERFECT);
    play_expire(&play, T(100));
    CHECK(play_done(&play));
    start(notes, 3); // after a finished chart
    CHECK(!play_done(&play));
    CHECK(play_score(&play)->score == 0 && play_score(&play)->perfect == 0 &&
          play_score(&play)->miss == 0 && play_score(&play)->combo == 0 &&
          play_score(&play)->max_combo == 0);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);
    CHECK(play_score(&play)->score == 300);
    start(notes, 3); // after a half-played one
    CHECK(play_score(&play)->perfect == 0);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);
    CHECK(play_press(&play, 1, T(20)) == JUDGE_PERFECT);
    CHECK(play_score(&play)->score == 300 + 305);
}

static void lanes_past_three(void) {
    // A note in lane 4 or 7 is skipped, not folded into lane 0 or 3: no
    // press judges it, it counts nowhere, and the chart still ends.
    static const RhythmNote notes[] = {{10, 0}, {20, 4}, {30, 3}, {40, 7}};
    start(notes, 4);
    CHECK(!play_done(&play));
    CHECK(play_press(&play, 0, T(20)) == JUDGE_NONE); // not lane 4 & 3
    CHECK(play_press(&play, 3, T(40)) == JUDGE_NONE); // not lane 7 & 3
    CHECK(play_press(&play, 4, T(20)) == JUDGE_NONE);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);
    CHECK(play_press(&play, 3, T(30)) == JUDGE_PERFECT);
    CHECK(play_done(&play));
    CHECK(play_expire(&play, T(1000)) == 0);
    const Score* s = play_score(&play);
    CHECK(s->perfect == 2 && s->good == 0 && s->miss == 0 && s->combo == 2);
}

static void windows_change(void) {
    // A tick's length changing mid-chart: wider windows from then on, the
    // judged notes and the score kept.
    static const RhythmNote notes[] = {{10, 0}, {20, 0}, {30, 1}};
    start(notes, 3);
    CHECK(play_press(&play, 0, T(10)) == JUDGE_PERFECT);
    CHECK(play_press(&play, 0, T(20) + PERFECT + 1) == JUDGE_GOOD);
    play_set_windows(&play, 2 * PERFECT, 2 * GOOD);
    CHECK(play_press(&play, 1, T(30) + PERFECT + 1) == JUDGE_PERFECT);
    CHECK(play_done(&play));
    CHECK(play_score(&play)->perfect == 2 && play_score(&play)->good == 1);
    CHECK(play_score(&play)->combo == 3);
    // ... and narrower: a press now outside the good window finds nothing.
    start(notes, 3);
    play_set_windows(&play, PERFECT / 2, GOOD / 2);
    CHECK(play_press(&play, 0, T(10) + GOOD / 2 + 1) == JUDGE_NONE);
    CHECK(play_expire(&play, T(10) + GOOD / 2 + 1) == 1);
}

static void note_judged(void) {
    static const RhythmNote notes[] = {{10, 0}, {20, 1}, {30, 5}, {40, 2}};
    start(notes, 4);
    CHECK(!play_note_judged(&play, 0) && !play_note_judged(&play, 1));
    CHECK(play_note_judged(&play, 2)); // lane 5: skipped
    CHECK(!play_note_judged(&play, 3));
    CHECK(play_note_judged(&play, 4)); // past the chart
    CHECK(play_press(&play, 1, T(20)) == JUDGE_PERFECT);
    CHECK(!play_note_judged(&play, 0) && play_note_judged(&play, 1));
    CHECK(play_expire(&play, T(20)) == 1); // note 0, missed
    CHECK(play_note_judged(&play, 0));
    CHECK(!play_note_judged(&play, 3));
}

static void play_done_when_all_judged(void) {
    static const RhythmNote notes[] = {{10, 0}, {20, 1}};
    start(notes, 2);
    CHECK(!play_done(&play));
    play_press(&play, 0, T(10));
    CHECK(!play_done(&play));
    play_expire(&play, T(20) + GOOD + 1);
    CHECK(play_done(&play));
    start(notes, 0); // an empty chart is done at once
    CHECK(play_done(&play));
}

// The saved bests: each one only when beaten, never by the autoplay; the
// calibration clamped; a save of another version or size, or holding values
// this game never writes, read as no records; erasing keeps the calibration.
static int memcmp_bytes(const void* a, const void* b, unsigned n) {
    const u8 *x = a, *y = b;
    for (unsigned i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

static bool best_is(const Records* r, int song, int diff, u32 score, u8 grade, u8 fc) {
    const Best* b = &r->best[song][diff];
    return b->score == score && b->grade == grade && b->full_combo == fc;
}

static bool records_empty(const Records* r) {
    for (int s = 0; s < SONG_COUNT; s++)
        for (int d = 0; d < DIFF_COUNT; d++)
            if (!best_is(r, s, d, 0, 0, 0))
                return false;
    return r->calibration_ms == 0;
}

static void calibration_taps(void) {
    // 120 BPM: 328 samples a tick, a click every 24 ticks (6144 in 24.8).
    // A tick is 328 / 15768 s = 20.80 ms, so 1/256 tick = 0.08126 ms.
    const FIXED beat = 24 << 8;
    s16 ms = 12345;
    CHECK(calibration_tap_ms(5 * beat, beat, 328, &ms) && ms == 0);          // on a click
    CHECK(calibration_tap_ms(0, beat, 328, &ms) && ms == 0);                 // on the first
    CHECK(calibration_tap_ms(5 * beat + 1231, beat, 328, &ms) && ms == 100); // 100.03
    CHECK(calibration_tap_ms(5 * beat - 1231, beat, 328, &ms) && ms == -100);
    CHECK(calibration_tap_ms(-1231, beat, 328, &ms) && ms == -100);     // early for the first
    CHECK(calibration_tap_ms(3 * beat + 6, beat, 328, &ms) && ms == 0); // 0.49 ms
    CHECK(calibration_tap_ms(3 * beat + 7, beat, 328, &ms) && ms == 1); // 0.57 ms
    CHECK(calibration_tap_ms(3 * beat - 7, beat, 328, &ms) && ms == -1);
    // The nearest click: just under half a beat late, just under half early
    // for the next.
    CHECK(calibration_tap_ms(2 * beat + beat / 2 - 1, beat, 328, &ms) && ms == 250); // 249.6
    CHECK(calibration_tap_ms(2 * beat + beat / 2 + 1, beat, 328, &ms) && ms == -250);
    // Half a beat or more from every click: ignored, *error_ms untouched.
    ms = 77;
    CHECK(!calibration_tap_ms(2 * beat + beat / 2, beat, 328, &ms)); // exactly between
    CHECK(!calibration_tap_ms(-beat / 2, beat, 328, &ms));           // before the first
    CHECK(!calibration_tap_ms(-beat, beat, 328, &ms));
    CHECK(!calibration_tap_ms(beat, 0, 328, &ms));
    CHECK(!calibration_tap_ms(beat, beat, 0, &ms));
    CHECK(ms == 77);
    CHECK(calibration_tap_ms(-beat / 2 + 1, beat, 328, &ms) && ms == -250);
    // Far into a long session the arithmetic holds: an hour of beats.
    CHECK(calibration_tap_ms(7200 * beat + 1231, beat, 328, &ms) && ms == 100);
}

static void records_rules(void) {
    Records r;
    for (unsigned i = 0; i < sizeof r; i++)
        ((u8*)&r)[i] = 0x5A;
    records_clear(&r);
    CHECK(records_empty(&r));
    CHECK(records_valid(&r));

    // The first play sets every best (all PERFECT: grade S, full combo).
    Score clean = {.perfect = 10, .max_combo = 10, .score = 4000};
    CHECK(records_add(&r, SONG_STRIDE, DIFF_HARD, &clean, false) ==
          (RECORD_SCORE | RECORD_GRADE | RECORD_FULL_COMBO));
    CHECK(best_is(&r, SONG_STRIDE, DIFF_HARD, 4000, 'S', 1));
    CHECK(best_is(&r, SONG_STRIDE, DIFF_EASY, 0, 0, 0)); // the others untouched
    CHECK(best_is(&r, SONG_CALM, DIFF_HARD, 0, 0, 0));
    // The same again, or worse: nothing falls.
    CHECK(records_add(&r, SONG_STRIDE, DIFF_HARD, &clean, false) == 0);
    Score worse = {.perfect = 5, .good = 2, .miss = 3, .score = 1500};
    CHECK(records_add(&r, SONG_STRIDE, DIFF_HARD, &worse, false) == 0);
    CHECK(best_is(&r, SONG_STRIDE, DIFF_HARD, 4000, 'S', 1));

    // Each best on its own: a higher score with a miss keeps the full combo
    // and the grade; a better grade on a lower score keeps the score.
    Score missed = {.perfect = 18, .miss = 2, .score = 3000}; // 900: A
    CHECK(records_add(&r, SONG_CALM, DIFF_EASY, &missed, false) == (RECORD_SCORE | RECORD_GRADE));
    CHECK(best_is(&r, SONG_CALM, DIFF_EASY, 3000, 'A', 0));
    Score higher = {.perfect = 10, .good = 5, .miss = 5, .score = 3500}; // 583: D
    CHECK(records_add(&r, SONG_CALM, DIFF_EASY, &higher, false) == RECORD_SCORE);
    CHECK(best_is(&r, SONG_CALM, DIFF_EASY, 3500, 'A', 0));
    Score better = {.perfect = 20, .score = 2000}; // S, full combo, a lower score
    CHECK(records_add(&r, SONG_CALM, DIFF_EASY, &better, false) ==
          (RECORD_GRADE | RECORD_FULL_COMBO));
    CHECK(best_is(&r, SONG_CALM, DIFF_EASY, 3500, 'S', 1));
    // A full combo of GOODs is still a full combo, and once set stays.
    Score goods = {.good = 20, .score = 2500}; // 333: D
    CHECK(records_add(&r, SONG_RUSH, DIFF_EASY, &goods, false) ==
          (RECORD_SCORE | RECORD_GRADE | RECORD_FULL_COMBO));
    CHECK(best_is(&r, SONG_RUSH, DIFF_EASY, 2500, 'D', 1));
    CHECK(records_add(&r, SONG_RUSH, DIFF_EASY, &missed, false) == (RECORD_SCORE | RECORD_GRADE));
    CHECK(best_is(&r, SONG_RUSH, DIFF_EASY, 3000, 'A', 1));
    // A first play that scores nothing is still a first: grade D.
    Score nothing = {.miss = 10};
    CHECK(records_add(&r, SONG_RUSH, DIFF_HARD, &nothing, false) == (RECORD_SCORE | RECORD_GRADE));
    CHECK(best_is(&r, SONG_RUSH, DIFF_HARD, 0, 'D', 0));
    CHECK(records_add(&r, SONG_RUSH, DIFF_HARD, &nothing, false) == 0);

    // The autoplay never sets a record, even on a fresh song.
    Records before = r;
    CHECK(records_add(&r, SONG_CALM, DIFF_HARD, &clean, true) == 0);
    Score huge = {.perfect = 900, .max_combo = 900, .score = 999999};
    CHECK(records_add(&r, SONG_STRIDE, DIFF_HARD, &huge, true) == 0);
    // Out of range, or no notes: nothing either.
    CHECK(records_add(&r, SONG_COUNT, DIFF_EASY, &clean, false) == 0);
    CHECK(records_add(&r, SONG_CALM, DIFF_COUNT, &clean, false) == 0);
    Score empty = {0};
    CHECK(records_add(&r, SONG_CALM, DIFF_HARD, &empty, false) == 0);
    CHECK(memcmp_bytes(&before, &r, sizeof r) == 0);
    CHECK(records_valid(&r));

    // What records_valid() refuses.
    Records bad = r;
    bad.best[SONG_CALM][DIFF_EASY].grade = 'E';
    CHECK(!records_valid(&bad));
    bad = r;
    bad.best[SONG_CALM][DIFF_HARD].full_combo = 1; // no grade: never played
    CHECK(!records_valid(&bad));
    bad = r;
    bad.best[SONG_CALM][DIFF_EASY].full_combo = 2;
    CHECK(!records_valid(&bad));
    bad = r;
    bad.best[SONG_CALM][DIFF_EASY].reserved[1] = 1;
    CHECK(!records_valid(&bad));
    bad = r;
    bad.reserved = 1;
    CHECK(!records_valid(&bad));
    bad = r;
    bad.calibration_ms = CALIBRATION_MAX_MS + 1;
    CHECK(!records_valid(&bad));
    bad.calibration_ms = -CALIBRATION_MAX_MS - 1;
    CHECK(!records_valid(&bad));
    bad.calibration_ms = -CALIBRATION_MAX_MS;
    CHECK(records_valid(&bad));

    // The saved copy. No save: empty.
    save_erase(RECORDS_SLOT);
    records_load();
    CHECK(records_empty(records_get()));
    CHECK(records_take(SONG_CALM, DIFF_HARD, &clean, false) ==
          (RECORD_SCORE | RECORD_GRADE | RECORD_FULL_COMBO));
    CHECK(save_slot_size(RECORDS_SLOT) == sizeof(Records));
    records_save_calibration(-37);
    records_load(); // as at the next boot
    CHECK(best_is(records_get(), SONG_CALM, DIFF_HARD, 4000, 'S', 1));
    CHECK(records_get()->calibration_ms == -37);
    CHECK(!records_save_failed());
    // Nothing fell, or the autoplay: nothing written.
    Records saved;
    save_write(RECORDS_SLOT, records_get(), sizeof saved, RECORDS_VERSION);
    CHECK(records_take(SONG_CALM, DIFF_HARD, &worse, false) == 0);
    CHECK(records_take(SONG_STRIDE, DIFF_EASY, &clean, true) == 0);
    save_erase(RECORDS_SLOT); // a write now would show as a save again
    CHECK(records_take(SONG_CALM, DIFF_HARD, &worse, false) == 0);
    CHECK(records_take(SONG_STRIDE, DIFF_EASY, &clean, true) == 0);
    CHECK(save_slot_size(RECORDS_SLOT) == 0);
    // The calibration is clamped.
    records_save_calibration(500);
    CHECK(records_get()->calibration_ms == CALIBRATION_MAX_MS);
    records_save_calibration(-500);
    CHECK(records_get()->calibration_ms == -CALIBRATION_MAX_MS);
    records_load();
    CHECK(records_get()->calibration_ms == -CALIBRATION_MAX_MS);
    CHECK(best_is(records_get(), SONG_CALM, DIFF_HARD, 4000, 'S', 1));
    // Erasing clears the bests, keeps the calibration, and saves.
    records_erase();
    CHECK(best_is(records_get(), SONG_CALM, DIFF_HARD, 0, 0, 0));
    CHECK(records_get()->calibration_ms == -CALIBRATION_MAX_MS);
    records_load();
    CHECK(best_is(records_get(), SONG_CALM, DIFF_HARD, 0, 0, 0));
    CHECK(records_get()->calibration_ms == -CALIBRATION_MAX_MS);

    // Another version, another size or a struct this game never wrote:
    // empty, and the slot left as it was.
    records_take(SONG_CALM, DIFF_HARD, &clean, false);
    save_read(RECORDS_SLOT, &saved, sizeof saved, RECORDS_VERSION);
    save_write(RECORDS_SLOT, &saved, sizeof saved, RECORDS_VERSION + 1);
    records_load();
    CHECK(records_empty(records_get()));
    CHECK(save_slot_version(RECORDS_SLOT) == RECORDS_VERSION + 1);
    save_write(RECORDS_SLOT, &saved, sizeof saved - 4, RECORDS_VERSION);
    records_load();
    CHECK(records_empty(records_get()));
    saved.best[SONG_RUSH][DIFF_EASY].grade = 'Z';
    save_write(RECORDS_SLOT, &saved, sizeof saved, RECORDS_VERSION);
    records_load();
    CHECK(records_empty(records_get()));
    saved.best[SONG_RUSH][DIFF_EASY].grade = 0;
    save_write(RECORDS_SLOT, &saved, sizeof saved, RECORDS_VERSION);
    records_load(); // and the same, valid, reads
    CHECK(best_is(records_get(), SONG_CALM, DIFF_HARD, 4000, 'S', 1));
    save_erase(RECORDS_SLOT);
}

TEST_SUITE(rhythm_tests, "rhythm", {"judge_windows", judge_windows},
           {"before_row_zero", before_row_zero}, {"mashing", mashing}, {"close_notes", close_notes},
           {"expire_counts", expire_counts}, {"scoring", scoring}, {"grades", grades},
           {"ms_to_ticks_values", ms_to_ticks_values},
           {"calibration_extremes", calibration_extremes}, {"play_start_resets", play_start_resets},
           {"lanes_past_three", lanes_past_three}, {"windows_change", windows_change},
           {"note_judged", note_judged}, {"play_done_when_all_judged", play_done_when_all_judged},
           {"calibration_taps", calibration_taps}, {"records_rules", records_rules});
