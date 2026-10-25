#include "imgui_overlay.hpp"
#include "reshade_parser.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"
#include "overlay/widgets.hpp"
#include "settings_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <future>
#include <set>
#include <system_error>

#ifdef __linux__
#include <malloc.h>
#endif

#include "background_work.hpp"
#include "imgui.h"
#include "reshade_effect_files.hpp"

namespace vkBasalt
{
    void ImGuiOverlay::startShaderTest()
    {
        if (shaderTestRunning)
            return;

        if (!shaderMgrInitialized)
        {
            ShaderManagerConfig config = ConfigSerializer::loadShaderManagerConfig();
            shaderMgrParentDirs = config.parentDirectories;
            shaderMgrShaderPaths = config.discoveredShaderPaths;
            shaderMgrTexturePaths = config.discoveredTexturePaths;
            shaderMgrInitialized = true;
        }

        shaderTestQueue.clear();
        shaderTestResults.clear();
        shaderTestCurrentIndex = 0;
        shaderTestComplete = false;
        shaderTestDuplicateCount = 0;

        const std::vector<std::string> includePaths = ConfigSerializer::loadShaderManagerConfig().discoveredShaderPaths;

        std::set<std::string> seenCanonicalPaths;
        for (const auto& shaderPath : shaderMgrShaderPaths)
        {
            std::error_code ec;
            for (const auto& file : reshadeEffectFilesIn(shaderPath, ec))
            {
                std::error_code canonicalEc;
                const std::filesystem::path canonical = std::filesystem::canonical(file, canonicalEc);
                const std::string key = canonicalEc ? file.string() : canonical.string();
                if (seenCanonicalPaths.count(key))
                {
                    shaderTestDuplicateCount++;
                    continue;
                }
                seenCanonicalPaths.insert(key);
                shaderTestQueue.emplace_back(file.stem().string(), file.string());
            }
            if (ec)
                Logger::warn("shader test: cannot read " + shaderPath + ": " + ec.message());
        }

        if (shaderTestQueue.empty())
            return;

        shaderTestRun = std::make_shared<ShaderTestRun>();
        try
        {
            BackgroundWork& work = BackgroundWork::instance();
            shaderTestDone       = std::async(std::launch::async, [run = shaderTestRun, queue = shaderTestQueue, includePaths, &work] {
                const BackgroundWork::Running running(work);
                size_t                        tested = 0;
                for (const auto& [name, path] : queue)
                {
                    if (run->cancelled.load() || running.stopped())
                        return;
                    ShaderTestResult result = testShaderCompilation(name, path, includePaths);
                    {
                        std::lock_guard<std::mutex> lock(run->mutex);
                        run->finished.push_back(std::move(result));
                    }
                    // glibc keeps the compiler's freed heap; without the trim a full pack sweep can run out of memory
#ifdef __linux__
                    if (++tested % 25 == 0)
                        malloc_trim(0);
#endif
                }
#ifdef __linux__
                malloc_trim(0);
#endif
            });
        }
        catch (const std::system_error& e)
        {
            shaderTestRun.reset();
            Logger::err(std::string("no thread could start for the shader test, so it does not run: ") + e.what());
            return;
        }
        shaderTestRunning = true;
        Logger::info("Shader test started in the background: " + std::to_string(shaderTestQueue.size()) + " shaders");
    }

    void ImGuiOverlay::processShaderTest()
    {
        if (!shaderTestRunning || !shaderTestRun)
            return;

        const bool allTested = shaderTestDone.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        std::vector<ShaderTestResult> finished;
        {
            std::lock_guard<std::mutex> lock(shaderTestRun->mutex);
            finished.swap(shaderTestRun->finished);
        }
        for (ShaderTestResult& result : finished)
        {
            if (result.usesDepth)
                depthEffectPaths.insert(std::filesystem::path(result.filePath).lexically_normal().string());
            shaderTestResults.push_back(std::move(result));
        }
        shaderTestCurrentIndex = shaderTestResults.size();

        if (!allTested)
            return;
        try
        {
            shaderTestDone.get();
        }
        catch (const std::exception& e)
        {
            Logger::err(std::string("the shader test stopped early: ") + e.what());
        }
        shaderTestRun.reset();
        shaderTestRunning  = false;
        shaderTestComplete = true;
        Logger::info("Shader test complete: tested " + std::to_string(shaderTestResults.size()) + " shaders");
    }

    void ImGuiOverlay::stopShaderTest()
    {
        if (shaderTestRun)
            shaderTestRun->cancelled.store(true);
        if (shaderTestDone.valid())
            shaderTestDone.wait();
    }

