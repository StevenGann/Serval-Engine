#!/usr/bin/env python3
"""Tests for tools/svlink.py, the debug link's reference client, and through
it the debug link end to end (docs/debug-link.md).

Usage: tools/svlink_test.py
       tools/svlink_test.py --page DEBUG.html --release-page RELEASE.html
                            --script-c fireflies_script.c --script-h fireflies_script.h
                            [--svmap fireflies.svmap] --version VERSION

Without --page (CTest svlink_tool, host preset): the frame codec, FNV-1a,
the WebSocket server against RFC 6455's own examples, and the server's checks
(Host, Origin, the token, a frame before HELLO, oversized and text messages),
with a WebSocket client written here standing in for the page.

With --page (CTest svlink_e2e, web preset): fireflies' debug page in headless
Chrome, served and driven by svlink.py: the handshake; AGENT_INFO (the engine
version, the limits, the title and game code, the blob's size and FNV-1a hash,
against the blob the build made and its .svmap); LOADED; pause and a frame step (the frame
number advances by one, and the hardware the picture comes from, OAM and
palette RAM, stops changing while parked and changes while running); globals
read and written (setting a global makes an engine warning arrive as LOG, and
setting the round's time to 1 ends the round: the serval sits down); an
entity's properties read and set (the serval moved, a read-only property
refused); STATS arriving; the endpoint's own READ_MEM and MACHINE_PAUSE; and
that the release build of the same page has no agent and never connects.
And source-level debugging (step E6), with the script's maps beside the page:
a breakpoint on a Lua line hit, the stack and its locals read, steps over,
into and out planned from the maps, continuing; then the game's state at a
later breakpoint matches, cell for cell, a run with that breakpoint alone.
And hot reload (step E9, --reload): with the serval walking (the test mode's
scripted buttons) and fireflies wandering, fireflies.lua rebuilt with the
serval twice as fast is reloaded into the running page: every behaviour is
kept at its new offsets (the fireflies where they were, waiting as before),
the score and the timer carry over, the serval then walks 3 pixels a frame
instead of 1.5, the timer counts on, and a breakpoint set by the new maps
stops on the edited line; --shots saves the frames before and after.
Exits 77 (skipped) without Chrome.
"""

import argparse
import base64
import contextlib
import io
import json
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svlink  # noqa: E402

ARGS = None


# --- Without a browser ---------------------------------------------------------------


class Codec(unittest.TestCase):
    def test_frame_layout(self):
        f = svlink.frame(svlink.GLOBALS, 0, 0x1234, struct.pack("<HH", 2, 3))
        self.assertEqual(f, bytes([8, 0, 0x48, 0, 0x34, 0x12, 2, 0, 3, 0]))
        self.assertEqual(svlink.parse(f), (0x48, 0, 0x1234, bytes([2, 0, 3, 0])))

    def test_frame_limits(self):
        svlink.frame(1, 0, 1, bytes(1018))
        with self.assertRaises(ValueError):
            svlink.frame(1, 0, 1, bytes(1019))
        for bad in (b"\x04\x00\x40\x00\x01", b"\x05\x00\x40\x00\x01\x00", bytes(1025)):
            with self.assertRaises(ValueError):
                svlink.parse(bad)

    def test_fnv1a(self):
        self.assertEqual(svlink.fnv1a(b""), 0x811C9DC5)
        self.assertEqual(svlink.fnv1a(b"a"), 0xE40C292C)
        self.assertEqual(svlink.fnv1a(b"foobar"), 0xBF9CF968)

    def test_hello(self):
        p = svlink.hello_payload(svlink.KIND_WEB, "1.2.0", "tok", 7)
        self.assertEqual(p, bytes([1, 0, 2, 5]) + b"1.2.0" + bytes([3]) + b"tok" + bytes([7, 0, 0, 0]))
        self.assertEqual(svlink.parse_hello(p),
                         {"proto": (1, 0), "kind": 2, "version": "1.2.0", "token": "tok", "capabilities": 7})

    def test_agent_info(self):
        p = (bytes([1, 0]) + svlink.pack_str("1.0.0") + struct.pack("<I", 0x31) + bytes([32, 64, 16])
             + struct.pack("<HBHHBHI", 256, 16, 1024, 128, 21, 0, 32768) + svlink.pack_str("FIREFLIES")
             + svlink.pack_str("SVFF") + struct.pack("<II", 1201, 0x74A1E107))
        info = svlink.parse_agent_info(p)
        self.assertEqual((info["engine"], info["capabilities"], info["contexts"], info["array_cells"],
                          info["entities"], info["scratch"], info["title"], info["blob_hash"]),
                         ("1.0.0", 0x31, 32, 1024, 128, 32768, "FIREFLIES", 0x74A1E107))

    def test_reloaded(self):  # E9
        p = bytes([0, 3, 0, 0, 4, 1, 7, 2])
        self.assertEqual(svlink.parse_reloaded(p), {"result": 0, "contexts": {0: 0, 4: 1, 7: 2}})
        self.assertEqual(svlink.parse_reloaded(bytes([12, 0])), {"result": 12, "contexts": {}})

    def test_plan_frames(self):  # E9: each section in frames that fit, u8 counts for 2 and 5
        sections = {1: [(i, i + 1, i + 2) for i in range(200)], 2: [(k, 0) for k in range(32)],
                    3: [(n, n) for n in range(256)], 5: [(1, 0)], 6: []}
        frames = svlink.plan_frames(sections)
        self.assertTrue(all(6 + len(p) <= svlink.FRAME_MAX for p in frames))
        ones = [p for p in frames if p[0] == 1]
        self.assertEqual([struct.unpack_from("<H", p, 1)[0] for p in ones], [84, 84, 32])
        self.assertEqual(struct.unpack_from("<III", ones[1], 3), (84, 85, 86))
        two = [p for p in frames if p[0] == 2]
        self.assertEqual((len(two), two[0][1], len(two[0])), (1, 32, 2 + 64))
        self.assertEqual([struct.unpack_from("<H", p, 1)[0] for p in frames if p[0] == 3], [253, 3])
        self.assertEqual([p for p in frames if p[0] == 5], [bytes([5, 1, 1, 0])])
        self.assertFalse([p for p in frames if p[0] == 6])

    def test_property_names(self):
        names = svlink.property_names()
        self.assertEqual((names[0], names[10], names[20]), ("x", "body_w", "object"))
        self.assertEqual(len(names), 21)


