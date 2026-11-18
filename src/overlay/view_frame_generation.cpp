#include "imgui_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "config_serializer.hpp"
#include "frame_generation.hpp"
#include "effects/effect_registry.hpp"
#include "frame_generation_place.hpp"
#include "logical_device.hpp"
#include "logger.hpp"
#include "latency_stats.hpp"
#include "overlay/desktop_scale.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/frame_generation_rows.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/widgets.hpp"

#include "imgui.h"
#include "imfilebrowser.h"
#include "imgui_internal.h"

namespace vkBasalt
{
    namespace
    {
        template<size_t N>
        bool chooseFrom(const char* label, const std::array<FrameGenerationChoice, N>& choices, std::optional<std::string>& value, const char* unset)
        {
            const auto shown = [&](const std::string& name) {
                const auto at = std::find_if(choices.begin(), choices.end(), [&](const FrameGenerationChoice& choice) { return choice.name == name; });
                return at == choices.end() ? name : std::string(at->shown);
            };
            const std::string preview = value ? shown(*value) : unset;
            bool              chosen  = false;
            if (ui::beginCombo(label, preview.c_str()))
            {
                for (const FrameGenerationChoice& choice : choices)
                {
                    const bool selected = value && *value == choice.name;
                    if (ui::option(std::string(choice.shown).c_str(), selected) && !selected)
                    {
                        value  = std::string(choice.name);
                        chosen = true;
                    }
                }
                ui::endCombo();
            }
            return chosen;
        }
    }

    bool ImGuiOverlay::readFrameGenerationOnce()
    {
        if (frameGenerationRead)
            return frameGenerationFile.has_value();
        frameGenerationRead = true;
        const LsfgRead read = readLsfgFile(lsfgConfigPath());
        frameGenerationFile        = read.file;
        frameGenerationFileProblem = read.problem;
        frameGenerationFileTime    = writeTimeOf(lsfgConfigPath());
        if (read.file)
        {
            frameGenerationGlobalAtStart = read.file->global;
            if (const std::optional<size_t> here = lsfgProfileFor(*read.file, thisLsfgProcess()))
                frameGenerationProfileHere = read.file->profiles[*here].name;
        }
        if (const FrameGenerationProfile* atStart = frameGenerationProfileInUse())
            frameGenerationAtStart = *atStart;
        return frameGenerationFile.has_value();
    }

    void ImGuiOverlay::refreshFrameGenerationFile(bool now)
    {
        if (!frameGenerationRead || (!frameGenerationPoll.due(std::chrono::steady_clock::now()) && !now))
            return;
        const std::string                                    path    = lsfgConfigPath();
        const std::optional<std::filesystem::file_time_type> written = writeTimeOf(path);
        if (!now && written == frameGenerationFileTime)
            return;
        const LsfgRead read        = readLsfgFile(path);
        frameGenerationFile        = read.file;
        frameGenerationFileProblem = read.problem;
        frameGenerationFileTime    = written;
        frameGenerationDraft.reset();
        frameGenerationDll.reset();
        Logger::debug("read lsfg-vk's file " + path + " again" + (now ? "" : ", since it changed"));
    }

    void ImGuiOverlay::frameGenerationAssignmentsChanged(const std::string& renamedFrom)
    {
        const std::string path = lsfgConfigPath();
        std::string       problem;
        if (!renamedFrom.empty() && !renameLsfgProfile(path, frameGenerationProfileNameOf(renamedFrom), frameGenerationProfileNameOf(activeProfileName), problem))
            Logger::warn("lsfg-vk's profile " + frameGenerationProfileNameOf(renamedFrom) + " keeps its name, so frame generation stays with the old name: " + problem);
        if (!syncLsfgActiveIn(path, ConfigSerializer::profileUsers(), problem))
            Logger::warn("lsfg-vk's file " + path + " keeps its game lists, so frame generation may follow another profile: " + problem);
        refreshFrameGenerationFile(true);
        const std::string makoPath = makoConfigPath();
        if (!renamedFrom.empty() && !renameMakoProfile(makoPath, frameGenerationProfileNameOf(renamedFrom), frameGenerationProfileNameOf(activeProfileName), problem))
            Logger::warn("MAKO's profile " + frameGenerationProfileNameOf(renamedFrom) + " keeps its name, so frame generation stays with the old name: " + problem);
        if (!syncMakoActiveIn(makoPath, ConfigSerializer::profileUsers(), problem))
            Logger::warn("MAKO's file " + makoPath + " keeps its game lists, so frame generation may follow another profile: " + problem);
        refreshMakoFile(true);
    }

    std::vector<std::string> ImGuiOverlay::frameGenerationGlobalsWaiting() const
    {
        std::vector<std::string> waiting;
        if (!frameGenerationFile || !frameGenerationGlobalAtStart)
            return waiting;
        if (frameGenerationFile->global.allowHalfPrecision != frameGenerationGlobalAtStart->allowHalfPrecision)
            waiting.push_back("Half precision");
        if (frameGenerationFile->global.dll != frameGenerationGlobalAtStart->dll)
            waiting.push_back("lsfg-vk.dll");
        return waiting;
    }

    const FrameGenerationProfile* ImGuiOverlay::frameGenerationProfileInUse() const
    {
        if (!frameGenerationFile)
            return nullptr;
        if (frameGenerationProfileHere && !madeByVkDaedalus(*frameGenerationProfileHere))
            if (const FrameGenerationProfile* here = profileNamed(*frameGenerationFile, *frameGenerationProfileHere))
                return here;
        return profileNamed(*frameGenerationFile, frameGenerationProfileNameOf(activeProfileName));
    }

    bool ImGuiOverlay::frameGenerationLiveHere() const
    {
        const FrameGenerationProfile* inUse = frameGenerationProfileInUse();
        return frameGenerationLayerLoaded() && inUse && frameGenerationProfileHere && inUse->name == *frameGenerationProfileHere;
    }

    void ImGuiOverlay::saveFrameGeneration(const std::optional<FrameGenerationGlobal>& global, const std::optional<FrameGenerationProfile>& profile)
    {
        const std::string path = lsfgConfigPath();
        std::string       problem;
        LsfgFile          written;
        if (saveLsfgFile(path, global, profile, written, problem))
        {
            frameGenerationFile     = written;
            frameGenerationFileTime = writeTimeOf(path);
            frameGenerationProblem.clear();
            if (global)
                frameGenerationDll.reset();
            Logger::info("lsfg-vk's file " + path + " changed" + (profile ? " for its profile '" + profile->name + "'" : ""));
        }
        else
        {
            frameGenerationProblem = problem;
            Logger::err("lsfg-vk's file " + path + " was not changed: " + problem);
        }
    }

