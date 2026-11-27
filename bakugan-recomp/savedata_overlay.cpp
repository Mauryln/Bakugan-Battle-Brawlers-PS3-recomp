/*
 * In-game save-data picker for the port.
 *
 * The firmware save manager is a system process that draws a list, lets the
 * player choose a directory and hands it back to cellSaveData. This port has
 * no such process, so cellSaveData exposes a chooser hook and this module
 * stands in for the applet -- without opening another window. The list is
 * rendered with GDI+ into a premultiplied BGRA canvas (so it can use the
 * game's own PIC1/ICON0 art and real fonts), uploaded to a D3D12 texture and
 * drawn over the finished frame by the render thread.
 *
 * The chooser runs on the guest thread and blocks in WaitForSingleObject while
 * the render thread owns the canvas. Input arrives through the backend's
 * window procedure and through XInput. Set PS3_SAVEDATA_UI=0 to disable.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "input_bridge.h"
#include "frame_generation.h"
#include "output_upscale.h"
#include <windowsx.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <xinput.h>
#include <gdiplus.h>
#include <wrl/client.h>
#include "savedata_gdi_cache.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include "cellSaveData.h"
#include "rsx_d3d12_backend.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "xinput.lib")

using namespace Gdiplus;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int   kMaxSlots      = 32;
constexpr int   kRowsVisible   = 4;
constexpr float kBaseHeight    = 720.0f;

/* ---------------------------------------------------------------------------
 * Shared state
 * -----------------------------------------------------------------------*/

constexpr UINT PICKER_SRV = 0;
constexpr UINT DIAGNOSTICS_SRV = 1;
constexpr UINT HUD_WIDTH = 352;
constexpr UINT HUD_HEIGHT = 116;

struct OverlayCanvas {
    ComPtr<ID3D12Resource> texture, upload;
    std::unique_ptr<Bitmap> bitmap;
    uint8_t* pixels = nullptr;
    UINT width = 0, height = 0, pitch = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    bool dirty = false;
};

struct OverlayState {
    CRITICAL_SECTION lock;
    HANDLE done;
    int ready;              /* render resources are live */
    int active;
    int input_blocked;
    int is_save;
    int count;
    int sel;
    int dirty;              /* canvas must be redrawn */
    int icons_dirty;        /* reload slot icons for this session */
    int pad_primed;
    int result;             /* 1 = picked, 0 = cancelled */
    HWND hwnd;
    HCURSOR prev_cursor;
    char out_dir[CELL_SAVEDATA_DIRNAME_SIZE];
    CellSaveDataSlotInfo slots[kMaxSlots];

    /* D3D12 */
    ID3D12Device* device;
    ID3D12RootSignature* root;
    ID3D12PipelineState* pso;
    ID3D12DescriptorHeap* srv_heap;
    ID3D12DescriptorHeap* rtv_heap;
    OverlayCanvas picker, diagnostics;
    ULONGLONG next_hud_update;

    /* GDI+ */
    ULONG_PTR gdiplus_token;
    int gdiplus_ready;
    Image* backdrop;
    Image* disc_icon;
    Image* icons[kMaxSlots];
    bakugan::SavedataGdiCache gdi_cache;

    rsx_d3d12_output_rect picker_bounds;
    UINT output_width, output_height;
    bool picker_enabled;
    int prompt_style;
    std::wstring vfs_root;
} g;

/* ---------------------------------------------------------------------------
 * Helpers
 * -----------------------------------------------------------------------*/

std::wstring utf8(const char* text)
{
    if (!text || !*text) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    UINT cp = CP_UTF8;
    if (n <= 1) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, text, -1, nullptr, 0);
    }
    if (n <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(cp, 0, text, -1, out.data(), n);
    return out;
}