class WebSocket(unittest.TestCase):
    def test_rfc6455_accept(self):  # RFC 6455, section 1.3
        self.assertEqual(svlink.ws_accept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

    def test_rfc6455_masked_hello(self):  # RFC 6455, section 5.7
        data = bytes([0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58])
        self.assertEqual(svlink.ws_read_frame(io.BytesIO(data)), (True, svlink.WS_TEXT, b"Hello"))

    def test_rfc6455_unmasked_is_refused(self):
        with self.assertRaises(svlink.WsClosed) as e:
            svlink.ws_read_frame(io.BytesIO(bytes([0x81, 0x05]) + b"Hello"))
        self.assertEqual(e.exception.code, 1002)

    def test_server_frames(self):
        self.assertEqual(svlink.ws_frame(svlink.WS_TEXT, b"Hello"), bytes([0x81, 0x05]) + b"Hello")
        self.assertEqual(svlink.ws_frame(svlink.WS_BINARY, bytes(256))[:4], bytes([0x82, 126, 1, 0]))


class Client:
    """A WebSocket client standing in for the page: masked frames."""

    def __init__(self, port, origin=None, host=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        origin = origin if origin is not None else f"http://127.0.0.1:{port}"
        host = host or f"127.0.0.1:{port}"
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f"GET /serval-link HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
                   + (f"Origin: {origin}\r\n" if origin else "") + "\r\n")
        self.sock.sendall(request.encode())
        self.file = self.sock.makefile("rb")
        self.status = int(self.file.readline().split()[1])
        self.headers = {}
        while True:
            line = self.file.readline().decode().strip()
            if not line:
                break
            name, _, value = line.partition(":")
            self.headers[name.strip().lower()] = value.strip()
        self.accept_ok = self.headers.get("sec-websocket-accept") == svlink.ws_accept(key)

    def send(self, opcode, payload, fin=True):
        mask = os.urandom(4)
        n = len(payload)
        head = bytes([(0x80 if fin else 0) | opcode])
        head += bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n)
        self.sock.sendall(head + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(payload)))

    def recv(self):
        """(opcode, payload) of the server's next frame, or None at EOF."""
        head = self.file.read(2)
        if len(head) < 2:
            return None
        n = head[1] & 0x7F
        if n == 126:
            (n,) = struct.unpack(">H", self.file.read(2))
        return head[0] & 0x0F, self.file.read(n)

    def hello(self, token):
        self.send(svlink.WS_BINARY, svlink.frame(svlink.HELLO, 0, 0,
                                                 svlink.hello_payload(svlink.KIND_WEB, "1.0.0", token, 7)))

    def close(self):
        self.sock.close()


