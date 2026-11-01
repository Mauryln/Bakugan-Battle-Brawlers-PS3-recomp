#define CINTERFACE
#include <initguid.h>
#include <d3d12.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
using u32 = uint32_t;
#include "depth_pool.inc"

static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main()
{
    ID3D12Device* device = nullptr;
    require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device, (void**)&device)), "create device");
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 1 + 2 * MAX_DEPTH_SURFACES;
    ID3D12DescriptorHeap* heap = nullptr;
    require(SUCCEEDED(device->lpVtbl->CreateDescriptorHeap(device, &hd,
        IID_ID3D12DescriptorHeap, (void**)&heap)), "create depth view heap");
    s_depth_pool.device = device; s_depth_pool.dsv_heap = heap;
    s_depth_pool.dsv_increment = device->lpVtbl->GetDescriptorHandleIncrementSize(device, hd.Type);
    int a = depth_pool_get(0xF100000, 16, 16), b = depth_pool_get(0xE460000, 16, 16);
    int resized = depth_pool_get(0xF100000, 8, 8);
    require(a >= 0 && b >= 0 && a != b && resized != a, "surface address and extent identify native resources");
    require(depth_pool_get(0xF100000, 16, 16) == a, "same surface keeps its resource");
    ID3D12CommandQueue* queue = nullptr; D3D12_COMMAND_QUEUE_DESC qd = {};
    require(SUCCEEDED(device->lpVtbl->CreateCommandQueue(device, &qd, IID_ID3D12CommandQueue, (void**)&queue)), "create queue");
    ID3D12CommandAllocator* allocator = nullptr;
    require(SUCCEEDED(device->lpVtbl->CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator, (void**)&allocator)), "create allocator");
    ID3D12GraphicsCommandList* commands = nullptr;
    require(SUCCEEDED(device->lpVtbl->CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, nullptr, IID_ID3D12GraphicsCommandList, (void**)&commands)), "create command list");
    depth_pool_transition(commands, a, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commands->lpVtbl->ClearDepthStencilView(commands, depth_pool_dsv(a, 0), D3D12_CLEAR_FLAG_DEPTH, .25f, 0, 0, nullptr);
    D3D12_RECT half = {0, 0, 8, 16};
    commands->lpVtbl->ClearDepthStencilView(commands, depth_pool_dsv(a, 0), D3D12_CLEAR_FLAG_DEPTH, .5f, 0, 1, &half);
    depth_pool_transition(commands, b, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commands->lpVtbl->ClearDepthStencilView(commands, depth_pool_dsv(b, 0), D3D12_CLEAR_FLAG_DEPTH, .75f, 0, 0, nullptr);
    depth_pool_transition(commands, a, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    depth_pool_transition(commands, a, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    depth_pool_transition(commands, b, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    require(depth_pool_find(0xF100000, 16, 16) == a && depth_pool_find(0xE460000, 16, 16) == b,
        "sampling preserves depth surface identity");
    ID3D12Resource* readback[2] = {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2] = {};
    int slots[] = {a, b};
    for (u32 i = 0; i < 2; ++i) {
        auto* resource = s_depth_pool.surfaces[slots[i]].resource;
        D3D12_RESOURCE_DESC desc; resource->lpVtbl->GetDesc(resource, &desc);
        UINT64 bytes;
        device->lpVtbl->GetCopyableFootprints(device, &desc, 0, 1, 0, &footprints[i], nullptr, nullptr, &bytes);
        printf("depth plane footprint format=%u pitch=%u\n", (unsigned)footprints[i].Footprint.Format,
            footprints[i].Footprint.RowPitch);
        D3D12_HEAP_PROPERTIES props = {}; props.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer = {}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes; buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        require(SUCCEEDED(device->lpVtbl->CreateCommittedResource(device, &props, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource, (void**)&readback[i])), "create readback");
        depth_pool_transition(commands, slots[i], D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
        src.pResource = resource; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback[i]; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprints[i];
        commands->lpVtbl->CopyTextureRegion(commands, &dst, 0, 0, 0, &src, nullptr);
    }
    require(SUCCEEDED(commands->lpVtbl->Close(commands)), "close native depth work");
    ID3D12CommandList* lists[] = {(ID3D12CommandList*)commands};
    queue->lpVtbl->ExecuteCommandLists(queue, 1, lists);
    ID3D12Fence* fence = nullptr;
    require(SUCCEEDED(device->lpVtbl->CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&fence)), "create fence");
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    require(SUCCEEDED(queue->lpVtbl->Signal(queue, fence, 1)), "signal completed work");
    require(SUCCEEDED(fence->lpVtbl->SetEventOnCompletion(fence, 1, event)), "wait for native depth work");
    require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "GPU completes independent depth passes");
    for (u32 i = 0; i < 2; ++i) {
        void* mapped = nullptr;
        require(SUCCEEDED(readback[i]->lpVtbl->Map(readback[i], 0, nullptr, &mapped)), "read native depth plane");
        float expected = i ? .75f : .25f;
        for (u32 y = 0; y < 16; ++y) for (u32 x = 0; x < 16; ++x) {
            u32 raw; memcpy(&raw, (char*)mapped + y * footprints[i].Footprint.RowPitch + x * 4, 4);
            float depth = float(raw & 0xFFFFFFu) / 16777215.0f;
            require(fabsf(depth - (!i && x < 8 ? .5f : expected)) < 0.00001f, "switching surfaces or sampling must not erase a previous depth pass");
        }
        readback[i]->lpVtbl->Unmap(readback[i], 0, nullptr); readback[i]->lpVtbl->Release(readback[i]);
    }
    for (u32 i = 3; i < MAX_DEPTH_SURFACES; ++i)
        require(depth_pool_get(0x100000 + i * 0x10000, 16, 16) >= 0, "fill independent depth pool");
    require(depth_pool_get(0x9990000, 16, 16) == -1, "never evict a surface pinned by this frame");
    s_depth_pool.surfaces[b].used = 0;
    require(depth_pool_get(0x9990000, 16, 16) == b, "reuse only a retired surface");
    require(depth_pool_find(0xF100000, 16, 16) == a, "eviction retains active shadow map");
    CloseHandle(event); fence->lpVtbl->Release(fence); commands->lpVtbl->Release(commands);
    allocator->lpVtbl->Release(allocator); queue->lpVtbl->Release(queue);
    depth_pool_shutdown(); heap->lpVtbl->Release(heap); device->lpVtbl->Release(device);
    puts("PASS: distinct native depth surfaces survive target switches and SRV transitions");
}