    void ImGuiOverlay::renderShaderTestSection()
    {
        if (shaderTestRunning)
        {
            float progress = shaderTestQueue.empty() ? 1.0f :
                static_cast<float>(shaderTestCurrentIndex) / static_cast<float>(shaderTestQueue.size());
            const OverlayPalette& palette = activePalette();
            ui::notef("Testing %zu of %zu", static_cast<size_t>(shaderTestCurrentIndex), shaderTestQueue.size());
            ui::splitBar({{progress, palette.accent}, {1.0 - progress, palette.well}});
        }

        if (shaderTestComplete && !shaderTestResults.empty())
        {
            const OverlayPalette& palette = activePalette();
            const bool blocking     = settingsManager.getSafeAntiCheat();
            int        allowedCount = 0, failCount = 0, depthCount = 0;
            for (const ShaderTestResult& result : shaderTestResults)
            {
                allowedCount += shaderTestAllowed(result, blocking) ? 1 : 0;
                failCount += result.success ? 0 : 1;
                depthCount += result.success && result.usesDepth ? 1 : 0;
            }
            ui::chip((std::to_string(allowedCount) + " allowed").c_str(), palette.ok);
            ImGui::SameLine();
            ui::chip((std::to_string(failCount) + " failed").c_str(), failCount == 0 ? palette.muted : palette.bad);
            if (depthCount > 0)
            {
                ImGui::SameLine();
                ui::chip((std::to_string(depthCount) + (blocking ? " blocked" : " read depth")).c_str(), palette.info,
                         blocking ? "These read depth, which Safe Anti-Cheat blocks" : "These read depth; Safe Anti-Cheat would block them");
            }
            if (shaderTestDuplicateCount > 0)
            {
                ui::notef("%d %s skipped", shaderTestDuplicateCount, shaderTestDuplicateCount == 1 ? "duplicate" : "duplicates");
                ImGui::SetItemTooltip("Files reached through more than one shader path (symlinks or the same folder twice)\nare tested once; a copy at another path is tested again.");
            }
        }
    }

    void renderShaderTestResults(const std::vector<ShaderTestResult>& results)
    {
        const OverlayPalette& palette = activePalette();
        if (results.empty())
            return;

        const bool blocking     = settingsManager.getSafeAntiCheat();
        int        failCount    = 0;
        int        depthCount   = 0;
        int        allowedCount = 0;
        for (const ShaderTestResult& result : results)
        {
            failCount += result.success ? 0 : 1;
            depthCount += result.success && result.usesDepth ? 1 : 0;
            allowedCount += shaderTestAllowed(result, blocking) ? 1 : 0;
        }

        const auto nameLine = [&](const std::string& name, const std::string& path, const std::string& error) {
            if (!error.empty())
                ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SetItemTooltip("%s", path.c_str());
            if (!error.empty())
            {
                const size_t lines = static_cast<size_t>(std::count(error.begin(), error.end(), '\n')) + (error.back() == '\n' ? 0 : 1);
                ImGui::SameLine();
                ui::chip((std::to_string(lines) + (lines == 1 ? " warning" : " warnings")).c_str(), palette.warn, error.c_str());
            }
        };

        if (failCount > 0)
        {
            const std::string count = std::to_string(failCount);
            if (ui::disclosure("FailedShaders", "Failed", palette.text, ImGui::GetContentRegionAvail().x, count.c_str()))
            {
                ui::beginPanel();
                for (const auto& [name, path, success, usesDepth, usesMinPrecision, error] : results)
                {
                    if (success)
                        continue;
                    ui::panelRow(ui::Size::control);
                    const bool open = ui::disclosure(path.c_str(), name.c_str(), palette.text, ImGui::GetContentRegionAvail().x);
                    if (!error.empty())
                        ImGui::SetItemTooltip("%s", error.c_str());
                    if (open)
                    {
                        ui::notef("Path: %s", path.c_str());
                        ui::errorText(("Error: " + error).c_str());
                        ImGui::TreePop();
                    }
                }
                ui::endPanel();
                ImGui::TreePop();
            }
        }

        if (depthCount > 0)
        {
            const std::string count = std::to_string(depthCount);
            const bool        open  = ui::disclosure("DepthShaders", blocking ? "Blocked" : "Read depth", palette.text, ImGui::GetContentRegionAvail().x, count.c_str());
            ImGui::SetItemTooltip(blocking ? "These read depth, which Safe Anti-Cheat blocks." : "These read depth; Safe Anti-Cheat would block them. They are allowed while it is off.");
            if (open)
            {
                ui::beginPanel();
                for (const auto& [name, path, success, usesDepth, usesMinPrecision, error] : results)
                    if (success && usesDepth)
                    {
                        ui::panelRow(ui::Size::control);
                        nameLine(name, path, error);
                    }
                ui::endPanel();
                ImGui::TreePop();
            }
        }

        if (allowedCount > 0)
        {
            const std::string count = std::to_string(allowedCount);
            if (ui::disclosure("PassedShaders", "Allowed", palette.text, ImGui::GetContentRegionAvail().x, count.c_str()))
            {
                ui::beginPanel();
                for (const ShaderTestResult& result : results)
                    if (shaderTestAllowed(result, blocking))
                    {
                        ui::panelRow(ui::Size::control);
                        nameLine(result.effectName, result.filePath, result.errorMessage);
                    }
                ui::endPanel();
                ImGui::TreePop();
            }
        }
    }

}
