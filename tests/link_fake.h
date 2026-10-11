#ifndef SERVAL_TESTS_LINK_FAKE_H
#define SERVAL_TESTS_LINK_FAKE_H

// The debug link's fake endpoint, for the agent's host tests (link_tests.c,
// the core; link_vm_tests.c, the VM's part): the tests play the endpoint
// (src/host/link_host.c rings and waits through it) and the debugger behind
// it. Each test file that includes this has an endpoint of its own. Host
// only, in debug builds with the agent.

#include "../src/core/link_internal.h"
#include "../src/core/vm_internal.h"
#include "../src/host/link_host.h"
#include "serval/ecs.h"
#include "serval/vm.h"
#include "test.h"

#ifdef SERVAL_LINK_AGENT

#define QUEUE_MAX 8192
#define GOT_MAX 65536

static struct {
    u8 queue[QUEUE_MAX]; // the debugger's frames, waiting for the inbox
    u32 queued;
    u8 got[GOT_MAX]; // every frame the agent sent, in order
    u32 got_n;
    bool stuck;             // the endpoint leaves the outbox full at rings
    u32 rings;              // rings seen
    u32 parked_rings;       // ... saying "parked at a frame boundary"
    u32 running_rings;      // ... saying "running"
    u32 inside_rings;       // ... saying "parked inside a frame"
    u32 waits;              // waits of a parked agent
    u32 max_waits;          // after this many (0: 100), the debugger leaves
    u32 max_out;            // the most out_len was at a ring
    void (*on_wait)(u32 n); // the debugger's move while the agent waits (n from 1)
} fake;

static u16 seq;

static inline u32 rd16(const u8* p) {
    return p[0] | (u32)p[1] << 8;
}

static inline u32 rd32(const u8* p) {
    return rd16(p) | rd16(p + 2) << 16;
}

