#include "overlay/frame_draw.hpp"
#include "overlay/widgets.hpp"
#include "imgui_internal.h"

#include <cfloat>
#include <cmath>
#include <initializer_list>
#include <string>

#include "overlay/ui_map.hpp"

namespace vkBasalt
{
    namespace
    {
        constexpr float degrees = 3.14159265358979f / 180.0f;

        struct Pen
        {
            ImDrawList* list;
            ImVec2      at;
            float       unit;
            ImU32       colour;
            float       width;

            ImVec2 p(float x, float y) const { return ImVec2(at.x + x * unit, at.y + y * unit); }

            void line(float x0, float y0, float x1, float y1) const { list->AddLine(p(x0, y0), p(x1, y1), colour, width); }

            void lines(std::initializer_list<ImVec2> points) const
            {
                for (const ImVec2& point : points)
                    list->PathLineTo(p(point.x, point.y));
                list->PathStroke(colour, width);
            }

            void circle(float x, float y, float r) const { list->AddCircle(p(x, y), r * unit, colour, 0, width); }

            void rect(float x, float y, float w, float h, float rounding) const
            {
                list->AddRect(p(x, y), p(x + w, y + h), colour, rounding * unit, width);
            }

            void arc(float cx, float cy, float r, float fromDegrees, float toDegrees) const
            {
                list->PathArcTo(p(cx, cy), r * unit, fromDegrees * degrees, toDegrees * degrees);
                list->PathStroke(colour, width);
            }
        };
    }

    ImVec2 topLeft(const LayoutRect& rect)
    {
        return ImVec2(rect.x, rect.y);
    }

    ImVec2 bottomRight(const LayoutRect& rect)
    {
        return ImVec2(rect.right(), rect.bottom());
    }

