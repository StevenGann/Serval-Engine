#!/usr/bin/env python3
"""Word lists for the servle example: validate, pack, derive.

examples/servle/words/answers.txt and guesses.txt are the source of truth: one
lower-case five-letter word per line, sorted, unique, every answer also a
guess. `pack` turns them into the Lua tables that examples/servle/servle.lua
ends with, between two marker lines:

    -- BEGIN GENERATED WORDS (tools/servle_words.py pack): do not edit
    -- END GENERATED WORDS

(`pack` replaces the block, or appends it after a blank line when the script
has none; `pack --check` exits 1 with a one-line message if it is stale or
missing). The tables, as the script reads them (letters a to z are 0 to 25):

    WORD_STARTS    677 numbers: guesses in the buckets before bucket
                   b = first * 26 + second; the last is the guess count
    WORD_TAILS     one per guess, sorted by bucket then tail:
                   third << 10 | fourth << 5 | fifth, bit 15 set for an answer
    ANSWER_STARTS  677 numbers: answers in the buckets before bucket b
    ANSWER_COUNT, GUESS_COUNT

`derive SCOWL_FINAL_DIR OUT_DIR` writes candidate lists from the `final/`
directory of SCOWL 2020.12.07
(https://sourceforge.net/projects/wordlist/files/SCOWL/2020.12.07/scowl-2020.12.07.tar.gz,
http://wordlist.aspell.net; its license is in third_party/licenses/scowl.txt):
lower-case five-letter a-z words of english-words.N and american-words.N
(guesses also british-words.N), answers from sizes up to 35 less a word ending
in "s" whose first four letters are a word in sizes up to 60, guesses from
sizes up to 70. The checked-in lists are those candidates curated by hand
afterwards (obscure, archaic, abbreviated, proper-noun-like, crude and
offensive entries removed), so they are not reproducible from SCOWL alone.
"""

import argparse
import os
import re
import sys
from collections import namedtuple

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORDS = os.path.join(ROOT, "examples", "servle", "words")
SCRIPT = os.path.join(ROOT, "examples", "servle", "servle.lua")

BEGIN = "-- BEGIN GENERATED WORDS (tools/servle_words.py pack): do not edit"
END = "-- END GENERATED WORDS"
BUCKETS = 26 * 26
ANSWER_FLAG = 0x8000

Tables = namedtuple("Tables", "starts tails answer_starts")
WORD_RE = re.compile(r"[a-z]{5}\Z")


def load_words(path):
    """Read a list; ValueError naming the file and line for a bad word."""
    words = []
    with open(path, encoding="ascii") as f:
        for n, line in enumerate(f, 1):
            w = line.rstrip("\n")
            where = "%s:%d" % (path, n)
            if not WORD_RE.match(w):
                raise ValueError("%s: %r is not five letters a to z" % (where, w))
            if words and w == words[-1]:
                raise ValueError("%s: %r is a duplicate" % (where, w))
            if words and w < words[-1]:
                raise ValueError("%s: %r is out of order (after %r)" % (where, w, words[-1]))
            words.append(w)
    return words


def pack_word(word):
    v = 0
    for c in word:
        v = v << 5 | (ord(c) - 97)
    return v


def _bucket(word):
    return (ord(word[0]) - 97) * 26 + (ord(word[1]) - 97)


def pack(guesses, answers):
    gset = set(guesses)
    for a in answers:
        if a not in gset:
            raise ValueError("answer %r is not a guess" % a)
    aset = set(answers)
    starts = [0] * (BUCKETS + 1)
    astarts = [0] * (BUCKETS + 1)
    tails = []
    for w in sorted(guesses):
        b = _bucket(w)
        starts[b + 1] += 1
        astarts[b + 1] += w in aset
        tails.append((ANSWER_FLAG if w in aset else 0) | pack_word(w) & 0x7FFF)
    for b in range(BUCKETS):
        starts[b + 1] += starts[b]
        astarts[b + 1] += astarts[b]
    return Tables(starts, tails, astarts)


def contains(t, word):
    b = _bucket(word)
    want = pack_word(word) & 0x7FFF
    lo, hi = t.starts[b], t.starts[b + 1] - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        v = t.tails[mid] & 0x7FFF
        if v == want:
            return True
        if v < want:
            lo = mid + 1
        else:
            hi = mid - 1
    return False


