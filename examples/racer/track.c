// The circuit and the race's rules.
//
// The circuit is a table of segments (a length and a curvature each), the
// way the arcade road racers stored theirs: the road's shape on screen
// comes from it (road.c), and so does the car's drift in curves here.
// Beside it, tables of what stands by the road, of checkpoints and of
// where traffic starts. The whole race is the Race struct; race_step() is
// one frame of it, and tells the game what happened as event bits, so the
// game draws and plays sounds without knowing the rules.
//
// The car's physics are a handful of numbers per frame, in track units and
// half-road widths (track.h):
// - speed: A accelerates, quickly at first and less as the car nears its top
//   speed; B brakes; with neither the car coasts down slowly. Off the road
//   the top speed is a third, and a faster car slows to it.
// - steering moves the car across the road by an amount that grows with
//   speed (none at a standstill: a car turns its wheels, not itself).
// - a curve pushes the car toward its outside in proportion to speed and
//   curvature; at top speed in the sharpest curve, full steering into it
//   only just wins.
// - the car stays within CAR_MAX_X, a quarter of the road's width past
//   either edge.
//
// Collisions are along the track, not on screen: the car hits a roadside
// object when, off the road and level with it sideways, it drives past the
// object's distance; it bumps a vehicle when, level with it sideways and
// faster than it, it would close to less than BUMP_GAP behind it. A vehicle
// that catches up with a slower car never bumps it: it waits BUMP_GAP
// behind until the car moves aside or pulls away.
//
// No division anywhere: speeds, steering and the speedometer are multiplies
// and shifts.

#include "track.h"

// --- Tuning -----------------------------------------------------------------

// Speed per frame, raw FIXED (1/256 track unit a frame, per frame).
#define ACCEL_MIN 3 // plus 1/256 of the speed still to gain
#define BRAKE 24    // from top speed to rest in about a second
#define COAST 3     // from top speed to rest in about seven seconds
#define OFFROAD_DRAG 16

// Steering: 2 + speed * 19 / 4096 a frame, raw lateral FIXED: 8 at top
// speed (a road width in a second), 4 at the off-road top speed.
#define STEER_MIN 2
#define STEER_K 19
// A curve's push: |curve| * speed * 19 >> 20, raw: 6 at top speed in the
// sharpest curve.
#define PUSH_K 19

// Hard steering at speed: steering held above three quarters of top speed
// in a curve at least half the sharpest.
#define SKID_SPEED (CAR_TOP_SPEED * 3 / 4)
#define SKID_CURVE (FX_ONE / 2)

// Collisions. A roadside object is hit within HIT_X sideways. A vehicle is
// bumped within BUMP_X sideways, about the jeep's and the vehicle's half
// widths together, a little forgiving: the middle of the road touches
// both lanes, so passing means keeping to the other lane, and two vehicles
// side by side block the road. The car stops BUMP_GAP behind it.
#define HIT_X (FX_ONE * 3 / 10)
#define BUMP_X (FX_ONE * 5 / 8)

// Traffic the car has left this far behind (out of sight, below the
// screen's bottom) drives on from TRAFFIC_AHEAD in front of it, out of
// sight too (road_project() draws objects up to 960 ahead), plus a random
// part of 1,024 more, and past any vehicle nearer than TRAFFIC_SPACING: so
// the traffic ahead is a loose column, a vehicle every 300 or more. All of
// it drives at one speed (TRAFFIC_SPEED), so vehicles never close on each
// other and the car can always weave through: never two side by side.
#define TRAFFIC_BEHIND FX(32)
#define TRAFFIC_AHEAD FX(1000)
#define TRAFFIC_SPREAD_MASK (FX(1024) - 1)
#define TRAFFIC_SPACING FX(300)

// The speedometer: speed * 221 / 1024 km/h, 290 at top speed.
#define KMH_K 221

// --- The circuit: Savanna ---------------------------------------------------

