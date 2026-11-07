#define NOMINMAX
#include "launcher_theme.h"
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <vector>

namespace launcher_theme {
namespace {
ULONG_PTR token = 0;
std::unique_ptr<Gdiplus::Bitmap> background, title;
HBRUSH panel_brush = nullptr, footer_brush = nullptr;
constexpr COLORREF PANEL = RGB(15, 23, 38), FOOTER = RGB(8, 13, 24), TEXT = RGB(236, 241, 248);
std::unique_ptr<Gdiplus::Bitmap> load(const std::filesystem::path &path) {
    auto result = std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromFile(path.c_str()));
    if (!result || result->GetLastStatus() != Gdiplus::Ok)
        return {};
    return result;
}
} // namespace
void initialize(const std::wstring &directory) {
    panel_brush = CreateSolidBrush(PANEL);
    footer_brush = CreateSolidBrush(FOOTER);
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        token = 0;
        return;
    }
    wchar_t env[32768];
    DWORD n = GetEnvironmentVariableW(L"PS3_VFS_ROOT", env, 32768);
    std::vector<std::filesystem::path> roots;
    if (n && n < 32768)
        roots.emplace_back(env);
    roots.emplace_back(std::filesystem::path(directory) / L"../../game");
    roots.emplace_back(std::filesystem::path(directory) / L"game");
    // Disc art stays in the user's dump; a missing image keeps the native theme usable.
    for (const auto &root : roots) {
        if (!background)
            background = load(root / L"PS3_GAME/PIC1.PNG");
        if (!title)
            title = load(root / L"PS3_GAME/ICON0.PNG");
    }
}
void shutdown() {
    background.reset();
    title.reset();
    if (token) {
        Gdiplus::GdiplusShutdown(token);
        token = 0;
    }
    if (panel_brush) {
        DeleteObject(panel_brush);
        panel_brush = nullptr;
    }
    if (footer_brush) {
        DeleteObject(footer_brush);
        footer_brush = nullptr;
    }
}
void paint(HWND window, HDC dc) {
    RECT client;
    GetClientRect(window, &client);
    const float scale = std::min(client.right / 1000.f, client.bottom / 800.f);
    if (scale <= 0)
        return;
    FillRect(dc, &client, footer_brush);
    if (token) {
        Gdiplus::Graphics graphics(dc);
        graphics.ScaleTransform(scale, scale);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (background) {
            float ratio = std::max(1000.f / background->GetWidth(), 800.f / background->GetHeight());
            float w = background->GetWidth() * ratio, h = background->GetHeight() * ratio;
            graphics.DrawImage(background.get(), Gdiplus::RectF((1000 - w) / 2, (800 - h) / 2, w, h));
        }
        Gdiplus::SolidBrush shade(Gdiplus::Color(150, 4, 8, 18));
        graphics.FillRectangle(&shade, Gdiplus::RectF(0, 0, 1000, 800));
        Gdiplus::SolidBrush panel(Gdiplus::Color(255, 15, 23, 38));
        graphics.FillRectangle(&panel, Gdiplus::RectF(16, 146, 302, 558));
        graphics.FillRectangle(&panel, Gdiplus::RectF(332, 146, 652, 558));
        Gdiplus::SolidBrush footer(Gdiplus::Color(255, 8, 13, 24));
        graphics.FillRectangle(&footer, Gdiplus::RectF(0, 710, 1000, 90));
        Gdiplus::Pen blue(Gdiplus::Color(255, 55, 83, 116), 1);
        graphics.DrawRectangle(&blue, Gdiplus::RectF(16, 146, 302, 558));
        graphics.DrawRectangle(&blue, Gdiplus::RectF(332, 146, 652, 558));
        Gdiplus::SolidBrush red(Gdiplus::Color(255, 195, 35, 46));
        graphics.FillRectangle(&red, Gdiplus::RectF(16, 146, 302, 3));
        graphics.FillRectangle(&red, Gdiplus::RectF(332, 146, 652, 3));
        if (title)
            graphics.DrawImage(title.get(), Gdiplus::RectF(24, 16, 174.5f, 96));
        Gdiplus::SolidBrush plate(Gdiplus::Color(215, 8, 13, 24));
        graphics.FillRectangle(&plate, Gdiplus::RectF(212, 16, 490, 96));
    }
    int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, TEXT);
    HFONT heading =
        CreateFontW(-(int)(28 * scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    SelectObject(dc, heading);
    RECT text{(LONG)(232 * scale), (LONG)(28 * scale), (LONG)(900 * scale), (LONG)(68 * scale)};
    DrawTextW(dc, L"BAKUGAN: BATTLE BRAWLERS", -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RestoreDC(dc, saved);
    DeleteObject(heading);
    saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(238, 193, 97));
    HFONT subtitle_font =
        CreateFontW(-(int)(15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    SelectObject(dc, subtitle_font);
    text = {(LONG)(234 * scale), (LONG)(76 * scale), (LONG)(950 * scale), (LONG)(103 * scale)};
    DrawTextW(dc, L"PREPARA TU PRÓXIMA BATALLA", -1, &text, DT_LEFT | DT_SINGLELINE);
    RestoreDC(dc, saved);
    DeleteObject(subtitle_font);
}
HBRUSH control_color(HWND, HDC dc, bool footer) {
    SetTextColor(dc, TEXT);
    SetBkColor(dc, footer ? FOOTER : PANEL);
    return footer ? footer_brush : panel_brush;
}
void draw_button(const DRAWITEMSTRUCT &item, bool primary) {
    int saved = SaveDC(item.hDC);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    COLORREF color = primary ? (pressed ? RGB(138, 24, 36) : RGB(189, 35, 49))
                             : (pressed ? RGB(38, 59, 83) : RGB(25, 39, 59));
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush((item.itemState & ODS_FOCUS) ? RGB(238, 193, 97) : RGB(67, 88, 116));
    FrameRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, (item.itemState & ODS_DISABLED) ? RGB(130, 140, 155) : TEXT);
    auto font = (HFONT)SendMessageW(item.hwndItem, WM_GETFONT, 0, 0);
    if (font)
        SelectObject(item.hDC, font);
    wchar_t text[256];
    GetWindowTextW(item.hwndItem, text, 256);
    RECT area = item.rcItem;
    InflateRect(&area, -4, 0);
    if (pressed)
        OffsetRect(&area, 1, 1);
    DrawTextW(item.hDC, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item.hDC, &focus);
    }
    RestoreDC(item.hDC, saved);
}
} // namespace launcher_theme
