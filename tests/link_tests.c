// The debug link's agent (src/core/link.c, vm_link.c; docs/debug-link.md:
// protocol 1.0, mailbox v1) against a fake endpoint: the tests play the
// endpoint (src/host/link_host.c rings and waits through them) and the
// debugger behind it. Host only: the GBA's endpoint is the debugger's mGBA
// fork, and the test ROM checks that the agent stays dormant without one
// (tests/rom/link_tests.c).

#include "../src/core/link_internal.h"
#include "../src/core/vm_internal.h"
#include "../src/host/link_host.h"
#include "link_probe_script.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "serval/vm.h"
#include "test.h"

#ifdef SERVAL_LINK_AGENT

// --- The fake endpoint -----------------------------------------------------------

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
    u32 waits;              // waits of a parked agent
    u32 max_out;            // the most out_len was at a ring
    void (*on_wait)(u32 n); // the debugger's move while the agent waits (n from 1)
} fake;

static u16 seq;

static u32 rd16(const u8* p) {
    return p[0] | (u32)p[1] << 8;
}

static u32 rd32(const u8* p) {
    return rd16(p) | rd16(p + 2) << 16;
}

static void wr16(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void wr32(u8* p, u32 v) {
    wr16(p, v);
    wr16(p + 2, v >> 16);
}

static ServalLinkMailbox* mailbox(void) {
    return &serval_link_mailbox;
}

// Writes the queued frames that fit into the inbox, while it is empty.
static void deliver(void) {
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

static void fake_ring(ServalLinkMailbox* m) {
    fake.rings++;
    if (m->state == LINK_STATE_BOUNDARY)
        fake.parked_rings++;
    else if (m->state == LINK_STATE_RUNNING)
        fake.running_rings++;
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
static void fake_wait(void) {
    fake.waits++;
    if (fake.on_wait && fake.waits < 100)
        fake.on_wait(fake.waits);
    else
        mailbox()->endpoint = 0;
}

// Starts the agent as serval_init() does, with an endpoint that answers (or
// not), and connects the debugger (SVLE).
static void start(bool present) {
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
static void frame(void) {
    deliver();
    serval_host_link.stats.frame++;
    serval_link_poll();
    CHECK(serval_vm_bookkeeping_ok()); // the agent's writes leave the scheduler's state alone
}

// Queues a request; returns its seq.
static u16 request_flags(u32 type, u32 flags, const u8* payload, u32 n) {
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

static u16 request(u32 type, const u8* payload, u32 n) {
    return request_flags(type, 0, payload, n);
}

// The nth frame the agent sent (from 0), or NULL.
static const u8* sent(u32 nth) {
    for (u32 at = 0; at + 2 <= fake.got_n; at += 2 + rd16(fake.got + at)) {
        if (nth-- == 0)
            return fake.got + at;
    }
    return NULL;
}

// The reply to seq s, or NULL.
static const u8* reply(u16 s) {
    const u8* f;
    for (u32 k = 0; (f = sent(k)); k++) {
        if ((f[3] & LINK_FLAG_REPLY) && rd16(f + 4) == s)
            return f;
    }
    return NULL;
}

static u32 replies(u16 s) {
    u32 count = 0;
    const u8* f;
    for (u32 k = 0; (f = sent(k)); k++)
        count += (f[3] & LINK_FLAG_REPLY) && rd16(f + 4) == s;
    return count;
}

// A reply without error and with this much payload.
static bool ok(u16 s, u32 payload) {
    const u8* f = reply(s);
    return f && f[3] == LINK_FLAG_REPLY && rd16(f) == 4 + payload;
}

// An error reply with this code.
static bool error(u16 s, u32 code) {
    const u8* f = reply(s);
    return f && f[3] == (LINK_FLAG_REPLY | LINK_FLAG_ERROR) && f[6] == code && rd16(f) >= 6 &&
           rd16(f) == 6u + f[7];
}

// The nth event of a type (from 0), or NULL.
static const u8* event(u32 type, u32 nth) {
    const u8* f;
    for (u32 k = 0; (f = sent(k)); k++) {
        if (f[2] == type && f[3] == 0 && rd16(f + 4) == 0 && nth-- == 0)
            return f;
    }
    return NULL;
}

static u32 events(u32 type) {
    u32 count = 0;
    while (event(type, count))
        count++;
    return count;
}

static bool text_is(const u8* str, const char* text) {
    u32 n = str[0];
    for (u32 i = 0; i < n; i++) {
        if (!text[i] || text[i] != (char)str[1 + i])
            return false;
    }
    return text[n] == '\0';
}

static u16 attach(void) {
    u16 s = request(LINK_AGENT_INFO, NULL, 0);
    frame();
    return s;
}

// --- The handshake --------------------------------------------------------------

// Without an endpoint (hardware, stock mGBA), serval_init() leaves the agent
// dormant: no hook, no ring, nothing written to the mailbox.
static void dormant_without_an_endpoint(void) {
    for (u32 i = 0; i < sizeof(ServalLinkMailbox); i++)
        ((u8*)mailbox())[i] = 0;
    start(false);
    CHECK(serval_link_hook == NULL);
    CHECK(!serval_link_started());
    CHECK(fake.rings == 0);
    CHECK(mailbox()->magic == 0 && mailbox()->version == 0 && mailbox()->size == 0);
}

// With one, it writes the mailbox's header and rings: the endpoint learns
// where the mailbox is.
static void starts_and_rings_with_an_endpoint(void) {
    start(true);
    CHECK(serval_link_hook == serval_link_poll);
    CHECK(serval_link_started() && !serval_link_attached());
    CHECK(fake.rings == 1 && fake.running_rings == 1);
    const u8* m = (const u8*)mailbox();
    CHECK(m[0] == 'S' && m[1] == 'V' && m[2] == 'L' && m[3] == 'K');
    CHECK(rd16(m + 4) == 1 && rd16(m + 6) == 3092);
    CHECK(mailbox()->in_len == 0 && mailbox()->out_len == 0 && mailbox()->state == 0);
    // Started again (serval_init() again), it zeroes the mailbox, the
    // endpoint's SVLE too: the endpoint knows it started again.
    serval_link_init();
    CHECK(mailbox()->endpoint == 0 && fake.rings == 2);
}

// AGENT_INFO's reply attaches the agent: the protocol version, the engine's,
// the capabilities, the limits, the ROM's title and game code, and the
// loaded blob's size and FNV-1a hash.
static void agent_info_attaches(void) {
    start(true);
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    u16 s = attach();
    const u8* f = reply(s);
    CHECK(f && f[2] == LINK_AGENT_INFO && f[3] == LINK_FLAG_REPLY);
    CHECK(serval_link_attached());
    if (!f)
        return;
    const u8* p = f + 6;
    CHECK(p[0] == 1 && p[1] == 0);
    CHECK(text_is(p + 2, SERVAL_LINK_ENGINE_VERSION));
    p += 3 + p[2];
    CHECK(rd32(p) == (LINK_CAP_VM | LINK_CAP_STATS | LINK_CAP_ENTITIES));
    CHECK(p[4] == VM_CONTEXTS && p[5] == VM_STACK && p[6] == VM_CALLS);
    CHECK(rd16(p + 7) == VM_GLOBALS && p[9] == VM_FIELDS && rd16(p + 10) == VM_ARRAY_CELLS);
    CHECK(rd16(p + 12) == MAX_ENT && p[14] == VM_P_COUNT && rd16(p + 15) == 0);
    CHECK(rd32(p + 17) == SERVAL_LINK_SCRATCH);
    p += 21;
    CHECK(text_is(p, "LINK TESTS"));
    p += 1 + p[0];
    CHECK(text_is(p, "SVLT"));
    p += 1 + p[0];
    CHECK(rd32(p) == link_probe_script_size);
    CHECK(rd32(p + 4) == serval_link_fnv1a(link_probe_script, link_probe_script_size));
    CHECK(p + 8 == f + 2 + rd16(f)); // nothing after
    // FNV-1a 32's own test vectors.
    CHECK(serval_link_fnv1a((const u8*)"", 0) == 0x811C9DC5u);
    CHECK(serval_link_fnv1a((const u8*)"a", 1) == 0xE40C292Cu);
    CHECK(serval_link_fnv1a((const u8*)"foobar", 6) == 0xBF9CF968u);
    // No blob: 0, 0.
    vm_unload();
    frame();
    s = attach();
    f = reply(s);
    CHECK(f && rd32(f + 2 + rd16(f) - 8) == 0 && rd32(f + 2 + rd16(f) - 4) == 0);
}

// Before AGENT_INFO, the agent answers the requests it knows with BAD_REQUEST
// (it may not park the game until attached) and the others with UNSUPPORTED.
static void requests_before_attaching(void) {
    start(true);
    u16 pause = request(LINK_PAUSE, NULL, 0);
    u16 globals = request(LINK_GLOBALS, (const u8[]){0, 0, 1, 0}, 4);
    u16 step = request(LINK_STEP, (const u8[]){0, 0}, 2);
    frame();
    CHECK(error(pause, LINK_BAD_REQUEST) && error(globals, LINK_BAD_REQUEST));
    CHECK(error(step, LINK_UNSUPPORTED));
    CHECK(!serval_link_parked());
}

// --- Pause, resume, a frame step ---------------------------------------------------

static u16 resume_seq;
static u32 resume_mode;
static void resume_on_first_wait(u32 n) {
    if (n == 1)
        resume_seq = request(LINK_RESUME, (const u8[]){(u8)resume_mode}, 1);
}

// PAUSE parks the game at this frame boundary (STOPPED, pause, the frame
// number) until RESUME: the agent rings saying parked, waits, mutes, and
// resumes on RESUME. RESUME 1 runs one frame and parks at the next boundary
// (STOPPED, frame, the frame number one more).
static void pause_and_step_a_frame(void) {
    start(true);
    attach();
    u16 pause = request(LINK_PAUSE, NULL, 0);
    resume_mode = 1;
    fake.on_wait = resume_on_first_wait;
    u32 paused_at = serval_host_link.stats.frame + 1;
    frame(); // parks, then RESUME 1
    CHECK(ok(pause, 0));
    const u8* stop = event(LINK_STOPPED, 0);
    CHECK(stop && rd16(stop) == 4 + 11);
    CHECK(stop && stop[6] == LINK_STOP_PAUSE && stop[7] == 0xFF && rd32(stop + 8) == 0);
    CHECK(stop && rd32(stop + 12) == paused_at && stop[16] == 0);
    CHECK(ok(resume_seq, 0));
    CHECK(fake.parked_rings >= 1 && fake.waits == 1);
    CHECK(!serval_host_link.muted && !serval_link_parked());
    CHECK(mailbox()->state == LINK_STATE_RUNNING);
    // The reply to PAUSE comes before STOPPED.
    const u8* first = sent(1); // sent(0) is AGENT_INFO's reply
    CHECK(first && first[2] == LINK_PAUSE && (first[3] & LINK_FLAG_REPLY));
    // The next frame boundary: STOPPED, frame, one frame later; then RESUME 0
    // runs on.
    resume_mode = 0;
    fake.waits = 0;
    frame();
    stop = event(LINK_STOPPED, 1);
    CHECK(stop && stop[6] == LINK_STOP_FRAME && rd32(stop + 12) == paused_at + 1);
    CHECK(events(LINK_STOPPED) == 2 && !serval_link_parked());
    frame();
    CHECK(events(LINK_STOPPED) == 2);
}

static u16 second_pause;
static void pause_then_resume(u32 n) {
    if (n == 1)
        second_pause = request(LINK_PAUSE, NULL, 0);
    if (n == 2)
        resume_seq = request(LINK_RESUME, (const u8[]){0}, 1);
}

static void pause_while_parked_sends_nothing_more(void) {
    start(true);
    attach();
    request(LINK_PAUSE, NULL, 0);
    fake.on_wait = pause_then_resume;
    frame();
    CHECK(ok(second_pause, 0) && ok(resume_seq, 0));
    CHECK(events(LINK_STOPPED) == 1 && fake.waits == 2);
    // A bad mode: BAD_REQUEST; the game isn't resumed by it.
    u16 bad = request(LINK_RESUME, (const u8[]){2}, 1);
    frame();
    CHECK(error(bad, LINK_BAD_REQUEST));
}

// The debugger leaving (the endpoint's SVLE gone) detaches the agent: a
// parked game goes on, stats stop, and requests need AGENT_INFO again.
static void leave_while_parked(u32 n) {
    (void)n;
    mailbox()->endpoint = 0;
}

static void detaching_resumes_the_game(void) {
    start(true);
    attach();
    request(LINK_STATS_SUBSCRIBE, (const u8[]){1, 0}, 2);
    frame(); // subscribed
    frame(); // a STATS
    u32 stats = events(LINK_STATS);
    CHECK(stats >= 1);
    request(LINK_PAUSE, NULL, 0);
    fake.on_wait = leave_while_parked;
    frame();
    CHECK(!serval_link_parked() && !serval_link_attached() && !serval_host_link.muted);
    stats = events(LINK_STATS);
    frame();
    frame();
    CHECK(events(LINK_STATS) == stats);
    mailbox()->endpoint = LINK_ENDPOINT_MAGIC; // a debugger again
    u16 s = request(LINK_ENTITIES, (const u8[]){0}, 1);
    frame();
    CHECK(error(s, LINK_BAD_REQUEST));
}

// --- Globals and arrays -----------------------------------------------------------

static void globals_read_and_write(void) {
    start(true);
    attach();
    u16 none = request(LINK_GLOBALS, (const u8[]){0, 0, 1, 0}, 4);
    frame();
    CHECK(error(none, LINK_NO_VM)); // no blob loaded
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    vm_set_global(3, -5);
    vm_set_global(255, 0x12345678);
    u16 read = request(LINK_GLOBALS, (const u8[]){2, 0, 3, 0}, 4);
    u16 end = request(LINK_GLOBALS, (const u8[]){254, 0, 10, 0}, 4); // cut at 256
    u16 most = request(LINK_GLOBALS, (const u8[]){0, 0, 253, 0}, 4);
    u16 many = request(LINK_GLOBALS, (const u8[]){0, 0, 254, 0}, 4);
    u16 past = request(LINK_GLOBALS, (const u8[]){0, 1, 1, 0}, 4);
    u16 shrt = request(LINK_GLOBALS, (const u8[]){0, 0, 1}, 3);
    frame();
    CHECK(ok(read, 4 + 12));
    const u8* f = reply(read);
    CHECK(f && rd16(f + 6) == 2 && rd16(f + 8) == 3 && (s32)rd32(f + 14) == -5);
    CHECK(ok(end, 4 + 8));
    f = reply(end);
    CHECK(f && rd16(f + 8) == 2 && rd32(f + 14) == 0x12345678);
    CHECK(ok(most, 4 + 253 * 4)); // the largest: a 1,022-byte frame
    CHECK(error(many, LINK_BAD_REQUEST) && error(past, LINK_NO_SUCH));
    CHECK(error(shrt, LINK_BAD_REQUEST));
    u8 set[6];
    wr16(set, 7);
    wr32(set + 2, (u32)-99);
    u16 s = request(LINK_SET_GLOBAL, set, 6);
    wr16(set, 256);
    u16 bad = request(LINK_SET_GLOBAL, set, 6);
    frame();
    CHECK(ok(s, 0) && vm_global(7) == -99 && error(bad, LINK_NO_SUCH));
}

static void array_cells_read_and_write(void) {
    start(true);
    attach();
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    u8 set[6];
    wr16(set, 1023);
    wr32(set + 2, 77);
    u16 s = request(LINK_SET_ARRAY_CELL, set, 6);
    wr16(set, 1024);
    u16 bad = request(LINK_SET_ARRAY_CELL, set, 6);
    u16 read = request(LINK_ARRAY_CELLS, (const u8[]){0xFE, 3, 5, 0}, 4); // 1022: cut to 2
    u16 past = request(LINK_ARRAY_CELLS, (const u8[]){0, 4, 1, 0}, 4);    // 1024
    frame();
    CHECK(ok(s, 0) && serval_vm_array_cell(1023) == 77 && error(bad, LINK_NO_SUCH));
    const u8* f = reply(read);
    CHECK(ok(read, 4 + 8) && f && rd16(f + 6) == 1022 && rd16(f + 8) == 2 && rd32(f + 14) == 77);
    CHECK(error(past, LINK_NO_SUCH));
}

// --- Entities ----------------------------------------------------------------------

// The entity with every component the properties need, and a Probe watching
// it (attached: object, context, fields).
static Entity target, probe;

static void make_entities(void) {
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    target = entity_create(C_POS | C_VEL | C_SPR | C_BODY | C_ANIM | C_GAME(2));
    probe = entity_create(C_POS);
    vm_attach(probe, PROBE_OBJ_PROBE);
    vm_set_global(PROBE_G_TARGET, target);
    vm_step(); // the Probe's create (it waits) and step
}

static void entities_lists_the_live_ones(void) {
    start(true);
    attach();
    make_entities();
    Entity dead = entity_create(C_POS);
    entity_destroy(dead);
    u16 s = request(LINK_ENTITIES, (const u8[]){0}, 1);
    u16 from = request(LINK_ENTITIES, (const u8[]){(u8)(entity_index(probe))}, 1);
    u16 end = request(LINK_ENTITIES, (const u8[]){MAX_ENT}, 1);
    u16 bad = request(LINK_ENTITIES, (const u8[]){MAX_ENT + 1}, 1);
    frame();
    CHECK(ok(s, 1 + 2 * 9));
    const u8* f = reply(s);
    if (f) {
        const u8* p = f + 6;
        CHECK(p[0] == MAX_ENT);
        CHECK(rd16(p + 1) == target && rd32(p + 3) == ent_mask[entity_index(target)]);
        CHECK(rd16(p + 7) == 0xFFFF && p[9] == 0xFF); // not attached: no object, no context
        CHECK(rd16(p + 10) == probe && rd32(p + 12) == (C_POS | C_ALIVE));
        CHECK(rd16(p + 16) == PROBE_OBJ_PROBE && p[18] != 0xFF && p[18] < VM_CONTEXTS);
    }
    CHECK(ok(from, 1 + 9) && ok(end, 1) && reply(end)[6] == MAX_ENT);
    CHECK(error(bad, LINK_BAD_REQUEST));
    // Every slot taken: the first reply stops where the frame is full, and
    // its next is where the second starts.
    vm_unload();
    ecs_reset();
    while (entity_create(C_POS) != ENTITY_NONE) {
    }
    u16 a = request(LINK_ENTITIES, (const u8[]){0}, 1);
    frame();
    f = reply(a);
    u32 next = f ? f[6] : 0;
    CHECK(f && next == (LINK_PAYLOAD_MAX - 1) / 9 && rd16(f) == 4 + 1 + next * 9);
    u16 b = request(LINK_ENTITIES, (const u8[]){(u8)next}, 1);
    frame();
    f = reply(b);
    CHECK(f && f[6] == MAX_ENT && rd16(f) == 4 + 1 + (MAX_ENT - next) * 9);
}

// What the Probe read with GETP last frame, property by property.
static const u16 probe_reads[VM_P_COUNT - 1] = {
    PROBE_G_READ_X,
    PROBE_G_READ_Y,
    PROBE_G_READ_VX,
    PROBE_G_READ_VY,
    PROBE_G_READ_SPRITE,
    PROBE_G_READ_FRAME,
    PROBE_G_READ_FLAGS,
    PROBE_G_READ_ANGLE,
    PROBE_G_READ_DEPTH,
    PROBE_G_READ_SCALE,
    PROBE_G_READ_BODY_W,
    PROBE_G_READ_BODY_H,
    PROBE_G_READ_TAGS,
    PROBE_G_READ_ANIM_TIME,
    PROBE_G_READ_ANIM_STEP,
    PROBE_G_READ_BODY_BOUNCE,
    PROBE_G_READ_BODY_FRICTION,
    PROBE_G_READ_BODY_MAX_FALL,
    PROBE_G_READ_BODY_GRAVITY,
    PROBE_G_READ_BODY_CONTACT,
};

// ENTITY's properties as GETP reads them: ENTITY against the Probe's GETPs.
static void entity_reads_properties_as_getp(void) {
    start(true);
    attach();
    make_entities();
    u32 i = entity_index(target);
    pos_x[i] = -FX(7);
    pos_y[i] = 3;
    vel_x[i] = 1;
    vel_y[i] = -1;
    spr_id[i] = 0xFFFF;
    spr_frame[i] = 200;
    spr_flags[i] = 0x8001;
    spr_angle[i] = 0xFFFF;
    spr_depth[i] = -2;
    spr_scale[i] = -32768;
    body_w[i] = 200;
    body_h[i] = 7;
    ent_mask[i] |= C_GAME(1) | C_GAME(13);
    spr_anim_time[i] = 250;
    spr_anim_step[i] = 3;
    body_bounce[i] = 255;
    body_friction[i] = 7;
    body_max_fall[i] = 0xFFFF;
    body_gravity[i] = -128;
    body_contact[i] = BODY_SIDE_BOTTOM;
    vm_step();
    u16 s = request(LINK_ENTITY, (const u8[]){(u8)target, (u8)(target >> 8)}, 2);
    u16 p = request(LINK_ENTITY, (const u8[]){(u8)probe, (u8)(probe >> 8)}, 2);
    frame();
    CHECK(ok(s, 2 + 2 + 1 + VM_P_COUNT * 4 + 1));
    const u8* f = reply(s);
    if (f) {
        CHECK(rd16(f + 6) == target && rd16(f + 8) == 0xFFFF && f[10] == VM_P_COUNT);
        u32 wrong = 0;
        for (u32 k = 0; k < VM_P_COUNT - 1; k++)
            wrong += (s32)rd32(f + 11 + k * 4) != vm_global(probe_reads[k]);
        CHECK(wrong == 0);
        CHECK((s32)rd32(f + 11 + VM_P_OBJECT * 4) == -1);
        CHECK(f[11 + VM_P_COUNT * 4] == 0); // no fields
    }
    // The Probe: attached, its object, its two fields (hp, mana: the first
    // two slots).
    CHECK(ok(p, 2 + 2 + 1 + VM_P_COUNT * 4 + 1 + VM_FIELDS * 4));
    f = reply(p);
    if (f) {
        CHECK(rd16(f + 8) == PROBE_OBJ_PROBE);
        CHECK((s32)rd32(f + 11 + VM_P_OBJECT * 4) == PROBE_OBJ_PROBE);
        const u8* fields = f + 11 + VM_P_COUNT * 4;
        CHECK(fields[0] == VM_FIELDS);
        CHECK((s32)rd32(fields + 1) == 7 && (s32)rd32(fields + 5) == -3);
    }
    // A dead entity, ENTITY_NONE: NO_SUCH.
    u16 none = request(LINK_ENTITY, (const u8[]){0, 0}, 2);
    entity_destroy(target);
    u16 dead = request(LINK_ENTITY, (const u8[]){(u8)target, (u8)(target >> 8)}, 2);
    frame();
    CHECK(error(none, LINK_NO_SUCH) && error(dead, LINK_NO_SUCH));
}

// SET_PROPERTY: SETP's rules. Each value is written, then read back by the
// Probe's GETP: truncated to the pool's type as SETP truncates
// (tests/vm_tests.c's properties_read_and_write_the_ecs, the same rows).
static const struct {
    s32 written, read;
} set_rows[VM_P_COUNT - 2] = {
    [VM_P_X] = {-FX(12) - 5, -FX(12) - 5},
    [VM_P_Y] = {INT32_MAX, INT32_MAX},
    [VM_P_VX] = {-FX(1) / 2, -FX(1) / 2},
    [VM_P_VY] = {FX(3), FX(3)},
    [VM_P_SPR] = {0x12345, 0x2345},
    [VM_P_FRAME] = {0x1FF, 0xFF},
    [VM_P_FLAGS] = {-1, 0xFFFF},
    [VM_P_ANGLE] = {0x18000, 0x8000},
    [VM_P_DEPTH] = {0x18000, -32768},
    [VM_P_SCALE] = {0xFFFF, -1},
    [VM_P_BODY_W] = {0x1FE, 0xFE},
    [VM_P_BODY_H] = {-1, 0xFF},
    [VM_P_TAGS] = {-1, 0x7FFF},
    [VM_P_ANIM_TIME] = {0x1FF, 0xFF},
    [VM_P_ANIM_STEP] = {-2, 0xFE},
    [VM_P_BODY_BOUNCE] = {0x1FF, 0xFF},
    [VM_P_BODY_FRICTION] = {-1, 0xFF},
    [VM_P_BODY_MAX_FALL] = {-FX(1), 0xFF00},
    [VM_P_BODY_GRAVITY] = {0x180, -128},
};

static u16 set_property(Entity e, u32 prop, s32 value) {
    u8 p[7] = {(u8)e, (u8)(e >> 8), (u8)prop};
    wr32(p + 3, (u32)value);
    return request(LINK_SET_PROPERTY, p, 7);
}

static void set_property_follows_setp(void) {
    start(true);
    attach();
    make_entities();
    u32 i = entity_index(target);
    u32 mask = ent_mask[i];
    u16 seqs[VM_P_COUNT - 2];
    for (u32 k = 0; k < VM_P_COUNT - 2; k++)
        seqs[k] = set_property(target, k, set_rows[k].written);
    u16 contact = set_property(target, VM_P_BODY_CONTACT, 1);
    u16 object = set_property(target, VM_P_OBJECT, 1);
    u16 hp = set_property(probe, VM_P_FIELD(0), -1234);
    u16 unattached = set_property(target, VM_P_FIELD(0), 1);
    u16 field_past = set_property(probe, VM_P_FIELD0 + VM_FIELDS, 1);
    u16 unknown = set_property(target, VM_P_COUNT, 1);
    u16 shrt = request(LINK_SET_PROPERTY, (const u8[]){(u8)target, (u8)(target >> 8), 0, 1}, 4);
    u32 warnings = debug_warning_count();
    frame();
    u32 bad = 0;
    for (u32 k = 0; k < VM_P_COUNT - 2; k++)
        bad += !ok(seqs[k], 0);
    CHECK(bad == 0);
    CHECK(error(contact, LINK_READ_ONLY) && error(object, LINK_READ_ONLY));
    CHECK(ok(hp, 0) && serval_vm_field(probe, 0) == -1234);
    CHECK(error(unattached, LINK_NO_SUCH) && error(field_past, LINK_NO_SUCH));
    CHECK(error(unknown, LINK_NO_SUCH) && error(shrt, LINK_BAD_REQUEST));
    CHECK(debug_warning_count() == warnings); // no warnings: the agent's writes aren't SETP's
    // Tags: only C_GAME(0) to C_GAME(14); C_ALIVE and the engine's kept.
    CHECK(ent_mask[i] == ((mask & ~(0x7FFFu << 16)) | 0x7FFFu << 16));
    vm_step(); // the Probe reads them back
    u32 wrong = 0;
    for (u32 k = 0; k < VM_P_COUNT - 2; k++)
        wrong += vm_global(probe_reads[k]) != set_rows[k].read;
    CHECK(wrong == 0);
    CHECK(body_contact[i] == 0);
}

// A C game: entities without the VM's state (no blob loaded here; the host
// links the VM, so its object is -1 and it has no fields).
static void entities_without_scripts(void) {
    start(true);
    attach();
    Entity e = entity_create(C_POS | C_SPR);
    pos_x[entity_index(e)] = FX(5);
    u16 s = request(LINK_ENTITY, (const u8[]){(u8)e, (u8)(e >> 8)}, 2);
    u16 set = set_property(e, VM_P_Y, FX(9));
    frame();
    const u8* f = reply(s);
    CHECK(ok(s, 2 + 2 + 1 + VM_P_COUNT * 4 + 1) && f && (s32)rd32(f + 11) == FX(5));
    CHECK(ok(set, 0) && pos_y[entity_index(e)] == FX(9));
}

// --- Stats and LOADED ----------------------------------------------------------------

static void stats_every_n_frames(void) {
    start(true);
    attach();
    make_entities();
    serval_host_link.stats.cycles = 4321;
    serval_host_link.stats.drawn = 9;
    serval_host_link.stats.matrices = 2;
    serval_host_link.stats.dropped = 300;
    serval_host_link.stats.mixer_cycles = 777;
    request(LINK_STATS_SUBSCRIBE, (const u8[]){3, 0}, 2);
    frame(); // subscribed here
    CHECK(events(LINK_STATS) == 0);
    frame();
    frame();
    CHECK(events(LINK_STATS) == 0);
    frame();
    CHECK(events(LINK_STATS) == 1);
    const u8* f = event(LINK_STATS, 0);
    CHECK(f && rd16(f) == 4 + 32);
    if (f) {
        const u8* p = f + 6;
        CHECK(rd32(p) == serval_host_link.stats.frame && rd32(p + 4) == 4321);
        CHECK(rd32(p + 8) == 0); // vm_cycles: with the VM's part (E6)
        CHECK(rd32(p + 12) == vm_ops_this_frame());
        CHECK(p[16] == 1); // the Probe's create waits
        CHECK(p[19] == 2); // two entities
        CHECK(p[20] == 9 && p[21] == 2 && rd16(p + 22) == 300 && rd32(p + 24) == 777);
        CHECK(rd32(p + 28) == debug_warning_count());
    }
    for (u32 k = 0; k < 6; k++)
        frame();
    CHECK(events(LINK_STATS) == 3);
    request(LINK_STATS_SUBSCRIBE, (const u8[]){0, 0}, 2);
    for (u32 k = 0; k < 6; k++)
        frame();
    CHECK(events(LINK_STATS) == 3);
}

// LOADED: the game loads, reloads or unloads a blob while attached.
static void loaded_while_attached(void) {
    start(true);
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    frame();
    attach();
    frame();
    CHECK(events(LINK_LOADED) == 0);
    vm_unload();
    frame();
    const u8* f = event(LINK_LOADED, 0);
    CHECK(f && rd16(f) == 4 + 8 && rd32(f + 6) == 0 && rd32(f + 10) == 0);
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    frame();
    f = event(LINK_LOADED, 1);
    CHECK(f && rd32(f + 6) == link_probe_script_size &&
          rd32(f + 10) == serval_link_fnv1a(link_probe_script, link_probe_script_size));
    frame();
    CHECK(events(LINK_LOADED) == 2);
}

// OPTIONS: accepted (stop on fault comes with the VM's part).
static void options_accepted(void) {
    start(true);
    attach();
    u16 s = request(LINK_OPTIONS, (const u8[]){1, 0, 0, 0}, 4);
    u16 shrt = request(LINK_OPTIONS, (const u8[]){1}, 1);
    frame();
    CHECK(ok(s, 0) && error(shrt, LINK_BAD_REQUEST));
}

// --- Framing, versions, a full mailbox ------------------------------------------------

// The inbox written by hand: a raw frame or several.
static void inbox(const u8* bytes, u32 n) {
    for (u32 i = 0; i < n; i++)
        mailbox()->in[i] = bytes[i];
    mailbox()->in_len = (u16)n;
}

static void malformed_frames(void) {
    start(true);
    attach();
    u32 before = fake.got_n;
    // A length under 4: nothing can be read, nothing answered, the inbox emptied.
    inbox((const u8[]){2, 0, LINK_AGENT_INFO, 0}, 4);
    serval_link_poll();
    CHECK(fake.got_n == before && mailbox()->in_len == 0);
    // A frame running past in_len: dropped.
    inbox((const u8[]){20, 0, LINK_AGENT_INFO, 0, 9, 0}, 6);
    serval_link_poll();
    CHECK(fake.got_n == before && mailbox()->in_len == 0);
    // An in_len past the inbox: dropped.
    mailbox()->in_len = LINK_IN_SIZE + 2;
    serval_link_poll();
    CHECK(fake.got_n == before && mailbox()->in_len == 0);
    // A good frame, then a bad one: the good one answered.
    inbox((const u8[]){4, 0, LINK_AGENT_INFO, 0, 9, 0, 1, 0}, 8);
    serval_link_poll();
    CHECK(replies(9) == 1);
    // A reply or an event from the debugger: ignored.
    u32 n = fake.got_n;
    request_flags(LINK_AGENT_INFO, LINK_FLAG_REPLY, NULL, 0);
    u8* f = fake.queue + fake.queued;
    wr16(f, 4);
    f[2] = LINK_PAUSE;
    f[3] = 0;
    wr16(f + 4, 0); // seq 0: an event
    fake.queued += 6;
    frame();
    CHECK(fake.got_n == n && !serval_link_parked());
    // Unknown types, the endpoint's types: UNSUPPORTED.
    u16 unknown = request(0x7F, NULL, 0);
    u16 endpoints = request(LINK_READ_MEM, (const u8[]){0, 0, 0, 4, 4, 0}, 6);
    u16 reload = request(LINK_RELOAD_BEGIN, NULL, 0);
    u16 contexts = request(LINK_CONTEXTS, NULL, 0);
    frame();
    CHECK(error(unknown, LINK_UNSUPPORTED) && error(endpoints, LINK_UNSUPPORTED));
    CHECK(error(reload, LINK_UNSUPPORTED) && error(contexts, LINK_UNSUPPORTED));
}

// A later minor's additions: trailing bytes are ignored, as are flag bits
// 1.0 doesn't define.
static void versions_trailing_fields_and_flags(void) {
    start(true);
    u16 info = request(LINK_AGENT_INFO, (const u8[]){1, 2, 3}, 3);
    u16 resume = request(LINK_RESUME, (const u8[]){0, 9, 9, 9}, 4);
    u16 flagged = request_flags(LINK_ENTITIES, 0xFC, (const u8[]){0}, 1);
    frame();
    CHECK(reply(info) && reply(info)[3] == LINK_FLAG_REPLY);
    CHECK(ok(resume, 0) && ok(flagged, 1));
}

// Every request gets exactly one reply, in order, even when the replies
// overflow the outbox: the agent rings to make room (saying "running", since
// it is still reading its inbox) and never writes past it.
static void full_inbox_and_full_replies(void) {
    start(true);
    attach();
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    // Ten GLOBALS of 253 cells: 10 KB of replies from a 100-byte inbox.
    u16 first = 0;
    for (u32 k = 0; k < 10; k++) {
        u16 s = request(LINK_GLOBALS, (const u8[]){0, 0, 253, 0}, 4);
        if (!k)
            first = s;
    }
    // ... and OPTIONS filling the rest of the inbox exactly (1,024 bytes), the
    // last with trailing bytes.
    static const u8 zeros[16];
    u32 fill = 0;
    for (; LINK_IN_SIZE - fake.queued >= 20; fill++)
        request(LINK_OPTIONS, zeros, 4);
    request(LINK_OPTIONS, zeros, LINK_IN_SIZE - fake.queued - 6);
    fill++;
    CHECK(fake.queued == LINK_IN_SIZE);
    u32 rings = fake.rings;
    frame();
    u32 missing = 0;
    for (u16 s = first; s < first + 10 + fill; s++)
        missing += replies(s) != 1;
    CHECK(missing == 0);
    CHECK(fake.rings - rings >= 5 && fake.max_out <= LINK_OUT_SIZE);
    CHECK(fake.parked_rings == 0);
}

// An endpoint that doesn't empty the outbox: the agent drops what doesn't
// fit, never writing past it, and goes on once it is emptied.
static void stuck_outbox_drops_frames(void) {
    start(true);
    attach();
    CHECK(vm_load(link_probe_script, link_probe_script_size));
    fake.stuck = true;
    for (u32 k = 0; k < 4; k++)
        request(LINK_GLOBALS, (const u8[]){0, 0, 253, 0}, 4);
    u16 last = request(LINK_ENTITIES, (const u8[]){0}, 1);
    frame();
    CHECK(mailbox()->out_len <= LINK_OUT_SIZE && mailbox()->out_len >= LINK_FRAME_MAX);
    // What is in the outbox is whole frames.
    u32 at = 0, frames = 0;
    while (at < mailbox()->out_len) {
        at += 2 + rd16(mailbox()->out + at);
        frames++;
    }
    CHECK(at == mailbox()->out_len && frames >= 1 && frames <= 2);
    fake.stuck = false;
    u16 after = request(LINK_ENTITIES, (const u8[]){0}, 1);
    frame();
    CHECK(reply(after) != NULL && reply(last) == NULL);
}

TEST_SUITE(link_tests, "link", {"dormant without an endpoint", dormant_without_an_endpoint},
           {"starts and rings", starts_and_rings_with_an_endpoint},
           {"AGENT_INFO attaches", agent_info_attaches},
           {"requests before attaching", requests_before_attaching},
           {"pause and a frame step", pause_and_step_a_frame},
           {"PAUSE while parked", pause_while_parked_sends_nothing_more},
           {"detaching resumes", detaching_resumes_the_game}, {"globals", globals_read_and_write},
           {"array cells", array_cells_read_and_write}, {"ENTITIES", entities_lists_the_live_ones},
           {"ENTITY reads as GETP", entity_reads_properties_as_getp},
           {"SET_PROPERTY follows SETP", set_property_follows_setp},
           {"entities without scripts", entities_without_scripts}, {"STATS", stats_every_n_frames},
           {"LOADED", loaded_while_attached}, {"OPTIONS", options_accepted},
           {"malformed frames", malformed_frames}, {"versions", versions_trailing_fields_and_flags},
           {"full inbox, full outbox", full_inbox_and_full_replies},
           {"stuck outbox", stuck_outbox_drops_frames});

#else

static void no_agent(void) {
    CHECK(true);
}

TEST_SUITE(link_tests, "link", {"no agent in this build", no_agent});

#endif // SERVAL_LINK_AGENT