#define SEG(n) ((n) * SEGMENT_LENGTH) // a distance of n segments
#define LAT(n) (FX_ONE * (n) / 10)    // n tenths of a half-road width
#define SECONDS(n) ((n) * 60)         // frames

static const TrackSegment savanna_segments[] = {
    {45, 0},                                // the start straight
    {20, 1},  {50, 2},  {20, 1},            // a long right
    {30, 0},                                //
    {20, -1}, {35, -3}, {20, -1},           // a firm left
    {45, 0},                                //
    {25, 2},  {35, 4},  {25, 2},            // the sharp right
    {70, 0},                                // the halfway straight: 370-440
    {25, -2}, {45, -4}, {25, -2},           // the sharp left
    {35, 0},                                //
    {25, 1},  {25, -1}, {25, 1},  {25, -1}, // the wiggles
    {45, 0},                                //
    {15, 1},  {35, 3},  {15, 1},            // a quick right
    {25, 0},                                //
    {25, -1}, {45, -2}, {25, -1},           // a long left
    {70, 0},                                // the home straight
};

// Every 7 to 11 segments, on alternate sides, an acacia, a termite mound, a
// rock or a giraffe, and now and then an acacia far out on the other side;
// before each curve of strength 3 or 4, three pairs of signs pointing its
// way. Those within 1.8 of the middle can be hit (the car reaches 1.5).
static const Scenery savanna_scenery[] = {
    {SEG(10), LAT(16), SCENERY_TERMITE_MOUND},   {SEG(21), LAT(-22), SCENERY_ACACIA},
    {SEG(29), LAT(-28), SCENERY_ACACIA},         {SEG(29), LAT(19), SCENERY_ACACIA},
    {SEG(36), LAT(-16), SCENERY_TERMITE_MOUND},  {SEG(47), LAT(13), SCENERY_TERMITE_MOUND},
    {SEG(54), LAT(-18), SCENERY_ACACIA},         {SEG(65), LAT(22), SCENERY_ACACIA},
    {SEG(76), LAT(-14), SCENERY_ROCK},           {SEG(86), LAT(16), SCENERY_ACACIA},
    {SEG(95), LAT(-16), SCENERY_ACACIA},         {SEG(104), LAT(-29), SCENERY_ACACIA},
    {SEG(104), LAT(15), SCENERY_ROCK},           {SEG(111), LAT(-19), SCENERY_ACACIA},
    {SEG(118), LAT(-20), SCENERY_ACACIA},        {SEG(118), LAT(13), SCENERY_ROCK},
    {SEG(128), LAT(-21), SCENERY_ACACIA},        {SEG(135), LAT(-14), SCENERY_SIGN_LEFT},
    {SEG(135), LAT(14), SCENERY_SIGN_LEFT},      {SEG(139), LAT(15), SCENERY_TERMITE_MOUND},
    {SEG(147), LAT(-14), SCENERY_SIGN_LEFT},     {SEG(147), LAT(14), SCENERY_SIGN_LEFT},
    {SEG(159), LAT(-14), SCENERY_SIGN_LEFT},     {SEG(159), LAT(14), SCENERY_SIGN_LEFT},
    {SEG(164), LAT(-25), SCENERY_GIRAFFE},       {SEG(164), LAT(21), SCENERY_ACACIA},
    {SEG(171), LAT(-28), SCENERY_ACACIA},        {SEG(171), LAT(22), SCENERY_ACACIA},
    {SEG(179), LAT(-14), SCENERY_TERMITE_MOUND}, {SEG(187), LAT(-26), SCENERY_ACACIA},
    {SEG(187), LAT(21), SCENERY_ACACIA},         {SEG(195), LAT(-19), SCENERY_ACACIA},
    {SEG(202), LAT(21), SCENERY_ACACIA},         {SEG(213), LAT(-13), SCENERY_TERMITE_MOUND},
    {SEG(213), LAT(22), SCENERY_ACACIA},         {SEG(221), LAT(15), SCENERY_ACACIA},
    {SEG(230), LAT(-16), SCENERY_ROCK},          {SEG(230), LAT(21), SCENERY_ACACIA},
    {SEG(238), LAT(-25), SCENERY_ACACIA},        {SEG(238), LAT(14), SCENERY_TERMITE_MOUND},
    {SEG(247), LAT(-14), SCENERY_TERMITE_MOUND}, {SEG(255), LAT(-14), SCENERY_SIGN_RIGHT},
    {SEG(255), LAT(14), SCENERY_SIGN_RIGHT},     {SEG(267), LAT(-14), SCENERY_SIGN_RIGHT},
    {SEG(267), LAT(14), SCENERY_SIGN_RIGHT},     {SEG(279), LAT(-14), SCENERY_SIGN_RIGHT},
    {SEG(279), LAT(14), SCENERY_SIGN_RIGHT},     {SEG(286), LAT(-17), SCENERY_ACACIA},
    {SEG(294), LAT(14), SCENERY_ROCK},           {SEG(302), LAT(-14), SCENERY_ROCK},
    {SEG(312), LAT(13), SCENERY_ROCK},           {SEG(319), LAT(-19), SCENERY_ACACIA},
    {SEG(319), LAT(26), SCENERY_ACACIA},         {SEG(328), LAT(-22), SCENERY_ACACIA},
    {SEG(328), LAT(22), SCENERY_ACACIA},         {SEG(335), LAT(-22), SCENERY_ACACIA},
    {SEG(346), LAT(-23), SCENERY_ACACIA},        {SEG(346), LAT(15), SCENERY_TERMITE_MOUND},
    {SEG(353), LAT(-21), SCENERY_ACACIA},        {SEG(360), LAT(19), SCENERY_ACACIA},
    {SEG(371), LAT(-25), SCENERY_GIRAFFE},       {SEG(381), LAT(32), SCENERY_GIRAFFE},
    {SEG(389), LAT(-13), SCENERY_TERMITE_MOUND}, {SEG(398), LAT(-30), SCENERY_ACACIA},
    {SEG(398), LAT(13), SCENERY_ROCK},           {SEG(405), LAT(-15), SCENERY_TERMITE_MOUND},
    {SEG(410), LAT(-14), SCENERY_SIGN_LEFT},     {SEG(410), LAT(14), SCENERY_SIGN_LEFT},
    {SEG(422), LAT(-14), SCENERY_SIGN_LEFT},     {SEG(422), LAT(14), SCENERY_SIGN_LEFT},
    {SEG(429), LAT(13), SCENERY_TERMITE_MOUND},  {SEG(434), LAT(-14), SCENERY_SIGN_LEFT},
    {SEG(434), LAT(14), SCENERY_SIGN_LEFT},      {SEG(446), LAT(22), SCENERY_ACACIA},
    {SEG(456), LAT(-24), SCENERY_GIRAFFE},       {SEG(467), LAT(21), SCENERY_ACACIA},
    {SEG(474), LAT(-13), SCENERY_TERMITE_MOUND}, {SEG(474), LAT(21), SCENERY_ACACIA},
    {SEG(483), LAT(15), SCENERY_ROCK},           {SEG(494), LAT(-16), SCENERY_TERMITE_MOUND},
    {SEG(501), LAT(13), SCENERY_TERMITE_MOUND},  {SEG(508), LAT(-14), SCENERY_TERMITE_MOUND},
    {SEG(515), LAT(-20), SCENERY_ACACIA},        {SEG(515), LAT(16), SCENERY_TERMITE_MOUND},
    {SEG(524), LAT(-18), SCENERY_ACACIA},        {SEG(533), LAT(16), SCENERY_TERMITE_MOUND},
    {SEG(541), LAT(-19), SCENERY_ACACIA},        {SEG(541), LAT(26), SCENERY_ACACIA},
    {SEG(549), LAT(-25), SCENERY_ACACIA},        {SEG(549), LAT(16), SCENERY_ROCK},
    {SEG(557), LAT(-22), SCENERY_ACACIA},        {SEG(564), LAT(15), SCENERY_ROCK},
    {SEG(571), LAT(-15), SCENERY_TERMITE_MOUND}, {SEG(581), LAT(22), SCENERY_ACACIA},
    {SEG(592), LAT(-13), SCENERY_TERMITE_MOUND}, {SEG(601), LAT(13), SCENERY_ROCK},
    {SEG(610), LAT(-15), SCENERY_TERMITE_MOUND}, {SEG(621), LAT(17), SCENERY_ACACIA},
    {SEG(629), LAT(-19), SCENERY_ACACIA},        {SEG(629), LAT(22), SCENERY_ACACIA},
    {SEG(636), LAT(25), SCENERY_GIRAFFE},        {SEG(647), LAT(-15), SCENERY_TERMITE_MOUND},
    {SEG(655), LAT(-20), SCENERY_ACACIA},        {SEG(655), LAT(22), SCENERY_ACACIA},
    {SEG(665), LAT(-13), SCENERY_TERMITE_MOUND}, {SEG(665), LAT(24), SCENERY_ACACIA},
    {SEG(674), LAT(14), SCENERY_TERMITE_MOUND},  {SEG(685), LAT(-14), SCENERY_SIGN_RIGHT},
    {SEG(685), LAT(14), SCENERY_SIGN_RIGHT},     {SEG(689), LAT(-27), SCENERY_ACACIA},
    {SEG(689), LAT(21), SCENERY_ACACIA},         {SEG(697), LAT(-14), SCENERY_SIGN_RIGHT},
    {SEG(697), LAT(14), SCENERY_SIGN_RIGHT},     {SEG(704), LAT(20), SCENERY_ACACIA},
    {SEG(709), LAT(-14), SCENERY_SIGN_RIGHT},    {SEG(709), LAT(14), SCENERY_SIGN_RIGHT},
    {SEG(719), LAT(-26), SCENERY_ACACIA},        {SEG(719), LAT(13), SCENERY_ROCK},
    {SEG(726), LAT(-32), SCENERY_GIRAFFE},       {SEG(735), LAT(15), SCENERY_ACACIA},
    {SEG(742), LAT(-20), SCENERY_ACACIA},        {SEG(751), LAT(-21), SCENERY_ACACIA},
    {SEG(751), LAT(13), SCENERY_TERMITE_MOUND},  {SEG(761), LAT(-14), SCENERY_ROCK},
    {SEG(769), LAT(16), SCENERY_TERMITE_MOUND},  {SEG(779), LAT(-13), SCENERY_TERMITE_MOUND},
    {SEG(789), LAT(15), SCENERY_TERMITE_MOUND},  {SEG(800), LAT(-15), SCENERY_TERMITE_MOUND},
    {SEG(810), LAT(27), SCENERY_GIRAFFE},        {SEG(818), LAT(-32), SCENERY_GIRAFFE},
    {SEG(818), LAT(27), SCENERY_ACACIA},         {SEG(826), LAT(16), SCENERY_ACACIA},
    {SEG(837), LAT(-15), SCENERY_ACACIA},        {SEG(847), LAT(13), SCENERY_TERMITE_MOUND},
    {SEG(855), LAT(-22), SCENERY_ACACIA},        {SEG(865), LAT(22), SCENERY_ACACIA},
    {SEG(874), LAT(-22), SCENERY_ACACIA},        {SEG(881), LAT(15), SCENERY_ACACIA},
    {SEG(889), LAT(-13), SCENERY_ROCK},          {SEG(889), LAT(22), SCENERY_ACACIA},
    {SEG(897), LAT(19), SCENERY_ACACIA},         {SEG(906), LAT(-22), SCENERY_ACACIA},
    {SEG(906), LAT(29), SCENERY_ACACIA},         {SEG(913), LAT(15), SCENERY_ROCK},
    {SEG(924), LAT(-13), SCENERY_ROCK},          {SEG(934), LAT(14), SCENERY_ROCK},
    {SEG(945), LAT(-14), SCENERY_TERMITE_MOUND}, {SEG(956), LAT(-30), SCENERY_ACACIA},
    {SEG(956), LAT(14), SCENERY_TERMITE_MOUND},  {SEG(965), LAT(-29), SCENERY_GIRAFFE},
};

