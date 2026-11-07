#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace launcher_theme {
void initialize(const std::wstring &directory);
void shutdown();
void set_english(bool english);
void paint(HWND window, HDC dc);
HBRUSH control_color(HWND child, HDC dc, bool footer);
void draw_button(const DRAWITEMSTRUCT &item, bool primary);
} // namespace launcher_theme
