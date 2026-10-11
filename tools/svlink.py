#!/usr/bin/env python3
"""The debug link's reference client (docs/debug-link.md: protocol 1.0).

A debugger, in Python's standard library, for a game's web page built with
the debug link's agent (a debug build: the web preset): as tools/svm.py is
the blob format's reference, this is the link protocol's from the debugger's
side. It serves the page over HTTP on 127.0.0.1 together with the debug
link's WebSocket at /serval-link (checking the Host and Origin headers and
the token in HELLO, docs/debug-link.md#security-of-the-web-transport), opens
the page in headless Chrome with #serval-link=TOKEN in its URL fragment (in
the page's test mode, so it needs no click to start; tools/web-shots.py
finds the browser), and drives the agent.

Usage: tools/svlink.py [options] PAGE.html [COMMAND...]

Commands, run in order (none: read them from standard input, one a line):
  hello                    the page's HELLO and the agent's AGENT_INFO
  pause                    park the game at the next frame boundary
  resume                   let it run
  step [N]                 run N frames (1), parking after each
  frames N                 let it run N frames, then park
  globals [FIRST [COUNT]]  read globals (0, 16)
  arrays [FIRST [COUNT]]   read the RAM arrays' pool (0, 16)
  entities                 every live entity: its mask, object and context
  entity N                 an entity's properties and instance fields
  set global N VALUE       write a global; set array N VALUE: a pool cell
  set entity N PROP VALUE  write a property (a number, or a name like x)
  stats [EVERY [COUNT]]    print COUNT (1) STATS, one every EVERY frames (60),
                           running the game meanwhile if it is parked
  logs                     the LOG lines received so far
  read ADDRESS LENGTH      read hardware memory (hex dump)
  sleep SECONDS            wait, the game running or not

Options: --no-browser prints the page's URL instead of opening it (open it
in a browser yourself, without test mode: click to start the game);
--port N serves on that port (default: any free one); --frames N stops the
page's test mode after N frames (default: a billion); --token T uses that
token (default: random).

As a library (tools/svlink_test.py uses it):

    with svlink.open_page("build/web/examples/fireflies.html") as link:
        info = link.agent_info()
        stop = link.pause()
        print(link.globals(0, 8))

Values are numbers as the agent sends them: cells as signed 32-bit, fixed
point raw (x = 256 is one pixel).
"""

import argparse
import base64
import contextlib
import hashlib
import http.server
import importlib.util
import os
import queue
import re
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

TOOLS = os.path.dirname(os.path.abspath(__file__))
VM_H = os.path.join(TOOLS, "..", "include", "serval", "vm.h")

PROTO_MAJOR, PROTO_MINOR = 1, 0
KIND_DEBUGGER, KIND_FORK, KIND_WEB, KIND_HARDWARE = 0, 1, 2, 3
KINDS = {0: "debugger", 1: "mGBA fork", 2: "web page", 3: "hardware"}
FRAME_MAX = 1024
REPLY, ERROR = 1, 2

# Message types (docs/debug-link.md#messages-protocol-10).
HELLO, READ_MEM, WRITE_MEM, MACHINE_PAUSE, MACHINE_RESUME = 0x01, 0x02, 0x03, 0x04, 0x05
LOG, GAME_RESET, AGENT_FOUND = 0x20, 0x21, 0x22
AGENT_INFO, PAUSE, RESUME, STEP, BREAK_SET, BREAK_CLEAR = 0x40, 0x41, 0x42, 0x43, 0x44, 0x45
CONTEXTS, FRAMES, GLOBALS, SET_GLOBAL, ARRAY_CELLS, SET_ARRAY_CELL = 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B
ENTITIES, ENTITY, SET_PROPERTY, SET_LOCAL = 0x4C, 0x4D, 0x4E, 0x4F
RELOAD_BEGIN, RELOAD_DATA, RELOAD_PLAN, RELOAD_COMMIT = 0x50, 0x51, 0x52, 0x53
STATS_SUBSCRIBE, OPTIONS = 0x54, 0x55
STOPPED, FAULT, STATS, RELOADED, LOADED = 0x60, 0x61, 0x62, 0x63, 0x64

ERRORS = {1: "UNSUPPORTED", 2: "BAD_REQUEST", 3: "NO_AGENT", 4: "NOT_PARKED", 5: "NOT_AT_BOUNDARY",
          6: "NO_VM", 7: "NO_SCRATCH", 8: "NO_SUCH", 9: "READ_ONLY", 10: "FULL", 11: "RELOADING",
          12: "MISMATCH"}