    void drawIcon(ImDrawList* list, Icon icon, ImVec2 at, float size, ImU32 colour)
    {
        const float unit  = size / 24.0f;
        const float width = std::max(1.0f, (size >= 48.0f ? 1.5f : 1.8f) * unit);
        const Pen   pen{list, at, unit, colour, width};
        switch (icon)
        {
            case Icon::Sliders:
                pen.line(4, 6, 14, 6), pen.line(18, 6, 20, 6), pen.line(4, 12, 8, 12), pen.line(12, 12, 20, 12), pen.line(4, 18, 16, 18);
                pen.circle(16, 6, 2), pen.circle(10, 12, 2), pen.circle(18, 18, 2);
                break;
            case Icon::Frames:
                pen.rect(3, 8, 13, 10, 1.5f);
                pen.line(7, 5, 18.5f, 5);
                pen.arc(18.5f, 6.5f, 1.5f, -90.0f, 0.0f);
                pen.line(20, 6.5f, 20, 15);
                break;
            case Icon::Chip:
                pen.rect(6, 6, 12, 12, 2), pen.rect(9.5f, 9.5f, 5, 5, 0);
                for (float v : {9.0f, 15.0f})
                    pen.line(v, 2, v, 6), pen.line(v, 18, v, 22), pen.line(2, v, 6, v), pen.line(18, v, 22, v);
                break;
            case Icon::Chart:
                pen.line(5, 20, 5, 11), pen.line(11, 20, 11, 5), pen.line(17, 20, 17, 14), pen.line(3, 20, 21, 20);
                break;
            case Icon::Layers:
                pen.lines({{12, 3}, {21, 8}, {12, 13}, {3, 8}, {12, 3}});
                pen.lines({{3, 13}, {12, 18}, {21, 13}});
                break;
            case Icon::Bug:
                pen.rect(8, 6, 8, 13, 4);
                pen.line(12, 6, 12, 3), pen.line(8, 10, 4, 10), pen.line(16, 10, 20, 10), pen.line(8, 15, 4, 15), pen.line(16, 15, 20, 15);
                pen.line(9.5f, 3.5f, 10.7f, 5.8f), pen.line(14.5f, 3.5f, 13.3f, 5.8f);
                break;
            case Icon::Keyboard:
                pen.rect(2.5f, 6, 19, 12, 2);
                pen.line(6.5f, 10, 7.5f, 10), pen.line(10.5f, 10, 11.5f, 10), pen.line(14.5f, 10, 15.5f, 10), pen.line(18, 10, 18.5f, 10), pen.line(7, 14, 17, 14);
                break;
            case Icon::Gear:
                pen.circle(12, 12, 3.2f);
                pen.line(12, 2.5f, 12, 5.5f), pen.line(12, 18.5f, 12, 21.5f), pen.line(2.5f, 12, 5.5f, 12), pen.line(18.5f, 12, 21.5f, 12);
                pen.line(5.3f, 5.3f, 7.4f, 7.4f), pen.line(16.6f, 16.6f, 18.7f, 18.7f), pen.line(5.3f, 18.7f, 7.4f, 16.6f), pen.line(16.6f, 7.4f, 18.7f, 5.3f);
                break;
            case Icon::Person:
                pen.circle(12, 8, 4);
                list->PathLineTo(pen.p(4, 20));
                list->PathBezierCubicCurveTo(pen.p(5.5f, 16), pen.p(8.5f, 14), pen.p(12, 14));
                list->PathBezierCubicCurveTo(pen.p(15.5f, 14), pen.p(18.5f, 16), pen.p(20, 20));
                list->PathStroke(colour, width);
                break;
            case Icon::Power:
                pen.line(12, 3, 12, 11);
                pen.arc(12, 12.2f, 8, -44.5f, 224.5f);
                break;
            case Icon::Shield:
                list->PathLineTo(pen.p(12, 3));
                list->PathLineTo(pen.p(19, 6));
                list->PathLineTo(pen.p(19, 12));
                list->PathBezierCubicCurveTo(pen.p(19, 16.5f), pen.p(16, 19.5f), pen.p(12, 21));
                list->PathBezierCubicCurveTo(pen.p(8, 19.5f), pen.p(5, 16.5f), pen.p(5, 12));
                list->PathLineTo(pen.p(5, 6));
                list->PathStroke(colour, width, ImDrawFlags_Closed);
                pen.lines({{9, 12}, {11, 14}, {15, 10}});
                break;
            case Icon::Eye:
                list->PathLineTo(pen.p(2, 12));
                list->PathBezierQuadraticCurveTo(pen.p(12, 1), pen.p(22, 12));
                list->PathBezierQuadraticCurveTo(pen.p(12, 23), pen.p(2, 12));
                list->PathStroke(colour, width, ImDrawFlags_Closed);
                pen.circle(12, 12, 3);
                break;
            case Icon::Close:
                pen.line(6, 6, 18, 18), pen.line(18, 6, 6, 18);
                break;
            case Icon::Chevron:
                pen.lines({{6, 9}, {12, 15}, {18, 9}});
                break;
            case Icon::Lock:
                pen.rect(5, 11, 14, 10, 2);
                pen.line(8, 11, 8, 8), pen.line(16, 8, 16, 11);
                pen.arc(12, 8, 4, 180.0f, 360.0f);
                break;
            case Icon::Warning:
                pen.lines({{12, 3}, {22, 21}, {2, 21}, {12, 3}});
                pen.line(12, 10, 12, 15), pen.line(12, 18, 12, 18.5f);
                break;
            case Icon::Restart:
                pen.arc(12, 12, 8, 0.0f, 315.0f);
                pen.lines({{20, 4}, {20, 9}, {15, 9}});
                break;
        }
    }

    void drawWing(ImDrawList* list, ImVec2 at, float height, ImU32 ink, ImU32 cut)
    {
        const float  unit   = height / 19.0f;
        const ImVec2 origin = ImVec2(at.x + 2.0f * unit, at.y + 17.0f * unit);
        const auto   p      = [&](float x, float y) { return ImVec2(origin.x + x * unit, origin.y + y * unit); };
        const float  base   = 1.4f;
        for (int i = 0; i < 6; i++)
        {
            const float angle  = (8.0f + static_cast<float>(i) * 13.5f) * degrees;
            const float length = 6.0f + static_cast<float>(i) * 1.7f;
            const float dx     = std::cos(angle);
            const float dy     = -std::sin(angle);
            const float bend   = 0.06f * length;
            const float x0 = base * dx, y0 = base * dy;
            const float x1 = (base + length) * dx, y1 = (base + length) * dy;
            list->PathLineTo(p(x0, y0));
            list->PathBezierQuadraticCurveTo(p((x0 + x1) / 2 + std::sin(angle) * bend, (y0 + y1) / 2 + std::cos(angle) * bend), p(x1, y1));
            list->PathStroke(ink, std::max(1.0f, 1.15f * unit));
        }
        list->PathArcTo(p(0, 0), 5.2f * unit, -82.0f * degrees, -2.0f * degrees);
        list->PathStroke(cut, std::max(1.0f, 0.34f * unit));
        list->AddCircleFilled(p(0, 0), 1.8f * unit, ink);
    }