    bool ImGuiOverlay::effectsAfterFrameGenerationSaved()
    {
        if (activeProfilePath.empty())
            return false;
        if (frameOrderProfilePath != activeProfilePath)
        {
            const ProfileSettings saved = ConfigSerializer::loadProfileSettings(activeProfilePath);
            frameOrderProfilePath       = activeProfilePath;
            effectsAfterFrameGeneration = saved.effectsAfterFrameGeneration.value_or(false);
            frameGenerationLayerChoice  = saved.frameGenerationLayer;
        }
        return effectsAfterFrameGeneration;
    }

    bool ImGuiOverlay::frameGenerationLayerInstalled(FrameGenerationLayer layer) const
    {
        if (layer == FrameGenerationLayer::Mako)
            return makoLayerLoaded() || makoLayerVisible() == true;
        return frameGenerationLayerLoaded() || frameGenerationLayerVisible() == true;
    }

    FrameGenerationLayer ImGuiOverlay::profileFrameGenerationLayer()
    {
        effectsAfterFrameGenerationSaved();
        if (frameOrderProfilePath == activeProfilePath && frameGenerationLayerChoice)
            return *frameGenerationLayerChoice;
        return defaultFrameGenerationLayer(frameGenerationLayerInstalled(FrameGenerationLayer::LsfgVk), frameGenerationLayerInstalled(FrameGenerationLayer::Mako),
                                           makoEnabledByCaller());
    }

    std::string ImGuiOverlay::frameGenerationBlocker(FrameGenerationLayer layer, bool& notInstalled)
    {
        return layer == FrameGenerationLayer::Mako ? makoBlocker(notInstalled) : frameGenerationBlocker(notInstalled);
    }

    ImGuiOverlay::FrameGenerationState ImGuiOverlay::frameGenerationState()
    {
        FrameGenerationState fg;
        fg.layer   = profileFrameGenerationLayer();
        fg.loadsNextLaunch = loadedAtNextLaunch(fg.layer, startedWithVkdaedalusRun(), makoEnabledInEnvironment());
        fg.blocker = frameGenerationBlocker(fg.layer, fg.notInstalled);
        if (fg.notInstalled && !frameGenerationLayerInstalled(FrameGenerationLayer::LsfgVk) && !frameGenerationLayerInstalled(FrameGenerationLayer::Mako))
            fg.blocker = "Neither lsfg-vk nor MAKO is installed where this game's Vulkan loader looks for layers.";
        if (!fg.blocker.empty())
            return fg;
        if (fg.layer == FrameGenerationLayer::Mako)
        {
            const MakoProfile* inUse = makoProfileInUse();
            if (!inUse)
                return fg;
            const MakoSettings& set = makoSettingsFor(*inUse, powerSupplies.source);
            fg.on                   = set.frameGenerationEnabled;
            fg.live                 = makoLiveHere() && makoAtStart && makoSettingsFor(*makoAtStart, powerSupplies.source).frameGenerationProvisioned;
            fg.unavailable          = makoModelMissing(set.frameGenerationModel);
            return fg;
        }
        fg.on   = generatesFrames(frameGenerationProfileInUse());
        fg.live = frameGenerationLiveHere();
        if (!frameGenerationDll)
            frameGenerationDll = lsfgDllStatus(frameGenerationFile->global);
        if (!frameGenerationDll->found && !frameGenerationDll->inSteamLibrary)
            fg.unavailable = "lsfg-vk.dll is in no Steam library";
        return fg;
    }

    std::string ImGuiOverlay::frameGenerationBlocker(bool& notInstalled)
    {
        notInstalled                      = false;
        const bool                loaded  = frameGenerationLayerLoaded();
        const std::optional<bool> visible = loaded ? std::optional<bool>(true) : frameGenerationLayerVisible();
        if (activeGameName.empty())
            return "This program has no name vkDaedalus can give lsfg-vk.";
        if (visible == false)
        {
            notInstalled = true;
            return "lsfg-vk is not installed where this game's Vulkan loader looks for layers.";
        }
        if (lsfgSettingsFromEnvironment())
            return "LSFGVK_ENV is set, so lsfg-vk reads its settings from the game's launch options, not its file.";
        if (!loaded && frameGenerationLayerDisabledBy(FrameGenerationLayer::LsfgVk))
            return "DISABLE_LSFGVK in the game's launch options keeps lsfg-vk out of it.";
        if (!readFrameGenerationOnce())
            return "lsfg-vk refuses its file " + lsfgConfigPath() + ": " + frameGenerationFileProblem + ".";
        if (ConfigSerializer::isWineLoaderName(activeGameName))
            return "This Wine process runs no .exe, so a profile for it would turn frame generation on in every Wine game.";
        return "";
    }

    void ImGuiOverlay::switchFrameGeneration(bool on)
    {
        if (!readFrameGenerationOnce())
            return;
        const FrameGenerationProfile* inUse = frameGenerationProfileInUse();
        if (inUse && inUse->multiplier >= lowestOnMultiplier)
            frameGenerationOnMultiplier = inUse->multiplier;
        const uint32_t onMultiplier = frameGenerationOnMultiplier >= lowestOnMultiplier ? frameGenerationOnMultiplier : lowestOnMultiplier;
        frameGenerationDll          = lsfgDllStatus(frameGenerationFile->global);
        std::optional<FrameGenerationGlobal> global;
        if (on && !frameGenerationDll->found && frameGenerationDll->inSteamLibrary)
        {
            global      = frameGenerationFile->global;
            global->dll = *frameGenerationDll->inSteamLibrary;
        }
        if (on && !frameGenerationFile->global.logFile && !Logger::stateFolder().empty())
        {
            if (!global)
                global = frameGenerationFile->global;
            global->logFile = Logger::stateFolder() + "/lsfg-vk.log";
        }
        if (on && !frameGenerationDll->found && !frameGenerationDll->inSteamLibrary)
        {
            frameGenerationProblem = "lsfg-vk.dll was not found, and with a profile but no DLL lsfg-vk stops the game's swapchain from being made";
            Logger::warn("frame generation for " + activeGameName + " not switched on: " + frameGenerationProblem);
        }
        else if (inUse)
        {
            FrameGenerationProfile changed = *inUse;
            changed.multiplier             = on ? onMultiplier : offMultiplier;
            if (on && madeByVkDaedalus(changed.name) && !changed.colorSpace && !changed.transferFunction)
            {
                const LsfgColour colour  = lsfgColourOf(swapchainColorSpace);
                changed.colorSpace       = colour.colorSpace;
                changed.transferFunction = colour.transferFunction;
            }
            saveFrameGeneration(global, changed);
        }
        else if (on)
        {
            std::vector<std::string> games = ConfigSerializer::profileUsers()[activeProfileName];
            if (std::find(games.begin(), games.end(), activeGameName) == games.end())
                games.push_back(activeGameName);
            saveFrameGeneration(global, newLsfgProfileFor(activeProfileName, games, onMultiplier, lsfgColourOf(swapchainColorSpace)));
            if (frameGenerationProblem.empty())
                frameGenerationAssignmentsChanged("");
        }
    }

