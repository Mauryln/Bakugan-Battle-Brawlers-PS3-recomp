#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL: %s\n", #expr); exit(1); } } while (0)

typedef struct {
    int is_vp, is_clear;
    u32 zeta_off, zeta_w, zeta_h, zeta_fmt, depth_key, clear_mask;
} D3D12DrawRecord;
static struct { D3D12DrawRecord draws[4]; u32 draw_count; } s_d3d;
static u32 g_guest_gen = 3;
static u32 g_guest_page_gen[1u << 20];

#include "packed_depth_resolve.inc"

int main(void)
{
    const u32 src = 0x180000, dst = 0x280000, w = 1120, h = 584;
    rsx_d3d12_backend_depth_resolve_tile(dst, src, w, h, 512);
    CHECK(s_depth_resolve_count == 0);
    s_d3d.draws[0] = (D3D12DrawRecord){.is_vp=1, .zeta_off=src, .zeta_w=w,
                                      .zeta_h=h, .zeta_fmt=2, .depth_key=3};
    s_d3d.draw_count = 1;
    rsx_d3d12_backend_depth_resolve_tile(dst, src, w, h, 512);
    rsx_d3d12_backend_depth_resolve_tile(dst + 2048, src + 4096, w, h, 512);
    CHECK(find_packed_resolve(dst, w, h, 1) == NULL);
    rsx_d3d12_backend_depth_resolve_tile(dst + 4096, src + 8192, w, h, 95);
    CHECK(find_packed_resolve(dst, w, h, 1) == NULL);
    rsx_d3d12_backend_depth_resolve_tile(dst + 4096, src + 8192, w, h, 96);
    CHECK(find_packed_resolve(dst, w, h, 0) == NULL);
    CHECK(find_packed_resolve(dst + 4, w, h, 1) == NULL);
    CHECK(find_packed_resolve(dst, w, h + 1, 1) == NULL);
    const PackedDepthResolve* resolve = find_packed_resolve(dst, w, h, 1);
    CHECK(resolve && resolve->src == src && resolve->gen == 3);
    const u32 ea = 0xC0000000u + dst;
    CHECK(packed_depth_range_unchanged(ea, w, h, resolve->gen));
    g_guest_page_gen[(ea >> 12) + 8] = ++g_guest_gen;
    CHECK(!packed_depth_range_unchanged(ea, w, h, resolve->gen));
    puts("PASS: depth resolve requires all tiles and rejects overwritten guest pages");
    return 0;
}
