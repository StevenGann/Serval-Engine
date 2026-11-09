// Sound effects and the two tunes, on the tone generators (audio.h).
//
// Who plays where. Effects play on square 1 and the noise channel, so the
// tunes keep what must not break on the other two: the melody on square 2 and
// the bass on the wave channel, which no effect ever takes. Square 1 gets the
// accompaniment (broken chords, a second voice, an echo) and the noise channel
// the drums: short notes all, so when an effect has borrowed the channel the
// tune is whole again with the next note (a track that fades comes back with
// its next note: docs/audio.md). The accompaniment and the drums are also the
// quietest voices, well under any effect.
//
// Who wins a channel when two effects want it is decided by priority:
//   0  the wand's spark and its dry click, menu blips
//   1  wisps: waking, striking, hurt; doors; a spark knocking an urn
//   2  a wisp gone, a hex cast, pickups, the locked door
//   3  the serval hurt, an urn bursting, the key, the unlocked door, pause,
//      the end jingles
// The tunes have priority 0, so every effect plays over them.
//
// Both tunes are built on one motif, the wisps' call: up a fifth, a flick to
// the note above and back, down a third. The title plays it as a waltz in D
// minor, the warren as a march in A minor, and the two end jingles are made
// of it too: in A major when the serval gets out, falling away when not.

#include "game.h"

// Note frequencies in Hz, for effects (the tunes use PSG_* note numbers).
#define A3 220
#define C4 262
#define D4 294
#define E4 330
#define F4 349
#define G4 392
#define GS4 415
#define A4 440
#define B4 494
#define C5 523
#define CS5 554
#define D5 587
#define E5 659
#define F5 698
#define FS5 740
#define G5 784
#define A5 880
#define C6 1047
#define E6 1319
#define G6 1568
#define C7 2093

static const u16 wisp_hurt_notes[] = {760, 520};
// A hollow knock: the clay's ring, low, then lower.
static const u16 urn_notes[] = {330, 196, 147};
static const u16 wisp_gone_notes[] = {980, 740, 560, 420, 310, 230};
static const u16 wisp_wakes_notes[] = {330, 0, 440, 660};
static const u16 hex_notes[] = {392, 494, 622, 784, 988, 1245, 1568};
static const u16 pickup_notes[] = {C6, E6, G6};
static const u16 key_notes[] = {G5, C6, E6, G6, C7};
static const u16 locked_notes[] = {196, 0, 196};
static const u16 unlock_notes[] = {E5, G5, C6, G6};
static const u16 confirm_notes[] = {G5, C6};
static const u16 pause_notes[] = {C6, G5};

// The end jingles: one voice each, as every sound is, so their rhythm is
// written out in steps of .frames: a note repeated is a note held (it holds
// its volume, so the steps run together), and a 0 lifts it.
//
// Out through the moon gate: a fanfare in A major, in 16th notes of 5 frames.
// The wisps' call as a march ("dum, da DUM, da-da dum"), then the chords of F,
// G and A climbing to the keynote, and the keynote once more.
static const u16 won_notes[] = {
    A4, A4,  0,  A4, E5, E5, E5, E5, FS5, FS5, E5, E5, CS5, CS5, CS5, 0, // the call
    F4, A4,  C5, F5, F5, 0,                                              // F
    G4, B4,  D5, G5, G5, 0,                                              // G
    A4, CS5, E5, A5, A5, A5, 0,                                          // A
    E5, A5,  A5, A5, A5, A5, A5, A5, A5,                                 // and home
};
// Caught, in steps of 7 frames: the call's tail (the flick, the falling
// thirds) twice, sinking, then the chord of A minor let down to its lowest
// note.
static const u16 lost_notes[] = {
    E5, E5, F5, E5, C5, C5, A4,  A4,                  // from the call's top
    D5, D5, E5, D5, B4, B4, GS4, GS4,                 // a step lower
    A4, A4, E4, E4, C4, C4, A3,  A3,  A3, A3, A3, A3, // down and out
};

