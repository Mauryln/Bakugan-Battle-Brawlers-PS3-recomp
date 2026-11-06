#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "rsx_d3d12_backend.h"
#include "output_upscale.h"

#define A_CPU 1
#include "third_party/fsr1/ffx_a.h"
#include "third_party/fsr1/ffx_fsr1.h"

using Microsoft::WRL::ComPtr;
namespace {
struct fsr_output {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> easu, rcas;
    ComPtr<ID3D12DescriptorHeap> srv_heap, rtv_heap;
    ComPtr<ID3D12Resource> intermediate;
    ID3D12Resource* inputs[2]{};
    ID3D12Resource* outputs[2]{};
    UINT srv_step{}, rtv_step{}, constants[24]{};
    UINT width = 1920, height = 1080;
    D3D12_RESOURCE_STATES intermediate_state = D3D12_RESOURCE_STATE_COMMON;
};
fsr_output output;

bool failed(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return false;
    std::fprintf(stderr, "[FSR1] %s failed: %08lX\n", operation, static_cast<unsigned long>(hr));
    return true;
}
D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle(ID3D12DescriptorHeap* heap, UINT step, UINT index) {
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(step) * index;
    return handle;
}
D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle(UINT index) {
    auto handle = output.srv_heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += UINT64(output.srv_step) * index;
    return handle;
}
void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}
bool compile(const std::wstring& path, const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &errors);
    if (FAILED(hr) && errors)
        std::fprintf(stderr, "[FSR1] %s: %.*s\n", entry, int(errors->GetBufferSize()),
                     static_cast<const char*>(errors->GetBufferPointer()));
    return !failed(hr, entry);
}
int initialize(void*, void* device, void*, u32 input_width, u32 input_height) {
    output.device = static_cast<ID3D12Device*>(device);
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.Num32BitValues = 24;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2; desc.pParameters = parameters;
    desc.NumStaticSamplers = 1; desc.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> serialized, errors;
    if (failed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                          &serialized, &errors), "root serialization")) return -1;
    if (failed(output.device->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&output.root)), "root signature")) return -1;
    wchar_t executable[32768];
    DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) return -1;
    std::wstring path(executable, length);
    auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return -1;
    path.resize(slash + 1); path += L"shaders\\fsr1\\present.hlsl";
    ComPtr<ID3DBlob> vs, easu, rcas;
    if (!compile(path, "vs_main", "vs_5_0", vs) ||
        !compile(path, "easu_main", "ps_5_0", easu) ||
        !compile(path, "rcas_main", "ps_5_0", rcas)) return -1;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = output.root.Get();
    pipeline.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pipeline.PS = {easu->GetBufferPointer(), easu->GetBufferSize()};
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.SampleMask = UINT_MAX;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.SampleDesc.Count = 1;
    if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.easu)), "EASU pipeline")) return -1;
    pipeline.PS = {rcas->GetBufferPointer(), rcas->GetBufferSize()};
    if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.rcas)), "RCAS pipeline")) return -1;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 3;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (failed(output.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&output.srv_heap)), "SRV heap")) return -1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (failed(output.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&output.rtv_heap)), "RTV heap")) return -1;
    output.srv_step = output.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    output.rtv_step = output.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_HEAP_PROPERTIES properties{}; properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = output.width; texture.Height = output.height;
    texture.DepthOrArraySize = 1; texture.MipLevels = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (failed(output.device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
            &texture, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&output.intermediate)), "EASU target")) return -1;
    output.device->CreateRenderTargetView(output.intermediate.Get(), nullptr,
        cpu_handle(output.rtv_heap.Get(), output.rtv_step, 2));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    output.device->CreateShaderResourceView(output.intermediate.Get(), &srv,
        cpu_handle(output.srv_heap.Get(), output.srv_step, 2));
    FsrEasuCon(output.constants, output.constants + 4, output.constants + 8, output.constants + 12,
        float(input_width), float(input_height), float(input_width), float(input_height),
        float(output.width), float(output.height));
    FsrRcasCon(output.constants + 16, 0.5f);
    output.constants[20] = output.width; output.constants[21] = output.height;
    std::fprintf(stderr, "[FSR1] EASU+RCAS ready: %ux%u -> %ux%u, FP32\n",
                 input_width, input_height, output.width, output.height);
    return 0;
}
int record(void*, void* command_list, void* input, void* destination, u32 frame) {
    if (frame >= 2) return -1;
    auto* list = static_cast<ID3D12GraphicsCommandList*>(command_list);
    auto* source = static_cast<ID3D12Resource*>(input);
    auto* target = static_cast<ID3D12Resource*>(destination);
    if (output.inputs[frame] != source) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        output.device->CreateShaderResourceView(source, &srv,
            cpu_handle(output.srv_heap.Get(), output.srv_step, frame));
        output.inputs[frame] = source;
    }
    if (output.outputs[frame] != target) {
        output.device->CreateRenderTargetView(target, nullptr,
            cpu_handle(output.rtv_heap.Get(), output.rtv_step, frame));
        output.outputs[frame] = target;
    }
    transition(list, source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(list, output.intermediate.Get(), output.intermediate_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap* heaps[] = {output.srv_heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(output.root.Get());
    list->SetGraphicsRoot32BitConstants(1, 24, output.constants, 0);
    D3D12_VIEWPORT viewport{0, 0, float(output.width), float(output.height), 0, 1};
    D3D12_RECT scissor{0, 0, LONG(output.width), LONG(output.height)};
    list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto rtv = cpu_handle(output.rtv_heap.Get(), output.rtv_step, 2);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetPipelineState(output.easu.Get());
    list->SetGraphicsRootDescriptorTable(0, gpu_handle(frame));
    list->DrawInstanced(3, 1, 0, 0);
    transition(list, output.intermediate.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    output.intermediate_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transition(list, target, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv = cpu_handle(output.rtv_heap.Get(), output.rtv_step, frame);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetPipelineState(output.rcas.Get());
    list->SetGraphicsRootDescriptorTable(0, gpu_handle(2));
    list->DrawInstanced(3, 1, 0, 0);
    transition(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    return 0;
}
void shutdown(void*) { output = fsr_output{}; }
}

int bakugan_output_configure() {
    const char* window_mode = std::getenv("PS3_WINDOW_MODE");
    if (window_mode && *window_mode && std::strcmp(window_mode, "windowed") &&
        std::strcmp(window_mode, "borderless")) {
        std::fprintf(stderr, "[OUTPUT] PS3_WINDOW_MODE must be windowed or borderless\n"); return -1;
    }
    const char* render_height = std::getenv("PS3_RENDER_HEIGHT");
    if (render_height && *render_height && std::strcmp(render_height, "720")) {
        std::fprintf(stderr, "[OUTPUT] 540p virtual mode disabled: title HUD retains 720p coordinates. Use 720p.\n");
        return -1;
    }
    const char* setting = std::getenv("PS3_FSR");
    if (!setting || !*setting || !std::strcmp(setting, "0")) return 0;
    if (std::strcmp(setting, "1") && std::strcmp(setting, "fsr1")) {
        std::fprintf(stderr, "[FSR1] PS3_FSR must be 0 or fsr1\n"); return -1;
    }
    rsx_d3d12_output_pass pass{output.width, output.height, nullptr, initialize, record, shutdown};
    return rsx_d3d12_backend_set_output_pass(&pass);
}
