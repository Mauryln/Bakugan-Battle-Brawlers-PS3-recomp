#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstddef>
#include <vector>

namespace bakugan {

struct SavedataGdiCache {
    struct FontEntry {
        float px;
        bool bold;
        Gdiplus::Font* font;
    };
    struct BrushEntry {
        Gdiplus::ARGB argb;
        Gdiplus::SolidBrush* brush;
    };

    static constexpr size_t kMaxFonts = 32;
    static constexpr size_t kMaxBrushes = 32;

    Gdiplus::FontFamily* family = nullptr;
    Gdiplus::StringFormat* formats[4]{};
    std::vector<FontEntry> fonts;
    std::vector<BrushEntry> brushes;

    Gdiplus::FontFamily* get_family() {
        if (!family) {
            auto* trebuchet = new Gdiplus::FontFamily(L"Trebuchet MS");
            if (trebuchet->IsAvailable()) {
                family = trebuchet;
            } else {
                delete trebuchet;
                family = new Gdiplus::FontFamily(L"Arial");
            }
        }
        return family;
    }

    Gdiplus::StringFormat* get_format(Gdiplus::StringAlignment align, bool wrap) {
        const int idx = (align == Gdiplus::StringAlignmentCenter ? 1 : 0) | (wrap ? 2 : 0);
        if (!formats[idx]) {
            auto* fmt = new Gdiplus::StringFormat();
            fmt->SetAlignment(align == Gdiplus::StringAlignmentCenter
                                  ? Gdiplus::StringAlignmentCenter
                                  : Gdiplus::StringAlignmentNear);
            fmt->SetLineAlignment(Gdiplus::StringAlignmentNear);
            fmt->SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            if (!wrap)
                fmt->SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                                    Gdiplus::StringFormatFlagsLineLimit);
            formats[idx] = fmt;
        }
        return formats[idx];
    }

    Gdiplus::Font* get_font(float px, bool bold) {
        for (const auto& entry : fonts) {
            if (entry.px == px && entry.bold == bold) return entry.font;
        }
        auto* font = new Gdiplus::Font(
            get_family(), px, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
            Gdiplus::UnitPixel);
        if (fonts.size() >= kMaxFonts) {
            delete fonts.front().font;
            fonts.erase(fonts.begin());
        }
        fonts.push_back({px, bold, font});
        return font;
    }

    Gdiplus::SolidBrush* get_brush(Gdiplus::Color color) {
        const Gdiplus::ARGB argb = color.GetValue();
        for (const auto& entry : brushes) {
            if (entry.argb == argb) return entry.brush;
        }
        auto* brush = new Gdiplus::SolidBrush(color);
        if (brushes.size() >= kMaxBrushes) {
            delete brushes.front().brush;
            brushes.erase(brushes.begin());
        }
        brushes.push_back({argb, brush});
        return brush;
    }

    void clear() {
        for (auto& entry : fonts) delete entry.font;
        fonts.clear();
        for (auto& entry : brushes) delete entry.brush;
        brushes.clear();
        for (auto*& fmt : formats) {
            delete fmt;
            fmt = nullptr;
        }
        delete family;
        family = nullptr;
    }
};

} // namespace bakugan
