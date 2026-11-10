#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint32_t u32;
typedef uint8_t u8;
typedef int DXGI_FORMAT;
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#define DXGI_FORMAT_R24_UNORM_X8_TYPELESS 46
#define D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING 5768
#define PS3_LOG_FIRST(counter, limit) ((counter)++ < (limit))
struct rsx_fp_shadow_desc { u8 enabled, func, linear, unnormalized; };
struct Texture {
    u32 set, w, h, raw, off, fmt, cube, mips, sampler_filter, sampler_address, sampler_control;
    u32 ctrl1, pitch;
};
struct Draw {
    Texture tex[1]; int tex_rt[1]; u32 fp_addr; u32 sampled_units = 1;
    int tex_depth[1], zeta_slot = -1; u32 depth_key = 0; rsx_fp_shadow_desc shadow[1];
    u32 packed_depth_units = 0;
};
struct PackedDepthResolve { u32 src, ready_draw, gen; };
static const PackedDepthResolve* find_packed_resolve(u32, u32, u32, u32) { return nullptr; }
static bool packed_depth_range_unchanged(u32, u32, u32, u32) { return false; }
static u32 cellGcmResolveLocated(int local, u32 raw) { return (local ? 0xC0000000u : 0x11100000u) + raw; }
static int native_depth;
static struct { struct { void* resource; } surfaces[1]; } s_depth_pool = {{{&native_depth}}};
static int depth_pool_find(u32 raw, u32, u32) { return raw == 0x0F100000 ? 0 : -1; }
static void rsx_fp_shadow_desc_from_texture(rsx_fp_shadow_desc* d, u32, u32, u32, u32, u32, u32, u32)
{ d->enabled = 1; }
static int rt_resource, screen_resource, sub_resource;
static struct {
    u32 width, height;
    struct { void* res; u32 dxgi; } off_rt[1];
    struct { u32 zeta_gen; } draws[1];
}
    s_d3d = {1280, 720, {{&rt_resource, 28}}};
static void* s_screen_copy = &screen_resource;
static int s_subvp_n = 0;
static struct { void* res; u32 w, h; } s_subvp[1] = {{&sub_resource, 640, 360}};
uint8_t* vm_base = nullptr;
static void* chosen;
static bool guest_has_data;
static bool tex_guest_has_data(u32, u32, u32, u32, int, u32, u32) { return guest_has_data; }
static int off_rt_find(u32 raw) { return raw == 0x02B50000 ? 0 : -1; }
static int off_rt_alias_find(u32 raw) { return raw == 0x0DF60000 ? 0 : -1; }
static void srv_write(u32, void* resource, DXGI_FORMAT, u32) { chosen = resource; }

static void select(Draw* dr)
{
    const int packed_depth = 0;
    const u32 _d = 0;
    const u32 zeta_writer[1] = {};
    chosen = nullptr;
    for (int _u = 0; _u < 1; ++_u) {
        u32 wslot = 0;
        #include "texture_source.inc"
        } // upload fallback branch opened by the extracted prefix
    }
}

static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main()
{
    Draw draw = {{{1, 1280, 720, 0x02B50000, 0}}, {-1}, 0x0215F801};
    select(&draw);
    require(chosen == &rt_resource && draw.tex_rt[0] == 0,
            "explicit screen-sized render target must win over previous-screen heuristic");
    draw.tex[0].raw = 0x0DF60000;
    select(&draw);
    require(chosen == &rt_resource && draw.tex_rt[0] == 0,
            "explicit resolve alias must win over previous-screen heuristic");
    draw.tex[0].raw = 0x01033D00;
    draw.tex[0].off = cellGcmResolveLocated(0, draw.tex[0].raw);
    select(&draw);
    require(chosen == nullptr && draw.tex_rt[0] == -1,
            "transparent main-memory overlay keeps its guest source");
    draw.tex[0].off = cellGcmResolveLocated(1, draw.tex[0].raw);
    select(&draw);
    require(chosen == &screen_resource && draw.tex_rt[0] == -1,
            "empty local screen-sized source retains previous-screen fallback");
    guest_has_data = true;
    select(&draw);
    require(chosen == nullptr, "populated local texture keeps its guest source");
    guest_has_data = false;
    draw.sampled_units = 0;
    draw.tex[0].raw = 0x02B50000;
    select(&draw);
    require(chosen == nullptr && draw.tex_rt[0] == -1,
            "unused texture bindings do not consume cache resources or feedback targets");
    draw.sampled_units = 1; draw.tex[0].raw = 0x0F100000; draw.tex[0].fmt = 0x90;
    select(&draw);
    require(chosen == &native_depth && draw.tex_depth[0] == 0 && draw.shadow[0].enabled == 1,
            "depth textures sample their native producer with comparison metadata");
    draw.zeta_slot = 0; draw.depth_key = 3;
    select(&draw);
    require(chosen != &native_depth && draw.tex_depth[0] == -1,
            "a depth-writing draw cannot simultaneously sample its own native depth surface");
    puts("PASS: explicit RT and resolve producers precede heuristic texture sources");
}
