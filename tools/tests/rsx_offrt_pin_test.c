#define _CRT_SECURE_NO_WARNINGS
#define COBJMACROS
#include <initguid.h>
#include <d3d12.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL: %s\n", #expr); exit(1); } } while (0)

#define MAX_OFF_RTS 4
#define MAX_DRAWS 8
#define RT_SRV_BASE 5
#define RSX_MAX_TEXTURES 4
#define PS3_LOG_FIRST(counter, limit) ((counter) < (limit) ? (++(counter), 1) : 0)

typedef struct {
    ID3D12Resource* res;
    ID3D12Resource* up;
    u32 off, w, h, dxgi, st;
    int used;
    u64 last_used;
} OffRT;

typedef struct {
    int is_vp, is_clear;
    u32 rt_off, rt_mrt[3], rt_w, rt_h, rt_fmt;
    u32 sampled_units;
    struct { int set; u32 raw; } tex[RSX_MAX_TEXTURES];
} D3D12DrawRecord;

static struct {
    ID3D12Device* device;
    ID3D12GraphicsCommandList* cmd_list;
    ID3D12DescriptorHeap* rt_rtv_heap;
    ID3D12DescriptorHeap* srv_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE rt_rtv_heap_cpu_base;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_heap_cpu_base;
    u32 rtv_descriptor_size, srv_inc, width, height;
    OffRT off_rt[MAX_OFF_RTS];
    D3D12DrawRecord draws[MAX_DRAWS];
    u32 draw_count;
} s_d3d;

static u32 g_guest_page_gen[1u << 20];
static u8 g_test_vm[1u << 22];
u8* vm_base = g_test_vm;
static u8 g_upload_buf[256 * 256 * 4];

static u32 cellGcmResolveOffset(u32 offset)
{
    return offset ? (offset + 0x00200000u) : 0u;
}

static u32 rsx_surface_dxgi(u32 rsx_fmt)
{
    (void)rsx_fmt;
    return DXGI_FORMAT_R8G8B8A8_UNORM;
}

static void wait_for_gpu(void) {}

typedef struct { ID3D12ResourceVtbl* lpVtbl; } MockRes;
typedef struct { ID3D12DeviceVtbl* lpVtbl; } MockDev;
typedef struct { ID3D12GraphicsCommandListVtbl* lpVtbl; } MockCmd;

static ULONG STDMETHODCALLTYPE mock_release(ID3D12Resource* self) { (void)self; return 0; }
static HRESULT STDMETHODCALLTYPE mock_map(ID3D12Resource* self, UINT sub,
    const D3D12_RANGE* rng, void** data)
{
    (void)self; (void)sub; (void)rng;
    if (data) *data = g_upload_buf;
    return S_OK;
}
static void STDMETHODCALLTYPE mock_unmap(ID3D12Resource* self, UINT sub, const D3D12_RANGE* rng)
{
    (void)self; (void)sub; (void)rng;
}

static ID3D12ResourceVtbl g_res_vtbl = { .Release = mock_release, .Map = mock_map, .Unmap = mock_unmap };
static MockRes g_res_pool[32];
static u32 g_res_next = 0;

static HRESULT STDMETHODCALLTYPE mock_create_res(ID3D12Device* self,
    const D3D12_HEAP_PROPERTIES* hp, D3D12_HEAP_FLAGS hf, const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES st, const D3D12_CLEAR_VALUE* cv, REFIID iid, void** out)
{
    (void)self; (void)hp; (void)hf; (void)desc; (void)st; (void)cv; (void)iid;
    MockRes* r = &g_res_pool[g_res_next++ & 31u];
    r->lpVtbl = &g_res_vtbl;
    *out = r;
    return S_OK;
}
static void STDMETHODCALLTYPE mock_create_rtv(ID3D12Device* self, ID3D12Resource* r,
    const D3D12_RENDER_TARGET_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    (void)self; (void)r; (void)d; (void)h;
}
static void STDMETHODCALLTYPE mock_create_srv(ID3D12Device* self, ID3D12Resource* r,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* d, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    (void)self; (void)r; (void)d; (void)h;
}
static void STDMETHODCALLTYPE mock_barrier(ID3D12GraphicsCommandList* self, UINT n,
    const D3D12_RESOURCE_BARRIER* b) { (void)self; (void)n; (void)b; }
