#include "imgui_overlay.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"
#include "shader_cache.hpp"
#include "overlay/widgets.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <set>

#include "imgui.h"
#include "imfilebrowser.h"
#include "imgui_internal.h"

namespace vkBasalt
{
    static ImGui::FileBrowser dirBrowser(
        ImGuiFileBrowserFlags_SelectDirectory |
        ImGuiFileBrowserFlags_HideRegularFiles |
        ImGuiFileBrowserFlags_CreateNewDir);

    void ImGuiOverlay::renderShaderManagerView()
    {
        if (!shaderMgrInitialized)
        {
            ShaderManagerConfig config = ConfigSerializer::loadShaderManagerConfig();
            shaderMgrParentDirs = config.parentDirectories;
            shaderMgrShaderPaths = config.discoveredShaderPaths;
            shaderMgrTexturePaths = config.discoveredTexturePaths;
            shaderMgrInitialized = true;
        }

        auto saveConfig = [&]() {
            ShaderManagerConfig config;
            config.parentDirectories = shaderMgrParentDirs;
            config.discoveredShaderPaths = shaderMgrShaderPaths;
            config.discoveredTexturePaths = shaderMgrTexturePaths;
            shaderMgrSaveFailed = !ConfigSerializer::saveShaderManagerConfig(config);
            shaderPathsChanged  = true;
        };

        const OverlayPalette& palette = activePalette();
        const char*           homeDir = std::getenv("HOME");
        const std::string     home    = homeDir != nullptr ? homeDir : "";
        const auto            tidy    = [&](const std::string& path) {
            return !home.empty() && path.rfind(home + "/", 0) == 0 ? "~" + path.substr(home.size()) : path;
        };
        const auto lastPart = [](const std::string& path) {
            const size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        };
        const auto relative = [&](const std::string& path) {
            for (const std::string& pack : shaderMgrParentDirs)
                if (path.rfind(pack + "/", 0) == 0)
                    return lastPart(pack) + path.substr(pack.size());
            return tidy(path);
        };
        const auto under = [](const std::vector<std::string>& paths, const std::string& pack) {
            return std::count_if(paths.begin(), paths.end(), [&](const std::string& path) { return path.rfind(pack + "/", 0) == 0; });
        };
        const auto removeButton = [&](const char* removeLabel, float rowStart, float rowWidth, const char* tip) {
            const float button = ui::px(ui::Size::inRow);
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(rowStart + rowWidth - button, ImGui::GetCursorScreenPos().y + (ui::px(ui::Size::control) - button) * 0.5f));
            return ui::iconButton(removeLabel, Icon::Close, tip);
        };
        const auto pathRow = [&](const char* removeLabel, const std::string& path, const char* tip) {
            ui::panelRow(ui::Size::control);
            const float rowStart = ImGui::GetCursorScreenPos().x;
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ui::elideStart(relative(path), rowWidth - ui::px(ui::Size::inRow) - ui::px(ui::Size::gap)).c_str());
            ImGui::SetItemTooltip("%s", path.c_str());
            return removeButton(removeLabel, rowStart, rowWidth, tip);
        };

        ui::heading("Shaders");
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##shaders", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (!table)
            return;
        ImGui::TableNextColumn();
        ui::beginScrollArea("##packs");

        ImGui::AlignTextToFramePadding();
        ui::sectionLabel("Packs");
        ImGui::SetItemTooltip("Folders holding ReShade shader packs, saved for every game.\nEach is searched for Shaders/ and Textures/ folders.");
        ui::alignRight(ui::buttonWidth("Browse...") + ImGui::GetStyle().ItemSpacing.x + ui::buttonWidth("Rescan all"));
        if (ui::button("Browse..."))
        {
            dirBrowser.SetTitle("Choose a shader pack folder");
            const char* home = std::getenv("HOME");
            dirBrowser.SetPwd(home ? home : "/");
            dirBrowser.Open();
        }

        ImGui::SameLine();
        const bool rescan = ui::button("Rescan all");
        ImGui::SetItemTooltip("Looks again in every pack for its Shaders and Textures folders");
        if (!shaderMgrRescanProblem.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, palette.bad);
            ImGui::TextWrapped("%s", shaderMgrRescanProblem.c_str());
            ImGui::PopStyleColor();
        }
        int removeIdx = -1;
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        ui::beginPanel();
        if (shaderMgrParentDirs.empty())
        {
            ui::panelRow();
            ui::note("No packs");
        }
        for (size_t i = 0; i < shaderMgrParentDirs.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            const std::string& pack     = shaderMgrParentDirs[i];
            ui::panelRow(ui::Size::tallRow);
            const float        rowStart = ImGui::GetCursorScreenPos().x;
            const float        rowWidth = ImGui::GetContentRegionAvail().x;
            const long         shaders  = under(shaderMgrShaderPaths, pack);
            const long         textures = under(shaderMgrTexturePaths, pack);
            char               found[64];
            std::snprintf(found, sizeof(found), "%ld shader, %ld texture %s", shaders, textures, textures == 1 ? "folder" : "folders");
            const float        foundW   = ImGui::CalcTextSize(found).x;
            const float        textRoom = rowWidth - ui::px(ui::Size::inRow) - foundW - 2.0f * ui::px(ui::Size::gap);
            ImGui::BeginGroup();
            ImGui::PushFont(ui::fonts().bold, FontSize::label);
            ImGui::TextUnformatted(ui::elideStart(lastPart(pack), textRoom).c_str());
            ImGui::PopFont();
            ImGui::PushFont(nullptr, FontSize::caption);
            ImGui::TextColored(palette.muted, "%s", ui::elideStart(tidy(pack), textRoom).c_str());
            ImGui::PopFont();
            ImGui::EndGroup();
            ImGui::SetItemTooltip("%s", pack.c_str());
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(rowStart + rowWidth - ui::px(ui::Size::inRow) - ui::px(ui::Size::gap) - foundW, ImGui::GetCursorScreenPos().y));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(shaders == 0 ? palette.warn : palette.muted, "%s", found);
            if (removeButton("X##pack", rowStart, rowWidth, "Remove this pack and the folders found in it"))
                removeIdx = static_cast<int>(i);
            ImGui::PopID();
        }
        ui::endPanel();

        if (removeIdx >= 0)
        {
            const std::string removed = shaderMgrParentDirs[static_cast<size_t>(removeIdx)] + "/";
            const auto        under   = [&](const std::string& path) { return path.rfind(removed, 0) == 0; };
            shaderMgrShaderPaths.erase(std::remove_if(shaderMgrShaderPaths.begin(), shaderMgrShaderPaths.end(), under), shaderMgrShaderPaths.end());
            shaderMgrTexturePaths.erase(std::remove_if(shaderMgrTexturePaths.begin(), shaderMgrTexturePaths.end(), under), shaderMgrTexturePaths.end());
            shaderMgrParentDirs.erase(shaderMgrParentDirs.begin() + removeIdx);
            saveConfig();
        }

        if (rescan)
        {
            std::vector<std::string> shaderPaths, texturePaths;
            shaderMgrRescanProblem.clear();
            for (const auto& parentDir : shaderMgrParentDirs)
            {
                std::error_code unreadable;
                if (std::filesystem::is_directory(parentDir, unreadable))
                    ConfigSerializer::scanShaderTree(parentDir, shaderPaths, texturePaths);
                else
                    shaderMgrRescanProblem += (shaderMgrRescanProblem.empty() ? "" : "\n") + parentDir +
                                              (unreadable ? " cannot be read: " + unreadable.message() : std::string(" is not a folder"));
            }
            if (!shaderMgrRescanProblem.empty())
                Logger::warn("Shader Manager: " + shaderMgrRescanProblem);
            const std::set<std::string> shaderSet(shaderPaths.begin(), shaderPaths.end());
            const std::set<std::string> textureSet(texturePaths.begin(), texturePaths.end());
            shaderMgrShaderPaths.assign(shaderSet.begin(), shaderSet.end());
            shaderMgrTexturePaths.assign(textureSet.begin(), textureSet.end());
            Logger::info("Shader Manager: Found " + std::to_string(shaderMgrShaderPaths.size()) +
                " shader paths, " + std::to_string(shaderMgrTexturePaths.size()) + " texture paths");
            saveConfig();
        }
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
        ui::sectionLabel("Found in the packs");
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        const std::string shaderCount = std::to_string(shaderMgrShaderPaths.size());
        if (ui::disclosure("shaderfolders", "Shader folders", palette.text, ImGui::GetContentRegionAvail().x, shaderCount.c_str()))
        {
            if (shaderMgrShaderPaths.empty())
                ui::note("None found");
            else
            {
                ui::beginPanel();
                int removeShaderIdx = -1;
                for (size_t i = 0; i < shaderMgrShaderPaths.size(); i++)
                {
                    ImGui::PushID(static_cast<int>(i));
                    if (pathRow("X##shader", shaderMgrShaderPaths[i], "Leave this folder out"))
                        removeShaderIdx = static_cast<int>(i);
                    ImGui::PopID();
                }
                ui::endPanel();
                if (removeShaderIdx >= 0)
                {
                    shaderMgrShaderPaths.erase(shaderMgrShaderPaths.begin() + removeShaderIdx);
                    saveConfig();
                }
            }
            ImGui::TreePop();
        }

        const std::string textureCount = std::to_string(shaderMgrTexturePaths.size());
        if (ui::disclosure("texturefolders", "Texture folders", palette.text, ImGui::GetContentRegionAvail().x, textureCount.c_str()))
        {
            if (shaderMgrTexturePaths.empty())
                ui::note("None found");
            else
            {
                ui::beginPanel();
                int removeTextureIdx = -1;
                for (size_t i = 0; i < shaderMgrTexturePaths.size(); i++)
                {
                    ImGui::PushID(static_cast<int>(i) + 1000);
                    if (pathRow("X##texture", shaderMgrTexturePaths[i], "Leave this folder out"))
                        removeTextureIdx = static_cast<int>(i);
                    ImGui::PopID();
                }
                ui::endPanel();
                if (removeTextureIdx >= 0)
                {
                    shaderMgrTexturePaths.erase(shaderMgrTexturePaths.begin() + removeTextureIdx);
                    saveConfig();
                }
            }
            ImGui::TreePop();
        }

        ui::endScrollArea();

        ui::nextColumn();
        ui::beginScrollArea("##test");
        ui::sectionLabel("Test");
        renderShaderTestSection();
        if (shaderTestComplete)
            renderShaderTestResults(shaderTestResults);
        ui::endScrollArea();
        ImGui::EndTable();

        ui::footer();
        if (ui::button("Clear compiled effects"))
        {
            clearShaderCache();
            applyRequested = true;
        }
        ImGui::SetItemTooltip("Every ReShade effect compiles again; the GPU driver's own shader cache is not touched");
        if (shaderTestRunning)
        {
            ui::alignRight(ui::buttonWidth("Cancel test"));
            if (ui::button("Cancel test") && shaderTestRun)
                shaderTestRun->cancelled.store(true);
            ImGui::SetItemTooltip("Stops after the shader being compiled now");
        }
        else
        {
            ui::alignRight(ui::buttonWidth("Test all shaders", ui::Kind::Primary));
            ImGui::BeginDisabled(shaderMgrShaderPaths.empty());
            if (ui::button("Test all shaders", ui::Kind::Primary))
                startShaderTest();
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", shaderMgrShaderPaths.empty() ? "No shader folders yet: add a pack, then Rescan all"
                                                                     : "Compile every .fx shader in the shader folders to find the ones that fail");
        }

        ImGuiErrorRecoveryState beforeBrowser;
        ImGui::ErrorRecoveryStoreState(&beforeBrowser);
        try
        {
            dirBrowser.Display();
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            ImGui::ErrorRecoveryTryToRecoverState(&beforeBrowser);
            dirBrowser.Close();
            shaderMgrRescanProblem = std::string("the folder browser could not open a folder: ") + e.what();
            Logger::warn("Shader Manager: " + shaderMgrRescanProblem);
        }
        if (dirBrowser.HasSelected())
        {
            std::string selectedPath = dirBrowser.GetSelected().string();
            bool exists = false;
            for (const auto& dir : shaderMgrParentDirs)
            {
                if (dir == selectedPath)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
            {
                shaderMgrParentDirs.push_back(selectedPath);
                saveConfig();
            }
            dirBrowser.ClearSelected();
        }

    }

}