#define NOTES(n) .notes = n, .note_count = sizeof n / sizeof n[0]

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_SPARK] = {.frequency = 1500,
                   .frames = 7,
                   .volume = 12,
                   .fade = -1,
                   .slide = -2,
                   .slide_size = 2,
                   .duty = PSG_DUTY_25},
    [SND_DRY] = {.channel = PSG_NOISE, .frequency = 3000, .frames = 2, .volume = 10},
    [SND_WISP_HURT] =
        {.frames = 3, .volume = 11, .duty = PSG_DUTY_12, .priority = 1, NOTES(wisp_hurt_notes)},
    [SND_WISP_GONE] =
        {.frames = 3, .volume = 12, .duty = PSG_DUTY_12, .priority = 2, NOTES(wisp_gone_notes)},
    [SND_WISP_WAKES] =
        {.frames = 4, .volume = 9, .duty = PSG_DUTY_25, .priority = 1, NOTES(wisp_wakes_notes)},
    [SND_STRIKE] = {.channel = PSG_NOISE,
                    .frequency = 900,
                    .frames = 8,
                    .volume = 12,
                    .fade = -2,
                    .priority = 1},
    [SND_HEX] = {.frames = 2, .volume = 10, .duty = PSG_DUTY_25, .priority = 2, NOTES(hex_notes)},
    [SND_HURT] = {.channel = PSG_NOISE,
                  .frequency = 260,
                  .frames = 12,
                  .volume = 15,
                  .fade = -2,
                  .priority = 3},
    // The knock on square 1 (over the spark's zap). The burst is two at
    // once: a deep roar of noise, fading slowly as it rolls round the walls,
    // and a thump on square 1 that drops away under it.
    [SND_URN] = {.frames = 2, .volume = 12, .duty = PSG_DUTY_12, .priority = 1, NOTES(urn_notes)},
    [SND_BLAST] = {.channel = PSG_NOISE,
                   .frequency = 110,
                   .frames = 34,
                   .volume = 15,
                   .fade = -5,
                   .priority = 3},
    [SND_BOOM] = {.frequency = 150,
                  .frames = 24,
                  .volume = 15,
                  .fade = -3,
                  .slide = -3,
                  .slide_size = 3,
                  .priority = 3},
    [SND_PICKUP] = {.frames = 3, .volume = 10, .priority = 2, NOTES(pickup_notes)},
    [SND_KEY] = {.frames = 4, .volume = 12, .priority = 3, NOTES(key_notes)},
    [SND_DOOR] = {.channel = PSG_NOISE,
                  .frequency = 160,
                  .frames = 20,
                  .volume = 13,
                  .fade = -4,
                  .priority = 1},
    [SND_LOCKED] =
        {.frames = 4, .volume = 13, .duty = PSG_DUTY_25, .priority = 2, NOTES(locked_notes)},
    [SND_UNLOCK] = {.frames = 4, .volume = 12, .priority = 3, NOTES(unlock_notes)},
    [SND_MOVE] = {.frequency = 880, .frames = 3, .volume = 11},
    [SND_CONFIRM] = {.frames = 4, .volume = 10, NOTES(confirm_notes)},
    [SND_PAUSE] = {.frames = 4, .volume = 10, .priority = 3, NOTES(pause_notes)},
    [SND_WON] = {.frames = 5, .volume = 12, .duty = PSG_DUTY_25, .priority = 3, NOTES(won_notes)},
    [SND_LOST] = {.frames = 7, .volume = 12, .priority = 3, NOTES(lost_notes)},
};

static const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_SPARK] = &sounds[SND_SPARK],
    [SND_DRY] = &sounds[SND_DRY],
    [SND_WISP_HURT] = &sounds[SND_WISP_HURT],
    [SND_WISP_GONE] = &sounds[SND_WISP_GONE],
    [SND_WISP_WAKES] = &sounds[SND_WISP_WAKES],
    [SND_STRIKE] = &sounds[SND_STRIKE],
    [SND_HEX] = &sounds[SND_HEX],
    [SND_HURT] = &sounds[SND_HURT],
    [SND_URN] = &sounds[SND_URN],
    [SND_BLAST] = &sounds[SND_BLAST],
    [SND_BOOM] = &sounds[SND_BOOM],
    [SND_PICKUP] = &sounds[SND_PICKUP],
    [SND_KEY] = &sounds[SND_KEY],
    [SND_DOOR] = &sounds[SND_DOOR],
    [SND_LOCKED] = &sounds[SND_LOCKED],
    [SND_UNLOCK] = &sounds[SND_UNLOCK],
    [SND_MOVE] = &sounds[SND_MOVE],
    [SND_CONFIRM] = &sounds[SND_CONFIRM],
    [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_WON] = &sounds[SND_WON],
    [SND_LOST] = &sounds[SND_LOST],
};

// --- The tunes ----------------------------------------------------------------------
//
// A track has one tone color, volume and envelope for all its notes, so a
// tune's light and shade come from who plays: the drums drop out, the
// accompaniment changes its figure, the melody climbs or rests.
//
// The notes are written N(A4, 2), the note PSG_A4 for 2 ticks (a tick is a
// 16th note), and R(2), a rest; the macros for a bar or half a bar of an
// accompaniment take the same names. (## pastes a name as it is written, so
// the effects' A4 above, 440, doesn't come between.)
#define NOTE(note, ticks) {(note), (ticks)}
#define N(name, ticks) NOTE(PSG_##name, ticks)
#define R(ticks) NOTE(PSG_REST, ticks)
#define TRACK_NOTES(n) .notes = n, .note_count = sizeof n / sizeof n[0]

// The drums: on the noise channel a note picks the noise's rate.
#define KICK(ticks) NOTE(PSG_C4, ticks)  // a low thump
#define SNARE(ticks) NOTE(PSG_C8, ticks) // a burst
#define HAT(ticks) NOTE(PSG_C10, ticks)  // a hiss

