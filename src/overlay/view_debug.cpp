#include "imgui_overlay.hpp"

#include <ctime>
#include <filesystem>
#include <sstream>
#include "frame_generation.hpp"
#include "frame_generation_place.hpp"
#include "latency_stats.hpp"
#include "logical_device.hpp"
#include "overlay/report.hpp"
#include "shader_cache.hpp"
#include "overlay/overlay_style.hpp"
#include "overlay/widgets.hpp"
#include "effects/effect_registry.hpp"
#include "config_serializer.hpp"
#include "settings_manager.hpp"
#include "logger.hpp"
#include "overlay/log_search.hpp"

#include <cstring>
#include <fstream>

#include "imgui.h"

namespace vkBasalt
{
    namespace
    {
        std::string usageNames(VkImageUsageFlags usage)
        {
            static constexpr std::pair<VkImageUsageFlagBits, const char*> names[] = {
                {VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "transfer source"}, {VK_IMAGE_USAGE_TRANSFER_DST_BIT, "transfer target"},
                {VK_IMAGE_USAGE_SAMPLED_BIT, "sampled"},              {VK_IMAGE_USAGE_STORAGE_BIT, "storage"},
                {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "colour attachment"}, {VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, "input attachment"}};
            std::string out;
            for (const auto& [bit, name] : names)
                if (usage & bit)
                    out += (out.empty() ? "" : ", ") + std::string(name);
            return out.empty() ? "none" : out;
        }

    }

    void ImGuiOverlay::exportReport()
    {
        const auto tail = [](const std::string& path, std::streamoff most) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
                return std::string();
            const std::streamoff size = file.tellg();
            file.seekg(size > most ? size - most : 0);
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        };
        const char*       home      = std::getenv("HOME");
        const std::string homeDir   = home ? home : "";
        const char*       config    = std::getenv("XDG_CONFIG_HOME");
        const std::string userDirs  = tail((config && *config ? std::string(config) : homeDir + "/.config") + "/user-dirs.dirs", 1 << 16);
        const std::string folder    = downloadFolderOf(userDirs, homeDir);
        const std::time_t now       = std::time(nullptr);
        char              stamp[32] = "";
        char              when[32]  = "";
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

