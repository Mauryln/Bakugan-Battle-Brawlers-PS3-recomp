#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using u8 = uint8_t;
using u32 = uint32_t;
constexpr u32 VP_VERT_STRIDE = 256;
struct VPSlot { float v[4]; };
struct rsx_state {
    struct { int enabled; } vertex_attribs[16];
    u32 begin_epoch, index_array_dma, restart_index_enable, restart_index;
};
static std::vector<u32> indices;
namespace reference {
#include "fixtures/rsx_strip_mocks.inc"
#include "fixtures/rsx_native_strip_reference.inc"
}
namespace candidate {
#include "fixtures/rsx_strip_mocks.inc"
#include "strip-defer-candidate.inc"
}
int main() {
    u32 epoch = 0, comparisons = 0;
    for (u32 bits = 0; bits < 256; ++bits)
    for (u32 enabled = 0; enabled < 2; ++enabled)
    for (u32 dma : {0u, 0x10u})
    for (u32 marker : {0xffffu, 0xffffffffu})
    for (u32 capacity : {2u, 5u, 32u})
    for (u32 copy_bytes : {64u, 256u})
    for (u32 partition : {0u, 1u}) {
        indices.clear();
        for (u32 i = 0; i < 16; ++i)
            indices.push_back(bits & (1u << (i & 7u)) ? marker : i % 7);
        rsx_state state{};
        state.vertex_attribs[0].enabled = 1;
        state.begin_epoch = ++epoch;
        state.index_array_dma = dma;
        state.restart_index_enable = enabled;
        state.restart_index = marker;
        reference::s_vp_copy_bytes = candidate::s_vp_copy_bytes = copy_bytes;
        u32 first = 0;
        for (u32 count : partition ? std::vector<u32>{1,2,3,1,4,5} : std::vector<u32>{16}) {
            reference::s_d3d.vp_vb_offset = candidate::s_d3d.vp_vb_offset = 0;
            reference::s_d3d.vp_vertex_capacity = candidate::s_d3d.vp_vertex_capacity = capacity;
            reference::revision = candidate::revision = epoch + first;
            reference::fetched = candidate::fetched = 0;
            reference::s_fetch_min_idx = candidate::s_fetch_min_idx = ~0u;
            reference::s_fetch_max_idx = candidate::s_fetch_max_idx = 0;
            memset(reference::buffer, 0xcd, sizeof(reference::buffer));
            memset(candidate::buffer, 0xcd, sizeof(candidate::buffer));
            u32 a = reference::upload_native_strip_vp_indexed(&state, first, count);
            u32 b = candidate::upload_native_strip_vp_indexed(&state, first, count);
            assert(a == b && a <= capacity);
            assert(reference::fetched == candidate::fetched);
            assert(reference::s_fetch_min_idx == candidate::s_fetch_min_idx);
            assert(reference::s_fetch_max_idx == candidate::s_fetch_max_idx);
            assert(!memcmp(reference::buffer, candidate::buffer, sizeof(reference::buffer)));
            assert(reference::s_d3d.vp_vb_offset == candidate::s_d3d.vp_vb_offset);
            first += count; ++comparisons;
        }
    }
    std::printf("PASS: %u packet comparisons; full emitted bytes, counts, bounds, fetches and index ranges identical\n", comparisons);
}