class ServerChecks(unittest.TestCase):
    def setUp(self):
        self.server = svlink.Server(os.path.dirname(os.path.abspath(__file__)), token="good-token")

    def tearDown(self):
        self.server.close()

    def closed_with(self, client, code):
        while True:
            got = client.recv()
            if got is None:
                return code is None
            if got[0] == svlink.WS_CLOSE:
                return struct.unpack(">H", got[1][:2])[0] == code

    def test_handshake_and_hello(self):
        c = Client(self.server.port)
        self.assertEqual(c.status, 101)
        self.assertTrue(c.accept_ok)
        c.hello("good-token")
        session = self.server.accept(5)
        self.assertEqual(session.hello["kind"], svlink.KIND_WEB)
        op, data = c.recv()
        self.assertEqual(op, svlink.WS_BINARY)
        type_, flags, seq, payload = svlink.parse(data)
        self.assertEqual((type_, flags, seq), (svlink.HELLO, 0, 0))
        self.assertEqual(svlink.parse_hello(payload)["kind"], svlink.KIND_DEBUGGER)
        # A request goes out, its reply comes back by seq; a ping is answered.
        c.send(svlink.WS_PING, b"hi")
        self.assertEqual(c.recv(), (svlink.WS_PONG, b"hi"))
        import threading
        result = {}
        t = threading.Thread(target=lambda: result.setdefault("r", session.request(svlink.MACHINE_PAUSE)))
        t.start()
        op, data = c.recv()
        type_, flags, seq, _ = svlink.parse(data)
        self.assertEqual((type_, flags), (svlink.MACHINE_PAUSE, 0))
        c.send(svlink.WS_BINARY, svlink.frame(svlink.MACHINE_PAUSE, svlink.REPLY, seq))
        t.join(5)
        self.assertEqual(result.get("r"), b"")
        # An error reply raises LinkError; events are kept, fragments joined.
        t = threading.Thread(target=lambda: self.assertRaises(
            svlink.LinkError, session.request, svlink.READ_MEM, bytes(6)))
        t.start()
        _, data = c.recv()
        seq = svlink.parse(data)[2]
        c.send(svlink.WS_BINARY, svlink.frame(svlink.READ_MEM, 3, seq, bytes([8]) + svlink.pack_str("no")))
        t.join(5)
        event = svlink.frame(svlink.LOG, 0, 0, bytes([1, 1]) + struct.pack("<I", 9) + svlink.pack_str("hi"))
        c.send(svlink.WS_BINARY, event[:3], fin=False)
        c.send(svlink.WS_CONTINUATION, event[3:])
        log = session.wait_event(svlink.LOG, 5)
        self.assertEqual((log["level"], log["frame"], log["text"]), (1, 9, "hi"))
        c.close()

    def test_wrong_origin(self):
        for origin in ("http://evil.example", "http://localhost:%d" % self.server.port, ""):
            c = Client(self.server.port, origin=origin)
            self.assertEqual(c.status, 403, origin)
            c.close()
        self.assertTrue(self.server.sessions.empty())

    def test_wrong_host(self):
        c = Client(self.server.port, host="evil.example:%d" % self.server.port)
        self.assertEqual(c.status, 403)
        c.close()

    def test_wrong_token(self):
        c = Client(self.server.port)
        c.hello("bad-token")
        self.assertTrue(self.closed_with(c, 1008))
        self.assertTrue(self.server.sessions.empty())
        self.assertIn("wrong token", self.server.refused)

    def test_frame_before_hello(self):
        c = Client(self.server.port)
        c.send(svlink.WS_BINARY, svlink.frame(svlink.LOG, 0, 0, bytes(7)))
        self.assertTrue(self.closed_with(c, 1008))

    def test_oversized_and_text(self):
        c = Client(self.server.port)
        c.send(svlink.WS_BINARY, bytes(1025))
        self.assertTrue(self.closed_with(c, 1009))
        c = Client(self.server.port)
        c.send(svlink.WS_TEXT, b"hello")
        self.assertTrue(self.closed_with(c, 1003))


# --- End to end, in a browser -----------------------------------------------------------

HARDWARE_OAM, HARDWARE_PALETTE, HARDWARE_IO = 0x07000000, 0x05000000, 0x04000000


def script_bytes(path):
    """The blob a generated <name>_script.c defines."""
    with open(path, encoding="ascii") as f:
        text = f.read()
    body = text[text.index("[] = {"):text.index("};")]
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})\b", re.sub(r"/\*.*?\*/", "", body)))


