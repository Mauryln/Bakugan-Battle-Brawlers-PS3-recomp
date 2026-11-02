#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using u32 = unsigned;
struct VPSlot { float v[4]; };
struct rsx_state { u32 generation; };
static std::vector<u32> indices;
static u32 fetches;
static u32 read_guest_index(const rsx_state*, u32 index) { return indices.at(index); }
static void read_vp_vertex(const rsx_state* state, u32 index, VPSlot* output)
{
    ++fetches;
    for (u32 attr = 0; attr < 16; ++attr)
        for (u32 lane = 0; lane < 4; ++lane)
            output[attr].v[lane] = float(index * 100 + attr * 4 + lane + state->generation);
}
#include "index_decode.inc"
static void check(bool condition)
{
    if (!condition) { puts("FAIL: indexed packet cache"); exit(1); }
}
int main()
{
    rsx_state state = {};
    for (u32 pattern = 0; pattern < 4; ++pattern) {
        indices.resize(512);
        for (u32 i = 0; i < indices.size(); ++i)
            indices[i] = pattern == 0 ? i % 12 : pattern == 1 ? (i % 8) * 64 :
                pattern == 2 ? i : (i * 17) % 83;
        std::vector<VPSlot> expected(indices.size() * 16), actual(expected.size() + 1);
        for (u32 generation = 0; generation < 2; ++generation) {
            state.generation = generation * 100000;
            for (u32 i = 0; i < indices.size(); ++i)
                read_vp_vertex(&state, indices[i], expected.data() + i * 16);
            memset(actual.data(), 0xA5, actual.size() * sizeof(VPSlot));
            fetches = 0;
            const u32 decoded = vp_fetch_triangle_vertices(&state, 0, u32(indices.size()), actual.data());
            check(decoded == fetches);
            check(!memcmp(expected.data(), actual.data(), expected.size() * sizeof(VPSlot)));
            if (pattern == 0) check(fetches == 12);
            for (unsigned char byte : *reinterpret_cast<unsigned char(*)[16]>(&actual.back()))
                check(byte == 0xA5);
        }
    }
    puts("PASS: reuse, collisions, unique indices, packet isolation and bounded canonical writes");
}
