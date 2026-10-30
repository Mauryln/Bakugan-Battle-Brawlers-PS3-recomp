#include <cstdint>
#include <cstdio>
#include <cstdlib>

typedef uint16_t u16;
typedef uint64_t u64;
typedef struct { u64 start_ms; int held; int active; } PadMotionPulse;
#include "pad_motion.inc"

static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void sample(PadMotionPulse* pulse, int pressed, u64 now, u16 expected_z)
{
    u16 values[6] = {0xCAFE, 9999, 9999, 9999, 9999, 0xFACE};
    pad_forward_motion(values + 1, pressed, now, pulse);
    require(values[0] == 0xCAFE && values[5] == 0xFACE, "motion writes only four sensor words");
    require(values[1] == 512 && values[2] == 399 && values[4] == 512,
            "forward launch keeps X, gravity Y, and gyro neutral");
    for (int i = 1; i <= 4; ++i) require(values[i] <= 1023, "Sixaxis sensor remains in 10-bit range");
    if (values[3] != expected_z) {
        fprintf(stderr, "FAIL: at %llu ms pressed=%d Z=%u expected=%u\n",
                (unsigned long long)now, pressed, values[3], expected_z);
        exit(1);
    }
}

int main()
{
    PadMotionPulse first = {}, second = {};
    sample(&first, 0, 0, 512);
    sample(&first, 1, 0, 1023); // zero is a valid start timestamp
    sample(&first, 1, 99, 1023);
    sample(&first, 1, 100, 0);
    sample(&first, 1, 199, 0);
    sample(&first, 1, 200, 512);
    sample(&first, 1, 1000, 512); // holding does not auto-repeat
    sample(&second, 0, 1000, 512); // state is independent per controller
    sample(&second, 1, 1000, 1023);
    sample(&first, 0, 1001, 512);
    sample(&first, 1, 1002, 1023); // release + press starts a fresh impulse
    sample(&first, 0, 1050, 1023); // a short tap still completes its waveform
    sample(&first, 0, 1102, 0);
    sample(&first, 0, 1202, 512);
    sample(&second, 1, 1202, 512);
    sample(&second, 0, 1300, 512);
    sample(&second, 1, 1301, 1023);
    sample(&first, 0, 1301, 512);
    u16 directional[4];
    pad_direction_motion(directional, 1, 0, 0, 0);
    require(directional[1] == 0 && directional[0] == 512, "up drives vertical acceleration");
    pad_direction_motion(directional, 0, 1, 0, 0);
    require(directional[1] == 1023, "down drives opposite acceleration");
    pad_direction_motion(directional, 0, 0, 1, 0);
    require(directional[0] == 0, "left drives lateral acceleration");
    pad_direction_motion(directional, 0, 0, 0, 1);
    require(directional[0] == 1023, "right drives opposite lateral acceleration");
    pad_direction_motion(directional, 1, 1, 1, 1);
    require(directional[0] == 512 && directional[1] == 399, "opposing directions cancel");
    puts("PASS: forward Sixaxis pulse handles zero time, phase boundaries, held buttons, retriggers, and independent ports");
}