    void ImGuiOverlay::toggleFrameGeneration()
    {
        refreshFrameGenerationFile(false);
        refreshMakoFile(false);
        const FrameGenerationLayer layer        = profileFrameGenerationLayer();
        bool                       notInstalled = false;
        const std::string          blocker      = frameGenerationBlocker(layer, notInstalled);
        shortNoticeStart               = std::chrono::steady_clock::now();
        shortNoticeProblem             = !blocker.empty();
        if (!blocker.empty())
        {
            shortNoticeLine = "unavailable: " + blocker;
            Logger::info("frame generation key: " + shortNoticeLine);
            return;
        }
        if (layer == FrameGenerationLayer::Mako)
        {
            const FrameGenerationState before = frameGenerationState();
            switchMako(&MakoSettings::frameGenerationEnabled, !before.on);
            const FrameGenerationState after = frameGenerationState();
            shortNoticeProblem               = !makoProblem.empty();
            const MakoProfile* inUse         = makoProfileInUse();
            const MakoSettings* set          = inUse ? &makoSettingsFor(*inUse, powerSupplies.source) : nullptr;
            if (shortNoticeProblem)
                shortNoticeLine = "not changed: " + makoProblem;
            else if (after.on && set)
                shortNoticeLine = "MAKO " + (set->adaptive ? std::string("adaptive") : std::to_string(set->multiplier) + "x") + ", on"
                                  + (after.live ? "" : after.loadsNextLaunch ? ", from the next launch" : ", needs vkdaedalus-run");
            else
                shortNoticeLine = "off";
            Logger::info("frame generation key: " + shortNoticeLine);
            return;
        }
        switchFrameGeneration(!generatesFrames(frameGenerationProfileInUse()));
        const FrameGenerationProfile* now = frameGenerationProfileInUse();
        shortNoticeProblem                = !frameGenerationProblem.empty();
        if (shortNoticeProblem)
            shortNoticeLine = "not changed: " + frameGenerationProblem;
        else if (generatesFrames(now))
            shortNoticeLine = std::to_string(now->multiplier) + "x, on" + (frameGenerationLiveHere() ? "" : ", from the next launch");
        else
            shortNoticeLine = "off";
        Logger::info("frame generation key: " + shortNoticeLine);
    }