std::wstring format_date(s64 mtime)
{
    time_t t = static_cast<time_t>(mtime);
    struct tm tmv;
    if (mtime <= 0 || localtime_s(&tmv, &t) != 0) return L"unknown date";
    wchar_t buf[64];
    swprintf(buf, sizeof buf / sizeof buf[0], L"%04d-%02d-%02d  %02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return buf;
}

std::wstring format_size(u32 kb)
{
    wchar_t buf[32];
    swprintf(buf, sizeof buf / sizeof buf[0], L"%u KB", kb);
    return buf;
}

Color rgba(int a, int r, int gr, int b) { return Color(a, r, gr, b); }

struct Layout {
    float scale;
    float px, py, pw, ph;
    float lx, ly, lw, row_h;
    float preview_x, preview_y, preview_w, preview_h;
    float ok_x, ok_y, ok_w, ok_h;
    float cancel_x, cancel_y, cancel_w, cancel_h;
    int start;
};

Layout make_layout(UINT width, UINT height, int count, int sel)
{
    const float s = height > 0 ? static_cast<float>(height) / kBaseHeight : 1.0f;
    Layout l{};
    l.scale = s;
    l.px = 80 * s;  l.py = 40 * s;  l.pw = 1120 * s; l.ph = 640 * s;
    l.lx = l.px + 32 * s; l.ly = l.py + 170 * s; l.lw = 640 * s; l.row_h = 100 * s;
    l.preview_x = l.px + 700 * s; l.preview_y = l.py + 170 * s;
    l.preview_w = 388 * s; l.preview_h = 400 * s;
    l.ok_x = l.px + 800 * s; l.ok_y = l.py + l.ph - 66 * s;
    l.ok_w = 140 * s; l.ok_h = 44 * s;
    l.cancel_x = l.px + 954 * s; l.cancel_y = l.ok_y;
    l.cancel_w = 140 * s; l.cancel_h = l.ok_h;

    int start = 0;
    if (sel >= kRowsVisible) start = sel - kRowsVisible + 1;
    int max_start = count > kRowsVisible ? count - kRowsVisible : 0;
    if (start > max_start) start = max_start;
    if (start < 0) start = 0;
    l.start = start;
    return l;
}

/* ---------------------------------------------------------------------------
 * GDI+ canvas
 * -----------------------------------------------------------------------*/

void draw_text(Graphics& gr, const std::wstring& text, const RectF& box, float px,
               bool bold, Color color, StringAlignment align = StringAlignmentNear,
               bool wrap = false)
{
    if (text.empty()) return;
    Font* font = g.gdi_cache.get_font(px, bold);
    StringFormat* fmt = g.gdi_cache.get_format(align, wrap);
    SolidBrush* brush = g.gdi_cache.get_brush(color);
    gr.DrawString(text.c_str(), static_cast<INT>(text.size()), font, box, fmt, brush);
}

void draw_button(Graphics& gr, const RectF& r, const wchar_t* label, Color fill,
                 Color text, float px)
{
    GraphicsPath path;
    float d = r.Height * 0.35f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    gr.FillPath(g.gdi_cache.get_brush(fill), &path);
    Pen edge(Color(200, 255, 235, 170), 1.5f);
    gr.DrawPath(&edge, &path);
    draw_text(gr, label, r, px, true, text, StringAlignmentCenter);
}

void draw_icon(Graphics& gr, Image* img, const RectF& box, bool border)
{
    if (border) {
        gr.FillRectangle(g.gdi_cache.get_brush(rgba(255, 4, 8, 18)), box);
    }
    if (img && img->GetLastStatus() == Ok) {
        /* Keep the 16:9 art centered without distorting it. */
        float iw = static_cast<float>(img->GetWidth());
        float ih = static_cast<float>(img->GetHeight());
        float scale = std::min(box.Width / iw, box.Height / ih);
        float w = iw * scale, h = ih * scale;
        RectF dst(box.X + (box.Width - w) * 0.5f, box.Y + (box.Height - h) * 0.5f, w, h);
        gr.DrawImage(img, dst);
    } else {
        draw_text(gr, L"BAKUGAN", box, box.Height * 0.16f, true,
                  rgba(255, 214, 178, 74), StringAlignmentCenter);
    }
    if (border) {
        Pen pen(rgba(255, 214, 178, 74), 2.0f);
        gr.DrawRectangle(&pen, box);
    }
}

void render_picker(OverlayCanvas& canvas)
{
    const UINT width = canvas.width, height = canvas.height;
    Graphics gr(canvas.bitmap.get());
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    gr.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const Layout l = make_layout(width, height, g.count, g.sel);
    const float s = l.scale;

    /* The game's own key art, then a dim layer so text stays readable. */
    if (g.backdrop && g.backdrop->GetLastStatus() == Ok) {
        gr.DrawImage(g.backdrop, RectF(0, 0, static_cast<float>(width),
                                       static_cast<float>(height)));
        gr.FillRectangle(g.gdi_cache.get_brush(rgba(168, 2, 6, 16)),
                         0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    } else {
        gr.FillRectangle(g.gdi_cache.get_brush(rgba(255, 3, 8, 20)),
                         0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    }

    /* Panel and its double frame. */
    gr.FillRectangle(g.gdi_cache.get_brush(rgba(228, 6, 12, 26)), l.px, l.py, l.pw, l.ph);
    Pen gold(rgba(255, 214, 178, 74), 3 * s);
    gr.DrawRectangle(&gold, l.px, l.py, l.pw, l.ph);
    Pen inner(rgba(90, 255, 245, 220), 1 * s);
    gr.DrawRectangle(&inner, l.px + 7 * s, l.py + 7 * s, l.pw - 14 * s, l.ph - 14 * s);

    const float accent = 18 * s;
    SolidBrush* red = g.gdi_cache.get_brush(rgba(255, 176, 32, 32));
    gr.FillRectangle(red, l.px, l.py, accent, accent);
    gr.FillRectangle(red, l.px + l.pw - accent, l.py, accent, accent);
    gr.FillRectangle(red, l.px, l.py + l.ph - accent, accent, accent);
    gr.FillRectangle(red, l.px + l.pw - accent, l.py + l.ph - accent, accent, accent);

    /* Header: the disc's save icon doubles as the game mark. */
    draw_icon(gr, g.disc_icon, RectF(l.px + 28 * s, l.py + 26 * s, 160 * s, 90 * s), true);
    draw_text(gr, g.is_save ? L"SAVE DATA" : L"LOAD DATA",
              RectF(l.px + 210 * s, l.py + 22 * s, 700 * s, 52 * s), 38 * s, true,
              rgba(255, 232, 196, 86));
    draw_text(gr, L"Bakugan: Battle Brawlers",
              RectF(l.px + 212 * s, l.py + 74 * s, 600 * s, 30 * s), 20 * s, false,
              rgba(255, 235, 240, 255));
    Pen rule(rgba(160, 214, 178, 74), 1.5f * s);
    gr.DrawLine(&rule, l.px + 28 * s, l.py + 138 * s,
                l.px + l.pw - 28 * s, l.py + 138 * s);

    if (g.count == 0) {
        draw_text(gr, L"No save data.",
                  RectF(l.lx, l.ly + 120 * s, l.lw, 60 * s), 26 * s, true,
                  rgba(255, 240, 240, 240));
    }

    for (int row = 0; row < kRowsVisible; row++) {
        const int idx = l.start + row;
        if (idx >= g.count) break;
        const CellSaveDataSlotInfo& slot = g.slots[idx];
        const bool selected = idx == g.sel;
        RectF box(l.lx, l.ly + row * l.row_h, l.lw, l.row_h - 10 * s);

        SolidBrush* fill = g.gdi_cache.get_brush(
            selected ? rgba(230, 122, 20, 26) : rgba(170, 8, 16, 32));
        gr.FillRectangle(fill, box);
        if (selected) {
            gr.FillRectangle(g.gdi_cache.get_brush(rgba(255, 214, 178, 74)),
                             box.X, box.Y, 5 * s, box.Height);
            Pen sel_pen(rgba(255, 214, 178, 74), 2 * s);
            gr.DrawRectangle(&sel_pen, box);
        }

        Image* icon = (idx < kMaxSlots) ? g.icons[idx] : nullptr;
        draw_icon(gr, icon ? icon : g.disc_icon,
                  RectF(box.X + 12 * s, box.Y + 10 * s, 132 * s, 74 * s), true);

        const std::wstring line1 = utf8(slot.subTitle[0] ? slot.subTitle : slot.title);
        draw_text(gr, line1.empty() ? utf8(slot.dirName) : line1,
                  RectF(box.X + 162 * s, box.Y + 8 * s, box.Width - 176 * s, 30 * s),
                  22 * s, true, rgba(255, 245, 245, 245));
        draw_text(gr, utf8(slot.dirName),
                  RectF(box.X + 162 * s, box.Y + 38 * s, box.Width - 176 * s, 22 * s),
                  16 * s, false, rgba(230, 196, 208, 224));
        const std::wstring meta = format_date(slot.mtime) + L"   -   " + format_size(slot.sizeKB);
        draw_text(gr, meta,
                  RectF(box.X + 162 * s, box.Y + 61 * s, box.Width - 176 * s, 20 * s),
                  15 * s, false, rgba(220, 168, 184, 200));
    }

    if (l.start > 0)
        draw_text(gr, L"\u25B2", RectF(l.lx + l.lw - 24 * s, l.ly - 22 * s, 20 * s, 18 * s),
                  15 * s, true, rgba(255, 214, 178, 74), StringAlignmentCenter);
    if (l.start + kRowsVisible < g.count)
        draw_text(gr, L"\u25BC",
                  RectF(l.lx + l.lw - 24 * s, l.ly + kRowsVisible * l.row_h + 2 * s, 20 * s, 18 * s),
                  15 * s, true, rgba(255, 214, 178, 74), StringAlignmentCenter);

    /* Preview of the selected slot. */
    if (g.count > 0 && g.sel >= 0 && g.sel < g.count) {
        const CellSaveDataSlotInfo& slot = g.slots[g.sel];
        RectF preview(l.preview_x, l.preview_y, l.preview_w, l.preview_h);
        gr.FillRectangle(g.gdi_cache.get_brush(rgba(200, 4, 9, 20)), preview);
        Pen pp(rgba(120, 214, 178, 74), 1.5f * s);
        gr.DrawRectangle(&pp, preview);
        draw_icon(gr, g.icons[g.sel] ? g.icons[g.sel] : g.disc_icon,
                  RectF(preview.X + 12 * s, preview.Y + 12 * s,
                        preview.Width - 24 * s, 207 * s), true);
        const std::wstring title = utf8(slot.subTitle[0] ? slot.subTitle : slot.title);
        draw_text(gr, title, RectF(preview.X + 14 * s, preview.Y + 232 * s,
                                   preview.Width - 28 * s, 28 * s),
                  20 * s, true, rgba(255, 232, 196, 86), StringAlignmentNear, true);
        draw_text(gr, utf8(slot.detail),
                  RectF(preview.X + 14 * s, preview.Y + 264 * s,
                        preview.Width - 28 * s, 70 * s),
                  15 * s, false, rgba(235, 230, 235, 240), StringAlignmentNear, true);
        draw_text(gr, utf8(slot.dirName),
                  RectF(preview.X + 14 * s, preview.Y + 338 * s,
                        preview.Width - 28 * s, 20 * s),
                  14 * s, false, rgba(220, 168, 184, 200));
        draw_text(gr, format_date(slot.mtime) + L"   -   " + format_size(slot.sizeKB),
                  RectF(preview.X + 14 * s, preview.Y + 360 * s,
                        preview.Width - 28 * s, 20 * s),
                  14 * s, false, rgba(220, 168, 184, 200));
    }

    const wchar_t* legend = L"\u2191\u2193 / D-Pad Select     [\u2715] / Enter Confirm     [\u25CB] / Esc Cancel";
    if (g.prompt_style == RSX_BUTTON_PROMPTS_XBOX) {
        legend = L"\u2191\u2193 / D-Pad Select     [A] / Enter Confirm     [B] / Esc Cancel";
    } else if (g.prompt_style == RSX_BUTTON_PROMPTS_NINTENDO) {
        legend = L"\u2191\u2193 / D-Pad Select     [B] / Enter Confirm     [A] / Esc Cancel";
    }
    draw_text(gr, legend,
              RectF(l.px + 28 * s, l.py + l.ph - 50 * s, 680 * s, 24 * s),
              16 * s, false, rgba(210, 210, 220, 230));
    draw_button(gr, RectF(l.ok_x, l.ok_y, l.ok_w, l.ok_h),
                g.is_save ? L"SAVE" : L"LOAD",
                rgba(255, 150, 28, 30), rgba(255, 255, 240, 220), 18 * s);
    draw_button(gr, RectF(l.cancel_x, l.cancel_y, l.cancel_w, l.cancel_h),
                L"CANCEL", rgba(255, 46, 56, 66),
                rgba(255, 255, 240, 220), 18 * s);

}

bool upload_canvas(OverlayCanvas& canvas)
{
    BitmapData data{};
    const Rect rect(0, 0, static_cast<INT>(canvas.width), static_cast<INT>(canvas.height));
    if (canvas.bitmap->LockBits(&rect, ImageLockModeRead, PixelFormat32bppPARGB, &data) != Ok)
        return false;
    const auto* source = static_cast<const uint8_t*>(data.Scan0);
    for (UINT row = 0; row < canvas.height; ++row)
        memcpy(canvas.pixels + static_cast<size_t>(row) * canvas.pitch,
               source + static_cast<ptrdiff_t>(row) * data.Stride, static_cast<size_t>(canvas.width) * 4);
    canvas.bitmap->UnlockBits(&data);
    canvas.dirty = true;
    return true;
}

void render_diagnostics(OverlayCanvas& canvas, const rsx_d3d12_overlay_frame& frame)
{
    Graphics gr(canvas.bitmap.get());
    gr.Clear(rgba(220, 10, 14, 22));
    gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    const bool fg = bakugan_frame_generation_active();
    const char* cap = std::getenv("PS3_FPS_CAP");
    const int target = cap ? std::atoi(cap) : 0;
    std::wstring pacing = L"off";
    if (fg)
        pacing = L"FG / 30 native";
    else if (cap && (!_stricmp(cap, "monitor") || !_stricmp(cap, "auto")))
        pacing = L"monitor cap";
    else if (target > 0 && target <= 240)
        pacing = std::to_wstring(target) + L" FPS cap";

    const auto on_off = [](bool enabled) { return enabled ? L"ON" : L"OFF"; };
    wchar_t lines[5][96]{};
    swprintf(lines[0], 96, L"Presented %.1f FPS   Native %.1f FPS", frame.presented_fps, frame.native_fps);
    swprintf(lines[1], 96, L"Frame %.1f ms   FSR 3.1 FG %ls", frame.frame_ms, on_off(fg));
    swprintf(lines[2], 96, L"VSync %ls   Pacing %ls", on_off(frame.vsync != 0), pacing.c_str());
    swprintf(lines[3], 96, L"%ux%u \u2192 %ux%u   %hs",
             frame.internal_width, frame.internal_height, frame.output_width, frame.output_height, frame.display_mode);
    swprintf(lines[4], 96, L"SMAA %ls   RCAS %ls   Sixaxis %ls",
             on_off(bakugan_output_smaa_active()), on_off(bakugan_output_rcas_active()),
             on_off(ps3_pad_sensor_active(0) != 0));

    for (int i = 0; i < 5; ++i)
        draw_text(gr, lines[i], RectF(10, 8.0f + 20.0f * i, HUD_WIDTH - 20.0f, 20),
                  14, i == 0, rgba(255, 241, 245, 250));
}

/* ---------------------------------------------------------------------------
 * D3D12 resources
 * -----------------------------------------------------------------------*/

const char* kOverlayShader =
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "VSOut vs_main(uint id : SV_VertexID) {\n"
    "    VSOut o;\n"
    "    float2 p = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);\n"
    "    o.pos = float4(p, 0.0, 1.0);\n"
    "    o.uv = float2(id == 1 ? 2.0 : 0.0, id == 2 ? -1.0 : 1.0);\n"
    "    return o;\n"
    "}\n"
    "Texture2D canvas_tex : register(t0);\n"
    "SamplerState canvas_smp : register(s0);\n"
    "float4 ps_main(VSOut i) : SV_Target { return canvas_tex.Sample(canvas_smp, i.uv); }\n";

bool build_resources(void* device_ptr)
{
    auto* device = static_cast<ID3D12Device*>(device_ptr);
    g.device = device;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    ID3DBlob* serialized = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &serialized, &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "[savedata-ui] root signature: %.*s\n",
                errors ? static_cast<int>(errors->GetBufferSize()) : 0,
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        if (errors) errors->Release();
        return false;
    }
    hr = device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                     serialized->GetBufferSize(),
                                     IID_PPV_ARGS(&g.root));
    serialized->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "[savedata-ui] CreateRootSignature failed: %08lX\n", (unsigned long)hr);
        return false;
    }

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    hr = D3DCompile(kOverlayShader, strlen(kOverlayShader), "savedata_overlay",
                    nullptr, nullptr, "vs_main", "vs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "[savedata-ui] vs compile: %.*s\n",
                errors ? static_cast<int>(errors->GetBufferSize()) : 0,
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        if (errors) errors->Release();
        return false;
    }
    hr = D3DCompile(kOverlayShader, strlen(kOverlayShader), "savedata_overlay",
                    nullptr, nullptr, "ps_main", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "[savedata-ui] ps compile: %.*s\n",
                errors ? static_cast<int>(errors->GetBufferSize()) : 0,
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        if (errors) errors->Release();
        vs->Release();
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g.root;
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g.pso));
    vs->Release();
    ps->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "[savedata-ui] CreateGraphicsPipelineState failed: %08lX\n", (unsigned long)hr);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 2;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&g.srv_heap)))) {
        fprintf(stderr, "[savedata-ui] SRV heap creation failed\n");
        return false;
    }

    heap.NumDescriptors = 1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&g.rtv_heap)))) {
        fprintf(stderr, "[savedata-ui] RTV heap creation failed\n");
        return false;
    }

    return true;
}

