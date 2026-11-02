#include <cstdint>
#include <cstdio>
using u32 = unsigned;
using u64 = uint64_t;
struct RSXPrimitivePerf { u64 calls, inputs; double seconds; };
#include "primitive_perf.inc"
int main()
{
    RSXPrimitivePerf arrays[11] = {}, indexed[11] = {};
    perf_record_primitive_sample(arrays, 8, 4, 0.25);
    perf_record_primitive_sample(arrays, 8, 8, 0.5);
    perf_record_primitive_sample(indexed, 5, 0xFFFFFFFFu, 0.125);
    perf_record_primitive_sample(indexed, 5, 1, 0.125);
    perf_record_primitive_sample(arrays, 0xFFFFFFFFu, 7, 0.0625);
    if (arrays[8].calls != 2 || arrays[8].inputs != 12 || arrays[8].seconds != 0.75) return 1;
    if (indexed[5].calls != 2 || indexed[5].inputs != 0x100000000ull || indexed[5].seconds != 0.25) return 2;
    if (arrays[0].calls != 1 || arrays[0].inputs != 7 || arrays[0].seconds != 0.0625) return 3;
    for (u32 i = 1; i < 11; ++i) if (i != 8 && arrays[i].calls) return 4;
    puts("PASS: per-primitive timing, separate submission paths, 64-bit counts and unknown values");
}