    void ImGuiOverlay::renderPipelineStrip(FrameGenerationLayer layer)
    {
        const OverlayPalette& palette = activePalette();
        if (frameGenerationGpuName.empty())
        {
            VkPhysicalDeviceProperties properties{};
            pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &properties);
            frameGenerationGpuName = properties.deviceName;
        }
        const std::vector<std::string> active = pEffectRegistry ? pEffectRegistry->getActiveEffects() : std::vector<std::string>{};
        const GpuPlan                  plan   = pEffectRegistry ? pEffectRegistry->gpuPlan() : GpuPlan{};
        std::vector<uint32_t>          effectGpus = gpusOfRows(plan.rows, plan.ownGpus, active);
        for (size_t i = 0; i < active.size(); i++)
            if (!pEffectRegistry->getEffectOnDevice(active[i], pLogicalDevice).gpuRefusal.empty())
                effectGpus[i] = gameGpu;
        const std::optional<uint32_t> multiplier = frameGenerationMultiplierHere();
        const std::string             running(loadedFrameGenerationLayerName());
        const bool                    makoRuns   = loadedFrameGenerationLayer() == FrameGenerationLayer::Mako;
        const MakoScalingShown        scaling    = makoRuns ? makoScaling() : MakoScalingShown{};
        const bool                    scales     = scaling.run == MakoScalingRun::Scaling;
        const bool                    adaptive   = frameGenerationAdaptiveHere().has_value();
        std::vector<PipelineStep>     steps      = pipelineSteps(pLogicalDevice->frameGenerationPlace, multiplier.has_value() || adaptive,
                                                                 state.effectsEnabled ? effectGpus : std::vector<uint32_t>{});
        if (scales || scaling.run == MakoScalingRun::NoStatus)
            steps.insert(steps.end() - 1, PipelineStep{"Scaling", {gameGpu}});
        const std::optional<std::string> elsewhere = layer == FrameGenerationLayer::Mako ? makoGpuElsewhere() : std::nullopt;
        ScaleInputs outputs;
        outputs.waylandOutputs              = desktopOutputs;
        outputs.swapchainWidth              = currentWidth;
        outputs.swapchainHeight             = currentHeight;
        const std::optional<double> refresh = gameOutputRefreshHz(outputs);
        const float                 scale   = ImGui::GetStyle().FontScaleMain;
        const float                 h       = ui::px(ui::Size::row);
        const float                 gap     = ui::px(ui::Size::gap);
        const float                 padX    = ui::px(10.0f);
        const float                 arrow   = ui::px(16.0f);
        const float                 title   = FontSize::small * scale;
        const float                 under   = FontSize::caption * scale;
        ImDrawList*                 draw    = ImGui::GetWindowDrawList();
        const ImVec2                lead    = fonts.body->CalcTextSizeA(title, FLT_MAX, 0.0f, "In this run");
        const ImVec2                start   = ImGui::GetCursorScreenPos();
        draw->AddText(fonts.body, title, ImVec2(start.x, start.y + (h - lead.y) * 0.5f), ImGui::GetColorU32(palette.muted), "In this run");
        ImGui::ItemSize(ImVec2(lead.x, h));
        for (size_t i = 0; i < steps.size(); i++)
        {
            const PipelineStep& step       = steps[i];
            const bool          generation = step.what == "Frame generation";
            const bool          scaler     = step.what == "Scaling";
            const std::string   name       = generation       ? running + (multiplier ? " " + std::to_string(*multiplier) + "x" : " adaptive")
                                             : scaler && scales ? shownChoice(makoScalingMethods, scaling.method)
                                                                : step.what;
            std::string         where      = "GPU ";
            for (size_t g = 0; g < step.gpus.size(); g++)
                where += (g ? " and " : "") + std::to_string(step.gpus[g]);
            if (scaler)
                where = scales ? scaling.detail : "no status file";
            if (step.what == "Display" && refresh)
            {
                char hz[32];
                std::snprintf(hz, sizeof(hz), "%.0f Hz", *refresh);
                where = hz;
            }
            if (i > 0)
            {
                ImGui::SameLine(0.0f, gap);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ui::drawChevron(draw, ImVec2(at.x + arrow * 0.5f, at.y + h * 0.5f), arrow, 0.0f, ImGui::GetColorU32(palette.faint));
                ImGui::ItemSize(ImVec2(arrow, h));
            }
            ImGui::SameLine(0.0f, gap);
            const ImVec2 at    = ImGui::GetCursorScreenPos();
            const ImVec2 named = fonts.bold->CalcTextSizeA(title, FLT_MAX, 0.0f, name.c_str());
            const ImVec2 sub   = fonts.body->CalcTextSizeA(under, FLT_MAX, 0.0f, where.c_str());
            const float  w     = std::max(ui::px(96.0f), std::max(named.x, sub.x) + 2.0f * padX);
            const float  textY = at.y + (h - named.y - sub.y) * 0.5f;
            draw->AddRectFilled(at, ImVec2(at.x + w, at.y + h), ImGui::GetColorU32(generation ? ui::mix(palette.row, palette.accent, 0.16f) : palette.row), ui::px(ui::Size::surface));
            if ((generation && elsewhere && makoRuns) || (scaler && !scales))
                draw->AddRect(at, ImVec2(at.x + w, at.y + h), ImGui::GetColorU32(palette.warn), ui::px(ui::Size::surface), ui::px(1.5f));
            draw->AddText(fonts.bold, title, ImVec2(at.x + padX, textY), ImGui::GetColorU32(generation ? palette.accent : palette.text), name.c_str());
            draw->AddText(fonts.body, under, ImVec2(at.x + padX, textY + named.y), ImGui::GetColorU32(palette.faint), where.c_str());
            ImGui::ItemSize(ImVec2(w, h));
            const std::string item = name + " step";
            UiMap::nameNextItem(item.c_str());
            ImGui::ItemAdd(ImRect(at, ImVec2(at.x + w, at.y + h)), ImGui::GetID(item.c_str()));
            if (generation && makoRuns)
                ImGui::SetItemTooltip("%s", ("MAKO generates frames on " + frameGenerationGpuName + ", the GPU the game renders on: MAKO does not support\n"
                                             "frame generation on another GPU than the game's.")
                                                .c_str());
            else if (scaler && scales)
                ImGui::SetItemTooltip("MAKO scales each frame on the game's GPU, from the size the game draws at to the size it presents");
            else if (scaler)
                ImGui::SetItemTooltip("%s", scaling.detail.c_str());
            else if (generation)
                ImGui::SetItemTooltip("%s", ("lsfg-vk 2 generates frames on " + frameGenerationGpuName + ", the GPU the game renders on: it hands each frame to its pipeline as memory\n"
                                             "only that GPU and its driver can open, and it has no setting for another GPU.")
                                                .c_str());
            else if (step.what == "Display" && refresh)
                ImGui::SetItemTooltip("The current mode of the display this swapchain fills, as the compositor reports it");
        }
    }

    void ImGuiOverlay::renderFrameGenerationGpuRow(FrameGenerationLayer layer)
    {
        const OverlayPalette& palette = activePalette();
        const bool            mako    = layer == FrameGenerationLayer::Mako;
        const std::string     shown   = "GPU " + std::to_string(gameGpu) + ", " + frameGenerationGpuName;
        const std::string     why     = mako ? "MAKO generates frames only on the GPU the game renders on: it supports no frame generation on another GPU.\n"
                                               "Effects can still run on another GPU, chosen on the GPUs card."
                                             : "lsfg-vk 2 generates frames only on the GPU the game renders on: it hands each frame to its pipeline as memory\n"
                                               "only that GPU and its driver can open, and it has no setting for another GPU.\n"
                                               "Effects can still run on another GPU, chosen on the GPUs card.";
        const float           column  = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
        ui::label("GPU");
        ImGui::SetItemTooltip("%s", why.c_str());
        ui::atColumn(column);
        ImGui::AlignTextToFramePadding();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::TextColored(palette.secondary, "%s", shown.c_str());
        UiMap::nameNextItem(shown.c_str());
        ImGui::ItemAdd(ImRect(at, ImVec2(at.x + ImGui::GetItemRectSize().x, at.y + ImGui::GetFrameHeight())), ImGui::GetID("##frameGenerationGpu"));
        ImGui::SetItemTooltip("%s", why.c_str());
        if (const std::optional<std::string> elsewhere = mako ? makoGpuElsewhere() : std::nullopt)
        {
            ImGui::SetCursorPosX(column);
            UiMap::nameNextItem("Use the game's GPU");
            if (ui::lineButton("Use the game's GPU"))
                useGamesGpuInMako();
            ImGui::SetItemTooltip("Removes the GPU from MAKO's profile, so MAKO generates frames on the GPU the game renders on");
            ImGui::SameLine(0.0f, ui::px(ui::Size::groupGap));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(palette.warn, "%s", elsewhere->c_str());
            ImGui::SetItemTooltip("%s", why.c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
    }

    void ImGuiOverlay::renderFrameGenerationView()
    {
        const OverlayPalette& palette = activePalette();
        refreshFrameGenerationFile(false);
        refreshMakoFile(false);

        const FrameGenerationState fg      = frameGenerationState();
        const bool                 mako    = fg.layer == FrameGenerationLayer::Mako;
        const std::string          name    = mako ? "MAKO" : "lsfg-vk";
        const std::string&         blocker = fg.blocker;
        const bool                 loaded  = mako ? makoLayerLoaded() : frameGenerationLayerLoaded();
        bool                       on      = blocker.empty() && fg.on;

        const FrameGenerationProfile* inUse     = !mako && blocker.empty() ? frameGenerationProfileInUse() : nullptr;
        const MakoProfile*            makoInUse = mako && blocker.empty() ? makoProfileInUse() : nullptr;
        const MakoSettings*           makoSet   = makoInUse ? &makoSettingsFor(*makoInUse, powerSupplies.source) : nullptr;
        const std::string             amount    = makoSet ? (makoSet->adaptive ? std::string("Adaptive") : std::to_string(makoSet->multiplier) + "x")
                                                          : std::to_string(inUse ? inUse->multiplier : lowestOnMultiplier) + "x";

        const bool               thisRunAfter = effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);
        std::vector<std::string> waiting;
        if (blocker.empty() && effectsAfterFrameGenerationSaved() != thisRunAfter)
            waiting.push_back("Effects after frame generation");
        const FrameGenerationTab* chosen     = frameGenerationTabChosen(fg.layer);
        const std::string         modelNow   = makoInUse ? makoSet->frameGenerationModel : MakoSettings{}.frameGenerationModel;
        const bool                layerWaits = frameGenerationTabWaits(frameGenerationTabOf(fg.layer, modelNow), loadedFrameGenerationLayer(),
                                                                       makoSettingsAtStart().frameGenerationModel);
        if (layerWaits)
            waiting.push_back("Layer");
        if (mako && blocker.empty())
            for (const std::string& changed : makoWaiting(makoInUse))
                waiting.push_back(changed);
        if (inUse && frameGenerationAtStart && frameGenerationAtStart->name == inUse->name)
        {
            if (inUse->overridePresentMode != frameGenerationAtStart->overridePresentMode)
                waiting.push_back("Force vsync (FIFO)");
            if (inUse->preserveSwapchainImageCount != frameGenerationAtStart->preserveSwapchainImageCount)
                waiting.push_back("Keep swapchain image count");
        }
        if (!mako && blocker.empty())
            for (const std::string& global : frameGenerationGlobalsWaiting())
                waiting.push_back(global);

        const std::string readyFromFirstLaunch = startedWithVkdaedalusRun() ? "" : "\nA game started with vkdaedalus-run has " + name + " ready from its first launch.";
        const std::string notLoaded            = fg.loadsNextLaunch ? name + " was not loaded when this game started" : std::string(makoNeedsVkdaedalusRun);
        ui::heading("Frame generation");
        if (inUse)
            ImGui::SetItemTooltip("Kept in lsfg-vk's profile '%s', used in: %s", inUse->name.c_str(), joined(inUse->activeIn).c_str());
        else if (makoInUse)
            ImGui::SetItemTooltip("Kept in MAKO's profile '%s', used in: %s", makoInUse->name.c_str(), joined(makoInUse->activeIn).c_str());
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!blocker.empty())
            ImGui::TextColored(palette.faint, "%s", fg.notInstalled ? "Not installed" : "Unavailable");
        else if (!on && !loaded)
        {
            ImGui::TextColored(palette.muted, "Off, not loaded");
            ImGui::SetItemTooltip("%s", fg.loadsNextLaunch ? (notLoaded + ", so On runs from the next launch." + readyFromFirstLaunch).c_str() : notLoaded.c_str());
        }
        else if (!on)
            ImGui::TextColored(palette.muted, "Off");
        else if (mako && !fg.unavailable.empty())
        {
            ImGui::TextColored(palette.warn, "%s, needs a model", amount.c_str());
            ImGui::SetItemTooltip("%s", fg.unavailable.c_str());
        }
        else if (fg.live)
            ImGui::TextColored(palette.ok, "%s, on", amount.c_str());
        else
        {
            ImGui::TextColored(palette.warn, "%s, %s", amount.c_str(), fg.loadsNextLaunch ? "next launch" : "needs vkdaedalus-run");
            ImGui::SetItemTooltip("%s", !loaded ? notLoaded.c_str()
                                        : mako  ? "MAKO set up no frame generation when this game started: Ready at start was off, or MAKO ran another profile"
                                                : "lsfg-vk was not loaded when this game started");
        }
        if (!waiting.empty())
        {
            ImGui::SameLine();
            const std::string pending = std::to_string(waiting.size()) + (waiting.size() == 1 ? " change" : " changes") + " at next launch";
            const std::string tip     = "Saved now, used at next launch: " + joined(waiting);
            ui::chip(pending.c_str(), palette.warn, tip.c_str());
        }

        const bool  neither = !frameGenerationLayerInstalled(FrameGenerationLayer::LsfgVk) && !frameGenerationLayerInstalled(FrameGenerationLayer::Mako);
        const float gap     = ui::px(ui::Size::gap);
        float       layerW  = ImGui::CalcTextSize("Layer").x + ui::px(restartMarkGap + restartMarkSize) + ui::px(ui::Size::groupGap);
        for (const FrameGenerationTab& tab : frameGenerationTabs)
            layerW += gap + ui::buttonWidth(std::string(tab.shown).c_str(), ui::Kind::Quiet);
        const float controls = (neither ? 0.0f : layerW) + ui::px(ui::Size::box) + gap + ImGui::CalcTextSize("On").x;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < controls)
        {
            ImGui::NewLine();
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
        ui::alignRight(controls);
        if (!neither)
        {
            renderLayerChoice(chosen, layerWaits);
            ImGui::SameLine(0.0f, ui::px(ui::Size::groupGap));
        }
        ImGui::BeginDisabled(!blocker.empty());
        const bool flipped = ui::checkbox("On###frameGeneration", &on);
        ImGui::EndDisabled();
        const bool unprovisioned = mako && loaded && makoLiveHere() && makoAtStart && !makoSettingsFor(*makoAtStart, powerSupplies.source).frameGenerationProvisioned;
        if (fg.notInstalled)
            ImGui::SetItemTooltip("%s is not installed", name.c_str());
        else if (blocker.empty() && unprovisioned)
            ImGui::SetItemTooltip("Ready at start was off when this game started, so MAKO set up no frame generation:\nOn runs from the next launch.");
        else if (blocker.empty())
            ImGui::SetItemTooltip("Adds generated frames, about one frame more input lag.\nOff keeps these settings in %s's profile%s.%s", name.c_str(),
                                  fg.live ? ", so On is live again" : "", readyFromFirstLaunch.c_str());

        if (neither)
        {
            ui::divider();
            ui::errorText("lsfg-vk is not installed where this game's Vulkan loader looks for layers. It comes from lsfg-vk.dev.");
            ui::errorText("MAKO is not installed where this game's Vulkan loader looks for layers. It comes from github.com/eugeniosegala/MAKO.");
            ui::footer();
            ui::note("No frame generation layer installed");
            return;
        }
        if (!blocker.empty())
        {
            ui::divider();
            ui::errorText(blocker.c_str());
            ui::footer();
            if (fg.notInstalled)
                ui::note(mako ? "MAKO not installed" : "lsfg-vk 2 not installed");
            return;
        }
        if (frameGenerationGpuName.empty())
        {
            VkPhysicalDeviceProperties properties{};
            pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &properties);
            frameGenerationGpuName = properties.deviceName;
        }
        ui::beginScrollArea("##framegenerationbody");
        renderFrameGenerationGpuRow(fg.layer);
        renderPipelineStrip(fg.layer);
        if (mako)
            renderMakoBody(flipped, on, waiting);
        else
            renderLsfgBody(flipped, on, fg.live, waiting);
    }

    const FrameGenerationTab* ImGuiOverlay::frameGenerationTabChosen(FrameGenerationLayer layer)
    {
        if (layer == FrameGenerationLayer::LsfgVk)
            return &frameGenerationTabOf(layer, "");
        if (!readMakoOnce())
            return nullptr;
        const MakoProfile* inUse = makoProfileInUse();
        return &frameGenerationTabOf(layer, inUse ? makoSettingsFor(*inUse, makoEditedSet(*inUse)).frameGenerationModel : MakoSettings{}.frameGenerationModel);
    }

    std::string ImGuiOverlay::frameGenerationTabGreyed(const FrameGenerationTab& tab)
    {
        const bool mako = tab.layer == FrameGenerationLayer::Mako;
        if (!frameGenerationLayerInstalled(tab.layer))
            return std::string(mako ? "MAKO" : "lsfg-vk") + " is not installed where this game's Vulkan loader looks";
        if (activeProfilePath.empty())
            return "This run reads a fixed config file, which keeps no frame generation layer";
        if (mako && tab.makoModel == "mako-fg")
            return readMakoOnce() ? makoModelMissing(std::string(tab.makoModel)) : "";
        return "";
    }

    std::string ImGuiOverlay::frameGenerationTabDllMissing(const FrameGenerationTab& tab)
    {
        if (tab.layer == FrameGenerationLayer::Mako)
            return tab.makoModel == "lossless-scaling" && readMakoOnce() ? makoModelMissing(std::string(tab.makoModel)) : "";
        if (!readFrameGenerationOnce())
            return "";
        if (!frameGenerationDll)
            frameGenerationDll = lsfgDllStatus(frameGenerationFile->global);
        return frameGenerationDll->found || frameGenerationDll->inSteamLibrary ? "" : "lsfg-vk.dll is in no Steam library";
    }

    void ImGuiOverlay::chooseFrameGenerationTab(const FrameGenerationTab& tab)
    {
        frameGenerationLayerChoice = tab.layer;
        autoSaveProfile();
        Logger::info("frame generation layer of profile " + activeProfileName + " set to " + std::string(frameGenerationLayerName(tab.layer))
                     + (tab.makoModel.empty() ? "" : " with the model " + std::string(tab.makoModel)));
        bool notInstalled = false;
        if (tab.layer != FrameGenerationLayer::Mako || !makoBlocker(notInstalled).empty())
            return;
        if (const MakoProfile* inUse = makoProfileInUse())
        {
            const MakoPowerSource edited = makoEditedSet(*inUse);
            if (makoSettingsFor(*inUse, edited).frameGenerationModel == tab.makoModel)
                return;
            const MakoProfile changed = makoProfileWithModel(*inUse, edited, tab.makoModel);
            saveMako(makoGlobalWithDll(makoSettingsFor(changed, edited)), changed);
            return;
        }
        if (tab.makoModel == MakoSettings{}.frameGenerationModel)
            return;
        std::vector<std::string> games = ConfigSerializer::profileUsers()[activeProfileName];
        if (std::find(games.begin(), games.end(), activeGameName) == games.end())
            games.push_back(activeGameName);
        MakoProfile made                   = newMakoProfileFor(activeProfileName, games);
        made.settings.frameGenerationModel = std::string(tab.makoModel);
        saveMako(makoGlobalWithDll(made.settings), made);
        if (makoProblem.empty())
            frameGenerationAssignmentsChanged("");
    }

    void ImGuiOverlay::renderLayerChoice(const FrameGenerationTab* chosen, bool waits)
    {
        const OverlayPalette& palette = activePalette();
        const std::string     kept    = "Read when a game starts: vkdaedalus-run loads this layer alone and keeps the other out.\nThe layer is kept in the vkDaedalus profile "
                                        + activeProfileName;
        const bool fellBack = makoLiveHere() && makoAtStart && makoFellBackToMakoFg(makoSettingsAtStart().frameGenerationModel, makoStatus.ofSwapchain);
        ui::label("Layer");
        ImGui::SetItemTooltip("%s.", kept.c_str());
        for (const FrameGenerationTab& tab : frameGenerationTabs)
        {
            const bool        lossless = tab.makoModel == "lossless-scaling";
            const std::string what     = tab.layer == FrameGenerationLayer::LsfgVk ? "lsfg-vk, which needs lsfg-vk.dll.\n" + kept + "."
                                         : lossless                                ? "MAKO running Lossless Scaling's model, which needs Lossless.dll.\n" + kept
                                                                                         + ", the model in MAKO's profile."
                                                                                   : "MAKO running its own model, MAKO FG, which needs no DLL.\n" + kept
                                                                                         + ", the model in MAKO's profile.";
            const std::string why      = frameGenerationTabGreyed(tab);
            const std::string missing  = why.empty() ? frameGenerationTabDllMissing(tab) : "";
            const std::string item     = std::string(tab.shown) + "##Layer";
            ImGui::SameLine(0.0f, ui::px(ui::Size::gap));
            ImGui::BeginDisabled(!why.empty());
            if (ui::tab(item.c_str(), chosen == &tab) && chosen != &tab)
                chooseFrameGenerationTab(tab);
            ImGui::EndDisabled();
            if ((lossless && fellBack) || !missing.empty())
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ImGui::GetItemRectMax().x - ui::px(6.0f), ImGui::GetItemRectMin().y + ui::px(6.0f)), ui::px(3.0f),
                                                            ImGui::GetColorU32(palette.warn));
            if (lossless && fellBack)
                ImGui::SetItemTooltip("MAKO could not start Lossless Scaling's model when this game started: this run has MAKO FG in its place.%s%s", why.empty() ? "" : "\n",
                                      why.c_str());
            else if (!missing.empty())
                ImGui::SetItemTooltip("%s\n%s. Chosen, its card's DLL row looks for it again.", what.c_str(), missing.c_str());
            else
                ImGui::SetItemTooltip("%s", why.empty() ? what.c_str() : why.c_str());
        }
        restartMark("Layer", waits, palette);
    }

    void ImGuiOverlay::renderFrameOrderNote()
    {
        const OverlayPalette&            palette      = activePalette();
        const bool                       thisRunAfter = effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);
        const bool                       after        = effectsAfterFrameGenerationSaved();
        const std::optional<std::string> named        = programNamedByVkdaedalusRun();
        if (after != thisRunAfter && !startedWithVkdaedalusRun())
            ui::noteBox("Not started with vkdaedalus-run. Launch option: vkdaedalus-run %command%", palette.warn);
        else if (after != thisRunAfter && named && activeGameName != *named)
            ui::noteBox(("vkdaedalus-run named this game " + *named + " and vkDaedalus names it " + activeGameName + ", so the launcher reads another profile.").c_str(),
                        palette.warn);
        else if (after != thisRunAfter)
            ui::noteBox(thisRunAfter ? "Effects after frame generation in this run, before in the profile" : "Effects before frame generation in this run, after in the profile",
                        palette.warn);
    }

    void ImGuiOverlay::renderLsfgBody(bool flipped, bool on, bool live, const std::vector<std::string>& waiting)
    {
        const OverlayPalette&         palette      = activePalette();
        const FrameGenerationProfile* inUse        = frameGenerationProfileInUse();
        const bool                    thisRunAfter = effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);
        if (on && live)
        {
            const FrameStats frames = frameStats(latency.lastSecondMs);
            if (frames.count > 0)
            {
                const FrameRates  rates    = frameRatesOf(frames.fps, inUse->multiplier, thisRunAfter);
                char              game[48] = "";
                char              shown[48] = "";
                std::snprintf(game, sizeof(game), "%.0f FPS", rates.gameFps);
                std::snprintf(shown, sizeof(shown), "%.0f FPS", rates.shownFps);
                const float gap    = ImGui::GetStyle().ItemSpacing.x;
                const float tilde  = ImGui::CalcTextSize("~").x;
                const float labels = std::max(ImGui::CalcTextSize("Rendered").x, ImGui::CalcTextSize("Shown").x);
                const float width  = labels + gap + tilde + std::max(ImGui::CalcTextSize(game).x, ImGui::CalcTextSize(shown).x);
                const auto  rate   = [&](const char* name, const char* value, bool measured) {
                    ImGui::TextColored(palette.muted, "%s", name);
                    ImGui::SameLine(labels + gap);
                    if (!measured)
                        ImGui::TextColored(palette.warn, "~");
                    else
                        ImGui::Dummy(ImVec2(tilde, ImGui::GetTextLineHeight()));
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextUnformatted(value);
                };
                ui::alignRight(width);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0.0f));
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (ui::px(ui::Size::row) - 2.0f * ImGui::GetTextLineHeight()) * 0.5f)));
                rate("Rendered", game, rates.gameMeasured);
                rate("Shown", shown, rates.shownMeasured);
                ImGui::EndGroup();
                ImGui::PopStyleVar();
                ImGui::SetItemTooltip(thisRunAfter ? "Every frame shown passes vkDaedalus, so shown is timed and rendered is it divided by %ux"
                                                   : "vkDaedalus sees only the game's own frames, so shown is rendered times %ux",
                                      inUse->multiplier);
            }
        }

        const std::string shownProfile = inUse ? inUse->name : frameGenerationProfileNameOf(activeProfileName);
        if (frameGenerationLayerLoaded() && frameGenerationProfileHere && *frameGenerationProfileHere != shownProfile)
            ui::noteBox(("Running " + *frameGenerationProfileHere + "; " + shownProfile + " from the next launch").c_str(), palette.warn);
        if (!frameGenerationDll)
            frameGenerationDll = lsfgDllStatus(frameGenerationFile->global);
        if (!frameGenerationDll->found && !frameGenerationDll->inSteamLibrary)
        {
            ui::errorText("lsfg-vk.dll not found");
            ImGui::SetItemTooltip("Install Lossless Scaling in Steam, on its lsfg-vk branch (Properties, Betas)");
            if (ui::rowButton("Look again"))
                frameGenerationDll.reset();
        }
        ui::divider();

        if (flipped)
        {
            ui::endScrollArea();
            switchFrameGeneration(on);
            return;
        }
        if (!inUse)
        {
            renderFrameGenerationGlobals();
            if (!frameGenerationProblem.empty())
                ui::errorText(("Not changed: " + frameGenerationProblem).c_str());
            ui::endScrollArea();
            ui::footer();
            if (activeProfileName.empty())
                ui::note("Off");
            else
                ui::notef("Off for profile %s", activeProfileName.c_str());
            return;
        }

        FrameGenerationProfile tuned   = frameGenerationDraft ? *frameGenerationDraft : *inUse;
        bool                   changed = false;
        bool                   held    = false;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##framegen", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (table)
        {
            ImGui::TableNextColumn();
            const auto caption = [&](const char* text, const char* tip) {
                ui::sectionLabel(text);
                ImGui::SetItemTooltip("%s", tip);
                ImGui::SameLine();
                ImGui::PushFont(nullptr, FontSize::label);
                ImGui::TextColored(palette.faint, "profile");
                ImGui::PopFont();
                ImGui::SetItemTooltip("%s", tip);
                ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
            };
            const std::string liveTip = "Kept in lsfg-vk's profile " + inUse->name + ", which every game running the vkDaedalus profile " + activeProfileName +
                                        " shares. Applies at once.";
            caption("Live", liveTip.c_str());
            ui::beginPanel();
            if (on)
            {
                ui::panelRow();
                int  multiplier = static_cast<int>(tuned.multiplier);
                char shown[16];
                std::snprintf(shown, sizeof(shown), "%dx", multiplier);
                ui::sliderRowBegin("Multiplier", shown);
                ui::sliderInt("##Multiplier", &multiplier, lowestOnMultiplier, highestOfferedMultiplier, ImGuiSliderFlags_AlwaysClamp);
                held             = ImGui::IsItemActive();
                changed          = ImGui::IsItemDeactivatedAfterEdit();
                tuned.multiplier = static_cast<uint32_t>(multiplier);
                ImGui::SetItemTooltip("Frames shown per real frame: 2x doubles them");
                ui::sliderRowEnd(shown);
            }
            ui::panelRow();
            char flow[16];
            std::snprintf(flow, sizeof(flow), "%.2f", tuned.flowScale);
            ui::sliderRowBegin("Flow scale", flow);
            ui::sliderFloat("##Flow scale", &tuned.flowScale, lowestFlowScale, highestFlowScale, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            tuned.flowScale = std::round(tuned.flowScale * 100.0f) / 100.0f;
            held            = held || ImGui::IsItemActive();
            changed         = ImGui::IsItemDeactivatedAfterEdit() || changed;
            ImGui::SetItemTooltip("Lower is faster and looks worse");
            ui::sliderRowEnd(flow);
            ui::panelRow();
            changed = ui::checkbox("Performance mode", &tuned.performanceMode) || changed;
            ImGui::SetItemTooltip("Much faster, slightly lower quality");
            ui::endPanel();
            const auto startOnly = [&](const char* label, bool& value, const char* tooltip) {
                const bool flippedHere = ui::checkbox(label, &value);
                ImGui::SetItemTooltip("Read when the game starts. %s", tooltip);
                restartMark(label, std::find(waiting.begin(), waiting.end(), std::string(label)) != waiting.end(), palette);
                return flippedHere;
            };
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
            caption("Output", liveTip.c_str());
            ui::beginPanel();
            const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
            const auto  combo  = [&](const char* label, const char* id, auto& choices, std::optional<std::string>& value) {
                ui::label(label);
                ui::atColumn(column);
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool chosen = chooseFrom(id, choices, value, "sRGB");
                ImGui::SetItemTooltip("Leave sRGB unless the game outputs HDR");
                return chosen;
            };
            ui::panelRow();
            changed = combo("Colour space", "##Colour space", lsfgColorSpaces, tuned.colorSpace) || changed;
            ui::panelRow();
            changed = combo("Transfer", "##Transfer function", lsfgTransferFunctions, tuned.transferFunction) || changed;
            ui::endPanel();

            ui::nextColumn();
            caption("Next launch", "Saved now and read when a game on this profile starts: lsfg-vk builds its swapchain then, and the Vulkan "
                                                          "loader fixes the order of the layers then.");
            ui::beginPanel();
            bool after = effectsAfterFrameGenerationSaved();
            if (!activeProfilePath.empty())
            {
                ui::panelRow();
                if (startOnly("Effects after frame generation", after, "Needs vkdaedalus-run. Saved in the vkDaedalus profile."))
                {
                    effectsAfterFrameGeneration = after;
                    autoSaveProfile();
                }
            }
            ui::panelRow();
            changed = startOnly("Force vsync (FIFO)", tuned.overridePresentMode, "lsfg-vk paces its frames on FIFO; its docs say leave it on.") || changed;
            ui::panelRow();
            changed = startOnly("Keep swapchain image count", tuned.preserveSwapchainImageCount, "Only if a game crashes with frame generation, says lsfg-vk.") || changed;
            ui::endPanel();
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
            renderFrameGenerationGlobals();
            ImGui::EndTable();
        }

        renderFrameOrderNote();

        if (held)
            frameGenerationDraft = tuned;
        else
            frameGenerationDraft.reset();
        if (changed)
            saveFrameGeneration(std::nullopt, tuned);
        if (!frameGenerationProblem.empty())
            ui::errorText(("Not changed: " + frameGenerationProblem).c_str());
        ui::endScrollArea();
        ui::footer();
        ui::notef("lsfg-vk profile %s, %zu %s", inUse->name.c_str(), inUse->activeIn.size(), inUse->activeIn.size() == 1 ? "game" : "games");
    }

    void ImGuiOverlay::renderFrameGenerationGlobals()
    {
        static ImGui::FileBrowser dllBrowser;
        const OverlayPalette&     palette = activePalette();
        ImGui::AlignTextToFramePadding();
        ui::sectionLabel("lsfg-vk");
        ImGui::SetItemTooltip("lsfg-vk's own settings, the same for every game. Read when a game starts.");
        ImGui::SameLine();
        ImGui::PushFont(nullptr, FontSize::label);
        ImGui::TextColored(palette.faint, "all games");
        ImGui::PopFont();
        ImGui::SetItemTooltip("lsfg-vk's own settings, the same for every game. Read when a game starts.");
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        if (!frameGenerationLayerLoaded() && frameGenerationLayerVisible() == false)
        {
            ui::note("lsfg-vk is not installed");
            return;
        }
        if (!readFrameGenerationOnce())
        {
            ui::errorText(("lsfg-vk refuses its file " + lsfgConfigPath() + ": " + frameGenerationFileProblem).c_str());
            return;
        }
        FrameGenerationGlobal global = frameGenerationFile->global;
        if (!frameGenerationGlobalAtStart)
            frameGenerationGlobalAtStart = global;
        ui::beginPanel();
        ui::panelRow();
        const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
        ui::label("Half precision");
        ImGui::SetItemTooltip("Read when a game starts. lsfg-vk: it does not affect quality.");
        ui::atColumn(column);
        bool changed = ui::checkbox("##Half precision", &global.allowHalfPrecision);
        ImGui::SetItemTooltip("Read when a game starts. lsfg-vk: it does not affect quality.");
        restartMark("Half precision", global.allowHalfPrecision != frameGenerationGlobalAtStart->allowHalfPrecision, palette);

        if (!frameGenerationDll)
            frameGenerationDll = lsfgDllStatus(global);
        ui::panelRow();
        ui::label("lsfg-vk.dll");
        ImGui::SetItemTooltip("The frame generation library from Lossless Scaling, which lsfg-vk needs");
        ui::atColumn(column);
        ImGui::AlignTextToFramePadding();
        if (frameGenerationDll->found)
        {
            ImGui::TextColored(palette.ok, "Found");
            ImGui::SetItemTooltip("%s", frameGenerationDll->path->c_str());
            ImGui::SameLine();
            const std::string where = frameGenerationDll->inSteamLibrary ? "Steam library" : frameGenerationDll->path->substr(0, frameGenerationDll->path->rfind('/'));
            const float       tail  = ui::px(ui::Size::gap) + ui::buttonWidth("Change...", ui::Kind::Secondary, true) + ui::px(restartMarkGap + restartMarkSize);
            ImGui::PushFont(nullptr, FontSize::small);
            ImGui::TextColored(palette.muted, "%s", ui::elideStart(where, ImGui::GetContentRegionAvail().x - tail).c_str());
            ImGui::PopFont();
        }
        else
            ImGui::TextColored(palette.bad, "Not found");
        const float trailing = ui::buttonWidth("Change...", ui::Kind::Secondary, true)
                             + (frameGenerationDll->found ? 0.0f : ImGui::GetStyle().ItemSpacing.x + ui::buttonWidth("Look again", ui::Kind::Secondary, true))
                             + ui::px(restartMarkGap + restartMarkSize);
        ui::alignRight(trailing);
        if (ui::lineButton("Change..."))
        {
            dllBrowser.SetTitle("Choose lsfg-vk.dll");
            dllBrowser.SetTypeFilters({".dll"});
            const char* home = std::getenv("HOME");
            dllBrowser.SetPwd(home ? home : "/");
            dllBrowser.Open();
        }
        if (!frameGenerationDll->found)
        {
            ImGui::SameLine();
            if (ui::lineButton("Look again"))
                frameGenerationDll.reset();
        }
        restartMark("lsfg-vk.dll", global.dll != frameGenerationGlobalAtStart->dll, palette);
        ui::endPanel();
        ImGuiErrorRecoveryState beforeBrowser;
        ImGui::ErrorRecoveryStoreState(&beforeBrowser);
        try
        {
            dllBrowser.Display();
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            ImGui::ErrorRecoveryTryToRecoverState(&beforeBrowser);
            dllBrowser.Close();
            frameGenerationProblem = std::string("the file browser could not open a folder: ") + e.what();
            Logger::warn(frameGenerationProblem);
        }
        if (dllBrowser.HasSelected())
        {
            global.dll = dllBrowser.GetSelected().string();
            dllBrowser.ClearSelected();
            changed = true;
        }
        if (changed)
            saveFrameGeneration(global, std::nullopt);
    }
}
