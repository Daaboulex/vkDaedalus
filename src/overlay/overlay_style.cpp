#include "overlay/overlay_style.hpp"
#include "overlay/palette_names.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "barlow_regular_font.h"
#include "barlow_semibold_font.h"
#include "barlow_semicondensed_bold_font.h"

namespace vkBasalt
{
    namespace
    {
        struct Surfaces
        {
            uint32_t panel, raised, row, control, bar;
        };

        constexpr Surfaces plain{0x101317, 0x1A222C, 0x151A21, 0x1B2129, 0x0B0D10};

        OverlayPalette paletteOf(uint32_t accent, uint32_t strong, uint32_t tint, Surfaces surfaces, uint32_t warn, uint32_t ok, uint32_t bad, uint32_t info)
        {
            OverlayPalette p;
            p.accent   = hexColour(accent);
            p.strong   = hexColour(strong);
            p.onStrong = hexColour(0xFFFFFF);
            p.panel    = hexColour(surfaces.panel);
            p.raised   = hexColour(surfaces.raised);
            p.row      = hexColour(surfaces.row);
            p.control  = hexColour(surfaces.control);
            p.bar      = hexColour(surfaces.bar);
            p.tint     = hexColour(tint);
            p.warn     = hexColour(warn);
            p.ok       = hexColour(ok);
            p.bad      = hexColour(bad);
            p.info     = hexColour(info);
            return p;
        }

        OverlayPalette neutral(uint32_t accent, uint32_t strong, uint32_t tint)
        {
            return paletteOf(accent, strong, tint, plain, 0xF2A541, 0x7EE0A1, 0xF06A6A, accent);
        }

        const std::array<OverlayPalette, paletteNames.size()>& palettes()
        {
            static const std::array<OverlayPalette, paletteNames.size()> all = {
                paletteOf(0xA88FD0, 0x6E4E8C, 0x241F30, {0x121117, 0x1E1B26, 0x17151D, 0x201D29, 0x0B0A0E}, 0xDEAA7C, 0xC6E8AA, 0xD16563, 0xACCEDF),
                paletteOf(0xC455FC, 0x8B2FD0, 0x26173A, {0x121019, 0x211A2E, 0x18151F, 0x221C2D, 0x0B0A10}, 0xFC8C54, 0xA4FC4C, 0xF45C54, 0x9CDCFC),
                neutral(0xC455FC, 0x8B2FD0, 0x26173A),
                neutral(0x6EA8FE, 0x2F6FDE, 0x13223A),
                neutral(0x7C9CFF, 0x3D5BD9, 0x172040),
                neutral(0x9DB4D0, 0x47627F, 0x1A222C),
                neutral(0x67D4F5, 0x11779A, 0x0E2A33),
                neutral(0xE879F9, 0xA3329F, 0x2C1530),
                paletteOf(0x6EA8FE, 0x2F6FDE, 0x1C2A42, {0x1C2129, 0x283040, 0x232A33, 0x2A313C, 0x151920}, 0xF2A541, 0x7EE0A1, 0xF17070, 0x6EA8FE),
            };
            return all;
        }

        size_t activeIndex = 0;
    }

    const OverlayPalette& overlayPalette(std::string_view word)
    {
        for (size_t i = 0; i < paletteNames.size(); i++)
            if (paletteNames[i].word == word)
                return palettes()[i];
        return palettes()[0];
    }

    const OverlayPalette& activePalette()
    {
        return palettes()[activeIndex];
    }

    void useOverlayPalette(std::string_view word)
    {
        activeIndex = 0;
        for (size_t i = 0; i < paletteNames.size(); i++)
            if (paletteNames[i].word == word)
                activeIndex = i;
    }

    OverlayFonts loadOverlayFonts(ImFontAtlas& atlas)
    {
        const auto add = [&atlas](const unsigned char* data, unsigned int size, const char* name) {
            ImFontConfig config;
            std::snprintf(config.Name, sizeof(config.Name), "%s", name);
            ImFont* font = atlas.AddFontFromMemoryCompressedTTF(data, static_cast<int>(size), FontSize::body, &config);
            if (font == nullptr)
                throw std::runtime_error(std::string("the built-in font ") + name + " could not be loaded");
            return font;
        };
        OverlayFonts fonts;
        fonts.body    = add(barlow_regular_compressed_data, barlow_regular_compressed_size, "Barlow");
        fonts.bold    = add(barlow_semibold_compressed_data, barlow_semibold_compressed_size, "Barlow SemiBold");
        fonts.heading = add(barlow_semicondensed_bold_compressed_data, barlow_semicondensed_bold_compressed_size, "Barlow Semi Condensed SemiBold");
        return fonts;
    }

