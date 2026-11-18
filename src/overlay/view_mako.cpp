#include "imgui_overlay.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <unistd.h>

#include "config_serializer.hpp"
#include "frame_generation_place.hpp"
#include "latency_stats.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/frame_generation_rows.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/widgets.hpp"

#include "imgui.h"

namespace vkBasalt
{
    namespace
    {
        constexpr std::array<FrameGenerationChoice, 2> makoModes            = {{{"fixed", "Fixed"}, {"adaptive", "Adaptive"}}};
        constexpr std::array<FrameGenerationChoice, 5> makoHeadroomPercents = {{{"0", "Off"}, {"5", "5 %"}, {"10", "10 %"}, {"15", "15 %"}, {"20", "20 %"}}};

        MakoSettings& settingsOf(MakoProfile& profile, MakoPowerSource set)
        {
            if (!profile.power || set == MakoPowerSource::Unknown)
                return profile.settings;
            return set == MakoPowerSource::Handheld ? profile.power->handheld : profile.power->docked;
        }

        bool needsLosslessDll(const MakoSettings& set)
        {
            return set.frameGenerationModel == "lossless-scaling" || set.scalingMethod == "ls1" || set.scalingMethod == "ls1-performance";
        }

        const char* setName(MakoPowerSource set)
        {
            return set == MakoPowerSource::Handheld ? ", on-battery set" : set == MakoPowerSource::Docked ? ", plugged-in set" : "";
        }

        void caption(const char* text, const std::string& tag, const char* tip)
        {
            ui::sectionLabel(text);
            ImGui::SetItemTooltip("%s", tip);
            ImGui::SameLine();
            ImGui::PushFont(nullptr, FontSize::label);
            ImGui::TextColored(activePalette().faint, "%s", tag.c_str());
            ImGui::PopFont();
            ImGui::SetItemTooltip("%s", tip);
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        }
    }

    bool ImGuiOverlay::readMakoOnce()
    {
        if (makoRead)
            return makoFile.has_value();
        makoRead              = true;
        const std::string path = makoConfigPath();
        const MakoRead    read = readMakoFile(path);
        useMakoFile(read.file);
        makoFileProblem = read.problem;
        makoFileTime    = writeTimeOf(path);
        if (read.file)
            makoGlobalAtStart = read.file->global;
        if (makoProfileHere)
            makoAtStart = *makoProfileNamed(*makoFile, *makoProfileHere);
        return makoFile.has_value();
    }

    void ImGuiOverlay::refreshMakoFile(bool now)
    {
        const auto clock = std::chrono::steady_clock::now();
        if (powerPoll.due(clock))
        {
            const PowerSupplies read = readPowerSupplies("/sys/class/power_supply");
            powerSupplies.battery    = read.battery;
            if (read.source != MakoPowerSource::Unknown)
                powerSupplies.source = read.source;
        }
        if (!makoRead || (!makoFilePoll.due(clock) && !now))
            return;
        const std::string                                    path    = makoConfigPath();
        const std::optional<std::filesystem::file_time_type> written = writeTimeOf(path);
        if (!now && written == makoFileTime)
            return;
        const MakoRead read = readMakoFile(path);
        useMakoFile(read.file);
        makoFileProblem = read.problem;
        makoFileTime    = written;
        makoDraft.reset();
        makoDll.reset();
        Logger::debug("read MAKO's file " + path + " again" + (now ? "" : ", since it changed"));
    }

    void ImGuiOverlay::useMakoFile(const std::optional<MakoFile>& file)
    {
        makoFile        = file;
        makoProfileHere = makoProfileAfterRead(file, thisMakoProcess(), makoProfileHere);
    }

    void ImGuiOverlay::saveMako(const std::optional<MakoGlobal>& global, const std::optional<MakoProfile>& profile)
    {
        const std::string path = makoConfigPath();
        std::string       problem;
        MakoFile          written;
        if (saveMakoFile(path, global, profile, written, problem))
        {
            useMakoFile(written);
            makoFileTime = writeTimeOf(path);
            makoProblem.clear();
            if (global)
                makoDll.reset();
            Logger::info("MAKO's file " + path + " changed" + (profile ? " for its profile '" + profile->name + "'" : ""));
        }
        else
        {
            makoProblem = problem;
            Logger::err("MAKO's file " + path + " was not changed: " + problem);
        }
    }

    const MakoProfile* ImGuiOverlay::makoProfileInUse() const
    {
        if (!makoFile)
            return nullptr;
        if (makoProfileHere && !madeByVkDaedalus(*makoProfileHere))
            if (const MakoProfile* here = makoProfileNamed(*makoFile, *makoProfileHere))
                return here;
        return makoProfileNamed(*makoFile, frameGenerationProfileNameOf(activeProfileName));
    }

