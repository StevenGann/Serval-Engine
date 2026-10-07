#!/usr/bin/env python3
"""Make Serval Engine's logo PNGs from the splash screen's own drawing code.

The logo exists as C: src/gba/splash_art.c draws it at boot from ASCII
pictures into a canvas of palette indices, then copies that to VRAM. This
tool compiles the drawing part for the host, runs it, and writes the canvas
as transparent PNGs, so the branding images are the splash's exact pixels
(palette index 0, the GBA's transparent color, becomes transparent) and can
never drift from it: CTest's logo_png test fails when they do.

Writes into docs/images/ (or --out DIR), as indexed PNGs with the logo's 16
colors, scaled by whole numbers with nearest-neighbour so the pixel art stays
crisp at any size a page shows it at:

  serval-engine-logo.png, @2x, @4x, @8x    the lockup: the head and the name
                                           (138 x 47 at 1x; "made with" is
                                           the splash's text, not the logo)
  serval-engine-mark.png, @4x, @8x, @16x   the head alone, centered on a 64 x
                                           64 square (256, 512, 1024): avatars,
                                           icons, favicons
  serval-engine-social.png                 GitHub's social preview card, 1280 x
                                           640 and opaque: the lockup at 6x on
                                           the splash's black, a tagline under
                                           it in the splash's 8x8 font (libtonc's
                                           sys8) at 3x. No Nintendo marks: the
                                           repository's description says what
                                           the engine runs on

Usage:
  tools/logo-png.py [--out DIR] [--cc COMPILER]
  tools/logo-png.py --check DIR [--cc COMPILER]   exit 1 if DIR's PNGs differ

Needs a host C compiler (cc, or $CC, or --cc) and Python 3's standard library.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Runs the splash's drawing (canvas_clear, logo) and writes the canvas: its
# size, the 16 palette entries (BGR555) and the indices, one byte per pixel.
# splash_art.c is included rather than linked so its static functions are
# reachable. Only the drawing runs: pack() and serval_splash_art_draw(),
# which write VRAM, are never called (the text call is stubbed to link).
HARNESS = r"""
#include "splash_art.c"
#include <stdio.h>