bool create_canvas_upload(OverlayCanvas& canvas, UINT width, UINT height, UINT index)
{
    if (canvas.texture && canvas.width == width && canvas.height == height) return true;
    OverlayCanvas next;
    next.width = width;
    next.height = height;
    next.bitmap = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
    if (next.bitmap->GetLastStatus() != Ok) return false;

    D3D12_HEAP_PROPERTIES def{};
    def.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = width;
    td.Height = height;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    if (FAILED(g.device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&next.texture)))) {
        fprintf(stderr, "[savedata-ui] canvas texture %ux%u creation failed\n", width, height);
        return false;
    }

    D3D12_HEAP_PROPERTIES up{};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    next.pitch = (width * 4 + 255) & ~255u;
    bd.Width = static_cast<UINT64>(next.pitch) * height;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;   /* required for buffers; {} leaves it zero */
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(g.device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&next.upload)))) {
        fprintf(stderr, "[savedata-ui] canvas upload buffer creation failed\n");
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(next.upload->Map(0, nullptr, &mapped)) || !mapped) {
        fprintf(stderr, "[savedata-ui] canvas upload buffer map failed\n");
        return false;
    }
    next.pixels = static_cast<uint8_t*>(mapped);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = g.srv_heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(index) *
               g.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g.device->CreateShaderResourceView(next.texture.Get(), &srv, cpu);
    canvas = std::move(next);
    return true;
}

