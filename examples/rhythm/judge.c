// Judging a press against a chart: see judge.h. Per press and per frame the
// work is bounded by the notes inside the good window of each lane (a
// per-lane cursor skips the judged ones), and there is no division in either:
// the combo multiplier is a table built once by play_start().

#include "judge.h"

#include <serval/audio.h> // MUSIC_RATE

#define NONE 0xFFFFu
#define COMBO_CAP 50

// 256 + combo * 256 / 50, 24.8: the multiplier for a combo before the hit.
static u16 multiplier[COMBO_CAP + 1];

static bool is_judged(const Play* p, u16 i) {
    return (p->judged[i >> 3] >> (i & 7)) & 1;
}

// The note's time in 24.8 ticks.
static FIXED note_time(const Play* p, u16 i) {
    return (FIXED)(p->notes[i].tick << 8);
}

// Moves the lane's cursor past judged notes.
static void skip_judged(Play* p, u8 lane) {
    u16 i = p->cursor[lane];
    while (i != NONE && is_judged(p, i))
        i = p->next[i];
    p->cursor[lane] = i;
}

static void judge(Play* p, u16 i, Judgement j) {
    Score* s = &p->score;
    p->judged[i >> 3] |= (u8)(1u << (i & 7));
    p->judged_count++;
    if (j == JUDGE_MISS) {
        s->miss++;
        s->combo = 0;
        return;
    }
    u32 points = j == JUDGE_PERFECT ? 300 : 100;
    u16 c = s->combo < COMBO_CAP ? s->combo : COMBO_CAP;
    s->score += (points * multiplier[c]) >> 8;
    if (j == JUDGE_PERFECT)
        s->perfect++;
    else
        s->good++;
    s->combo++;
    if (s->combo > s->max_combo)
        s->max_combo = s->combo;
}

void play_start(Play* p, const RhythmNote* notes, u16 count, FIXED perfect_ticks,
                FIXED good_ticks) {
    if (multiplier[1] == 0)
        for (u16 c = 0; c <= COMBO_CAP; c++)
            multiplier[c] = (u16)(256 + c * 256 / COMBO_CAP);
    if (count > PLAY_MAX_NOTES)
        count = PLAY_MAX_NOTES;
    p->notes = notes;
    p->count = count;
    p->judged_count = 0;
    p->perfect = perfect_ticks;
    p->good = good_ticks;
    p->score = (Score){0};
    for (u16 i = 0; i < PLAY_MAX_NOTES / 8; i++)
        p->judged[i] = 0;
    u16 last[4];
    for (u8 l = 0; l < 4; l++)
        p->cursor[l] = last[l] = NONE;
    for (u16 i = 0; i < count; i++) {
        u8 l = notes[i].lane;
        p->next[i] = NONE;
        if (l > 3) {
            // A lane play_press() has no press for: the note is skipped,
            // set aside as judged at once, in no count of the score, so
            // the chart still ends (play_done()).
            p->judged[i >> 3] |= (u8)(1u << (i & 7));
            p->judged_count++;
            continue;
        }
        if (last[l] == NONE)
            p->cursor[l] = i;
        else
            p->next[last[l]] = i;
        last[l] = i;
    }
}

Judgement play_press(Play* p, u8 lane, FIXED press_ticks) {
    if (lane > 3)
        return JUDGE_NONE;
    skip_judged(p, lane);
    for (u16 i = p->cursor[lane]; i != NONE; i = p->next[i]) {
        if (is_judged(p, i))
            continue;
        FIXED error = press_ticks - note_time(p, i);
        if (error < -p->good)
            break; // this and every later note is still to come
        if (error > p->good)
            continue; // too old: play_expire() will take it
        Judgement j = (error <= p->perfect && error >= -p->perfect) ? JUDGE_PERFECT : JUDGE_GOOD;
        judge(p, i, j);
        return j;
    }
    return JUDGE_NONE;
}

u16 play_expire(Play* p, FIXED now_ticks) {
    u16 missed = 0;
    for (u8 lane = 0; lane < 4; lane++) {
        skip_judged(p, lane);
        for (u16 i = p->cursor[lane]; i != NONE; i = p->next[i]) {
            if (is_judged(p, i))
                continue;
            if (note_time(p, i) + p->good >= now_ticks)
                break; // notes are sorted: the rest are not old yet
            judge(p, i, JUDGE_MISS);
            missed++;
        }
        skip_judged(p, lane);
    }
    return missed;
}

bool play_done(const Play* p) {
    return p->judged_count == p->count;
}

const Score* play_score(const Play* p) {
    return &p->score;
}

u16 score_accuracy_permille(const Score* s) {
    u32 notes = (u32)s->perfect + s->good + s->miss;
    if (notes == 0)
        return 0;
    return (u16)(((u32)s->perfect * 3 + s->good) * 1000 / (3 * notes));
}

char grade_of(u16 accuracy_permille) {
    if (accuracy_permille >= 950)
        return 'S';
    if (accuracy_permille >= 900)
        return 'A';
    if (accuracy_permille >= 800)
        return 'B';
    if (accuracy_permille >= 700)
        return 'C';
    return 'D';
}

FIXED ms_to_ticks(s32 ms, u16 tick_samples) {
    if (tick_samples == 0)
        return 0;
    int64_t num = (int64_t)ms * MUSIC_RATE * 256;
    int64_t den = 1000 * (int64_t)tick_samples;
    int64_t half = den / 2;
    return (FIXED)(num >= 0 ? (num + half) / den : -((-num + half) / den));
}

s16 calibration_median_ms(const s16* errors_ms, u8 count) {
    if (count == 0)
        return 0;
    s16 sorted[255];
    for (u8 i = 0; i < count; i++) {
        s16 v = errors_ms[i];
        u8 j = i;
        while (j > 0 && sorted[j - 1] > v) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = v;
    }
    if (count & 1)
        return sorted[count / 2];
    return (s16)(((s32)sorted[count / 2 - 1] + sorted[count / 2]) / 2);
}