void serval_text_print_bank(int col, int row, const char* s, u32 bank) {
    (void)col, (void)row, (void)s, (void)bank;
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    canvas_clear();
    logo();
    FILE* f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    fprintf(f, "%d %d\n", CANVAS_W, CANVAS_H);
    for (int c = 0; c < 16; c++)
        fprintf(f, "%u ", (unsigned)colors[c]);
    fprintf(f, "\n");
    fwrite(canvas, 1, CANVAS_W * CANVAS_H, f);
    return fclose(f) != 0;
}
"""

MARK_SQUARE = 64  # the mark's canvas: the 39 x 47 head with room for a round crop

SOCIAL_W, SOCIAL_H = 1280, 640  # GitHub's recommended social preview size
SOCIAL_LOGO_SCALE = 6           # 828 x 282: clear of the edges that cards crop
SOCIAL_TEXT_SCALE = 3
SOCIAL_GAP = 48                 # pixels between the lockup and the tagline
TAGLINE = "An open-source game runtime in C"
TAGLINE_COLOR = 14              # the wordmark's warm grey (palette index B)


def draw(cc):
    """The canvas as (width, height, palette RGB triples, rows of indices)."""
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, "logo_harness.c")
        exe = os.path.join(tmp, "logo_harness")
        dump = os.path.join(tmp, "canvas.dat")
        with open(source, "w") as f:
            f.write(HARNESS)
        # libtonc's headers are written for the GBA; they compile on the host
        # but warn, and none of their functions run here.
        command = [cc, "-std=gnu11", "-w",
                   "-I", os.path.join(ROOT, "src", "gba"),
                   "-I", os.path.join(ROOT, "include"),
                   "-I", os.path.join(ROOT, "third_party", "libtonc", "include"),
                   source, "-o", exe]
        subprocess.run(command, check=True)
        subprocess.run([exe, dump], check=True)
        with open(dump, "rb") as f:
            data = f.read()
    end1 = data.index(b"\n")
    end2 = data.index(b"\n", end1 + 1)
    width, height = map(int, data[:end1].split())
    palette = [bgr555_to_rgb(int(v)) for v in data[end1 + 1:end2].split()]
    pixels = data[end2 + 1:end2 + 1 + width * height]
    if len(palette) != 16 or len(pixels) != width * height:
        sys.exit("logo-png: the harness wrote an unexpected canvas")
    if max(pixels) > 15:
        sys.exit("logo-png: the canvas holds an index above 15 (a marker bit left set?)")
    return width, height, palette, [list(pixels[y * width:(y + 1) * width]) for y in range(height)]


def bgr555_to_rgb(color):
    """The GBA's 5-bit channels to 8 bits, as the hardware shows them (the
    top bits repeated at the bottom, so 31 is 255)."""
    def expand(v):
        return v << 3 | v >> 2
    return (expand(color & 31), expand(color >> 5 & 31), expand(color >> 10 & 31))


def sys8():
    """libtonc's 8x8 font, as the text layer uses it: 96 glyphs (ASCII 32 to
    127) of 8 rows, one byte per row, bit 0 the leftmost pixel. Read from
    libtonc's source (little-endian .word data after sys8Glyphs)."""
    words, inside = [], False
    with open(os.path.join(ROOT, "third_party", "libtonc", "src", "font", "sys8.s")) as f:
        for line in f:
            line = line.split("@")[0].strip()
            if line.startswith("sys8Glyphs:"):
                inside = True
            elif inside and line.startswith(".word"):
                words += [int(w, 16) for w in line[len(".word"):].split(",")]
            elif inside and line:
                break
    data = b"".join(struct.pack("<I", w) for w in words)
    if len(data) != 96 * 8:
        sys.exit("logo-png: couldn't read libtonc's sys8 font")
    return data


def text(s, color):
    """s in sys8, one row of 8 x 8 cells, as rows of palette indices."""
    font = sys8()
    rows = [[0] * (8 * len(s)) for _ in range(8)]
    for i, ch in enumerate(s):
        glyph = font[(ord(ch) - 32) * 8:(ord(ch) - 31) * 8] if 32 <= ord(ch) < 128 else bytes(8)
        for y in range(8):
            for x in range(8):
                if glyph[y] >> x & 1:
                    rows[y][8 * i + x] = color
    return rows


def place(target, rows, left, top):
    for y, row in enumerate(rows):
        target[top + y][left:left + len(row)] = row