void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
             D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

/* ---------------------------------------------------------------------------
 * Backend callbacks
 * -----------------------------------------------------------------------*/

int overlay_initialize(void*, void* device, void*, u32, u32, u32, u32)
{
    if (!build_resources(device)) return -1;

    GdiplusStartupInput input;
    if (GdiplusStartup(&g.gdiplus_token, &input, nullptr) == Ok)
        g.gdiplus_ready = 1;

    const char* vfs = getenv("PS3_VFS_ROOT");
    std::wstring base = g.vfs_root;
    if (base.empty() && vfs && *vfs) base = utf8(vfs);
    if (g.picker_enabled && !base.empty()) {
        std::wstring path = base + L"\\PS3_GAME\\PIC1.PNG";
        g.backdrop = Image::FromFile(path.c_str());
        if (!g.backdrop || g.backdrop->GetLastStatus() != Ok) {
            if (g.backdrop) { delete g.backdrop; g.backdrop = nullptr; }
            path = base + L"\\PIC1.PNG";
            g.backdrop = Image::FromFile(path.c_str());
            if (g.backdrop && g.backdrop->GetLastStatus() != Ok) {
                delete g.backdrop; g.backdrop = nullptr;
            }
        }
        path = base + L"\\PS3_GAME\\ICON0.PNG";
        g.disc_icon = Image::FromFile(path.c_str());
        if (!g.disc_icon || g.disc_icon->GetLastStatus() != Ok) {
            if (g.disc_icon) { delete g.disc_icon; g.disc_icon = nullptr; }
            path = base + L"\\ICON0.PNG";
            g.disc_icon = Image::FromFile(path.c_str());
            if (g.disc_icon && g.disc_icon->GetLastStatus() != Ok) {
                delete g.disc_icon; g.disc_icon = nullptr;
            }
        }
    }

    g.prompt_style = rsx_d3d12_backend_get_button_prompts();
    g.ready = 1;
    return 0;
}

