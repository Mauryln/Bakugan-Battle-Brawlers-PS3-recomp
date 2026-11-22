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
constexpr u32 OUTPUT_FRAMES = 2;
constexpr u32 HUDLESS_DESCRIPTOR_BASE = 6;
struct fsr_output {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> smaa_edge, smaa_weight, smaa_blend, copy, easu, rcas;
    ComPtr<ID3D12DescriptorHeap> srv_heap, rtv_heap;
    ComPtr<ID3D12Resource> intermediate, smaa_edges, smaa_weights, smaa_color;
    ID3D12Resource* inputs[OUTPUT_FRAMES * 2]{};
    ID3D12Resource* outputs[OUTPUT_FRAMES * 2]{};
    UINT srv_step{}, rtv_step{}, constants[28]{};
    rsx_d3d12_output_rect content{};
    UINT width = 1920, height = 1080;
    UINT input_width = 1280, input_height = 720;
    float sharpness = 0.6f;
    bool fsr_enabled = true, aa_enabled = true, fsr_active = false;
    D3D12_RESOURCE_STATES intermediate_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES smaa_edges_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES smaa_weights_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES smaa_color_state = D3D12_RESOURCE_STATE_COMMON;
};
fsr_output output;

static void monitor_size(u32* width, u32* height)
{
    *width = 0; *height = 0;
    DEVMODEW mode{};
    mode.dmSize = sizeof mode;
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
        *width = mode.dmPelsWidth;
        *height = mode.dmPelsHeight;
    }
    if (!*width || !*height) { *width = 1920; *height = 1080; }
}

static void configure_output_size(u32* width, u32* height)
{
    u32 monitor_w = 0, monitor_h = 0;
    monitor_size(&monitor_w, &monitor_h);
    u32 wanted = 0;
    const char* value = std::getenv("PS3_OUTPUT");
    const bool auto_mode = !value || !*value || !_stricmp(value, "auto");
    if (auto_mode) {
        /* Release default: the largest common step the monitor can show,
         * never above it. A 4K panel gets 2160p instead of staying at 720p. */
        if (monitor_h >= 2160) wanted = 2160;
        else if (monitor_h >= 1440) wanted = 1440;
        else if (monitor_h >= 1080) wanted = 1080;
        else wanted = monitor_h;
    } else if (!_stricmp(value, "native")) {
        wanted = monitor_h;
    } else if (!_stricmp(value, "1080p")) {
        wanted = 1080;
    } else if (!_stricmp(value, "1440p")) {
        wanted = 1440;
    } else if (!_stricmp(value, "2160p") || !_stricmp(value, "4k")) {
        wanted = 2160;
    } else {
        wanted = (u32)atoi(value);
    }
    if (!wanted) wanted = monitor_h ? monitor_h : 1080;
    if (wanted < 480) wanted = 480;
    if (wanted > 8192) wanted = 8192;
    /* Never ask for a raster taller than the panel: DWM would downscale the
     * whole window and blur the image on smaller monitors. */
    if (monitor_h && wanted > monitor_h) wanted = monitor_h;
    const auto content = rsx_d3d12_fit_16_9(monitor_w, wanted);
    *width = content.width;
    *height = content.height;
}

static int internal_height_from_env(void)
{
    const char* value = std::getenv("PS3_INTERNAL");
    int height = 720;
    if (value && *value) {
        int parsed = atoi(value);
        if (parsed >= 480 && parsed <= 2160) height = parsed;
    }
    return height;
}

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
bool create_rt_2d(UINT w, UINT h, DXGI_FORMAT fmt, UINT heap_index, ComPtr<ID3D12Resource>& out,
                  const char* tag) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = w;
    texture.Height = h;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = fmt;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (failed(output.device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
            &texture, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)), tag))
        return false;
    output.device->CreateRenderTargetView(out.Get(), nullptr,
        cpu_handle(output.rtv_heap.Get(), output.rtv_step, heap_index));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = fmt;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    output.device->CreateShaderResourceView(out.Get(), &srv,
        cpu_handle(output.srv_heap.Get(), output.srv_step, heap_index));
    return true;
}
int resize(void*, u32 width, u32 height);

