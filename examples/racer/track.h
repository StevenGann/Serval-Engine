#ifndef RACER_TRACK_H
#define RACER_TRACK_H

// The circuit and the race's rules: where the road bends, what stands beside
// it, who drives on it, and what a frame of input does to the player's car.
// Portable arithmetic only (no hardware), so tests/racer_tests.c runs it on
// the host.
//
// Units:
// - Distance along the track is FIXED (24.8) in track units, from the start
//   line (0) to track_length(), where it wraps back to 0. A segment, the
//   unit the circuit is written in and one light or dark stripe of the road,
//   is SEGMENT_LENGTH long.
// - Lateral position is FIXED in half-road widths: 0 is the road's middle,
//   -FX_ONE and FX_ONE its left and right edges (the rumble strips' outer
//   edges). The car can go CAR_MAX_X out.
// - Speed is FIXED track units per frame.
// - Times are frames at 60 a second.

#include <serval/serval.h>

// --- Units ------------------------------------------------------------------

// A segment: 16 track units (SEGMENT_LENGTH is FIXED, like all distances).
// A power of two, so a distance's segment is a shift: distance >> SEGMENT_SHIFT.
#define SEGMENT_SHIFT 12
#define SEGMENT_LENGTH (1 << SEGMENT_SHIFT)

// TrackSegment.curve runs from -CURVE_SHARP (the sharpest left) to CURVE_SHARP
// (the sharpest right); track_curve_at() turns it into FIXED, where FX_ONE is
// the sharpest curve the circuit has.
#define CURVE_SHARP 4

// The camera sits this far behind the car: the screen's bottom line shows
// the road at the car (road.h). The car is "at" Race.distance; the jeep is
// drawn with its wheels on that bottom line.
#define CAR_DEPTH FX(64)

// --- The circuit ------------------------------------------------------------

// A stretch of road of one curvature.
typedef struct {
    u8 length; // in segments
    s8 curve;  // -CURVE_SHARP (left) .. CURVE_SHARP (right); 0 straight
} TrackSegment;

// What stands beside the road. SCENERY_CHECKPOINT is the marker drawn at
// each of Track.checkpoints; it is never in Track.scenery.
enum {
    SCENERY_ACACIA,
    SCENERY_TERMITE_MOUND,
    SCENERY_ROCK,
    SCENERY_SIGN_LEFT,  // a sign whose arrow points left: a left curve ahead
    SCENERY_SIGN_RIGHT, // ... right
    SCENERY_GIRAFFE,
    SCENERY_CHECKPOINT,
};

// One roadside object, standing still.
typedef struct {
    FIXED distance; // along the track, track units
    FIXED lateral;  // half-road widths; beyond the road (|lateral| > FX_ONE)
    u8 kind;        // SCENERY_*
} Scenery;

// A checkpoint: passing it adds time. Checkpoint 0 is the start line
// (distance 0).
typedef struct {
    FIXED distance; // along the track, track units
    u32 bonus;      // frames added to the time left
} Checkpoint;

// Traffic: other safari vehicles, driving at constant speeds in lanes.
enum {
    TRAFFIC_JEEP,
    TRAFFIC_TRUCK,
};

// Where one vehicle starts. Spawns are at least 300 track units apart.
typedef struct {
    FIXED distance; // along the track, track units, at the race's start
    FIXED lateral;  // half-road widths: TRAFFIC_LANE_LEFT or TRAFFIC_LANE_RIGHT
    u8 kind;        // TRAFFIC_*
} TrafficSpawn;

// The two lanes, and the one speed all traffic drives at, track units per
// frame: about half the car's top speed. One speed, so vehicles never close
// on each other: never two side by side, never one through another.
//
// Collisions with traffic are only along the road: the car bumps a vehicle
// only by running into it from behind (RACE_EV_BUMP), and a vehicle that
// catches the car up waits behind it. A vehicle alongside the car never
// collides with it, so its sprite may overlap the jeep's side; the game
// draws them as they are.
#define TRAFFIC_LANE_LEFT (-FX_ONE / 2)
#define TRAFFIC_LANE_RIGHT (FX_ONE / 2)
#define TRAFFIC_SPEED (FX(11) / 4) // 2.75: 151 km/h