void load_icons(void)
{
    for (int i = 0; i < kMaxSlots; i++) {
        if (g.icons[i]) { delete g.icons[i]; g.icons[i] = nullptr; }
    }
    for (int i = 0; i < g.count && i < kMaxSlots; i++) {
        std::wstring path = utf8(g.slots[i].path) + L"\\ICON0.PNG";
        Image* img = Image::FromFile(path.c_str());
        if (img && img->GetLastStatus() == Ok) g.icons[i] = img;
        else if (img) delete img;
    }
}

void accept_choice(void)
{
    EnterCriticalSection(&g.lock);
    if (g.result == 0 && g.sel >= 0 && g.sel < g.count) {
        snprintf(g.out_dir, sizeof g.out_dir, "%s", g.slots[g.sel].dirName);
        g.result = 1;
        SetEvent(g.done);
    }
    LeaveCriticalSection(&g.lock);
}

void cancel_choice(void)
{
    EnterCriticalSection(&g.lock);
    if (g.result == 0) {
        g.result = 0;
        SetEvent(g.done);
    }
    LeaveCriticalSection(&g.lock);
}

void move_selection(int delta)
{
    if (g.count <= 0) return;
    int next = g.sel + delta;
    if (next < 0) next = 0;
    if (next >= g.count) next = g.count - 1;
    if (next != g.sel) {
        g.sel = next;
        g.dirty = 1;
    }
}