def script_names(path):
    with open(path, encoding="ascii") as f:
        return {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (\w+) (\d+)", f.read())}


class EndToEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = script_bytes(ARGS.script_c)
        cls.names = script_names(ARGS.script_h)

    def open(self, keys=""):
        """The game, debugged: its web page in headless Chrome, or (--rom) its
        debug ROM in serval-emu, the mGBA fork."""
        if ARGS.rom:
            return svlink.open_rom(ARGS.rom, emu=ARGS.emu, keys=keys)
        return svlink.open_page(ARGS.page, keys=keys)

    @staticmethod
    def maps_dir():
        return os.path.dirname(os.path.abspath(ARGS.rom or ARGS.page))

    def picture(self, link):
        """What the screen is made of: OAM, palette RAM, the scroll registers."""
        return (link.read_mem(HARDWARE_OAM, 1024) + link.read_mem(HARDWARE_PALETTE, 1024)
                + link.read_mem(HARDWARE_IO + 0x10, 16))

    def run_until(self, link, done, timeout=60):
        """Lets the game run, parking it now and then to look, until done(link)."""
        deadline = time.monotonic() + timeout
        while True:
            link.pause()
            if done(link):
                return
            self.assertLess(time.monotonic(), deadline, "the game never got there")
            link.resume(0)
            time.sleep(0.1)

    def test_debug_page(self):
        g = self.names
        with self.open() as link:
            # The handshake: the page's HELLO, then AGENT_INFO.
            if ARGS.rom:  # the fork's own version
                self.assertEqual(link.hello["kind"], svlink.KIND_FORK)
                self.assertTrue(link.hello["version"].startswith("serval-emu "), link.hello["version"])
            else:  # a page says its engine's
                self.assertEqual(link.hello["kind"], svlink.KIND_WEB)
                self.assertEqual(link.hello["version"], ARGS.version)
            self.assertEqual(link.hello["proto"], (1, 0))
            self.assertEqual(link.hello["capabilities"], 7)
            info = link.agent
            self.assertEqual(info["proto"], (1, 0))
            self.assertEqual(info["engine"], ARGS.version)
            # vm, breakpoints, step, reload (E9), stats, entities, stop on fault
            self.assertEqual(info["capabilities"], 0x7F)
            self.assertEqual((info["contexts"], info["stack"], info["calls"], info["globals"],
                              info["fields"], info["array_cells"], info["entities"],
                              info["properties"], info["sites"], info["scratch"]),
                             (32, 64, 16, 256, 16, 1024, 128, 21, 256, 32768))
            self.assertEqual((info["title"], info["game_code"]), ("FIREFLIES", "SVFF"))
            # The blob: loaded after the splash, which may still be showing.
            # Its hash, as svlink.py computes it and as the build's pc map
            # (svm.py --map) gives it.
            expected = (len(self.blob), svlink.fnv1a(self.blob))
            if ARGS.svmap:
                with open(ARGS.svmap, encoding="utf-8") as f:
                    svmap = json.load(f)
                self.assertEqual((svmap["blob"]["size"], svmap["blob"]["fnv1a32"]), expected)
            if info["blob_size"] == 0:
                loaded = link.wait_event(svlink.LOADED, 60)
                self.assertEqual((loaded["size"], loaded["hash"]), expected)
                info = link.agent_info()
            self.assertEqual((info["blob_size"], info["blob_hash"]), expected)

            # Running, the picture changes; parked, it doesn't, and a frame
            # step is one frame.
            self.run_until(link, lambda l: l.globals(g["G_PLAYING"], 1) == [1])
            link.resume(0)
            first = self.picture(link)
            deadline = time.monotonic() + 30
            while self.picture(link) == first:
                self.assertLess(time.monotonic(), deadline, "the picture never changed")
                time.sleep(0.05)
            stop = link.pause()
            self.assertEqual((stop["reason"], stop["context"], stop["pc"], stop["fault"]), (0, 0xFF, 0, 0))
            parked = self.picture(link)
            time.sleep(0.5)
            self.assertEqual(self.picture(link), parked)
            step = link.step_frame()
            self.assertEqual((step["reason"], step["frame"]), (1, stop["frame"] + 1))
            step2 = link.step_frame()
            self.assertEqual(step2["frame"], stop["frame"] + 2)
            stops = link.count(svlink.STOPPED)
            time.sleep(0.3)
            self.assertEqual(link.count(svlink.STOPPED), stops)  # parked: no frame runs

            # Globals: read, and written. A dead entity in `player` makes the
            # Spawner's next look at it warn: the warning arrives as LOG.
            values = link.globals(0, g["G_COUNT"])
            self.assertEqual(values[g["G_PLAYING"]], 1)
            self.assertTrue(0 < values[g["G_TIME"]] <= 60)
            player = values[g["G_PLAYER"]]
            link.set_global(g["G_PLAYER"], 0xFEFF)
            self.assertEqual(link.globals(g["G_PLAYER"], 1), [0xFEFF])
            logs = link.count(svlink.LOG)
            link.resume(0)
            log = link.wait_event(svlink.LOG, 60, after=logs, test=lambda e: "is not alive" in e["text"])
            self.assertEqual((log["level"], log["source"]), (1, 1))
            self.assertTrue(log["text"].startswith("serval: vm: GETP/SETP"), log["text"])
            link.pause()
            link.set_global(g["G_PLAYER"], player)

            # An entity: the serval's properties, read and set.
            serval = [e for e in link.entities() if e["object"] == g["OBJ_PLAYER"]]
            self.assertEqual(len(serval), 1)
            self.assertEqual(serval[0]["entity"], player)
            self.assertIsNone(serval[0]["context"])  # its create has ended
            # A firefly's create is a behaviour, waiting: it has a context.
            self.run_until(link, lambda l: any(x["object"] == g["OBJ_FIREFLY"] for x in l.entities()))
            flies = [x for x in link.entities() if x["object"] == g["OBJ_FIREFLY"]]
            self.assertTrue(all(x["context"] is not None and x["context"] < 32 for x in flies))
            e = link.entity(player)
            self.assertEqual((e["object"], len(e["properties"]), e["properties"][20]),
                             (g["OBJ_PLAYER"], 21, g["OBJ_PLAYER"]))
            link.set_property(player, 0, 40 * 256)  # x: 40 pixels
            link.set_property(player, 1, 100 * 256)  # y
            self.assertEqual(link.entity(player)["properties"][:2], [40 * 256, 100 * 256])
            link.step_frame()
            self.assertEqual(link.entity(player)["properties"][:2], [40 * 256, 100 * 256])
            with self.assertRaises(svlink.LinkError) as refused:
                link.set_property(player, 19, 1)  # body_contact
            self.assertEqual(refused.exception.code, 9)
            with self.assertRaises(svlink.LinkError) as refused:
                link.entity(0xFEFF)
            self.assertEqual(refused.exception.code, 8)

            # STATS, every 5 frames.
            stats = link.count(svlink.STATS)
            link.stats_subscribe(5)
            link.resume(0)
            s1 = link.wait_event(svlink.STATS, 30, after=stats)
            s2 = link.wait_event(svlink.STATS, 30, after=stats + 1)
            self.assertEqual(s2["frame"], s1["frame"] + 5)
            self.assertGreater(s1["ops"], 0)
            self.assertGreaterEqual(s1["contexts"], 2)
            self.assertGreaterEqual(s1["entities"], 1)
            self.assertGreaterEqual(s1["drawn"], 1)
            self.assertGreaterEqual(s1["warnings"], 1)
            link.stats_subscribe(0)

            # The endpoint's own: READ_MEM of what isn't hardware (the fork
            # reads any address; a page only the hardware's), MACHINE_PAUSE.
            if ARGS.rom:
                self.assertEqual(link.read_mem(0x08000000 + 0xAC, 4), b"SVFF")  # the header's game code
            else:
                with self.assertRaises(svlink.LinkError) as refused:
                    link.read_mem(0x02000000, 4)
                self.assertEqual(refused.exception.code, 8)
            with self.assertRaises(svlink.LinkError) as refused:
                link.request(svlink.READ_MEM, struct.pack("<IH", HARDWARE_OAM, 0))
            self.assertEqual(refused.exception.code, 2)
            link.stats_subscribe(1)
            link.machine_pause()
            time.sleep(0.3)
            frozen = link.count(svlink.STATS)
            time.sleep(0.5)
            self.assertEqual(link.count(svlink.STATS), frozen)
            link.machine_resume()
            link.wait_event(svlink.STATS, 10, after=frozen)
            link.stats_subscribe(0)

            # A global changes what the game does: one second left, and the
            # round ends: playing goes false, the serval sits down (a Resting
            # in its place).
            link.pause()
            link.set_global(g["G_TIME"], 1)
            self.run_until(link, lambda l: l.globals(g["G_PLAYING"], 1) == [0], 60)
            objects = [x["object"] for x in link.entities()]
            self.assertIn(g["OBJ_RESTING"], objects)
            self.assertNotIn(g["OBJ_PLAYER"], objects)
            link.resume(0)

    # --- Source-level debugging (E6) -------------------------------------------------

    SYNC_TIME = 50  # the round's seconds left when the runs are compared

    def state_at_sync(self, link, maps):
        """Lets the game run to the breakpoint on `time = time - 1` (the
        Room's once a second) with SYNC_TIME seconds left, and returns what
        the game is then: the stop, the globals, every entity's properties
        and fields, every context."""
        g = self.names
        link.break_set(svlink.SITE_BREAKPOINT, maps.line_sites("fireflies.lua:122"))
        while True:
            stop = link.go(60)
            self.assertEqual((stop["reason"], maps.place(stop["pc"])), (3, "fireflies.lua:122"))
            left = link.globals(g["G_TIME"], 1)[0]
            self.assertGreaterEqual(left, self.SYNC_TIME, "the session ran past the sync point")
            if left == self.SYNC_TIME:
                break
        entities = [(e, link.entity(e["entity"])) for e in link.entities()]
        state = {"stop": (stop["frame"], stop["pc"], stop["context"]),
                 "globals": link.globals(0, g["G_COUNT"]), "entities": entities,
                 "contexts": link.contexts()}
        link.break_clear(svlink.SITE_BREAKPOINT)
        return state

    def test_breakpoints_and_steps(self):
        maps_dir = self.maps_dir()
        # A run with the sync breakpoint alone.
        with self.open() as link:
            undisturbed = self.state_at_sync(link, svlink.maps_for(link, maps_dir))
            link.resume(0)
        with self.open() as link:
            maps = svlink.maps_for(link, maps_dir)
            self.assertEqual(maps.blob, link.blob)
            # A breakpoint in Firefly:create's flights, hit by the first
            # firefly: its stack, its locals.
            pcs = maps.line_sites("fireflies.lua:230")  # path_start(self, ...)
            link.break_set(svlink.SITE_BREAKPOINT, pcs)
            stop = link.go(60)
            self.assertEqual((stop["reason"], stop["pc"]), (3, pcs[0]))
            k = stop["context"]
            frames = link.frames(k)
            self.assertEqual(len(frames["activations"]), 1)
            frame = frames["activations"][0]["frames"][0]
            self.assertEqual(frame["pc"], pcs[0])
            self.assertEqual(maps.function(frame["pc"])["name"], "Firefly:create")
            found = maps.locals_at(frame["pc"])
            self.assertIn("flight", found)
            self.assertEqual(frames["cells"][frame["fp"] + found["flight"][0]], 1)
            context = [c for c in link.contexts() if c["context"] == k][0]
            self.assertEqual((context["state"], context["event"], context["pc"]), (2, 0, pcs[0]))
            entity = link.entity(context["self"])
            self.assertEqual(entity["object"], self.names["OBJ_FIREFLY"])
            # Step over: the next line, in this firefly (the others run the
            # same code past the step sites without stopping); over again,
            # past wait_move(): the game runs until its path ends (or the
            # serval catches it: then its create ends).
            link.break_clear(svlink.SITE_BREAKPOINT)
            stop = svlink.plan_step(link, maps, "over")
            self.assertEqual((stop["reason"], stop["context"]), (4, k))
            self.assertEqual(maps.place(stop["pc"]), "fireflies.lua:232")  # wait_move()
            before = stop["frame"]
            stop = svlink.plan_step(link, maps, "over")
            self.assertIn(stop["reason"], (4, 5))
            self.assertEqual(stop["context"], k)
            if stop["reason"] == 4:
                self.assertEqual(maps.place(stop["pc"]), "fireflies.lua:233")  # wait(...)
                self.assertGreater(stop["frame"], before)  # it waited for its path
            # Into print_time() from the Room's loop, over a line, out again.
            room = maps.line_sites("fireflies.lua:123")  # print_time()
            link.break_set(svlink.SITE_BREAKPOINT, room)
            stop = link.go(60)
            self.assertEqual((stop["reason"], stop["pc"]), (3, room[0]))
            link.break_clear(svlink.SITE_BREAKPOINT)
            stop = svlink.plan_step(link, maps, "into")
            self.assertEqual((stop["reason"], maps.place(stop["pc"])), (4, "fireflies.lua:298"))
            frames = link.frames(stop["context"])["activations"][0]["frames"]
            self.assertEqual([maps.function(f["pc"] - (n > 0))["name"] for n, f in enumerate(frames)],
                             ["print_time", "Room:room_start"])
            self.assertEqual(maps.place(frames[1]["pc"] - 1), "fireflies.lua:123")
            stop = svlink.plan_step(link, maps, "over")
            self.assertEqual((stop["reason"], maps.place(stop["pc"])), (4, "fireflies.lua:299"))
            stop = svlink.plan_step(link, maps, "out")
            self.assertEqual(stop["reason"], 4)
            self.assertEqual(maps.function(stop["pc"])["name"], "Room:room_start")
            self.assertEqual(len(link.frames(stop["context"])["activations"][0]["frames"]), 1)
            # The breakpoints and steps changed nothing: at the sync point
            # the game is exactly as in the run without them.
            disturbed = self.state_at_sync(link, maps)
            link.resume(0)
        self.assertEqual(disturbed["stop"], undisturbed["stop"])
        self.assertEqual(disturbed["globals"], undisturbed["globals"])
        self.assertEqual(disturbed["contexts"], undisturbed["contexts"])
        self.assertEqual(disturbed["entities"], undisturbed["entities"])

    # --- Hot reload (E9) -----------------------------------------------------------

    @staticmethod
    def walking_keys(frames=20000, leg=40):
        """The serval walking right, then left, 40 frames each (1.5 pixels a
        frame: 60 pixels either way, never reaching an edge)."""
        return ",".join(f"{at}:{'r' if (at // leg) % 2 == 0 else 'l'}:{leg}"
                        for at in range(0, frames, leg))

    def serval_steps(self, link, player, frames):
        """How far the serval moves in each of the next frames (frame steps)."""
        steps = []
        x = link.entity(player)["properties"][0]
        for _ in range(frames):
            link.step_frame()
            now = link.entity(player)["properties"][0]
            steps.append(abs(now - x))
            x = now
        return steps

    def test_hot_reload(self):
        if not ARGS.reload:
            self.skipTest("no --reload build")
        g = self.names
        maps_dir = self.maps_dir()
        with self.open(keys=self.walking_keys()) as link:
            old = svlink.running_build(link, maps_dir)
            new = svlink.Build.load(ARGS.reload, stem=old.stem)
            # The round on, the serval walking, three fireflies wandering.
            flies = lambda l: [e for e in l.entities() if e["object"] == g["OBJ_FIREFLY"]]  # noqa: E731
            self.run_until(link, lambda l: l.globals(g["G_PLAYING"], 1) == [1] and len(flies(l)) >= 3)
            link.set_global(g["G_SCORE"], 7)  # something to carry over (the HUD shows it at a catch)
            player = link.globals(g["G_PLAYER"], 1)[0]
            shots = ARGS.shots or tempfile.mkdtemp(prefix="svlink-reload-")
            pictures = {}

            def shot(name):
                # A page's frame as a PNG; the fork's (no screenshots in 1.0) as
                # what the screen is made of.
                pictures[name] = self.picture(link)
                if not ARGS.rom:
                    link.screenshot(os.path.join(shots, f"svlink-reload-{name}.png"))
            shot("1-before")
            before_speed = self.serval_steps(link, player, 8)
            self.assertTrue(set(before_speed) <= {0, 384} and before_speed.count(384) >= 6, before_speed)
            shot("2-reloading")
            globals_before = link.globals(0, g["G_COUNT"])
            positions = {e["entity"]: link.entity(e["entity"])["properties"][:2] for e in flies(link)}
            contexts = {c["context"]: c for c in link.contexts()}
            started = time.monotonic()
            done = svlink.reload(link, old, new)
            took = time.monotonic() - started
            print(f"\nreload: {len(new.blob)} bytes, {len(done['contexts'])} contexts, {took * 1000:.0f} ms",
                  file=sys.stderr)
            # Every behaviour kept: only Player:step (a reaction) changed.
            self.assertEqual(done["result"], 0)
            self.assertEqual(set(done["contexts"]), set(contexts))
            self.assertEqual(set(done["contexts"].values()), {0}, done["contexts"])
            changed = {n for n in svlink.function_ranges(old)} - \
                {n for n, *_ in svlink.unchanged_functions(old, new)}
            self.assertEqual(changed, {"Player:step"})
            # The fireflies where they were, their behaviours at their new
            # offsets, waiting as before; the score and the timer carried
            # over; the new global at its initial value.
            for e, xy in positions.items():
                self.assertEqual(link.entity(e)["properties"][:2], xy)
            shift = {n: ns - s for n, s, _, ns in svlink.unchanged_functions(old, new)}
            for c in link.contexts():
                was = contexts[c["context"]]
                name = old.maps.function(was["pc"])["name"]
                self.assertEqual((c["state"], c["wait"], c["self"], c["event"]),
                                 (was["state"], was["wait"], was["self"], was["event"]))
                self.assertEqual(c["pc"], was["pc"] + shift[name])
                self.assertEqual(new.maps.function(c["pc"])["name"], name)
            self.assertTrue(any(v != 0 for v in shift.values()))  # the code after Player:step moved
            names = {e["name"]: e["number"] for e in new.maps.svdbg["globals"]}
            after = link.globals(0, len(names))
            for entry in old.maps.svdbg["globals"]:
                self.assertEqual(after[names[entry["name"]]], globals_before[entry["number"]], entry["name"])
            self.assertEqual(after[names["speed"]], 768)  # 3.0
            self.assertEqual(after[names["score"]], 7)
            self.assertEqual(link.agent_info()["blob_hash"], svlink.fnv1a(new.blob))
            # Nothing on the screen changed with the reload: the same frame.
            shot("3-reloaded")
            self.assertEqual(pictures["2-reloading"], pictures["3-reloaded"])
            if not ARGS.rom:
                with open(os.path.join(shots, "svlink-reload-2-reloading.png"), "rb") as a, \
                        open(os.path.join(shots, "svlink-reload-3-reloaded.png"), "rb") as b:
                    self.assertEqual(a.read(), b.read())
            # The new speed shows: the serval walks 3 pixels a frame (less
            # at an edge); the fireflies fly on.
            after_speed = self.serval_steps(link, player, 8)
            self.assertTrue(set(after_speed) <= set(range(769)) and 768 in after_speed, after_speed)
            self.assertNotIn(384, after_speed)
            moved = [link.entity(e)["properties"][:2] != xy for e, xy in positions.items()
                     if any(x["entity"] == e for x in link.entities())]
            self.assertTrue(any(moved))
            shot("4-after")
            # The timer goes on: the Room's thread, kept, counts the seconds.
            time_left = after[names["time"]]
            self.run_until(link, lambda l: l.globals(names["time"], 1)[0] < time_left, 60)
            # A breakpoint by the new build's maps: the edited line.
            pcs = new.maps.line_sites(f"fireflies.lua:{self.line_of(new, 'vx = speed')}")
            link.break_set(svlink.SITE_BREAKPOINT, pcs)
            stop = link.go(60)
            self.assertEqual((stop["reason"], stop["pc"]), (3, pcs[0]))
            self.assertEqual(new.maps.function(stop["pc"])["name"], "Player:step")
            link.break_clear(svlink.SITE_BREAKPOINT)
            link.resume(0)

    @staticmethod
    def line_of(build, text):
        """The line of a build's script that holds text."""
        with open(build.maps.svdbg["sources"][0], encoding="utf-8") as f:
            for number, line in enumerate(f, 1):
                if text in line:
                    return number
        raise ValueError(text)

    def test_release_page(self):
        if ARGS.rom:
            self.release_rom()
            return
        # No agent in the page: none of the link's script, no export.
        with open(ARGS.release_page, "rb") as f:
            page = f.read()
        for marker in (b"servalLinkRing", b"serval_link_attach", b"/serval-link"):
            self.assertFalse(marker in page, f"the release page holds {marker.decode()}")
        # Opened with the fragment, it runs its frames and never dials. The
        # debug page, opened the same way, does: the check can fail.
        for path, dials in ((ARGS.release_page, False), (ARGS.page, True)):
            server = svlink.Server(os.path.dirname(os.path.abspath(path)))
            try:
                dom = svlink.dump_page(server.url(path, f"serval-link={server.token}&frames=120"),
                                       budget_ms=60000, timeout=300)
                self.assertIn("done", dom, path)
                self.assertEqual(bool(server.attempts), dials, path)
            finally:
                server.close()


    def release_rom(self):
        """A release ROM in the fork: the endpoint connects and says HELLO, the
        agent never rings, and agent messages are answered NO_AGENT; the
        endpoint's own messages work. The debug ROM, the same way, does ring:
        the check can fail."""
        if not ARGS.release_rom:
            self.skipTest("no --release-rom")
        for path, rings in ((ARGS.release_rom, False), (ARGS.rom, True)):
            with svlink.open_rom(path, emu=ARGS.emu, attach=False, timeout=5) \
                    if rings else self._no_agent(path) as link:
                if not rings:
                    with self.assertRaises(svlink.LinkError) as refused:
                        link.agent_info()
                    self.assertEqual(refused.exception.code, 3)  # NO_AGENT
                    self.assertEqual(link.read_mem(0x080000AC, 4), b"SVFF")
                    self.assertEqual(link.count(svlink.AGENT_FOUND), 0)
                else:
                    self.assertEqual(link.count(svlink.AGENT_FOUND), 1)

    @staticmethod
    @contextlib.contextmanager
    def _no_agent(path):
        """The fork running a game that never rings: the session after 3 s."""
        server = svlink.ForkServer()
        program = svlink.find_emulator(ARGS.emu)
        process = subprocess.Popen([program, "--headless", "--link", f"127.0.0.1:{server.port}",
                                    "--link-token", server.token, path], stdout=subprocess.DEVNULL)
        try:
            session = server.accept(30)
            time.sleep(3)
            yield session
        finally:
            server.close()
            process.terminate()
            process.wait(10)


def main():
    global ARGS
    parser = argparse.ArgumentParser()
    parser.add_argument("--page")
    parser.add_argument("--rom", help="the debug ROM, run in serval-emu (the mGBA fork) instead of a page")
    parser.add_argument("--release-rom", help="with --rom: the release ROM, which has no agent")
    parser.add_argument("--emu", help="serval-emu (default: SERVAL_EMU, then PATH)")
    parser.add_argument("--release-page")
    parser.add_argument("--script-c")
    parser.add_argument("--script-h")
    parser.add_argument("--svmap")
    parser.add_argument("--version")
    parser.add_argument("--reload", help="a rebuilt fireflies script's .svmap (its _script.c beside it)")
    parser.add_argument("--shots", help="where test_hot_reload saves the frames before and after "
                        "(default: a temporary directory)")
    ARGS, rest = parser.parse_known_args()
    if ARGS.rom:
        if not svlink.find_emulator(ARGS.emu):
            print("svlink_test: no serval-emu: skipped (set SERVAL_EMU)")
            sys.exit(77)
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(EndToEnd)
    elif ARGS.page:
        try:
            svlink.find_browser()
        except SystemExit:
            print("svlink_test: no Chrome or Chromium: skipped (set SERVAL_CHROME)")
            sys.exit(77)
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(EndToEnd)
    else:
        suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(c)
                                   for c in (Codec, WebSocket, ServerChecks))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