static inline void wr16(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static inline void wr32(u8* p, u32 v) {
    wr16(p, v);
    wr16(p + 2, v >> 16);
}

static inline ServalLinkMailbox* mailbox(void) {
    return &serval_link_mailbox;
}

// Writes the queued frames that fit into the inbox, while it is empty.
static inline void deliver(void) {
    ServalLinkMailbox* m = mailbox();
    if (m->in_len || !fake.queued)
        return;
    u32 n = 0;
    while (n < fake.queued) {
        u32 size = 2 + rd16(fake.queue + n);
        if (n + size > LINK_IN_SIZE)
            break;
        n += size;
    }
    for (u32 i = 0; i < n; i++)
        m->in[i] = fake.queue[i];
    for (u32 i = n; i < fake.queued; i++)
        fake.queue[i - n] = fake.queue[i];
    fake.queued -= n;
    m->in_len = (u16)n;
}

static inline void fake_ring(ServalLinkMailbox* m) {
    fake.rings++;
    if (m->state == LINK_STATE_BOUNDARY)
        fake.parked_rings++;
    else if (m->state == LINK_STATE_RUNNING)
        fake.running_rings++;
    else if (m->state == LINK_STATE_INSIDE)
        fake.inside_rings++;
    if (m->out_len > fake.max_out)
        fake.max_out = m->out_len;
    if (!fake.stuck) {
        for (u32 i = 0; i < m->out_len && fake.got_n < GOT_MAX; i++)
            fake.got[fake.got_n++] = m->out[i];
        m->out_len = 0;
    }
    deliver();
}

// A parked agent waits: the debugger's next move. With none scripted, or
// after too many, the debugger leaves (so a broken agent can't hang the test).
static inline void fake_wait(void) {
    fake.waits++;
    if (fake.on_wait && fake.waits < (fake.max_waits ? fake.max_waits : 100))
        fake.on_wait(fake.waits);
    else
        mailbox()->endpoint = 0;
}

// Starts the agent as serval_init() does, with an endpoint that answers (or
// not), and connects the debugger (SVLE).
static inline void start(bool present) {
    serval_host_link.present = false; // the last test's agent goes first: it may be
    serval_link_init();               // attached, even parked mid-step
    vm_unload();
    vm_bind(NULL);
    ecs_reset();
    fake = (typeof(fake)){0};
    fake.got_n = 0;
    seq = 0;
    serval_host_link = (ServalHostLink){.present = present,
                                        .ring = fake_ring,
                                        .wait = fake_wait,
                                        .title = "LINK TESTS",
                                        .game_code = "SVLT",
                                        .stats = {.frame = 100, .cycles = 12345}};
    serval_link_init();
    if (present)
        mailbox()->endpoint = LINK_ENDPOINT_MAGIC;
}

// The game's frame: the endpoint has written the inbox meanwhile, and
// frame_end() polls.
static inline void frame(void) {
    deliver();
    serval_host_link.stats.frame++;
    serval_link_poll();
    CHECK(serval_vm_bookkeeping_ok()); // the agent's writes leave the scheduler's state alone
}

// Queues a request; returns its seq.
static inline u16 request_flags(u32 type, u32 flags, const u8* payload, u32 n) {
    u16 s = ++seq;
    u8* f = fake.queue + fake.queued;
    wr16(f, 4 + n);
    f[2] = (u8)type;
    f[3] = (u8)flags;
    wr16(f + 4, s);
    for (u32 i = 0; i < n; i++)
        f[6 + i] = payload[i];
    fake.queued += 6 + n;
    return s;
}

static inline u16 request(u32 type, const u8* payload, u32 n) {
    return request_flags(type, 0, payload, n);
}

// The nth frame the agent sent (from 0), or NULL.
static inline const u8* sent(u32 nth) {
    for (u32 at = 0; at + 2 <= fake.got_n; at += 2 + rd16(fake.got + at)) {
        if (nth-- == 0)
            return fake.got + at;
    }
    return NULL;
}

// The reply to seq s, or NULL.
static inline const u8* reply(u16 s) {
    const u8* f;
    for (u32 k = 0; (f = sent(k)); k++) {
        if ((f[3] & LINK_FLAG_REPLY) && rd16(f + 4) == s)
            return f;
    }
    return NULL;
}

static inline u32 replies(u16 s) {
    u32 count = 0;
    const u8* f;
    for (u32 k = 0; (f = sent(k)); k++)
        count += (f[3] & LINK_FLAG_REPLY) && rd16(f + 4) == s;
    return count;
}

// A reply without error and with this much payload.
static inline bool ok(u16 s, u32 payload) {
    const u8* f = reply(s);
    return f && f[3] == LINK_FLAG_REPLY && rd16(f) == 4 + payload;
}

// An error reply with this code.
static inline bool error(u16 s, u32 code) {
    const u8* f = reply(s);
    return f && f[3] == (LINK_FLAG_REPLY | LINK_FLAG_ERROR) && f[6] == code && rd16(f) >= 6 &&
           rd16(f) == 6u + f[7];
}

// The nth event of a type (from 0), or NULL.
static inline const u8* event(u32 type, u32 nth) {
    const u8* f;
    for (u32 k = 0; (f = sent(k)); k++) {
        if (f[2] == type && f[3] == 0 && rd16(f + 4) == 0 && nth-- == 0)
            return f;
    }
    return NULL;
}

static inline u32 events(u32 type) {
    u32 count = 0;
    while (event(type, count))
        count++;
    return count;
}

static inline bool text_is(const u8* str, const char* text) {
    u32 n = str[0];
    for (u32 i = 0; i < n; i++) {
        if (!text[i] || text[i] != (char)str[1 + i])
            return false;
    }
    return text[n] == '\0';
}

static inline u16 attach(void) {
    u16 s = request(LINK_AGENT_INFO, NULL, 0);
    frame();
    return s;
}

#endif // SERVAL_LINK_AGENT

#endif // SERVAL_TESTS_LINK_FAKE_H