HWND overlay_window(void)
{
    if (g.hwnd && IsWindow(g.hwnd))
        return g.hwnd;
    HWND fg = GetActiveWindow();
    char cls[32]{};
    if (fg && GetClassNameA(fg, cls, sizeof cls) > 0 && std::strcmp(cls, "ps3recomp_d3d12") == 0) {
        g.hwnd = fg;
        return g.hwnd;
    }
    HWND found = nullptr;
    while ((found = FindWindowExA(nullptr, found, "ps3recomp_d3d12", nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(found, &pid);
        if (pid == GetCurrentProcessId()) {
            g.hwnd = found;
            return g.hwnd;
        }
    }
    return nullptr;
}

int hit_test(int x, int y)
{
    if (!g.picker_bounds.width || !g.picker_bounds.height) return -3;
    const Layout l = make_layout(g.picker_bounds.width, g.picker_bounds.height, g.count, g.sel);
    float fx = static_cast<float>(x), fy = static_cast<float>(y);
    RECT client{};
    if (HWND hwnd = overlay_window(); hwnd && GetClientRect(hwnd, &client) &&
        client.right > client.left && client.bottom > client.top) {
        fx = fx * static_cast<float>(g.output_width) / static_cast<float>(client.right - client.left);
        fy = fy * static_cast<float>(g.output_height) / static_cast<float>(client.bottom - client.top);
    }
    fx -= static_cast<float>(g.picker_bounds.x);
    fy -= static_cast<float>(g.picker_bounds.y);
    auto inside = [](const RectF& r, float px, float py) {
        return px >= r.X && px < r.GetRight() && py >= r.Y && py < r.GetBottom();
    };
    if (g.count > 0 && inside(RectF(l.ok_x, l.ok_y, l.ok_w, l.ok_h), fx, fy)) return -1;
    if (inside(RectF(l.cancel_x, l.cancel_y, l.cancel_w, l.cancel_h), fx, fy)) return -2;
    for (int row = 0; row < kRowsVisible; row++) {
        const int idx = l.start + row;
        if (idx >= g.count) break;
        if (inside(RectF(l.lx, l.ly + row * l.row_h, l.lw, l.row_h - 10 * l.scale), fx, fy))
            return idx;
    }
    return -3;
}

int overlay_input(void*, unsigned msg, unsigned long long wp, long long lp)
{
    const bool active = __atomic_load_n(&g.active, __ATOMIC_RELAXED) != 0;
    const bool blocked = __atomic_load_n(&g.input_blocked, __ATOMIC_RELAXED) != 0;
    if (!active) {
        if (blocked && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
                        (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                        msg == WM_INPUT))
            return 1;
        return 0;
    }

    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP:    move_selection(-1); return 1;
        case VK_DOWN:  move_selection(1);  return 1;
        case VK_PRIOR: move_selection(-kRowsVisible); return 1;
        case VK_NEXT:  move_selection(kRowsVisible);  return 1;
        case VK_HOME:  g.sel = 0; g.dirty = 1; return 1;
        case VK_END:   g.sel = g.count ? g.count - 1 : 0; g.dirty = 1; return 1;
        case VK_RETURN:
        case VK_SPACE: accept_choice(); return 1;
        case VK_ESCAPE: cancel_choice(); return 1;
        }
        return 1;   /* modal: the game must not see the menu keys */
    case WM_MOUSEMOVE: {
        int hit = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit >= 0 && hit != g.sel) { g.sel = hit; g.dirty = 1; }
        return 1;
    }
    case WM_LBUTTONDOWN: {
        int hit = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit == -1) accept_choice();
        else if (hit == -2) cancel_choice();
        else if (hit >= 0) { g.sel = hit; g.dirty = 1; }
        return 1;
    }
    case WM_LBUTTONDBLCLK:
        if (hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) >= 0) accept_choice();
        return 1;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        move_selection(delta > 0 ? -1 : 1);
        return 1;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return 1;
        }
        return 0;
    }
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
        (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
        msg == WM_INPUT)
        return 1;
    return 0;
}

