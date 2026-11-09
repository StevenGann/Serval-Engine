#!/usr/bin/env python3
"""Tests for tools/servle_words.py, run by CTest in the host build (servle_words_tool).

The pack format (buckets by the first two letters, 15-bit tails, an answer
flag), the lookup and the answer pick as the script does them, what the list
loader refuses, the sizes of the checked-in lists, and that the generated
block in examples/servle/servle.lua is current (skipped while it is missing).
Run directly: python3 tools/servle_words_test.py
"""

import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
import io

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import servle_words  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORDS = os.path.join(ROOT, "examples", "servle", "words")
SCRIPT = os.path.join(ROOT, "examples", "servle", "servle.lua")

answers = servle_words.load_words(os.path.join(WORDS, "answers.txt"))
guesses = servle_words.load_words(os.path.join(WORDS, "guesses.txt"))
tables = servle_words.pack(guesses, answers)


class ServleWordsTest(unittest.TestCase):
    def test_pack_word(self):
        self.assertEqual(servle_words.pack_word("aaaab"), 1)
        self.assertEqual(servle_words.pack_word("baaaa"), 1 << 20)
        self.assertEqual(servle_words.pack_word("zzzzz"), 25 << 20 | 25 << 15 | 25 << 10 | 25 << 5 | 25)

    def test_tables(self):
        t = servle_words.pack(["abbey", "abide", "babes", "plant"], ["abbey", "plant"])
        self.assertEqual(len(t.starts), 677)
        self.assertEqual(t.starts[0], 0)  # bucket "aa"
        self.assertEqual(t.starts[1], 0)  # bucket "ab" starts at 0
        self.assertEqual(t.starts[2], 2)  # two "ab" words before bucket "ac"
        self.assertEqual(t.starts[676], 4)
        self.assertEqual(t.tails[0], 0x8000 | 1 << 10 | 4 << 5 | 24)  # abbey: b, e, y, an answer
        self.assertEqual(t.tails[1], 8 << 10 | 3 << 5 | 4)  # abide: i, d, e
        self.assertEqual(t.answer_starts[676], 2)

    def test_every_word_found_and_others_not(self):
        for w in guesses:
            self.assertTrue(servle_words.contains(tables, w), w)
        for w in ("qxzjk", "aaaaa", "zzzzz", "snalp"):
            self.assertFalse(servle_words.contains(tables, w), w)

    def test_nth_answer_is_every_answer_once(self):
        got = [servle_words.nth_answer(tables, k) for k in range(1, len(answers) + 1)]
        self.assertEqual(sorted(got), answers)

    def test_named_words(self):
        for w in ("abbey", "babes", "llama", "plant", "crane"):
            self.assertIn(w, guesses)

    def test_rejects(self):
        def load(text):
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "w.txt")
                with open(p, "w") as f:
                    f.write(text)
                return servle_words.load_words(p)

        for bad in ("Plant\n", "plan\n", "plan7\n", "plant\nplant\n", "plant\nabbey\n", "plants\n"):
            with self.assertRaises(ValueError, msg=repr(bad)):
                load(bad)
        self.assertEqual(load("abbey\nplant\n"), ["abbey", "plant"])
        with self.assertRaises(ValueError):
            servle_words.pack(["abbey", "plant"], ["abbey", "crane"])

    def test_render_block(self):
        text = servle_words.render_block(tables)
        self.assertIn("WORD_STARTS", text)
        self.assertIn("local ANSWER_COUNT <const> = %d" % len(answers), text)
        self.assertIn("local GUESS_COUNT <const> = %d" % len(guesses), text)
        self.assertTrue(text.startswith(servle_words.BEGIN))
        self.assertTrue(text.rstrip().endswith(servle_words.END))

    def test_list_sizes(self):
        self.assertTrue(1500 <= len(answers) <= 2500)
        self.assertTrue(5000 <= len(guesses) <= 8000)
        self.assertTrue(set(answers) <= set(guesses))
        self.assertTrue(max(b - a for a, b in zip(tables.starts, tables.starts[1:])) <= 200)

    def test_pack_rewrites_and_checks(self):
        with tempfile.TemporaryDirectory() as d, redirect_stdout(io.StringIO()) as out:
            p = os.path.join(d, "s.lua")
            self.assertEqual(servle_words.main(["pack", "--check", "--script", p]), 1)
            with open(p, "w") as f:
                f.write("print(1)\n")
            self.assertEqual(servle_words.main(["pack", "--check", "--script", p]), 1)
            self.assertEqual(servle_words.main(["pack", "--script", p]), 0)
            self.assertEqual(servle_words.main(["pack", "--check", "--script", p]), 0)
            with open(p) as f:
                first = f.read()
            self.assertTrue(first.startswith("print(1)\n"))
            self.assertEqual(servle_words.main(["pack", "--script", p]), 0)
            with open(p) as f:
                self.assertEqual(f.read(), first)
            with open(p, "w") as f:
                f.write(first.replace("WORD_STARTS", "WORD_STARTX", 1))
            self.assertEqual(servle_words.main(["pack", "--check", "--script", p]), 1)
        self.assertEqual(len(out.getvalue().splitlines()), 3)  # one line per failed check

    @unittest.skipUnless(os.path.exists(SCRIPT), "examples/servle/servle.lua does not exist yet")
    def test_block_is_current(self):
        self.assertEqual(servle_words.main(["pack", "--check"]), 0)


if __name__ == "__main__":
    unittest.main()
