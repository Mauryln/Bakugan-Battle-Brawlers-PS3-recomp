#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
#include "draw_capacity.inc"
struct rsx_state {
    u32 surface_format, surface_zeta_offset, surface_clip_w, surface_clip_h;
    float viewport_scale[4], viewport_offset[4];
};
struct D3D12DrawRecord {
    int is_clear, is_vp, tex_slot;
    u32 rt_off, rt_mrt[3], rt_w, rt_h, rt_fmt, cmask, clear_mask;
    float cc[4], clear_z;
    u8 clear_s;
    u32 zeta_off, zeta_w, zeta_h, zeta_fmt;
    float viewport_scale[4], viewport_offset[4];
};
static struct {
    D3D12DrawRecord draws[MAX_DRAWS];
    u32 draw_count, vb_offset, vp_vb_offset, fps;
    u64 frame_count, last_fps_time;
    bool initialized = true;
    float clear_color[4];
    rsx_state* current_rsx_state;
} s_d3d;
static u32 s_dbg_clears_since_present, s_clear_presents;
static unsigned flip_seq, presents;
static int blink_dbg() { return 0; }
unsigned cellGcm_flip_request_count() { return flip_seq; }
static u32 current_rt_off(u32* w, u32* h, u32* mrt)
{
    *w = 1280; *h = 720;
    memset(mrt, 0, 3 * sizeof(u32));
    return 0;
}
static void render_frame()
{
    ++presents;
    ++s_d3d.frame_count;
    s_d3d.draw_count = 0;
}
static int depth_pool_find(u32 offset, u32, u32) { return offset == 0x0F510000 ? 0 : -1; }
#include "frame_boundary.inc"
static void submit_frame(u32 draws)
{
    s_d3d.draw_count = draws;
    memset(s_d3d.draws, 0, sizeof(s_d3d.draws));
    s_d3d.draws[0].is_vp = 1;
    s_d3d.draws[0].cmask = 15;
    ++flip_seq;
    const unsigned before = presents;
    // Multiple clears of the display may be passes in one guest frame.
    const bool native = s_d3d.current_rsx_state != nullptr;
    d3d12_clear(nullptr, native ? 0xf3 : 0xf0, 0, 1, 0);
    d3d12_clear(nullptr, native ? 0xf3 : 0xf0, 0, 1, 0);
    assert(presents == before);
    assert(s_d3d.draw_count == draws + (native ? 2 : 0));
    if (native) {
        assert(s_d3d.draws[draws].zeta_off == 0x0F510000);
        assert(s_d3d.draws[draws].clear_mask == 0xf3);
        assert(s_d3d.draws[draws + 1].zeta_w == 1280);
    }
    // Only the actual flip presents the completed batch.
    d3d12_present(nullptr, 0);
    assert(presents == before + 1);
}
int main()
{
    assert(MAX_DRAWS >= 4098);
    _putenv_s("RSX_ACCUM_FRAME", "1");
    submit_frame(1800);
    for (unsigned i = 0; i < 120; ++i) submit_frame(40);
    assert(presents == 121 && s_clear_presents == 0);
    rsx_state native = {0x40, 0x0F510000, 1280, 720};
    s_d3d.current_rsx_state = &native;
    for (unsigned i = 0; i < 120; ++i) submit_frame(40);
    assert(presents == 241 && s_clear_presents == 0);
    submit_frame(4096);
    assert(presents == 242);
    puts("PASS: a large arena followed by 120 small menu frames presents exactly once per flip");
}