void poll_pad(void)
{
    static WORD previous;
    static ULONGLONG last_repeat;
    XINPUT_STATE state;
    memset(&state, 0, sizeof state);
    ps3_host_pad_report mapped{};
    const bool provider = bakugan_input_gamepad_snapshot(&mapped);
    WORD now = 0;
    if (provider) {
        if (mapped.buttons & (1u << 4)) now |= XINPUT_GAMEPAD_DPAD_UP;
        if (mapped.buttons & (1u << 6)) now |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (mapped.buttons & (1u << 14)) now |= XINPUT_GAMEPAD_A;
        if (mapped.buttons & (1u << 13)) now |= XINPUT_GAMEPAD_B;
    } else {
        if (XInputGetState(0, &state) != ERROR_SUCCESS) { previous = 0; return; }
        now = state.Gamepad.wButtons;
    }
    if (!g.pad_primed) {
        /* The button that opened the save menu is still held here; only an
         * edge after the picker is visible should act on it. */
        previous = now;
        g.pad_primed = 1;
        return;
    }
    const WORD pressed = static_cast<WORD>(now & ~previous);
    previous = now;
    const ULONGLONG t = GetTickCount64();
    const bool repeat = (now & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) &&
                        (t - last_repeat > 220);
    if (pressed & XINPUT_GAMEPAD_DPAD_UP || (repeat && (now & XINPUT_GAMEPAD_DPAD_UP))) {
        move_selection(-1); last_repeat = t;
    } else if (pressed & XINPUT_GAMEPAD_DPAD_DOWN ||
               (repeat && (now & XINPUT_GAMEPAD_DPAD_DOWN))) {
        move_selection(1); last_repeat = t;
    } else if (pressed & XINPUT_GAMEPAD_A) {
        accept_choice();
    } else if (pressed & XINPUT_GAMEPAD_B) {
        cancel_choice();
    }
}

