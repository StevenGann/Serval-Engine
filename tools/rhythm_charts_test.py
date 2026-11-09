#!/usr/bin/env python3
"""Tests for the rhythm example's songs and charts, run by CTest in the host
build (rhythm_charts_tool).

tools/make-example-audio.py's rhythm set writes Beat Paws' modules and its
note charts, rhythm_charts.h, from the same note lists. This runs the set into
a temporary directory, reads the modules back with a MOD reader of its own
(following the order list as a player does) and checks the charts against
them: every note on a note of its part in the module (LEFT and RIGHT on the
drums', B and A on the other channels'), the lengths, the tempos, the
count-in, what makes a chart playable (sorted, lanes 0-3, notes in one lane
far enough apart, at most two notes at once and those in different hands,
EASY's notes among HARD's, every lane and both hands used, how many notes a
second), and the title loop and the calibration clicks.
Run directly: python3 tools/rhythm_charts_test.py
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "tools", "make-example-audio.py")

SONGS = {"calm": 100, "stride": 124, "rush": 148}  # the song keys and their tempo, about
DIFFICULTIES = ("easy", "hard")
SPEED = 6
ROWS_A_BAR = 16  # speed 6: 4 rows a beat, 4 beats a bar
MIN_GAP_ROWS = {"easy": 4, "hard": 2}  # two notes in one lane at least this far apart
# Notes a second, at most, counted over any 2 seconds, and the most notes in
# any 1 second.
MAX_RATE_2S = {"easy": 2.1, "hard": 6.0}
MAX_NOTES_1S = {"easy": 3, "hard": 7}
MIN_LANE_SHARE = 0.08  # of a chart's notes, in each lane
HAND_SHARE = (0.35, 0.65)  # of a HARD chart's notes, in each hand
DRUMS = 0  # the drum channel: the left hand's lanes (0, 1) come from it, B and A from the others
# The drums each of LEFT and RIGHT plays, by the module's sample names.
LEFT_DRUMS = {0: {"kick", "tomlo"}, 1: {"snare", "clap", "rim", "tomhi"}}
MODULES = ("title", "calibrate", "calm", "stride", "rush")


def row_seconds(tempo):
    """A row lasts SPEED ticks of 2.5 / tempo seconds."""
    return SPEED * 2.5 / tempo


class Module:
    """A 4-channel ProTracker MOD, read and played through its order list:
    `rows` is the list of rows played, each a list of four cells
    (sample, period, effect, param); `tempo` the tempo it sets."""

    def __init__(self, data, name):
        self.name = name
        if data[1080:1084] != b"M.K.":
            raise AssertionError(f"{name}: not a 4-channel MOD ({data[1080:1084]!r})")
        self.names = [data[20 + 30 * i:42 + 30 * i].rstrip(b"\0").decode("ascii")
                      for i in range(31)]
        self.length, self.restart = data[950], data[951]
        self.order = list(data[952:952 + self.length])
        count = max(data[952:952 + 128]) + 1
        self.patterns = []
        for p in range(count):
            base = 1084 + p * 1024
            pattern = []
            for row in range(64):
                cells = []
                for ch in range(4):
                    b = data[base + row * 16 + ch * 4:base + row * 16 + ch * 4 + 4]
                    cells.append(((b[0] & 0xF0) | (b[2] >> 4), (b[0] & 0x0F) << 8 | b[1],
                                  b[2] & 0x0F, b[3]))
                pattern.append(cells)
            self.patterns.append(pattern)
        self.play()

    def play(self):
        """Follows the order list once, from order 0 to its end, as a player
        playing it once does: Dxx breaks to the next order, Bxx jumps
        (forward only: a jump back would loop forever), Fxx sets the speed
        (which must stay 6) or the tempo."""
        self.rows, self.tempos = [], []
        order, row = 0, 0
        while order < self.length:
            cells = self.patterns[self.order[order]][row]
            self.rows.append(cells)
            next_order, next_row = order, row + 1
            for _, _, effect, param in cells:
                if effect == 0xF:
                    if param < 0x20:
                        if param != SPEED:
                            raise AssertionError(f"{self.name}: speed {param}")
                    else:
                        self.tempos.append((len(self.rows) - 1, param))
                elif effect == 0xD:
                    next_order, next_row = order + 1, (param >> 4) * 10 + (param & 0xF)
                elif effect == 0xB:
                    if param <= order:
                        raise AssertionError(f"{self.name}: Bxx jumps back to order {param}")
                    next_order, next_row = param, 0
            if next_row >= 64:
                next_order, next_row = order + 1, 0
            order, row = next_order, next_row
        if not self.tempos or self.tempos[0][0] != 0 or len({t for _, t in self.tempos}) != 1:
            raise AssertionError(f"{self.name}: one tempo, set on row 0: {self.tempos}")
        self.tempo = self.tempos[0][1]

    def note_rows(self, channels=range(4), samples=None):
        """The played rows (0 = the first) on which one of `channels` starts a
        note (of one of the named `samples`, if given)."""
        def starts(cell):
            sample, period = cell[0], cell[1]
            return period and (samples is None or self.names[sample - 1] in samples)
        return {i for i, cells in enumerate(self.rows) if any(starts(cells[c]) for c in channels)}

    def all_cells(self):
        for pattern in self.patterns:
            for cells in pattern:
                yield from cells


def read_charts(text):
    """The generated header's defines and note arrays."""
    defines = {m[1]: int(m[2]) for m in re.finditer(r"^#define (RHYTHM_\w+) (\d+)$", text, re.M)}
    arrays = {}
    for m in re.finditer(r"static const RhythmNote (rhythm_\w+)\[\] = \{(.*?)\};", text, re.S):
        arrays[m[1]] = [(int(a), int(b)) for a, b in re.findall(r"\{(\d+), (\d+)\}", m[2])]
    return defines, arrays