STOP_REASONS = {0: "pause", 1: "frame", 2: "brk", 3: "breakpoint", 4: "step", 5: "ended", 6: "fault",
                7: "debug_break"}
CAPABILITIES = {0: "vm", 1: "breakpoints", 2: "step", 3: "reload", 4: "stats", 5: "entities",
                6: "stop on fault"}
ENDPOINT_CAPABILITIES = {0: "raw memory", 1: "machine pause", 2: "logs"}
LOG_LEVELS = {0: "info", 1: "warning", 2: "error"}
LOG_SOURCES = {0: "game", 1: "engine", 2: "emulator"}
VM_P_FIELD0 = 64


class LinkError(Exception):
    """An error reply: its request type, code and text."""

    def __init__(self, type_, code, text):
        super().__init__(f"{name_of(type_)}: {ERRORS.get(code, code)} ({text})")
        self.type, self.code, self.text = type_, code, text


class Closed(Exception):
    """The page closed the connection, or it was refused."""


def name_of(type_):
    for name, value in globals().items():
        if value == type_ and name.isupper() and name not in ("REPLY", "ERROR", "PROTO_MAJOR") \
                and not name.startswith(("KIND", "FRAME", "VM_P")):
            return name
    return f"0x{type_:02X}"


def fnv1a(data):
    """FNV-1a over 32 bits (docs/debug-link.md#handshake)."""
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def property_names(path=VM_H):
    """VM_P_* numbers to their Lua-ish names (x, body_w...), read from vm.h."""
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return {}
    body = re.search(r"enum\s*\{([^}]*VM_P_X[^}]*)\}", text)
    names = {}
    lua = {"spr": "sprite"}  # the scripts' names where they differ (docs/lua.md)
    if body:
        for n, m in enumerate(re.finditer(r"^\s*VM_P_([A-Z0-9_]+)", body.group(1), re.M)):
            if m.group(1) != "COUNT":
                names[n] = lua.get(m.group(1).lower(), m.group(1).lower())
    return names


# --- Frames ---------------------------------------------------------------------


def frame(type_, flags, seq, payload=b""):
    """A frame: length, type, flags, seq, payload (docs/debug-link.md#framing)."""
    if 6 + len(payload) > FRAME_MAX:
        raise ValueError(f"a frame of {6 + len(payload)} bytes is over {FRAME_MAX}")
    return struct.pack("<HBBH", 4 + len(payload), type_, flags, seq) + bytes(payload)


def parse(data):
    """(type, flags, seq, payload) of a whole frame, or ValueError."""
    if len(data) < 6 or len(data) > FRAME_MAX:
        raise ValueError(f"a frame of {len(data)} bytes")
    length, type_, flags, seq = struct.unpack_from("<HBBH", data)
    if length != len(data) - 2:
        raise ValueError(f"a frame's length field says {length}, it has {len(data) - 2}")
    return type_, flags, seq, bytes(data[6:])


def pack_str(text):
    data = text.encode("ascii")[:255]
    return bytes([len(data)]) + data


class Reader:
    """Reads a payload's fields; a missing trailing field reads as None."""

    def __init__(self, data):
        self.data, self.at = data, 0

    def _take(self, fmt):
        size = struct.calcsize(fmt)
        if self.at + size > len(self.data):
            raise ValueError("the payload is too short")
        (value,) = struct.unpack_from("<" + fmt, self.data, self.at)
        self.at += size
        return value

    def u8(self):
        return self._take("B")

    def u16(self):
        return self._take("H")

    def u32(self):
        return self._take("I")

    def s16(self):
        return self._take("h")

    def s32(self):
        return self._take("i")

    def str(self):
        n = self.u8()
        if self.at + n > len(self.data):
            raise ValueError("the payload is too short")
        text = self.data[self.at:self.at + n].decode("ascii", "replace")
        self.at += n
        return text

    def rest(self):
        data = self.data[self.at:]
        self.at = len(self.data)
        return data


def hello_payload(kind, version, token, capabilities):
    return (bytes([PROTO_MAJOR, PROTO_MINOR, kind]) + pack_str(version) + pack_str(token)
            + struct.pack("<I", capabilities))


