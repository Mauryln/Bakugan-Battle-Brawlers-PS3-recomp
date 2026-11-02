#include "spu_helpers.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

static uint32_t random_bits = 0x89ABCDEF;
static uint32_t next_bits()
{
    random_bits ^= random_bits << 13;
    random_bits ^= random_bits >> 17;
    random_bits ^= random_bits << 5;
    return random_bits;
}

int main()
{
    const uint32_t edges[] = {0, 0x80000000, 1, 0x007FFFFF, 0x00800000,
        0x3F800000, 0xBF800000, 0x4EFFFFFF, 0x4F000000, 0xCF000000,
        0x7F000000, 0xFF000000, 0x7F800000, 0x7FFFFFFF, 0xFFFFFFFF};
    for (unsigned mode = 0; mode < 4; ++mode) {
        _mm_setcsr((_mm_getcsr() & ~0x6000u) | (mode << 13));
        for (unsigned test = 0; test < 1000000; ++test) {
            u128 input;
            for (int lane = 0; lane < 4; ++lane)
                input._u32[lane] = test < sizeof(edges) / sizeof(edges[0])
                    ? edges[test] : next_bits();
            for (int scale : {173, 155, 0, 255}) {
                const u128 actual = spu_cflts(input, scale);
                for (int lane = 0; lane < 4; ++lane)
                    if (actual._u32[lane] != spu__cfl_lane(input._u32[lane], 173 - scale, 0)) {
                        puts("FAIL: CFLTS differs from scalar SPU semantics");
                        return 1;
                    }
            }
        }
    }
    _mm_setcsr((_mm_getcsr() & ~0x6000u));
    constexpr unsigned count = 16384;
    u128 values[count];
    for (auto& value : values)
        for (auto& lane : value._u32) lane = next_bits();
    volatile uint32_t checksum = 0;
    for (int path = 0; path < 2; ++path) {
        const auto start = std::chrono::steady_clock::now();
        uint32_t sum = 0;
        for (int repeat = 0; repeat < 256; ++repeat)
            for (const auto& value : values) {
                if (path) {
                    const u128 result = spu_cflts(value, 173);
                    for (auto lane : result._u32) sum += lane;
                } else {
                    for (auto lane : value._u32) sum += spu__cfl_lane(lane, 0, 0);
                }
            }
        checksum = sum;
        printf("%s %.2f ms checksum=%u\n", path ? "native" : "reference",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), unsigned(checksum));
    }
    puts("PASS: CFLTS edges, random bits, scales and all MXCSR rounding modes");
}