def social(logo):
    """The 1280 x 640 card: the lockup and the tagline, centered together on
    a black field (index 0, written opaque)."""
    lockup = scale(logo, SOCIAL_LOGO_SCALE)
    tagline = scale(crop(text(TAGLINE, TAGLINE_COLOR), *bounds(text(TAGLINE, TAGLINE_COLOR))),
                    SOCIAL_TEXT_SCALE)
    height = len(lockup) + SOCIAL_GAP + len(tagline)
    top = (SOCIAL_H - height) // 2
    card = [[0] * SOCIAL_W for _ in range(SOCIAL_H)]
    place(card, lockup, (SOCIAL_W - len(lockup[0])) // 2, top)
    place(card, tagline, (SOCIAL_W - len(tagline[0])) // 2, top + len(lockup) + SOCIAL_GAP)
    return card


def crop(rows, x0, y0, x1, y1):
    return [row[x0:x1] for row in rows[y0:y1]]


def bounds(rows, columns=None):
    """The smallest box (x0, y0, x1, y1) holding every drawn pixel, of the
    given columns only if `columns` is a range."""
    xs = columns if columns is not None else range(len(rows[0]))
    used = [(x, y) for y, row in enumerate(rows) for x in xs if row[x]]
    return (min(x for x, _ in used), min(y for _, y in used),
            max(x for x, _ in used) + 1, max(y for _, y in used) + 1)


def head_columns(rows):
    """The head's columns: the first run of columns with anything drawn (an
    empty gap separates it from the name)."""
    width = len(rows[0])
    filled = [any(row[x] for row in rows) for x in range(width)]
    start = filled.index(True)
    end = filled.index(False, start)
    return range(start, end)


def pad(rows, size):
    """rows centered on a size x size square of transparent pixels (index 0)."""
    h, w = len(rows), len(rows[0])
    if w > size or h > size:
        sys.exit(f"logo-png: the mark ({w} x {h}) no longer fits its {size} x {size} square")
    left, top = (size - w) // 2, (size - h) // 2
    out = [[0] * size for _ in range(size)]
    for y, row in enumerate(rows):
        out[top + y][left:left + w] = row
    return out


def scale(rows, n):
    return [[v for v in row for _ in range(n)] for row in rows for _ in range(n)]


def png(rows, palette, transparent=True):
    """An indexed-color PNG (4 bits per pixel), index 0 fully transparent
    unless `transparent` is false (then it shows as its palette color)."""
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter: none
        padded = row + [0] if w % 2 else row
        raw.extend(padded[i] << 4 | padded[i + 1] for i in range(0, len(padded), 2))

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 4, 3, 0, 0, 0)) +
            chunk(b"PLTE", b"".join(bytes(c) for c in palette)) +
            (chunk(b"tRNS", b"\x00") if transparent else b"") +  # index 0 transparent
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def images(cc):
    """{file name: PNG bytes} for every image this tool makes."""
    width, height, palette, rows = draw(cc)
    logo = crop(rows, *bounds(rows))
    mark = pad(crop(rows, *bounds(rows, head_columns(rows))), MARK_SQUARE)
    out = {}
    for n in (1, 2, 4, 8):
        out[f"serval-engine-logo{'' if n == 1 else f'@{n}x'}.png"] = png(scale(logo, n), palette)
    for n in (1, 4, 8, 16):
        out[f"serval-engine-mark{'' if n == 1 else f'@{n}x'}.png"] = png(scale(mark, n), palette)
    # Index 0 is black in the palette already: the splash's backdrop.
    out["serval-engine-social.png"] = png(social(logo), palette, transparent=False)
    return out


def decoded(data):
    """A PNG this tool wrote, as comparable (header, palette, transparency,
    pixel bytes): the compressed bytes may differ between zlib versions."""
    chunks, at = {}, 8
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        chunks.setdefault(kind, b"")
        chunks[kind] += data[at + 8:at + 8 + length]
        at += 12 + length
    return (chunks.get(b"IHDR"), chunks.get(b"PLTE"), chunks.get(b"tRNS"),
            zlib.decompress(chunks[b"IDAT"]) if b"IDAT" in chunks else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default=os.path.join(ROOT, "docs", "images"),
                        help="where to write the PNGs (default: docs/images)")
    parser.add_argument("--check", metavar="DIR",
                        help="compare DIR's PNGs with what the drawing code makes now")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"),
                        help="host C compiler (default: $CC, else cc)")
    args = parser.parse_args()

    made = images(args.cc)
    if args.check:
        stale = []
        for name, data in made.items():
            path = os.path.join(args.check, name)
            if not os.path.exists(path):
                stale.append(f"{name} (missing)")
                continue
            with open(path, "rb") as f:
                try:
                    same = decoded(f.read()) == decoded(data)
                except (zlib.error, struct.error, KeyError):
                    same = False
            if not same:
                stale.append(name)
        if stale:
            print("logo-png: these differ from the splash's drawing code; run tools/logo-png.py "
                  "to regenerate them:\n  " + "\n  ".join(stale), file=sys.stderr)
            return 1
        print(f"logo-png: {len(made)} images match the splash's drawing code")
        return 0

    os.makedirs(args.out, exist_ok=True)
    for name, data in made.items():
        with open(os.path.join(args.out, name), "wb") as f:
            f.write(data)
        print(f"{os.path.relpath(os.path.join(args.out, name))}  ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
