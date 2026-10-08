#!/usr/bin/env python3
"""Runs a web build headless and saves chosen frames as PNG files.

Uses the page's test mode (src/web/shell.html): the game runs N frames as fast
as possible with scripted buttons, no sound. Frames are numbered from 1, one
per frame_end(), and buttons use mgba-capture's letters (A B s S R L U D r l),
so the shots can be compared with an emulator's.

Usage: tools/web-shots.py [--require-picture] PAGE.html FRAMES OUT_PREFIX
                          [shot=N]... [key=FRAME:KEYS:LENGTH]... [save=FILE]
                          [expect-title=TEXT] [expect-save-key=TEXT]
Writes OUT_PREFIX-<frame>.png for each shot. save=FILE stands in for the
cartridge's save memory (as big as the game's save type, like an mGBA .sav):
the game starts from FILE if it exists, and FILE gets the save memory at the
end if the game used it, so consecutive runs see each other's saves.
--require-picture fails if a shot is a single flat color (a smoke test that
the game draws). expect-title and expect-save-key fail unless the page's
title (as the browser shows it) and the localStorage key of its saves are
TEXT. Needs Chrome or Chromium (found on PATH, or set SERVAL_CHROME).
"""

import base64
import functools
import html
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import struct
import threading
import zlib

BROWSERS = ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome"]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    save = None  # the save memory to serve at /serval-save.sav (save=FILE)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/serval-save.sav" and self.save is not None:
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(self.save)))
            self.end_headers()
            self.wfile.write(self.save)
        else:
            super().do_GET()


def find_browser():
    if os.environ.get("SERVAL_CHROME"):
        return os.environ["SERVAL_CHROME"]
    for name in BROWSERS:
        path = shutil.which(name)
        if path:
            return path
    sys.exit("web-shots: no Chrome or Chromium found; set SERVAL_CHROME")


def png_rows(png):
    """Decodes the 8-bit RGBA, non-interlaced PNGs browsers write: rows of bytes."""
    pos, idat = 8, b""
    while pos < len(png):
        length, kind = struct.unpack(">I4s", png[pos:pos + 8])
        data = png[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", data[:10])
            if depth != 8 or color != 6:
                sys.exit("web-shots: unexpected PNG format")
        elif kind == b"IDAT":
            idat += data
        pos += 12 + length
    raw, stride, rows, prev = zlib.decompress(idat), width * 4, [], bytearray(width * 4)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return rows


def main():
    args = sys.argv[1:]
    require_picture = "--require-picture" in args
    args = [a for a in args if a != "--require-picture"]
    if len(args) < 3:
        sys.exit(__doc__)
    page, frames, prefix = args[0], int(args[1]), args[2]
    shots, keys, save, expect = [], [], None, {}
    for arg in args[3:]:
        name, _, value = arg.partition("=")
        if name == "shot":
            shots.append(value)
        elif name == "key":
            keys.append(value)
        elif name == "save":
            save = value
        elif name in ("expect-title", "expect-save-key"):
            expect[name] = value
        else:
            sys.exit(f"web-shots: unknown argument {arg}")

    # Served over HTTP: browsers installed as snaps can't read every local path.
    directory, filename = os.path.split(os.path.abspath(page))
    if save and os.path.exists(save):
        # Served rather than put in the URL: a 128 KiB save is too long for a
        # command line.
        with open(save, "rb") as f:
            QuietHandler.save = f.read()
    handler = functools.partial(QuietHandler, directory=directory)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    fragment = f"frames={frames}"
    if shots:
        fragment += "&shot=" + ",".join(shots)
    if keys:
        fragment += "&keys=" + ",".join(keys)
    if QuietHandler.save is not None:
        fragment += "&saveurl=/serval-save.sav"
    url = f"http://127.0.0.1:{server.server_address[1]}/{filename}#{fragment}"
    # Virtual time runs the page's timers as fast as it can, and the DOM is
    # dumped once nothing is left to run (the page stops after the last frame).
    result = subprocess.run(
        [find_browser(), "--headless=new", "--no-sandbox", "--disable-gpu",
         "--virtual-time-budget=600000", "--dump-dom", url],
        capture_output=True, text=True, timeout=600)
    server.shutdown()

    # What the page's test mode wrote, one line each (src/web/shell.html).
    block = re.search(r'<pre id="shots">(.*?)</pre>', result.stdout, re.DOTALL)
    output = html.unescape(block.group(1)) if block else ""
    found = re.findall(r"^frame (\d+) data:image/png;base64,([A-Za-z0-9+/=]+)$", output, re.MULTILINE)
    flat = []
    for frame, data in found:
        png = base64.b64decode(data)
        with open(f"{prefix}-{int(frame):05d}.png", "wb") as f:
            f.write(png)
        rows = png_rows(png)
        if len(set(rows)) == 1 and len(set(rows[0][i:i + 4] for i in range(0, len(rows[0]), 4))) == 1:
            flat.append(frame)
    saved = re.search(r"^save ([A-Za-z0-9+/=]+)$", output, re.MULTILINE)
    if save and saved:
        with open(save, "wb") as f:
            f.write(base64.b64decode(saved.group(1)))
    if not re.search(r"^done$", output, re.MULTILINE):
        sys.stderr.write(result.stderr[-2000:])
        sys.exit(f"web-shots: the page did not finish {frames} frames")
    if len(found) != len(shots):
        sys.exit(f"web-shots: got {len(found)} of {len(shots)} shots")
    if require_picture and flat:
        sys.exit(f"web-shots: {page}: frame(s) {', '.join(flat)} are a single flat color")
    for name, line in (("expect-title", "title"), ("expect-save-key", "key")):
        if name in expect:
            match = re.search(rf"^{line} (.*)$", output, re.MULTILINE)
            actual = json.loads(match.group(1)) if match else None
            if actual != expect[name]:
                sys.exit(f"web-shots: {page}: the page's {line} is {json.dumps(actual)}, "
                         f"expected {json.dumps(expect[name])}")


if __name__ == "__main__":
    main()
