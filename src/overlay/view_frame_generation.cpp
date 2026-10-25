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
        bool chooseFrom(const char* label, const std::array<LsfgChoice, N>& choices, std::optional<std::string>& value, const char* unset)
        {
            const auto shown = [&](const std::string& name) {
                const auto at = std::find_if(choices.begin(), choices.end(), [&](const LsfgChoice& choice) { return choice.name == name; });
                return at == choices.end() ? name : std::string(at->shown);
            };
            const std::string preview = value ? shown(*value) : unset;
            bool              chosen  = false;
            if (ui::beginCombo(label, preview.c_str()))
            {
                for (const LsfgChoice& choice : choices)
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

        std::optional<std::filesystem::file_time_type> writeTimeOf(const std::string& path)
        {
            std::error_code unreadable;
            const auto      written = std::filesystem::last_write_time(path, unreadable);
            if (unreadable)
                return std::nullopt;
            return written;
        }

        std::string joined(const std::vector<std::string>& names)
        {
            std::string out;
            for (const std::string& name : names)
                out += (out.empty() ? "" : ", ") + name;
            return out;
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
        frameGenerationCheckedAt   = std::chrono::steady_clock::now();
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
        if (!frameGenerationRead)
            return;
        const auto clock = std::chrono::steady_clock::now();
        if (!now && clock - frameGenerationCheckedAt < std::chrono::milliseconds(500))
            return;
        frameGenerationCheckedAt                                     = clock;
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
        if (!renamedFrom.empty() && !renameLsfgProfile(path, lsfgProfileNameOf(renamedFrom), lsfgProfileNameOf(activeProfileName), problem))
            Logger::warn("lsfg-vk's profile " + lsfgProfileNameOf(renamedFrom) + " keeps its name, so frame generation stays with the old name: " + problem);
        if (!syncLsfgActiveIn(path, ConfigSerializer::profileUsers(), problem))
            Logger::warn("lsfg-vk's file " + path + " keeps its game lists, so frame generation may follow another profile: " + problem);
        refreshFrameGenerationFile(true);
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
        return profileNamed(*frameGenerationFile, lsfgProfileNameOf(activeProfileName));
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
            frameOrderProfilePath       = activeProfilePath;
            effectsAfterFrameGeneration = ConfigSerializer::loadProfileSettings(activeProfilePath).effectsAfterFrameGeneration.value_or(false);
        }
        return effectsAfterFrameGeneration;
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
        if (!loaded && std::getenv("DISABLE_LSFGVK") != nullptr)
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
        bool              notInstalled = false;
        const std::string blocker      = frameGenerationBlocker(notInstalled);
        shortNoticeStart               = std::chrono::steady_clock::now();
        shortNoticeProblem             = !blocker.empty();
        if (!blocker.empty())
        {
            shortNoticeLine = "unavailable: " + blocker;
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

    void ImGuiOverlay::renderPipelineStrip()
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
        const std::optional<uint32_t>   multiplier = frameGenerationMultiplierHere();
        const std::vector<PipelineStep> steps =
            pipelineSteps(pLogicalDevice->frameGenerationPlace, multiplier.has_value(), state.effectsEnabled ? effectGpus : std::vector<uint32_t>{});
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
            const std::string   name       = generation ? "lsfg-vk " + std::to_string(*multiplier) + "x" : step.what;
            std::string         where      = "GPU ";
            for (size_t g = 0; g < step.gpus.size(); g++)
                where += (g ? " and " : "") + std::to_string(step.gpus[g]);
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
            draw->AddText(fonts.bold, title, ImVec2(at.x + padX, textY), ImGui::GetColorU32(generation ? palette.accent : palette.text), name.c_str());
            draw->AddText(fonts.body, under, ImVec2(at.x + padX, textY + named.y), ImGui::GetColorU32(palette.faint), where.c_str());
            ImGui::ItemSize(ImVec2(w, h));
            const std::string item = name + " step";
            UiMap::nameNextItem(item.c_str());
            ImGui::ItemAdd(ImRect(at, ImVec2(at.x + w, at.y + h)), ImGui::GetID(item.c_str()));
            if (generation)
                ImGui::SetItemTooltip("%s", ("lsfg-vk 2 generates frames on " + frameGenerationGpuName + ", the GPU the game renders on: it hands each frame to its pipeline as memory\n"
                                             "only that GPU and its driver can open, and it has no setting for another GPU.")
                                                .c_str());
            else if (step.what == "Display" && refresh)
                ImGui::SetItemTooltip("The current mode of the display this swapchain fills, as the compositor reports it");
        }
    }

    void ImGuiOverlay::renderFrameGenerationView()
    {
        const OverlayPalette& palette = activePalette();
        refreshFrameGenerationFile(false);

        bool              notInstalled = false;
        const std::string blocker      = frameGenerationBlocker(notInstalled);

        const FrameGenerationProfile* inUse = blocker.empty() ? frameGenerationProfileInUse() : nullptr;
        const bool                    live  = blocker.empty() && frameGenerationLiveHere();
        bool                          on    = blocker.empty() && generatesFrames(inUse);

        const bool               thisRunAfter = effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);
        std::vector<std::string> waiting;
        if (blocker.empty() && effectsAfterFrameGenerationSaved() != thisRunAfter)
            waiting.push_back("Effects after frame generation");
        if (inUse && frameGenerationAtStart && frameGenerationAtStart->name == inUse->name)
        {
            if (inUse->overridePresentMode != frameGenerationAtStart->overridePresentMode)
                waiting.push_back("Force vsync (FIFO)");
            if (inUse->preserveSwapchainImageCount != frameGenerationAtStart->preserveSwapchainImageCount)
                waiting.push_back("Keep swapchain image count");
        }
        if (blocker.empty())
            for (const std::string& global : frameGenerationGlobalsWaiting())
                waiting.push_back(global);

        ui::heading("Frame generation");
        if (inUse)
            ImGui::SetItemTooltip("Kept in lsfg-vk's profile '%s', used in: %s", inUse->name.c_str(), joined(inUse->activeIn).c_str());
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!blocker.empty())
            ImGui::TextColored(palette.faint, "%s", notInstalled ? "Not installed" : "Unavailable");
        else if (!on && !frameGenerationLayerLoaded())
        {
            ImGui::TextColored(palette.muted, "Off, not loaded");
            ImGui::SetItemTooltip("lsfg-vk was not loaded when this game started, so On runs from the next launch.%s",
                                  std::getenv("VKDAEDALUS_RUN") == nullptr ? "\nA game started with vkdaedalus-run has lsfg-vk ready from its first launch." : "");
        }
        else if (!on)
            ImGui::TextColored(palette.muted, "Off");
        else if (live)
            ImGui::TextColored(palette.ok, "%ux, on", inUse->multiplier);
        else
        {
            ImGui::TextColored(palette.warn, "%ux, next launch", inUse ? inUse->multiplier : lowestOnMultiplier);
            ImGui::SetItemTooltip("lsfg-vk was not loaded when this game started");
        }
        if (!waiting.empty())
        {
            ImGui::SameLine();
            const std::string pending = std::to_string(waiting.size()) + (waiting.size() == 1 ? " change" : " changes") + " at next launch";
            const std::string tip     = "Saved now, used at next launch: " + joined(waiting);
            ui::chip(pending.c_str(), palette.warn, tip.c_str());
        }

        ui::alignRight(ui::px(ui::Size::box) + ui::px(ui::Size::gap) + ImGui::CalcTextSize("On").x);
        ImGui::BeginDisabled(!blocker.empty());
        const bool flipped = ui::checkbox("On###frameGeneration", &on);
        ImGui::EndDisabled();
        if (notInstalled)
            ImGui::SetItemTooltip("lsfg-vk is not installed");
        else if (blocker.empty())
            ImGui::SetItemTooltip("Adds generated frames, about one frame more input lag.\nOff keeps these settings in lsfg-vk's profile%s.%s", live ? ", so On is live again" : "",
                                  std::getenv("VKDAEDALUS_RUN") == nullptr ? "\nA game started with vkdaedalus-run has lsfg-vk ready from its first launch." : "");

        if (!blocker.empty())
        {
            ui::divider();
            ui::errorText(blocker.c_str());
            ui::footer();
            if (notInstalled)
                ui::note("lsfg-vk 2 not installed");
            return;
        }
        if (frameGenerationGpuName.empty())
        {
            VkPhysicalDeviceProperties properties{};
            pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &properties);
            frameGenerationGpuName = properties.deviceName;
        }
        ui::beginScrollArea("##framegenerationbody");
        renderPipelineStrip();
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

        const std::string shownProfile = inUse ? inUse->name : lsfgProfileNameOf(activeProfileName);
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

        const bool after = effectsAfterFrameGenerationSaved();
        if (after != thisRunAfter && std::getenv("VKDAEDALUS_RUN") == nullptr)
            ui::noteBox("Not started with vkdaedalus-run. Launch option: vkdaedalus-run %command%", palette.warn);
        else if (const char* named = std::getenv("VKDAEDALUS_RUN_PROGRAM"); after != thisRunAfter && named != nullptr && activeGameName != named)
            ui::noteBox(("vkdaedalus-run named this game " + std::string(named) + " and vkDaedalus names it " + activeGameName + ", so the launcher reads another profile.").c_str(),
                        palette.warn);
        else if (after != thisRunAfter)
            ui::noteBox(thisRunAfter ? "Effects after frame generation in this run, before in the profile" : "Effects before frame generation in this run, after in the profile",
                        palette.warn);

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