int initialize(void*, void* device, void*, u32 input_width, u32 input_height,
               u32 output_width, u32 output_height) {
    output.device = static_cast<ID3D12Device*>(device);
    output.input_width = input_width;
    output.input_height = input_height;
    D3D12_DESCRIPTOR_RANGE range0{};
    range0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range0.NumDescriptors = 1;
    range0.BaseShaderRegister = 0;
    D3D12_DESCRIPTOR_RANGE range1{};
    range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors = 1;
    range1.BaseShaderRegister = 1;
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range1;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants.Num32BitValues = 28;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int s = 0; s < 2; ++s) {
        samplers[s].Filter = s == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[s].AddressU = samplers[s].AddressV = samplers[s].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[s].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[s].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[s].ShaderRegister = (UINT)s;
        samplers[s].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3; desc.pParameters = parameters;
    desc.NumStaticSamplers = 2; desc.pStaticSamplers = samplers;
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
    ComPtr<ID3DBlob> vs;
    if (!compile(path, "vs_main", "vs_5_0", vs)) return -1;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = output.root.Get();
    pipeline.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.SampleMask = UINT_MAX;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.SampleDesc.Count = 1;
    if (output.aa_enabled) {
        ComPtr<ID3DBlob> edge_ps, weight_ps, blend_ps;
        if (!compile(path, "smaa_edge_main", "ps_5_0", edge_ps) ||
            !compile(path, "smaa_weight_main", "ps_5_0", weight_ps) ||
            !compile(path, "smaa_blend_main", "ps_5_0", blend_ps))
            return -1;
        pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8_UNORM;
        pipeline.PS = {edge_ps->GetBufferPointer(), edge_ps->GetBufferSize()};
        if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.smaa_edge)), "SMAA edge"))
            return -1;
        pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline.PS = {weight_ps->GetBufferPointer(), weight_ps->GetBufferSize()};
        if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.smaa_weight)), "SMAA weight"))
            return -1;
        pipeline.PS = {blend_ps->GetBufferPointer(), blend_ps->GetBufferSize()};
        if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.smaa_blend)), "SMAA blend"))
            return -1;
    }
    if (!output.aa_enabled) {
        ComPtr<ID3DBlob> copy;
        if (!compile(path, "copy_main", "ps_5_0", copy)) return -1;
        pipeline.PS = {copy->GetBufferPointer(), copy->GetBufferSize()};
        if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.copy)), "copy pipeline"))
            return -1;
    }
    if (output.fsr_enabled) {
        ComPtr<ID3DBlob> easu, rcas;
        if (!compile(path, "easu_main", "ps_5_0", easu) ||
            !compile(path, "rcas_main", "ps_5_0", rcas))
            return -1;
        pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline.PS = {easu->GetBufferPointer(), easu->GetBufferSize()};
        if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.easu)), "EASU pipeline"))
            return -1;
        pipeline.PS = {rcas->GetBufferPointer(), rcas->GetBufferSize()};
        if (failed(output.device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output.rcas)), "RCAS pipeline"))
            return -1;
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = HUDLESS_DESCRIPTOR_BASE + OUTPUT_FRAMES;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (failed(output.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&output.srv_heap)), "SRV heap")) return -1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (failed(output.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&output.rtv_heap)), "RTV heap")) return -1;
    output.srv_step = output.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    output.rtv_step = output.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (output.aa_enabled) {
        if (!create_rt_2d(input_width, input_height, DXGI_FORMAT_R8G8_UNORM, 3, output.smaa_edges, "SMAA edges") ||
            !create_rt_2d(input_width, input_height, DXGI_FORMAT_R8G8B8A8_UNORM, 4, output.smaa_weights, "SMAA weights"))
            return -1;
        if (output.fsr_enabled &&
            !create_rt_2d(input_width, input_height, DXGI_FORMAT_R8G8B8A8_UNORM, 5, output.smaa_color, "SMAA color"))
            return -1;
    }
    return resize(nullptr, output_width, output_height);
}