        ReportFacts facts;
        facts.when          = when;
        facts.game          = activeGameName;
        const std::optional<std::string> fixed = fixedConfigFile();
        facts.profile = fixed ? "config file " + *fixed : activeProfileName.empty() ? DEFAULT_PROFILE_NAME : activeProfileName;
        VkPhysicalDeviceProperties properties{};
        pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &properties);
        facts.gpus.push_back(std::string("the game's: ") + properties.deviceName + ", Vulkan " + std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "." +
                             std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) + "." + std::to_string(VK_API_VERSION_PATCH(properties.apiVersion)) +
                             ", driver version " + std::to_string(properties.driverVersion));
        for (const OverlayGpu& gpu : state.gpus)
            facts.gpus.push_back("GPU " + std::to_string(gpu.number) + ": " + gpu.name + (gpu.status.empty() ? "" : ", " + gpu.status));
        facts.layersBelow          = pLogicalDevice->layersBelow;
        facts.frameGenerationPlace = frameGenerationPlaceWords(pLogicalDevice->frameGenerationPlace);
        const std::streamoff most  = 2 << 20;
        facts.files.emplace_back(fixed ? facts.profile : "profile " + facts.profile, tail(fixed ? *fixed : ConfigSerializer::getProfilePath(facts.profile), most));
        facts.files.emplace_back("game " + activeGameName, tail(ConfigSerializer::getGamePath(activeGameName), most));
        facts.files.emplace_back("vkdaedalus.log, the last 2 MiB", tail(Logger::stateLogPath(), most));
        const LsfgRead lsfg = readLsfgFile(lsfgConfigPath());
        if (lsfg.file && lsfg.file->global.logFile)
            facts.files.emplace_back("lsfg-vk.log, the last 2 MiB", tail(*lsfg.file->global.logFile, most));

        std::error_code problem;
        std::filesystem::create_directories(folder, problem);
        const std::string path = folder + "/vkdaedalus-report-" + (activeGameName.empty() ? std::string("unknown") : activeGameName) + "-" + stamp + ".txt";
        std::ofstream     out(path, std::ios::binary);
        out << diagnosticReport(facts, settingsManager.getSafeAntiCheat());
        out.close();
        if (problem || out.fail())
        {
            exportedReport.clear();
            Logger::err("the report could not be written to " + path + (problem ? ": " + problem.message() : ""));
            return;
        }
        exportedReport = path;
        Logger::info("report exported to " + path);
    }

    void ImGuiOverlay::renderDebugView()
    {
        const OverlayPalette& palette = activePalette();
        if (!Logger::isHistoryEnabled())
            Logger::setHistoryEnabled(true);

        ui::heading("Debug");
        ImGui::SameLine(0.0f, ui::px(ui::Size::groupGap));
        if (ui::tab("Effects", debugWindowTab == 0))
            debugWindowTab = 0;
        ImGui::SameLine();
        if (ui::tab("Frame path", debugWindowTab == 2))
            debugWindowTab = 2;
        ImGui::SameLine();
        if (ui::tab("Paths", debugWindowTab == 3))
            debugWindowTab = 3;
        ImGui::SameLine();
        if (ui::tab("Log", debugWindowTab == 1))
            debugWindowTab = 1;
        const float levelWidth = ui::px(120.0f);
        ui::alignRight(ImGui::CalcTextSize("Log level").x + ImGui::GetStyle().ItemSpacing.x + levelWidth);
        ui::label("Log level");
        ImGui::SameLine();
        const bool fromEnvironment = Logger::levelSetByEnvironment();
        ImGui::BeginDisabled(fromEnvironment);
        if (ui::beginCombo("##logLevel", settingsManager.getLogLevel().c_str(), levelWidth))
        {
            for (const char* level : {"trace", "debug", "info", "warn", "error", "none"})
                if (ui::option(level, settingsManager.getLogLevel() == level))
                {
                    settingsManager.setLogLevel(level);
                    Logger::useSettingsLevel(level);
                    saveOverlaySettings();
                }
            ui::endCombo();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(fromEnvironment ? "VKDAEDALUS_LOG_LEVEL sets the level for this run, over this setting."
                                              : "How much the layer logs, from now on and at every launch.\nVKDAEDALUS_LOG_LEVEL overrides it for one run.");

        if (debugWindowTab == 3)
        {
            ui::beginScrollArea("##debugpaths");
            const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
            const auto  path   = [&](const char* name, const std::string& value, const char* tooltip) {
                ui::label(name);
                ImGui::SetItemTooltip("%s", tooltip);
                ui::atColumn(column);
                ImGui::TextUnformatted(ui::elideStart(value, ImGui::GetContentRegionAvail().x).c_str());
                ImGui::SetItemTooltip("%s", value.c_str());
            };
            const char*       named  = std::getenv("VKDAEDALUS_LOG_FILE");
            const std::string folder = compiledEffectsFolder();
            path("Program", activeGameName, "The name vkDaedalus gives this game, which its profile and its own file follow");
            if (const std::optional<std::string> fixed = fixedConfigFile())
                path("Config file", *fixed, "VKDAEDALUS_CONFIG_FILE names it: this run reads it, and the overlay saves nothing");
            else
                path("Profile", activeProfilePath.empty() ? std::string("none, vkDaedalus is off for this game") : activeProfilePath, "The profile's file, which the overlay saves");
            path("Game", ConfigSerializer::getGamePath(activeGameName), "The game's own settings, Safe Anti-Cheat for now");
            path("Effects", ConfigSerializer::getBaseConfigDir() + "/vkdaedalus.conf", "Your effects and their settings for every program; the overlay never rewrites it");
            path("Settings", ConfigSerializer::getBaseConfigDir() + "/settings.conf", "The overlay's own settings");
            path("lsfg-vk", lsfgConfigPath(), "lsfg-vk's own file, which the Frame generation card edits");
            path("Log", named != nullptr && *named != '\0' ? std::string(named) : Logger::stateLogPath(), "Where this run's log goes; Export copies its end into the report");
            path("Compiled effects", folder.empty() ? std::string("in memory only, this build cannot be told apart from others") : folder,
                 "Compiled effects kept between runs; Clear compiled effects on the Shaders card empties it");
            ui::endScrollArea();
            ui::footer();
            return;
        }

        if (debugWindowTab == 2)
        {
            ui::beginScrollArea("##debugframepath");
            renderPipelineStrip();
            ui::divider();
            const float column = ImGui::GetCursorPosX() + ui::px(ui::Size::labelColumn);
            const auto  fact   = [&](const char* name, const std::string& value, const char* tooltip) {
                ui::label(name);
                ImGui::SetItemTooltip("%s", tooltip);
                ui::atColumn(column);
                ImGui::TextUnformatted(value.c_str());
            };
            std::string below;
            for (const std::string& layer : pLogicalDevice->layersBelow)
                below += (below.empty() ? "" : ", ") + layer;
            fact("Layers below", below.empty() ? "none, the driver comes next" : below,
                 "The libraries the game's calls pass through after vkDaedalus, nearest first; the loader fixes the order when the game starts");
            fact("Image use", usageNames(swapchainAppUsage), "What the game asked of its swapchain images");
            fact("vkDaedalus adds", swapchainUsage & ~swapchainAppUsage ? usageNames(swapchainUsage & ~swapchainAppUsage) : "nothing",
                 "What vkDaedalus added to draw the effects and the overlay on the game's images");
            fact("Views", swapchainMutable ? "UNORM and sRGB, the format is mutable" : "one format", "The views the effects read and write the game's images through");
            fact("Other GPUs", chainCrossings.empty() ? "none, every effect runs on GPU 1" : chainCrossings,
                 "Where the frame goes after the effects before it on GPU 1; each crossing is a copy through system memory");

            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
            ui::sectionLabel("Measured or estimated, in this run");
            const std::optional<uint32_t> multiplier = frameGenerationMultiplierHere();
            const bool                    after      = multiplier && effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);
            const FrameRates              rates      = frameRatesOf(0.0, multiplier, after);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(8.0f), ui::px(6.0f)));
            if (ImGui::BeginTable("##figures", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("figure", ImGuiTableColumnFlags_WidthFixed, ui::px(220.0f));
                ImGui::TableSetupColumn("taken", ImGuiTableColumnFlags_WidthStretch);
                const auto figure = [&](const char* name, ui::Mark mark) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(name);
                    ImGui::TableNextColumn();
                    if (mark == ui::Mark::Estimated)
                    {
                        ImGui::TextColored(palette.warn, "~");
                        ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
                    }
                    ImGui::TextColored(mark == ui::Mark::Absent ? palette.faint : palette.muted, "%s",
                                       mark == ui::Mark::Measured    ? "measured"
                                       : mark == ui::Mark::Estimated ? "estimated"
                                       : mark == ui::Mark::Sum       ? "worked out"
                                                                     : "not measured");
                };
                figure("FPS", rates.gameMeasured ? ui::Mark::Measured : ui::Mark::Estimated);
                if (multiplier)
                    figure("FPS shown", rates.shownMeasured ? ui::Mark::Measured : ui::Mark::Estimated);
                figure("Frame times", ui::Mark::Measured);
                figure("Effects on GPU 1", ui::Mark::Measured);
                if (latency.handsOff)
                {
                    figure("Round trip", ui::Mark::Measured);
                    figure("Its parts", ui::Mark::Measured);
                    figure("Waiting", ui::Mark::Sum);
                    figure("GPU 1's copies out and back", ui::Mark::Absent);
                    if (latency.rowsAfterReturn)
                        figure("Effects after the return", ui::Mark::Absent);
                }
                if (multiplier)
                    figure("lsfg-vk holds one frame", ui::Mark::Estimated);
                figure("Input to screen", ui::Mark::Absent);
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            ui::endScrollArea();
            ui::footer();
            return;
        }

        if (debugWindowTab == 0)
        {
            ui::beginScrollArea("##debugeffects");
            if (!pEffectRegistry)
                ui::note("No effects are loaded yet");
            else
            {
                const auto& effects = pEffectRegistry->getAllEffectsUnlocked();
                for (const auto& effect : effects)
                {
                    if (!ui::disclosure(effect.instanceName.c_str(), effect.instanceName.c_str(), palette.text, ImGui::GetContentRegionAvail().x,
                                        effect.type == EffectType::BuiltIn ? "built in" : "ReShade"))
                        continue;
                    ui::beginPanel();
                    ui::factRow("Type", effect.baseType.c_str());
                    ui::factRow("Ticked", effect.enabled ? "yes" : "no");
                    if (!effect.filePath.empty())
                    {
                        ui::factRow("Path", ui::elideStart(effect.filePath, ImGui::GetContentRegionAvail().x * 0.7f).c_str());
                        ImGui::SetItemTooltip("%s", effect.filePath.c_str());
                    }
                    if (effect.hasFailed())
                    {
                        ui::panelRow();
                        ui::errorText(effect.compileError.c_str());
                    }
                    ui::endPanel();
                    if (!effect.parameters.empty())
                    {
                        const std::string count = std::to_string(effect.parameters.size());
                        if (ui::disclosure("Parameters", "Parameters", palette.text, ImGui::GetContentRegionAvail().x, count.c_str()))
                        {
                            ui::beginPanel();
                            for (const auto& param : effect.parameters)
                            {
                                const auto  serialized = param->serialize();
                                std::string valueStr;
                                for (size_t i = 0; i < serialized.size(); i++)
                                {
                                    if (i > 0)
                                        valueStr += ", ";
                                    if (!serialized[i].first.empty())
                                        valueStr += serialized[i].first + "=";
                                    valueStr += serialized[i].second;
                                }
                                ui::factRow(param->name.c_str(), (valueStr + "  " + param->getTypeName()).c_str());
                            }
                            ui::endPanel();
                            ImGui::TreePop();
                        }
                    }
                    if (!effect.preprocessorDefs.empty())
                    {
                        const std::string count = std::to_string(effect.preprocessorDefs.size());
                        if (ui::disclosure("Preprocessor", "Definitions", palette.text, ImGui::GetContentRegionAvail().x, count.c_str()))
                        {
                            ui::beginPanel();
                            for (const auto& def : effect.preprocessorDefs)
                                ui::factRow(def.name.c_str(), (def.value + (def.value == def.defaultValue ? "" : "  default " + def.defaultValue)).c_str());
                            ui::endPanel();
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ui::endScrollArea();
            ui::footer();
            if (pEffectRegistry)
            {
                const size_t count = pEffectRegistry->getAllEffectsUnlocked().size();
                ui::notef("%zu %s in the registry", count, count == 1 ? "effect" : "effects");
            }
            return;
        }

        bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (windowFocused && !ImGui::IsAnyItemActive())
        {
            ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < io.InputQueueCharacters.Size; i++)
            {
                ImWchar c = io.InputQueueCharacters[i];
                if (isPrintableAscii(c))
                {
                    size_t len = strlen(debugLogSearch);
                    if (len < sizeof(debugLogSearch) - 1)
                    {
                        debugLogSearch[len]     = static_cast<char>(c);
                        debugLogSearch[len + 1] = '\0';
                    }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && debugLogSearch[0] != '\0')
            {
                size_t len = strlen(debugLogSearch);
                if (len > 0)
                    debugLogSearch[len - 1] = '\0';
            }
        }

        const bool hasSearch = debugLogSearch[0] != '\0';
        if (hasSearch)
        {
            ui::textField("##logsearch", debugLogSearch, sizeof(debugLogSearch), "Search", ImGui::GetContentRegionAvail().x - ui::px(ui::Size::inRow) - ImGui::GetStyle().ItemSpacing.x,
                          ImGuiInputTextFlags_AutoSelectAll);
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ui::px(ui::Size::control) - ui::px(ui::Size::inRow)) * 0.5f);
            if (ui::iconButton("x", Icon::Close, "Clear the search"))
                debugLogSearch[0] = '\0';
        }
        else
            ui::note("Type to search");

        ui::label("Show");
        for (const auto& [name, level] : {std::pair{"Trace", LogLevel::Trace}, std::pair{"Debug", LogLevel::Debug}, std::pair{"Info", LogLevel::Info},
                                           std::pair{"Warn", LogLevel::Warn}, std::pair{"Error", LogLevel::Error}})
        {
            ImGui::SameLine(0.0f, ui::px(ui::Size::groupGap));
            ui::checkbox(name, &logFilter(level));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::px(ui::Size::gap), ui::px(ui::Size::gap)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.well);
        ImGui::BeginChild("LogScrollRegion", ImVec2(0, ui::heightLeftAboveFooter()), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        auto history    = Logger::getHistory();
        auto levelColor = [&](LogLevel level) {
            switch (level)
            {
                case LogLevel::Trace: return palette.faint;
                case LogLevel::Debug: return palette.info;
                case LogLevel::Info: return palette.text;
                case LogLevel::Warn: return palette.warn;
                case LogLevel::Error:
                case LogLevel::None: break;
            }
            return palette.bad;
        };
        const std::string search = loweredSearch(debugLogSearch);
        size_t shown = 0;
        for (const auto& entry : history)
        {
            if (entry.level >= LogLevel::None || !logFilter(entry.level))
                continue;
            if (hasSearch && !matchesSearch(entry.message, search))
                continue;
            shown++;
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
            ImGui::TextUnformatted(("[" + std::string(Logger::levelName(entry.level)) + "] " + entry.message).c_str());
            ImGui::PopStyleColor();
        }
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ui::px(10.0f);
        if (atBottom && !hasSearch)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ui::footer();
        if (exportedReport.empty())
            ui::notef("%zu %s", shown, shown == 1 ? "line" : "lines");
        else
            ui::note(("Report saved to " + exportedReport).c_str());
        ui::alignRight(ui::buttonWidth("Clear") + ImGui::GetStyle().ItemSpacing.x + ui::buttonWidth("Export"));
        if (ui::button("Clear"))
        {
            const size_t dropped = history.size();
            Logger::clearHistory();
            Logger::info("the debug card's log was cleared: " + std::to_string(dropped) + " lines dropped");
        }
        ImGui::SameLine();
        if (ui::button("Export"))
            exportReport();
        ImGui::SetItemTooltip("Save one report to your download folder: both logs, the GPUs, the layer order and the profile");
    }

}