def parse_hello(payload):
    r = Reader(payload)
    return {"proto": (r.u8(), r.u8()), "kind": r.u8(), "version": r.str(), "token": r.str(),
            "capabilities": r.u32()}


def parse_agent_info(payload):
    r = Reader(payload)
    info = {"proto": (r.u8(), r.u8()), "engine": r.str(), "capabilities": r.u32(),
            "contexts": r.u8(), "stack": r.u8(), "calls": r.u8(), "globals": r.u16(),
            "fields": r.u8(), "array_cells": r.u16(), "entities": r.u16(),
            "properties": r.u8(), "sites": r.u16(), "scratch": r.u32(), "title": r.str(),
            "game_code": r.str(), "blob_size": r.u32(), "blob_hash": r.u32()}
    return info


def parse_stopped(payload):
    r = Reader(payload)
    return {"reason": r.u8(), "context": r.u8(), "pc": r.u32(), "frame": r.u32(), "fault": r.u8()}


def parse_stats(payload):
    r = Reader(payload)
    return {"frame": r.u32(), "cycles": r.u32(), "vm_cycles": r.u32(), "ops": r.u32(),
            "contexts": r.u8(), "queue": r.u16(), "entities": r.u8(), "drawn": r.u8(),
            "matrices": r.u8(), "dropped": r.u16(), "mixer_cycles": r.u32(),
            "warnings": r.u32()}


def parse_log(payload):
    r = Reader(payload)
    return {"level": r.u8(), "source": r.u8(), "frame": r.u32(), "text": r.str()}


def parse_loaded(payload):
    r = Reader(payload)
    return {"size": r.u32(), "hash": r.u32()}


EVENT_PARSERS = {STOPPED: parse_stopped, STATS: parse_stats, LOG: parse_log, LOADED: parse_loaded}


# --- WebSocket (RFC 6455), the server's end, as much as a browser needs --------------

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
WS_CONTINUATION, WS_TEXT, WS_BINARY, WS_CLOSE, WS_PING, WS_PONG = 0, 1, 2, 8, 9, 10


def ws_accept(key):
    """Sec-WebSocket-Accept for a Sec-WebSocket-Key."""
    return base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")


def ws_frame(opcode, payload=b""):
    """A server's frame: final, unmasked."""
    n = len(payload)
    if n < 126:
        head = struct.pack(">BB", 0x80 | opcode, n)
    elif n < 65536:
        head = struct.pack(">BBH", 0x80 | opcode, 126, n)
    else:
        head = struct.pack(">BBQ", 0x80 | opcode, 127, n)
    return head + bytes(payload)


class WsClosed(Exception):
    def __init__(self, code=1000):
        super().__init__(f"closed ({code})")
        self.code = code


def _read_exactly(rfile, n):
    data = rfile.read(n)
    if data is None or len(data) < n:
        raise WsClosed(1006)
    return data


def ws_read_frame(rfile):
    """(fin, opcode, payload) of one frame from a client (which masks)."""
    b0, b1 = _read_exactly(rfile, 2)
    if b0 & 0x70:
        raise WsClosed(1002)  # no extension was agreed
    n = b1 & 0x7F
    if n == 126:
        (n,) = struct.unpack(">H", _read_exactly(rfile, 2))
    elif n == 127:
        (n,) = struct.unpack(">Q", _read_exactly(rfile, 8))
    if not b1 & 0x80:
        raise WsClosed(1002)  # a client's frames are masked
    if n > FRAME_MAX and (b0 & 0x0F) < 8:
        raise WsClosed(1009)
    mask = _read_exactly(rfile, 4)
    data = bytearray(_read_exactly(rfile, n))
    for i in range(n):
        data[i] ^= mask[i & 3]
    return bool(b0 & 0x80), b0 & 0x0F, bytes(data)


# --- The server -------------------------------------------------------------------


