#include "overlay/widgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "imgui_internal.h"
#include "overlay/elide_start.hpp"
#include "overlay/motion.hpp"
#include "overlay/ui_map.hpp"

namespace vkBasalt::ui
{
    namespace
    {
        const OverlayFonts* frameFonts = nullptr;

        struct Panel
        {
            ImDrawListSplitter splitter;
            ImVec2             start;
            float              width       = 0.0f;
            float              workMaxX    = 0.0f;
            float              contentMaxX = 0.0f;
            float              rowBottom   = 0.0f;
            float              pad         = 0.0f;
            float              bleed       = 0.0f;
            ImDrawFlags        corners     = ImDrawFlags_RoundCornersAll;
            int                rows        = 0;
        };
        std::array<Panel, 4> panels;
        size_t               panelDepth = 0;
        constexpr float      panelPad   = 12.0f;

        const OverlayPalette& palette()
        {
            return activePalette();
        }

        ImVec4 withAlpha(const ImVec4& c, float alpha)
        {
            return ImVec4(c.x, c.y, c.z, c.w * alpha);
        }

        ImU32 u32(const ImVec4& c)
        {
            return ImGui::GetColorU32(c);
        }

        float paddingFor(float designHeight)
        {
            return std::max(0.0f, (px(designHeight) - ImGui::GetFontSize()) * 0.5f);
        }

        void pointAt(bool hovered)
        {
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        float easedSoFar(ImGuiID id, const char* channel)
        {
            return ImGui::GetStateStorage()->GetFloat(ImGui::GetIDWithSeed(channel, nullptr, id), 0.0f);
        }

        std::string shownPart(const char* label)
        {
            return std::string(label, ImGui::FindRenderedTextEnd(label));
        }

        float parameterName(const char* label)
        {
            const ImVec2      at      = ImGui::GetCursorScreenPos();
            const float       nameW   = px(Size::nameColumn);
            const float       lineH   = px(Size::control);
            const float       textY   = at.y + (lineH - ImGui::GetFontSize()) * 0.5f;
            const float       nameMax = at.x + nameW - px(Size::gap);
            const std::string name    = shownPart(label);
            ImGui::PushStyleColor(ImGuiCol_Text, activePalette().secondary);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(at.x, textY), ImVec2(nameMax, textY + ImGui::GetFontSize()), nameMax, name.c_str(), nullptr,
                                      nullptr);
            ImGui::PopStyleColor();
            const float right = at.x + ImGui::GetContentRegionAvail().x - px(restartMarkSize) - px(Size::gap);
            ImGui::Dummy(ImVec2(nameW, lineH));
            ImGui::SameLine(0.0f, 0.0f);
            return right;
        }