// The bass's two waveforms (psg_waves_set), 32 steps each, two steps a byte
// with the first in the high nibble. Round, for the waltz: sin x + 0.25 sin
// 2x + 0.08 sin 3x, a plucked string. Reedy, for the march: five harmonics
// (1, 0.55, 0.35, 0.2, 0.1), so that its low E and F, whose fundamentals a
// small speaker loses, are heard by their overtones.
enum { WAVE_ROUND, WAVE_REED, WAVE_COUNT };
static const u32 bass_waves[WAVE_COUNT * 4] = {
    0xFEFFDE9B, 0x98BACBED, 0x21435476, 0x46120010, // WAVE_ROUND
    0xCBEDFFAD, 0x8899A9BB, 0x44656677, 0x25002143, // WAVE_REED
};

// --- TUNE_TITLE: the wisps' waltz ---------------------------------------------------
//
// D minor, 3/4, 126 beats a minute, 12 ticks to the bar. Two bars of harp,
// then 32 bars that loop (46 seconds):
//   A  (1-16)   the call and three answers to it, in D minor; the third
//               time it starts from F and climbs to the tune's highest note
//   B  (17-32)  in the major keys next door: long notes rising by step over
//               a walking bass and harp sweeps, to a cadence of B flat, C
//               and D major in fanfare rhythm; then A7 turns it back to D
//               minor and the call
// The melody's last bar leads into its first, so the loop has no seam.

static const PsgNote title_lead[] = {
    R(24),                                       // the harp alone
    N(D5, 4),   N(A5, 4),  N(AS5, 2), N(A5, 2),  // 1 Dm: the call
    N(F5, 8),   N(E5, 4),                        // 2 Dm
    N(D5, 4),   N(G5, 4),  N(F5, 4),             // 3 Bb
    N(E5, 8),   N(CS5, 4),                       // 4 A
    N(D5, 4),   N(A5, 4),  N(AS5, 2), N(A5, 2),  // 5 Dm
    N(C6, 8),   N(A5, 4),                        // 6 F
    N(AS5, 4),  N(A5, 4),  N(G5, 4),             // 7 Gm
    N(A5, 8),   N(E5, 4),                        // 8 A
    N(F5, 4),   N(C6, 4),  N(D6, 2),  N(C6, 2),  // 9 F: the call, a third up
    N(A5, 8),   N(G5, 4),                        // 10 F
    N(AS5, 6),  N(C6, 2),  N(D6, 4),             // 11 Bb
    N(CS6, 8),  N(A5, 4),                        // 12 A
    N(D6, 4),   N(A5, 4),  N(F5, 4),             // 13 Dm
    N(E5, 4),   N(G5, 4),  N(AS5, 4),            // 14 Gm6
    N(A5, 6),   N(G5, 2),  N(E5, 2),  N(CS5, 2), // 15 A7
    N(D5, 8),   R(4),                            // 16 Dm
    N(D5, 8),   N(F5, 4),                        // 17 Bb
    N(E5, 8),   N(G5, 4),                        // 18 C
    N(A5, 8),   N(E5, 4),                        // 19 Am
    N(F5, 8),   R(4),                            // 20 Dm
    N(D5, 4),   N(F5, 4),  N(AS5, 4),            // 21 Bb
    N(E5, 4),   N(G5, 4),  N(C6, 4),             // 22 C
    N(C6, 6),   N(AS5, 2), N(A5, 4),             // 23 F
    N(F5, 8),   R(4),                            // 24 F
    N(F5, 8),   N(AS5, 4),                       // 25 Bb
    N(G5, 8),   N(C6, 4),                        // 26 C
    N(A5, 8),   N(D6, 4),                        // 27 Dm
    N(C6, 4),   N(A5, 4),  N(F5, 4),             // 28 Dm7
    N(D5, 6),   N(D5, 2),  N(D5, 4),             // 29 Bb
    N(E5, 6),   N(E5, 2),  N(E5, 4),             // 30 C
    N(FS5, 12),                                  // 31 D major
    N(G5, 4),   N(E5, 4),  N(CS5, 4),            // 32 A7, back to the call
};