    bool ImGuiOverlay::makoLiveHere() const
    {
        const MakoProfile* inUse = makoProfileInUse();
        return makoLayerLoaded() && inUse && makoProfileHere && inUse->name == *makoProfileHere;
    }

    MakoPowerSource ImGuiOverlay::makoEditedSet(const MakoProfile& profile) const
    {
        if (!profile.power)
            return MakoPowerSource::Unknown;
        return makoTab.value_or(powerSupplies.source);
    }

    std::string ImGuiOverlay::makoBlocker(bool& notInstalled)
    {
        notInstalled      = false;
        const bool loaded = makoLayerLoaded();
        if (activeGameName.empty())
            return "This program has no name vkDaedalus can give MAKO.";
        if (!loaded && makoLayerVisible() == false)
        {
            notInstalled = true;
            return "MAKO is not installed where this game's Vulkan loader looks for layers.";
        }
        if (makoSettingsFromEnvironment())
            return "MAKO_ENV is set, so MAKO reads its settings from the game's launch options, not its file.";
        if (!loaded && frameGenerationLayerDisabledBy(FrameGenerationLayer::Mako))
            return "DISABLE_MAKO in the game's launch options keeps MAKO out of it.";
        if (!readMakoOnce())
            return "MAKO refuses its file " + makoConfigPath() + ": " + makoFileProblem + ".";
        if (ConfigSerializer::isWineLoaderName(activeGameName))
            return "This Wine process runs no .exe, so a profile for it would turn frame generation on in every Wine game.";
        return "";
    }

    std::string ImGuiOverlay::makoModelMissing(const std::string& model)
    {
        if (model == "mako-fg")
        {
            if (!makoFgChecked)
            {
                makoFgGone    = makoFgMissing();
                makoFgChecked = true;
            }
            return makoFgGone.value_or("");
        }
        if (!makoFile)
            return "";
        if (!makoDll)
            makoDll = makoDllStatus(makoFile->global);
        return makoDll->found || makoDll->inSteamLibrary ? "" : "Needs Lossless Scaling from Steam";
    }

    const MakoSettings* ImGuiOverlay::makoGeneratingHere()
    {
        if (!makoLayerLoaded())
            return nullptr;
        readMakoOnce();
        if (!cardsDrawn())
            refreshMakoFile(false);
        if (!makoLiveHere() || !makoAtStart)
            return nullptr;
        const MakoSettings& then = makoSettingsFor(*makoAtStart, powerSupplies.source);
        const MakoSettings& now  = makoSettingsFor(*makoProfileInUse(), powerSupplies.source);
        if (!then.frameGenerationProvisioned || !now.frameGenerationEnabled || !makoModelMissing(then.frameGenerationModel).empty())
            return nullptr;
        return &now;
    }

    std::optional<uint32_t> ImGuiOverlay::makoMultiplierHere()
    {
        const MakoSettings* now = makoGeneratingHere();
        if (now == nullptr || now->adaptive)
            return std::nullopt;
        return static_cast<uint32_t>(now->multiplier);
    }

    std::optional<AdaptiveTarget> ImGuiOverlay::makoAdaptiveHere()
    {
        const MakoSettings* now = makoGeneratingHere();
        if (now == nullptr || !now->adaptive)
            return std::nullopt;
        return AdaptiveTarget{now->targetFps, now->adaptiveTargetRefreshRate && underGamescope(), static_cast<uint32_t>(now->adaptiveMaxMultiplier)};
    }

    MakoSettings ImGuiOverlay::makoSettingsAtStart() const
    {
        return makoAtStart ? makoSettingsFor(*makoAtStart, powerSupplies.source) : MakoSettings{};
    }

    std::vector<std::string> ImGuiOverlay::makoWaiting(const MakoProfile* inUse)
    {
        std::vector<std::string> waiting;
        if (inUse)
            waiting = makoNextLaunchChanges(makoSettingsFor(*inUse, powerSupplies.source), makoSettingsAtStart());
        if (makoFile && makoGlobalAtStart)
        {
            if (makoFile->global.allowHalfPrecision != makoGlobalAtStart->allowHalfPrecision)
                waiting.push_back("Half precision");
            if (makoFile->global.dll != makoGlobalAtStart->dll)
                waiting.push_back("Lossless.dll");
        }
        return waiting;
    }

    std::optional<MakoGlobal> ImGuiOverlay::makoGlobalWithDll(const MakoSettings& written)
    {
        if (!needsLosslessDll(written))
            return std::nullopt;
        if (!makoDll)
            makoDll = makoDllStatus(makoFile->global);
        if (makoDll->found || !makoDll->inSteamLibrary)
            return std::nullopt;
        MakoGlobal global = makoFile->global;
        global.dll        = *makoDll->inSteamLibrary;
        return global;
    }

