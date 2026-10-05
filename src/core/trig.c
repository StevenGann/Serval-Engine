#include "serval/math.h"

// sin(x) for x in [0, 90] degrees, 256 steps plus the endpoint, in 1/4096ths.
// Generated: round(sin(i * pi / 512) * 4096) for i = 0..256. The other three
// quadrants mirror it, so a full turn has 1024 steps.
static const u16 sin_quarter[257] = {
    0,    25,   50,   75,   101,  126,  151,  176,  201,  226,  251,  276,  301,  326,  351,  376,
    401,  426,  451,  476,  501,  526,  551,  576,  601,  626,  651,  675,  700,  725,  750,  774,
    799,  824,  848,  873,  897,  922,  946,  971,  995,  1020, 1044, 1068, 1092, 1117, 1141, 1165,
    1189, 1213, 1237, 1261, 1285, 1309, 1332, 1356, 1380, 1404, 1427, 1451, 1474, 1498, 1521, 1544,
    1567, 1591, 1614, 1637, 1660, 1683, 1706, 1729, 1751, 1774, 1797, 1819, 1842, 1864, 1886, 1909,
    1931, 1953, 1975, 1997, 2019, 2041, 2062, 2084, 2106, 2127, 2149, 2170, 2191, 2213, 2234, 2255,
    2276, 2296, 2317, 2338, 2359, 2379, 2399, 2420, 2440, 2460, 2480, 2500, 2520, 2540, 2559, 2579,
    2598, 2618, 2637, 2656, 2675, 2694, 2713, 2732, 2751, 2769, 2788, 2806, 2824, 2843, 2861, 2878,
    2896, 2914, 2932, 2949, 2967, 2984, 3001, 3018, 3035, 3052, 3068, 3085, 3102, 3118, 3134, 3150,
    3166, 3182, 3198, 3214, 3229, 3244, 3260, 3275, 3290, 3305, 3320, 3334, 3349, 3363, 3378, 3392,
    3406, 3420, 3433, 3447, 3461, 3474, 3487, 3500, 3513, 3526, 3539, 3551, 3564, 3576, 3588, 3600,
    3612, 3624, 3636, 3647, 3659, 3670, 3681, 3692, 3703, 3713, 3724, 3734, 3745, 3755, 3765, 3775,
    3784, 3794, 3803, 3812, 3822, 3831, 3839, 3848, 3857, 3865, 3873, 3881, 3889, 3897, 3905, 3912,
    3920, 3927, 3934, 3941, 3948, 3954, 3961, 3967, 3973, 3979, 3985, 3991, 3996, 4002, 4007, 4012,
    4017, 4022, 4027, 4031, 4036, 4040, 4044, 4048, 4052, 4055, 4059, 4062, 4065, 4068, 4071, 4074,
    4076, 4079, 4081, 4083, 4085, 4087, 4088, 4090, 4091, 4092, 4093, 4094, 4095, 4095, 4096, 4096,
    4096,
};

FIXED fx_sin(u16 angle) {
    // sin(-a) == -sin(a): work on the angle's size in [0, 180] degrees and apply
    // the sign last, so every rounding step treats both signs the same.
    bool negative = angle > 0x8000;
    u32 a = negative ? 0x10000u - angle : angle;
    u32 steps = (a + 32) >> 6; // nearest of 512 steps per half turn
    u32 index = steps > 256 ? 512 - steps : steps;
    FIXED v = (sin_quarter[index] + 8) >> 4; // 1/4096ths to 24.8, rounded
    return negative ? -v : v;
}

FIXED fx_cos(u16 angle) {
    return fx_sin((u16)(angle + ANGLE_DEG(90)));
}

// --- angle_of and fx_length -------------------------------------------------
//
// Both reduce (dx, dy) to the larger magnitude `big` and the ratio small / big
// in [0, 1], without dividing: big is scaled by a power of two (shifts) into
// [2^16, 2^17), and the ratio is small times a table reciprocal of big's top
// bits. atan and sqrt(1 + r^2) of the ratio then come from small tables with
// linear interpolation.