def make_set(out_dir):
    subprocess.run([sys.executable, SCRIPT, "rhythm", out_dir], check=True)
    files = {}
    for name in sorted(os.listdir(out_dir)):
        with open(os.path.join(out_dir, name), "rb") as f:
            files[name] = f.read()
    return files


class RhythmChartsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as d:
            cls.files = make_set(d)
        cls.modules = {k: Module(cls.files[f"rhythm_{k}.mod"], k) for k in MODULES}
        cls.header = cls.files["rhythm_charts.h"].decode("ascii")
        cls.defines, cls.arrays = read_charts(cls.header)

    def chart(self, song, difficulty):
        notes = self.arrays[f"rhythm_{song}_{difficulty}"]
        count = self.defines[f"RHYTHM_{song.upper()}_{difficulty.upper()}_COUNT"]
        self.assertEqual(len(notes), count)
        self.assertGreater(len(notes), 0)
        return notes

    def test_files(self):
        self.assertEqual(sorted(self.files),
                         sorted([f"rhythm_{k}.mod" for k in MODULES] + ["rhythm_charts.h"]))
        self.assertIn('#include "chart.h"', self.header)

    def test_same_every_run(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(make_set(d), self.files)

    def test_no_pattern_delay_or_loop(self):
        for name, module in self.modules.items():
            for _, _, effect, param in module.all_cells():
                self.assertFalse(effect == 0xE and param >> 4 in (0x6, 0xE),
                                 f"{name}: E{param:02X}")

    def test_song_tempo_speed_length(self):
        for song, tempo in SONGS.items():
            module = self.modules[song]
            k = song.upper()
            self.assertLessEqual(abs(module.tempo - tempo), 5, song)
            self.assertEqual(self.defines[f"RHYTHM_{k}_TEMPO"], module.tempo, song)
            self.assertEqual(self.defines[f"RHYTHM_{k}_SPEED"], SPEED, song)
            self.assertEqual(self.defines[f"RHYTHM_{k}_LENGTH_TICKS"], len(module.rows) * SPEED,
                             song)
            seconds = len(module.rows) * row_seconds(module.tempo)
            self.assertTrue(60 <= seconds <= 90, f"{song}: {seconds:.1f} s")

    def test_notes_on_their_parts_notes(self):
        # LEFT on a kick or low tom, RIGHT on a snare, clap, rim or high tom,
        # B and A on a note of the bass, the lead or the pad.
        for song in SONGS:
            module = self.modules[song]
            rows = {lane: module.note_rows([DRUMS], drums) for lane, drums in LEFT_DRUMS.items()}
            rows[2] = rows[3] = module.note_rows([c for c in range(4) if c != DRUMS])
            for difficulty in DIFFICULTIES:
                for tick, lane in self.chart(song, difficulty):
                    self.assertEqual(tick % SPEED, 0, (song, difficulty, tick))
                    self.assertIn(tick // SPEED, rows[lane], (song, difficulty, tick, lane))

    def test_lanes_and_hands_shared(self):
        for song in SONGS:
            for difficulty in DIFFICULTIES:
                notes = self.chart(song, difficulty)
                where = (song, difficulty)
                for lane in range(4):
                    share = sum(1 for _, n in notes if n == lane) / len(notes)
                    self.assertGreaterEqual(share, MIN_LANE_SHARE, (where, lane))
                if difficulty == "hard":
                    left = sum(1 for _, n in notes if n < 2) / len(notes)
                    self.assertTrue(HAND_SHARE[0] <= left <= HAND_SHARE[1], (where, left))

    def test_playable(self):
        for song in SONGS:
            for difficulty in DIFFICULTIES:
                notes = self.chart(song, difficulty)
                where = (song, difficulty)
                self.assertEqual(notes, sorted(notes), where)
                self.assertEqual(len(set(notes)), len(notes), where)
                self.assertGreaterEqual(notes[0][0], ROWS_A_BAR * SPEED, where)  # the count-in bar
                self.assertLess(notes[-1][0], self.defines[f"RHYTHM_{song.upper()}_LENGTH_TICKS"])
                last = {}
                at = {}
                for tick, lane in notes:
                    self.assertIn(lane, range(4), where)
                    if lane in last:
                        self.assertGreaterEqual(tick - last[lane], MIN_GAP_ROWS[difficulty] * SPEED,
                                                (where, tick, lane))
                    last[lane] = tick
                    at.setdefault(tick, []).append(lane)
                for tick, lanes in at.items():
                    # At most two at once, one for each hand (LEFT/RIGHT, B/A).
                    self.assertLessEqual(len(lanes), 2, (where, tick))
                    self.assertEqual(len({lane // 2 for lane in lanes}), len(lanes), (where, tick))

    def test_easy_within_hard(self):
        for song in SONGS:
            hard = set(self.chart(song, "hard"))
            for note in self.chart(song, "easy"):
                self.assertIn(note, hard, song)
            self.assertLess(len(self.chart(song, "easy")), len(hard), song)

    def test_notes_a_second(self):
        def most(ticks, seconds, tempo):
            window = seconds / row_seconds(tempo) * SPEED  # in ticks
            best, j = 0, 0
            for i, tick in enumerate(ticks):
                while ticks[j] <= tick - window:
                    j += 1
                best = max(best, i - j + 1)
            return best

        for song in SONGS:
            tempo = self.modules[song].tempo
            for difficulty in DIFFICULTIES:
                ticks = [tick for tick, _ in self.chart(song, difficulty)]
                where = (song, difficulty)
                self.assertLessEqual(most(ticks, 2, tempo) / 2, MAX_RATE_2S[difficulty], where)
                self.assertLessEqual(most(ticks, 1, tempo), MAX_NOTES_1S[difficulty], where)

    def test_title_loops(self):
        module = self.modules["title"]
        self.assertEqual(module.restart, 0)
        self.assertEqual(len(module.rows) % ROWS_A_BAR, 0)
        self.assertTrue(12 <= len(module.rows) // ROWS_A_BAR <= 20, len(module.rows))

    def test_calibration_clicks_every_beat(self):
        module = self.modules["calibrate"]
        self.assertEqual(module.tempo, 120)
        self.assertEqual(module.note_rows(), set(range(0, len(module.rows), 4)))
        self.assertEqual(len(module.rows) % 4, 0)


if __name__ == "__main__":
    unittest.main()
