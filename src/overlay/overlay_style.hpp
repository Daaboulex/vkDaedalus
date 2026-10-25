#ifndef OVERLAY_STYLE_HPP_INCLUDED
#define OVERLAY_STYLE_HPP_INCLUDED

#include <cstdint>

#include <string_view>

#include "imgui.h"

namespace vkBasalt
{
    constexpr ImVec4 hexColour(uint32_t rgb, float alpha = 1.0f)
    {
        return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
    }

    struct OverlayPalette
    {
        ImVec4 accent;
        ImVec4 strong;
        ImVec4 onStrong;
        ImVec4 panel;
        ImVec4 raised;
        ImVec4 row;
        ImVec4 control;
        ImVec4 bar;
        ImVec4 tint;
        ImVec4 warn;
        ImVec4 ok;
        ImVec4 bad;
        ImVec4 info;
        ImVec4 text      = hexColour(0xE9ECEF);
        ImVec4 bright    = hexColour(0xFFFFFF);
        ImVec4 secondary = hexColour(0xC9D0D7);
        ImVec4 muted     = hexColour(0xA3ACB6);
        ImVec4 faint     = hexColour(0x919AA6);
        ImVec4 disabled  = hexColour(0x6B7480);
        ImVec4 line      = hexColour(0x20262E);
        ImVec4 edge      = hexColour(0x343C46);
        ImVec4 chipEdge  = hexColour(0x3A424C);
        ImVec4 well      = hexColour(0x0C0F13);
        ImVec4 hairline  = hexColour(0xFFFFFF, 0.06f);
    };

    const OverlayPalette& overlayPalette(std::string_view word);
    const OverlayPalette& activePalette();
    void                  useOverlayPalette(std::string_view word);

    struct FontSize
    {
        static constexpr float caption = 12.0f;
        static constexpr float small   = 13.0f;
        static constexpr float label   = 14.0f;
        static constexpr float body    = 15.0f;
        static constexpr float tile    = 18.0f;
        static constexpr float heading = 20.0f;
        static constexpr float brand   = 21.0f;
    };

    struct OverlayFonts
    {
        ImFont* body    = nullptr;
        ImFont* bold    = nullptr;
        ImFont* heading = nullptr;
    };

    OverlayFonts loadOverlayFonts(ImFontAtlas& atlas);
    void         applyOverlayStyle(ImGuiStyle& style, const OverlayPalette& palette);
    void         scaleOverlayStyle(ImGuiStyle& style, const ImGuiStyle& base, float scale);
    float        separatorAndButtonRowHeight();
}

#endif
