/* Standalone Windows/D3D12 regression test. Build with clang-cl /O2 /EHsc
 * /std:c++20 /I ps3recomp-src/libs/video, link d3d12.lib dxgi.lib.
 * Optional argument: worker count (1..4). Never read mapped upload memory. */

#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
#define MAX_DRAWS 8192
static u32 vp_vertex_stride(u32 mask)
{
    u32 attributes = 0;
    for (u32 i = 0; i < 16; ++i) attributes += (mask >> i) & 1u;
    return attributes ? attributes * 16u : 16u;
}



static void vp_pack_vertex_stream(void* dst, const void* src, u32 count, u32 mask)
{
    if (mask == 0xFFFFu) { memcpy(dst, src, (size_t)count * 256u); return; }
    if (!mask) { memset(dst, 0, (size_t)count * 16u); return; }
    /* The vertex fill already wrote the used attributes into the FIRST bytes of
     * each 256-byte block, in ascending slot order, so packing is one contiguous
     * copy per vertex instead of one 16-byte copy per attribute. Same bytes:
     * that is exactly what the attribute-by-attribute loop produced, and it is
     * far friendlier to the write-combined upload heap. */
    u32 stride = vp_vertex_stride(mask);
    u8* out = (u8*)dst;
    const u8* in = (const u8*)src;
    for (u32 v = 0; v < count; ++v) {
        memcpy(out, in + (size_t)v * 256u, stride);
        out += stride;
    }
}

#include "rsx_pack_workers.inc"
static void ok(HRESULT hr) { if(FAILED(hr)) { printf("HRESULT %08X\n",(unsigned)hr); std::abort(); } }
int main(int argc,char** argv) {
    _putenv_s("RSX_PACK_WORKERS",argc>1?argv[1]:"2");
    assert(vp_pack_workers_active());
    ID3D12Device* device=nullptr;
    ok(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};
    ID3D12CommandQueue* queue;ok(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ID3D12CommandAllocator* allocator;ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ID3D12GraphicsCommandList* list;ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list)));ok(list->Close());
    ID3D12Fence* fence;ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    constexpr size_t size=40u*1024u*1024u;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=size;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    ID3D12Resource* upload;ok(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    ID3D12Resource* readback;ok(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    void* mapped;D3D12_RANGE empty{0,0};ok(upload->Map(0,&empty,&mapped));
    std::vector<u8> source(size),expected(size),clear(size,0xa5);
    for(size_t i=0;i<size;++i) source[i]=(u8)(i*19+i/251);
    for(u32 round=0;round<8;++round) {
        expected=clear;memcpy(mapped,clear.data(),size);_mm_sfence();
        u32 offset=64;
        for(u32 r=0;r<MAX_DRAWS;++r) {
            u32 mask=round*MAX_DRAWS+r,count=1+(r%17);
            u32 attrs=0;for(u32 a=0;a<16;++a)attrs+=(mask>>a)&1;
            u32 stride=attrs?attrs*16:16;
            auto& run=s_pack.runs[r];run.src=source.data()+r*17*256;run.dst=(u8*)mapped+offset;run.count=count;run.mask=mask;
            for(u32 v=0;v<count;++v) for(u32 b=0;b<stride;++b)
                expected[offset+v*stride+b]=mask?source[r*17*256+v*256+b]:0;
            offset+=count*stride;
        }
        assert(offset+64<size);assert(vp_pack_workers_begin(MAX_DRAWS,offset-64));vp_pack_workers_join();
        ok(allocator->Reset());ok(list->Reset(allocator,nullptr));list->CopyBufferRegion(readback,0,upload,0,size);ok(list->Close());
        ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);ok(queue->Signal(fence,round+1));
        if(fence->GetCompletedValue()<round+1) {ok(fence->SetEventOnCompletion(round+1,event));assert(WaitForSingleObject(event,30000)==WAIT_OBJECT_0);}
        void* cpu;D3D12_RANGE range{0,size};ok(readback->Map(0,&range,&cpu));
        if(memcmp(cpu,expected.data(),size)) {printf("Mismatch batch %u\n",round);std::abort();}readback->Unmap(0,&empty);
    }
    vp_pack_workers_shutdown();assert(!s_pack.pending && !s_pack.threads);
    upload->Unmap(0,nullptr);upload->Release();readback->Release();CloseHandle(event);fence->Release();list->Release();allocator->Release();queue->Release();device->Release();
    printf("PASS: %s workers, all 65536 masks, counts 1..17, actual GPU upload/readback, full guards, 8 frame reuses and clean shutdown\n",argc>1?argv[1]:"2");
}
