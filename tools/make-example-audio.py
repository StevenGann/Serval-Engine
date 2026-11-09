#!/usr/bin/env python3
"""Make the tracker modules and WAV samples of the engine's examples and tests.

Every note, tune and sound here is original, composed for Serval Engine and
synthesized by this script (waveforms, envelopes and noise from a fixed
generator): nothing is sampled or downloaded. The output is the same, byte for
byte, on every run and platform.

Usage: make-example-audio.py SET OUT_DIR [--header-dir DIR]

SET jukebox (examples/jukebox), 16000 Hz 8-bit WAVs:
- theme.mod: upbeat, in A minor / C major, 132 BPM (speed 6, 4 rows a beat).
  Drums (synthesized kick, snare and hat on one channel), a driving octave
  bass, a pulse lead and chord arpeggios. An intro, then sections A, A', B,
  B', A, A'; it loops back past the intro after about 51 s.
- calm.mod: slow and mellow, in D major, 90 BPM: a sine bass, a triangle
  melody, a bell arpeggio and a soft pad; about 32 s, then loops.
- coin.wav: two quick rising tones (B5, E6), 0.25 s.
- laser.wav: a fast falling sweep, 2000 Hz to 180 Hz, 0.3 s.
- boom.wav: an explosion: filtered noise that fades out over 0.6 s.
- engine.wav: a low buzzing hum, 80 Hz with a 20 Hz throb, 0.25 s (4,000
  samples, 20 whole cycles) looped whole (smpl loop 0-4000): it plays until
  stopped.

SET rhythm (examples/rhythm, BEAT PAWS): four-channel modules (drums,
bass, lead, pad), speed 6 throughout (4 rows a beat), one tempo each, no
pattern delay (EEx) or pattern loop (E6x), and rhythm_charts.h, the songs'
note charts, made from the same notes as the patterns (written to
--header-dir if given). Every song opens with a bar of woodblock clicks, a
count-in with no chart notes, and ends with a held note fading out.
- rhythm_title.mod: the title loop, cheerful, in C major, 112 BPM: 16 bars
  (34 s), a hook and its answer, looped from the start.
- rhythm_calibrate.mod: a woodblock click every beat at 120 BPM, a fifth
  higher on each bar's first, on two channels (in the middle); 4 bars,
  looped.
- rhythm_calm.mod: "calm", relaxed, in F major, 100 BPM: a laid-back groove
  with a rim click, a round sine bass, a soft flute-like lead with vibrato
  and broken chords on an electric piano; 30 bars (72 s).
- rhythm_stride.mod: "stride", bouncy and funky, in D dorian, 124 BPM: kick
  and claps with an open hat, a syncopated slap bass, a nasal pulse lead
  and chord stabs on the off-beats; 34 bars (66 s).
- rhythm_rush.mod: "rush", driving, in E minor, 148 BPM: a rock beat with
  tom fills, an eighth-note octave bass, a square lead and fast chord
  arpeggios; verse, chorus, a half-time break, verse, two choruses; 50 bars
  (81 s).
The charts (charts() below): per song, HARD and EASY, (tick, lane) pairs,
lanes 0-3 LEFT, RIGHT, B, A. The right hand (B, A) follows the melody's
contour, the left (LEFT, RIGHT) the drums (kick LEFT, snare or clap RIGHT);
on HARD the hands take turns, the melody leading each bar's first half and
the drums its second. EASY keeps one beat of HARD every beat (calm), three
a bar (stride) or two (rush), spread over all four lanes.

SET tests (tests/rom), measurable:
- tone.mod: 4 channels, speed 6, tempo 125 (a row is 6 ticks of 20 ms: 120
  ms). One pattern, of which rows 0-15 play (D00 on row 15), order list of
  length 1, restart position 0. Instrument 1 is a looped square wave (32
  samples a cycle: C-2 plays at 8363 Hz, 261 Hz). Channel 1 plays C-2 on row
  0, held to the end. So the song lasts 16 rows, 1.92 s (about 115 frames at
  59.73 Hz), then ends (played once) or starts again (looped).
- wide.mod: 10 channels ("10CH"), the same instrument and timing: C-2 on
  channel 1 at row 0 and on channel 10 at row 4, D00 on row 15. Maxmod
  playing only 8 module channels stops it when it reaches row 4.
- beep.wav: one-shot, 1600 samples at 16000 Hz (0.1 s): a 1000 Hz square
  wave (8 samples high, 8 low: 100 cycles), at half amplitude (64 and 192).
- hum.wav: looping, 512 samples at 16000 Hz: a 500 Hz square wave (16 high,
  16 low: exactly 16 cycles) at half amplitude, smpl loop 0-512.
- position.mod: for music_position() and music_ticks(). 4 channels, speed 6,
  tempo 125 from row 0 of order 0 (F06, F7D): a tick is 314 mixer samples
  (Maxmod's 39420 / 125, rounded down to even). Instrument 1 is a click: 64
  samples of a full-amplitude square wave (8 high, 8 low), not looped, about
  120 mixer samples long on C-2. Channel 1 clicks (C-2) on every row that
  plays. Order list 0, 1 (patterns 0 and 1), restart position 0. Pattern 0
  plays rows 0-15 (D00 on row 15): 96 ticks. Pattern 1 plays rows 0-15 too,
  with speed 3 from its row 8 (F03), and jumps back to order 0 at its row
  15 (B00): rows 0-7 take 6 ticks, rows 8-15 3, 72 ticks in all. So row r of
  pattern 0 starts at tick 6r, pattern 1's row r at 96 + 6r (r < 8) or 144 +
  3(r - 8), and the loop is 168 ticks (52,752 samples, about 199.8 frames),
  played once or looped alike (B00 never reaches the end of the order list).

MOD notes: ProTracker's octaves 1-3 (C-1 = period 856 ... B-3 = 113); C-2
plays a sample at 8363 Hz, so an instrument of N samples a cycle sounds at
8363 / N Hz on C-2. Melodies below are written at their real pitch (C4 is
middle C) and each instrument converts them with its cycle length.
"""

import argparse
import math
import pathlib
import struct
import sys

WAV_RATE = 16000
C2_RATE = 8363  # what C-2 plays a MOD sample at (finetune 0)
DRUM_RATE = 2 * C2_RATE  # drums are synthesized at C-3's rate and played on C-3

# ProTracker's periods for octaves 1-3, finetune 0.
PERIODS = [
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
]
C2 = 12  # index of C-2 in PERIODS
C3 = 24

NOTE_NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


# --- Synthesis ----------------------------------------------------------------

