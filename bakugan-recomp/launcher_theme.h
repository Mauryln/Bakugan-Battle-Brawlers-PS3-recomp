#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace Gdiplus { class Bitmap; }

namespace launcher_theme {
void initialize(const std::wstring &directory);
void shutdown();
void paint(HWND window, HDC dc);
HBRUSH control_color(HWND child, HDC dc, bool footer);
void track_button(HWND button);
void draw_button(const DRAWITEMSTRUCT &item, bool primary);
void draw_combo(const DRAWITEMSTRUCT &item);
Gdiplus::Bitmap *controller_image();
} // namespace launcher_theme
