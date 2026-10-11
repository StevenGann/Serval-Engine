// The web page's end of the debug link (docs/debug-link.md: protocol 1.0,
// mailbox v1). cmake/ServalWeb.cmake adds this script (--pre-js) only to pages
// built with the debug link's agent (debug builds, unless SERVAL_LINK is
// OFF): a release page has none of it.
//
// Active only when the page's URL fragment holds serval-link=TOKEN (beside
// the test mode's frames=...; src/web/shell.html). Then, before the game
// starts, it dials ws://<the host and port that served the page>/serval-link,
// sends HELLO with the token, and calls the module's serval_link_attach(),
// which starts the agent (src/web/link_web.c). From then on it carries frames
// between the WebSocket and the agent's mailbox in the module's memory: the
// debugger's for the agent (types 0x40-0xFF) go into the inbox while it is
// empty, and at every ring (Module.servalLinkRing, the doorbell) the outbox's
// frames go to the debugger. It answers its own messages: READ_MEM and
// WRITE_MEM of the hardware the page keeps at its GBA addresses (the I/O
// registers, palette RAM, VRAM, OAM; anything else is NO_SUCH),
// MACHINE_PAUSE and MACHINE_RESUME (the page stops at its next VBlank wait),
// and sends LOG for debug_log() and the engine's warnings (src/web/
// platform.c), which still go to the console too. A parked agent waits in
// Module.servalLinkWait(), with Asyncify unwinding the game to the browser.
//
// The debugger checks the Host and Origin headers and the token
// (docs/debug-link.md#security-of-the-web-transport). Both HELLOs have flags 0
// and seq 0.
(function servalLink() {
  "use strict";
  const params = new URLSearchParams(location.hash.slice(1));
  const token = params.get("serval-link");
  if (token === null) return;

  const PROTO_MAJOR = 1, PROTO_MINOR = 0;
  const KIND_WEB = 2;
  const CAPABILITIES = 1 | 2 | 4; // raw memory, machine pause, logs
  const HELLO = 0x01, READ_MEM = 0x02, WRITE_MEM = 0x03, MACHINE_PAUSE = 0x04, MACHINE_RESUME = 0x05;
  const LOG = 0x20, GAME_RESET = 0x21, AGENT_FOUND = 0x22, AGENT_TYPES = 0x40;
  const UNSUPPORTED = 1, BAD_REQUEST = 2, NO_AGENT = 3, NO_SUCH = 8;
  const REPLY = 1, ERROR = 2;
  const FRAME_MAX = 1024, READ_MAX = 1016;
  // Mailbox v1: the header's fields, the inbox and the outbox.
  const MAGIC = 0x4B4C5653, ENDPOINT = 0x454C5653; // the bytes SVLK and SVLE
  const M_VERSION = 4, M_SIZE = 6, M_ENDPOINT = 8, M_IN_LEN = 12, M_OUT_LEN = 14;
  const M_IN = 20, M_OUT = 1044, IN_SIZE = 1024, MAILBOX_SIZE = 3092;
  // What READ_MEM and WRITE_MEM reach: [start, size].
  const HARDWARE = [[0x04000000, 0x400], [0x05000000, 0x400], [0x06000000, 0x18000], [0x07000000, 0x400]];
  const LOGS_KEPT = 256; // LOG frames kept from before the handshake

  const link = {
    socket: null,
    connected: false, // the debugger answered HELLO
    mailbox: 0,       // its address, once the agent rang
    found: false,     // AGENT_FOUND sent for this agent
    pending: [],      // the debugger's frames for the agent, waiting for the inbox
    logs: [],         // LOG frames from before the handshake
    paused: false,    // MACHINE_PAUSE
    unpause: null,    // resolves the page's wait at its VBlank
    wake: null,       // resolves a parked agent's wait
    frame: 0,         // frames shown (LOG's frame)
    version: "",      // the engine's (serval_link_attach)
  };

  const heap = () => Module.HEAPU8;
  const get16 = (b, at) => b[at] | b[at + 1] << 8;
  const get32 = (b, at) => (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
  function set16(b, at, v) { b[at] = v & 0xFF; b[at + 1] = v >> 8 & 0xFF; }
  function set32(b, at, v) { set16(b, at, v & 0xFFFF); set16(b, at + 2, v >>> 16); }
  function bytes(...parts) {
    const out = [];
    for (const part of parts) for (const b of part) out.push(b);
    return out;
  }
  const u8 = v => [v & 0xFF];
  const u16 = v => [v & 0xFF, v >> 8 & 0xFF];
  const u32 = v => [v & 0xFF, v >> 8 & 0xFF, v >> 16 & 0xFF, v >>> 24];
  function str(text) {
    text = String(text).slice(0, 255);
    const out = [text.length];
    for (let i = 0; i < text.length; i++) out.push(text.charCodeAt(i) & 0x7F);
    return out;
  }
  function cstring(address) {
    const h = heap();
    let text = "";
    for (let i = address; h[i]; i++) text += String.fromCharCode(h[i]);
    return text;
  }
  function frame(type, flags, seq, payload) {
    const f = new Uint8Array(6 + payload.length);
    set16(f, 0, 4 + payload.length);
    f[2] = type;
    f[3] = flags;
    set16(f, 4, seq);
    f.set(payload, 6);
    return f;
  }
  function send(f) {
    if (link.socket && link.socket.readyState === WebSocket.OPEN) link.socket.send(f);
  }
  const answer = (request, payload) => send(frame(request[2], REPLY, get16(request, 4), payload || []));
  const refuse = (request, code, text) => send(frame(request[2], REPLY | ERROR, get16(request, 4), bytes(u8(code), str(text))));

  // --- The mailbox ----------------------------------------------------------

  // Writes SVLE while the debugger is connected, and tells it about the
  // agent: AGENT_FOUND at the agent's first ring, GAME_RESET first if it
  // started again (it zeroes the mailbox when it starts, SVLE included).
  function announce() {
    const m = link.mailbox, h = heap();
    if (!m || !link.connected) return;
    if (get32(h, m + M_ENDPOINT) !== ENDPOINT) {
      if (link.found) send(frame(GAME_RESET, 0, 0, []));
      link.found = false;
      set32(h, m + M_ENDPOINT, ENDPOINT);
    }
    if (!link.found) {
      link.found = true;
      send(frame(AGENT_FOUND, 0, 0, []));
    }
  }

  // Moves the debugger's frames into the inbox while it is empty.
  function deliver() {
    const m = link.mailbox, h = heap();
    if (!m || link.paused || !link.pending.length || get16(h, m + M_IN_LEN)) return;
    let n = 0;
    while (link.pending.length && n + link.pending[0].length <= IN_SIZE) {
      const f = link.pending.shift();
      h.set(f, m + M_IN + n);
      n += f.length;
    }
    set16(h, m + M_IN_LEN, n);
    wake();
  }

  function wake() {
    const resolve = link.wake;
    link.wake = null;
    if (resolve) resolve();
  }

  // The doorbell: the agent rings with its mailbox's address after writing
  // to it. Sends the outbox's frames and empties it, then fills the inbox.
  Module.servalLinkRing = address => {
    const h = heap();
    if (address !== link.mailbox) {
      if (get32(h, address) !== MAGIC || get16(h, address + M_VERSION) !== 1 || get16(h, address + M_SIZE) < MAILBOX_SIZE) {
        console.warn("serval: debug link: the agent's mailbox isn't mailbox v1; not connecting to it");
        return;
      }
      link.mailbox = address;
    }
    const m = link.mailbox;
    announce();
    const length = get16(h, m + M_OUT_LEN);
    for (let at = 0; at + 2 <= length;) {
      const n = get16(h, m + M_OUT + at);
      if (n < 4 || at + 2 + n > length) break;
      if (link.connected) send(h.slice(m + M_OUT + at, m + M_OUT + at + 2 + n));
      at += 2 + n;
    }
    set16(h, m + M_OUT_LEN, 0);
    deliver();
  };

  // A parked agent's wait: until frames reach the inbox, the debugger leaves,
  // or a second passes (the agent rings again, which costs nothing).
  Module.servalLinkWait = () => new Promise(resolve => {
    link.wake = resolve;
    setTimeout(wake, 1000);
    deliver();
  });

  // LOG: level, source, the frame, the text.
  Module.servalLinkLog = (level, source, address) => {
    const f = frame(LOG, 0, 0, bytes(u8(level), u8(source), u32(link.frame), str(cstring(address))));
    if (link.connected) return send(f);
    link.logs.push(f);
    if (link.logs.length > LOGS_KEPT) link.logs.shift();
  };

  // --- The debugger's frames ----------------------------------------------------

  function hardware(address, length) {
    return HARDWARE.some(([start, size]) => address >= start && address + length <= start + size);
  }

  function endpointRequest(f, type) {
    const p = f.subarray(6);
    switch (type) {
      case READ_MEM: {
        if (p.length < 6) return refuse(f, BAD_REQUEST, "the payload is too short");
        const address = get32(p, 0), length = get16(p, 4);
        if (length < 1 || length > READ_MAX) return refuse(f, BAD_REQUEST, "READ_MEM reads 1 to 1,016 bytes");
        if (!hardware(address, length)) return refuse(f, NO_SUCH, "the web page reads only the I/O registers, palette RAM, VRAM and OAM");
        return answer(f, heap().subarray(address, address + length));
      }
      case WRITE_MEM: {
        if (p.length < 5) return refuse(f, BAD_REQUEST, "the payload is too short");
        const address = get32(p, 0), data = p.subarray(4);
        if (!hardware(address, data.length)) return refuse(f, NO_SUCH, "the web page writes only the I/O registers, palette RAM, VRAM and OAM");
        heap().set(data, address);
        return answer(f);
      }
      case MACHINE_PAUSE:
        link.paused = true;
        return answer(f);
      case MACHINE_RESUME: {
        link.paused = false;
        const resolve = link.unpause;
        link.unpause = null;
        if (resolve) resolve();
        answer(f);
        return deliver();
      }
      default:
        return refuse(f, UNSUPPORTED, "the web page doesn't support the request");
    }
  }

  function receive(f) {
    if (f.length < 6 || f.length > FRAME_MAX || get16(f, 0) !== f.length - 2) {
      console.warn("serval: debug link: a malformed frame from the debugger, ignored");
      return;
    }
    const type = f[2], flags = f[3], seq = get16(f, 4);
    if (!link.connected) {
      // The debugger answers HELLO first, in this major, or closes.
      if (type !== HELLO || f.length < 9 || f[6] !== PROTO_MAJOR) {
        console.warn("serval: debug link: the debugger's first frame isn't HELLO of protocol 1; closing");
        link.socket.close();
        return;
      }
      link.connected = true;
      announce();
      for (const log of link.logs) send(log);
      link.logs = [];
      return;
    }
    if (flags & REPLY) return; // the page asks the debugger nothing
    if (type < AGENT_TYPES) {
      if (seq === 0 || type === HELLO) return; // events from the debugger: none in 1.0
      return endpointRequest(f, type);
    }
    if (!link.mailbox) {
      if (seq) refuse(f, NO_AGENT, "no agent has rung: the game hasn't started, or was built without the agent");
      return;
    }
    link.pending.push(f.slice());
    deliver();
  }

  function connect() {
    const socket = new WebSocket("ws://" + location.host + "/serval-link");
    socket.binaryType = "arraybuffer";
    socket.onopen = () => {
      send(frame(HELLO, 0, 0, bytes(u8(PROTO_MAJOR), u8(PROTO_MINOR), u8(KIND_WEB), str(link.version), str(token), u32(CAPABILITIES))));
    };
    socket.onmessage = e => {
      if (e.data instanceof ArrayBuffer) receive(new Uint8Array(e.data));
    };
    socket.onclose = () => {
      // The debugger left: no SVLE (the agent detaches, and resumes the game
      // if it is parked), no machine pause, nothing waiting for the agent.
      link.connected = false;
      link.found = false;
      link.pending = [];
      if (link.mailbox) set32(heap(), link.mailbox + M_ENDPOINT, 0);
      link.paused = false;
      const resolve = link.unpause;
      link.unpause = null;
      if (resolve) resolve();
      wake();
    };
    link.socket = socket;
  }

  // The page's VBlank wait (src/web/shell.html's Module.servalPresent): count
  // the frame, and stop there while the machine is paused.
  const present = Module.servalPresent;
  Module.servalPresent = async function () {
    link.frame++;
    await present.apply(this, arguments);
    while (link.paused) await new Promise(resolve => { link.unpause = resolve; });
    deliver();
  };

  // Before the game starts: dial the debugger and start the agent.
  const initialized = Module.onRuntimeInitialized;
  Module.onRuntimeInitialized = function () {
    if (typeof Module._serval_link_attach === "function") {
      link.version = cstring(Module._serval_link_attach());
      connect();
    }
    return initialized ? initialized.apply(this, arguments) : undefined;
  };
})();