        void placeBefore(float right, float width)
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(std::max(at.x, right - width), at.y));
        }

        void parameterValue(const char* values, float width)
        {
            const ImVec2 at    = ImGui::GetCursorScreenPos();
            const float  lineH = px(Size::control);
            const float  textY = at.y + (lineH - ImGui::GetFontSize()) * 0.5f;
            const ImVec2 size  = ImGui::CalcTextSize(values);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(std::max(at.x, at.x + width - size.x), textY), ImVec2(at.x + width, textY + ImGui::GetFontSize()),
                                      at.x + width, values, nullptr, &size);
            ImGui::Dummy(ImVec2(width, lineH));
        }

        struct Look
        {
            ImVec4 rest;
            ImVec4 hover;
            ImVec4 press;
            ImVec4 ink;
            ImVec4 border;
            ImVec4 borderHover;
        };

        Look lookOf(Kind kind)
        {
            const OverlayPalette& p = palette();
            const ImVec4          none(0.0f, 0.0f, 0.0f, 0.0f);
            switch (kind)
            {
                case Kind::Primary: return {p.strong, mix(p.strong, p.accent, 0.22f), mix(p.strong, p.bar, 0.25f), p.onStrong, none, none};
                case Kind::Quiet: return {withAlpha(p.control, 0.0f), p.control, p.tint, p.text, none, none};
                case Kind::Danger: return {mix(p.bad, p.panel, 0.3f), p.bad, mix(p.bad, p.bar, 0.25f), p.onStrong, none, none};
                case Kind::Secondary: break;
            }
            return {p.control, mix(p.control, p.accent, 0.12f), mix(p.control, p.accent, 0.22f), p.text, p.edge, mix(p.edge, p.accent, 0.45f)};
        }

        void paint(ImDrawList* list, const ImRect& box, const Look& look, float hover, bool pressed)
        {
            const float rounding = px(Size::radius);
            list->AddRectFilled(box.Min, box.Max, u32(pressed ? look.press : mix(look.rest, look.hover, hover)), rounding);
            if (look.border.w > 0.0f)
                list->AddRect(box.Min, box.Max, u32(mix(look.border, look.borderHover, hover)), rounding, 1.0f);
        }

        bool kitButton(const char* label, Kind kind, float width, float designHeight, float designPadX, const ImVec4* border, bool drawLabel = true)
        {
            Look look = lookOf(kind);
            if (border != nullptr)
            {
                look.border      = *border;
                look.borderHover = *border;
            }
            const bool bold = kind == Kind::Primary || kind == Kind::Danger;
            if (bold)
                ImGui::PushFont(frameFonts != nullptr ? frameFonts->bold : nullptr, FontSize::body);
            const ImGuiID id       = ImGui::GetID(label);
            const ImVec2  padding(px(designPadX), paddingFor(designHeight));
            const ImVec2  textSize = ImGui::CalcTextSize(label, nullptr, true);
            const ImVec2  at       = ImGui::GetCursorScreenPos();
            const ImVec2  size     = ImGui::CalcItemSize(ImVec2(width, 0.0f), textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
            const bool    held     = ImGui::GetActiveID() == id && ImGui::IsMouseHoveringRect(at, ImVec2(at.x + size.x, at.y + size.y));
            paint(ImGui::GetWindowDrawList(), ImRect(at, ImVec2(at.x + size.x, at.y + size.y)), look, easedSoFar(id, "hover"), held);

            const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, none);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, none);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, none);
            ImGui::PushStyleColor(ImGuiCol_Text, drawLabel ? look.ink : none);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            if (bold)
                ImGui::PopFont();
            const bool hovered = ImGui::IsItemHovered();
            eased(id, "hover", hovered);
            pointAt(hovered);
            return pressed;
        }

        template <typename Draw>
        bool fieldLook(const char* label, float width, Draw draw)
        {
            const OverlayPalette& p = palette();
            ImGui::PushID(label);
            ImGui::SetNextItemWidth(width > 0.0f ? width : ImGui::CalcItemWidth());
            ImGui::PushStyleColor(ImGuiCol_FrameBg, p.row);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, mix(p.row, p.accent, 0.06f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, p.row);
            ImGui::PushStyleColor(ImGuiCol_Border, p.edge);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10.0f), paddingFor(Size::control)));
            UiMap::nameNextItem(label);
            const bool changed = draw();
            const bool active  = ImGui::IsItemActive();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            if (active)
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), u32(p.accent), px(Size::radius), 1.0f);
            ImGui::PopID();
            return changed;
        }
    }

    void beginFrame(const OverlayFonts& fonts)
    {
        frameFonts = &fonts;
        for (Panel& panel : panels)
            panel.splitter.Clear();
        panelDepth = 0;
    }

    const OverlayFonts& fonts()
    {
        static const OverlayFonts none;
        return frameFonts != nullptr ? *frameFonts : none;
    }

    float px(float designUnits)
    {
        return designUnits * ImGui::GetStyle().FontScaleMain;
    }

    ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    float eased(ImGuiID id, const char* channel, bool on)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID key     = ImGui::GetIDWithSeed(channel, nullptr, id);
        const float   target  = on ? 1.0f : 0.0f;
        const float   value   = approach(storage->GetFloat(key, target), target, ImGui::GetIO().DeltaTime, Motion::hoverSeconds);
        storage->SetFloat(key, value);
        return value;
    }

    float buttonWidth(const char* label, Kind kind, bool inRow)
    {
        const bool bold = kind == Kind::Primary || kind == Kind::Danger;
        if (bold)
            ImGui::PushFont(fonts().bold, FontSize::body);
        const float width = ImGui::CalcTextSize(label, nullptr, true).x + px(inRow ? 20.0f : 28.0f);
        if (bold)
            ImGui::PopFont();
        return width;
    }

    bool button(const char* label, Kind kind, float width, const ImVec4* border)
    {
        return kitButton(label, kind, width, Size::control, 14.0f, border);
    }

    bool rowButton(const char* label, float width, const ImVec4* border)
    {
        return kitButton(label, Kind::Secondary, width, Size::inRow, 10.0f, border);
    }

    bool lineButton(const char* label)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(Size::control) - px(Size::inRow)) * 0.5f);
        return rowButton(label);
    }

    bool tab(const char* label, bool selected)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float  w  = buttonWidth(label, Kind::Quiet);
        if (selected)
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + w, at.y + px(Size::control)), u32(palette().tint), px(Size::radius));
        const bool pressed = kitButton(label, Kind::Quiet, w, Size::control, 14.0f, nullptr);
        UiMap::markChecked(ImGui::GetItemID(), selected);
        return pressed;
    }

    bool iconButton(const char* label, Icon icon, const char* tip)
    {
        const float side    = px(Size::inRow);
        const bool  pressed = kitButton(label, Kind::Quiet, side, Size::inRow, 0.0f, nullptr, false);
        const float glyph   = px(12.0f);
        const ImVec2 min    = ImGui::GetItemRectMin();
        drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(min.x + (side - glyph) * 0.5f, min.y + (side - glyph) * 0.5f), glyph, u32(palette().text));
        if (tip != nullptr)
            ImGui::SetItemTooltip("%s", tip);
        return pressed;
    }

    bool beginScrollArea(const char* id)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool shown = ImGui::BeginChild(id, ImVec2(0.0f, heightLeftAboveFooter()), ImGuiChildFlags_None);
        ImGui::PopStyleVar();
        ImGui::PushTextWrapPos(0.0f);
        return shown;
    }

    void endScrollArea()
    {
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    TileState pressable(const char* label, const LayoutRect& rect, Kind kind, bool enabled)
    {
        const TileState state = hitArea(label, rect, enabled);
        const bool      live  = state.hovered && enabled;
        paint(ImGui::GetWindowDrawList(), ImRect(topLeft(rect), bottomRight(rect)), lookOf(kind), eased(ImGui::GetItemID(), "hover", live), state.held && live);
        pointAt(live);
        return state;
    }

    TileState tile(const char* label, const LayoutRect& rect, bool selected, bool enabled)
    {
        const TileState       state  = hitArea(label, rect, enabled);
        const ImGuiID         id     = ImGui::GetItemID();
        const bool            live   = state.hovered && enabled;
        const OverlayPalette& p      = palette();
        const float           hover  = eased(id, "hover", live);
        const float           chosen = eased(id, "selected", selected);
        ImVec4                fill   = mix(p.panel, p.tint, chosen);
        fill                         = mix(fill, mix(fill, p.accent, 0.10f), hover);
        if (state.held && live)
            fill = mix(fill, p.accent, 0.16f);
        ImGui::GetWindowDrawList()->AddRectFilled(topLeft(rect), bottomRight(rect), u32(fill), px(Size::radius));
        pointAt(live);
        return state;
    }

    bool listItem(const char* label, const char* tag, bool enabled)
    {
        const OverlayPalette& p      = palette();
        const ImVec2          at     = ImGui::GetCursorScreenPos();
        const float           width  = ImGui::GetContentRegionAvail().x;
        const float           height = px(Size::row);
        ImGui::BeginDisabled(!enabled);
        UiMap::nameNextItem(label);
        const bool    pressed = ImGui::InvisibleButton(label, ImVec2(width, height));
        const ImGuiID id      = ImGui::GetItemID();
        const bool    hovered = ImGui::IsItemHovered();
        const bool    held    = ImGui::IsItemActive() && hovered;
        ImDrawList*   list    = ImGui::GetWindowDrawList();
        const ImRect  box(at, ImVec2(at.x + width, at.y + height));
        Look          look = lookOf(Kind::Secondary);
        look.rest          = p.row;
        look.hover         = mix(p.row, p.accent, 0.07f);
        look.border        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        look.borderHover   = look.border;
        paint(list, box, look, eased(id, "hover", hovered), held);

        const float  padX     = px(12.0f);
        const float  textY    = at.y + (height - ImGui::GetFontSize()) * 0.5f;
        float        textMaxX = box.Max.x - padX;
        if (tag != nullptr && *tag != '\0')
        {
            ImGui::PushFont(nullptr, FontSize::caption);
            const ImVec2 tagSize = ImGui::CalcTextSize(tag);
            list->AddText(ImVec2(textMaxX - tagSize.x, at.y + (height - ImGui::GetFontSize()) * 0.5f), u32(p.muted), tag);
            ImGui::PopFont();
            textMaxX -= tagSize.x + px(Size::gap);
        }
        const std::string shown = shownPart(label);
        ImGui::RenderTextEllipsis(list, ImVec2(box.Min.x + padX, textY), ImVec2(textMaxX, textY + ImGui::GetFontSize()), textMaxX, shown.c_str(), nullptr,
                                  nullptr);
        ImGui::EndDisabled();
        pointAt(hovered);
        return pressed;
    }

    void drawChevron(ImDrawList* list, ImVec2 centre, float size, float open, ImU32 colour)
    {
        const float angle = open * 1.5707963f;
        const float c     = std::cos(angle);
        const float s     = std::sin(angle);
        const auto  at    = [&](float x, float y) { return ImVec2(centre.x + (x * c - y * s) * size, centre.y + (x * s + y * c) * size); };
        list->PathLineTo(at(-0.15f, -0.3f));
        list->PathLineTo(at(0.15f, 0.0f));
        list->PathLineTo(at(-0.15f, 0.3f));
        list->PathStroke(colour, std::max(1.0f, size * 0.13f));
    }

    bool disclosure(const char* id, const char* shown, const ImVec4& ink, float maxWidth, const char* trailing, bool openAtFirst)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;
        ImGuiContext& g       = *GImGui;
        const ImGuiID itemId  = window->GetID(id);
        ImGuiStorage* storage = window->DC.StateStorage;
        bool          open    = storage->GetInt(itemId, openAtFirst ? 1 : 0) != 0;

        const float  lineH    = px(Size::control);
        const float  gap      = px(Size::gap);
        const float  chevron  = std::max(px(8.0f), ImGui::GetStyle().IndentSpacing - gap);
        const ImVec2 textSize = ImGui::CalcTextSize(shown, nullptr, false);
        const ImVec2 pos      = window->DC.CursorPos;
        const float  width    = std::max(chevron + gap, maxWidth);
        const ImRect box(pos, ImVec2(pos.x + width, pos.y + lineH));
        ImGui::ItemSize(box, (lineH - g.FontSize) * 0.5f);
        if (ImGui::ItemAdd(box, itemId))
        {
            bool hovered = false;
            bool held    = false;
            if (ImGui::ButtonBehavior(box, itemId, &hovered, &held, ImGuiButtonFlags_PressedOnClickRelease))
            {
                open = !open;
                storage->SetInt(itemId, open ? 1 : 0);
            }
            const float  opened = eased(itemId, "open", open);
            const float  hover  = eased(itemId, "hover", hovered);
            const ImVec4 inkNow = mix(ink, palette().bright, 0.35f * hover);
            drawChevron(window->DrawList, ImVec2(pos.x + chevron * 0.5f, pos.y + lineH * 0.5f), chevron, opened, u32(mix(ink, palette().panel, 0.3f)));
            const float textX = pos.x + chevron + gap;
            const float textY = pos.y + (lineH - g.FontSize) * 0.5f;
            const float trailingW = trailing != nullptr ? ImGui::CalcTextSize(trailing).x + gap : 0.0f;
            const float textMax   = box.Max.x - trailingW;
            ImGui::PushStyleColor(ImGuiCol_Text, inkNow);
            ImGui::RenderTextEllipsis(window->DrawList, ImVec2(textX, textY), ImVec2(textMax, textY + g.FontSize), textMax, shown, nullptr, &textSize);
            ImGui::PopStyleColor();
            if (trailing != nullptr)
                window->DrawList->AddText(ImVec2(std::min(textX + textSize.x, textMax) + gap, textY), u32(palette().muted), trailing);
            pointAt(hovered);
        }
        if (open)
            ImGui::TreePushOverrideID(itemId);
        IMGUI_TEST_ENGINE_ITEM_INFO(itemId, shown, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Openable | (open ? ImGuiItemStatusFlags_Opened : 0));
        return open;
    }

    bool disclosureOpen(const char* id)
    {
        return ImGui::GetStateStorage()->GetInt(ImGui::GetID(id), 0) != 0;
    }

    bool checkbox(const char* label, bool* value)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;
        ImGuiContext&     g        = *GImGui;
        const ImGuiID     id       = window->GetID(label);
        const char*       labelEnd = ImGui::FindRenderedTextEnd(label);
        const ImVec2      textSize = ImGui::CalcTextSize(label, labelEnd, false);
        const float       lineH    = px(Size::control);
        const float       side     = px(Size::box);
        const float       gap      = px(Size::gap);
        const ImVec2      pos      = window->DC.CursorPos;
        const ImRect      total(pos, ImVec2(pos.x + side + (textSize.x > 0.0f ? gap + textSize.x : 0.0f), pos.y + lineH));
        ImGui::ItemSize(total, (lineH - g.FontSize) * 0.5f);
        if (!ImGui::ItemAdd(total, id))
        {
            IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*value ? ImGuiItemStatusFlags_Checked : 0));
            return false;
        }
        bool       hovered = false;
        bool       held    = false;
        const bool pressed = ImGui::ButtonBehavior(total, id, &hovered, &held);
        if (pressed)
        {
            *value = !*value;
            ImGui::MarkItemEdited(id);
        }

        const OverlayPalette& p     = palette();
        const float           on    = eased(id, "on", *value);
        const float           hover = eased(id, "hover", hovered);
        const ImRect          box(ImVec2(pos.x, pos.y + (lineH - side) * 0.5f), ImVec2(pos.x + side, pos.y + (lineH + side) * 0.5f));
        const float           rounding = px(Size::radius);
        ImVec4                fill     = mix(mix(p.control, mix(p.control, p.accent, 0.14f), hover), mix(p.accent, p.bright, 0.12f * hover), on);
        if (held && hovered)
            fill = mix(fill, p.bar, 0.2f);
        ImGui::RenderNavCursor(total, id);
        window->DrawList->AddRectFilled(box.Min, box.Max, u32(fill), rounding);
        if (on < 1.0f)
            window->DrawList->AddRect(box.Min, box.Max, u32(withAlpha(mix(p.edge, p.accent, 0.6f * hover), 1.0f - on)), rounding, 1.0f);
        if (on > 0.0f)
        {
            const float pad = side * 0.2f;
            ImGui::RenderCheckMark(window->DrawList, ImVec2(box.Min.x + pad, box.Min.y + pad), u32(withAlpha(p.panel, on)), side - pad * 2.0f);
        }
        if (textSize.x > 0.0f)
            ImGui::RenderText(ImVec2(box.Max.x + gap, pos.y + (lineH - g.FontSize) * 0.5f), label, labelEnd, false);
        pointAt(hovered);
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*value ? ImGuiItemStatusFlags_Checked : 0));
        return pressed;
    }

    bool slider(const char* label, ImGuiDataType type, void* value, const void* lowest, const void* highest, const char* format, ImGuiSliderFlags flags)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;
        ImGuiContext& g     = *GImGui;
        const ImGuiID id    = window->GetID(label);
        const float   width = ImGui::CalcItemWidth();
        const float   lineH = px(Size::control);
        const ImVec2  pos   = window->DC.CursorPos;
        const ImRect  frame(pos, ImVec2(pos.x + width, pos.y + lineH));
        const bool    typingAllowed = (flags & ImGuiSliderFlags_NoInput) == 0;
        ImGui::ItemSize(frame, (lineH - g.FontSize) * 0.5f);
        if (!ImGui::ItemAdd(frame, id, &frame, typingAllowed ? ImGuiItemFlags_Inputable : 0))
        {
            IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (typingAllowed ? ImGuiItemStatusFlags_Inputable : 0));
            return false;
        }
        if (format == nullptr)
            format = ImGui::DataTypeGetInfo(type)->PrintFmt;

        const bool hovered = ImGui::ItemHoverable(frame, id, g.LastItemData.ItemFlags);
        bool       typing  = typingAllowed && ImGui::TempInputIsActive(id);
        if (!typing)
        {
            const bool clicked    = hovered && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);
            const bool makeActive = clicked || g.NavActivateId == id;
            if (makeActive && clicked)
                ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
            if (makeActive && typingAllowed && ((clicked && g.IO.KeyCtrl) || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput))))
                typing = true;
            if (makeActive && !typing)
            {
                ImGui::SetActiveID(id, window);
                ImGui::SetFocusID(id, window);
                ImGui::FocusWindow(window);
                g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
            }
        }

        const OverlayPalette& p = palette();
        if (typing)
        {
            const bool clamp = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
            ImGui::PushStyleColor(ImGuiCol_FrameBg, p.row);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(Size::radius));
            const bool changed = ImGui::TempInputScalar(frame, id, label, type, value, format, clamp ? lowest : nullptr, clamp ? highest : nullptr);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            window->DrawList->AddRect(frame.Min, frame.Max, u32(p.accent), px(Size::radius), 1.0f);
            return changed;
        }

        ImRect     grab;
        const bool changed = ImGui::SliderBehavior(frame, id, type, value, lowest, highest, format, flags, &grab);
        if (changed)
            ImGui::MarkItemEdited(id);

        const bool  active = g.ActiveId == id;
        const float hover  = eased(id, "hover", hovered || active);
        const float centreY = frame.GetCenter().y;
        const float track   = px(Size::track);
        if (grab.Max.x > grab.Min.x)
        {
            const float left  = frame.Min.x + 2.0f;
            const float right = frame.Max.x - 2.0f;
            const float knobX = grab.GetCenter().x;
            const float knobR = px(Size::knob) * 0.5f;
            window->DrawList->AddRectFilled(ImVec2(left - track * 0.5f, centreY - track * 0.5f), ImVec2(right + track * 0.5f, centreY + track * 0.5f),
                                            u32(mix(p.control, p.edge, 0.6f + 0.4f * hover)), track * 0.5f);
            window->DrawList->AddRectFilled(ImVec2(left - track * 0.5f, centreY - track * 0.5f), ImVec2(knobX, centreY + track * 0.5f), u32(p.accent), track * 0.5f);
            if (hover > 0.0f)
                window->DrawList->AddCircleFilled(ImVec2(knobX, centreY), knobR + px(5.0f) * hover, u32(withAlpha(p.accent, 0.18f * hover)));
            window->DrawList->AddCircleFilled(ImVec2(knobX, centreY), knobR, u32(active ? mix(p.accent, p.bright, 0.35f) : p.accent));
        }
        ImGui::RenderNavCursor(frame, id);
        pointAt(hovered);
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (typingAllowed ? ImGuiItemStatusFlags_Inputable : 0));
        return changed;
    }

    bool sliderFloat(const char* label, float* value, float lowest, float highest, const char* format, ImGuiSliderFlags flags)
    {
        return slider(label, ImGuiDataType_Float, value, &lowest, &highest, format, flags);
    }

    bool sliderInt(const char* label, int* value, int lowest, int highest, ImGuiSliderFlags flags)
    {
        return slider(label, ImGuiDataType_S32, value, &lowest, &highest, "%d", flags);
    }

    bool parameterSlider(const char* label, ImGuiDataType type, void* value, int components, const void* lowest, const void* highest, const char* format)
    {
        const float  gap      = px(Size::gap);
        const size_t typeSize = ImGui::DataTypeGetInfo(type)->Size;
        if (format == nullptr)
            format = ImGui::DataTypeGetInfo(type)->PrintFmt;
        const auto valuesOf = [&]
        {
            std::string values;
            for (int i = 0; i < std::max(1, components); i++)
            {
                char part[64];
                ImGui::DataTypeFormatString(part, sizeof(part), type, static_cast<const char*>(value) + static_cast<size_t>(i) * typeSize, format);
                values += (i > 0 ? ", " : "") + std::string(part);
            }
            return values;
        };
        const float right   = parameterName(label);
        const float valueW  = std::max(px(Size::valueColumn), ImGui::CalcTextSize(valuesOf().c_str()).x);
        const float sliderW = std::max(px(60.0f), right - valueW - gap - ImGui::GetCursorScreenPos().x);
        bool        changed = false;
        if (components <= 1)
        {
            ImGui::SetNextItemWidth(sliderW);
            changed = slider(label, type, value, lowest, highest, format);
        }
        else
        {
            ImGui::PushID(label);
            const float partW = (sliderW - gap * static_cast<float>(components - 1)) / static_cast<float>(components);
            for (int i = 0; i < components; i++)
            {
                ImGui::PushID(i);
                if (i > 0)
                    ImGui::SameLine(0.0f, gap);
                if (i == 0)
                    UiMap::nameNextItem(label);
                ImGui::SetNextItemWidth(partW);
                changed |= slider("", type, static_cast<char*>(value) + static_cast<size_t>(i) * typeSize, lowest, highest, format);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::SameLine(0.0f, gap);
        parameterValue(valuesOf().c_str(), valueW);
        return changed;
    }

    bool parameterChoice(const char* label, int* value, const std::vector<std::string>& items)
    {
        const float right = parameterName(label);
        const float width = std::min(px(220.0f), std::max(px(60.0f), right - ImGui::GetCursorScreenPos().x));
        bool        changed = false;
        placeBefore(right, width);
        const bool known = *value >= 0 && static_cast<size_t>(*value) < items.size();
        if (beginCombo(label, known ? items[static_cast<size_t>(*value)].c_str() : "", width))
        {
            for (size_t i = 0; i < items.size(); i++)
                if (option(items[i].c_str(), static_cast<int>(i) == *value) && static_cast<int>(i) != *value)
                {
                    *value  = static_cast<int>(i);
                    changed = true;
                }
            endCombo();
        }
        return changed;
    }

    bool parameterSwitch(const char* label, bool* value)
    {
        const float right = parameterName(label);
        placeBefore(right, px(Size::box));
        ImGui::PushID(label);
        UiMap::nameNextItem(label);
        const bool changed = checkbox("", value);
        ImGui::PopID();
        return changed;
    }

    bool parameterSegments(const char* label, int* value, const std::vector<std::string>& items)
    {
        const float gap  = px(Size::gap);
        const float room = ImGui::GetContentRegionAvail().x - px(Size::nameColumn) - px(restartMarkSize) - gap;
        float       need = gap * static_cast<float>(items.empty() ? 0 : items.size() - 1);
        for (const std::string& item : items)
            need += buttonWidth(item.c_str(), Kind::Quiet);
        if (items.empty() || items.size() > 4 || need > room)
            return parameterChoice(label, value, items);
        const float right = parameterName(label);
        placeBefore(right, need);
        bool changed = false;
        ImGui::PushID(label);
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, gap);
            if (tab(items[i].c_str(), static_cast<int>(i) == *value) && static_cast<int>(i) != *value)
            {
                *value  = static_cast<int>(i);
                changed = true;
            }
        }
        ImGui::PopID();
        return changed;
    }

    bool parameterColour(const char* label, float* colour, int components)
    {
        const float gap    = px(Size::gap);
        const float side   = px(Size::box);
        const float lineH  = px(Size::control);
        const float valueW = px(Size::valueColumn);
        const float right  = parameterName(label);
        const auto  byte   = [](float c) { return static_cast<int>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); };
        char        hex[16];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", byte(colour[0]), byte(colour[1]), byte(colour[2]));
        placeBefore(right - valueW - gap, side);
        const ImVec2 at      = ImGui::GetCursorScreenPos();
        const bool   pressed = ImGui::InvisibleButton(label, ImVec2(side, lineH));
        const bool   hovered = ImGui::IsItemHovered();
        ImDrawList*  list    = ImGui::GetWindowDrawList();
        const ImVec2 min(at.x, at.y + (lineH - side) * 0.5f);
        const ImVec2 max(at.x + side, min.y + side);
        list->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(colour[0], colour[1], colour[2], 1.0f)), px(Size::radius));
        list->AddRect(min, max, u32(hovered ? palette().accent : palette().edge), px(Size::radius), 1.0f);
        pointAt(hovered);
        bool changed = false;
        ImGui::PushID(label);
        if (pressed)
            ImGui::OpenPopup("##colour");
        if (beginPopup("##colour"))
        {
            const ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar;
            ImGui::SetNextItemWidth(px(200.0f));
            changed = components >= 4 ? ImGui::ColorPicker4("##picker", colour, flags | ImGuiColorEditFlags_AlphaBar) : ImGui::ColorPicker3("##picker", colour, flags);
            endPopup();
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, gap);
        parameterValue(hex, valueW);
        return changed;
    }

    bool parameterNameFits(const char* label)
    {
        return ImGui::CalcTextSize(shownPart(label).c_str()).x <= px(Size::nameColumn) - px(Size::gap);
    }

    bool beginCombo(const char* label, const char* preview, float width)
    {
        const OverlayPalette& p      = palette();
        const float           w      = width > 0.0f ? width : ImGui::CalcItemWidth();
        const ImVec2          at     = ImGui::GetCursorScreenPos();
        const float           h      = px(Size::control);
        ImDrawList*           list   = ImGui::GetWindowDrawList();
        const std::string     hidden = std::string(label).rfind("##", 0) == 0 ? std::string(label) : "##" + std::string(label);
        const bool            hovered = ImGui::IsMouseHoveringRect(at, ImVec2(at.x + w, at.y + h)) && ImGui::IsWindowHovered();
        const ImGuiID         id      = ImGui::GetID(hidden.c_str());

        ImGui::PushStyleColor(ImGuiCol_FrameBg, p.row);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, mix(p.row, p.accent, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, mix(p.row, p.accent, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_Border, mix(p.edge, p.accent, 0.45f * easedSoFar(id, "hover")));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(12.0f), paddingFor(Size::control)));
        pushPopupPadding();
        ImGui::SetNextItemWidth(w);
        UiMap::nameNextItem(label);
        const bool open = ImGui::BeginCombo(hidden.c_str(), "", ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
        if (open)
        {
            popupShadow();
            pushMenuSpacing();
        }

        const float chevron = px(14.0f);
        const float textX   = at.x + px(12.0f);
        const float textMax = at.x + w - px(12.0f) - chevron - px(6.0f);
        const float textY   = at.y + (h - ImGui::GetFontSize()) * 0.5f;
        if (preview != nullptr && *preview != '\0')
            ImGui::RenderTextEllipsis(list, ImVec2(textX, textY), ImVec2(textMax, textY + ImGui::GetFontSize()), textMax, preview, nullptr, nullptr);
        drawChevron(list, ImVec2(at.x + w - px(12.0f) - chevron * 0.5f, at.y + h * 0.5f), chevron, open ? 1.0f : 0.95f, u32(p.muted));
        if (!open)
        {
            eased(id, "hover", hovered);
            pointAt(hovered);
        }
        return open;
    }

    void endCombo()
    {
        ImGui::PopStyleVar();
        ImGui::EndCombo();
    }

    void pushMenuSpacing()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, px(Size::control) - ImGui::GetFontSize()));
    }

    void menuSeparator()
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float  w  = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x - px(4.0f), at.y), ImVec2(at.x + w + px(4.0f), at.y), u32(palette().chipEdge), px(1.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    void popupTitle(const char* text)
    {
        ImGui::PushFont(fonts().bold, FontSize::body);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    }

    void errorText(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().bad);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    void sliderRowBegin(const char* text, const char* value, float trailing)
    {
        const float start = ImGui::GetCursorPosX();
        label(text);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), start + px(Size::labelColumn)));
        const float valueWidth = std::max(ImGui::CalcTextSize(value).x, px(48.0f));
        ImGui::SetNextItemWidth(std::max(px(60.0f), ImGui::GetContentRegionAvail().x - valueWidth - ImGui::GetStyle().ItemSpacing.x - trailing));
    }

    void sliderRowEnd(const char* value, float trailing)
    {
        alignRight(ImGui::CalcTextSize(value).x + trailing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(palette().secondary, "%s", value);
    }

    void noteBox(const char* text, const ImVec4& ink)
    {
        const ImVec2 at   = ImGui::GetCursorScreenPos();
        const float  w    = ImGui::GetContentRegionAvail().x;
        const float  padX = px(12.0f);
        const float  padY = px(Size::gap);
        const ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, w - 2.0f * padX);
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + w, at.y + size.y + 2.0f * padY), u32(mix(palette().row, ink, 0.12f)), px(Size::radius));
        ImGui::SetCursorScreenPos(ImVec2(at.x + padX, at.y + padY));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 2.0f * padX);
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + size.y + 2.0f * padY));
        ImGui::Dummy(ImVec2(w, 0.0f));
    }

    void rightAlign(float width)
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > width)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
    }

    std::string elideStart(const std::string& text, float room)
    {
        return vkBasalt::elideStart(text, room, [](const std::string& shown) { return ImGui::CalcTextSize(shown.c_str()).x; });
    }

    void pushPopupPadding()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12.0f), px(10.0f)));
    }

    bool beginPopup(const char* id)
    {
        pushPopupPadding();
        const bool open = ImGui::BeginPopup(id);
        ImGui::PopStyleVar();
        if (open)
        {
            popupShadow();
            pushMenuSpacing();
        }
        return open;
    }

    bool beginContextMenu(const char* id)
    {
        pushPopupPadding();
        const bool open = ImGui::BeginPopupContextItem(id);
        ImGui::PopStyleVar();
        if (open)
        {
            popupShadow();
            pushMenuSpacing();
        }
        return open;
    }

    bool beginSubmenu(const char* label)
    {
        pushPopupPadding();
        const bool open = ImGui::BeginMenu(label);
        ImGui::PopStyleVar();
        if (open)
            popupShadow();
        return open;
    }

    void endSubmenu()
    {
        ImGui::EndMenu();
    }

    void endPopup()
    {
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    bool option(const char* label, bool selected, bool enabled, const ImVec4* ink)
    {
        const OverlayPalette& p       = palette();
        const ImGuiStyle&     style   = ImGui::GetStyle();
        const ImVec2          at      = ImGui::GetCursorScreenPos();
        const float           half    = style.ItemSpacing.y * 0.5f;
        const ImVec2          from(at.x - px(4.0f), at.y - half);
        const ImVec2          to(at.x + ImGui::GetContentRegionAvail().x + px(4.0f), at.y + ImGui::GetFontSize() + half);
        const ImGuiID         id      = ImGui::GetID(label);
        const bool            hovered = enabled && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(from, to);
        const float           hover   = eased(id, "hover", hovered);
        const float           chosen  = eased(id, "selected", selected);
        if (hover > 0.0f || chosen > 0.0f)
            ImGui::GetWindowDrawList()->AddRectFilled(from, to, u32(mix(withAlpha(p.tint, chosen), mix(p.control, p.accent, 0.14f), hover)), px(Size::radius));
        const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Header, none);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
        if (ink != nullptr)
            ImGui::PushStyleColor(ImGuiCol_Text, *ink);
        ImGui::BeginDisabled(!enabled);
        const bool picked = ImGui::Selectable(label, selected);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(ink != nullptr ? 4 : 3);
        pointAt(hovered);
        return picked;
    }

    bool moreButton(const char* label, float width, bool menuOpen)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (menuOpen)
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + width, at.y + px(Size::inRow)), u32(palette().tint), px(Size::radius));
        const bool pressed = kitButton(label, Kind::Quiet, width, Size::inRow, 10.0f, nullptr, false);
        const ImVec2 min    = ImGui::GetItemRectMin();
        const ImVec2 max    = ImGui::GetItemRectMax();
        const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float  step   = px(5.0f);
        for (int i = -1; i <= 1; i++)
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(centre.x + step * static_cast<float>(i), centre.y), px(1.6f), u32(palette().text));
        return pressed;
    }

    void nextColumn()
    {
        ImGui::TableNextColumn();
        ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0f;
    }

    void popupShadow()
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImRect box    = window->Rect();
        const float  reach  = px(14.0f);
        const float  drop   = px(6.0f);
        window->DrawList->PushClipRect(ImVec2(box.Min.x - reach, box.Min.y - reach), ImVec2(box.Max.x + reach, box.Max.y + reach + drop), false);
        for (int i = 1; i <= 7; i++)
        {
            const float grow = px(2.0f) * static_cast<float>(i);
            window->DrawList->AddRect(ImVec2(box.Min.x - grow, box.Min.y - grow + drop * 0.5f), ImVec2(box.Max.x + grow, box.Max.y + grow + drop),
                                      u32(ImVec4(0.0f, 0.0f, 0.0f, 0.10f * (1.0f - static_cast<float>(i) / 8.0f))), px(6.0f) + grow, px(2.0f));
        }
        window->DrawList->PopClipRect();
    }

    bool textField(const char* label, char* buffer, size_t size, const char* hint, float width, ImGuiInputTextFlags flags)
    {
        return fieldLook(label, width, [&] { return ImGui::InputTextWithHint("", hint, buffer, size, flags); });
    }

    bool intField(const char* label, int* value, int lowest, int highest)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::BeginDisabled(*value <= lowest);
        if (button("-", Kind::Secondary, px(Size::control)))
        {
            *value  = std::max(lowest, *value - 1);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::SameLine();
        if (fieldLook(label, px(52.0f), [&] { return ImGui::InputInt("", value, 0, 0); }))
            changed = true;
        ImGui::SameLine();
        ImGui::PushID(label);
        ImGui::BeginDisabled(*value >= highest);
        if (button("+", Kind::Secondary, px(Size::control)))
        {
            *value  = std::min(highest, *value + 1);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        *value = std::clamp(*value, lowest, highest);
        return changed;
    }

    void heading(const char* text)
    {
        ImGui::PushFont(fonts().heading, FontSize::heading);
        const ImVec2 size  = ImGui::CalcTextSize(text);
        const ImVec2 at    = ImGui::GetCursorScreenPos();
        const float  lineH = px(Size::control);
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x, at.y + (lineH - ImGui::GetFontSize()) * 0.5f), u32(palette().text), text);
        ImGui::PopFont();
        ImGui::ItemSize(ImVec2(size.x, lineH), paddingFor(Size::control));
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + size.x, at.y + lineH)), 0);
    }

    void sectionLabel(const char* text)
    {
        ImGui::PushFont(fonts().bold, FontSize::label);
        ImGui::TextColored(palette().muted, "%s", text);
        ImGui::PopFont();
    }

    void label(const char* text)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
    }

    void note(const char* text)
    {
        ImGui::PushFont(nullptr, FontSize::small);
        ImGui::PushStyleColor(ImGuiCol_Text, palette().muted);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void notef(const char* format, ...)
    {
        char    text[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        note(text);
    }

    void chip(const char* text, const ImVec4& colour, const char* tip)
    {
        ImGui::PushFont(nullptr, FontSize::caption);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float  h        = px(Size::chip);
        const float  padX     = px(8.0f);
        const float  w        = textSize.x + padX * 2.0f;
        const float  lineH    = px(Size::control);
        const ImVec2 at       = ImGui::GetCursorScreenPos();
        const ImRect pill(ImVec2(at.x, at.y + (lineH - h) * 0.5f), ImVec2(at.x + w, at.y + (lineH + h) * 0.5f));
        ImDrawList*  list = ImGui::GetWindowDrawList();
        list->AddRect(pill.Min, pill.Max, u32(mix(colour, palette().panel, 0.45f)), h * 0.5f, 1.0f);
        list->AddText(ImVec2(pill.Min.x + padX, pill.Min.y + (h - ImGui::GetFontSize()) * 0.5f), u32(colour), text);
        ImGui::PopFont();
        ImGui::ItemSize(ImVec2(w, lineH), paddingFor(Size::control));
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + w, at.y + lineH)), 0);
        if (tip != nullptr && *tip != '\0')
            ImGui::SetItemTooltip("%s", tip);
    }

    void divider()
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float  y      = ImGui::GetCursorScreenPos().y;
        window->DrawList->PushClipRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), false);
        window->DrawList->AddLine(ImVec2(window->Pos.x, y), ImVec2(window->Pos.x + window->Size.x, y), u32(palette().line), 1.0f);
        window->DrawList->PopClipRect();
        ImGui::Dummy(ImVec2(0.0f, px(Size::gap) * 0.5f));
    }

    float heightLeftAboveFooter()
    {
        const ImGuiTable* table      = ImGui::GetCurrentTable();
        const float       cellBottom = table != nullptr ? table->RowCellPaddingY : 0.0f;
        return ImGui::GetCurrentWindow()->ContentRegionRect.Max.y - ImGui::GetCursorScreenPos().y - separatorAndButtonRowHeight() - cellBottom;
    }

    void footer()
    {
        const float pinned = ImGui::GetCurrentWindow()->ContentRegionRect.Max.y - separatorAndButtonRowHeight() + ImGui::GetStyle().ItemSpacing.y;
        const ImVec2 below = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(below.x, std::max(pinned, below.y)));
        divider();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        UiMap::nameNextItem("##footer");
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + 1.0f)), ImGui::GetID("##footer"));
    }

    void atColumn(float x)
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(x);
    }

    void alignRight(float width)
    {
        ImGui::SameLine();
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > width)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
    }

    namespace
    {
        void openPanel(float bleed, float pad, ImDrawFlags corners)
        {
            if (panelDepth == panels.size())
                throw std::logic_error("panels nest deeper than the overlay kit allows");
            Panel&       p      = panels[panelDepth++];
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const ImVec2 at     = ImGui::GetCursorScreenPos();
            p.start             = ImVec2(at.x - bleed, at.y);
            p.width             = ImGui::GetContentRegionAvail().x + 2.0f * bleed;
            p.pad               = pad;
            p.bleed             = bleed;
            p.corners           = corners;
            p.rows              = 0;
            p.rowBottom         = p.start.y;
            p.workMaxX          = window->WorkRect.Max.x;
            p.contentMaxX       = window->ContentRegionRect.Max.x;
            p.splitter.Split(window->DrawList, 2);
            p.splitter.SetCurrentChannel(window->DrawList, 1);
            window->WorkRect.Max.x          = p.start.x + p.width - pad;
            window->ContentRegionRect.Max.x = window->WorkRect.Max.x;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            if (pad > bleed)
                ImGui::Indent(pad - bleed);
            ImGui::BeginGroup();
        }
    }

    void beginPanel()
    {
        openPanel(0.0f, px(panelPad), ImDrawFlags_RoundCornersAll);
    }

    void beginPanelUnder(float inset)
    {
        openPanel(inset, inset, ImDrawFlags_RoundCornersBottom);
    }

    void panelRow(float designHeight)
    {
        if (panelDepth == 0)
            throw std::logic_error("a panel row outside a panel");
        Panel&      p   = panels[panelDepth - 1];
        const float top = std::max(ImGui::GetCursorScreenPos().y, p.rowBottom);
        if (p.rows > 0)
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.start.x + p.pad, top), ImVec2(p.start.x + p.width - p.pad, top), u32(palette().hairline), 1.0f);
        p.rows++;
        p.rowBottom                = top + px(designHeight);
        const ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::SetCursorScreenPos(ImVec2(window->Pos.x + window->DC.Indent.x + window->DC.ColumnsOffset.x, top + std::max(0.0f, px(designHeight) - px(Size::control)) * 0.5f));
    }

    void endPanel()
    {
        if (panelDepth == 0)
            throw std::logic_error("a panel closed that was never opened");
        Panel& p = panels[--panelDepth];
        if (ImGui::GetCursorScreenPos().y < p.rowBottom)
            ImGui::Dummy(ImVec2(0.0f, p.rowBottom - ImGui::GetCursorScreenPos().y));
        ImGui::EndGroup();
        const float bottom = std::max(ImGui::GetItemRectMax().y, p.rowBottom);
        if (p.pad > p.bleed)
            ImGui::Unindent(p.pad - p.bleed);
        ImGui::PopStyleVar();
        ImGuiWindow* window             = ImGui::GetCurrentWindow();
        window->WorkRect.Max.x          = p.workMaxX;
        window->ContentRegionRect.Max.x = p.contentMaxX;
        p.splitter.SetCurrentChannel(window->DrawList, 0);
        if (p.bleed > 0.0f)
            window->DrawList->PushClipRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), true);
        window->DrawList->AddRectFilled(p.start, ImVec2(p.start.x + p.width, bottom), u32(palette().row), px(Size::surface), p.corners);
        if (p.bleed > 0.0f)
            window->DrawList->PopClipRect();
        p.splitter.Merge(window->DrawList);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    void factRow(const char* label, const char* value, Mark mark, const ImVec4* swatch)
    {
        panelRow(Size::control);
        factCell(label, value, mark, swatch, ImGui::GetContentRegionAvail().x);
    }

    void factCell(const char* label, const char* value, Mark mark, const ImVec4* swatch, float w)
    {
        ImGui::PushFont(nullptr, FontSize::label);
        const OverlayPalette& p      = palette();
        const ImVec2          at     = ImGui::GetCursorScreenPos();
        const float           h      = px(Size::control);
        const float           textY  = at.y + (h - ImGui::GetFontSize()) * 0.5f;
        ImDrawList*           list   = ImGui::GetWindowDrawList();
        float                 x      = at.x;
        const bool            sum    = mark == Mark::Sum;
        const float           tildeW = mark == Mark::Estimated ? ImGui::CalcTextSize("~").x : 0.0f;
        if (swatch != nullptr)
        {
            const float side = px(10.0f);
            list->AddRectFilled(ImVec2(x, at.y + (h - side) * 0.5f), ImVec2(x + side, at.y + (h + side) * 0.5f), u32(*swatch), px(2.0f));
            x += side + px(Size::gap);
        }
        ImGui::PushFont(sum ? fonts().bold : nullptr, FontSize::label);
        const float valueW = ImGui::CalcTextSize(value).x;
        const float valueX = at.x + w - valueW;
        ImGui::PopFont();
        const float labelMax = valueX - tildeW - px(Size::gap);
        ImGui::PushStyleColor(ImGuiCol_Text, sum ? p.text : p.muted);
        ImGui::RenderTextEllipsis(list, ImVec2(x, textY), ImVec2(labelMax, textY + ImGui::GetFontSize()), labelMax, label, nullptr, nullptr);
        ImGui::PopStyleColor();
        if (mark == Mark::Estimated)
            list->AddText(ImVec2(valueX - tildeW, textY), u32(p.warn), "~");
        ImGui::PushFont(sum ? fonts().bold : nullptr, FontSize::label);
        list->AddText(ImVec2(valueX, textY), u32(mark == Mark::Absent ? p.faint : p.text), value);
        ImGui::PopFont();
        ImGui::PopFont();
        ImGui::ItemSize(ImVec2(w, h));
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + w, at.y + h)), 0);
    }

    void meter(const char* id, const char* label, float fraction, const char* value, float width)
    {
        ImGui::PushFont(nullptr, FontSize::small);
        const OverlayPalette& p      = palette();
        const ImVec2          at     = ImGui::GetCursorScreenPos();
        const float           h      = px(Size::control);
        const float           gap    = px(Size::gap);
        const float           textY  = at.y + (h - ImGui::GetFontSize()) * 0.5f;
        const float           track  = px(6.0f);
        const float           shown  = fraction > 0.0f ? std::min(fraction, 1.0f) : 0.0f;
        ImDrawList*           list   = ImGui::GetWindowDrawList();
        const float           labelW = ImGui::CalcTextSize(label).x;
        const float           valueW = ImGui::CalcTextSize(value).x;
        const float           barL   = at.x + labelW + gap;
        const float           barR   = std::max(barL, at.x + width - valueW - gap);
        const float           barY   = at.y + (h - track) * 0.5f;
        list->AddText(ImVec2(at.x, textY), u32(p.muted), label);
        list->AddRectFilled(ImVec2(barL, barY), ImVec2(barR, barY + track), u32(p.well), track * 0.5f);
        if (shown > 0.0f)
            list->AddRectFilled(ImVec2(barL, barY), ImVec2(barL + (barR - barL) * shown, barY + track), u32(p.accent), track * 0.5f);
        list->AddText(ImVec2(barR + gap, textY), u32(p.text), value);
        ImGui::PopFont();
        ImGui::ItemSize(ImVec2(width, h));
        UiMap::nameNextItem(id);
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + width, at.y + h)), ImGui::GetID(id));
    }

    void splitBar(const std::vector<BarPart>& parts)
    {
        const ImVec2 at    = ImGui::GetCursorScreenPos();
        const float  w     = ImGui::GetContentRegionAvail().x;
        const float  h     = px(8.0f);
        const float  gap   = px(2.0f);
        ImDrawList*  list  = ImGui::GetWindowDrawList();
        double       total = 0.0;
        size_t       shown = 0;
        for (const BarPart& part : parts)
            if (part.value > 0.0)
            {
                total += part.value;
                shown++;
            }
        list->AddRectFilled(at, ImVec2(at.x + w, at.y + h), u32(palette().well), h * 0.5f);
        if (shown > 0)
        {
            const float usable = w - gap * static_cast<float>(shown - 1);
            float       x      = at.x;
            size_t      drawn  = 0;
            for (const BarPart& part : parts)
            {
                if (part.value <= 0.0)
                    continue;
                const float          segment = usable * static_cast<float>(part.value / total);
                const ImDrawFlags    corners = (drawn == 0 ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_None) | (drawn + 1 == shown ? ImDrawFlags_RoundCornersRight : ImDrawFlags_None);
                list->AddRectFilled(ImVec2(x, at.y), ImVec2(x + segment, at.y + h), u32(part.colour), corners == ImDrawFlags_None ? 0.0f : h * 0.5f,
                                    corners == ImDrawFlags_None ? ImDrawFlags_RoundCornersNone : corners);
                x += segment + gap;
                drawn++;
            }
        }
        ImGui::ItemSize(ImVec2(w, h));
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + w, at.y + h)), 0);
    }
}

