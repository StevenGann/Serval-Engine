#!/usr/bin/env python3
"""Tests for the servle example's game script (examples/servle/servle.lua),
run by CTest in the host build (servle_script).

The script is compiled by tools/svlua.py, assembled by tools/svm.py with the
headers it names, and run on the engine's VM by svlua_runner
(tests/svlua/runner.c, built by the host preset) with scripted buttons, as
tools/svlua_test.py runs compiled programs: the marking rule (repeated letters
included), the dictionary lookup, the controls and their repeat timing, the
round's flow, statistics and their save, and what review asked to be sure of
(a held button carried into a new state, input during a reveal, a blank save,
counters past their width). The runner draws nothing, so these check the
game's state, its sounds and its text, not its pictures.
Run directly: python3 tools/servle_test.py (with the host preset built, or
SERVAL_SVLUA_RUNNER naming the runner; without one the tests that run the
script are skipped).
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import servle_words  # noqa: E402
import svlua  # noqa: E402
from svlua_test import RUNNER, assemble, header_names, needs_runner, run_vm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "examples", "servle", "servle.lua")
WORDS = os.path.join(ROOT, "examples", "servle", "words")
# The headers serval_add_script gives the script: the game's, and the
# engine's it names (buttons, components, saves, colors).
FILES = [os.path.join(ROOT, "examples", "servle", "game.h")] + [
    os.path.join(ROOT, "include", "serval", name)
    for name in ("core.h", "ecs.h", "save.h", "screen.h", "sprites.h")]

ANSWERS = servle_words.load_words(os.path.join(WORDS, "answers.txt"))
GUESSES = set(servle_words.load_words(os.path.join(WORDS, "guesses.txt")))

# The script's own numbers (servle.lua), as the tests compute with them.
STATE_TITLE, STATE_PLAY, STATE_REVEAL, STATE_WON, STATE_LOST = range(5)
REPEAT_DELAY, REPEAT_EVERY = 16, 4
REVEAL_MAX = 120  # frames from the A that enters a guess to input accepted again
BLOB_LIMIT = 26000  # the debug link's hot reload (docs/debug-link.md, "The scratch")

A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L = (1 << n for n in range(10))
IDLE_AFTER_GUESS = 130


def source():
    with open(SCRIPT, encoding="utf-8") as f:
        return f.read()


def names():
    return header_names(files=FILES)


def hold_for(steps):
    """Frames to hold Up or Down for that many letter steps: one acts on the
    first frame, the second REPEAT_DELAY frames later, then every
    REPEAT_EVERY."""
    return 1 if steps == 1 else REPEAT_DELAY + 1 + REPEAT_EVERY * (steps - 2)


def timeline(guesses, start_frame=5):
    """The buttons that start a game at start_frame and type and enter each
    guess, the frames of the A presses that enter them, and the frame the
    last guess's idle frames end."""
    buttons = [(start_frame, START, 1)]
    t = start_frame + 2
    enters = []
    for word in guesses:
        for c in word:
            n = ord(c) - 96  # 1 to 26; Up from empty gives A, Down gives Z
            up, down = n, 27 - n
            button, steps = (UP, up) if up <= down else (DOWN, down)
            length = hold_for(steps)
            buttons.append((t, button, length))
            t += length + 2
            buttons.append((t, A, 1))  # the next empty cell, or the fifth enters the guess
            t += 1 + 2
        enters.append(t - 3)
        t += IDLE_AFTER_GUESS
    return buttons, enters, t


def play(guesses, answer=None, start_frame=5, extra=(), frames=None, probe=""):
    """Runs the game through the guesses (each must be in guesses.txt), with
    the answer preset (packed) when given, extra buttons, and `probe` script
    text appended to servle.lua. The run carries the timeline as .enters and
    .end."""
    for word in guesses:
        assert word in GUESSES, word
    buttons, enters, end = timeline(guesses, start_frame)
    buttons += list(extra)
    assert len(buttons) <= 64, "svlua_runner takes 64 --buttons"
    set_ = {"ANSWER": servle_words.pack_word(answer)} if answer else None
    run = run_vm(source() + probe, frames=frames or end, start=["GAME"], buttons=buttons,
                 set_=set_, files=FILES)
    run.enters, run.end = enters, end
    return run


def unpack(v):
    return "".join(chr(97 + (v >> s & 31)) for s in (20, 15, 10, 5, 0))