    float separatorAndButtonRowHeight()
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        return style.ItemSpacing.y * 2.0f + 4.0f * style.FontScaleMain + ImGui::GetFrameHeight();
    }

    void scaleOverlayStyle(ImGuiStyle& style, const ImGuiStyle& base, float scale)
    {
        style = base;
        style.ScaleAllSizes(scale);
        style.FramePadding.y = base.FramePadding.y * scale;
        style.FontScaleMain  = scale;
    }

    void applyOverlayStyle(ImGuiStyle& style, const OverlayPalette& p)
    {
        ImGui::StyleColorsDark(&style);

        style.Alpha             = 1.0f;
        style.FontSizeBase      = FontSize::body;
        style.WindowRounding    = 8.0f;
        style.ChildRounding     = 4.0f;
        style.FrameRounding     = 4.0f;
        style.PopupRounding     = 8.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 7.0f;
        style.TabRounding       = 4.0f;

        style.WindowPadding       = ImVec2(12.0f, 10.0f);
        style.FramePadding        = ImVec2(12.0f, 8.5f);
        style.ItemSpacing         = ImVec2(8.0f, 8.0f);
        style.ItemInnerSpacing    = ImVec2(8.0f, 6.0f);
        style.CellPadding         = ImVec2(8.0f, 4.0f);
        style.IndentSpacing       = 22.0f;
        style.ScrollbarSize       = 8.0f;
        style.GrabMinSize         = 14.0f;
        style.DisabledAlpha       = 0.45f;
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
        style.ButtonTextAlign     = ImVec2(0.5f, 0.5f);

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize  = 0.0f;
        style.PopupBorderSize  = 1.0f;
        style.FrameBorderSize  = 0.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

        const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
        ImVec4*      c                 = style.Colors;
        c[ImGuiCol_Text]             = p.text;
        c[ImGuiCol_TextDisabled]     = p.disabled;
        c[ImGuiCol_WindowBg]         = p.panel;
        c[ImGuiCol_ChildBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_PopupBg]          = p.raised;
        c[ImGuiCol_Border]           = p.hairline;
        c[ImGuiCol_FrameBg]          = p.row;
        c[ImGuiCol_FrameBgHovered]   = p.control;
        c[ImGuiCol_FrameBgActive]    = p.tint;
        c[ImGuiCol_TitleBg]          = p.bar;
        c[ImGuiCol_TitleBgActive]    = p.bar;
        c[ImGuiCol_Header]           = p.tint;
        c[ImGuiCol_HeaderHovered]    = p.control;
        c[ImGuiCol_HeaderActive]     = p.tint;
        c[ImGuiCol_Button]           = p.control;
        c[ImGuiCol_ButtonHovered]    = p.tint;
        c[ImGuiCol_ButtonActive]     = p.strong;
        c[ImGuiCol_SliderGrab]       = p.accent;
        c[ImGuiCol_SliderGrabActive] = p.accent;
        c[ImGuiCol_Separator]        = p.line;
        c[ImGuiCol_Tab]              = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TabHovered]       = p.control;
        c[ImGuiCol_TabSelected]      = p.tint;
        c[ImGuiCol_PlotLines]        = p.accent;
        c[ImGuiCol_ScrollbarBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_ScrollbarGrab]    = p.edge;
        c[ImGuiCol_NavCursor]        = p.accent;
        c[ImGuiCol_BorderShadow]               = none;
        c[ImGuiCol_TitleBgCollapsed]           = p.bar;
        c[ImGuiCol_MenuBarBg]                  = p.bar;
        c[ImGuiCol_ScrollbarGrabHovered]       = hexColour(0x4A515B);
        c[ImGuiCol_ScrollbarGrabActive]        = p.accent;
        c[ImGuiCol_CheckboxSelectedBg]         = p.accent;
        c[ImGuiCol_CheckMark]                  = p.panel;
        c[ImGuiCol_SeparatorHovered]           = p.edge;
        c[ImGuiCol_SeparatorActive]            = p.accent;
        c[ImGuiCol_ResizeGrip]                 = none;
        c[ImGuiCol_ResizeGripHovered]          = none;
        c[ImGuiCol_ResizeGripActive]           = none;
        c[ImGuiCol_InputTextCursor]            = p.text;
        c[ImGuiCol_TabSelectedOverline]        = none;
        c[ImGuiCol_TabDimmed]                  = none;
        c[ImGuiCol_TabDimmedSelected]          = p.tint;
        c[ImGuiCol_TabDimmedSelectedOverline]  = none;
        c[ImGuiCol_DockingPreview]             = p.tint;
        c[ImGuiCol_DockingEmptyBg]             = p.panel;
        c[ImGuiCol_PlotLinesHovered]           = p.bright;
        c[ImGuiCol_PlotHistogram]              = p.accent;
        c[ImGuiCol_PlotHistogramHovered]       = p.bright;
        c[ImGuiCol_TableHeaderBg]              = p.raised;
        c[ImGuiCol_TableBorderStrong]          = p.edge;
        c[ImGuiCol_TableBorderLight]           = p.line;
        c[ImGuiCol_TableRowBg]                 = none;
        c[ImGuiCol_TableRowBgAlt]              = p.row;
        c[ImGuiCol_TextLink]                   = p.info;
        c[ImGuiCol_TextSelectedBg]             = p.tint;
        c[ImGuiCol_TreeLines]                  = p.line;
        c[ImGuiCol_DragDropTarget]             = p.accent;
        c[ImGuiCol_DragDropTargetBg]           = p.tint;
        c[ImGuiCol_UnsavedMarker]              = p.warn;
        c[ImGuiCol_NavWindowingHighlight]      = p.accent;
        c[ImGuiCol_NavWindowingDimBg]          = hexColour(0x05070A, 0.5f);
        c[ImGuiCol_ModalWindowDimBg]           = hexColour(0x05070A, 0.5f);
    }
}