class Session:
    """The connection to one page: frames to and from the endpoint and its
    agent, matched by seq; events kept in arrival order."""

    def __init__(self, server, hello):
        self.server = server
        self.hello = hello
        self._send_lock = threading.Lock()
        self._seq = 0
        self._pending = {}  # seq -> [threading.Event, (flags, payload)]
        self._events = []  # (type, payload, time) in order
        self._cond = threading.Condition()
        self.closed = False
        self.wfile = None
        self.agent = None  # AGENT_INFO's answer, once asked
        self.stopped = None  # the latest STOPPED while parked, else None

    # Called by the server's thread.
    def _deliver(self, type_, flags, seq, payload):
        if flags & REPLY:
            waiting = self._pending.pop(seq, None)
            if waiting:
                waiting[1] = (flags, payload)
                waiting[0].set()
            return
        if seq == 0:
            with self._cond:
                self._events.append((type_, payload, time.monotonic()))
                if type_ == STOPPED:
                    with contextlib.suppress(ValueError):
                        self.stopped = parse_stopped(payload)
                self._cond.notify_all()

    def _close(self):
        with self._cond:
            self.closed = True
            self._cond.notify_all()
        for waiting in list(self._pending.values()):
            waiting[0].set()

    def send_frame(self, data):
        with self._send_lock:
            if self.closed:
                raise Closed("the page closed the connection")
            self.wfile.write(ws_frame(WS_BINARY, data))
            self.wfile.flush()

    def request(self, type_, payload=b"", timeout=10):
        """Sends a request and returns its reply's payload; LinkError for an
        error reply."""
        self._seq = self._seq % 65535 + 1
        seq = self._seq
        waiting = [threading.Event(), None]
        self._pending[seq] = waiting
        self.send_frame(frame(type_, 0, seq, payload))
        if not waiting[0].wait(timeout):
            self._pending.pop(seq, None)
            raise TimeoutError(f"{name_of(type_)}: no reply in {timeout} s")
        if waiting[1] is None:
            raise Closed("the page closed the connection")
        flags, reply = waiting[1]
        if flags & ERROR:
            r = Reader(reply)
            code = r.u8()
            text = r.str() if r.at < len(reply) else ""
            raise LinkError(type_, code, text)
        return reply

    def events(self, type_=None):
        """The events received so far (of a type), parsed where known."""
        with self._cond:
            found = [(t, p) for t, p, _ in self._events if type_ is None or t == type_]
        return [(t, EVENT_PARSERS[t](p) if t in EVENT_PARSERS else p) for t, p in found] \
            if type_ is None else [EVENT_PARSERS[type_](p) if type_ in EVENT_PARSERS else p
                                   for _, p in found]

    def wait_event(self, type_, timeout=10, after=0, test=None):
        """The first event of a type from the after-th on (in arrival order)
        that passes test, parsed; TimeoutError if none comes."""
        deadline = time.monotonic() + timeout
        with self._cond:
            while True:
                seen = [p for t, p, _ in self._events if t == type_][after:]
                for payload in seen:
                    event = EVENT_PARSERS[type_](payload) if type_ in EVENT_PARSERS else payload
                    if test is None or test(event):
                        return event
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError(f"no {name_of(type_)} in {timeout} s")
                if self.closed:
                    raise Closed("the page closed the connection")
                self._cond.wait(left)

    def count(self, type_):
        with self._cond:
            return sum(1 for t, _, _ in self._events if t == type_)

    # --- The messages ---------------------------------------------------------

    def agent_info(self):
        self.agent = parse_agent_info(self.request(AGENT_INFO))
        return self.agent

    def pause(self, timeout=10):
        """Parks the game at the next frame boundary; returns its STOPPED (the
        one it parked with, if it was parked already: the agent sends no
        other)."""
        before = self.count(STOPPED)
        self.request(PAUSE, timeout=timeout)
        if self.stopped and self.count(STOPPED) == before:
            return self.stopped
        return self.wait_event(STOPPED, timeout, after=before)

    def resume(self, mode=0):
        self.stopped = None
        self.request(RESUME, bytes([mode]))

    def step_frame(self, timeout=10):
        """Runs one frame from a frame boundary; returns the next STOPPED."""
        before = self.count(STOPPED)
        self.resume(1)
        return self.wait_event(STOPPED, timeout, after=before)

    def _cells(self, type_, first, count):
        values = []
        while count > 0:
            r = Reader(self.request(type_, struct.pack("<HH", first, min(count, 253))))
            got_first, got = r.u16(), r.u16()
            values += [r.s32() for _ in range(got)]
            if got == 0 or got_first != first:
                break
            first, count = first + got, count - got
        return values

    def globals(self, first=0, count=16):
        return self._cells(GLOBALS, first, count)

    def set_global(self, index, value):
        self.request(SET_GLOBAL, struct.pack("<Hi", index, value))

    def array_cells(self, first=0, count=16):
        return self._cells(ARRAY_CELLS, first, count)

    def set_array_cell(self, index, value):
        self.request(SET_ARRAY_CELL, struct.pack("<Hi", index, value))

    def entities(self):
        """Every live entity: {entity, mask, object (-1: none), context (None)}."""
        found, first = [], 0
        while first < 256:
            r = Reader(self.request(ENTITIES, bytes([first])))
            nxt = r.u8()
            while r.at < len(r.data):
                e, mask, obj, ctx = r.u16(), r.u32(), r.s16(), r.u8()
                found.append({"entity": e, "mask": mask, "object": obj,
                              "context": None if ctx == 0xFF else ctx})
            if nxt <= first or nxt >= (self.agent or {}).get("entities", 128):
                break
            first = nxt
        return found

    def entity(self, e):
        """{entity, object, properties: [cells], fields: [cells]}."""
        r = Reader(self.request(ENTITY, struct.pack("<H", e)))
        e, obj, count = r.u16(), r.s16(), r.u8()
        props = [r.s32() for _ in range(count)]
        fields = [r.s32() for _ in range(r.u8())]
        return {"entity": e, "object": obj, "properties": props, "fields": fields}

    def set_property(self, e, prop, value):
        self.request(SET_PROPERTY, struct.pack("<HBi", e, prop, value))

    def stats_subscribe(self, every):
        self.request(STATS_SUBSCRIBE, struct.pack("<H", every))

    def options(self, flags):
        self.request(OPTIONS, struct.pack("<I", flags))

    def read_mem(self, address, length):
        data = b""
        while length > 0:
            n = min(length, 1016)
            data += self.request(READ_MEM, struct.pack("<IH", address, n))
            address, length = address + n, length - n
        return data

    def write_mem(self, address, data):
        self.request(WRITE_MEM, struct.pack("<I", address) + bytes(data))

    def machine_pause(self):
        self.request(MACHINE_PAUSE)

    def machine_resume(self):
        self.request(MACHINE_RESUME)

    def logs(self):
        return self.events(LOG)


