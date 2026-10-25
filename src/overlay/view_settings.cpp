#include "imgui_overlay.hpp"

#include "overlay/hud.hpp"
#include "overlay/overlay_style.hpp"
#include "overlay/palette_names.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/widgets.hpp"
#include "settings_manager.hpp"
#include "input_blocker.hpp"
#include "logger.hpp"
#include "format.hpp"
#include "reshade_compile_defines.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <utility>

#include "imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderKeysCard(const KeyboardState& keyboard)
    {
        const OverlayPalette& palette = activePalette();
        struct Binding
        {
            const char*                             label;
            const char*                             tooltip;
            std::string                             key;
            std::function<void(const std::string&)> set;
            KeyBinding                              id;
        };
        const std::vector<Binding> bindings = {
            {"Toggle overlay", "Shows or hides this overlay", settingsManager.getOverlayKey(), [](const std::string& key) { settingsManager.setOverlayKey(key); }, KeyBinding::Overlay},
            {"Toggle effects", "Turns every effect on or off", settingsManager.getToggleKey(), [](const std::string& key) { settingsManager.setToggleKey(key); }, KeyBinding::Toggle},
            {"Apply", "Same as Apply on the Effects card", settingsManager.getReloadKey(),
             [](const std::string& key) { settingsManager.setReloadKey(key); }, KeyBinding::Reload},
            {"Toggle frame generation", "Turns lsfg-vk's frame generation off, and back on at its multiplier, while the game runs", settingsManager.getFrameGenerationKey(),
             [](const std::string& key) { settingsManager.setFrameGenerationKey(key); }, KeyBinding::FrameGeneration},
            {"Hold to preview", isInputBlocked() ? "While held, the overlay hides and the game still gets no input" : "While held, the overlay hides", settingsManager.getPreviewKey(),
             [](const std::string& key) { settingsManager.setPreviewKey(key); }, KeyBinding::Preview}};

        ui::heading("Keys");
        if (!keyBindingProblem.empty())
        {
            ImGui::SameLine();
            ui::chip(keyBindingProblem.c_str(), palette.warn);
        }
        const float keyWidth = ui::px(150.0f);
        const auto  row      = [&](const Binding& binding) {
            ui::panelRow();
            ImGui::PushID(binding.label);
            const bool listening = listeningForKey == binding.id;
            ui::label(binding.label);
            ImGui::SetItemTooltip("%s", binding.tooltip);
            ui::alignRight(keyWidth);
            const ImVec4 focus = palette.accent;
            if (ui::button(listening ? "Press a key..." : binding.key.c_str(), ui::Kind::Secondary, keyWidth, listening ? &focus : nullptr))
                listeningForKey = listening ? KeyBinding::None : binding.id;
            ImGui::SetItemTooltip(listening ? "Click again to cancel" : "Click, then press the new key");
            if (listening && !keyboard.lastKeyName.empty())
            {
                std::vector<KeyUse> others;
                for (const Binding& other : bindings)
                    if (other.id != binding.id)
                        others.push_back({other.label, other.key});
                const std::string taken = keyTakenBy(keyboard.lastKeyName, others);
                listeningForKey         = KeyBinding::None;
                if (!taken.empty())
                    keyBindingProblem = keyboard.lastKeyName + " is " + taken;
                else
                {
                    keyBindingProblem.clear();
                    binding.set(keyboard.lastKeyName);
                    saveOverlaySettings();
                }
            }
            ImGui::PopID();
        };
        const auto group = [&](const char* caption, std::initializer_list<size_t> members) {
            ui::sectionLabel(caption);
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
            ui::beginPanel();
            for (const size_t i : members)
                row(bindings[i]);
            ui::endPanel();
        };
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (table)
        {
            ImGui::TableNextColumn();
            group("Overlay", {0, 4});
            ui::nextColumn();
            group("In the game", {1, 3, 2});
            ImGui::EndTable();
        }

        ui::footer();
        ImGui::AlignTextToFramePadding();
        ui::note("Saved for every game");
        ui::alignRight(ui::buttonWidth("Reset all"));
        if (ui::button("Reset all"))
        {
            const VkBasaltSettings defaults;
            settingsManager.setOverlayKey(defaults.overlayKey);
            settingsManager.setToggleKey(defaults.toggleKey);
            settingsManager.setReloadKey(defaults.reloadKey);
            settingsManager.setPreviewKey(defaults.previewKey);
            settingsManager.setFrameGenerationKey(defaults.frameGenerationKey);
            listeningForKey = KeyBinding::None;
            keyBindingProblem.clear();
            saveOverlaySettings();
        }
        ImGui::SetItemTooltip("Back to %s, %s, %s, %s and %s", VkBasaltSettings{}.overlayKey.c_str(), VkBasaltSettings{}.toggleKey.c_str(), VkBasaltSettings{}.reloadKey.c_str(),
                              VkBasaltSettings{}.frameGenerationKey.c_str(), VkBasaltSettings{}.previewKey.c_str());
    }

    void ImGuiOverlay::renderSettingsView()
    {
        const OverlayPalette& palette = activePalette();

        ui::heading("Settings");
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##settings", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (!table)
            return;

        ImGui::TableNextColumn();
        ui::sectionLabel("Interface");
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        ui::beginPanel();
        const float        column  = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
        const DesktopScale desktop = desktopScale();
        ui::panelRow();
        ui::label("Size");
        ui::atColumn(column);
        const int   chosen = settingsManager.getOverlaySizePercent();
        ScaleInputs desktopOnly;
        desktopOnly.waylandOutputs     = desktopOutputs;
        desktopOnly.xftDpi             = desktopXftDpi;
        desktopOnly.swapchainWidth     = currentWidth;
        desktopOnly.swapchainHeight    = currentHeight;
        const DesktopScale fromDesktop = resolveDesktopScale(desktopOnly);
        char               desktopLabel[48];
        std::snprintf(desktopLabel, sizeof(desktopLabel), "Desktop (%.0f%%)", fromDesktop.scale * 100.0f);
        const std::string current = chosen > 0 ? std::to_string(chosen) + "%" : desktopLabel;
        if (ui::beginCombo("##overlaySize", current.c_str(), ui::px(150.0f)))
        {
            if (ui::option(desktopLabel, chosen == 0))
            {
                settingsManager.setOverlaySizePercent(0);
                saveOverlaySettings();
            }
            for (int size : {75, 100, 125, 150, 175, 200})
                if (ui::option((std::to_string(size) + "%").c_str(), chosen == size))
                {
                    settingsManager.setOverlaySizePercent(size);
                    saveOverlaySettings();
                }
            ui::endCombo();
        }
        ImGui::SameLine();
        const char* source = fromDesktop.source == ScaleSource::WaylandOutput ? "Wayland"
                           : fromDesktop.source == ScaleSource::XftDpi        ? "X11"
                                                                              : "not found";
        ui::chip(source, palette.secondary,
                 fromDesktop.source == ScaleSource::WaylandOutput ? "From the display the game is on"
                 : fromDesktop.source == ScaleSource::XftDpi      ? "From X11's DPI setting; a game KWin already scales is not scaled again"
                                                                  : "No scale found: 100% is used");
        if (defaultMark("Size", chosen != VkBasaltSettings{}.overlaySizePercent, "Desktop", palette))
        {
            settingsManager.setOverlaySizePercent(VkBasaltSettings{}.overlaySizePercent);
            saveOverlaySettings();
        }
        if (desktop.scale != fromDesktop.scale && chosen == 0)
            ui::notef("Shown at %.0f%%, the most that fits", desktop.scale * 100.0f);

        int               backdrop      = settingsManager.getOverlayBackdropPercent();
        const std::string backdropShown = std::to_string(backdrop) + "%";
        const float       markRoom      = restartMarkSize * ImGui::GetStyle().FontScaleMain + ImGui::GetStyle().ItemSpacing.x;
        ui::panelRow();
        ui::sliderRowBegin("Backdrop", backdropShown.c_str(), markRoom);
        if (ui::sliderInt("##overlayBackdrop", &backdrop, OVERLAY_BACKDROP_PERCENT_RANGE.lowest, OVERLAY_BACKDROP_PERCENT_RANGE.highest, ImGuiSliderFlags_AlwaysClamp))
            settingsManager.setOverlayBackdropPercent(backdrop);
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveOverlaySettings();
        ImGui::SetItemTooltip("How much the game behind the overlay is darkened");
        ui::sliderRowEnd(backdropShown.c_str(), markRoom);
        if (defaultMark("Backdrop", backdrop != VkBasaltSettings{}.overlayBackdropPercent, std::to_string(VkBasaltSettings{}.overlayBackdropPercent) + "%", palette))
        {
            settingsManager.setOverlayBackdropPercent(VkBasaltSettings{}.overlayBackdropPercent);
            saveOverlaySettings();
        }

        static constexpr std::pair<LaunchNotice, const char*> noticeChoices[] = {
            {LaunchNotice::EveryLaunch, "Every launch"}, {LaunchNotice::FirstLaunch, "First launch of a game"}, {LaunchNotice::Never, "Never"}};
        const LaunchNotice notice      = settingsManager.getLaunchNotice();
        const char*        noticeShown = noticeChoices[0].second;
        for (const auto& [choice, words] : noticeChoices)
            if (choice == notice)
                noticeShown = words;
        ui::panelRow();
        ui::label("Launch notice");
        ImGui::SetItemTooltip("The card at the top that says vkDaedalus loaded, its profile and effects, for 5 seconds after a game starts");
        ui::atColumn(column);
        const bool noticeFromEnvironment = launchNoticeFromEnvironment().has_value();
        ImGui::BeginDisabled(noticeFromEnvironment);
        if (ui::beginCombo("##launchNotice", noticeShown, ui::px(190.0f)))
        {
            for (const auto& [choice, words] : noticeChoices)
                if (ui::option(words, choice == notice))
                {
                    settingsManager.setLaunchNotice(choice);
                    saveOverlaySettings();
                }
            ui::endCombo();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(noticeFromEnvironment ? "VKDAEDALUS_LAUNCH_NOTICE sets the notice for this run, over this setting."
                                                    : "Saved for every game. VKDAEDALUS_LAUNCH_NOTICE overrides it for one run.");
        if (defaultMark("Launch notice", notice != VkBasaltSettings{}.launchNotice, noticeChoices[0].second, palette))
        {
            settingsManager.setLaunchNotice(VkBasaltSettings{}.launchNotice);
            saveOverlaySettings();
        }

        std::string_view paletteShown = paletteNames[0].shown;
        for (const PaletteName& name : paletteNames)
            if (name.word == settingsManager.getPalette())
                paletteShown = name.shown;
        ui::panelRow();
        ui::label("Colours");
        ImGui::SetItemTooltip("The overlay's colour scheme; warnings, errors and success keep their own colours in every one");
        ui::atColumn(column);
        if (ui::beginCombo("##palette", std::string(paletteShown).c_str(), ui::px(190.0f)))
        {
            for (const PaletteName& name : paletteNames)
                if (ui::option(std::string(name.shown).c_str(), name.word == settingsManager.getPalette()))
                {
                    settingsManager.setPalette(std::string(name.word));
                    useOverlayPalette(name.word);
                    applyOverlayStyle(baseStyle, activePalette());
                    saveOverlaySettings();
                }
            ui::endCombo();
        }
        if (defaultMark("Colours", settingsManager.getPalette() != VkBasaltSettings{}.palette, std::string(paletteNames[0].shown), palette))
        {
            settingsManager.setPalette(VkBasaltSettings{}.palette);
            useOverlayPalette(VkBasaltSettings{}.palette);
            applyOverlayStyle(baseStyle, activePalette());
            saveOverlaySettings();
        }

        const bool                 hud   = settingsManager.getHud();
        const std::vector<HudItem> items = hudItemsFrom(settingsManager.getHudItems()).value_or(hudPresets().front().items);
        std::string                hudShown = "Off";
        if (hud)
        {
            hudShown = "Custom";
            for (const HudPreset& preset : hudPresets())
                if (preset.items == items)
                    hudShown = std::string(preset.shown);
        }
        ui::panelRow();
        ui::label("HUD");
        ImGui::SetItemTooltip("Figures at the top left while the overlay is closed, in Steam's levels; Off draws nothing");
        ui::atColumn(column);
        if (ui::beginCombo("##hud", hudShown.c_str(), ui::px(190.0f)))
        {
            if (ui::option("Off", !hud))
            {
                settingsManager.setHud(false);
                saveOverlaySettings();
            }
            for (const HudPreset& preset : hudPresets())
                if (ui::option(std::string(preset.shown).c_str(), hud && preset.items == items))
                {
                    settingsManager.setHud(true);
                    settingsManager.setHudItems(hudItemsWord(preset.items));
                    saveOverlaySettings();
                }
            ui::menuSeparator();
            for (const HudItemName& name : hudItemNames)
            {
                bool on = hud && std::find(items.begin(), items.end(), name.item) != items.end();
                if (ui::checkbox((std::string(name.shown) + "##hudItem").c_str(), &on))
                {
                    std::vector<HudItem> next = hud ? items : std::vector<HudItem>{};
                    if (on)
                        next.push_back(name.item);
                    else
                        next.erase(std::remove(next.begin(), next.end(), name.item), next.end());
                    if (!next.empty())
                    {
                        settingsManager.setHud(true);
                        settingsManager.setHudItems(hudItemsWord(next));
                        saveOverlaySettings();
                    }
                }
                if (name.item == HudItem::Fps)
                    ImGui::SetItemTooltip("The game's frames over the last second, and the shown frames while lsfg-vk generates");
                if (name.item == HudItem::EffectsTime)
                    ImGui::SetItemTooltip("Needs Per-effect GPU timing on the Performance page");
            }
            ui::endCombo();
        }
        if (defaultMark("HUD", hud != VkBasaltSettings{}.hud, "Off", palette))
        {
            settingsManager.setHud(VkBasaltSettings{}.hud);
            settingsManager.setHudItems(VkBasaltSettings{}.hudItems);
            saveOverlaySettings();
        }

        ui::endPanel();

        ui::nextColumn();
        ui::sectionLabel("Effects");
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        ui::beginPanel();
        const float effectsColumn = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
        ui::panelRow();
        ui::label("Effect limit");
        ImGui::SetItemTooltip("The most effects the chain can hold; applies at the next rebuild.\nAn effect past it is left out and marked, and adding one past it is refused.");
        ui::atColumn(effectsColumn);
        int maxEffectsVal = settingsManager.getMaxEffects();
        if (ui::intField("##maxEffects", &maxEffectsVal, MAX_EFFECTS_RANGE.lowest, MAX_EFFECTS_RANGE.highest))
        {
            settingsManager.setMaxEffects(maxEffectsVal);
            saveOverlaySettings();
        }
        const double bytesPerPixel = reshadeColorBitDepth(convertToUNORM(currentFormat)) == 16 ? 8.0 : 4.0;
        const double bytesPerSlot  = static_cast<double>(currentImageCount) * currentWidth * currentHeight * bytesPerPixel;
        const int    fullChainMB   = static_cast<int>(maxEffectsVal * bytesPerSlot / (1024.0 * 1024.0));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(nullptr, FontSize::small);
        ImGui::TextColored(palette.muted, "~%d MB at %ux%u", fullChainMB, currentWidth, currentHeight);
        ImGui::PopFont();
        ImGui::SetItemTooltip("With the chain full, each effect holds one output image per swapchain image.\nReShade effects' own textures come on top.");
        if (defaultMark("Effect limit", maxEffectsVal != VkBasaltSettings{}.maxEffects, std::to_string(VkBasaltSettings{}.maxEffects), palette))
        {
            settingsManager.setMaxEffects(VkBasaltSettings{}.maxEffects);
            saveOverlaySettings();
        }

        ui::panelRow();
        bool autoApply = settingsManager.getAutoApply();
        if (ui::checkbox("Apply automatically", &autoApply))
        {
            settingsManager.setAutoApply(autoApply);
            saveOverlaySettings();
        }
        ImGui::SetItemTooltip("An effect change rebuilds shortly after it, open or closed. Off: changes wait for Apply.");
        if (defaultMark("Apply automatically", autoApply != VkBasaltSettings{}.autoApply, VkBasaltSettings{}.autoApply ? "on" : "off", palette))
        {
            settingsManager.setAutoApply(VkBasaltSettings{}.autoApply);
            saveOverlaySettings();
        }

        ui::panelRow();
        ImGui::BeginDisabled(!autoApply);
        ui::label("Rebuild after");
        ImGui::SetItemTooltip("How long after the last change the rebuild starts");
        ui::atColumn(effectsColumn);
        ImGui::SetNextItemWidth(ui::px(180.0f));
        int delay = settingsManager.getAutoApplyDelayMs();
        if (ui::sliderInt("##autoApplyDelay", &delay, AUTO_APPLY_DELAY_MS_RANGE.lowest, AUTO_APPLY_DELAY_MS_RANGE.highest, ImGuiSliderFlags_AlwaysClamp))
            settingsManager.setAutoApplyDelayMs(delay);
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveOverlaySettings();
        ImGui::SetItemTooltip("How long after the last change the rebuild starts");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(palette.secondary, "%d ms", delay);
        ImGui::EndDisabled();
        if (defaultMark("Rebuild after", delay != VkBasaltSettings{}.autoApplyDelayMs, std::to_string(VkBasaltSettings{}.autoApplyDelayMs) + " ms", palette))
        {
            settingsManager.setAutoApplyDelayMs(VkBasaltSettings{}.autoApplyDelayMs);
            saveOverlaySettings();
        }

        ui::panelRow();
        bool liveReshadeUniforms = settingsManager.getLiveReshadeUniforms();
        if (ui::checkbox("Instant ReShade sliders", &liveReshadeUniforms))
        {
            settingsManager.setLiveReshadeUniforms(liveReshadeUniforms);
            saveOverlaySettings();
            applyRequested = true;
        }
        ImGui::SetItemTooltip("ReShade values change without a rebuild; built-in effects still rebuild.\nChanging this reloads the active effects.");
        if (defaultMark("Instant ReShade sliders", liveReshadeUniforms != VkBasaltSettings{}.liveReshadeUniforms, VkBasaltSettings{}.liveReshadeUniforms ? "on" : "off", palette))
        {
            settingsManager.setLiveReshadeUniforms(VkBasaltSettings{}.liveReshadeUniforms);
            saveOverlaySettings();
            applyRequested = true;
        }

        ui::panelRow();
        bool enableOnLaunch = settingsManager.getEnableOnLaunch();
        if (ui::checkbox("Effects on at launch", &enableOnLaunch))
        {
            settingsManager.setEnableOnLaunch(enableOnLaunch);
            saveOverlaySettings();
        }
        ImGui::SetItemTooltip("Off: effects start off and are turned on with the effects switch");
        if (defaultMark("Effects on at launch", enableOnLaunch != VkBasaltSettings{}.enableOnLaunch, VkBasaltSettings{}.enableOnLaunch ? "on" : "off", palette))
        {
            settingsManager.setEnableOnLaunch(VkBasaltSettings{}.enableOnLaunch);
            saveOverlaySettings();
        }

        ui::panelRow();
        bool       depthCapture  = settingsManager.getDepthCapture();
        const bool safeAntiCheat = settingsManager.getSafeAntiCheat();
        const bool running       = depthCaptureRunning.load(std::memory_order_relaxed);
        ImGui::BeginDisabled(safeAntiCheat);
        if (ui::checkbox("Depth capture", &depthCapture))
        {
            settingsManager.setDepthCapture(depthCapture);
            saveOverlaySettings();
        }
        ImGui::EndDisabled();
        if (safeAntiCheat)
            ImGui::SetItemTooltip("%s", depthCaptureStatus(depthCapture, safeAntiCheat, running).c_str());
        else
            ImGui::SetItemTooltip("For effects that read depth; most do not need it. Read when the game starts.\n%s", depthCaptureStatus(depthCapture, safeAntiCheat, running).c_str());
        if (safeAntiCheat)
        {
            ImGui::SameLine();
            ui::chip("off while Safe Anti-Cheat is on for this game", palette.muted);
        }
        else
            restartMark("Depth capture", depthCapture != running, palette);
        if (defaultMark("Depth capture", depthCapture != VkBasaltSettings{}.depthCapture, VkBasaltSettings{}.depthCapture ? "on" : "off", palette))
        {
            settingsManager.setDepthCapture(VkBasaltSettings{}.depthCapture);
            saveOverlaySettings();
        }
        ui::endPanel();
        ImGui::EndTable();
        ui::footer();
        ui::note("Saved for every game");
    }

}
