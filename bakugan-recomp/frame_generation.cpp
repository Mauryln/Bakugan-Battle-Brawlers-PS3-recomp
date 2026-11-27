#include "frame_generation.h"
#include "graphics_config.h"
#include "rsx_d3d12_backend.h"

#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string_view>

namespace {
using Microsoft::WRL::ComPtr;
// Scene surface from the battle composite's RSX capture.
constexpr u32 SCENE_COLOR_OFFSET = 0x0eab0000;
constexpr FfxDimensions2D SCENE_SIZE{1120, 584};
constexpr std::size_t BACKEND_CONTEXT_COUNT =
    1 + FFX_OPTICALFLOW_CONTEXT_COUNT + FFX_FRAMEINTERPOLATION_CONTEXT_COUNT;
constexpr float BASE_FRAME_MS = 1000.0f / 30.0f;
constexpr unsigned INPUT_GAP_FRAMES = 2;
using Clock = std::chrono::steady_clock;

struct FrameGeneration {
    FfxSwapchain swapchain{};
    DXGI_SWAP_CHAIN_DESC1 display{};
    std::unique_ptr<std::byte[]> scratch;
    std::unique_ptr<FfxFsr3Context> context;
    ComPtr<ID3D12Resource> motion;
    ComPtr<ID3D12DescriptorHeap> rtv;
    Clock::time_point last_prepared_time{};
    std::array<float, 16> previous_view{};
    float previous_fov = 0.0f;
    void *previous_depth = nullptr;
    std::uint64_t frame_id = 0;
    std::atomic<std::uint64_t> prepared_frame_id{0};
    std::atomic<std::uint64_t> dispatches{0};
    std::uint64_t native_gaps = 0;
    std::atomic<std::uint64_t> resets{0};
    unsigned gap_frames = 0;
    std::atomic<bool> reset_history{false};
    bool async_compute = true;
    bool context_ready = false;
    std::atomic<bool> enabled{false};
    std::atomic<bool> failed{false};

    void reset() {
        swapchain = {};
        display = {};
        scratch.reset();
        context.reset();
        motion.Reset();
        rtv.Reset();
        last_prepared_time = {};
        previous_view = {};
        previous_fov = 0.0f;
        previous_depth = nullptr;
        frame_id = 0;
        prepared_frame_id.store(0, std::memory_order_relaxed);
        dispatches.store(0, std::memory_order_relaxed);
        native_gaps = 0;
        resets.store(0, std::memory_order_relaxed);
        gap_frames = 0;
        reset_history.store(false, std::memory_order_relaxed);
        async_compute = true;
        context_ready = false;
        enabled.store(false, std::memory_order_relaxed);
        failed.store(false, std::memory_order_relaxed);
    }
};
FrameGeneration generation;

void log_error(const char *operation, FfxErrorCode result) {
    std::fprintf(stderr, "[FRAME GEN] %s failed (%#x)\n", operation, static_cast<unsigned>(result));
}

bool av_sync_debug() {
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = std::getenv("BAKUGAN_AV_SYNC_DEBUG");
        enabled = (e && *e && std::string_view(e) != "0") ? 1 : 0;
    }
    return enabled != 0;
}