static const Checkpoint savanna_checkpoints[] = {
    {0, SECONDS(25)},        // the start line
    {SEG(400), SECONDS(25)}, // on the halfway straight
};

static const TrafficSpawn savanna_traffic[] = {
    {SEG(30), TRAFFIC_LANE_RIGHT, TRAFFIC_TRUCK}, {SEG(60), TRAFFIC_LANE_LEFT, TRAFFIC_JEEP},
    {SEG(95), TRAFFIC_LANE_RIGHT, TRAFFIC_JEEP},  {SEG(130), TRAFFIC_LANE_LEFT, TRAFFIC_TRUCK},
    {SEG(170), TRAFFIC_LANE_RIGHT, TRAFFIC_JEEP}, {SEG(210), TRAFFIC_LANE_LEFT, TRAFFIC_JEEP},
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

const Track savanna = {
    .segments = savanna_segments,
    .segment_count = COUNT(savanna_segments),
    .scenery = savanna_scenery,
    .scenery_count = COUNT(savanna_scenery),
    .checkpoints = savanna_checkpoints,
    .checkpoint_count = COUNT(savanna_checkpoints),
    .traffic = savanna_traffic,
    .traffic_count = COUNT(savanna_traffic),
    .start_time = SECONDS(34),
};

// --- The track --------------------------------------------------------------

// Summed once per track and kept: it is asked for several times a frame.
// The track is remembered by its address, so it must not change while the
// game runs (the game's and the tests' tracks are all const).
FIXED track_length(const Track* t) {
    static const Track* summed;
    static FIXED length;
    if (t != summed) {
        length = 0;
        for (int i = 0; i < t->segment_count; i++)
            length += t->segments[i].length * SEGMENT_LENGTH;
        summed = t;
    }
    return length;
}

// Distance taken around a lap into [0, lap): a loop rather than a
// division, since distances are at most a lap or two out.
static FIXED around(FIXED distance, FIXED lap) {
    while (distance >= lap)
        distance -= lap;
    while (distance < 0)
        distance += lap;
    return distance;
}

FIXED track_around(const Track* t, FIXED distance) {
    return around(distance, track_length(t));
}

int track_segment_at(const Track* t, FIXED distance, FIXED* end) {
    distance = around(distance, track_length(t));
    FIXED at = 0;
    int i = 0;
    for (;;) {
        at += t->segments[i].length * SEGMENT_LENGTH;
        if (distance < at || i + 1 == t->segment_count)
            break;
        i++;
    }
    *end = at;
    return i;
}

FIXED track_curve_at(const Track* t, FIXED distance) {
    FIXED end;
    int i = track_segment_at(t, distance, &end);
    return t->segments[i].curve * (FX_ONE / CURVE_SHARP);
}

// --- The race ---------------------------------------------------------------

// The race's own random numbers: xorshift32, as random.h's, but kept in the
// race so a race replays the same whatever else the game draws at random.
static u32 next_random(Race* r) {
    u32 x = r->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->rng = x;
    return x;
}

// How far b is ahead of a around the lap, in [-lap / 2, lap / 2).
static FIXED ahead_of(FIXED a, FIXED b, FIXED lap) {
    return around(b - a + (lap >> 1), lap) - (lap >> 1);
}

void race_start(Race* r, const Track* t, u32 seed) {
    *r = (Race){
        .track = t,
        .countdown = RACE_COUNTDOWN,
        .time_left = t->start_time,
        .next_checkpoint = t->checkpoint_count > 1 ? 1 : 0,
        .rng = seed ? seed : 0x5EB7A1u,
    };
    for (int i = 0; i < t->traffic_count && i < RACE_TRAFFIC_MAX; i++) {
        const TrafficSpawn* s = &t->traffic[i];
        r->traffic[i] = (Vehicle){s->distance, s->lateral, TRAFFIC_SPEED, s->kind};
        r->traffic_count++;
    }
}

// A skid is a sustained thing a sound can follow: it starts once hard
// steering at speed has lasted RACE_SKID_START frames (a tap of the stick
// never skids), and goes on RACE_SKID_TAIL frames after it ends (so steering
// let go and taken again is one skid, not two).
static u32 skid(Race* r, bool hard) {
    if (!hard)
        r->skid_hold = 0;
    else if (r->skid_hold < RACE_SKID_START)
        r->skid_hold++;
    u32 ev = r->skid_hold >= RACE_SKID_START || r->skid_tail > 0 ? RACE_EV_SKID : 0;
    if (r->skid_hold >= RACE_SKID_START)
        r->skid_tail = RACE_SKID_TAIL;
    else if (r->skid_tail > 0)
        r->skid_tail--;
    return ev;
}

// Steering and the curve's push; the car's state on or off the road.
static u32 steer(Race* r, u16 buttons) {
    u32 ev = 0;
    r->steer = (s8)(((buttons & BUTTON_RIGHT) ? 1 : 0) - ((buttons & BUTTON_LEFT) ? 1 : 0));
    FIXED rate = r->speed > 0 ? STEER_MIN + ((r->speed * STEER_K) >> 12) : 0;
    FIXED curve = track_curve_at(r->track, r->distance);
    FIXED push = (int_abs(curve) * r->speed * PUSH_K) >> 20;
    r->lateral += r->steer * rate - (curve < 0 ? -push : push);
    r->lateral = int_clamp(r->lateral, -CAR_MAX_X, CAR_MAX_X);
    r->car_state = int_abs(r->lateral) > FX_ONE ? CAR_OFFROAD : CAR_NORMAL;
    if (r->car_state == CAR_OFFROAD)
        ev |= RACE_EV_OFFROAD;
    bool hard = r->car_state == CAR_NORMAL && r->steer && r->speed >= SKID_SPEED &&
                int_abs(curve) >= SKID_CURVE;
    return ev | skid(r, hard);
}

// Accelerating, braking, coasting, and the off-road limit.
static void throttle(Race* r, u16 buttons) {
    FIXED top = r->car_state == CAR_OFFROAD ? CAR_OFFROAD_SPEED : CAR_TOP_SPEED;
    r->braking = (buttons & BUTTON_B) != 0;
    if (r->speed > top)
        r->speed = int_max(top, r->speed - OFFROAD_DRAG);
    else if (r->braking)
        r->speed = int_max(0, r->speed - BRAKE);
    else if (buttons & BUTTON_A)
        r->speed = int_min(top, r->speed + ACCEL_MIN + ((top - r->speed) >> 8));
    else
        r->speed = int_max(0, r->speed - COAST);
}

// A crash's frames: the car spins where it is, then is put back on the road.
static void spin(Race* r) {
    r->steer = 0;
    r->skid_hold = r->skid_tail = 0;
    r->braking = false;
    if (--r->crash_timer == 0) {
        r->car_state = CAR_NORMAL;
        r->lateral = 0;
        r->speed = 0;
    }
}

// Shortens the car's move this frame if it would close on a vehicle ahead
// in its way: it stops BUMP_GAP behind it, slower than it. Only a car
// faster than the vehicle bumps it; one the vehicle catches up with makes
// it wait (move_traffic()).
static u32 bump_traffic(Race* r, FIXED lap, FIXED* advance) {
    u32 ev = 0;
    if (r->car_state == CAR_CRASHED)
        return 0;
    for (int i = 0; i < r->traffic_count; i++) {
        const Vehicle* v = &r->traffic[i];
        FIXED gap = ahead_of(r->distance, v->distance, lap);
        if (gap < 0 || *advance <= v->speed || int_abs(v->lateral - r->lateral) >= BUMP_X)
            continue;
        if (gap + v->speed - *advance < BUMP_GAP) {
            *advance = int_max(0, gap + v->speed - BUMP_GAP);
            r->speed = (v->speed * 3) >> 2;
            r->bump_timer = CAR_BUMP_FRAMES;
            ev |= RACE_EV_BUMP;
        }
    }
    return ev;
}

// Traffic drives on, `from` being where the car was before its move. A
// vehicle that was behind the car and within its reach sideways never
// passes through it: it waits BUMP_GAP behind the car, at the car's pace,
// until the car moves out of its lane or pulls away (never backward). A
// vehicle never closes to less than BUMP_GAP behind another in its lane
// either, so a queue forms behind a waiting one instead of a stack: all
// drive at one speed, so only waiting can close them up. (The other is
// where it is now, moved this frame or not: never behind where it ends.)
static void move_traffic(Race* r, FIXED lap, FIXED from) {
    for (int i = 0; i < r->traffic_count; i++) {
        Vehicle* v = &r->traffic[i];
        FIXED move = v->speed;
        FIXED behind = ahead_of(v->distance, from, lap); // the car ahead of it by this
        if (behind > 0 && int_abs(v->lateral - r->lateral) < BUMP_X) {
            FIXED room = behind + ahead_of(from, r->distance, lap) - BUMP_GAP;
            move = int_clamp(room, 0, move);
        }
        for (int j = 0; j < r->traffic_count; j++) {
            const Vehicle* w = &r->traffic[j];
            FIXED gap = ahead_of(v->distance, w->distance, lap);
            if (j != i && gap > 0 && int_abs(v->lateral - w->lateral) < BUMP_X)
                move = int_clamp(gap - BUMP_GAP, 0, move);
        }
        v->distance = around(v->distance + move, lap);
    }
}

// Traffic left behind drives on from out of sight ahead.
static void recycle_traffic(Race* r, FIXED lap) {
    for (int i = 0; i < r->traffic_count; i++) {
        Vehicle* v = &r->traffic[i];
        if (ahead_of(r->distance, v->distance, lap) >= -TRAFFIC_BEHIND)
            continue;
        FIXED at = r->distance + TRAFFIC_AHEAD + (FIXED)(next_random(r) & TRAFFIC_SPREAD_MASK);
        // Just ahead of any vehicle that is too near; then look again from
        // there.
        for (int pass = 0; pass < RACE_TRAFFIC_MAX; pass++) {
            bool moved = false;
            for (int j = 0; j < r->traffic_count; j++) {
                const Vehicle* w = &r->traffic[j];
                if (j != i && int_abs(ahead_of(at, w->distance, lap)) < TRAFFIC_SPACING) {
                    at = w->distance + TRAFFIC_SPACING;
                    moved = true;
                }
            }
            if (!moved)
                break;
        }
        v->distance = around(at, lap);
    }
}

// The checkpoints and the start line the car reaches moving to `to` (its
// new distance, which may be past the lap's end); laps and the finish.
// Leaves the car at `to`, taken around the lap.
static u32 pass_checkpoints(Race* r, FIXED lap, FIXED to) {
    const Track* t = r->track;
    u32 ev = 0;
    while (r->next_checkpoint > 0 && to >= t->checkpoints[r->next_checkpoint].distance) {
        r->last_bonus = t->checkpoints[r->next_checkpoint].bonus;
        r->time_left += r->last_bonus;
        ev |= RACE_EV_CHECKPOINT;
        r->next_checkpoint =
            r->next_checkpoint + 1 < t->checkpoint_count ? r->next_checkpoint + 1 : 0;
    }
    if (to >= lap) {
        to -= lap;
        r->lap_times[r->lap] = r->race_time - r->lap_start;
        r->lap_start = r->race_time;
        r->lap++;
        ev |= RACE_EV_LAP;
        if (r->lap == RACE_LAPS) {
            r->over = r->finished = true;
            ev |= RACE_EV_FINISH;
        } else if (t->checkpoint_count > 0) {
            r->last_bonus = t->checkpoints[0].bonus;
            r->time_left += r->last_bonus;
            ev |= RACE_EV_CHECKPOINT;
            r->next_checkpoint = t->checkpoint_count > 1 ? 1 : 0;
        }
    }
    r->distance = to;
    return ev;
}

// Off the road, driving past a roadside object level with the car: a crash.
static u32 hit_scenery(Race* r, FIXED lap, FIXED from, FIXED to) {
    const Track* t = r->track;
    for (int i = 0; i < t->scenery_count; i++) {
        const Scenery* s = &t->scenery[i];
        bool passed = (s->distance > from && s->distance <= to) ||
                      (s->distance + lap > from && s->distance + lap <= to);
        if (passed && int_abs(s->lateral - r->lateral) < HIT_X) {
            r->car_state = CAR_CRASHED;
            r->crash_timer = CAR_CRASH_FRAMES;
            r->speed = 0;
            r->steer = 0;
            return RACE_EV_CRASH;
        }
    }
    return 0;
}

u32 race_step(Race* r, u16 buttons) {
    if (r->over)
        return 0;
    if (r->countdown > 0)
        return --r->countdown == 0 ? RACE_EV_GO : 0;
    r->race_time++;
    if (r->bump_timer > 0)
        r->bump_timer--;

    u32 ev = 0;
    FIXED advance = 0;
    if (r->car_state == CAR_CRASHED) {
        spin(r);
    } else {
        ev |= steer(r, buttons);
        throttle(r, buttons);
        advance = r->speed;
    }
    FIXED lap = track_length(r->track);
    ev |= bump_traffic(r, lap, &advance);

    FIXED from = r->distance;
    r->travelled += advance;
    ev |= pass_checkpoints(r, lap, from + advance);
    move_traffic(r, lap, from);
    if (r->car_state == CAR_OFFROAD)
        ev |= hit_scenery(r, lap, from, from + advance);
    recycle_traffic(r, lap);

    if (!r->over && r->time_left > 0)
        r->time_left--;
    if (!r->over && r->time_left == 0) {
        r->over = true;
        ev |= RACE_EV_TIME_UP;
    }
    return ev;
}

int race_speed_kmh(const Race* r) {
    return (r->speed * KMH_K) >> 10;
}

// A unit is 0.256 m (KMH_K's top speed, 5.25 units a frame, is 290 km/h):
// 10 m, a hundredth of a kilometre, is 2500 / 64 units. A FIXED holds at
// most 2^23 whole units, so the product stays under 2^29.
u32 race_hundredths_km(FIXED distance) {
    if (distance <= 0)
        return 0;
    return (u32)(distance >> FX_SHIFT) * 64 / 2500;
}

// --- Records ----------------------------------------------------------------

u32 records_update(Records* rec, const Race* r) {
    u32 fell = 0;
    if (!r->over)
        return 0;
    if (r->finished && (rec->best_race == 0 || r->race_time < rec->best_race)) {
        rec->best_race = r->race_time;
        fell |= RECORD_RACE;
    }
    for (int i = 0; i < r->lap && i < RACE_LAPS; i++) {
        if (rec->best_lap == 0 || r->lap_times[i] < rec->best_lap) {
            rec->best_lap = r->lap_times[i];
            fell |= RECORD_LAP;
        }
    }
    return fell;
}