// A whole circuit, a closed loop. Its length is the sum of its segments' and
// must be an even number of segments, so that the stripes run on unbroken
// across the start line.
typedef struct Track {
    const TrackSegment* segments;
    u16 segment_count;
    const Scenery* scenery; // sorted by distance
    u16 scenery_count;
    const Checkpoint* checkpoints; // sorted by distance; [0] is the start line
    u8 checkpoint_count;
    const TrafficSpawn* traffic;
    u8 traffic_count; // at most RACE_TRAFFIC_MAX
    u32 start_time;   // frames on the clock at GO
} Track;

// The game's one circuit.
extern const Track savanna;

// The lap's length, track units.
FIXED track_length(const Track* t);

// A distance taken around the lap into [0, track_length()). An object's
// distance ahead of the car: track_around(t, object - car), which is near
// the lap's length for one just behind.
FIXED track_around(const Track* t, FIXED distance);

// The curve at a distance (any value: it is taken around the lap), FIXED:
// -FX_ONE the sharpest left, FX_ONE the sharpest right, 0 straight.
FIXED track_curve_at(const Track* t, FIXED distance);

// The index of the segment holding a distance (taken around the lap), and in
// *end the distance where that segment ends (track units, at most
// track_length()). road_frame() walks on from there, line by line.
int track_segment_at(const Track* t, FIXED distance, FIXED* end);

// --- The race ---------------------------------------------------------------

#define RACE_LAPS 3
#define RACE_TRAFFIC_MAX 6
// Frames from race_start() to GO: three seconds, for "3", "2", "1".
#define RACE_COUNTDOWN 180

// The car's top speed: 5.25 track units a frame, shown as 290 km/h. Under a
// segment a frame, so the stripes never seem to run backwards (road.h).
#define CAR_TOP_SPEED (FX(21) / 4)
// Top speed off the road: a third.
#define CAR_OFFROAD_SPEED (CAR_TOP_SPEED / 3)
// How far out the car can go: 1.5 half-road widths, a quarter of the road's
// width past an edge (further would put the jeep half off screen, road.c).
#define CAR_MAX_X (FX(3) / 2)
// A crash lasts this long (frames) before the car is put back on the road.
#define CAR_CRASH_FRAMES 90
// The picture shakes this long after a bump (frames).
#define CAR_BUMP_FRAMES 16
// Traffic and the car keep this far apart along the track (track units):
// a bumped car stops this far behind the vehicle, and a vehicle catching
// the car from behind waits this far behind it.
#define BUMP_GAP FX(12)

// Race.car_state.
enum {
    CAR_NORMAL,  // on the road
    CAR_OFFROAD, // past the rumble strips: slow, bumpy
    CAR_CRASHED, // spinning after hitting scenery; crash_timer counts down
};

// One vehicle of traffic.
typedef struct {
    FIXED distance; // along the track, track units, in [0, track_length())
    FIXED lateral;  // half-road widths (its lane)
    FIXED speed;    // track units per frame, constant
    u8 kind;        // TRAFFIC_*
} Vehicle;