void set_viewport(ID3D12GraphicsCommandList* list, const rsx_d3d12_output_rect& rect) {
    const D3D12_VIEWPORT viewport{float(rect.x), float(rect.y), float(rect.width), float(rect.height), 0, 1};
    const D3D12_RECT scissor{LONG(rect.x), LONG(rect.y), LONG(rect.x + rect.width), LONG(rect.y + rect.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

int record_output(void* command_list, void* input, void* destination, u32 frame, bool hudless) {
    if (frame >= OUTPUT_FRAMES) return -1;
    const u32 descriptor = hudless ? HUDLESS_DESCRIPTOR_BASE + frame : frame;
    const u32 cache_slot = hudless ? OUTPUT_FRAMES + frame : frame;
    auto* list = static_cast<ID3D12GraphicsCommandList*>(command_list);
    auto* source = static_cast<ID3D12Resource*>(input);
    auto* target = static_cast<ID3D12Resource*>(destination);
    if (output.inputs[cache_slot] != source) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        output.device->CreateShaderResourceView(source, &srv,
            cpu_handle(output.srv_heap.Get(), output.srv_step, descriptor));
        output.inputs[cache_slot] = source;
    }
    if (output.outputs[cache_slot] != target) {
        output.device->CreateRenderTargetView(target, nullptr,
            cpu_handle(output.rtv_heap.Get(), output.rtv_step, descriptor));
        output.outputs[cache_slot] = target;
    }
    transition(list, source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[] = {output.srv_heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(output.root.Get());
    list->SetGraphicsRoot32BitConstants(2, 28, output.constants, 0);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (output.aa_enabled) {
        set_viewport(list, {0, 0, output.input_width, output.input_height});

        transition(list, output.smaa_edges.Get(), output.smaa_edges_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = cpu_handle(output.rtv_heap.Get(), output.rtv_step, 3);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->SetPipelineState(output.smaa_edge.Get());
        list->SetGraphicsRootDescriptorTable(0, gpu_handle(descriptor));
        list->DrawInstanced(3, 1, 0, 0);
        transition(list, output.smaa_edges.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        output.smaa_edges_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        transition(list, output.smaa_weights.Get(), output.smaa_weights_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtv = cpu_handle(output.rtv_heap.Get(), output.rtv_step, 4);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->SetPipelineState(output.smaa_weight.Get());
        list->SetGraphicsRootDescriptorTable(0, gpu_handle(3));
        list->DrawInstanced(3, 1, 0, 0);
        transition(list, output.smaa_weights.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        output.smaa_weights_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        if (output.fsr_active) {
            const UINT native_view[] = {output.input_width, output.input_height,
                                        output.input_width, output.input_height, 0, 0};
            list->SetGraphicsRoot32BitConstants(2, 6, native_view, 20);
            transition(list, output.smaa_color.Get(), output.smaa_color_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
            rtv = cpu_handle(output.rtv_heap.Get(), output.rtv_step, 5);
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            list->SetPipelineState(output.smaa_blend.Get());
            list->SetGraphicsRootDescriptorTable(0, gpu_handle(descriptor));
            list->SetGraphicsRootDescriptorTable(1, gpu_handle(4));
            list->DrawInstanced(3, 1, 0, 0);
            transition(list, output.smaa_color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            output.smaa_color_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            list->SetGraphicsRoot32BitConstants(2, 6, output.constants + 20, 20);
        }
    }

    if (output.fsr_active) {
        set_viewport(list, {0, 0, output.content.width, output.content.height});
        transition(list, output.intermediate.Get(), output.intermediate_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const auto intermediate = cpu_handle(output.rtv_heap.Get(), output.rtv_step, 2);
        list->OMSetRenderTargets(1, &intermediate, FALSE, nullptr);
        list->SetPipelineState(output.easu.Get());
        list->SetGraphicsRootDescriptorTable(0, gpu_handle(output.aa_enabled ? 5 : descriptor));
        list->DrawInstanced(3, 1, 0, 0);
        transition(list, output.intermediate.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        output.intermediate_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    set_viewport(list, output.content);
    transition(list, target, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto rtv = cpu_handle(output.rtv_heap.Get(), output.rtv_step, descriptor);
    if (output.content.width != output.width || output.content.height != output.height) {
        constexpr float black[] = {0, 0, 0, 1};
        list->ClearRenderTargetView(rtv, black, 0, nullptr);
    }
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    if (output.fsr_active) {
        list->SetPipelineState(output.rcas.Get());
        list->SetGraphicsRootDescriptorTable(0, gpu_handle(2));
    } else {
        list->SetPipelineState(output.aa_enabled ? output.smaa_blend.Get() : output.copy.Get());
        list->SetGraphicsRootDescriptorTable(0, gpu_handle(descriptor));
        if (output.aa_enabled) list->SetGraphicsRootDescriptorTable(1, gpu_handle(4));
    }
    list->DrawInstanced(3, 1, 0, 0);
    transition(list, source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    return 0;
}
int record(void*, void* commands, void* input, void* output, u32 frame) {
    return record_output(commands, input, output, frame, false);
}
int record_hudless(void*, void* commands, void* input, void* output, u32 frame) {
    return record_output(commands, input, output, frame, true);
}
int resize(void*, u32 width, u32 height) {
    const auto content = rsx_d3d12_fit_16_9(width, height);
    if (!content.width || !content.height) return -1;
    for (auto& cached : output.outputs) cached = nullptr;
    const bool use_fsr = output.fsr_enabled && content.width >= output.input_width &&
                         content.height >= output.input_height;
    if (use_fsr && (!output.intermediate || content.width != output.content.width ||
                   content.height != output.content.height)) {
        if (!create_rt_2d(content.width, content.height, DXGI_FORMAT_R8G8B8A8_UNORM, 2,
                          output.intermediate, "EASU target"))
            return -1;
        output.intermediate_state = D3D12_RESOURCE_STATE_COMMON;
    }
    output.width = width;
    output.height = height;
    output.content = content;
    output.fsr_active = use_fsr;
    FsrEasuCon(output.constants, output.constants + 4, output.constants + 8, output.constants + 12,
        float(output.input_width), float(output.input_height),
        float(output.input_width), float(output.input_height),
        float(content.width), float(content.height));
    FsrRcasCon(output.constants + 16, output.sharpness);
    output.constants[20] = content.width;
    output.constants[21] = content.height;
    output.constants[22] = output.input_width;
    output.constants[23] = output.input_height;
    output.constants[24] = content.x;
    output.constants[25] = content.y;
    return 0;
}
void shutdown(void*) { output = fsr_output{}; }
}

int bakugan_output_configure() {
    const char* window_mode = std::getenv("PS3_WINDOW_MODE");
    if (window_mode && *window_mode && _stricmp(window_mode, "windowed") &&
        _stricmp(window_mode, "borderless") && _stricmp(window_mode, "fullscreen")) {
        std::fprintf(stderr, "[OUTPUT] PS3_WINDOW_MODE must be windowed, borderless or fullscreen\n");
        return -1;
    }

    configure_output_size(&output.width, &output.height);

    int internal_height = internal_height_from_env();
    const char* setting = std::getenv("PS3_FSR");
    int enable = output.height > (UINT)internal_height;
    if (setting && *setting) {
        if (!std::strcmp(setting, "0") || !_stricmp(setting, "off"))
            enable = 0;
        else if (!std::strcmp(setting, "1") || !_stricmp(setting, "fsr1") ||
                 !_stricmp(setting, "on"))
            enable = 1;
        else if (!_stricmp(setting, "auto"))
            enable = output.height > (UINT)internal_height;
        else {
            std::fprintf(stderr, "[FSR1] PS3_FSR must be auto, on, off or fsr1\n");
            return -1;
        }
    }
    bool aa = true;
    const char* aa_env = std::getenv("PS3_AA");
    if (aa_env && *aa_env) {
        if (!_stricmp(aa_env, "off") || !std::strcmp(aa_env, "0"))
            aa = false;
        else if (!_stricmp(aa_env, "smaa") || !std::strcmp(aa_env, "1"))
            aa = true;
        else {
            std::fprintf(stderr, "[OUTPUT] PS3_AA must be smaa or off\n");
            return -1;
        }
    }
    output.fsr_enabled = enable != 0;
    output.aa_enabled = aa;

    if (!output.fsr_enabled) {
        if (output.height > (UINT)internal_height) {
            /* Native output above the internal raster would leave the title's
             * 720p frame in a corner. Clamp it: native means "no upscaler", not
             * "bigger window than the renderer covers". */
            output.height = (UINT)internal_height;
            output.width = (UINT)((internal_height * 16 + 8) / 9) & ~1u;
            std::fprintf(stderr, "[OUTPUT] native output clamped to %ux%u; "
                                 "use PS3_FSR=fsr1 for a larger presentation\n",
                         output.width, output.height);
        }
        std::fprintf(stderr, "[OUTPUT] native %ux%u, AA=%s\n",
                     output.width, output.height, output.aa_enabled ? "smaa" : "off");
        const rsx_d3d12_output_pass pass{output.width, output.height, nullptr, initialize, record, shutdown, record_hudless, resize};
        return rsx_d3d12_backend_set_output_pass(&pass);
    }

    const char* sharpness = std::getenv("PS3_FSR_SHARPNESS");
    if (sharpness && *sharpness) {
        if (!_stricmp(sharpness, "low")) output.sharpness = 1.2f;
        else if (!_stricmp(sharpness, "medium")) output.sharpness = 0.6f;
        else if (!_stricmp(sharpness, "high")) output.sharpness = 0.25f;
        else {
            output.sharpness = (float)atof(sharpness);
            if (output.sharpness < 0.0f) output.sharpness = 0.0f;
            if (output.sharpness > 2.0f) output.sharpness = 2.0f;
        }
    }
    std::fprintf(stderr, "[OUTPUT] %dp internal -> %ux%u, AA=%s, FSR1 sharpness %.2f\n",
                 internal_height, output.width, output.height,
                 output.aa_enabled ? "smaa" : "off", output.sharpness);
    rsx_d3d12_output_pass pass{output.width, output.height, nullptr, initialize, record, shutdown, record_hudless, resize};
    return rsx_d3d12_backend_set_output_pass(&pass);
}

bool bakugan_output_smaa_active() { return output.device && output.aa_enabled; }
bool bakugan_output_rcas_active() { return output.fsr_active; }
