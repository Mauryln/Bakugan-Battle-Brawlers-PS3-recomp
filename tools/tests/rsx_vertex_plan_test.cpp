#define _CRT_SECURE_NO_WARNINGS
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using u8 = uint8_t;
using u32 = uint32_t;
struct VPSlot { float v[4]; };
struct rsx_vertex_attrib { u32 enabled, stride, type, size, frequency; };
struct rsx_state {
    rsx_vertex_attrib vertex_attribs[16];
    float vertex_data4f[16][4];
};
struct rsx_vertex_desc { const u8* base; u32 stride, type, n; };
static u8 storage[16][256];
static const u8* rsx_attrib_base(const rsx_state* st, int i) {
    return st->vertex_attribs[i].enabled && st->vertex_attribs[i].stride ? storage[i] : nullptr;
}

#include "vertex_plan.inc"
int main() {
    rsx_state state{};
    for (u32 i = 0; i < 16; ++i) {
        state.vertex_attribs[i] = {1, 12, 2, 3, 1};
        vp_prepare_vertex_fetch(&state, 1u << i);
        if (s_vertex_fetch_plan.desc[i].base != storage[i]) return 2;
        state.vertex_attribs[i].enabled = 0;
        vp_prepare_vertex_fetch(&state, 1u << i);
        if (s_vertex_fetch_plan.desc[i].base) {
            std::printf("FAIL: disabled attribute %u retains previous draw's array base\n", i);
            return 1;
        }
        state.vertex_attribs[i].enabled = 1;
        vp_prepare_vertex_fetch(&state, 1u << i);
        state.vertex_attribs[i].stride = 0;
        vp_prepare_vertex_fetch(&state, 1u << i);
        if (s_vertex_fetch_plan.desc[i].base) return 3;
    }
    std::puts("PASS: all16 enabled/disabled/zero-stride transitions reset descriptor bases");
}
