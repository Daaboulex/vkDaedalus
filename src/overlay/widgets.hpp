#ifndef WIDGETS_HPP_INCLUDED
#define WIDGETS_HPP_INCLUDED

#include <cstddef>
#include <string>
#include <vector>

#include "imgui.h"

#include "overlay/frame_draw.hpp"
#include "overlay/overlay_style.hpp"

namespace vkBasalt::ui
{
    struct Size
    {
        static constexpr float control     = 32.0f;
        static constexpr float inRow       = 26.0f;
        static constexpr float row         = 40.0f;
        static constexpr float tallRow     = 52.0f;
        static constexpr float chip        = 20.0f;
        static constexpr float box         = 18.0f;
        static constexpr float knob        = 14.0f;
        static constexpr float track       = 4.0f;
        static constexpr float valueColumn = 80.0f;
        static constexpr float nameColumn  = 170.0f;
        static constexpr float labelColumn = 110.0f;
        static constexpr float radius      = 4.0f;
        static constexpr float surface     = 8.0f;
        static constexpr float gap         = 8.0f;
        static constexpr float groupGap    = 16.0f;
        static constexpr float cardPadX    = 18.0f;
        static constexpr float cardPadY    = 14.0f;
    };

    enum class Kind
    {
        Primary,
        Secondary,
        Quiet,
        Danger
    };

    enum class Mark
    {
        Measured,
        Estimated,
        Absent,
        Sum
    };

    struct BarPart
    {
        double value = 0.0;
        ImVec4 colour;
    };

    void                beginFrame(const OverlayFonts& fonts);
    const OverlayFonts& fonts();
    float               px(float designUnits);
    float               eased(ImGuiID id, const char* channel, bool on);
    ImVec4              mix(const ImVec4& a, const ImVec4& b, float t);

    float     buttonWidth(const char* label, Kind kind = Kind::Secondary, bool inRow = false);
    bool      button(const char* label, Kind kind = Kind::Secondary, float width = 0.0f, const ImVec4* border = nullptr);
    bool      rowButton(const char* label, float width = 0.0f, const ImVec4* border = nullptr);
    bool      lineButton(const char* label);
    bool      iconButton(const char* label, Icon icon, const char* tip = nullptr);
    bool      beginScrollArea(const char* id);
    void      endScrollArea();
    TileState pressable(const char* label, const LayoutRect& rect, Kind kind, bool enabled = true);
    TileState tile(const char* label, const LayoutRect& rect, bool selected, bool enabled = true);
    bool      listItem(const char* label, const char* tag, bool enabled = true);
    bool      disclosure(const char* id, const char* shown, const ImVec4& ink, float maxWidth, const char* trailing = nullptr, bool openAtFirst = false);
    bool      disclosureOpen(const char* id);
    bool      tab(const char* label, bool selected);
    bool      checkbox(const char* label, bool* value);
    bool      slider(const char* label, ImGuiDataType type, void* value, const void* lowest, const void* highest, const char* format = nullptr,
                     ImGuiSliderFlags flags = 0);
    bool      sliderFloat(const char* label, float* value, float lowest, float highest, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
    bool      sliderInt(const char* label, int* value, int lowest, int highest, ImGuiSliderFlags flags = 0);
    bool      parameterSlider(const char* label, ImGuiDataType type, void* value, int components, const void* lowest, const void* highest, const char* format);
    bool      parameterChoice(const char* label, int* value, const std::vector<std::string>& items);
    bool      parameterSwitch(const char* label, bool* value);
    bool      parameterSegments(const char* label, int* value, const std::vector<std::string>& items);
    bool      parameterColour(const char* label, float* colour, int components);
    bool      parameterNameFits(const char* label);
    bool      beginCombo(const char* label, const char* preview, float width = 0.0f);
    void      endCombo();
    bool      beginPopup(const char* id);
    bool      beginContextMenu(const char* id);
    bool      beginSubmenu(const char* label);
    void      endSubmenu();
    void      endPopup();
    void      pushMenuSpacing();
    void      menuSeparator();
    void      popupTitle(const char* text);
    void      errorText(const char* text);
    void      rightAlign(float width);
    std::string elideStart(const std::string& text, float room);
    void      sliderRowBegin(const char* text, const char* value, float trailing = 0.0f);
    void      sliderRowEnd(const char* value, float trailing = 0.0f);
    void      noteBox(const char* text, const ImVec4& ink);
    void      pushPopupPadding();
    bool      option(const char* label, bool selected, bool enabled = true, const ImVec4* ink = nullptr);
    bool      moreButton(const char* label, float width, bool menuOpen);
    void      nextColumn();
    void      popupShadow();
    bool      textField(const char* label, char* buffer, size_t size, const char* hint = "", float width = 0.0f, ImGuiInputTextFlags flags = 0);
    bool      intField(const char* label, int* value, int lowest, int highest);

    void heading(const char* text);
    void sectionLabel(const char* text);
    void label(const char* text);
    void note(const char* text);
    void notef(const char* format, ...) IM_FMTARGS(1);
    void chip(const char* text, const ImVec4& colour, const char* tip = nullptr);
    void divider();
    void alignRight(float width);
    void atColumn(float x);
    float heightLeftAboveFooter();
    void  footer();
    void drawChevron(ImDrawList* list, ImVec2 centre, float size, float open, ImU32 colour);

    void beginPanel();
    void beginPanelUnder(float inset);
    void endPanel();
    void panelRow(float designHeight = Size::row);
    void factRow(const char* label, const char* value, Mark mark = Mark::Measured, const ImVec4* swatch = nullptr);
    void factCell(const char* label, const char* value, Mark mark, const ImVec4* swatch, float width);
    void meter(const char* id, const char* label, float fraction, const char* value, float width);
    void splitBar(const std::vector<BarPart>& parts);
}

#endif