// The harp: broken chords in 8th notes, a bar each.
// clang-format off
#define HARP(a, b, c, d, e, f)                                                                     \
    NOTE(PSG_##a, 2), NOTE(PSG_##b, 2), NOTE(PSG_##c, 2), NOTE(PSG_##d, 2), NOTE(PSG_##e, 2),      \
    NOTE(PSG_##f, 2)
// In A, up a chord and down again.
#define HARP_ROCK(a, b, c, d)                                                                      \
    NOTE(PSG_##a, 2), NOTE(PSG_##b, 2), NOTE(PSG_##c, 2), NOTE(PSG_##d, 2), NOTE(PSG_##c, 2),      \
    NOTE(PSG_##b, 2)
// At the cadence, 16th notes: up a chord through two octaves and down again.
#define HARP_RUN(a, b, c)                                                                          \
    NOTE(PSG_##a, 1), NOTE(PSG_##b, 1), NOTE(PSG_##c, 1), NOTE(PSG_##a + 12, 1),                   \
    NOTE(PSG_##b + 12, 1), NOTE(PSG_##c + 12, 1), NOTE(PSG_##a + 24, 1), NOTE(PSG_##c + 12, 1),    \
    NOTE(PSG_##b + 12, 1), NOTE(PSG_##a + 12, 1), NOTE(PSG_##c, 1), NOTE(PSG_##b, 1)
// clang-format on

static const PsgNote title_harp[] = {
    HARP_ROCK(D4, F4, A4, D5),      // intro
    HARP(D4, F4, A4, D5, AS4, A4),  // ... and the call's flick
    HARP(D4, F4, A4, F4, D5, A4),   // 1 Dm
    HARP(D4, F4, A4, D5, A4, D4),   // 2 Dm
    HARP_ROCK(D4, F4, AS4, D5),     // 3 Bb
    HARP_ROCK(CS4, E4, A4, CS5),    // 4 A
    HARP(D4, F4, A4, F4, D5, A4),   // 5 Dm
    HARP_ROCK(C4, F4, A4, C5),      // 6 F
    HARP(D4, AS4, G4, D5, G4, AS4), // 7 Gm
    HARP_ROCK(CS4, E4, A4, CS5),    // 8 A
    HARP_ROCK(C4, F4, A4, C5),      // 9 F
    HARP_ROCK(C4, F4, A4, C5),      // 10 F
    HARP_ROCK(D4, F4, AS4, D5),     // 11 Bb
    HARP_ROCK(CS4, E4, A4, CS5),    // 12 A
    HARP_ROCK(D4, F4, A4, D5),      // 13 Dm
    HARP_ROCK(E4, G4, AS4, D5),     // 14 Gm6
    HARP_ROCK(CS4, E4, G4, A4),     // 15 A7
    HARP_ROCK(D4, F4, A4, D5),      // 16 Dm
    HARP(AS3, D4, F4, AS4, D5, F5), // 17 Bb: in B, sweeping up
    HARP(C4, E4, G4, C5, E5, G5),   // 18 C
    HARP(A3, C4, E4, A4, C5, E5),   // 19 Am
    HARP(A3, D4, F4, A4, D5, F5),   // 20 Dm
    HARP(AS3, D4, F4, AS4, D5, F5), // 21 Bb
    HARP(C4, E4, G4, C5, E5, G5),   // 22 C
    HARP(C4, F4, A4, C5, F5, A5),   // 23 F
    HARP(F5, C5, A4, F4, C4, A3),   // 24 F, down again
    HARP(AS3, D4, F4, AS4, D5, F5), // 25 Bb
    HARP(C4, E4, G4, C5, E5, G5),   // 26 C
    HARP(D4, F4, A4, D5, F5, A5),   // 27 Dm
    HARP(A5, F5, D5, C5, A4, F4),   // 28 Dm7
    HARP_RUN(AS3, D4, F4),          // 29 Bb
    HARP_RUN(C4, E4, G4),           // 30 C
    HARP_RUN(D4, FS4, A4),          // 31 D major
    HARP(A3, CS4, E4, G4, E4, CS4), // 32 A7
};

// The bass: a waltz's, the chord's root on the first beat and a step toward
// the next chord on the third; in B it walks, a note to the beat; at the
// cadence it takes the melody's rhythm, "dum, da dum".
#define WALTZ(a, b) NOTE(PSG_##a, 8), NOTE(PSG_##b, 4)
#define WALK(a, b, c) NOTE(PSG_##a, 4), NOTE(PSG_##b, 4), NOTE(PSG_##c, 4)
#define FANFARE(a, b) NOTE(PSG_##a, 6), NOTE(PSG_##b, 2), NOTE(PSG_##a, 4)

static const PsgNote title_bass[] = {
    WALTZ(D3, A2),     N(D3, 12),        // intro
    WALTZ(D3, F3),     WALTZ(D3, C3),    // 1 Dm, 2 Dm
    WALTZ(AS2, D3),    WALTZ(A2, E3),    // 3 Bb, 4 A
    WALTZ(D3, F3),     WALTZ(F2, C3),    // 5 Dm, 6 F
    WALTZ(G2, AS2),    WALTZ(A2, E3),    // 7 Gm, 8 A
    WALTZ(F3, C3),     WALTZ(A2, C3),    // 9 F, 10 F
    WALTZ(AS2, G2),    WALTZ(A2, CS3),   // 11 Bb, 12 A
    WALTZ(D3, F3),     WALTZ(G2, AS2),   // 13 Dm, 14 Gm6
    WALTZ(A2, E3),     WALTZ(D3, C3),    // 15 A7, 16 Dm
    WALK(AS2, F3, D3), WALK(C3, G3, E3), // 17 Bb, 18 C
    WALK(A2, C3, E3),  WALK(D3, F3, C3), // 19 Am, 20 Dm
    WALK(AS2, D3, F3), WALK(C3, E3, G3), // 21 Bb, 22 C
    WALK(F3, C3, F3),  WALK(F3, C3, A2), // 23 F, 24 F
    WALK(AS2, F3, D3), WALK(C3, G3, E3), // 25 Bb, 26 C
    WALK(D3, F3, A3),  WALTZ(C3, A2),    // 27 Dm, 28 Dm7
    FANFARE(AS2, F3),  FANFARE(C3, G3),  // 29 Bb, 30 C
    WALTZ(D3, A2),     WALK(A2, E3, A2), // 31 D major, 32 A7
};

// The drums stay out of the call's first eight bars, tick the second and
// third beats of the next eight, and take the first beat too in B.
#define TICKS R(4), HAT(4), HAT(4)
#define OOM_TICKS KICK(4), HAT(4), HAT(4)

static const PsgNote title_drums[] = {
    R(24),                                                         // intro
    R(96),                                                         // 1-8
    TICKS,     TICKS,     TICKS,     TICKS,                        // 9-12
    TICKS,     TICKS,     TICKS,     TICKS,                        // 13-16
    OOM_TICKS, OOM_TICKS, OOM_TICKS, OOM_TICKS,                    // 17-20
    OOM_TICKS, OOM_TICKS, OOM_TICKS, OOM_TICKS,                    // 21-24
    OOM_TICKS, OOM_TICKS, OOM_TICKS, OOM_TICKS,                    // 25-28
    KICK(6),   KICK(2),   SNARE(4),  KICK(6),   KICK(2), SNARE(4), // 29-30: the fanfare's rhythm
    OOM_TICKS,                                                     // 31
    SNARE(4),  SNARE(4),  SNARE(2),  SNARE(2),                     // 32
};

// Each track loops from where the intro ends.
static const PsgTrack title_tracks[] = {
    // The melody: a full square wave that rings for a second, like a bell.
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_50,
     .volume = 9,
     .fade = -7,
     .loop = 1,
     TRACK_NOTES(title_lead)},
    // The harp: thin and soft, plucked.
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_12,
     .volume = 5,
     .fade = -3,
     .loop = 12,
     TRACK_NOTES(title_harp)},
    // The bass: plucked at 75% of the wave channel's volume, the lowest volume
    // that plays so, then fading through 50% and 25%.
    {.channel = PSG_WAVE,
     .duty = WAVE_ROUND,
     .volume = 10,
     .fade = -5,
     .loop = 3,
     TRACK_NOTES(title_bass)},
    {.channel = PSG_NOISE, .volume = 4, .fade = -1, .loop = 1, TRACK_NOTES(title_drums)},
};

static const PsgSong title_song = {.tempo = 126, .tracks = title_tracks, .track_count = 4};

// --- TUNE_WARREN: the warren's march ------------------------------------------------
//
// A minor, 4/4, 120 beats a minute, 16 ticks to the bar; 36 bars that loop
// (72 seconds):
//   vamp (1-4)    the bass and the drums alone, then the chords: the breath
//                 between two rounds of the tune
//   A   (5-12)    the call as a march and its answer, three times, to a
//                 half close by way of B flat, the note that doesn't belong
//   A'  (13-20)   again with a second voice under it, a third or a sixth
//                 below; the third time climbs through G and C to the top
//                 and closes in A minor
//   B   (21-28)   creeping: the call's flick alone, a semitone down and back,
//                 each answered by its echo on square 1, over a heartbeat of
//                 a bass, between A minor and B flat; a drum roll out of it
//   C   (29-36)   the way out: long high notes over F and G, the call
//                 answering from below, and E major's close into the vamp

static const PsgNote warren_lead[] = {
    R(60),    N(E4, 2),  N(GS4, 2),                                // vamp, and a way in
    N(A4, 3), N(A4, 1),  N(E5, 4),  N(F5, 2),  N(E5, 2), N(C5, 4), // 5 Am: the call
    N(D5, 2), N(C5, 2),  N(B4, 2),  N(C5, 2),  N(A4, 6), R(2),     // 6 Am: its answer
    N(A4, 3), N(A4, 1),  N(F5, 4),  N(G5, 2),  N(F5, 2), N(C5, 4), // 7 F
    N(E5, 2), N(D5, 2),  N(C5, 2),  N(D5, 2),  N(B4, 6), R(2),     // 8 E
    N(A4, 3), N(A4, 1),  N(E5, 4),  N(F5, 2),  N(E5, 2), N(A5, 4), // 9 Am
    N(G5, 4), N(F5, 4),  N(E5, 2),  N(F5, 2),  N(D5, 4),           // 10 Dm
    N(F5, 3), N(F5, 1),  N(D5, 4),  N(AS4, 4), N(D5, 4),           // 11 Bb
    N(E5, 6), N(D5, 2),  N(B4, 4),  N(GS4, 4),                     // 12 E
    N(A4, 3), N(A4, 1),  N(E5, 4),  N(F5, 2),  N(E5, 2), N(C5, 4), // 13 Am
    N(D5, 2), N(C5, 2),  N(B4, 2),  N(C5, 2),  N(A4, 6), R(2),     // 14 Am
    N(A4, 3), N(A4, 1),  N(F5, 4),  N(G5, 2),  N(F5, 2), N(A5, 4), // 15 F
    N(D5, 4), N(G5, 4),  N(A5, 4),  N(B5, 4),                      // 16 G
    N(C6, 6), N(B5, 2),  N(G5, 4),  N(E5, 4),                      // 17 C
    N(F5, 6), N(E5, 2),  N(D5, 4),  N(F5, 4),                      // 18 Dm
    N(E5, 4), N(D5, 4),  N(B4, 4),  N(GS4, 4),                     // 19 E
    N(A4, 8), R(8),                                                // 20 Am
    R(4),     N(E5, 2),  N(DS5, 2), N(E5, 6),  R(2),               // 21 Am
    R(4),     N(C5, 2),  N(B4, 2),  N(C5, 6),  R(2),               // 22 Am
    R(4),     N(F5, 2),  N(E5, 2),  N(F5, 6),  R(2),               // 23 Bb
    R(4),     N(D5, 2),  N(CS5, 2), N(D5, 6),  R(2),               // 24 Bb
    R(4),     N(A5, 2),  N(GS5, 2), N(A5, 6),  R(2),               // 25 Am
    R(4),     N(E5, 2),  N(DS5, 2), N(E5, 6),  R(2),               // 26 Am
    R(4),     N(AS5, 2), N(A5, 2),  N(AS5, 6), R(2),               // 27 Bb
    R(4),     N(B4, 4),  N(D5, 4),  N(E5, 4),                      // 28 E7
    N(F5, 8), N(A5, 4),  N(C6, 4),                                 // 29 F
    N(B5, 8), N(G5, 4),  N(B5, 4),                                 // 30 G
    N(C6, 8), N(A5, 4),  N(E5, 4),                                 // 31 Am
    N(A4, 3), N(A4, 1),  N(E5, 4),  N(F5, 2),  N(E5, 2), N(C5, 4), // 32 Am: the call
    N(F5, 8), N(A5, 4),  N(C6, 4),                                 // 33 F
    N(D6, 8), N(B5, 4),  N(G5, 4),                                 // 34 G
    N(B5, 8), N(GS5, 4), N(E5, 4),                                 // 35 E
    N(E5, 3), N(E5, 1),  N(E5, 4),  R(8),                          // 36 E
};

// Square 1, half a bar at a time. Under the march, chords: two quick notes
// after each beat, leaving the beat itself to the bass and the drums.
// clang-format off
#define GALLOP(a, b, c)                                                                            \
    R(2), NOTE(PSG_##c, 1), NOTE(PSG_##b, 1), R(2), NOTE(PSG_##b, 1), NOTE(PSG_##a, 1)
// clang-format on
// Under C's long notes, the chord rocking in 8th notes, an octave higher.
#define ROCK(a, b, c) NOTE(PSG_##a, 2), NOTE(PSG_##b, 2), NOTE(PSG_##c, 2), NOTE(PSG_##b, 2)
// In B, a bar: the melody's flick again, three beats late: the end of one
// echo and the start of the next.
#define ECHO(last, a, b) NOTE(PSG_##last, 4), R(8), NOTE(PSG_##a, 2), NOTE(PSG_##b, 2)

// clang-format off
static const PsgNote warren_chords[] = {
    R(32),                                                       // 1-2
    GALLOP(C4, E4, A4),  GALLOP(C4, E4, A4),                     // 3 Am
    GALLOP(C4, E4, A4),  GALLOP(B3, E4, GS4),                    // 4 Am, E
    GALLOP(C4, E4, A4),  GALLOP(C4, E4, A4),                     // 5 Am
    GALLOP(C4, E4, A4),  GALLOP(C4, E4, A4),                     // 6 Am
    GALLOP(C4, F4, A4),  GALLOP(C4, F4, A4),                     // 7 F
    GALLOP(B3, E4, GS4), GALLOP(B3, E4, GS4),                    // 8 E
    GALLOP(C4, E4, A4),  GALLOP(C4, E4, A4),                     // 9 Am
    GALLOP(D4, F4, A4),  GALLOP(D4, F4, A4),                     // 10 Dm
    GALLOP(D4, F4, AS4), GALLOP(D4, F4, AS4),                    // 11 Bb
    GALLOP(B3, E4, GS4), GALLOP(B3, E4, GS4),                    // 12 E
    N(E4, 3),  N(E4, 1), N(C5, 4),  N(D5, 2), N(C5, 2), N(A4, 4), // 13 Am: the second voice
    N(B4, 2),  N(A4, 2), N(G4, 2),  N(A4, 2), N(E4, 6), R(2),    // 14 Am
    N(F4, 3),  N(F4, 1), N(A4, 4),  N(C5, 2), N(A4, 2), N(C5, 4), // 15 F
    N(B4, 4),  N(D5, 4), N(F5, 4),  N(G5, 4),                    // 16 G
    N(E5, 6),  N(D5, 2), N(E5, 4),  N(C5, 4),                    // 17 C
    N(D5, 6),  N(C5, 2), N(A4, 4),  N(D5, 4),                    // 18 Dm
    N(GS4, 4), N(B4, 4), N(GS4, 4), N(E4, 4),                    // 19 E
    N(E4, 4),  N(C4, 4), R(8),                                   // 20 Am
    R(12),     N(E5, 2), N(DS5, 2),                              // 21 Am: the first echo
    ECHO(E5, C5, B4),                                            // 22 Am
    ECHO(C5, F5, E5),                                            // 23 Bb
    ECHO(F5, D5, CS5),                                           // 24 Bb
    ECHO(D5, A5, GS5),                                           // 25 Am
    ECHO(A5, E5, DS5),                                           // 26 Am
    ECHO(E5, AS5, A5),                                           // 27 Bb
    R(8),      N(E4, 1), N(GS4, 1), N(B4, 1), N(D5, 1),          // 28 E7: a run up
    N(E5, 1),  N(GS5, 1), N(B5, 1), N(D6, 1),                    //    into C
    ROCK(A4, C5, F5),    ROCK(A4, C5, F5),                       // 29 F
    ROCK(B4, D5, G5),    ROCK(B4, D5, G5),                       // 30 G
    ROCK(A4, C5, E5),    ROCK(A4, C5, E5),                       // 31 Am
    GALLOP(C4, E4, A4),  GALLOP(C4, E4, A4),                     // 32 Am: under the call
    ROCK(A4, C5, F5),    ROCK(A4, C5, F5),                       // 33 F
    ROCK(B4, D5, G5),    ROCK(B4, D5, G5),                       // 34 G
    ROCK(GS4, B4, E5),   ROCK(GS4, B4, E5),                      // 35 E
    ROCK(GS4, B4, E5),   N(E5, 1), N(D5, 1), N(B4, 1), N(GS4, 1), // 36 E, and a run down
    N(E4, 1),  N(D4, 1), N(B3, 1),  N(GS3, 1),                   //    into the vamp
};
// clang-format on

// The bass drives the march: "dum, da dum dah" on the chord's root, twice a
// bar, the fourth note its fifth or a step toward the next root.
#define DRIVE(root, last)                                                                          \
    NOTE(PSG_##root, 3), NOTE(PSG_##root, 1), NOTE(PSG_##root, 2), NOTE(PSG_##last, 2)
// In B, a bar of heartbeat. (The bass never rests: its notes fade to
// silence by themselves, and the wave channel is left on.)
#define PULSE(root) NOTE(PSG_##root, 3), NOTE(PSG_##root, 5), NOTE(PSG_##root, 8)

static const PsgNote warren_bass[] = {
    DRIVE(A2, E3),  DRIVE(A2, E3),                                           // 1 Am
    DRIVE(A2, E3),  DRIVE(A2, G2),                                           // 2 Am
    DRIVE(A2, E3),  DRIVE(A2, E3),                                           // 3 Am
    DRIVE(A2, E3),  DRIVE(E3, GS2),                                          // 4 Am, E
    DRIVE(A2, E3),  DRIVE(A2, E3),                                           // 5 Am
    DRIVE(A2, E3),  DRIVE(A2, G2),                                           // 6 Am
    DRIVE(F2, C3),  DRIVE(F2, C3),                                           // 7 F
    DRIVE(E2, B2),  DRIVE(E2, GS2),                                          // 8 E
    DRIVE(A2, E3),  DRIVE(A2, C3),                                           // 9 Am
    DRIVE(D3, A2),  DRIVE(D3, C3),                                           // 10 Dm
    DRIVE(AS2, F3), DRIVE(AS2, F2),                                          // 11 Bb
    DRIVE(E2, B2),  DRIVE(E2, GS2),                                          // 12 E
    DRIVE(A2, E3),  DRIVE(A2, E3),                                           // 13 Am
    DRIVE(A2, E3),  DRIVE(A2, G2),                                           // 14 Am
    DRIVE(F2, C3),  DRIVE(F2, A2),                                           // 15 F
    DRIVE(G2, D3),  DRIVE(G2, B2),                                           // 16 G
    DRIVE(C3, G2),  DRIVE(C3, E3),                                           // 17 C
    DRIVE(D3, A2),  DRIVE(D3, F2),                                           // 18 Dm
    DRIVE(E2, B2),  DRIVE(E2, GS2),                                          // 19 E
    DRIVE(A2, E3),  N(A2, 8),                                                // 20 Am
    PULSE(A2),      PULSE(A2),                                               // 21-22 Am
    PULSE(AS2),     PULSE(AS2),                                              // 23-24 Bb
    PULSE(A2),      PULSE(A2),                                               // 25-26 Am
    PULSE(AS2),                                                              // 27 Bb
    N(E2, 3),       N(E2, 5),       N(E2, 2), N(E2, 2), N(E2, 2),  N(E2, 2), // 28 E7
    DRIVE(F2, C3),  DRIVE(F2, A2),                                           // 29 F
    DRIVE(G2, D3),  DRIVE(G2, GS2),                                          // 30 G
    DRIVE(A2, E3),  DRIVE(A2, E3),                                           // 31 Am
    DRIVE(A2, E3),  DRIVE(A2, G2),                                           // 32 Am
    DRIVE(F2, C3),  DRIVE(F2, FS2),                                          // 33 F
    DRIVE(G2, D3),  DRIVE(G2, F2),                                           // 34 G
    DRIVE(E2, B2),  DRIVE(E2, E3),                                           // 35 E
    DRIVE(E2, B2),  N(E3, 2),       N(D3, 2), N(B2, 2), N(GS2, 2),           // 36 E, and down to A
};

// The drums, a bar at a time. The march: kick on the first and third beats,
// snare on the second and fourth, the hat between them.
#define DRUM_VAMP KICK(4), HAT(2), HAT(2), KICK(4), HAT(2), HAT(2)
#define DRUM_MARCH KICK(2), HAT(2), SNARE(2), HAT(2), KICK(2), HAT(2), SNARE(2), HAT(2)
// clang-format off
// Its other bar, with a second kick and a ruff on the last beat.
#define DRUM_RUFF                                                                                  \
    KICK(2), HAT(2), SNARE(2), HAT(2), KICK(2), KICK(2), SNARE(2), SNARE(1), SNARE(1)
// A fill, at the end of a section.
#define DRUM_FILL                                                                                  \
    KICK(2), HAT(2), SNARE(2), HAT(2), SNARE(1), SNARE(1), SNARE(2), SNARE(1), SNARE(1), SNARE(1), \
    SNARE(1)
// clang-format on
// B: the kick with the bass's heartbeat, the hat ticking off the beat like
// a clock; then a snare on the fourth beat as well.
#define DRUM_CREEP KICK(3), KICK(1), R(2), HAT(2), R(2), HAT(2), R(2), HAT(2)
#define DRUM_CREEP_SNARE KICK(3), KICK(1), R(2), HAT(2), R(2), HAT(2), SNARE(2), HAT(2)

// clang-format off
static const PsgNote warren_drums[] = {
    DRUM_VAMP,        DRUM_VAMP,        DRUM_MARCH,       DRUM_FILL,  // 1-4
    DRUM_MARCH,       DRUM_RUFF,        DRUM_MARCH,       DRUM_RUFF,  // 5-8
    DRUM_MARCH,       DRUM_RUFF,        DRUM_MARCH,       DRUM_FILL,  // 9-12
    DRUM_MARCH,       DRUM_RUFF,        DRUM_MARCH,       DRUM_RUFF,  // 13-16
    DRUM_MARCH,       DRUM_RUFF,        DRUM_MARCH,                   // 17-19
    KICK(2), HAT(2),  SNARE(2), HAT(2), KICK(4), R(4),                // 20: a stop
    DRUM_CREEP,       DRUM_CREEP,       DRUM_CREEP,       DRUM_CREEP, // 21-24
    DRUM_CREEP_SNARE, DRUM_CREEP_SNARE, DRUM_CREEP_SNARE,             // 25-27
    KICK(3), KICK(1), R(4),             SNARE(2), SNARE(2),           // 28: a roll
    SNARE(1), SNARE(1), SNARE(1), SNARE(1),                           //     into C
    DRUM_MARCH,       DRUM_RUFF,        DRUM_MARCH,       DRUM_RUFF,  // 29-32
    DRUM_MARCH,       DRUM_RUFF,        DRUM_MARCH,       DRUM_FILL,  // 33-36
};
// clang-format on

static const PsgTrack warren_tracks[] = {
    // The melody: a quarter-width pulse, brighter than the title's.
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 7,
     .fade = -7,
     TRACK_NOTES(warren_lead)},
    // Chords, second voice and echo: thin, soft and short.
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_12,
     .volume = 5,
     .fade = -3,
     TRACK_NOTES(warren_chords)},
    // The bass: plucked short (silent in a fifth of a second), so that a note
    // repeated is heard twice and the march keeps its step.
    {.channel = PSG_WAVE, .duty = WAVE_REED, .volume = 13, .fade = -1, TRACK_NOTES(warren_bass)},
    {.channel = PSG_NOISE, .volume = 6, .fade = -1, TRACK_NOTES(warren_drums)},
};

static const PsgSong warren_song = {.tempo = 120, .tracks = warren_tracks, .track_count = 4};

void sound_init(void) {
    psg_table_set(sound_table, SOUND_COUNT);
    psg_waves_set(bass_waves, WAVE_COUNT);
}

void sound_music(u32 tune) {
    static const PsgSong* const songs[TUNE_COUNT] = {
        [TUNE_TITLE] = &title_song,
        [TUNE_WARREN] = &warren_song,
    };
    static u8 playing = TUNE_NONE;
    if (tune >= TUNE_COUNT || tune == playing)
        return;
    playing = (u8)tune;
    if (tune == TUNE_NONE)
        psg_music_stop();
    else
        psg_music_play(songs[tune]);
}