double av_sync_sec() {
    static const auto t0 = Clock::now();
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

bool get_camera(const rsx_d3d12_present_scene &scene, FfxFsr3DispatchFrameGenerationPrepareDescription &prepare) {
    const float *p = scene.projection;
    if (!(p[0] > 0.0f && p[5] > 0.0f && p[11] == -1.0f &&
          p[8] == 0.0f && p[9] == 0.0f && p[12] == 0.0f && p[13] == 0.0f))
        return false;

    // In the stored perspective matrix and viewport transform, device depth
    // is a + b / w. Solve at the D3D12 clip depths 0 and 1.
    const float z_over_w = p[10] / p[11];
    const float a = z_over_w * scene.depth_scale + scene.depth_offset;
    const float b = (p[14] - z_over_w * p[15]) * scene.depth_scale;
    if (!(a > 1.0f && b < 0.0f))
        return false;
    prepare.cameraNear = -b / a;
    prepare.cameraFar = -b / (a - 1.0f);
    prepare.cameraFovAngleVertical = 2.0f * std::atan(1.0f / p[5]);
    prepare.viewSpaceToMetersFactor = 1.0f;
    return std::isfinite(prepare.cameraFar);
}

float dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool view_is_rigid(const float *view) {
    for (unsigned row = 0; row < 3; ++row) {
        const float *axis = view + row * 4;
        if (!(std::abs(dot3(axis, axis) - 1.0f) < 0.025f && std::abs(axis[3]) < 0.001f))
            return false;
        for (unsigned other = 0; other < row; ++other)
            if (!(std::abs(dot3(axis, view + other * 4)) < 0.025f))
                return false;
    }
    return true;
}

const char *camera_cut_reason(const rsx_d3d12_present_scene &scene, float fov) {
    if (scene.depth != generation.previous_depth)
        return "depth";
    if (std::abs(fov - generation.previous_fov) > 0.20f)
        return "fov";
    const float *previous = generation.previous_view.data();
    if (!view_is_rigid(scene.view) || !view_is_rigid(previous))
        return nullptr;

    // A rotation above 45 degrees or a jump of 25 scene units in a single
    // rendered frame is a camera cut in the battle camera sequence.
    float relative_rotation_trace = 0.0f;
    float distance_squared = 0.0f;
    for (unsigned row = 0; row < 3; ++row) {
        const float *current_axis = scene.view + row * 4;
        const float *previous_axis = previous + row * 4;
        relative_rotation_trace += dot3(current_axis, previous_axis);
        const float delta = dot3(previous + 12, previous_axis) - dot3(scene.view + 12, current_axis);
        distance_squared += delta * delta;
    }
    if (relative_rotation_trace < 2.414214f)
        return "rot";
    if (distance_squared > 25.0f * 25.0f)
        return "dist";
    return nullptr;
}

FfxErrorCode create_context(ID3D12Device *device) {
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_2};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof shader_model)) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_2)
        return FFX_ERROR_INVALID_VERSION;

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&generation.rtv))))
        return FFX_ERROR_BACKEND_API_ERROR;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC motion{};
    motion.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    motion.Width = SCENE_SIZE.width;
    motion.Height = SCENE_SIZE.height;
    motion.DepthOrArraySize = motion.MipLevels = 1;
    motion.Format = DXGI_FORMAT_R16G16_FLOAT;
    motion.SampleDesc.Count = 1;
    motion.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = motion.Format;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &motion,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&generation.motion))))
        return FFX_ERROR_BACKEND_API_ERROR;
    device->CreateRenderTargetView(generation.motion.Get(), nullptr,
                                  generation.rtv->GetCPUDescriptorHandleForHeapStart());

    const std::size_t size = ffxGetScratchMemorySizeDX12(BACKEND_CONTEXT_COUNT);
    generation.scratch.reset(new (std::nothrow) std::byte[size]{});
    generation.context.reset(new (std::nothrow) FfxFsr3Context);
    if (!generation.scratch || !generation.context)
        return FFX_ERROR_OUT_OF_MEMORY;

    FfxFsr3ContextDescription desc{};
    FfxErrorCode result = ffxGetInterfaceDX12(&desc.backendInterfaceSharedResources, ffxGetDeviceDX12(device),
                                            generation.scratch.get(), size, BACKEND_CONTEXT_COUNT);
    if (result != FFX_OK)
        return result;
    desc.backendInterfaceFrameInterpolation = desc.backendInterfaceSharedResources;
    desc.flags = FFX_FSR3_ENABLE_INTERPOLATION_ONLY;
    if (generation.async_compute)
        desc.flags |= FFX_FSR3_ENABLE_ASYNC_WORKLOAD_SUPPORT;
    desc.maxRenderSize = SCENE_SIZE;
    desc.maxUpscaleSize = desc.displaySize = {generation.display.Width, generation.display.Height};
    desc.backBufferFormat = ffxGetSurfaceFormatDX12(generation.display.Format);
    result = ffxFsr3ContextCreate(generation.context.get(), &desc);
    if (result != FFX_OK && (desc.flags & FFX_FSR3_ENABLE_ASYNC_WORKLOAD_SUPPORT)) {
        std::fprintf(stderr, "[FRAME GEN] async compute context creation failed (%#x), retrying synchronously\n",
                     static_cast<unsigned>(result));
        generation.async_compute = false;
        desc.flags &= ~FFX_FSR3_ENABLE_ASYNC_WORKLOAD_SUPPORT;
        result = ffxFsr3ContextCreate(generation.context.get(), &desc);
    }
    generation.context_ready = result == FFX_OK;
    return result;
}

