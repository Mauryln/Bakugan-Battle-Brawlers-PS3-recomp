#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint32_t u32;
typedef uint8_t u8;
typedef int DXGI_FORMAT;
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#define D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING 5768
#define PS3_LOG_FIRST(counter, limit) ((counter)++ < (limit))
struct Texture { u32 set, w, h, raw, off; };
struct Draw { Texture tex[1]; int tex_rt[1]; u32 fp_addr; };
static int rt_resource, screen_resource, sub_resource;
static struct { u32 width, height; struct { void* res; u32 dxgi; } off_rt[1]; }
    s_d3d = {1280, 720, {{&rt_resource, 28}}};
static void* s_screen_copy = &screen_resource;
static int s_subvp_n = 0;
static struct { void* res; u32 w, h; } s_subvp[1] = {{&sub_resource, 640, 360}};
uint8_t* vm_base = nullptr;
static void* chosen;
static int off_rt_find(u32 raw) { return raw == 0x02B50000 ? 0 : -1; }
static int off_rt_alias_find(u32 raw) { return raw == 0x0DF60000 ? 0 : -1; }
static void srv_write(u32, void* resource, DXGI_FORMAT, u32) { chosen = resource; }

static void select(Draw* dr)
{
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
    draw.tex[0].raw = 0x12345678;
    select(&draw);
    require(chosen == &screen_resource && draw.tex_rt[0] == -1,
            "unknown screen-sized source retains previous-screen fallback");
    puts("PASS: explicit RT and resolve producers precede heuristic texture sources");
}