class ServleScriptTest(unittest.TestCase):
    def glob(self, run, name):
        return run.globals[name]

    def array(self, run, name):
        return run.arrays[name]

    def sounds(self, run):
        return [c[0] for c in run.calls_of("PSG_PLAY")]

    # --- Marking ------------------------------------------------------------

    @needs_runner
    def test_marks_plain(self):
        run = play(["crane"], answer="plant")
        self.assertEqual(self.array(run, "MARKS")[:5], [1, 1, 3, 3, 1])
        self.assertEqual(self.array(run, "LETTERS")[:5], [3, 18, 1, 14, 5])
        self.assertEqual((self.glob(run, "ROW"), self.glob(run, "STATE")), (1, STATE_PLAY))

    @needs_runner
    def test_marks_repeated_letters(self):
        for answer, guess, marks in (("abbey", "babes", [2, 2, 3, 3, 1]),
                                     ("plant", "llama", [1, 3, 3, 1, 1])):
            with self.subTest(answer=answer, guess=guess):
                run = play([guess], answer=answer)
                self.assertEqual(self.array(run, "MARKS")[:5], marks)

    @needs_runner
    def test_known_keeps_the_best_mark(self):
        run = play(["crane", "llama"], answer="plant")
        known = self.array(run, "KNOWN")
        letter = lambda c: known[ord(c) - 97]  # noqa: E731
        self.assertEqual([letter(c) for c in "alcmz"], [3, 3, 1, 1, 0])

    # --- The dictionary --------------------------------------------------------

    @needs_runner
    def test_refuses_a_word_outside_the_list(self):
        word = "qxzjk"
        self.assertNotIn(word, GUESSES)
        buttons, _, end = timeline([word])
        run = run_vm(source(), frames=end, start=["GAME"], buttons=buttons,
                     set_={"ANSWER": servle_words.pack_word("plant")}, files=FILES)
        self.assertEqual((self.glob(run, "ROW"), self.glob(run, "STATE")), (0, STATE_PLAY))
        self.assertEqual(self.array(run, "MARKS"), [0] * 30)
        self.assertEqual(self.array(run, "LETTERS")[:5], [17, 24, 26, 10, 11])
        self.assertIn("NOT A WORD", [c[-1] for c in run.calls_of("TEXT_PRINT_CENTERED")])
        self.assertIn(names().lookup("SND_BUZZ"), self.sounds(run))

    # --- Controls ------------------------------------------------------------

    @needs_runner
    def test_a_on_empty_cell_and_b(self):
        src = source()
        set_ = {"ANSWER": servle_words.pack_word("plant")}

        def run_with(buttons, frames):
            return run_vm(src, frames=frames, start=["GAME"], buttons=[(5, START, 1)] + buttons,
                          set_=set_, files=FILES)

        # A on the empty first cell: a buzz, the cursor stays.
        run = run_with([(8, A, 1)], 12)
        self.assertEqual(self.glob(run, "COL"), 0)
        self.assertIn(names().lookup("SND_BUZZ"), self.sounds(run))
        # c (3 steps up: 1 + 16 + 4 + 1 frames), A: the cursor goes to col 1.
        typed = [(8, UP, hold_for(3)), (30, A, 1)]
        run = run_with(typed, 33)
        self.assertEqual((self.glob(run, "COL"), self.array(run, "LETTERS")[0]), (1, 3))
        # B on the empty col 1: steps left and clears the c.
        run = run_with(typed + [(35, B, 1)], 38)
        self.assertEqual((self.glob(run, "COL"), self.array(run, "LETTERS")[0]), (0, 0))
        # B again, on the empty first cell: stays.
        run = run_with(typed + [(35, B, 1), (40, B, 1)], 43)
        self.assertEqual(self.glob(run, "COL"), 0)
        # B on a filled cell clears it and stays.
        run = run_with([(8, UP, 1), (10, B, 1)], 13)
        self.assertEqual((self.glob(run, "COL"), self.array(run, "LETTERS")[0]), (0, 0))
        # Left at col 0 stays; Right moves to col 4 and stays there.
        run = run_with([(8, LEFT, 1)], 11)
        self.assertEqual(self.glob(run, "COL"), 0)
        run = run_with([(8 + 2 * k, RIGHT, 1) for k in range(6)], 22)
        self.assertEqual(self.glob(run, "COL"), 4)

    @needs_runner
    def test_spin_wraps_and_repeats(self):
        src = source()

        def letter(buttons, frames):
            run = run_vm(src, frames=frames, start=["GAME"], buttons=[(5, START, 1)] + buttons,
                         set_={"ANSWER": servle_words.pack_word("plant")}, files=FILES)
            return self.array(run, "LETTERS")[0]

        self.assertEqual(letter([(8, DOWN, 1)], 10), 26)  # empty, Down: Z
        self.assertEqual(letter([(8, DOWN, 1), (11, UP, 1)], 13), 1)  # Z, Up: A
        self.assertEqual(letter([(8, UP, 17 + 4 * 3)], 8 + 17 + 4 * 3 + 2), 5)  # five steps: E
        self.assertEqual(letter([(8, UP, 17 + 4 * 3 - 1)], 8 + 17 + 4 * 3 + 2), 4)  # a frame less

    # --- The round ------------------------------------------------------------

    @needs_runner
    def test_win(self):
        probe = """
Probe = object {}
probe_saved = array(4)
probe_ok = false
probe_size = 0
function Probe:room_start()
  repeat wait(1) until state == STATE_WON
  wait(1)
  probe_ok = save_read(0, probe_saved, 1) == SAVE_OK
  probe_size = save_slot_size(0)
end
"""
        buttons, _, end = timeline(["plant"])
        run = run_vm(source() + probe, frames=end, start=["GAME", "PROBE"], buttons=buttons,
                     set_={"ANSWER": servle_words.pack_word("plant")}, files=FILES)
        self.assertEqual(self.glob(run, "STATE"), STATE_WON)
        self.assertEqual(self.array(run, "STATS"), [1, 1, 1, 1])
        self.assertEqual(self.glob(run, "PROBE_OK"), 1)  # slot 0, version 1
        self.assertEqual(self.array(run, "PROBE_SAVED"), [1, 1, 1, 1])
        self.assertEqual(self.glob(run, "PROBE_SIZE"), 16)
        self.assertIn(names().lookup("SND_WIN"), self.sounds(run))

    @needs_runner
    def test_loss_after_six(self):
        run = play(["crane"] * 6, answer="plant")
        self.assertEqual(self.glob(run, "STATE"), STATE_LOST)
        self.assertEqual(self.array(run, "STATS"), [1, 0, 0, 0])
        self.assertEqual(self.glob(run, "ROW"), 5)
        self.assertIn(names().lookup("SND_LOSE"), self.sounds(run))

    @needs_runner
    def test_next_round_clears_and_picks(self):
        buttons, _, end = timeline(["plant"])
        again = end + 100
        run = run_vm(source(), frames=again + 3, start=["GAME"], buttons=buttons + [(again, A, 1)],
                     set_={"ANSWER": servle_words.pack_word("plant")}, files=FILES)
        self.assertEqual((self.glob(run, "STATE"), self.glob(run, "ROW"), self.glob(run, "COL")),
                         (STATE_PLAY, 0, 0))
        for name in ("LETTERS", "MARKS", "KNOWN"):
            self.assertEqual(set(self.array(run, name)), {0}, name)
        self.assertNotEqual(self.glob(run, "ANSWER"), 0)
        self.assertEqual(self.array(run, "STATS"), [1, 1, 1, 1])

    @needs_runner
    def test_picked_answer_is_an_answer(self):
        src = source()
        picked = []
        for frame in (5, 6, 7, 30, 200):
            run = run_vm(src, frames=frame + 3, start=["GAME"], buttons=[(frame, START, 1)],
                         files=FILES)
            self.assertEqual(self.glob(run, "STATE"), STATE_PLAY)
            word = unpack(self.glob(run, "ANSWER"))
            self.assertIn(word, ANSWERS)
            picked.append(word)
        self.assertGreaterEqual(len(set(picked)), 2, picked)

    @needs_runner
    def test_nth_answer_matches_the_tool(self):
        """The script's pick finds what servle_words.nth_answer does: the
        first and last answers, and the one with the longest scan (the last
        answer of the bucket with the most words before it), each in one frame
        without running out of operations."""
        tables = servle_words._tables()
        worst, worst_k = 0, 1
        for b in range(26 * 26):
            k = tables.answer_starts[b]
            for i in range(tables.starts[b], tables.starts[b + 1]):
                if tables.tails[i] & servle_words.ANSWER_FLAG:
                    k += 1
                    if i - tables.starts[b] >= worst:
                        worst, worst_k = i - tables.starts[b], k
        ks = [1, len(ANSWERS), worst_k, len(ANSWERS) // 2]
        probe = "\nProbe = object {}\nprobe_words = array(%d)\nfunction Probe:room_start()\n" % len(ks)
        for n, k in enumerate(ks, 1):
            probe += "  probe_words[%d] = nth_answer(%d)\n  wait(1)\n" % (n, k)
        probe += "end\n"
        run = run_vm(source() + probe, frames=len(ks) + 1, start=["PROBE"], files=FILES)
        self.assertEqual([unpack(v) for v in self.array(run, "PROBE_WORDS")],
                         [servle_words.nth_answer(tables, k) for k in ks])

    # --- Review focus -----------------------------------------------------------

    @needs_runner
    def test_held_button_does_not_carry_over(self):
        # A (and Up) held from the title's START frame: nothing happens.
        run = run_vm(source(), frames=70, start=["GAME"],
                     buttons=[(5, START, 1), (5, A, 60), (5, UP, 30)],
                     set_={"ANSWER": servle_words.pack_word("plant")}, files=FILES)
        self.assertEqual(self.glob(run, "STATE"), STATE_PLAY)
        self.assertEqual(self.glob(run, "COL"), 0)
        self.assertEqual(self.array(run, "LETTERS")[0], 0)
        self.assertNotIn(names().lookup("SND_BUZZ"), self.sounds(run))
        # Up held from inside a reveal until 40 frames after it can have
        # ended: the new row's first cell stays empty.
        enter = timeline(["crane"])[1][0]
        run = play(["crane"], answer="plant", extra=[(enter + 10, UP, REVEAL_MAX - 10 + 40)],
                   frames=enter + REVEAL_MAX + 50)
        self.assertEqual((self.glob(run, "ROW"), self.glob(run, "STATE")), (1, STATE_PLAY))
        self.assertEqual(self.array(run, "LETTERS")[5], 0)

    @needs_runner
    def test_input_ignored_during_reveal(self):
        enter = timeline(["plant"])[1][0]
        run = play(["plant"], answer="plant", extra=[(enter + 10, A, 1), (enter + 60, A, 1)])
        self.assertEqual(self.glob(run, "STATE"), STATE_WON)
        self.assertEqual(self.array(run, "STATS")[0], 1)

    @needs_runner
    def test_blank_save_starts_at_zero(self):
        run = run_vm(source(), frames=10, start=["GAME"], files=FILES)  # warns: run_vm fails
        self.assertEqual(self.glob(run, "STATE"), STATE_TITLE)
        self.assertEqual(self.array(run, "STATS"), [0, 0, 0, 0])

    @needs_runner
    def test_counter_display_clamps(self):
        probe = """
Probe = object {}
function Probe:room_start()
  print_stat(0, 2, 1000)
  print_stat(23, 5, 7)
end
"""
        run = run_vm(source() + probe, start=["PROBE"], files=FILES)
        self.assertEqual(run.calls_of("TEXT_PRINT_NUMBER"), [(0, 2, 999, 3), (23, 5, 7, 3)])

    @needs_runner
    def test_reset_on_the_title(self):
        # L and R held, SELECT pressed on the title: the statistics reset.
        run = run_vm(source(), frames=20, start=["GAME"],
                     buttons=[(5, L | R, 10), (8, SELECT, 1)], files=FILES)
        self.assertEqual(self.glob(run, "STATE"), STATE_TITLE)
        self.assertIn("STATS RESET", [c[-1] for c in run.calls_of("TEXT_PRINT_CENTERED")])
        self.assertIn(names().lookup("SND_RESET"), self.sounds(run))
        # SELECT without both shoulders: nothing.
        run = run_vm(source(), frames=20, start=["GAME"],
                     buttons=[(5, L, 10), (8, SELECT, 1)], files=FILES)
        self.assertNotIn(names().lookup("SND_RESET"), self.sounds(run))

    # --- Size and cleanliness ---------------------------------------------------

    def test_blob_fits_the_debug_link(self):
        compiled = svlua.compile_program(source(), "servle.lua")
        blob = assemble(compiled.listing, names()).blob
        self.assertLessEqual(len(blob), BLOB_LIMIT)

    @needs_runner
    def test_no_warnings_in_a_full_round(self):
        guesses = ["crane", "llama", "babes", "abbey", "crane", "llama"]
        buttons, _, end = timeline(guesses)
        again = end + 100
        # run_vm fails on any engine warning (the operation budget included).
        run = run_vm(source(), frames=again + 5, start=["GAME"], buttons=buttons + [(again, A, 1)],
                     set_={"ANSWER": servle_words.pack_word("plant")}, files=FILES)
        self.assertEqual(self.glob(run, "STATE"), STATE_PLAY)
        self.assertEqual(self.array(run, "STATS"), [1, 0, 0, 0])


if __name__ == "__main__":
    if not RUNNER:
        print("servle_test: no svlua_runner; the tests that run the script are skipped")
    unittest.main()