static void STDMETHODCALLTYPE mock_copy_tex(ID3D12GraphicsCommandList* self,
    const D3D12_TEXTURE_COPY_LOCATION* dst, UINT dx, UINT dy, UINT dz,
    const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* box)
{
    (void)self; (void)dst; (void)dx; (void)dy; (void)dz; (void)src; (void)box;
}

static ID3D12DeviceVtbl g_dev_vtbl = {
    .CreateCommittedResource = mock_create_res,
    .CreateRenderTargetView = mock_create_rtv,
    .CreateShaderResourceView = mock_create_srv,
};
static ID3D12GraphicsCommandListVtbl g_cmd_vtbl = {
    .ResourceBarrier = mock_barrier,
    .CopyTextureRegion = mock_copy_tex,
};
static MockDev g_dev = { &g_dev_vtbl };
static MockCmd g_cmd = { &g_cmd_vtbl };

#include "offrt_pin.inc"

int main(void)
{
    s_d3d.device = (ID3D12Device*)&g_dev;
    s_d3d.cmd_list = (ID3D12GraphicsCommandList*)&g_cmd;
    s_d3d.rt_rtv_heap = (ID3D12DescriptorHeap*)1;
    s_d3d.srv_heap = (ID3D12DescriptorHeap*)1;
    s_d3d.rtv_descriptor_size = 32;
    s_d3d.srv_inc = 32;
    s_d3d.width = 1280;
    s_d3d.height = 720;

    /* Frame 1: fill all 4 offscreen RTs (0x010000 is the portrait written once). */
    for (u32 i = 0; i < 4; i++) {
        s_d3d.draws[i] = (D3D12DrawRecord){
            .is_vp = 1, .rt_off = 0x00010000u * (i + 1u), .rt_w = 64, .rt_h = 64, .rt_fmt = 5
        };
    }
    s_d3d.draw_count = 4;
    off_rt_prepass();
    CHECK(off_rt_find(0x00010000u) == 0);

    /* Frame 2: only sample 0x010000 while updating 0x020000 and introducing 0x050000. */
    s_d3d.draws[0] = (D3D12DrawRecord){
        .is_vp = 1, .rt_off = 0x00020000u, .rt_w = 64, .rt_h = 64, .rt_fmt = 5,
        .sampled_units = 1u, .tex = { { .set = 1, .raw = 0x00010000u } }
    };
    s_d3d.draws[1] = (D3D12DrawRecord){
        .is_vp = 1, .rt_off = 0x00050000u, .rt_w = 64, .rt_h = 64, .rt_fmt = 5
    };
    s_d3d.draw_count = 2;
    off_rt_prepass();
    CHECK(off_rt_find(0x00010000u) == 0);
    CHECK(off_rt_find(0x00050000u) == 2);

    /* Frame 3: sample 0x010000 indirectly through a resolve alias at 0x00100000,
     * bump raw offset page (must NOT invalidate), then introduce 0x060000. */
    s_rt_alias[0].dst = 0x00100000u;
    s_rt_alias[0].src = 0x00010000u;
    s_rt_alias[0].gen = 1u;
    s_rt_alias_n = 1;
    g_guest_page_gen[0x00100000u >> 12] = 99u;
    CHECK(off_rt_alias_find(0x00100000u) == 0);

    s_d3d.draws[0] = (D3D12DrawRecord){
        .is_vp = 1, .rt_off = 0x00020000u, .rt_w = 64, .rt_h = 64, .rt_fmt = 5,
        .sampled_units = 1u, .tex = { { .set = 1, .raw = 0x00100000u } }
    };
    s_d3d.draws[1] = (D3D12DrawRecord){
        .is_vp = 1, .rt_off = 0x00060000u, .rt_w = 64, .rt_h = 64, .rt_fmt = 5
    };
    s_d3d.draw_count = 2;
    off_rt_prepass();
    CHECK(off_rt_find(0x00010000u) == 0);
    CHECK(off_rt_find(0x00060000u) == 3);

    /* Resolved guest EA write invalidates the alias immediately. */
    g_guest_page_gen[cellGcmResolveOffset(0x00100000u) >> 12] = 2u;
    CHECK(off_rt_alias_find(0x00100000u) == -1);

    puts("PASS: sampled offscreen RTs stay pinned under LRU and alias checks resolved EA");
    return 0;
}