// 2^31 / big for big in [2^16, 2^17), by its top 9 bits rounded:
// round(2^23 / (256 + j)) for j = 0..256.
static const u16 recip_table[257] = {
    32768, 32640, 32514, 32388, 32264, 32140, 32018, 31896, 31775, 31655, 31536, 31418, 31301,
    31184, 31069, 30954, 30840, 30728, 30615, 30504, 30394, 30284, 30175, 30067, 29959, 29853,
    29747, 29642, 29537, 29434, 29331, 29229, 29127, 29026, 28926, 28827, 28728, 28630, 28533,
    28436, 28340, 28244, 28150, 28056, 27962, 27869, 27777, 27685, 27594, 27504, 27414, 27324,
    27236, 27148, 27060, 26973, 26887, 26801, 26715, 26631, 26546, 26462, 26379, 26297, 26214,
    26133, 26052, 25971, 25891, 25811, 25732, 25653, 25575, 25497, 25420, 25343, 25267, 25191,
    25116, 25041, 24966, 24892, 24818, 24745, 24672, 24600, 24528, 24457, 24385, 24315, 24245,
    24175, 24105, 24036, 23967, 23899, 23831, 23764, 23697, 23630, 23564, 23498, 23432, 23367,
    23302, 23237, 23173, 23109, 23046, 22982, 22920, 22857, 22795, 22733, 22672, 22611, 22550,
    22490, 22429, 22370, 22310, 22251, 22192, 22134, 22075, 22017, 21960, 21902, 21845, 21789,
    21732, 21676, 21620, 21565, 21509, 21454, 21400, 21345, 21291, 21237, 21183, 21130, 21077,
    21024, 20972, 20919, 20867, 20815, 20764, 20713, 20662, 20611, 20560, 20510, 20460, 20410,
    20361, 20311, 20262, 20214, 20165, 20117, 20068, 20021, 19973, 19925, 19878, 19831, 19784,
    19738, 19692, 19645, 19600, 19554, 19508, 19463, 19418, 19373, 19329, 19284, 19240, 19196,
    19152, 19108, 19065, 19022, 18979, 18936, 18893, 18851, 18809, 18766, 18725, 18683, 18641,
    18600, 18559, 18518, 18477, 18437, 18396, 18356, 18316, 18276, 18236, 18197, 18157, 18118,
    18079, 18040, 18001, 17963, 17924, 17886, 17848, 17810, 17772, 17735, 17697, 17660, 17623,
    17586, 17549, 17513, 17476, 17440, 17404, 17368, 17332, 17296, 17261, 17225, 17190, 17155,
    17120, 17085, 17050, 17015, 16981, 16947, 16913, 16878, 16845, 16811, 16777, 16744, 16710,
    16677, 16644, 16611, 16578, 16546, 16513, 16481, 16448, 16416, 16384,
};

// atan(k / 32) for k = 0..32, in u16 angle units (65536 per turn):
// round(atan(k / 32) * 65536 / (2 * pi)).
static const u16 atan_table[33] = {
    0,    326,  651,  975,  1297, 1617, 1933, 2246, 2555, 2860, 3159,
    3453, 3742, 4025, 4302, 4572, 4836, 5094, 5344, 5589, 5826, 6058,
    6282, 6500, 6712, 6917, 7117, 7310, 7498, 7679, 7856, 8026, 8192,
};

// (sqrt(1 + (k / 32)^2) - 1) for k = 0..32, in 1/32768ths:
// round((sqrt(1 + (k / 32)^2) - 1) * 32768).
static const u16 hypot_table[33] = {
    0,    16,   64,   144,  255,  398,   571,   775,   1008,  1271,  1563,
    1882, 2228, 2601, 2999, 3421, 3868,  4337,  4828,  5341,  5874,  6426,
    6997, 7586, 8192, 8814, 9453, 10106, 10773, 11454, 12148, 12855, 13573,
};