class Server:
    """Serves a directory over HTTP on 127.0.0.1, and the debug link's
    WebSocket at /serval-link: the Host header must name this server
    (127.0.0.1, localhost or [::1] with its port; against DNS rebinding), the
    upgrade's Origin must be http://127.0.0.1:<port> (the page's own), and the
    endpoint's HELLO must carry the token; anything else is closed. Records
    every upgrade attempt (tests check that a release page makes none)."""

    def __init__(self, directory, token=None, port=0):
        self.directory = os.path.abspath(directory)
        self.token = token or secrets.token_urlsafe(24)  # 32 characters, 192 bits
        self.sessions = queue.Queue()
        self.attempts = []  # (path, headers) of every request for /serval-link
        self.refused = []   # why connections were refused
        server = self

        class Handler(http.server.SimpleHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=server.directory, **kwargs)

            def log_message(self, *args):
                pass

            def do_GET(self):
                if not server._host_ok(self.headers.get("Host", "")):
                    self.send_error(403, "wrong Host")
                    return
                if urllib.parse.urlsplit(self.path).path == "/serval-link":
                    server.attempts.append((self.path, dict(self.headers)))
                    server._websocket(self)
                    return
                super().do_GET()

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        self.httpd.daemon_threads = True
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def _host_ok(self, host):
        return host in (f"127.0.0.1:{self.port}", f"localhost:{self.port}", f"[::1]:{self.port}")

    def url(self, page, fragment=""):
        """The page's URL (page relative to the served directory)."""
        rel = urllib.parse.quote(os.path.relpath(os.path.abspath(page), self.directory).replace(os.sep, "/"))
        return f"http://127.0.0.1:{self.port}/{rel}" + (f"#{fragment}" if fragment else "")

    def _refuse(self, handler, status, why):
        self.refused.append(why)
        handler.send_error(status, why)

    def _websocket(self, handler):
        h = handler.headers
        if h.get("Origin") != f"http://127.0.0.1:{self.port}":
            return self._refuse(handler, 403, f"wrong Origin {h.get('Origin')!r}")
        if h.get("Upgrade", "").lower() != "websocket" or "upgrade" not in h.get("Connection", "").lower() \
                or h.get("Sec-WebSocket-Version") != "13" or not h.get("Sec-WebSocket-Key"):
            return self._refuse(handler, 400, "not a WebSocket upgrade")
        handler.send_response(101, "Switching Protocols")
        handler.send_header("Upgrade", "websocket")
        handler.send_header("Connection", "Upgrade")
        handler.send_header("Sec-WebSocket-Accept", ws_accept(h["Sec-WebSocket-Key"]))
        handler.end_headers()
        handler.wfile.flush()
        handler.close_connection = True
        self._serve_session(handler.rfile, handler.wfile)

    def _close_ws(self, wfile, code):
        with contextlib.suppress(OSError):
            wfile.write(ws_frame(WS_CLOSE, struct.pack(">H", code)))
            wfile.flush()

    def _messages(self, rfile, wfile, lock):
        """Yields each whole binary message; answers pings and closes."""
        message, opcode = b"", None
        while True:
            fin, op, data = ws_read_frame(rfile)
            if op == WS_PING:
                with lock:
                    wfile.write(ws_frame(WS_PONG, data))
                    wfile.flush()
                continue
            if op == WS_PONG:
                continue
            if op == WS_CLOSE:
                code = struct.unpack(">H", data[:2])[0] if len(data) >= 2 else 1005
                raise WsClosed(code)
            if op == WS_TEXT or (op == WS_CONTINUATION and opcode == WS_TEXT):
                raise WsClosed(1003)
            if op != WS_CONTINUATION:
                opcode, message = op, b""
            message += data
            if len(message) > FRAME_MAX:
                raise WsClosed(1009)
            if fin:
                yield message
                message, opcode = b"", None

    def _serve_session(self, rfile, wfile):
        session = None
        lock = threading.Lock()
        try:
            for message in self._messages(rfile, wfile, lock):
                try:
                    type_, flags, seq, payload = parse(message)
                except ValueError as e:
                    if session is None:
                        raise WsClosed(1008) from e
                    continue  # a malformed frame: ignored
                if session is None:
                    # The endpoint's HELLO, first; then the debugger's.
                    if type_ != HELLO:
                        self.refused.append("a frame before HELLO")
                        raise WsClosed(1008)
                    hello = parse_hello(payload)
                    if hello["proto"][0] != PROTO_MAJOR:
                        self.refused.append(f"protocol {hello['proto']}")
                        raise WsClosed(1008)
                    if not secrets.compare_digest(hello["token"].encode(), self.token.encode()):
                        self.refused.append("wrong token")
                        raise WsClosed(1008)
                    session = Session(self, hello)
                    session.wfile = wfile
                    session._send_lock = lock
                    minor = min(PROTO_MINOR, hello["proto"][1])
                    reply = bytes([PROTO_MAJOR, minor, KIND_DEBUGGER]) + pack_str("svlink") + pack_str("") \
                        + struct.pack("<I", 0)
                    session.send_frame(frame(HELLO, 0, 0, reply))
                    self.sessions.put(session)
                    continue
                session._deliver(type_, flags, seq, payload)
        except WsClosed as e:
            if e.code not in (1000, 1001, 1005, 1006):
                self._close_ws(wfile, e.code)
            elif e.code != 1006:
                self._close_ws(wfile, 1000)
        except (OSError, ValueError):
            pass
        finally:
            if session:
                session._close()

    def accept(self, timeout=30):
        """The next page that connected and said HELLO with the token."""
        try:
            return self.sessions.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(f"no page connected in {timeout} s") from None

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