def nth_answer(t, k):
    """The k-th answer (1-based) in list order."""
    # The bucket is the last one whose answers before it number fewer than k.
    lo, hi = 0, BUCKETS - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if t.answer_starts[mid] < k:
            lo = mid
        else:
            hi = mid - 1
    b = lo
    seen = t.answer_starts[b]
    for i in range(t.starts[b], t.starts[b + 1]):
        if t.tails[i] & ANSWER_FLAG:
            seen += 1
            if seen == k:
                tail = t.tails[i] & 0x7FFF
                letters = (b // 26, b % 26, tail >> 10, tail >> 5 & 31, tail & 31)
                return "".join(chr(97 + c) for c in letters)
    raise ValueError("no answer %d" % k)


def _numbers(name, values):
    lines = ["local %s = {" % name]
    for i in range(0, len(values), 10):
        lines.append("  " + ", ".join(str(v) for v in values[i:i + 10]) + ",")
    lines.append("}")
    return lines


def render_block(t):
    out = [BEGIN]
    out += _numbers("WORD_STARTS", t.starts)
    out += _numbers("WORD_TAILS", t.tails)
    out += _numbers("ANSWER_STARTS", t.answer_starts)
    out.append("local ANSWER_COUNT <const> = %d" % t.answer_starts[-1])
    out.append("local GUESS_COUNT <const> = %d" % t.starts[-1])
    out.append(END)
    return "\n".join(out) + "\n"


def _tables():
    return pack(load_words(os.path.join(WORDS, "guesses.txt")), load_words(os.path.join(WORDS, "answers.txt")))


def _split(text):
    """(before, block, after) around the marker lines, or None."""
    b = text.find(BEGIN)
    e = text.find(END)
    if b < 0 or e < b:
        return None
    e = text.find("\n", e)
    e = len(text) if e < 0 else e + 1
    return text[:b], text[b:e], text[e:]


def _pack_command(script, check):
    block = render_block(_tables())
    try:
        with open(script, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print("servle_words: %s does not exist" % script)
        return 1
    parts = _split(text)
    if check:
        if parts is None:
            print("servle_words: %s has no generated word block; run tools/servle_words.py pack" % script)
            return 1
        if parts[1] != block:
            print("servle_words: the word block in %s is stale; run tools/servle_words.py pack" % script)
            return 1
        return 0
    if parts is None:
        if text and not text.endswith("\n"):
            text += "\n"
        new = text + "\n" + block
    else:
        new = parts[0] + block + parts[2]
    if new != text:
        with open(script, "w", encoding="utf-8", newline="\n") as f:
            f.write(new)
    return 0


def _read_scowl(final, prefixes, top, pattern=WORD_RE):
    words = set()
    for prefix in prefixes:
        for size in (10, 20, 35, 40, 50, 55, 60, 70):
            if size > top:
                break
            path = os.path.join(final, "%s-words.%d" % (prefix, size))
            if not os.path.exists(path):
                continue
            with open(path, encoding="iso-8859-1") as f:
                words.update(w for w in f.read().split("\n") if pattern.match(w))
    return words


def derive(final, out):
    base = ("english", "american")
    small = _read_scowl(final, base, 35)
    stems = _read_scowl(final, base, 60, re.compile(r"[a-z]{4}\Z"))
    answers = sorted(w for w in small if not (w.endswith("s") and w[:4] in stems))
    guesses = sorted(_read_scowl(final, base + ("british",), 70) | set(answers))
    os.makedirs(out, exist_ok=True)
    for name, words in (("answers.txt", answers), ("guesses.txt", guesses)):
        with open(os.path.join(out, name), "w", encoding="ascii", newline="\n") as f:
            f.write("".join(w + "\n" for w in words))
    print("derived %d answers, %d guesses" % (len(answers), len(guesses)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack", help="rewrite (or --check) the generated block in servle.lua")
    p.add_argument("--check", action="store_true")
    p.add_argument("--script", default=SCRIPT)
    d = sub.add_parser("derive", help="write candidate lists from SCOWL's final/ directory")
    d.add_argument("scowl_final_dir")
    d.add_argument("out_dir")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "pack":
            return _pack_command(args.script, args.check)
        derive(args.scowl_final_dir, args.out_dir)
        return 0
    except ValueError as e:
        print("servle_words: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