    void ImGuiOverlay::switchMako(bool MakoSettings::*setting, bool on)
    {
        if (!readMakoOnce())
            return;
        if (const MakoProfile* inUse = makoProfileInUse())
        {
            const MakoProfile changed = makoProfileSwitched(*inUse, powerSupplies.source, setting, on);
            saveMako(on ? makoGlobalWithDll(makoSettingsFor(changed, powerSupplies.source)) : std::nullopt, changed);
            return;
        }
        if (!on)
            return;
        std::vector<std::string> games = ConfigSerializer::profileUsers()[activeProfileName];
        if (std::find(games.begin(), games.end(), activeGameName) == games.end())
            games.push_back(activeGameName);
        MakoProfile made       = newMakoProfileFor(activeProfileName, games);
        made.settings.*setting = true;
        saveMako(makoGlobalWithDll(made.settings), made);
        if (makoProblem.empty())
            frameGenerationAssignmentsChanged("");
    }

    std::optional<std::string> ImGuiOverlay::makoGpuElsewhere()
    {
        const MakoProfile* inUse = readMakoOnce() ? makoProfileInUse() : nullptr;
        if (!inUse)
            return std::nullopt;
        VkPhysicalDeviceProperties properties{};
        pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &properties);
        MakoGpu game{properties.deviceName, properties.vendorID, properties.deviceID, std::nullopt};
        for (const OverlayGpu& gpu : state.gpus)
            if (const std::optional<GpuId> id = parseGpuId(gpu.id); gpu.number == gameGpu && id)
                game.pci = id->pci;
        std::vector<const MakoSettings*> sets = {&inUse->settings};
        if (inUse->power)
            sets.insert(sets.end(), {&inUse->power->handheld, &inUse->power->docked});
        for (const MakoSettings* set : sets)
            if (set->gpu && makoGpuNamesAnother(*set->gpu, game))
                return "MAKO's file names " + *set->gpu;
        return std::nullopt;
    }

    void ImGuiOverlay::useGamesGpuInMako()
    {
        const MakoProfile* inUse = makoProfileInUse();
        if (!inUse)
            return;
        MakoProfile changed  = *inUse;
        changed.settings.gpu = std::nullopt;
        if (changed.power)
        {
            changed.power->handheld.gpu = std::nullopt;
            changed.power->docked.gpu   = std::nullopt;
        }
        saveMako(std::nullopt, changed);
    }

    void ImGuiOverlay::renderMakoBody(bool flipped, bool on, const std::vector<std::string>& waiting)
    {
        const OverlayPalette& palette = activePalette();
        const MakoProfile*    inUse   = makoProfileInUse();
        const FrameGenerationState fg = frameGenerationState();
        const bool            live    = fg.live;
        const char*           later   = fg.loadsNextLaunch ? "from the next launch" : "needs vkdaedalus-run";
        const std::string     shown   = inUse ? inUse->name : frameGenerationProfileNameOf(activeProfileName);
        if (makoLayerLoaded() && makoProfileHere && *makoProfileHere != shown)
            ui::noteBox(("Running " + *makoProfileHere + "; " + shown + " from the next launch").c_str(), palette.warn);
        ui::divider();
        if (flipped)
        {
            ui::endScrollArea();
            switchMako(&MakoSettings::frameGenerationEnabled, on);
            return;
        }
        if (!inUse)
        {
            renderMakoGlobals();
            if (!makoProblem.empty())
                ui::errorText(("Not changed: " + makoProblem).c_str());
            ui::endScrollArea();
            ui::footer();
            if (activeProfileName.empty())
                ui::note("Off");
            else
                ui::notef("Off for profile %s", activeProfileName.c_str());
            return;
        }

        MakoProfile               tuned   = makoDraft ? *makoDraft : *inUse;
        const MakoPowerSource     edited  = makoEditedSet(tuned);
        MakoSettings&             set     = settingsOf(tuned, edited);
        if (!set.frameGenerationProvisioned)
            ui::noteBox("Ready at start is off, so MAKO sets up no frame generation when the game starts: On works from a launch with it on", palette.warn);
        bool                      changed = false;
        bool                      held    = false;
        const bool                recovery = set.dynamicCadenceRecovery;
        const auto                pending  = [&](const char* label) { return std::find(waiting.begin(), waiting.end(), std::string(label)) != waiting.end(); };
        const auto                slide    = [&](const char* label, const char* id, auto& value, auto lowest, auto highest, const std::string& valueText, const char* tip) {
            ui::panelRow();
            ui::sliderRowBegin(label, valueText.c_str());
            if constexpr (std::is_floating_point_v<std::remove_reference_t<decltype(value)>>)
            {
                float edit = static_cast<float>(value);
                if (ui::sliderFloat(id, &edit, static_cast<float>(lowest), static_cast<float>(highest), "%.2f", ImGuiSliderFlags_AlwaysClamp))
                    value = std::round(edit * 100.0f) / 100.0f;
            }
            else
            {
                int edit = static_cast<int>(value);
                if (ui::sliderInt(id, &edit, static_cast<int>(lowest), static_cast<int>(highest), ImGuiSliderFlags_AlwaysClamp))
                    value = static_cast<std::remove_reference_t<decltype(value)>>(edit);
            }
            held    = held || ImGui::IsItemActive();
            changed = ImGui::IsItemDeactivatedAfterEdit() || changed;
            ImGui::SetItemTooltip("%s", tip);
            ui::sliderRowEnd(valueText.c_str());
        };
        const auto flag = [&](const char* label, bool& value, const char* tip, bool nextLaunch) {
            ui::panelRow();
            changed = ui::checkbox(label, &value) || changed;
            ImGui::SetItemTooltip("%s", tip);
            if (nextLaunch)
                restartMarkAtRight(label, pending(label));
        };
        const auto greyed = [&](bool off, const char* why, const auto& draw) {
            ImGui::BeginDisabled(off);
            draw();
            ImGui::EndDisabled();
            if (off)
                whyGreyed(why);
        };
        const auto none = std::function<std::string(std::string_view)>();
        char       text[48];

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##mako", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (table)
        {
            ImGui::TableNextColumn();
            const std::string liveTip = "Kept in MAKO's profile " + tuned.name + ", which every game running the vkDaedalus profile " + activeProfileName
                                        + " shares. Applies at once" + (live ? "." : std::string(" once MAKO runs this profile, ") + later + ".");
            caption("Live", live ? "profile" : std::string("profile, ") + later, liveTip.c_str());
            ui::beginPanel();
            const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
            ui::panelRow();
            std::string mode = set.adaptive ? "adaptive" : "fixed";
            if (pickFrom("Mode", makoModes, mode, column, none))
            {
                set.adaptive = mode == "adaptive";
                changed      = true;
            }
            ImGui::SetItemTooltip("Fixed shows a set number of frames per real frame; Adaptive aims at a target frame rate");
            if (!set.adaptive)
                slide("Multiplier", "##Multiplier", set.multiplier, MakoLimits::lowestMultiplier, MakoLimits::highestMultiplier, std::to_string(set.multiplier) + "x",
                      "Frames shown per real frame: 2x doubles them");
            else
            {
                const MakoGreyedWhy greyedWhy = makoGreyedWhy(set, underGamescope());
                greyed(!greyedWhy.target.empty(), greyedWhy.target.c_str(), [&] {
                    slide("Target", "##Target", set.targetFps, MakoLimits::lowestTargetFps, MakoLimits::highestTargetFps, std::to_string(set.targetFps) + " FPS",
                          "The frame rate MAKO aims to show");
                });
                const std::optional<AdaptiveTarget> running = frameGenerationAdaptiveHere();
                const FrameStats                    game    = frameStats(latency.lastSecondMs);
                if (running && game.count > 0 && (!tuned.power || edited == powerSupplies.source) && !effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace))
                    if (const std::optional<std::string> ceiling = adaptiveCeilingWords(game.fps, *running))
                    {
                        ui::note(ceiling->c_str());
                        ImGui::SetItemTooltip("The game's own rate, measured, times Max multiplier, the most frames MAKO shows per real frame.\n"
                                              "Below the target of %u FPS, this ceiling decides how many frames MAKO shows.",
                                              running->fps);
                    }
                greyed(!greyedWhy.displayRefresh.empty(), greyedWhy.displayRefresh.c_str(), [&] {
                    flag("Display refresh", set.adaptiveTargetRefreshRate, "Use the display's refresh rate as the target, following it when it changes", false);
                });
                slide("Max multiplier", "##Max multiplier", set.adaptiveMaxMultiplier, MakoLimits::lowestAdaptiveMaxMultiplier, MakoLimits::highestAdaptiveMaxMultiplier,
                      std::to_string(set.adaptiveMaxMultiplier) + "x", "The most frames MAKO shows per real frame");
                ui::panelRow();
                greyed(recovery, "Auto while Cadence recovery is on", [&] {
                    if (pickFrom("Real frames", makoRealFramePriorities, set.adaptiveFractionalRealFramePriority, column, none))
                        changed = true;
                });
                flag("Stable cadence", set.adaptiveStableCadence, "Keeps an even rhythm of real and generated frames", false);
            }
            std::snprintf(text, sizeof(text), "%.2f", set.flowScale);
            slide("Flow scale", "##Flow scale", set.flowScale, MakoLimits::lowestFlowScale, MakoLimits::highestFlowScale, text, "Lower is faster and looks worse");
            ui::panelRow();
            greyed(set.frameGenerationModel != "mako-fg", "MAKO FG only", [&] {
                if (pickFrom("MAKO FG mode", makoFgModes, set.makoFgMode, column, none))
                    changed = true;
            });
            ui::panelRow();
            const std::string losslessMissing = makoModelMissing("lossless-scaling");
            greyed(set.frameGenerationModel != "lossless-scaling" || !losslessMissing.empty(), losslessMissing.empty() ? "MAKO LSFG only" : losslessMissing.c_str(), [&] {
                changed = ui::checkbox("Performance mode", &set.performanceMode) || changed;
                ImGui::SetItemTooltip("Much faster, slightly lower quality");
            });
            ui::endPanel();

            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
            caption("Pacing", "profile", liveTip.c_str());
            ui::beginPanel();
            greyed(recovery, "Off while Cadence recovery is on", [&] {
                slide("Base FPS cap", "##Base FPS cap", set.baseFpsCap, 0u, MakoLimits::highestBaseFpsCap,
                      set.baseFpsCap == 0 ? "Off" : std::to_string(set.baseFpsCap), "Caps the game's own frames before frame generation; 0 is off");
            });
            ui::panelRow();
            greyed(recovery, "Off while Cadence recovery is on", [&] {
                changed = ui::checkbox("Auto base cap", &set.adaptiveAutoBaseFpsCap) || changed;
                ImGui::SetItemTooltip("Caps the game's own frames at half the target");
            });
            slide("Refresh threshold", "##Refresh threshold", set.frameGenerationRefreshThreshold, 0u, MakoLimits::highestRefreshThreshold,
                  set.frameGenerationRefreshThreshold == 0 ? "Off" : std::to_string(set.frameGenerationRefreshThreshold) + " Hz",
                  "The highest display refresh rate at which frame generation stays paused; 0 is off");
            flag("Cadence recovery", set.dynamicCadenceRecovery, "Checks the pacing now and then, for games that change their frame rate; turns the base caps off", false);
            std::snprintf(text, sizeof(text), "%.2f s", set.dynamicCadenceProbeIntervalSeconds);
            slide("Probe interval", "##Probe interval", set.dynamicCadenceProbeIntervalSeconds, MakoLimits::lowestProbeIntervalSeconds,
                  MakoLimits::highestProbeIntervalSeconds, text, "How often Cadence recovery checks the pacing");
            VkPhysicalDeviceProperties properties{};
            pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &properties);
            if (properties.vendorID == 0x1002)
            {
                ui::panelRow();
                std::string headroom = std::to_string(set.gpuHeadroomPercent);
                greyed(recovery, "Off while Cadence recovery is on", [&] {
                    if (pickFrom("GPU headroom", makoHeadroomPercents, headroom, column, none))
                    {
                        set.gpuHeadroomPercent = static_cast<uint32_t>(std::stoul(headroom));
                        changed                = true;
                    }
                });
            }
            ui::endPanel();
            const bool hdr       = swapchainColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            const bool gamescope = underGamescope();
            if (hdr || gamescope)
            {
                ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
                caption("Output", "profile", liveTip.c_str());
                ui::beginPanel();
                if (hdr)
                {
                    ui::panelRow();
                    changed = pickFrom("HDR quality", makoHdrQualities, set.hdrQuality, column, none) || changed;
                }
                if (gamescope)
                {
                    ui::panelRow();
                    greyed(true, "MAKO applies it through a helper that only MAKO's own launchers start, and vkDaedalus runs without them", [&] {
                        changed = pickFrom("Gamescope VRR", makoGamescopeVrrModes, set.gamescopeVrrMode, column, none) || changed;
                    });
                }
                ui::endPanel();
            }

            ui::nextColumn();
            caption("Next launch", "profile", "Saved now and read when a game on this profile starts.");
            ui::beginPanel();
            const float nextColumn = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
            flag("Ready at start", set.frameGenerationProvisioned, "MAKO sets up frame generation when the game starts; needed to switch it on while the game runs", true);
            flag("Ultra performance", set.ultraPerformance, "Less GPU and memory use for low-power devices, at some cost in quality", true);
            bool after = effectsAfterFrameGenerationSaved();
            if (!activeProfilePath.empty())
            {
                ui::panelRow();
                if (ui::checkbox("Effects after frame generation", &after))
                {
                    effectsAfterFrameGeneration = after;
                    autoSaveProfile();
                }
                ImGui::SetItemTooltip("Read when the game starts. Needs vkdaedalus-run. Saved in the vkDaedalus profile.");
                restartMarkAtRight("Effects after frame generation", pending("Effects after frame generation"));
            }
            flag("Keep swapchain image count", set.swapchainImageCountCompatibility, "Only for a game that fails with frame generation", true);
            flag("Mailbox compatibility", set.mailboxPresentationCompatibility, "Presents through mailbox: may fix a game that hangs with frame generation", true);
            ui::endPanel();
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
            renderMakoGlobals();
            if (powerSupplies.battery || tuned.power)
            {
                ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
                caption("Power", "profile",
                        "MAKO runs its docked set while the machine is plugged in and its handheld set on battery, and switches as the power source changes.");
                ui::beginPanel();
                ui::panelRow();
                bool separate = tuned.power.has_value();
                if (ui::checkbox("Separate settings on battery", &separate))
                {
                    tuned   = makoProfileWithPowerSets(tuned, separate, powerSupplies.source);
                    makoTab = std::nullopt;
                    changed = true;
                }
                ImGui::SetItemTooltip("%s", separate ? "Off keeps the set MAKO runs now as the profile's settings" : "On starts both sets from these settings");
                if (tuned.power)
                {
                    ui::panelRow();
                    ui::label("Settings for");
                    ui::atColumn(nextColumn);
                    for (const auto& [source, shownTab, makoName] :
                         {std::tuple{MakoPowerSource::Docked, "Plugged in##Power", "docked"}, std::tuple{MakoPowerSource::Handheld, "On battery##Power", "handheld"}})
                    {
                        if (source == MakoPowerSource::Handheld)
                            ImGui::SameLine(0.0f, ui::px(ui::Size::gap));
                        UiMap::nameNextItem(shownTab);
                        if (ui::tab(shownTab, makoEditedSet(tuned) == source))
                            makoTab = source;
                        const bool now = powerSupplies.source == source;
                        if (now)
                            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ImGui::GetItemRectMax().x - ui::px(6.0f), ImGui::GetItemRectMin().y + ui::px(6.0f)),
                                                                        ui::px(3.0f), ImGui::GetColorU32(palette.ok));
                        ImGui::SetItemTooltip("MAKO's %s set%s", makoName, now ? ": MAKO runs it now" : "");
                    }
                }
                ui::endPanel();
            }
            ImGui::EndTable();
        }
        renderFrameOrderNote();
        if (held)
            makoDraft = tuned;
        else
            makoDraft.reset();
        if (changed)
            saveMako(std::nullopt, tuned);
        if (!makoProblem.empty())
            ui::errorText(("Not changed: " + makoProblem).c_str());
        ui::endScrollArea();
        ui::footer();
        ui::notef("MAKO profile %s, %zu %s%s", tuned.name.c_str(), tuned.activeIn.size(), tuned.activeIn.size() == 1 ? "game" : "games", setName(edited));
    }

    void ImGuiOverlay::refreshMakoStatus()
    {
        if (!makoStatusPoll.due(std::chrono::steady_clock::now()) || !makoLayerLoaded())
            return;
        if (!startTicks)
            startTicks = thisProcessStartTicks();
        makoStatus = readMakoStatus(makoStatusFolder(makoConfigPath()), static_cast<uint64_t>(getpid()), *startTicks, currentWidth, currentHeight);
    }

    MakoScalingShown ImGuiOverlay::makoScaling()
    {
        const MakoProfile* inUse = readMakoOnce() ? makoProfileInUse() : nullptr;
        return makoScalingShown(inUse && makoSettingsFor(*inUse, powerSupplies.source).scalingEnabled, makoLiveHere(), makoStatus);
    }

    void ImGuiOverlay::renderMakoGlobals()
    {
        const OverlayPalette& palette = activePalette();
        const char*           tip     = "MAKO's own settings, the same for every game. Read when a game starts.";
        ImGui::AlignTextToFramePadding();
        caption("MAKO", "all games", tip);
        if (!readMakoOnce())
        {
            ui::errorText(("MAKO refuses its file " + makoConfigPath() + ": " + makoFileProblem).c_str());
            return;
        }
        MakoGlobal global = makoFile->global;
        if (!makoGlobalAtStart)
            makoGlobalAtStart = global;
        if (!makoDll)
            makoDll = makoDllStatus(global);
        bool changed = false;
        ui::beginPanel();
        const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
        ui::panelRow();
        ui::label("Lossless.dll");
        ImGui::SetItemTooltip("Lossless Scaling's library, which MAKO needs only for the Lossless Scaling model and LS1");
        ui::atColumn(column);
        ImGui::AlignTextToFramePadding();
        const char* action = nullptr;
        if (makoDll->found)
        {
            ImGui::TextColored(palette.ok, "Found");
            ImGui::SetItemTooltip("%s", makoDll->path->c_str());
        }
        else if (makoDll->inSteamLibrary)
        {
            ImGui::TextColored(palette.warn, "In a Steam library");
            ImGui::SetItemTooltip("%s, in a Steam library MAKO does not search: Set writes it into MAKO's file", makoDll->inSteamLibrary->c_str());
            action = "Set";
        }
        else
        {
            ImGui::TextColored(palette.muted, "Not found");
            ImGui::SetItemTooltip("Install Lossless Scaling in Steam");
            action = "Look again";
        }
        if (action)
        {
            ui::alignRight(ui::buttonWidth(action, ui::Kind::Secondary, true) + ui::px(restartMarkGap + restartMarkSize));
            UiMap::nameNextItem(action);
            if (ui::lineButton(action))
            {
                if (makoDll->inSteamLibrary)
                {
                    global.dll = *makoDll->inSteamLibrary;
                    changed    = true;
                }
                else
                    makoDll.reset();
            }
        }
        if (action)
            restartMark("Lossless.dll", makoGlobalAtStart && global.dll != makoGlobalAtStart->dll, palette);
        else
            restartMarkAtRight("Lossless.dll", makoGlobalAtStart && global.dll != makoGlobalAtStart->dll);
        ui::panelRow();
        ui::label("Half precision");
        ImGui::SetItemTooltip("Read when a game starts. MAKO: FP16 for the models that support it and MAKO Scaler; LS1 always uses FP32.");
        ui::atColumn(column);
        changed = ui::checkbox("##Half precision", &global.allowHalfPrecision) || changed;
        ImGui::SetItemTooltip("Read when a game starts. MAKO: FP16 for the models that support it and MAKO Scaler; LS1 always uses FP32.");
        restartMarkAtRight("Half precision", makoGlobalAtStart && global.allowHalfPrecision != makoGlobalAtStart->allowHalfPrecision);
        ui::endPanel();
        if (changed)
            saveMako(global, std::nullopt);
    }

    void ImGuiOverlay::renderScalingView()
    {
        const OverlayPalette& palette = activePalette();
        refreshMakoFile(false);
        bool               notInstalled = false;
        const std::string  blocker      = makoBlocker(notInstalled);
        const MakoProfile* inUse        = blocker.empty() ? makoProfileInUse() : nullptr;
        MakoProfile        tuned        = inUse ? (makoDraft ? *makoDraft : *inUse) : MakoProfile{};
        const MakoPowerSource edited    = makoEditedSet(tuned);
        MakoSettings&         set       = settingsOf(tuned, edited);
        const MakoSettings&   running   = makoSettingsFor(tuned, powerSupplies.source);
        bool                  on        = inUse && running.scalingEnabled;
        const MakoScalingShown scaling  = blocker.empty() ? makoScaling() : MakoScalingShown{};
        const bool            scales    = scaling.run == MakoScalingRun::Scaling;
        const bool            unsure    = scaling.run == MakoScalingRun::NotScaling || scaling.run == MakoScalingRun::NoStatus;
        const bool            pending   = inUse && makoSettingsFor(*inUse, powerSupplies.source).scalingEnabled != makoSettingsAtStart().scalingEnabled;
        const std::string     amount    = shownChoice(makoScalingMethods, running.scalingMethod) + " " + factorText(running.scalingFactor);
        const bool            loads     = loadedAtNextLaunch(FrameGenerationLayer::Mako, startedWithVkdaedalusRun(), makoEnabledInEnvironment());

        ui::heading("Scaling");
        if (inUse)
            ImGui::SetItemTooltip("Kept in MAKO's profile '%s', used in: %s", inUse->name.c_str(), joined(inUse->activeIn).c_str());
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!blocker.empty())
            ImGui::TextColored(palette.faint, "Unavailable");
        else if (scales)
        {
            ImGui::TextColored(palette.ok, "%s %s, on", shownChoice(makoScalingMethods, scaling.method).c_str(), scaling.detail.c_str());
            ImGui::SetItemTooltip("From MAKO's status file for this swapchain: the size the game draws at and the size MAKO presents");
        }
        else if (unsure)
        {
            ImGui::TextColored(palette.warn, "%s", scaling.run == MakoScalingRun::NotScaling ? "Not scaling" : "No status file");
            ImGui::SetItemTooltip("%s", scaling.detail.c_str());
        }
        else if (scaling.run == MakoScalingRun::Off)
            ImGui::TextColored(palette.muted, "Off");
        else
        {
            ImGui::TextColored(palette.warn, "%s, %s", amount.c_str(), loads ? "next launch" : "needs vkdaedalus-run");
            ImGui::SetItemTooltip("%s", loads ? "MAKO starts scaling when the game starts" : std::string(makoNeedsVkdaedalusRun).c_str());
        }
        ui::alignRight(ui::px(ui::Size::box) + ui::px(ui::Size::gap) + ImGui::CalcTextSize("On").x + ui::px(restartMarkGap + restartMarkSize));
        ImGui::BeginDisabled(!blocker.empty());
        const bool flipped = ui::checkbox("On###scaling", &on);
        ImGui::EndDisabled();
        if (unsure)
            ImGui::SetItemTooltip("%s", scaling.detail.c_str());
        else
            ImGui::SetItemTooltip("Read when the game starts: MAKO scales the game's frames up to the display. Saved in MAKO's profile.");
        restartMark("Scaling", pending, palette);
        if (unsure)
            whyGreyed(scaling.detail);

        if (!blocker.empty())
        {
            ui::divider();
            ui::errorText(blocker.c_str());
            ui::footer();
            return;
        }
        ui::beginScrollArea("##scalingbody");
        ui::divider();
        if (flipped)
        {
            ui::endScrollArea();
            switchMako(&MakoSettings::scalingEnabled, on);
            return;
        }
        if (!inUse)
        {
            ui::endScrollArea();
            ui::footer();
            ui::notef("Off for profile %s", activeProfileName.c_str());
            return;
        }

        bool                      changed = false;
        bool                      held    = false;
        std::optional<MakoGlobal> global;
        const auto                slide = [&](const char* label, const char* id, float& value, float lowest, float highest, const char* format, const char* tip) {
            char shown[24];
            std::snprintf(shown, sizeof(shown), format, value);
            ui::panelRow();
            ui::sliderRowBegin(label, shown);
            if (ui::sliderFloat(id, &value, lowest, highest, "%.2f", ImGuiSliderFlags_AlwaysClamp))
                value = std::round(value * 100.0f) / 100.0f;
            held    = held || ImGui::IsItemActive();
            changed = ImGui::IsItemDeactivatedAfterEdit() || changed;
            ImGui::SetItemTooltip("%s", tip);
            ui::sliderRowEnd(shown);
        };
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##scaling", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (table)
        {
            ImGui::TableNextColumn();
            const bool live = scales;
            caption("Live", live ? "profile" : unsure ? "profile, once MAKO scales" : loads ? "profile, from the next launch" : "profile, needs vkdaedalus-run",
                    ("Kept in MAKO's profile " + tuned.name + ". Applies at once while MAKO scales the game.").c_str());
            ui::beginPanel();
            const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
            ui::panelRow();
            const auto lossless = [&](std::string_view method) { return method.starts_with("ls1") ? makoModelMissing("lossless-scaling") : std::string(); };
            if (pickFrom("Method", makoScalingMethods, set.scalingMethod, column, lossless))
            {
                changed = true;
                global  = makoGlobalWithDll(set);
            }
            if (const std::string why = lossless("ls1"); !why.empty())
                whyGreyed("LS1: " + why);
            slide("Sharpness", "##Sharpness", set.scalingSharpness, MakoLimits::lowestScalingSharpness, MakoLimits::highestScalingSharpness, "%.2f",
                  "Higher is sharper, and can show grain and halos at strong edges");
            ui::endPanel();

            ui::nextColumn();
            caption("Swapchain", "profile", "Saved now and used when the game next makes its swapchain, as after a resize or a mode change.");
            ui::beginPanel();
            slide("Factor", "##Factor", set.scalingFactor, MakoLimits::lowestScalingFactor, MakoLimits::highestScalingFactor, "%.2fx",
                  "2x shows the game's frame at twice its width and height");
            ui::panelRow();
            changed = ui::checkbox("Supersampling", &set.scalingSupersampling) || changed;
            ImGui::SetItemTooltip("Lets MAKO exceed a gamescope output limit to scale down at higher quality, at more GPU and memory use");
            ui::endPanel();
            ImGui::EndTable();
        }
        if (held)
            makoDraft = tuned;
        else
            makoDraft.reset();
        if (changed)
            saveMako(global, tuned);
        if (!makoProblem.empty())
            ui::errorText(("Not changed: " + makoProblem).c_str());
        ui::endScrollArea();
        ui::footer();
        ui::notef("MAKO profile %s, %zu %s%s", tuned.name.c_str(), tuned.activeIn.size(), tuned.activeIn.size() == 1 ? "game" : "games", setName(edited));
    }
}