# --- The browser -------------------------------------------------------------------


def _web_shots():
    spec = importlib.util.spec_from_file_location("web_shots", os.path.join(TOOLS, "web-shots.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_browser():
    return _web_shots().find_browser()


def open_browser(url):
    """Headless Chrome on the page, running until terminated."""
    profile = tempfile.mkdtemp(prefix="svlink-chrome-")
    process = subprocess.Popen(
        [find_browser(), "--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
         "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
         f"--user-data-dir={profile}", url],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    process.profile = profile
    return process


def close_browser(process):
    if process is None:
        return
    process.terminate()
    try:
        process.wait(10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    import shutil
    shutil.rmtree(getattr(process, "profile", ""), ignore_errors=True)


def dump_page(url, budget_ms=600000, timeout=600):
    """Runs a page headless with virtual time until it settles (as
    tools/web-shots.py does) and returns the DOM it ends with."""
    result = subprocess.run(
        [find_browser(), "--headless=new", "--no-sandbox", "--disable-gpu",
         f"--virtual-time-budget={budget_ms}", "--dump-dom", url],
        capture_output=True, text=True, timeout=timeout)
    return result.stdout


@contextlib.contextmanager
def open_page(page, frames=10**9, browser=True, port=0, token=None, timeout=60, attach=True):
    """Serves the page, opens it (headless Chrome, test mode) and yields the
    Session once the agent has rung (AGENT_FOUND) and, with attach, answered
    AGENT_INFO. browser=False prints the URL for a person to open."""
    server = Server(os.path.dirname(os.path.abspath(page)), token=token, port=port)
    fragment = f"serval-link={server.token}" + (f"&frames={frames}" if frames else "")
    url = server.url(page, fragment)
    process = None
    session = None
    try:
        if browser:
            process = open_browser(url)
        else:
            print(f"open {url}", file=sys.stderr, flush=True)
        session = server.accept(timeout)
        session.url = url
        session.wait_event(AGENT_FOUND, timeout)
        if attach:
            session.agent_info()
        yield session
    finally:
        if session and not session.closed:
            with contextlib.suppress(Exception):
                session.wfile.write(ws_frame(WS_CLOSE, struct.pack(">H", 1000)))
                session.wfile.flush()
        close_browser(process)
        server.close()


# --- The command line --------------------------------------------------------------


def _number(text):
    return int(text, 0)


def _flags(value, names):
    return ", ".join(name for bit, name in names.items() if value >> bit & 1) or "none"


def _print_stopped(stop):
    ctx = "none" if stop["context"] == 0xFF else stop["context"]
    print(f"stopped: {STOP_REASONS.get(stop['reason'], stop['reason'])} at frame {stop['frame']}"
          f" (context {ctx}, pc 0x{stop['pc']:X})")


def run_command(link, words, props):
    cmd, args = words[0], words[1:]
    if cmd == "hello":
        h = link.hello
        print(f"endpoint: {KINDS.get(h['kind'], h['kind'])} {h['version']}, protocol "
              f"{h['proto'][0]}.{h['proto'][1]}, capabilities: {_flags(h['capabilities'], ENDPOINT_CAPABILITIES)}")
        a = link.agent_info()
        print(f"agent: Serval Engine {a['engine']}, protocol {a['proto'][0]}.{a['proto'][1]}, "
              f"capabilities: {_flags(a['capabilities'], CAPABILITIES)}")
        print(f"  game {a['title']!r} ({a['game_code']}), blob {a['blob_size']} bytes, "
              f"FNV-1a 0x{a['blob_hash']:08X}")
        print(f"  limits: {a['contexts']} contexts, stack {a['stack']}, calls {a['calls']}, "
              f"{a['globals']} globals, {a['fields']} fields, {a['array_cells']} array cells, "
              f"{a['entities']} entities, {a['properties']} properties, {a['sites']} sites, "
              f"scratch {a['scratch']}")
    elif cmd == "pause":
        _print_stopped(link.pause())
    elif cmd == "resume":
        link.resume(0)
        print("running")
    elif cmd == "step":
        for _ in range(_number(args[0]) if args else 1):
            _print_stopped(link.step_frame())
    elif cmd == "frames":
        before = link.count(STOPPED)
        link.resume(0)
        stats_every = _number(args[0])
        link.stats_subscribe(stats_every)
        link.wait_event(STATS, 30, after=link.count(STATS))
        link.stats_subscribe(0)
        link.request(PAUSE)
        _print_stopped(link.wait_event(STOPPED, 10, after=before))
    elif cmd in ("globals", "arrays"):
        first = _number(args[0]) if args else 0
        count = _number(args[1]) if len(args) > 1 else 16
        values = link.globals(first, count) if cmd == "globals" else link.array_cells(first, count)
        for i, v in enumerate(values):
            print(f"  {cmd[:-1] if cmd == 'globals' else 'cell'} {first + i}: {v}")
    elif cmd == "entities":
        for e in link.entities():
            ctx = "-" if e["context"] is None else e["context"]
            print(f"  entity {e['entity']} (slot {e['entity'] & 0xFF}): mask 0x{e['mask']:08X}, "
                  f"object {e['object']}, context {ctx}")
    elif cmd == "entity":
        e = link.entity(_number(args[0]))
        print(f"entity {e['entity']}: object {e['object']}")
        for n, v in enumerate(e["properties"]):
            print(f"  {props.get(n, n)}: {v}")
        if e["fields"]:
            print("  fields: " + " ".join(str(v) for v in e["fields"]))
    elif cmd == "set":
        what = args[0]
        if what == "global":
            link.set_global(_number(args[1]), _number(args[2]))
        elif what == "array":
            link.set_array_cell(_number(args[1]), _number(args[2]))
        elif what == "entity":
            by_name = {v: k for k, v in props.items()}
            prop = by_name[args[2]] if args[2] in by_name else _number(args[2])
            link.set_property(_number(args[1]), prop, _number(args[3]))
        else:
            raise ValueError("set global N VALUE, set array N VALUE or set entity N PROP VALUE")
        print("ok")
    elif cmd == "stats":
        every = _number(args[0]) if args else 60
        count = _number(args[1]) if len(args) > 1 else 1
        after = link.count(STATS)
        parked = link.stopped is not None
        link.stats_subscribe(every)
        if parked:
            link.resume(0)  # stats come from frames that run
        for k in range(count):
            s = link.wait_event(STATS, 10 + every / 30, after=after + k)
            print(f"  frame {s['frame']}: {s['cycles']} cycles, {s['ops']} ops, {s['contexts']} contexts,"
                  f" queue {s['queue']}, {s['entities']} entities, {s['drawn']} drawn,"
                  f" {s['matrices']} matrices, {s['dropped']} dropped, mixer {s['mixer_cycles']},"
                  f" {s['warnings']} warnings")
        link.stats_subscribe(0)
        if parked:
            _print_stopped(link.pause())
    elif cmd == "logs":
        for log in link.logs():
            print(f"  [{log['frame']}] {LOG_SOURCES.get(log['source'], log['source'])} "
                  f"{LOG_LEVELS.get(log['level'], log['level'])}: {log['text']}")
    elif cmd == "read":
        address, length = _number(args[0]), _number(args[1])
        data = link.read_mem(address, length)
        for at in range(0, len(data), 16):
            print(f"  {address + at:08X}: " + " ".join(f"{b:02X}" for b in data[at:at + 16]))
    elif cmd == "sleep":
        time.sleep(float(args[0]))
    else:
        raise ValueError(f"unknown command {cmd!r}")


def main():
    parser = argparse.ArgumentParser(
        description="The debug link's reference client: serves a debug web page and drives its agent.",
        epilog=__doc__.split("Commands,")[1].split("As a library")[0] if "Commands," in __doc__ else None,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("page", help="the page, built with the agent (a debug build: the web preset)")
    parser.add_argument("commands", nargs="*", help="commands, run in order (quote ones with arguments)")
    parser.add_argument("--no-browser", action="store_true", help="print the URL instead of opening it")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--frames", type=int, default=10**9, help="the page's test mode's frames")
    parser.add_argument("--token")
    args = parser.parse_args()
    if not os.path.exists(args.page):
        sys.exit(f"svlink: {args.page}: no such file")
    # Commands given as separate words: a new one starts at each command name.
    names = {"hello", "pause", "resume", "step", "frames", "globals", "arrays", "entities", "entity",
             "set", "stats", "logs", "read", "sleep"}
    commands = []
    for word in args.commands:
        parts = word.split()
        if len(parts) > 1 or not commands or parts[0] in names:
            commands.append(parts)
        else:
            commands[-1] += parts
    props = property_names()
    try:
        with open_page(args.page, frames=0 if args.no_browser else args.frames,
                       browser=not args.no_browser, port=args.port, token=args.token,
                       timeout=600 if args.no_browser else 60) as link:
            interactive = not commands
            if interactive:
                print("svlink: connected; commands as in --help, one a line (EOF ends)", file=sys.stderr)
            source = iter(commands) if commands else (line.split() for line in sys.stdin)
            for words in source:
                if not words:
                    continue
                if not interactive:
                    print("> " + " ".join(words))
                try:
                    run_command(link, words, props)
                except (LinkError, ValueError, IndexError, KeyError, TimeoutError) as e:
                    print(f"svlink: {' '.join(words)}: {e}", file=sys.stderr)
                    if not interactive:
                        sys.exit(1)
    except (TimeoutError, Closed) as e:
        sys.exit(f"svlink: {e}")


if __name__ == "__main__":
    main()