    ImVec2 keyCapSize(ImFont* font, float fontSize, const char* key)
    {
        const ImVec2 text = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, key);
        return ImVec2(text.x + fontSize * 0.9f, text.y + fontSize * 0.3f);
    }

    void drawKeyCap(ImDrawList* list, ImFont* font, float fontSize, ImVec2 at, const char* key, const OverlayPalette& palette)
    {
        const ImVec2 size = keyCapSize(font, fontSize, key);
        list->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), ImGui::GetColorU32(hexColour(0x3A424C)), fontSize * 0.3f);
        list->AddText(font, fontSize, ImVec2(at.x + fontSize * 0.45f, at.y + fontSize * 0.15f), ImGui::GetColorU32(palette.secondary), key);
    }

    void restartMark(const char* what, bool pending, const OverlayPalette& palette)
    {
        const float scale = ImGui::GetStyle().FontScaleMain;
        ImGui::SameLine(0.0f, restartMarkGap * scale);
        const ImVec2 at   = ImGui::GetCursorScreenPos();
        const float  size = restartMarkSize * scale;
        drawIcon(ImGui::GetWindowDrawList(), Icon::Restart, ImVec2(at.x, at.y + (ImGui::GetFrameHeight() - size) * 0.5f), size,
                 ImGui::GetColorU32(pending ? palette.warn : palette.faint));
        const ImVec2      box(size, ImGui::GetFrameHeight());
        const std::string name = std::string(what) + " restart mark";
        ImGui::ItemSize(box);
        UiMap::nameNextItem(name.c_str());
        if (ImGui::ItemAdd(ImRect(at, ImVec2(at.x + box.x, at.y + box.y)), ImGui::GetID(name.c_str()), nullptr, ImGuiItemFlags_NoNav))
            UiMap::markChecked(ImGui::GetItemID(), pending);
    }

    bool defaultMark(const char* what, bool changed, const std::string& defaultShown, const OverlayPalette& palette)
    {
        const float scale = ImGui::GetStyle().FontScaleMain;
        const float size  = restartMarkSize * scale;
        ui::alignRight(size);
        const ImVec2      at = ImGui::GetCursorScreenPos();
        const ImVec2      box(size, ImGui::GetFrameHeight());
        const ImRect      area(at, ImVec2(at.x + box.x, at.y + box.y));
        const std::string name = std::string(what) + " default mark";
        const ImGuiID     id   = ImGui::GetID(name.c_str());
        ImGui::ItemSize(box);
        UiMap::nameNextItem(name.c_str());
        if (!ImGui::ItemAdd(area, id, nullptr, ImGuiItemFlags_NoNav))
            return false;
        UiMap::markChecked(id, changed);
        bool       hovered = false;
        bool       held    = false;
        const bool pressed = ImGui::ButtonBehavior(area, id, &hovered, &held);
        const ImVec2 centre(at.x + size * 0.5f, at.y + box.y * 0.5f);
        if (changed)
            ImGui::GetWindowDrawList()->AddCircleFilled(centre, size * 0.28f, ImGui::GetColorU32(hovered ? palette.bright : palette.accent));
        else
            ImGui::GetWindowDrawList()->AddCircle(centre, size * 0.28f, ImGui::GetColorU32(palette.faint), 0, scale);
        if (changed)
            ImGui::SetItemTooltip("Changed: the default is %s. Click to set it back.", defaultShown.c_str());
        else
            ImGui::SetItemTooltip("The default");
        return changed && pressed;
    }

    TileState hitArea(const char* label, const LayoutRect& rect, bool enabled)
    {
        TileState state;
        ImGui::SetCursorScreenPos(topLeft(rect));
        ImGui::BeginDisabled(!enabled);
        UiMap::nameNextItem(label);
        state.clicked = ImGui::InvisibleButton(label, ImVec2(rect.w, rect.h));
        state.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        state.held    = ImGui::IsItemActive();
        ImGui::EndDisabled();
        return state;
    }

}