class Noise:
    """White noise from a fixed linear congruential generator: the same
    sequence everywhere."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return ((self.state >> 16) & 0xFFFF) / 32767.5 - 1.0


def square(phase, duty=0.5):
    return 1.0 if phase % 1.0 < duty else -1.0


def saw(phase):
    return 2.0 * (phase % 1.0) - 1.0


def triangle(phase):
    p = phase % 1.0
    return 4.0 * p - 1.0 if p < 0.5 else 3.0 - 4.0 * p


def sine(phase):
    return math.sin(2.0 * math.pi * phase)


def to_s8(values):
    """Floats in -1..1 as signed 8-bit samples (MOD)."""
    return bytes(max(-127, min(127, round(v * 127))) & 0xFF for v in values)


def to_u8(values):
    """Floats in -1..1 as unsigned 8-bit samples (WAV)."""
    return bytes(max(0, min(255, 128 + round(v * 127))) for v in values)


def cycles(wave, length, envelope):
    """A sample of single cycles of `length` samples: cycle c of wave(phase)
    times envelope(c), for each c the envelope list gives."""
    return [wave(i / length) * level for level in envelope for i in range(length)]


def ramp(start, end, count):
    """count levels from start towards end (end itself not included)."""
    return [start + (end - start) * c / count for c in range(count)]


# --- WAV ----------------------------------------------------------------------

def wav_file(samples, rate=WAV_RATE, loop=None):
    """RIFF WAVE, PCM mono 8-bit unsigned, with an smpl chunk holding one
    forward loop (start, end), end exclusive, if `loop` is given."""
    data = to_u8(samples)
    assert len(data) % 2 == 0, "an even sample count keeps the chunks word-aligned"
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate, 1, 8)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(data)) + data
    if loop:
        start, end = loop
        period_ns = round(1e9 / rate)
        smpl = struct.pack("<9I", 0, 0, period_ns, 60, 0, 0, 0, 1, 0)
        smpl += struct.pack("<6I", 0, 0, start, end, 0, 0)  # id, forward, start, end
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


# --- MOD ----------------------------------------------------------------------

class Instrument:
    """A MOD sample. `cycle` is its waveform's length in samples (0 for drums
    and other unpitched sounds); `loop` the (start, length) in samples it
    loops over, or None."""

    def __init__(self, name, samples, volume=64, cycle=0, loop=None):
        data = to_s8(samples)
        if len(data) % 2:
            data += b"\x00"
        self.name, self.data, self.volume, self.cycle, self.loop = name, data, volume, cycle, loop

    def note(self, midi):
        """The PERIODS index that sounds MIDI note `midi` (C4 = 60)."""
        index = midi - 48 + round(12 * math.log2(self.cycle / 32))
        if not 0 <= index < len(PERIODS):
            raise ValueError(f"{self.name}: MIDI note {midi} is outside ProTracker's octaves")
        return index


def midi(name):
    """'C4', 'F#5', 'Bb3' as a MIDI note number (C4 = 60)."""
    semitone = NOTE_NAMES[name[0]]
    rest = name[1:]
    if rest[0] == "#":
        semitone, rest = semitone + 1, rest[1:]
    elif rest[0] == "b":
        semitone, rest = semitone - 1, rest[1:]
    return 12 * (int(rest) + 1) + semitone


class Pattern:
    """64 rows of `channels` cells, each [period index or None, instrument
    number (0: none), effect, parameter]."""

    def __init__(self, channels):
        self.rows = [[[None, 0, 0, 0] for _ in range(channels)] for _ in range(64)]

    def note(self, row, channel, instrument, index, effect=None):
        cell = self.rows[row][channel]
        cell[0], cell[1] = index, instrument
        if effect:
            cell[2], cell[3] = effect

    def effect(self, row, channel, effect, param):
        cell = self.rows[row][channel]
        cell[2], cell[3] = effect, param

    def melody(self, channel, number, instrument, text, row=0):
        """Places a line of 'NOTE/sixteenths' tokens ('r' rests: volume 0)
        from `row` on; returns the row after it."""
        for token in text.split():
            name, length = token.split("/")
            if name == "r":
                self.effect(row, channel, 0xC, 0)
            else:
                self.note(row, channel, number, instrument.note(midi(name)))
            row += int(length)
        return row

    def encode(self):
        out = bytearray()
        for row in self.rows:
            for index, instrument, effect, param in row:
                period = PERIODS[index] if index is not None else 0
                out += bytes([(instrument & 0xF0) | (period >> 8), period & 0xFF,
                              (instrument & 0x0F) << 4 | effect, param])
        return bytes(out)


def mod_file(title, instruments, patterns, order, restart=0):
    """A ProTracker MOD: 31 sample slots, "M.K." for 4 channels, "NNCH" for
    more."""
    channels = len(patterns[0].rows[0])
    out = bytearray(title.encode("ascii").ljust(20, b"\0"))
    for slot in range(31):
        if slot < len(instruments):
            ins = instruments[slot]
            loop_start, loop_length = (ins.loop if ins.loop else (0, 2))
            out += ins.name.encode("ascii").ljust(22, b"\0")
            out += struct.pack(">HBBHH", len(ins.data) // 2, 0, ins.volume,
                               loop_start // 2, loop_length // 2)
        else:
            out += bytes(22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([len(order), restart]) + bytes(order).ljust(128, b"\0")
    out += b"M.K." if channels == 4 else f"{channels:02d}CH".encode("ascii")
    for pattern in patterns:
        out += pattern.encode()
    for ins in instruments:
        out += ins.data
    return bytes(out)


def tone(name, cycle, wave, envelope, loop_cycles, volume):
    """A pitched instrument: cycles of `wave` shaped by `envelope`, the last
    `loop_cycles` of which loop (whole cycles at one level, so seamlessly)."""
    samples = cycles(wave, cycle, envelope)
    loop = (len(samples) - loop_cycles * cycle, loop_cycles * cycle) if loop_cycles else None
    return Instrument(name, samples, volume, cycle, loop)


# --- theme.mod ------------------------------------------------------------------

def kick():
    out, phase = [], 0.0
    count = round(0.22 * DRUM_RATE)
    for i in range(count):
        t = i / DRUM_RATE
        phase += (45 + 105 * math.exp(-t / 0.035)) / DRUM_RATE
        level = math.exp(-t / 0.09)
        click = 0.5 * math.exp(-t / 0.002)
        out.append(min(1.0, sine(phase) * level + click))
    return out


def snare():
    noise, out, phase = Noise(1), [], 0.0
    for i in range(round(0.18 * DRUM_RATE)):
        t = i / DRUM_RATE
        phase += 190 / DRUM_RATE
        body = sine(phase) * math.exp(-t / 0.03)
        out.append(0.55 * body + 0.6 * noise.next() * math.exp(-t / 0.05))
    return out


def hat():
    noise, out, last = Noise(2), [], 0.0
    for i in range(round(0.05 * DRUM_RATE)):
        value = noise.next()
        out.append((value - last) * 0.5 * math.exp(-i / DRUM_RATE / 0.012))  # brightened
        last = value
    return out


THEME_CHORDS = {  # arpeggio root, arpeggio (0xy), bass root
    "Am": ("A3", 0x37, "A2"), "F": ("F3", 0x47, "F2"), "C": ("C4", 0x47, "C3"),
    "G": ("G3", 0x47, "G2"), "Dm": ("D4", 0x37, "D2"), "E": ("E4", 0x47, "E2"),
}

DRUM_BARS = {
    "intro": "K.H.K.H.K.H.K.H.",
    "groove": "K.H.S.H.K.K.S.H.",
    "fill": "K.H.S.H.K.K.SSSS",
}

THEME_LEAD = {
    "A": ["A4/2 C5/2 E5/4 D5/2 C5/2 D5/2 E5/2",
          "F5/4 E5/2 C5/2 A4/6 r/2",
          "G4/2 C5/2 E5/2 G5/4 E5/2 F5/2 E5/2",
          "D5/6 B4/2 G4/4 r/4"],
    "A2": ["A4/2 C5/2 E5/4 G5/2 E5/2 D5/2 C5/2",
           "F5/2 A5/2 G5/2 F5/2 E5/4 C5/4",
           "E5/2 G5/2 C6/4 B5/2 G5/2 E5/2 G5/2",
           "B5/4 A5/2 G5/2 D5/8"],
    "B": ["F5/3 E5/1 D5/2 A4/2 D5/4 F5/4",
          "G5/3 F5/1 E5/2 D5/2 B4/4 D5/4",
          "E5/3 D5/1 C5/2 G4/2 C5/4 E5/4",
          "A5/6 G5/2 E5/8"],
    "B2": ["A5/3 G5/1 F5/2 C5/2 F5/4 A5/4",
           "B5/3 A5/1 G5/2 D5/2 G5/4 B5/4",
           "C6/4 B5/2 A5/2 E5/4 A5/4",
           "G#5/4 B5/4 E6/6 r/2"],
}


def theme():
    instruments = [
        Instrument("kick", kick(), 54),
        Instrument("snare", snare(), 50),
        Instrument("hat", hat(), 34),
        tone("bass", 64, lambda p: 0.55 * saw(p) + 0.45 * square(p, 0.25),
             ramp(1.0, 0.6, 12) + [0.6] * 4, 4, 44),
        tone("lead", 16, lambda p: square(p, 0.25), ramp(1.0, 0.5, 120) + [0.5] * 4, 4, 36),
        tone("arp", 32, square, ramp(1.0, 0.3, 60) + [0.3] * 2, 2, 30),
    ]
    kick_, snare_, hat_, bass, lead, arp = range(1, 7)
    drum_of = {"K": kick_, "S": snare_, "H": hat_}
    DRUMS, BASS, LEAD, ARP = range(4)

    def section(chords, drums, lead_lines=None, tempo=None):
        p = Pattern(4)
        for bar, chord in enumerate(chords):
            top = bar * 16
            arp_root, arp_param, bass_root = THEME_CHORDS[chord]
            for row, hit in enumerate(DRUM_BARS[drums[bar]]):
                if hit != ".":
                    p.note(top + row, DRUMS, drum_of[hit], C3)
            root = midi(bass_root)
            for eighth, step in enumerate([0, 0, 12, 0, 7, 0, 12, 7]):
                p.note(top + 2 * eighth, BASS, bass, instruments[bass - 1].note(root + step))
            for row in range(16):
                p.effect(top + row, ARP, 0x0, arp_param)
                if row % 4 == 0:
                    p.note(top + row, ARP, arp, instruments[arp - 1].note(midi(arp_root)))
            if lead_lines:
                end = p.melody(LEAD, lead, instruments[lead - 1], lead_lines[bar], top)
                assert end == top + 16, f"theme bar {bar}: not 16 sixteenths"
        if tempo:
            p.effect(0, LEAD, 0xF, tempo)
        return p

    a, b, b2 = ["Am", "F", "C", "G"], ["Dm", "G", "C", "Am"], ["F", "G", "Am", "E"]
    groove = ["groove", "groove", "groove", "fill"]
    patterns = [
        section(a, ["intro", "intro", "groove", "fill"], tempo=132),
        section(a, groove, THEME_LEAD["A"]),
        section(a, groove, THEME_LEAD["A2"]),
        section(b, groove, THEME_LEAD["B"]),
        section(b2, groove, THEME_LEAD["B2"]),
    ]
    return mod_file("serval theme", instruments, patterns, [0, 1, 2, 3, 4, 1, 2], restart=1)


# --- calm.mod -------------------------------------------------------------------

CALM_CHORDS = {  # bell arpeggio tones, pad note, bass (root, fifth below or above)
    "Dmaj7": (["D4", "F#4", "A4", "C#5"], "F#4", ("D3", "A2")),
    "Bm7": (["B3", "D4", "F#4", "A4"], "D4", ("B2", "F#2")),
    "Gmaj7": (["G3", "B3", "D4", "F#4"], "B3", ("G2", "D2")),
    "A": (["A3", "C#4", "E4", "A4"], "C#4", ("A2", "E2")),
    "Em7": (["E4", "G4", "B4", "D5"], "G4", ("E3", "B2")),
    "A7": (["A3", "C#4", "E4", "G4"], "G4", ("A2", "E2")),
    "F#m7": (["F#3", "A3", "C#4", "E4"], "A3", ("F#2", "C#2")),
}

CALM_LEAD = [
    ["F#5/6 E5/2 D5/4 A4/4", "B4/6 C#5/2 D5/4 F#5/4", "E5/8 D5/4 B4/4", "C#5/12 r/4"],
    ["G5/6 F#5/2 E5/4 B4/4", "C#5/6 E5/2 G5/4 E5/4", "F#5/12 A5/4", "B5/6 A5/2 F#5/4 D5/4"],
    ["D5/4 F#5/4 B5/8", "A5/6 F#5/2 E5/4 C#5/4", "B4/6 D5/2 E5/4 G5/4", "E5/4 C#5/4 A4/8"],
]

CALM_PROGRESSION = [
    ["Dmaj7", "Bm7", "Gmaj7", "A"],
    ["Em7", "A7", "Dmaj7", "Bm7"],
    ["Gmaj7", "F#m7", "Em7", "A"],
]


def calm():
    def bell_wave(p):
        return 0.8 * sine(p) + 0.2 * sine(3 * p)

    instruments = [
        tone("bass", 64, lambda p: 0.85 * sine(p) + 0.15 * sine(2 * p),
             ramp(1.0, 0.7, 20) + [0.7] * 2, 2, 50),
        tone("lead", 32, triangle, ramp(0.0, 1.0, 3) + ramp(1.0, 0.75, 30) + [0.75] * 2, 2, 44),
        tone("bell", 32, bell_wave, [math.exp(-c / 30) for c in range(120)], 0, 34),
        tone("pad", 32, lambda p: 0.6 * triangle(p) + 0.4 * sine(p), ramp(0.0, 1.0, 30) + [1.0] * 2,
             2, 24),
    ]
    bass, lead, bell, pad = range(1, 5)
    BASS, LEAD, BELL, PAD = range(4)
    patterns = []
    for number, chords in enumerate(CALM_PROGRESSION):
        p = Pattern(4)
        for bar, chord in enumerate(chords):
            top = bar * 16
            tones, pad_note, (root, fifth) = CALM_CHORDS[chord]
            p.note(top, BASS, bass, instruments[bass - 1].note(midi(root)))
            p.note(top + 8, BASS, bass, instruments[bass - 1].note(midi(fifth)))
            for eighth, which in enumerate([0, 1, 2, 3, 2, 1, 0, 2]):
                p.note(top + 2 * eighth, BELL, bell, instruments[bell - 1].note(midi(tones[which])))
            p.note(top, PAD, pad, instruments[pad - 1].note(midi(pad_note)))
            end = p.melody(LEAD, lead, instruments[lead - 1], CALM_LEAD[number][bar], top)
            assert end == top + 16, f"calm pattern {number} bar {bar}: not 16 sixteenths"
        patterns.append(p)
    patterns[0].effect(0, PAD, 0xF, 90)  # tempo 90 (speed stays 6)
    return mod_file("serval calm", instruments, patterns, [0, 1, 2], restart=0)


# --- Jukebox sound effects ------------------------------------------------------

def coin():
    out, phase = [], 0.0
    first, total = round(0.07 * WAV_RATE), round(0.25 * WAV_RATE)
    for i in range(total):
        freq = 988 if i < first else 1319  # B5, then E6
        phase += freq / WAV_RATE
        level = 0.6 if i < first else 0.6 * math.exp(-(i - first) / WAV_RATE / 0.07)
        out.append(square(phase) * level)
    return out


def laser():
    out, phase = [], 0.0
    total = round(0.3 * WAV_RATE)
    for i in range(total):
        t = i / total
        phase += 2000 * (180 / 2000) ** t / WAV_RATE  # exponential fall, 2000 Hz to 180 Hz
        out.append((0.6 * square(phase, 0.3) + 0.3 * saw(phase)) * (1.0 - 0.8 * t))
    return out


def boom():
    noise, out, low = Noise(3), [], 0.0
    total = round(0.6 * WAV_RATE)
    for i in range(total):
        t = i / WAV_RATE
        cutoff = 0.25 * math.exp(-t / 0.25) + 0.02  # one-pole low-pass, closing
        low += cutoff * (noise.next() - low)
        out.append(max(-1.0, min(1.0, 2.2 * low * math.exp(-t / 0.18))))
    return out


def engine():
    out = []
    period = WAV_RATE // 80  # 200 samples: 80 Hz
    throb = WAV_RATE // 20  # 800 samples: 20 Hz
    for i in range(20 * period):
        p = i % period / period  # phases from the sample index: exactly periodic
        buzz = 0.5 * saw(p) + 0.3 * square(p, 0.3) + 0.2 * sine(2 * p)
        out.append(0.7 * buzz * (0.8 + 0.2 * sine(i % throb / throb)))
    return out


# --- Test set -------------------------------------------------------------------

def test_square():
    return tone("square", 32, square, [1.0] * 8, 8, 64)


def test_module(channels, notes):
    """speed 6, tempo 125, rows 0-15 played (D00 on row 15), C-2 of the
    square at each (row, channel) of `notes`."""
    p = Pattern(channels)
    for row, channel in notes:
        p.note(row, channel, 1, C2)
    p.effect(0, 1, 0xF, 6)
    p.effect(0, 2, 0xF, 125)
    p.effect(15, 0, 0xD, 0)
    return mod_file("serval test", [test_square()], [p], [0])


def position_module():
    """The music_position() test module (the docstring's position.mod)."""
    click = Instrument("click", [square(i / 16) for i in range(64)])
    first, second = Pattern(4), Pattern(4)
    for row in range(16):
        first.note(row, 0, 1, C2)
        second.note(row, 0, 1, C2)
    first.effect(0, 1, 0xF, 6)
    first.effect(0, 2, 0xF, 125)
    first.effect(15, 1, 0xD, 0)
    second.effect(8, 1, 0xF, 3)
    second.effect(15, 1, 0xB, 0)
    return mod_file("serval position", [click], [first, second], [0, 1])


def square_wav(half, count):
    return [0.5 if (i // half) % 2 == 0 else -0.5 for i in range(count)]


# --- Beat Paws (rhythm) ---------------------------------------------------------
#
# A song is written bar by bar into a Song, which keeps every note it places
# with the part it plays (drums, bass, lead, pad). The module's patterns and
# the charts are both made from those notes, so a chart note always lands on a
# note of the music.

DRUMS, BASS, LEAD, PAD = range(4)  # channels

CHORD_INTERVALS = {
    "": (0, 4, 7), "m": (0, 3, 7), "7": (0, 4, 7, 10), "m7": (0, 3, 7, 10),
    "maj7": (0, 4, 7, 11), "sus4": (0, 5, 7), "add9": (0, 4, 7, 14),
}
ARP_PARAMS = {  # 0xy: the two tones above the root an arpeggio adds
    "": 0x47, "m": 0x37, "7": 0x4A, "m7": 0x3A, "maj7": 0x4B, "sus4": 0x57, "add9": 0x47,
}


def chord(name):
    """'F#m7' as (root pitch class 0-11, quality)."""
    root = NOTE_NAMES[name[0]]
    rest = name[1:]
    if rest[:1] == "#":
        root, rest = root + 1, rest[1:]
    elif rest[:1] == "b":
        root, rest = root - 1, rest[1:]
    if rest not in CHORD_INTERVALS:
        raise ValueError(f"chord {name}: unknown quality")
    return root % 12, rest


def near(pitch_class, low):
    """The MIDI note of `pitch_class` at or above `low`, within an octave."""
    return low + (pitch_class - low) % 12


def stick():
    """A woodblock click: the count-in and the calibration beat."""
    out = []
    for i in range(round(0.07 * DRUM_RATE)):
        t = i / DRUM_RATE
        out.append((0.8 * sine(1250 * t) + 0.3 * sine(2610 * t)) * math.exp(-t / 0.014))
    return out


def rim():
    noise, out = Noise(4), []
    for i in range(round(0.07 * DRUM_RATE)):
        t = i / DRUM_RATE
        out.append(0.6 * sine(1700 * t) * math.exp(-t / 0.01)
                   + 0.4 * sine(480 * t) * math.exp(-t / 0.02)
                   + 0.35 * noise.next() * math.exp(-t / 0.005))
    return out


def clap():
    noise, out, last = Noise(5), [], 0.0
    for i in range(round(0.2 * DRUM_RATE)):
        t = i / DRUM_RATE
        bursts = sum(math.exp(-(t - start) / 0.004) for start in (0.0, 0.011, 0.022) if t >= start)
        tail = math.exp(-(t - 0.022) / 0.06) if t >= 0.022 else 0.0
        value = noise.next()
        out.append(max(-1.0, min(1.0, (value - 0.6 * last) * (0.45 * bursts + 0.5 * tail))))
        last = value
    return out


def open_hat():
    noise, out, last = Noise(6), [], 0.0
    for i in range(round(0.2 * DRUM_RATE)):
        value = noise.next()
        out.append((value - last) * 0.45 * math.exp(-i / DRUM_RATE / 0.07))
        last = value
    return out


def tom(freq):
    out, phase = [], 0.0
    for i in range(round(0.25 * DRUM_RATE)):
        t = i / DRUM_RATE
        phase += freq * (1 + 0.6 * math.exp(-t / 0.03)) / DRUM_RATE
        out.append(sine(phase) * math.exp(-t / 0.1))
    return out


# What each drum letter plays (the instrument's name) and at which volume
# (None: the instrument's own). Kick and low tom are the left hand's LEFT,
# snare, clap, rim and high tom its RIGHT; hats and the stick are not charted.
DRUM_LETTERS = {
    "K": ("kick", None), "S": ("snare", None), "s": ("snare", 0x16), "C": ("clap", None),
    "R": ("rim", None), "H": ("hat", None), "h": ("hat", 0x14), "O": ("ohat", None),
    "T": ("tomlo", None), "t": ("tomhi", None), "X": ("stick", None),
}
LEFT_LANES = {"K": 0, "T": 0, "S": 1, "C": 1, "R": 1, "t": 1}
COUNT_IN = "X...X...X...X..."
# The songs' instruments are balanced against each other at full volume, then
# all turned down by this, so that four channels at their loudest don't clip
# the mixer's 8-bit sum.
MIX_GAIN = 0.7


class Song:
    """A module being written. Cells are placed by row from the song's start
    (64 rows a pattern); `notes` keeps every note-on as (row, channel, part,
    pitch), the part being "drums" (pitch: the drum's letter), "bass", "lead"
    or "pad" (pitch: the MIDI note); `hands` the parts each bar's chart takes
    for the left and right hand."""

    def __init__(self, title, tempo, instruments, gain=1.0):
        """`gain` scales every instrument's volume and every volume a cell
        sets, so that the mix stays clear of the mixer's limit."""
        self.title, self.tempo, self.gain = title, tempo, gain
        for ins in instruments:
            ins.volume = round(ins.volume * gain)
        self.instruments = instruments
        self.number = {ins.name: n + 1 for n, ins in enumerate(instruments)}
        self.cells, self.notes, self.hands = {}, [], {}
        self.bars = 0

    def cell(self, row, channel):
        return self.cells.setdefault((row, channel), [None, 0, 0, 0])

    def effect(self, row, channel, effect, param):
        cell = self.cell(row, channel)
        if cell[2] or cell[3]:
            raise ValueError(f"{self.title}: row {row} channel {channel} has an effect already")
        cell[2], cell[3] = effect, param

    def note(self, row, channel, name, pitch, part, volume=None):
        """`pitch`: a MIDI note, or a drum letter (drums play at C-3)."""
        cell = self.cell(row, channel)
        if cell[0] is not None:
            raise ValueError(f"{self.title}: two notes on row {row} channel {channel}")
        ins = self.instruments[self.number[name] - 1]
        cell[0] = C3 if part == "drums" else ins.note(pitch)
        cell[1] = self.number[name]
        if volume is not None:
            self.effect(row, channel, 0xC, round(volume * self.gain))
        self.notes.append((row, channel, part, pitch))

    def rest(self, row, channel):
        self.effect(row, channel, 0xC, 0)

    def melody(self, row, channel, name, part, text, vibrato=0):
        """Places 'NOTE/sixteenths' tokens from `row`: 'r' rests, '-' holds
        the note before. A note of 6 sixteenths or more gets the vibrato 4xy
        `vibrato` from its third row on. Returns the row after the line."""
        for token in text.split():
            pitch, length = token.split("/")
            length = int(length)
            if pitch == "r":
                self.rest(row, channel)
            elif pitch != "-":
                self.note(row, channel, name, midi(pitch), part)
                if vibrato and length >= 6:
                    for r in range(row + 2, row + length):
                        self.effect(r, channel, 0x4, vibrato)
            row += length
        return row

    def drums(self, row, text):
        for i, letter in enumerate(text):
            if letter != ".":
                name, volume = DRUM_LETTERS[letter]
                self.note(row + i, DRUMS, name, letter, "drums", volume)

    def module(self, restart=0):
        """The MOD: the tempo set on row 0, the rows packed into 64-row
        patterns played in order, the last one cut short by D00 when the
        song's rows don't fill it."""
        total = self.bars * 16
        self.effect(0, BASS, 0xF, self.tempo)
        if total % 64:
            free = [ch for ch in range(4) if not any(self.cell(total - 1, ch)[2:])]
            self.effect(total - 1, free[0], 0xD, 0)
        patterns = []
        for start in range(0, total, 64):
            p = Pattern(4)
            for (row, channel), cell in self.cells.items():
                if start <= row < start + 64:
                    p.rows[row - start][channel] = list(cell)
            patterns.append(p)
        if any(row >= total for row, _ in self.cells):
            raise ValueError(f"{self.title}: a cell after the song's end")
        return mod_file(self.title, self.instruments, patterns, list(range(len(patterns))), restart)


# Bass lines: (row, semitones above the chord's root, or None to rest) in a bar.
BASS_LINES = {
    "calm": [(0, 0), (6, 7), (8, 12), (14, 7)],
    "stride": [(0, 0), (3, 0), (6, 12), (8, 0), (10, 10), (11, 12), (14, 7)],
    "stride_walk": [(0, 0), (4, 7), (8, 12), (12, 10)],
    "rush": [(r, 12 if r % 4 else 0) for r in range(0, 16, 2)],
    "rush_half": [(0, 0), (8, 0)],
    "rush_build": [(0, 0), (4, 0), (8, 0), (12, 0)],
    "title": [(0, 0), (2, 12), (4, 0), (6, 12), (8, 7), (10, 12), (12, 0), (14, 10)],
    "root": [(0, 0)],
}

# Pad patterns: "arp" retriggers the chord's root on `rows` with the 0xy
# arpeggio held on every row; "broken" plays chord tones (index into the
# chord's tones, octave above when past the end) on `rows`; "stab" is "arp"
# on off-beats, short.
PAD_STYLES = {
    "calm": ("broken", [(0, 0), (3, 1), (6, 2), (8, 3), (11, 2), (14, 1)]),
    "calm_sparse": ("broken", [(0, 0), (6, 2), (8, 3)]),
    "stride": ("stab", [2, 6, 10, 14]),
    "stride_push": ("stab", [2, 6, 7, 10, 14]),
    "rush": ("arp", [0, 4, 8, 12]),
    "rush_half": ("arp", [0, 8]),
    "title": ("arp", [0, 6, 12]),
    "hold": ("broken", [(0, 0)]),
    "none": ("none", []),
}


def arrange(song, sections, bass_low, pad_low):
    """Writes `sections` into `song`, bar after bar. A section is a dict:
    chords (one a bar), drums (a 16-letter line a bar), bass and pad (styles
    above), lead (a line a bar, or None), hands ((left part, right part) for
    the chart, None for none), vibrato. The bass plays from `bass_low` up, the
    pad's tones from `pad_low` up."""
    lead_sounding = False
    for section in sections:
        lines = section.get("lead")
        for i, name in enumerate(section["chords"]):
            top = song.bars * 16
            root, quality = chord(name)
            intervals = CHORD_INTERVALS[quality]
            song.drums(top, section["drums"][i])
            for row, step in BASS_LINES[section["bass"]]:
                if step is None:
                    song.rest(top + row, BASS)
                else:
                    song.note(top + row, BASS, "bass", near(root, bass_low) + step, "bass")
            kind, pattern = PAD_STYLES[section["pad"]]
            pad_root = near(root, pad_low)
            if kind in ("arp", "stab"):
                for row in range(16):
                    song.effect(top + row, PAD, 0x0, ARP_PARAMS[quality])
                for row in pattern:
                    song.note(top + row, PAD, "pad", pad_root, "pad")
            elif kind == "broken":
                tones = list(intervals) + [12 + s for s in intervals]
                for row, which in pattern:
                    song.note(top + row, PAD, "pad", pad_root + tones[which], "pad")
            if lines:
                end = song.melody(top, LEAD, "lead", "lead", lines[i], section.get("vibrato", 0))
                if end != top + 16:
                    raise ValueError(f"{song.title} bar {song.bars + 1}: not 16 sixteenths")
                lead_sounding = True
            elif lead_sounding:
                song.rest(top, LEAD)
                lead_sounding = False
            song.hands[song.bars] = section.get("hands", ("drums", None)) or (None, None)
            song.bars += 1


def fade_out(song, bar, start=4):
    """A volume slide down (A02) on the bass, lead and pad from row `start` of
    `bar` to the song's end, where no other effect is."""
    for row in range(bar * 16 + start, song.bars * 16):
        for channel in (BASS, LEAD, PAD):
            if not any(song.cell(row, channel)[2:]):
                song.effect(row, channel, 0xA, 0x02)


def charts(song, anchors, easy_slots):
    """HARD and EASY, each a sorted list of (tick, lane), from the song's
    notes and its bars' hands. The left hand (LEFT, RIGHT) plays the drums,
    the right hand (B, A) its bar's melodic part (lead, bass or pad).

    HARD: the right hand's notes follow the melody's contour: up to A, down
    to B, a repeated note on the other button, and two notes a sixteenth
    apart always on both. The left hand's are the kicks and low toms on LEFT,
    the snares, claps, rims and high toms on RIGHT, on eighths. The hands
    take turns: in the first half of a bar the right hand leads and a drum
    hit within a row of one of its notes is left out; in the second half the
    drums lead and the melody's notes near them are left out. On the rows of
    a bar in `anchors` both play. Two notes in one lane are at least 2 rows
    apart.

    EASY: from HARD, at most one note for each slot of `easy_slots` (per
    bar: the rows a slot may take its note from), on beats only. Of the
    notes a slot offers it takes the one in the lane used least so far
    (then the earlier, then a different button from the last), so every
    lane gets its share."""
    right, previous, lane = {}, None, 2
    last_part, last_row = None, -99
    for row, _, part, pitch in sorted(song.notes):
        hand = song.hands.get(row // 16, (None, None))[1]
        if row < 16 or part != hand:
            continue
        if part != last_part:
            previous = None
        if previous is None or pitch == previous:
            lane = 5 - lane
        else:
            lane = 3 if pitch > previous else 2
        if right and right[last_row] == lane and row - last_row < 2:
            lane = 5 - lane
        right[row] = lane
        previous, last_part, last_row = pitch, part, row
    left = {}
    for row, _, part, letter in sorted(song.notes):
        hand = song.hands.get(row // 16, (None, None))[0]
        on_eighth = row % 2 == 0 or row % 16 in anchors
        if row >= 16 and part == hand and letter in LEFT_LANES and on_eighth:
            left[row] = LEFT_LANES[letter]

    def near(hand, row):
        return any(r in hand for r in (row - 1, row, row + 1))

    candidates = []
    for row, lane in right.items():
        if row % 16 in anchors or row % 16 < 8 or not near(left, row):
            candidates.append((row, lane))
    for row, lane in left.items():
        if row % 16 in anchors or row % 16 >= 8 or not near(right, row):
            candidates.append((row, lane))
    hard, last = [], {}
    for row, lane in sorted(candidates):
        if row - last.get(lane, -99) >= 2:
            hard.append((row, lane))
            last[lane] = row
    at = {}
    for row, lane in hard:
        at.setdefault(row, []).append(lane)
    easy, previous, used = [], None, [0, 0, 0, 0]
    for top in range(16, song.bars * 16, 16):
        for rows in easy_slots:
            offered = [(used[lane], i, lane == previous, lane, top + row)
                       for i, row in enumerate(rows) for lane in at.get(top + row, [])]
            if offered:
                _, _, _, lane, row = min(offered)
                easy.append((row, lane))
                used[lane] += 1
                previous = lane
    return [(row * 6, lane) for row, lane in hard], [(row * 6, lane) for row, lane in easy]


def drum_kit(names, volumes):
    synth = {"kick": kick, "snare": snare, "hat": hat, "ohat": open_hat, "clap": clap, "rim": rim,
             "stick": stick, "tomlo": lambda: tom(110), "tomhi": lambda: tom(170)}
    return [Instrument(name, synth[name](), volumes[name]) for name in names]


# --- rhythm_calm.mod: "Sunny Nap", 100 BPM, F major -----------------------------

CALM_A = ["C5/4 A4/2 C5/2 E5/6 F5/2", "E5/4 D5/2 C5/2 A4/8",
          "D5/4 F5/2 A5/2 G5/4 F5/2 D5/2", "E5/6 D5/2 C5/4 r/4"]
CALM_A2 = ["C5/4 A4/2 C5/2 E5/6 G5/2", "A5/4 G5/2 E5/2 C5/4 E5/4",
           "F5/2 E5/2 D5/2 F5/2 A5/4 G5/2 F5/2", "G5/8 r/4 C5/2 D5/2"]
CALM_B = ["F5/6 E5/2 D5/4 A4/4", "Bb4/4 D5/2 F5/2 A5/6 G5/2",
          "F5/4 D5/2 F5/2 Bb5/4 A5/4", "G5/8 E5/4 C5/4",
          "D5/2 E5/2 F5/2 A5/2 C6/6 A5/2", "Bb5/4 A5/2 G5/2 F5/4 D5/4",
          "F5/2 G5/2 A5/2 Bb5/2 A5/4 F5/4", "G5/6 E5/2 C5/4 Bb4/4"]
CALM_A3 = ["A5/4 G5/2 F5/2 E5/4 C5/4", "E5/2 F5/2 G5/2 A5/2 C6/8",
           "Bb5/4 A5/2 G5/2 F5/4 D5/4", "E5/8 D5/4 E5/4"]


def calm_song():
    instruments = drum_kit(["kick", "rim", "hat", "stick", "snare"],
                           {"kick": 46, "rim": 34, "hat": 22, "stick": 40, "snare": 30}) + [
        tone("bass", 64, lambda p: 0.85 * sine(p) + 0.15 * sine(2 * p),
             ramp(1.0, 0.75, 16) + [0.75] * 2, 2, 52),
        tone("lead", 16, lambda p: 0.7 * sine(p) + 0.3 * triangle(p),
             ramp(0.3, 1.0, 6) + ramp(1.0, 0.8, 40) + [0.8] * 4, 4, 42),
        tone("pad", 32, lambda p: 0.75 * sine(p) + 0.2 * sine(2 * p) + 0.05 * sine(3 * p),
             [math.exp(-c / 70) for c in range(300)], 0, 30),
    ]
    song = Song("beat paws calm", 100, instruments, MIX_GAIN)
    groove = "K.h.R.h.K.hKR.h."
    groove2 = "K.h.R.h.K.h.R.hK"
    fill = "K.h.R.h.K.R.sRss"
    a = ["Fmaj7", "Am7", "Bbmaj7", "C"]
    b = ["Dm7", "Gm7", "Bbmaj7", "C", "Dm7", "Gm7", "Bbmaj7", "C7"]
    tune = {"bass": "calm", "pad": "calm", "hands": ("drums", "lead"), "vibrato": 0x42}
    arrange(song, [
        {"chords": ["Fmaj7"], "drums": [COUNT_IN], "bass": "root", "pad": "calm_sparse",
         "hands": None},
        {"chords": a[1:], "drums": [groove, groove2, fill], "bass": "calm", "pad": "calm",
         "hands": ("drums", "bass")},
        {**tune, "chords": a + a, "drums": [groove, groove2] * 4, "lead": CALM_A + CALM_A2},
        {**tune, "chords": b, "drums": [groove, groove2] * 3 + [groove, fill], "lead": CALM_B},
        {**tune, "chords": a + a, "drums": [groove, groove2] * 3 + [groove, fill],
         "lead": CALM_A + CALM_A3},
        {**tune, "chords": ["Fmaj7", "Fmaj7"], "drums": ["K...............", "................"],
         "pad": "hold", "bass": "root", "lead": ["F5/16", "-/8 r/8"]},
    ], bass_low=41, pad_low=53)
    fade_out(song, song.bars - 1)
    return song


# --- rhythm_stride.mod: "Savanna Stride", 124 BPM, D dorian ---------------------

STRIDE_A = ["A4/2 C5/2 D5/3 F5/1 r/2 E5/2 D5/2 C5/2", "B4/2 D5/2 F5/2 G5/2 r/2 F5/2 D5/4",
            "A5/3 G5/1 F5/2 D5/2 r/2 C5/2 D5/4", "B4/4 r/2 G4/2 A4/2 B4/2 D5/4",
            "A4/2 C5/2 D5/3 F5/1 r/2 E5/2 D5/2 C5/2", "B4/2 D5/2 F5/2 A5/2 r/2 G5/2 F5/2 D5/2",
            "F5/2 D5/2 Bb4/2 D5/2 F5/3 G5/1 A5/4", "G5/2 E5/2 C#5/2 E5/2 A5/6 r/2"]
STRIDE_A2 = ["D5/2 F5/2 A5/3 C6/1 r/2 B5/2 A5/2 G5/2", "G5/2 F5/2 D5/2 B4/2 r/2 D5/2 G5/4",
             "F5/1 G5/1 A5/2 C6/2 A5/2 r/2 G5/2 F5/2 D5/2", "E5/4 D5/2 B4/2 G4/2 A4/2 B4/4",
             "A4/2 C5/2 D5/3 F5/1 r/2 E5/2 D5/2 C5/2", "B4/2 D5/2 F5/2 A5/2 r/2 G5/2 F5/2 D5/2",
             "Bb5/3 A5/1 F5/2 D5/2 F5/2 G5/2 A5/4", "E5/2 C#5/2 A4/4 r/4 A4/1 C#5/1 E5/2"]
STRIDE_B = ["D5/3 F5/3 A5/2 r/2 G5/2 F5/2 D5/2", "E5/3 G5/3 C6/2 r/2 B5/2 G5/2 E5/2",
            "C6/3 B5/1 A5/2 E5/2 r/2 G5/2 A5/4", "F5/4 E5/2 D5/2 r/4 A4/4",
            "D5/3 F5/3 A5/2 r/2 C6/2 Bb5/2 A5/2", "G5/3 E5/3 C5/2 r/2 E5/2 G5/2 C6/2",
            "D6/4 C6/2 A5/2 D5/4 E5/4", "C#6/4 A5/2 E5/2 G5/2 E5/2 C#5/2 A4/2"]
STRIDE_END = ["A4/2 C5/2 D5/3 F5/1 r/2 E5/2 D5/2 C5/2", "B4/2 D5/2 F5/2 G5/2 r/2 F5/2 D5/4",
              "F5/2 D5/2 Bb4/2 D5/2 F5/3 G5/1 A5/4", "E5/2 G5/2 A5/2 C#6/2 E6/4 r/4"]


def stride_song():
    instruments = drum_kit(["kick", "clap", "hat", "ohat", "stick"],
                           {"kick": 54, "clap": 52, "hat": 32, "ohat": 28, "stick": 40}) + [
        tone("bass", 64, lambda p: 0.6 * saw(p) - 0.4 * square(p),
             ramp(1.0, 0.6, 24) + [0.6] * 2, 2, 44),
        tone("lead", 16, lambda p: 0.5 * square(p, 0.25) - 0.5 * saw(p),
             ramp(1.0, 0.7, 60) + [0.7] * 4, 4, 34),
        tone("pad", 32, saw, [math.exp(-c / 22) for c in range(100)], 0, 34),
    ]
    song = Song("beat paws stride", 124, instruments, MIX_GAIN)
    groove = "K.h.C.hKh.K.C.hO"
    groove2 = "K.h.C.h.K.hKC.hh"
    fill = "K.h.C.hKC.KCK.CC"
    a = ["Dm7", "G7", "Dm7", "G7", "Dm7", "G7", "Bbmaj7", "A7"]
    b = ["Bbmaj7", "C", "Am7", "Dm7", "Bbmaj7", "C", "Asus4", "A7"]
    grooves = [groove, groove2, groove, groove2, groove, groove2, groove, fill]
    tune = {"bass": "stride", "pad": "stride", "hands": ("drums", "lead")}
    arrange(song, [
        {"chords": ["Dm7"], "drums": [COUNT_IN], "bass": "root", "pad": "none", "hands": None},
        {"chords": ["Dm7", "G7", "Dm7"], "drums": [groove, groove2, fill], "bass": "stride_walk",
         "pad": "stride", "hands": ("drums", "bass")},
        {**tune, "chords": a, "drums": grooves, "lead": STRIDE_A},
        {**tune, "chords": a, "drums": grooves, "lead": STRIDE_A2, "pad": "stride_push"},
        {**tune, "chords": b, "drums": grooves, "lead": STRIDE_B},
        {**tune, "chords": ["Dm7", "G7", "Bbmaj7", "A7"], "drums": [groove, groove2, groove, fill],
         "lead": STRIDE_END},
        {**tune, "chords": ["Dm7", "Dm7"], "drums": ["K.......K.......", "................"],
         "bass": "root", "pad": "hold", "lead": ["D6/16", "-/8 r/8"]},
    ], bass_low=38, pad_low=50)
    fade_out(song, song.bars - 1)
    return song


# --- rhythm_rush.mod: "Night Chase", 148 BPM, E minor --------------------------

RUSH_A = ["E5/2 G5/2 B5/3 A5/3 G5/2 F#5/4", "E5/2 G5/2 C6/4 B5/2 G5/2 E5/4",
          "F#5/2 A5/2 D6/3 C6/1 B5/2 A5/2 F#5/4", "B5/6 A5/2 F#5/4 D5/4",
          "E5/2 G5/2 B5/3 D6/3 C6/2 B5/4", "G5/2 A5/2 B5/2 C6/2 E6/4 D6/2 C6/2",
          "D6/2 C6/2 A5/2 F#5/2 D5/4 F#5/2 A5/2", "B5/4 A5/2 F#5/2 D#5/8"]
RUSH_A2 = RUSH_A[:4] + ["B5/3 D6/3 E6/2 D6/1 B5/1 A5/2 B5/4", "C6/2 B5/2 G5/2 E5/2 G5/2 C6/2 E6/4",
                        "F#6/2 E6/2 D6/2 A5/2 F#5/2 A5/2 D6/4", "D#6/4 B5/2 F#5/2 B5/8"]
RUSH_B = ["G5/4 E6/6 D6/2 C6/2 B5/2", "A5/4 F#5/2 A5/2 D6/6 C6/2",
          "B5/4 D6/2 F#6/2 E6/4 D6/2 B5/2", "E6/8 r/4 B5/1 C6/1 D6/2",
          "E6/4 G6/4 E6/2 D6/2 C6/4", "D6/4 F#6/4 A6/2 G6/2 F#6/4",
          "G6/4 F#6/2 E6/2 D#6/4 B5/4", "F#6/8 D#6/2 B5/2 A5/1 G5/1 F#5/2"]
RUSH_B_END = RUSH_B[:6] + ["G6/4 F#6/2 E6/2 D#6/4 B5/4", "F#6/4 D#6/4 B5/4 F#5/4"]


def rush_song():
    instruments = drum_kit(["kick", "snare", "hat", "ohat", "tomlo", "tomhi", "stick"],
                           {"kick": 56, "snare": 48, "hat": 28, "ohat": 24, "tomlo": 48,
                            "tomhi": 44, "stick": 40}) + [
        tone("bass", 64, saw, ramp(1.0, 0.65, 10) + [0.65] * 2, 2, 44),
        tone("lead", 16, square, ramp(1.0, 0.7, 40) + [0.7] * 4, 4, 30),
        tone("pad", 32, lambda p: square(p, 0.25), [math.exp(-c / 40) for c in range(160)], 0, 24),
    ]
    song = Song("beat paws rush", 148, instruments, MIX_GAIN)
    beat = "K.h.S.h.K.h.S.hK"
    beat2 = "K.h.S.hKK.h.S.hO"
    fill = "K.h.S.h.T.TtT.tS"
    fill2 = "K.h.S.hTt.Tt.SSS"
    half = "K...h...S...h..."
    build = "K...K...K...K..."
    build2 = "K...S...K...SsSs"
    grooves = [beat, beat2, beat, beat2, beat, beat2, beat, fill]
    grooves2 = [beat, beat2, beat, beat2, beat, beat2, beat, fill2]
    verse = ["Em", "C", "D", "Bm", "Em", "C", "D", "B7"]
    chorus = ["C", "D", "Bm", "Em", "C", "D", "B7", "B7"]
    tune = {"bass": "rush", "pad": "rush", "hands": ("drums", "lead")}
    arrange(song, [
        {"chords": ["Em"], "drums": [COUNT_IN], "bass": "root", "pad": "none", "hands": None},
        {"chords": ["Em", "C", "D"], "drums": [build, build, build2], "bass": "rush_build",
         "pad": "rush_half", "hands": ("drums", "bass")},
        {**tune, "chords": verse, "drums": grooves, "lead": RUSH_A},
        {**tune, "chords": chorus, "drums": grooves2, "lead": RUSH_B},
        {"chords": ["Am", "C", "Em", "D"], "drums": [half, half, half, build2], "bass": "rush_half",
         "pad": "rush", "hands": ("drums", "pad")},
        {**tune, "chords": verse, "drums": grooves, "lead": RUSH_A2},
        {**tune, "chords": chorus, "drums": grooves2, "lead": RUSH_B},
        {**tune, "chords": chorus, "drums": grooves, "lead": RUSH_B_END},
        {**tune, "chords": ["Em", "Em"], "drums": ["K...............", "................"],
         "bass": "root", "pad": "hold", "lead": ["E6/16", "-/8 r/8"]},
    ], bass_low=40, pad_low=52)
    fade_out(song, song.bars - 1)
    return song


# --- rhythm_title.mod: the title loop, 112 BPM, C major --------------------------

TITLE_LEAD = ["E5/2 G5/2 C6/3 B5/1 A5/2 G5/2 E5/4", "A5/2 G5/2 E5/2 C5/2 D5/4 E5/4",
              "F5/2 A5/2 C6/2 A5/2 G5/3 F5/1 E5/2 D5/2", "D5/4 G4/2 B4/2 D5/8",
              "E5/2 G5/2 C6/3 B5/1 A5/2 G5/2 E5/4", "A5/2 C6/2 B5/2 A5/2 G5/4 E5/4",
              "F5/2 E5/2 D5/2 F5/2 A5/4 G5/2 F5/2", "G5/6 F5/2 E5/4 D5/4",
              "C5/4 F5/4 A5/6 G5/2", "B4/4 D5/4 G5/6 F5/2",
              "E5/4 G5/4 B5/4 A5/2 G5/2", "A5/8 E5/4 C5/4",
              "F5/2 G5/2 A5/2 C6/2 A5/4 F5/4", "G5/2 A5/2 B5/2 D6/2 B5/4 G5/4",
              "C6/6 B5/2 G5/4 E5/4", "D5/4 F5/4 G5/4 r/4"]


def title_song():
    instruments = drum_kit(["kick", "snare", "hat", "ohat"],
                           {"kick": 50, "snare": 44, "hat": 26, "ohat": 22}) + [
        tone("bass", 64, lambda p: 0.6 * triangle(p) + 0.4 * square(p),
             ramp(1.0, 0.6, 14) + [0.6] * 2, 2, 46),
        tone("lead", 16, lambda p: 0.7 * square(p) + 0.3 * sine(p),
             ramp(1.0, 0.6, 80) + [0.6] * 4, 4, 30),
        tone("pad", 32, triangle, [math.exp(-c / 50) for c in range(200)], 0, 30),
    ]
    song = Song("beat paws title", 112, instruments, MIX_GAIN)
    beat = "K.h.S.hOK.h.S.h."
    beat2 = "K.h.S.h.K.KhS.hO"
    fill = "K.h.S.h.K.h.SsSS"
    a = ["C", "Am", "F", "G", "C", "Am", "F", "G"]
    b = ["F", "G", "Em", "Am", "F", "G", "C", "G7"]
    arrange(song, [
        {"chords": a, "drums": [beat, beat2, beat, beat2, beat, beat2, beat, fill], "bass": "title",
         "pad": "title", "lead": TITLE_LEAD[:8], "vibrato": 0x32},
        {"chords": b, "drums": [beat, beat2, beat, beat2, beat, beat2, beat, fill], "bass": "title",
         "pad": "title", "lead": TITLE_LEAD[8:], "vibrato": 0x32},
    ], bass_low=36, pad_low=52)
    return song


def calibrate_module():
    """A click every beat at 120 BPM, higher on each bar's first: one 64-row
    pattern, looped. The click plays on channels 1 and 2 at once, panned
    left and right, so it sounds in the middle."""
    song = Song("beat paws calibrate", 120, drum_kit(["stick"], {"stick": 64}))
    for row in range(0, 64, 4):
        for channel in (DRUMS, BASS):
            cell = song.cell(row, channel)
            cell[0], cell[1] = (C3 + 7 if row % 16 == 0 else C3), 1
    song.bars = 4
    return song.module()


RHYTHM_SONGS = {  # key: (song, the rows of a bar both hands play, EASY's slots in a bar)
    "calm": (calm_song, {0, 12}, [(0,), (4,), (8,), (12,)]),
    "stride": (stride_song, {0, 12}, [(0,), (4,), (8, 12)]),
    "rush": (rush_song, {0, 12}, [(0, 4), (8, 12)]),
}


def charts_header(songs):
    out = ["// Beat Paws' charts, generated by tools/make-example-audio.py rhythm from the",
           "// same notes as its modules (rhythm_<song>.mod): do not edit.",
           "// A note's tick counts from the module's row 0 (6 ticks a row, 4 rows a",
           "// beat); lanes 0-3 are LEFT, RIGHT, B, A.",
           "", "#ifndef RHYTHM_CHARTS_H", "#define RHYTHM_CHARTS_H", "", '#include "chart.h"']
    for key, (song, hard, easy) in songs.items():
        k = key.upper()
        out += ["", f"#define RHYTHM_{k}_TEMPO {song.tempo}", f"#define RHYTHM_{k}_SPEED 6",
                f"#define RHYTHM_{k}_LENGTH_TICKS {song.bars * 16 * 6}"]
        for name, notes in (("easy", easy), ("hard", hard)):
            out.append(f"#define RHYTHM_{k}_{name.upper()}_COUNT {len(notes)}")
            out.append(f"static const RhythmNote rhythm_{key}_{name}[] = {{")
            for i in range(0, len(notes), 6):
                out.append("    " + " ".join(f"{{{t}, {lane}}}," for t, lane in notes[i:i + 6]))
            out.append("};")
    out += ["", "#endif", ""]
    return "\n".join(out).encode("ascii")


def rhythm_set():
    files = {"rhythm_title.mod": title_song().module(restart=0),
             "rhythm_calibrate.mod": calibrate_module()}
    songs = {}
    for key, (make, anchors, easy_slots) in RHYTHM_SONGS.items():
        song = make()
        hard, easy = charts(song, anchors, easy_slots)
        files[f"rhythm_{key}.mod"] = song.module()
        songs[key] = (song, hard, easy)
    files["rhythm_charts.h"] = charts_header(songs)
    return files


# --- Sets -----------------------------------------------------------------------

def jukebox_set():
    return {
        "theme.mod": theme(),
        "calm.mod": calm(),
        "coin.wav": wav_file(coin()),
        "laser.wav": wav_file(laser()),
        "boom.wav": wav_file(boom()),
        "engine.wav": wav_file(engine(), loop=(0, 20 * (WAV_RATE // 80))),
    }


def tests_set():
    return {
        "tone.mod": test_module(4, [(0, 0)]),
        "wide.mod": test_module(10, [(0, 0), (4, 9)]),
        "beep.wav": wav_file(square_wav(8, 1600)),
        "hum.wav": wav_file(square_wav(16, 512), loop=(0, 512)),
        "position.mod": position_module(),
    }


SETS = {"jukebox": jukebox_set, "rhythm": rhythm_set, "tests": tests_set}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("set", choices=sorted(SETS), help="which files to make")
    parser.add_argument("out_dir", type=pathlib.Path, help="where to write them")
    parser.add_argument("--header-dir", type=pathlib.Path,
                        help="where to write the C headers a set makes (default: OUT_DIR)")
    args = parser.parse_args()
    for name, data in SETS[args.set]().items():
        out_dir = args.header_dir if args.header_dir and name.endswith(".h") else args.out_dir
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / name).write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