FfxErrorCode dispatch(const FfxFrameGenerationDispatchDescription *desc, void*) {
    // The proxy owns this mutable descriptor and uses its count to schedule
    // the generated output. A gap still needs the interpolation fence signal.
    if (generation.prepared_frame_id.load(std::memory_order_acquire) != desc->frameID) {
        const_cast<FfxFrameGenerationDispatchDescription *>(desc)->numInterpolatedFrames = 0;
        return FFX_OK;
    }
    auto request = *desc;
    request.reset |= generation.reset_history.load(std::memory_order_acquire);
    const FfxErrorCode result = ffxFsr3DispatchFrameGeneration(&request);
    if (result == FFX_OK) {
        generation.dispatches.fetch_add(1, std::memory_order_relaxed);
        if (request.reset)
            generation.resets.fetch_add(1, std::memory_order_relaxed);
    } else {
        generation.failed.store(true, std::memory_order_release);
        log_error("interpolation", result);
    }
    return result;
}

FfxErrorCode configure_dispatch(bool enabled, ID3D12Resource *hudless = nullptr, const char *reason = "") {
    FfxFrameGenerationConfig config{};
    config.swapChain = generation.swapchain;
    config.frameGenerationCallback = dispatch;
    config.frameGenerationEnabled = enabled;
    config.allowAsyncWorkloads = generation.async_compute;
    if (hudless)
        config.HUDLessColor = ffxGetResourceDX12(hudless, ffxGetResourceDescriptionDX12(hudless),
                                               L"HUD-less scene", FFX_RESOURCE_STATE_COMMON);
    const auto content = rsx_d3d12_fit_16_9(generation.display.Width, generation.display.Height);
    config.interpolationRect = {static_cast<int>(content.x), static_cast<int>(content.y),
                               static_cast<int>(content.width), static_cast<int>(content.height)};
    config.frameID = generation.frame_id;
    const bool prev_enabled = generation.enabled.load(std::memory_order_acquire);
    // The proxy releases the previous interpolation before the next Present
    // returns. Its two Prepare slots and the display-indexed HUD-less buffers
    // therefore stay live until the compute queue has consumed them.
    const FfxErrorCode result = ffxFsr3ConfigureFrameGeneration(generation.context.get(), &config);
    if (result == FFX_OK) {
        generation.enabled.store(enabled, std::memory_order_release);
        if (av_sync_debug() && prev_enabled != enabled) {
            std::fprintf(stderr,
                "[AVSYNC][FG] t=%.3fs frame=%llu active %d->%d reason=%s gap=%u hudless=%d dispatches=%llu resets=%llu native_gaps=%llu\n",
                av_sync_sec(),
                static_cast<unsigned long long>(generation.frame_id),
                prev_enabled ? 1 : 0,
                enabled ? 1 : 0,
                reason,
                generation.gap_frames,
                hudless ? 1 : 0,
                static_cast<unsigned long long>(generation.dispatches.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(generation.resets.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(generation.native_gaps));
        }
    }
    return result;
}

void skip_frame() {
    if (!generation.enabled.load(std::memory_order_acquire)) return;
    ++generation.gap_frames;
    // Missing input for one batch should not drain every queue and restart
    // the proxy threads. Prolonged gaps return to native presentation.
    const bool keep_proxy_pacing = !generation.failed.load(std::memory_order_acquire) &&
                                   generation.gap_frames <= INPUT_GAP_FRAMES;
    const FfxErrorCode result = configure_dispatch(keep_proxy_pacing, nullptr,
                                                   keep_proxy_pacing ? "gap_keep" : "gap_disable");
    generation.native_gaps += keep_proxy_pacing;
    if (result != FFX_OK)
        log_error("configure gap", result);
}

void clear_motion_vectors(ID3D12GraphicsCommandList *commands) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {generation.motion.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
    commands->ResourceBarrier(1, &barrier);
    constexpr float zero[4]{};
    commands->ClearRenderTargetView(generation.rtv->GetCPUDescriptorHandleForHeapStart(), zero, 0, nullptr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commands->ResourceBarrier(1, &barrier);
}

void record_frame(void *commands, const rsx_d3d12_present_scene *scene) {
    ++generation.frame_id;
    FfxFsr3DispatchFrameGenerationPrepareDescription prepare{};
    if (generation.failed.load(std::memory_order_acquire) || !scene || !scene->hudless || !get_camera(*scene, prepare)) {
        skip_frame();
        return;
    }

    auto *command_list = static_cast<ID3D12GraphicsCommandList *>(commands);
    auto *depth = static_cast<ID3D12Resource *>(scene->depth);
    if (!generation.context_ready) {
        const auto display = static_cast<ID3D12Resource *>(scene->hudless)->GetDesc();
        generation.display.Width = static_cast<UINT>(display.Width);
        generation.display.Height = display.Height;
        ComPtr<ID3D12Device> device;
        const FfxErrorCode result = SUCCEEDED(depth->GetDevice(IID_PPV_ARGS(&device)))
            ? create_context(device.Get()) : FFX_ERROR_NULL_DEVICE;
        if (result != FFX_OK) {
            generation.failed.store(true, std::memory_order_release);
            log_error("context creation", result);
            return;
        }
        std::fprintf(stderr, "[FRAME GEN] FSR 3.1 interpolation %ux%u -> %ux%u; HUD-less; %s; zero motion vectors\n",
                     SCENE_SIZE.width, SCENE_SIZE.height, generation.display.Width, generation.display.Height,
                     generation.async_compute ? "async compute" : "direct queue");
    }

    clear_motion_vectors(command_list);
    prepare.commandList = ffxGetCommandListDX12(command_list);
    prepare.depth = ffxGetResourceDX12(depth, ffxGetResourceDescriptionDX12(depth, FFX_RESOURCE_USAGE_DEPTHTARGET),
                                      L"RSX scene depth", FFX_RESOURCE_STATE_COMPUTE_READ);
    prepare.motionVectors = ffxGetResourceDX12(generation.motion.Get(),
        ffxGetResourceDescriptionDX12(generation.motion.Get()), L"Zero motion vectors", FFX_RESOURCE_STATE_COMPUTE_READ);
    prepare.renderSize = SCENE_SIZE;
    prepare.motionVectorScale = {static_cast<float>(SCENE_SIZE.width), static_cast<float>(SCENE_SIZE.height)};
    const auto now = Clock::now();
    const std::uint64_t prev_prepared = generation.prepared_frame_id.load(std::memory_order_acquire);
    const bool consecutive = prev_prepared && prev_prepared + 1 == generation.frame_id;
    prepare.frameTimeDelta = consecutive
        ? std::chrono::duration<float, std::milli>(now - generation.last_prepared_time).count()
        : BASE_FRAME_MS;
    const char *cut_reason = nullptr;
    if (consecutive && prepare.frameTimeDelta <= 3.0f * BASE_FRAME_MS)
        cut_reason = camera_cut_reason(*scene, prepare.cameraFovAngleVertical);
    const char *reset_reason = !consecutive ? "nonconsec"
        : (prepare.frameTimeDelta > 3.0f * BASE_FRAME_MS) ? "dt"
        : cut_reason;
    const bool reset_hist = reset_reason != nullptr;
    if (av_sync_debug() && reset_hist) {
        std::fprintf(stderr,
            "[AVSYNC][FG] t=%.3fs frame=%llu history_reset=1 reason=%s dt_ms=%.2f consecutive=%d\n",
            av_sync_sec(),
            static_cast<unsigned long long>(generation.frame_id),
            reset_reason,
            prepare.frameTimeDelta,
            consecutive ? 1 : 0);
    }
    generation.reset_history.store(reset_hist, std::memory_order_release);
    prepare.frameID = generation.frame_id;
    FfxErrorCode result = ffxFsr3ContextDispatchFrameGenerationPrepare(generation.context.get(), &prepare);
    if (result == FFX_OK) {
        generation.prepared_frame_id.store(generation.frame_id, std::memory_order_release);
        result = configure_dispatch(true, static_cast<ID3D12Resource *>(scene->hudless), "prepare");
        if (result == FFX_OK) {
            generation.last_prepared_time = now;
            generation.previous_depth = scene->depth;
            generation.previous_fov = prepare.cameraFovAngleVertical;
            for (unsigned i = 0; i < generation.previous_view.size(); ++i)
                generation.previous_view[i] = scene->view[i];
            generation.gap_frames = 0;
        } else {
            generation.prepared_frame_id.store(prev_prepared, std::memory_order_release);
        }
    }
    if (result != FFX_OK) {
        log_error("prepare or enable", result);
        const FfxErrorCode disable_result = configure_dispatch(false, nullptr, "error_disable");
        if (disable_result != FFX_OK)
            log_error("disable", disable_result);
        generation.failed.store(true, std::memory_order_release);
    }
}

void *create_swapchain(void *window, const void *desc, void *queue, void *factory) {
    generation.display = *static_cast<const DXGI_SWAP_CHAIN_DESC1 *>(desc);
    const FfxErrorCode result = ffxCreateFrameinterpolationSwapchainForHwndDX12(
        static_cast<HWND>(window), &generation.display, nullptr, static_cast<ID3D12CommandQueue *>(queue),
        static_cast<IDXGIFactory2 *>(factory), generation.swapchain);
    if (result != FFX_OK) {
        std::fprintf(stderr, "[FRAME GEN] proxy creation failed (%#x), using DXGI\n",
                     static_cast<unsigned>(result));
        return nullptr;
    }
    std::fprintf(stderr, "[FRAME GEN] DX12 proxy created\n");
    return static_cast<IDXGISwapChain1 *>(ffxGetDX12SwapchainPtr(generation.swapchain));
}

void prepare_swapchain_resize(void*) {
    if (!generation.swapchain) return;
    const FfxErrorCode wait_result = ffxWaitForPresents(generation.swapchain);
    if (wait_result != FFX_OK)
        log_error("waiting for proxy presents", wait_result);
    if (generation.context_ready) {
        const FfxErrorCode disable_result = configure_dispatch(false, nullptr, "resize");
        if (disable_result != FFX_OK)
            log_error("disable", disable_result);
        const FfxErrorCode result = ffxFsr3ContextDestroy(generation.context.get());
        if (result != FFX_OK)
            log_error("context destruction", result);
        generation.context_ready = false;
        generation.context.reset();
        generation.scratch.reset();
        generation.motion.Reset();
        generation.rtv.Reset();
    }
    generation.prepared_frame_id.store(0, std::memory_order_release);
    generation.reset_history.store(true, std::memory_order_release);
    generation.enabled.store(false, std::memory_order_release);
    generation.gap_frames = 0;
}

void drain_swapchain(void* swapchain) {
    const bool had_context = generation.context_ready;
    prepare_swapchain_resize(swapchain);
    if (had_context)
        std::fprintf(stderr, "[FRAME GEN] context destroyed after %llu dispatches, %llu resets, %llu short gaps\n",
                     static_cast<unsigned long long>(generation.dispatches.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(generation.resets.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(generation.native_gaps));
    generation.reset();
}
}

int bakugan_frame_generation_configure() {
    const wchar_t *mode = _wgetenv(bakugan::FRAME_GEN_ENV);
    if (!mode || std::wstring_view(mode) != bakugan::FRAME_GEN_FSR3)
        return 0;
    const char *async = std::getenv("PS3_FSR3_ASYNC");
    generation.async_compute = !async || std::string_view(async) != "0";

    const rsx_d3d12_swapchain_provider provider{
        SCENE_COLOR_OFFSET, SCENE_SIZE.width, SCENE_SIZE.height,
        create_swapchain, record_frame, drain_swapchain, prepare_swapchain_resize
    };
    return rsx_d3d12_backend_set_swapchain_provider(&provider);
}

bool bakugan_frame_generation_active() {
    return generation.enabled.load(std::memory_order_acquire) &&
           !generation.failed.load(std::memory_order_acquire);
}

std::uint64_t bakugan_frame_generation_dispatches() {
    return generation.dispatches.load(std::memory_order_relaxed);
}