// The whole state of a race. race_start() sets all of it.
typedef struct Race {
    const Track* track;

    // The start and the end.
    u16 countdown; // frames to GO; 0 once the race is on
    bool over;     // finished or timed out: race_step() does nothing more
    bool finished; // the last lap was completed (over and not timed out)

    // The player's car.
    FIXED distance;  // along the track, track units, in [0, track_length())
    FIXED lateral;   // half-road widths, within +-CAR_MAX_X
    FIXED speed;     // track units per frame, 0 .. CAR_TOP_SPEED
    u8 car_state;    // CAR_*
    u8 crash_timer;  // frames left of a crash
    u8 bump_timer;   // frames left of a bump's shake
    s8 steer;        // -1 left, 0, 1 right: what is held, for the jeep's frame
    bool braking;    // B held, for the brake lights
    u8 skid_hold;    // frames hard steering has lasted, up to RACE_SKID_START
    u8 skid_tail;    // frames the skid goes on for (RACE_EV_SKID while > 0)
    FIXED travelled; // track units driven since GO (for "distance reached")

    // Traffic.
    Vehicle traffic[RACE_TRAFFIC_MAX];
    u8 traffic_count;

    // The clock, frames.
    u32 time_left;  // counts down from Track.start_time at GO
    u32 race_time;  // frames since GO
    u32 last_bonus; // the bonus of the checkpoint passed last, for "+12"

    // Laps and checkpoints.
    u8 lap;                   // laps completed, 0 .. RACE_LAPS
    u32 lap_times[RACE_LAPS]; // frames; 0 for a lap not completed
    u32 lap_start;            // race_time when the current lap began
    u8 next_checkpoint;       // index into Track.checkpoints

    u32 rng; // the race's own random numbers (xorshift32), for traffic
} Race;

// race_step()'s events, a bitmask. The last two are held: set every frame
// while they last.
#define RACE_EV_GO (1u << 0)         // the countdown ended: the race is on
#define RACE_EV_CHECKPOINT (1u << 1) // a checkpoint passed; Race.last_bonus added
#define RACE_EV_LAP (1u << 2)        // a lap completed (also on the last lap)
#define RACE_EV_FINISH (1u << 3)     // the last lap completed: the race is over
#define RACE_EV_TIME_UP (1u << 4)    // the clock reached 0: the race is over
#define RACE_EV_CRASH (1u << 5)      // the car hit scenery
#define RACE_EV_BUMP (1u << 6)       // the car ran into a vehicle from behind
#define RACE_EV_OFFROAD (1u << 7)    // held: the car is off the road
#define RACE_EV_SKID (1u << 8)       // held: a skid (RACE_SKID_START, _TAIL)

// A skid: steering held, on the road, at three quarters of top speed or
// more, in a curve at least half the sharpest, for RACE_SKID_START frames
// in a row; it goes on RACE_SKID_TAIL frames after that ends.
#define RACE_SKID_START 8
#define RACE_SKID_TAIL 12

// Sets up a race on a track: the car on the start line, at rest, the
// countdown running, traffic at its spawns, the clock full. The same seed
// gives the same race for the same input.
void race_start(Race* r, const Track* t, u32 seed);

// One frame of the race with the buttons held (BUTTON_A accelerates,
// BUTTON_B brakes, BUTTON_LEFT and BUTTON_RIGHT steer; others are ignored).
// During the countdown and a crash input does nothing; once the race is over
// nothing changes. Pausing is the caller's: it stops calling. Returns
// RACE_EV_* bits.
u32 race_step(Race* r, u16 buttons);

// The car's speed for the speedometer: 0 .. 290.
int race_speed_kmh(const Race* r);

// A distance in hundredths of a kilometre, rounded down: the results'
// "distance reached". Any FIXED distance converts without overflowing.
u32 race_hundredths_km(FIXED distance);

// --- Records ----------------------------------------------------------------

// The saved bests, frames; 0 means none yet.
typedef struct {
    u32 best_race;
    u32 best_lap;
} Records;

#define RECORD_RACE (1u << 0)
#define RECORD_LAP (1u << 1)

// Takes a race's results into the records: the race time if the race was
// finished and beats the best (or there is none), and each completed lap's
// time that beats the best lap. Returns the RECORD_* bits of what fell.
u32 records_update(Records* rec, const Race* r);

#endif // RACER_TRACK_H
