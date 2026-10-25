#ifndef FRAME_DRAW_HPP_INCLUDED
#define FRAME_DRAW_HPP_INCLUDED

#include "imgui.h"

#include "overlay/frame_layout.hpp"
#include "overlay/overlay_style.hpp"

namespace vkBasalt
{
    enum class Icon
    {
        Sliders,
        Frames,
        Chip,
        Chart,
        Layers,
        Bug,
        Keyboard,
        Gear,
        Person,
        Power,
        Shield,
        Eye,
        Close,
        Chevron,
        Lock,
        Restart,
        Warning
    };

    struct TileState
    {
        bool clicked = false;
        bool hovered = false;
        bool held    = false;
    };

    ImVec2 topLeft(const LayoutRect& rect);
    ImVec2 bottomRight(const LayoutRect& rect);

    void drawIcon(ImDrawList* list, Icon icon, ImVec2 at, float size, ImU32 colour);
    void drawWing(ImDrawList* list, ImVec2 at, float height, ImU32 ink, ImU32 cut);
    void drawKeyCap(ImDrawList* list, ImFont* font, float fontSize, ImVec2 at, const char* key, const OverlayPalette& palette);
    ImVec2 keyCapSize(ImFont* font, float fontSize, const char* key);

    constexpr float restartMarkGap  = 8.0f;
    constexpr float restartMarkSize = 14.0f;
    void            restartMark(const char* what, bool pending, const OverlayPalette& palette);
    bool            defaultMark(const char* what, bool changed, const std::string& defaultShown, const OverlayPalette& palette);
    TileState hitArea(const char* label, const LayoutRect& rect, bool enabled);
}

#endif