typedef struct {
    u32 big;    // max(|dx|, |dy|), unscaled
    u32 scaled; // big scaled into [2^16, 2^17)
    int shift;  // scaled = big >> shift (shift > 0) or big << -shift
    u32 ratio;  // small / big in 1/65536ths, 0..65535
} Ratio;

// Requires big > 0 and small <= big.
static Ratio ratio_of(u32 big, u32 small) {
    Ratio r = {.big = big, .shift = 0};
    // Scale by shifts in decreasing steps (no count-leading-zeros on the
    // ARM7TDMI); each step keeps big >= 2^16.
#define SCALE_DOWN(limit, n)                                                                       \
    if (big >= 1u << (limit)) {                                                                    \
        big >>= (n);                                                                               \
        small >>= (n);                                                                             \
        r.shift += (n);                                                                            \
    }
#define SCALE_UP(limit, n)                                                                         \
    if (big < 1u << (limit)) {                                                                     \
        big <<= (n);                                                                               \
        small <<= (n);                                                                             \
        r.shift -= (n);                                                                            \
    }
    if (big >= 1u << 17) {
        SCALE_DOWN(25, 8)
        SCALE_DOWN(21, 4)
        SCALE_DOWN(19, 2)
        SCALE_DOWN(18, 1)
        SCALE_DOWN(17, 1)
    } else {
        SCALE_UP(8, 8)
        SCALE_UP(12, 4)
        SCALE_UP(14, 2)
        SCALE_UP(15, 1)
        SCALE_UP(16, 1)
    }
#undef SCALE_DOWN
#undef SCALE_UP
    r.scaled = big;
    // small < 2^17 and the reciprocal < 2^15 + 1: the product fits 32 bits.
    u32 ratio = (small * recip_table[(big - (1u << 16) + 128) >> 8]) >> 15;
    r.ratio = ratio > 0xFFFF ? 0xFFFF : ratio;
    return r;
}

// Linear interpolation in a 33-entry table over [0, 1] (ratio < 65536).
static u32 lerp_table(const u16* table, u32 ratio) {
    u32 k = ratio >> 11, f = ratio & 0x7FF;
    u32 lo = table[k], hi = table[k + 1]; // both tables increase
    return lo + (((hi - lo) * f + 0x400) >> 11);
}

static u32 magnitude(FIXED v) {
    return v < 0 ? 0u - (u32)v : (u32)v; // exact for INT32_MIN too
}

u16 angle_of(FIXED dx, FIXED dy) {
    u32 ax = magnitude(dx), ay = magnitude(dy);
    if (ax == 0 && ay == 0)
        return 0;
    u32 a = ax >= ay ? lerp_table(atan_table, ratio_of(ax, ay).ratio)
                     : 0x4000u - lerp_table(atan_table, ratio_of(ay, ax).ratio);
    if (dx < 0)
        a = 0x8000u - a;
    if (dy < 0)
        a = 0u - a;
    return (u16)a;
}

FIXED fx_length(FIXED dx, FIXED dy) {
    u32 ax = magnitude(dx), ay = magnitude(dy);
    if (ax == 0 && ay == 0)
        return 0;
    Ratio r = ax >= ay ? ratio_of(ax, ay) : ratio_of(ay, ax);
    // big * (sqrt(1 + ratio^2) - 1), on the scaled value: < 2^17 * 2^14.
    u32 extra = r.scaled * lerp_table(hypot_table, r.ratio) >> 15;
    if (r.shift > 0)
        extra <<= r.shift; // at most 0.42 * 2^31: no overflow
    else if (r.shift < 0)
        extra = (extra + (1u << (-r.shift - 1))) >> -r.shift;
    u32 length = r.big + extra; // < 1.42 * 2^31
    return length > 0x7FFFFFFFu ? 0x7FFFFFFF : (FIXED)length;
}
