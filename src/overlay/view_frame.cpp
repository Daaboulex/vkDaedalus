#include "overlay/imgui_overlay.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "effects/effect_registry.hpp"
#include "latency_stats.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/motion.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/widgets.hpp"
#include "settings_manager.hpp"
#include "input_blocker.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    namespace
    {
        void centredText(ImDrawList* list, ImFont* font, float size, float centreX, float y, ImU32 colour, const char* text)
        {
            const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            list->AddText(font, size, ImVec2(centreX - extent.x * 0.5f, y), colour, text);
        }

        TileState compactTile(const char* label, const char* title, const LayoutRect& rect, Icon icon, bool selected, bool dim, const char* note, ImFont* font,
                              const OverlayPalette& palette)
        {
            const TileState state = ui::tile(label, rect, selected);
            if (note != nullptr && *note != '\0')
                ImGui::SetItemTooltip("%s", note);
            ImDrawList*  list = ImGui::GetWindowDrawList();
            const float  s    = rect.h / 64.0f;
            const float  size = 22.0f * s;
            const ImVec4 ink  = dim ? palette.disabled : (selected ? palette.accent : palette.secondary);
            drawIcon(list, icon, ImVec2(rect.x + (rect.w - size) * 0.5f, rect.y + 10.0f * s), size, ImGui::GetColorU32(ink));
            const float       textSize = FontSize::caption * s;
            const std::string shown    = title;
            const ImVec2      extent   = font->CalcTextSizeA(textSize, FLT_MAX, rect.w - 6.0f * s, shown.c_str());
            const ImVec4      clip(rect.x, rect.y, rect.right(), rect.bottom());
            list->AddText(font, textSize, ImVec2(rect.x + std::max(3.0f * s, (rect.w - extent.x) * 0.5f), rect.y + 38.0f * s),
                          ImGui::GetColorU32(dim ? palette.disabled : (selected ? palette.bright : palette.text)), shown.c_str(), nullptr, rect.w - 6.0f * s, &clip);
            return state;
        }
    }

    DesktopScale ImGuiOverlay::desktopScale() const
    {
        ScaleInputs inputs;
        if (settingsManager.getOverlaySizePercent() > 0)
            inputs.chosen = static_cast<float>(settingsManager.getOverlaySizePercent()) / 100.0f;
        inputs.waylandOutputs  = desktopOutputs;
        inputs.xftDpi          = desktopXftDpi;
        inputs.swapchainWidth  = currentWidth;
        inputs.swapchainHeight = currentHeight;
        return resolveDesktopScale(inputs);
    }

    void ImGuiOverlay::applyFrameScale(float scale)
    {
        if (scale == appliedScale)
            return;
        scaleOverlayStyle(ImGui::GetStyle(), baseStyle, scale);
        appliedScale = scale;
    }

    void ImGuiOverlay::buildFrame(const KeyboardState& keyboard)
    {
        ImGuiIO& io        = ImGui::GetIO();
        io.MouseDrawCursor = true;
        const FrameLayout layout = frameLayout(io.DisplaySize.x, io.DisplaySize.y, desktopScale().scale);
        applyFrameScale(layout.scale);
        ui::beginFrame(fonts);
        const OverlayPalette& palette = activePalette();

        frameShown = approach(frameShown, 1.0f, io.DeltaTime, Motion::cardSeconds);
        if (openSection != shownSection)
        {
            shownSection = openSection;
            cardShown    = 0.0f;
        }
        cardShown = approach(cardShown, 1.0f, io.DeltaTime, Motion::cardSeconds);

        const float backdrop = static_cast<float>(settingsManager.getOverlayBackdropPercent()) / 100.0f * frameShown;
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, ImGui::GetColorU32(ImVec4(5.0f / 255.0f, 7.0f / 255.0f, 10.0f / 255.0f, backdrop)));

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, frameShown);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("vkDaedalus", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar(2);

        processShaderTest();
        renderFrameTopBar(layout, palette);
        renderWarnings(layout.warnings, palette);
        renderProfileTile(layout.leftTiles[0], palette);
        renderEffectsSwitchTile(layout.leftTiles[1], palette);
        renderSafeAntiCheatTile(layout.leftTiles[2], palette);
        renderSectionTiles(layout, palette);
        renderRightTiles(layout, palette);
        ImGui::End();

        if (openSection)
        {
            const LayoutRect card = layout.card();
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, frameShown * cardShown);
            ImGui::SetNextWindowPos(topLeft(card), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(card.w, card.h), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::px(ui::Size::cardPadX), ui::px(ui::Size::cardPadY)));
            ImGui::Begin("vkDaedalus card", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PopStyleVar();
            ImGui::PushTextWrapPos(0.0f);
            switch (*openSection)
            {
                case OverlaySection::Effects:
                    if (inSelectionMode)
                        renderAddEffectsView();
                    else
                        renderMainView(keyboard);
                    break;
                case OverlaySection::FrameGeneration: renderFrameGenerationView(); break;
                case OverlaySection::Gpus: renderGpusView(); break;
                case OverlaySection::Performance: renderLatencyView(); break;
                case OverlaySection::Shaders: renderShaderManagerView(); break;
                case OverlaySection::Keys: renderKeysCard(keyboard); break;
                case OverlaySection::Settings: renderSettingsView(); break;
                case OverlaySection::Debug: renderDebugView(); break;
            }
            ImGui::PopTextWrapPos();
            ImGui::End();
            ImGui::PopStyleVar();
        }

        if (openSection != OverlaySection::Debug && Logger::isHistoryEnabled())
            Logger::setHistoryEnabled(false);
        ImGui::PopStyleVar();
    }

    void ImGuiOverlay::renderFrameTopBar(const FrameLayout& layout, const OverlayPalette& palette)
    {
        ImDrawList*       list = ImGui::GetWindowDrawList();
        const float       s    = layout.scale;
        const LayoutRect& bar  = layout.topBar;
        list->AddRectFilled(topLeft(bar), bottomRight(bar), ImGui::GetColorU32(palette.bar));

        const float barLeft    = layout.compact ? layout.contentLeft : bar.x + 18.0f * s;
        const float barRight   = layout.compact ? layout.contentLeft + layout.contentWidth : bar.right() - 16.0f * s;
        const float wingHeight = 32.0f * s;
        drawWing(list, ImVec2(barLeft, bar.y + (bar.h - wingHeight) * 0.5f), wingHeight, ImGui::GetColorU32(palette.accent), ImGui::GetColorU32(palette.bar));
        const float brandSize = FontSize::brand * s;
        list->AddText(fonts.heading, brandSize, ImVec2(barLeft + 32.0f * s, bar.y + (bar.h - brandSize) * 0.5f), ImGui::GetColorU32(palette.text), "vkDaedalus");

        const float      side = ui::Size::control * s;
        const float      top  = bar.y + (bar.h - side) * 0.5f;
        const LayoutRect close{barRight - side, top, side, side};
        const TileState  closeState = ui::pressable("Close##frame", close, ui::Kind::Quiet);
        ImGui::SetItemTooltip("Close (%s)", settingsManager.getOverlayKey().c_str());
        const float iconSize = 18.0f * s;
        drawIcon(list, Icon::Close, ImVec2(close.x + (side - iconSize) * 0.5f, close.y + (side - iconSize) * 0.5f), iconSize, ImGui::GetColorU32(palette.text));
        if (closeState.clicked)
            closeRequested = true;

        const std::string previewKey = settingsManager.getPreviewKey();
        const float       textSize   = FontSize::body * s;
        const float       icon       = 16.0f * s;
        const float       pad        = 14.0f * s;
        const float       gap        = ui::Size::gap * s;
        const ImVec2      label      = fonts.body->CalcTextSizeA(textSize, FLT_MAX, 0.0f, "Preview");
        const ImVec2      cap        = keyCapSize(fonts.body, FontSize::caption * s, previewKey.c_str());
        const float       width      = pad + icon + gap + label.x + gap + cap.x + pad;
        const LayoutRect  preview{close.x - ui::Size::gap * s - width, top, width, side};
        const TileState   previewState = ui::pressable("Preview", preview, ui::Kind::Secondary);
        if (!previewState.held)
            ImGui::SetItemTooltip(isInputBlocked() ? "Hold %s or this button: the overlay hides, the game still gets no input" : "Hold %s or this button: the overlay hides", previewKey.c_str());
        drawIcon(list, Icon::Eye, ImVec2(preview.x + pad, preview.y + (side - icon) * 0.5f), icon, ImGui::GetColorU32(palette.text));
        list->AddText(fonts.body, textSize, ImVec2(preview.x + pad + icon + gap, preview.y + (side - textSize) * 0.5f), ImGui::GetColorU32(palette.text), "Preview");
        drawKeyCap(list, fonts.body, FontSize::caption * s, ImVec2(preview.right() - pad - cap.x, preview.y + (side - cap.y) * 0.5f), previewKey.c_str(), palette);
        if (previewState.held)
            previewingByMouse = true;
    }

    ImGuiOverlay::EffectsRunning ImGuiOverlay::effectsRunning()
    {
        EffectsRunning running;
        for (const std::string& name : getSelectedEffects())
        {
            if (pEffectRegistry && !pEffectRegistry->isEffectEnabled(name))
                continue;
            running.on++;
            if (!pEffectRegistry || (!pEffectRegistry->hasEffectFailed(name) && pEffectRegistry->getEffectOnDevice(name, pLogicalDevice).leftOut.empty()))
                running.run++;
        }
        return running;
    }

    void ImGuiOverlay::renderSectionTiles(const FrameLayout& layout, const OverlayPalette& palette)
    {
        ImDrawList* list = ImGui::GetWindowDrawList();
        const float s    = layout.scale;

        const EffectsRunning running    = effectsRunning();
        const size_t         effectsOn  = running.on;
        const size_t         effectsRun = running.run;
        const bool        effectsShort = effectsRun < effectsOn;
        const std::string effectsLine  = !state.effectsEnabled ? "Off"
                                       : effectsShort         ? std::to_string(effectsRun) + " of " + std::to_string(effectsOn) + " on"
                                                              : std::to_string(effectsOn) + " on";
        const ImVec4      effectsInk   = !state.effectsEnabled ? palette.muted : effectsShort ? palette.warn : palette.accent;

        bool                          missing  = false;
        const std::string             fgBlock  = frameGenerationBlocker(missing);
        const FrameGenerationProfile* inUse    = fgBlock.empty() ? frameGenerationProfileInUse() : nullptr;
        std::string                   fgLine   = "Off";
        ImVec4                        fgInk    = palette.muted;
        if (!fgBlock.empty())
        {
            fgLine = missing ? "Not installed" : "Unavailable";
            fgInk  = palette.disabled;
        }
        else if (generatesFrames(inUse))
        {
            fgLine = std::to_string(inUse->multiplier) + "x" + (frameGenerationLiveHere() ? ", on" : ", next launch");
            fgInk  = frameGenerationLiveHere() ? palette.accent : palette.warn;
        }
        const size_t gpusRunning = 1 + static_cast<size_t>(std::count_if(state.gpus.begin(), state.gpus.end(),
                                                                       [](const OverlayGpu& gpu) { return gpu.number != gameGpu && gpu.runsChain; }));
        const size_t      gpuTotal = std::max<size_t>(1, state.gpus.size()) + state.addableGpus.size();
        const std::string gpuLine  = gpusInUseLine(gpusRunning, gpuTotal);

        struct Tile
        {
            OverlaySection section;
            const char*    label;
            std::string    title;
            Icon           icon;
            std::string    line;
            ImVec4         lineInk;
            bool           dim;
        };
        if (!layout.compact)
            list->AddRectFilled(topLeft(layout.sectionTiles[0]), bottomRight(layout.sectionTiles[2]), ImGui::GetColorU32(palette.bar), ui::px(ui::Size::radius));
        const Tile tiles[3] = {{OverlaySection::Effects, "Effects", "Effects", Icon::Sliders, effectsLine, effectsInk, false},
                               {OverlaySection::FrameGeneration, "Frame generation", "Frame generation", Icon::Frames, fgLine, fgInk, !fgBlock.empty()},
                               {OverlaySection::Gpus, "GPUs", gpuTotal == 1 ? "GPU" : "GPUs", Icon::Chip, gpuLine, palette.accent, false}};
        for (size_t i = 0; i < 3; i++)
        {
            const Tile&       tile     = tiles[i];
            const LayoutRect& rect     = layout.sectionTiles[i];
            const bool        selected = openSection == tile.section;
            if (layout.compact)
            {
                const std::string note = tile.dim ? fgBlock : tile.line;
                if (compactTile(tile.label, tile.title.c_str(), rect, tile.icon, selected, tile.dim, note.c_str(), fonts.body, palette).clicked)
                    openSection = tile.section;
                continue;
            }
            const TileState clicked = ui::tile(tile.label, rect, selected);
            if (tile.dim)
                ImGui::SetItemTooltip("%s", fgBlock.c_str());
            const float centre = rect.x + rect.w * 0.5f;
            centredText(list, fonts.heading, FontSize::tile * s, centre, rect.y + 16.0f * s, ImGui::GetColorU32(selected ? palette.bright : palette.text), tile.title.c_str());
            const ImVec4 iconInk = tile.dim ? palette.disabled : (selected ? palette.accent : palette.secondary);
            drawIcon(list, tile.icon, ImVec2(centre - 32.0f * s, rect.y + (rect.h - 64.0f * s) * 0.5f), 64.0f * s, ImGui::GetColorU32(iconInk));
            centredText(list, fonts.body, FontSize::label * s, centre, rect.bottom() - 18.0f * s - FontSize::label * s, ImGui::GetColorU32(tile.lineInk), tile.line.c_str());
            if (clicked.clicked)
                openSection = tile.section;
        }
    }

    void ImGuiOverlay::renderRightTiles(const FrameLayout& layout, const OverlayPalette& palette)
    {
        ImDrawList* list = ImGui::GetWindowDrawList();
        const float s    = layout.scale;

        const auto columnItem = [&](const char* label, const LayoutRect& rect, Icon icon, OverlaySection section, const char* caption) {
            const bool selected = openSection == section;
            if (layout.compact)
            {
                if (compactTile(label, label, rect, icon, selected, false, caption, fonts.body, palette).clicked)
                    openSection = section;
                return;
            }
            const TileState state = ui::tile(label, rect, selected);
            drawIcon(list, icon, ImVec2(rect.x + 14.0f * s, rect.y + (rect.h - 22.0f * s) * 0.5f), 22.0f * s, ImGui::GetColorU32(selected ? palette.accent : palette.secondary));
            const float textX = rect.x + 48.0f * s;
            const ImU32 ink   = ImGui::GetColorU32(selected ? palette.bright : palette.text);
            const float big   = 16.0f * s;
            const float cap   = FontSize::caption * s;
            if (caption != nullptr && *caption != '\0')
            {
                const float top = rect.y + (rect.h - big - cap - 2.0f * s) * 0.5f;
                list->AddText(fonts.bold, big, ImVec2(textX, top), ink, label);
                list->AddText(fonts.body, cap, ImVec2(textX, top + big + 2.0f * s), ImGui::GetColorU32(palette.muted), caption);
            }
            else
                list->AddText(fonts.bold, big, ImVec2(textX, rect.y + (rect.h - big) * 0.5f), ink, label);
            if (state.clicked)
                openSection = section;
        };

        const FrameStats              stats      = frameStats(latency.lastSecondMs);
        const std::optional<uint32_t> multiplier = frameGenerationMultiplierHere();
        char                          fps[32]    = "";
        if (stats.count > 0)
            std::snprintf(fps, sizeof(fps), "%.0f FPS",
                          frameRatesOf(stats.fps, multiplier, multiplier && effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace)).gameFps);
        columnItem("Performance", layout.rightTiles[0], Icon::Chart, OverlaySection::Performance, fps);
        columnItem("Shaders", layout.rightTiles[1], Icon::Layers, OverlaySection::Shaders, nullptr);

        struct IconTile
        {
            const char* label;
            Icon        icon;
            bool        selected;
        };
        const IconTile icons[3] = {{"Debug", Icon::Bug, openSection == OverlaySection::Debug},
                                   {"Keys", Icon::Keyboard, openSection == OverlaySection::Keys},
                                   {"Settings", Icon::Gear, openSection == OverlaySection::Settings}};
        for (size_t i = 0; i < 3; i++)
        {
            const LayoutRect& rect  = layout.iconTiles[i];
            const TileState   state = layout.compact ? compactTile(icons[i].label, icons[i].label, rect, icons[i].icon, icons[i].selected, false, nullptr, fonts.body, palette)
                                                     : ui::tile(icons[i].label, rect, icons[i].selected);
            UiMap::markChecked(ImGui::GetItemID(), icons[i].selected);
            if (!layout.compact)
            {
                ImGui::SetItemTooltip("%s", icons[i].label);
                drawIcon(list, icons[i].icon, ImVec2(rect.x + (rect.w - 24.0f * s) * 0.5f, rect.y + (rect.h - 24.0f * s) * 0.5f), 24.0f * s,
                         ImGui::GetColorU32(icons[i].selected ? palette.accent : palette.secondary));
            }
            if (!state.clicked)
                continue;
            openSection = i == 0 ? OverlaySection::Debug : i == 1 ? OverlaySection::Keys : OverlaySection::Settings;
        }
    }
}
