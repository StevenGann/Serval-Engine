#!/usr/bin/env python3
"""Make the tracker modules and WAV samples of the engine's examples and tests.

Every note, tune and sound here is original, composed for Serval Engine and
synthesized by this script (waveforms, envelopes and noise from a fixed
generator): nothing is sampled or downloaded. The output is the same, byte for
byte, on every run and platform.

Usage: make-example-audio.py SET OUT_DIR

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


def square_wav(half, count):
    return [0.5 if (i // half) % 2 == 0 else -0.5 for i in range(count)]


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
    }


SETS = {"jukebox": jukebox_set, "tests": tests_set}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("set", choices=sorted(SETS), help="which files to make")
    parser.add_argument("out_dir", type=pathlib.Path, help="where to write them")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name, data in SETS[args.set]().items():
        (args.out_dir / name).write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
