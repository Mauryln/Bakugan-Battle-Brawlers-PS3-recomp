#define NOMINMAX
#include "launcher_theme.h"
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <memory>

namespace launcher_theme {
namespace {
constexpr COLORREF BASE = RGB(16, 15, 18);
constexpr COLORREF PANEL = RGB(28, 25, 29);
constexpr COLORREF FIELD = RGB(40, 34, 38);
constexpr COLORREF BORDER = RGB(74, 56, 61);
constexpr COLORREF TEXT = RGB(247, 241, 236);
constexpr COLORREF GOLD = RGB(237, 191, 113);
ULONG_PTR token = 0;
std::unique_ptr<Gdiplus::Bitmap> controller;
HBRUSH panel_brush = nullptr, base_brush = nullptr;
HWND hovered_button = nullptr;

void fill(HDC dc, const RECT &bounds, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void draw_text(HDC dc, const wchar_t *text, RECT bounds, int height, int weight, COLORREF color) {
    HFONT font = CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &bounds, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, previous);
    DeleteObject(font);
}

LRESULT CALLBACK button_subclass(HWND button, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE && hovered_button != button) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, button, 0};
        if (TrackMouseEvent(&tracking)) {
            HWND previous = hovered_button;
            hovered_button = button;
            if (previous) InvalidateRect(previous, nullptr, FALSE);
            InvalidateRect(button, nullptr, FALSE);
        }
    } else if (msg == WM_MOUSELEAVE && hovered_button == button) {
        hovered_button = nullptr;
        InvalidateRect(button, nullptr, FALSE);
    } else if (msg == WM_NCDESTROY) {
        if (hovered_button == button) hovered_button = nullptr;
        RemoveWindowSubclass(button, button_subclass, id);
    }
    return DefSubclassProc(button, msg, wp, lp);
}
} // namespace

void initialize(const std::wstring &directory) {
    panel_brush = CreateSolidBrush(PANEL);
    base_brush = CreateSolidBrush(BASE);
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        token = 0;
        return;
    }
    const auto image = std::filesystem::path(directory) / L"assets/launcher/Control.png";
    controller.reset(Gdiplus::Bitmap::FromFile(image.c_str()));
    if (controller && controller->GetLastStatus() != Gdiplus::Ok) controller.reset();
}

Gdiplus::Bitmap *controller_image() { return controller.get(); }

void shutdown() {
    controller.reset();
    if (token) Gdiplus::GdiplusShutdown(token);
    if (panel_brush) DeleteObject(panel_brush);
    if (base_brush) DeleteObject(base_brush);
}

void paint(HWND window, HDC dc) {
    using namespace Gdiplus;
    RECT client{};
    GetClientRect(window, &client);
    const float scale = std::min(client.right / 1000.f, client.bottom / 800.f);
    if (scale <= 0) return;
    FillRect(dc, &client, base_brush);

    if (token) {
        Graphics graphics(dc);
        graphics.ScaleTransform(scale, scale);
        const RectF header(16, 18, 968, 92);
        LinearGradientBrush header_fill(header, Color(255, 69, 24, 35), Color(255, 24, 21, 25),
                                        LinearGradientModeHorizontal);
        SolidBrush panel(Color(255, 28, 25, 29));
        Pen outline(Color(255, 70, 51, 56));
        SolidBrush accent(Color(255, 176, 44, 63));
        graphics.FillRectangle(&header_fill, header);
        graphics.DrawRectangle(&outline, header);
        graphics.FillRectangle(&accent, RectF(16, 18, 4, 92));
        graphics.FillRectangle(&panel, RectF(692, 18, 292, 92));
        for (const RectF &card : {RectF(16, 128, 302, 582), RectF(332, 128, 652, 582)}) {
            graphics.FillRectangle(&panel, card);
            graphics.DrawRectangle(&outline, card);
        }
        SolidBrush gold(Color(255, 237, 191, 113));
        graphics.FillRectangle(&gold, RectF(32, 156, 24, 2));
        graphics.FillRectangle(&gold, RectF(348, 156, 24, 2));
        graphics.DrawLine(&outline, 16.f, 723.f, 984.f, 723.f);
    }

    const auto area = [scale](int left, int top, int right, int bottom) {
        return RECT{LONG(left * scale), LONG(top * scale), LONG(right * scale), LONG(bottom * scale)};
    };
    int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    draw_text(dc, L"Bakugan: Battle Brawlers", area(36, 32, 676, 74), int(34 * scale), FW_BOLD, TEXT);
    draw_text(dc, L"v1.4.0", area(38, 74, 676, 99), int(15 * scale), FW_SEMIBOLD, GOLD);
    RestoreDC(dc, saved);
}

HBRUSH control_color(HWND, HDC dc, bool footer) {
    SetTextColor(dc, TEXT);
    SetBkColor(dc, footer ? BASE : PANEL);
    return footer ? base_brush : panel_brush;
}

void track_button(HWND button) { SetWindowSubclass(button, button_subclass, 1, 0); }

void draw_button(const DRAWITEMSTRUCT &item, bool primary) {
    int saved = SaveDC(item.hDC);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hovered = item.hwndItem == hovered_button;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF color = primary ? (pressed ? RGB(129, 27, 43) : hovered ? RGB(201, 55, 77) : RGB(172, 39, 61))
                                   : (pressed ? RGB(60, 40, 46) : hovered ? RGB(70, 47, 54) : FIELD);
    fill(item.hDC, item.rcItem, color);
    HBRUSH edge = CreateSolidBrush(focused || hovered ? GOLD : BORDER);
    FrameRect(item.hDC, &item.rcItem, edge);
    DeleteObject(edge);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, (item.itemState & ODS_DISABLED) ? RGB(161, 151, 154) : TEXT);
    auto font = (HFONT)SendMessageW(item.hwndItem, WM_GETFONT, 0, 0);
    if (font) SelectObject(item.hDC, font);
    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, 256);
    RECT area = item.rcItem;
    InflateRect(&area, -4, 0);
    if (pressed) OffsetRect(&area, 1, 1);
    DrawTextW(item.hDC, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RestoreDC(item.hDC, saved);
}

void draw_combo(const DRAWITEMSTRUCT &item) {
    const bool selected = (item.itemState & ODS_SELECTED) && !(item.itemState & ODS_COMBOBOXEDIT);
    fill(item.hDC, item.rcItem, selected ? RGB(101, 39, 52) : FIELD);
    if (item.itemID == UINT(-1)) return;
    const LRESULT size = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0);
    if (size == CB_ERR) return;
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(text.data()));
    const int saved = SaveDC(item.hDC);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, TEXT);
    auto font = (HFONT)SendMessageW(item.hwndItem, WM_GETFONT, 0, 0);
    if (font) SelectObject(item.hDC, font);
    RECT area = item.rcItem;
    InflateRect(&area, -5, 0);
    DrawTextW(item.hDC, text.c_str(), -1, &area, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
        DrawFocusRect(item.hDC, &item.rcItem);
    RestoreDC(item.hDC, saved);
}
} // namespace launcher_theme