void draw_canvas(ID3D12GraphicsCommandList* list, OverlayCanvas& canvas, UINT index,
                 const rsx_d3d12_output_rect& bounds, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    if (canvas.dirty) {
        if (canvas.state != D3D12_RESOURCE_STATE_COPY_DEST)
            barrier(list, canvas.texture.Get(), canvas.state, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = canvas.texture.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = canvas.upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, canvas.width, canvas.height, 1, canvas.pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(list, canvas.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        canvas.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        canvas.dirty = false;
    }
    if (canvas.state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) return;

    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT vp{float(bounds.x), float(bounds.y), float(bounds.width), float(bounds.height), 0, 1};
    const D3D12_RECT sc{LONG(bounds.x), LONG(bounds.y), LONG(bounds.x + bounds.width), LONG(bounds.y + bounds.height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    ID3D12DescriptorHeap* heaps[] = {g.srv_heap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(g.root);
    list->SetPipelineState(g.pso);
    auto srv = g.srv_heap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += static_cast<UINT64>(index) *
               g.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    list->SetGraphicsRootDescriptorTable(0, srv);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

int overlay_record(void*, void* command_list, void* target, u32, const rsx_d3d12_overlay_frame* frame)
{
    static int was_active = 0;
    if (!g.ready) return 0;
    const int active = __atomic_load_n(&g.active, __ATOMIC_RELAXED);
    if (active && !was_active) {
        g.prev_cursor = SetCursor(LoadCursor(nullptr, IDC_ARROW));
    } else if (!active && was_active) {
        SetCursor(g.prev_cursor);
        if (HWND hwnd = overlay_window(); hwnd && GetForegroundWindow() == hwnd) {
            POINT pt{};
            RECT client{};
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt) &&
                GetClientRect(hwnd, &client) && PtInRect(&client, pt)) {
                SendMessageA(hwnd, WM_SETCURSOR, (WPARAM)hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            }
        }
    }
    was_active = active;
    if (!frame->diagnostics) g.next_hud_update = 0;
    if ((!active && !frame->diagnostics) || !g.gdiplus_ready) return 0;
    const auto& content = frame->content;
    if (!content.width || !content.height) return 0;

    auto* list = static_cast<ID3D12GraphicsCommandList*>(command_list);
    auto* res = static_cast<ID3D12Resource*>(target);
    const auto rtv = g.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    g.device->CreateRenderTargetView(res, nullptr, rtv);

    if (active) {
        poll_pad();
        const int prompt_style = rsx_d3d12_backend_get_button_prompts();
        if (prompt_style != g.prompt_style) {
            g.prompt_style = prompt_style;
            g.dirty = 1;
        }
        if (g.icons_dirty) {
            load_icons();
            g.icons_dirty = 0;
        }
        g.picker_bounds = content;
        g.output_width = frame->output_width;
        g.output_height = frame->output_height;
        const bool size_changed = g.picker.width != content.width || g.picker.height != content.height;
        if (create_canvas_upload(g.picker, content.width, content.height, PICKER_SRV)) {
            if (g.dirty || size_changed) {
                render_picker(g.picker);
                g.dirty = !upload_canvas(g.picker);
            }
            draw_canvas(list, g.picker, PICKER_SRV, content, rtv);
        }
    }

    if (frame->diagnostics && create_canvas_upload(g.diagnostics, HUD_WIDTH, HUD_HEIGHT, DIAGNOSTICS_SRV)) {
        const ULONGLONG now = GetTickCount64();
        if (now >= g.next_hud_update) {
            render_diagnostics(g.diagnostics, *frame);
            if (upload_canvas(g.diagnostics)) g.next_hud_update = now + 500;
        }
        const u32 inset = std::min(12u, std::min(content.width, content.height) / 8);
        const u32 panel_width = std::min({HUD_WIDTH, content.width - 2 * inset,
                                        (content.height - 2 * inset) * HUD_WIDTH / HUD_HEIGHT});
        const rsx_d3d12_output_rect panel{content.x + inset, content.y + inset,
                                         panel_width, panel_width * HUD_HEIGHT / HUD_WIDTH};
        draw_canvas(list, g.diagnostics, DIAGNOSTICS_SRV, panel, rtv);
    }
    return 0;
}

void overlay_shutdown(void*)
{
    g.ready = 0;
    if (g.done) SetEvent(g.done);
    g.gdi_cache.clear();
    for (int i = 0; i < kMaxSlots; i++) {
        if (g.icons[i]) { delete g.icons[i]; g.icons[i] = nullptr; }
    }
    if (g.disc_icon) { delete g.disc_icon; g.disc_icon = nullptr; }
    if (g.backdrop) { delete g.backdrop; g.backdrop = nullptr; }
    g.picker = {};
    g.diagnostics = {};
    if (g.srv_heap) { g.srv_heap->Release(); g.srv_heap = nullptr; }
    if (g.rtv_heap) { g.rtv_heap->Release(); g.rtv_heap = nullptr; }
    if (g.pso) { g.pso->Release(); g.pso = nullptr; }
    if (g.root) { g.root->Release(); g.root = nullptr; }
    if (g.gdiplus_ready) {
        GdiplusShutdown(g.gdiplus_token);
        g.gdiplus_ready = 0;
    }
    g.device = nullptr;
}

/* ---------------------------------------------------------------------------
 * Chooser
 * -----------------------------------------------------------------------*/

void wait_for_input_release(void)
{
    for (int i = 0; i < 40; i++) {
        XINPUT_STATE state;
        memset(&state, 0, sizeof state);
        ps3_host_pad_report mapped{};
        bool any = bakugan_input_gamepad_snapshot(&mapped)
                       ? mapped.buttons != 0
                       : (XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons != 0);
        any = any || (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        any = any || (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        any = any || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        any = any || (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        any = any || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        any = any || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!any) break;
        Sleep(25);
    }
}

int pick_slot(int is_save, const CellSaveDataSlotInfo* slots, int count, int focus,
              const char* new_name, char* out_dir, size_t out_size)
{
    (void)new_name;
    if (!g.ready) return -1;   /* backend not up: use the headless default */

    EnterCriticalSection(&g.lock);
    __atomic_store_n(&g.input_blocked, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g.active, 1, __ATOMIC_RELAXED);
    g.is_save = is_save;
    g.count = count < kMaxSlots ? count : kMaxSlots;
    if (g.count < 0) g.count = 0;
    for (int i = 0; i < g.count; i++) g.slots[i] = slots[i];
    g.sel = (focus >= 0 && focus < g.count) ? focus : 0;
    g.result = 0;
    g.prompt_style = rsx_d3d12_backend_get_button_prompts();
    g.dirty = 1;
    g.icons_dirty = 1;
    g.pad_primed = 0;
    ResetEvent(g.done);
    LeaveCriticalSection(&g.lock);

    WaitForSingleObject(g.done, INFINITE);

    EnterCriticalSection(&g.lock);
    const int result = g.result;
    __atomic_store_n(&g.active, 0, __ATOMIC_RELAXED);
    if (result == 1) snprintf(out_dir, out_size, "%s", g.out_dir);
    LeaveCriticalSection(&g.lock);

    wait_for_input_release();
    __atomic_store_n(&g.input_blocked, 0, __ATOMIC_RELAXED);
    printf("[savedata-ui] %s -> '%s'\n", is_save ? "save" : "load",
           result == 1 ? out_dir : "(cancelled)");
    return result;
}

} // namespace

extern "C" bool bakugan_savedata_overlay_active() {
    return __atomic_load_n(&g.active, __ATOMIC_RELAXED) != 0 ||
           __atomic_load_n(&g.input_blocked, __ATOMIC_RELAXED) != 0;
}

void bakugan_savedata_overlay_install(const char* vfs_root)
{
    const char* off = getenv("PS3_SAVEDATA_UI");
    g.picker_enabled = !(off && *off == '0');
    if (g.picker_enabled) {
        if (vfs_root && *vfs_root) g.vfs_root = utf8(vfs_root);
        InitializeCriticalSection(&g.lock);
        g.done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        g.picker_enabled = g.done != nullptr;
    }

    static rsx_d3d12_host_overlay overlay{};
    overlay.userdata = nullptr;
    overlay.initialize = overlay_initialize;
    overlay.record = overlay_record;
    overlay.input = overlay_input;
    overlay.shutdown = overlay_shutdown;
    if (rsx_d3d12_backend_set_host_overlay(&overlay) != 0) {
        printf("[savedata-ui] backend does not accept a host overlay\n");
        return;
    }
    if (g.picker_enabled) {
        ps3_savedata_set_chooser(pick_slot);
        printf("[savedata-ui] in-game save picker installed\n");
    }
}
