#include "imgui_overlay.hpp"
#include "launch_notice.hpp"
#include "logger.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/frame_layout.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/widgets.hpp"
#include "settings_manager.hpp"

#include <cfloat>
#include <cstdlib>
#include <string>

#include "imgui.h"

namespace vkBasalt
{
    namespace
    {
        LaunchNotice launchNoticeForThisRun()
        {
            if (const std::optional<LaunchNotice> notice = launchNoticeFromEnvironment())
                return *notice;
            const char* asked = std::getenv("VKDAEDALUS_LAUNCH_NOTICE");
            if (asked == nullptr || *asked == '\0')
                return settingsManager.getLaunchNotice();
            Logger::warn(std::string("VKDAEDALUS_LAUNCH_NOTICE=") + asked + " is not every, first or never, so the saved choice, "
                         + launchNoticeWord(settingsManager.getLaunchNotice()) + ", applies");
            return settingsManager.getLaunchNotice();
        }
    }

    bool ImGuiOverlay::launchNoticeShowing(std::chrono::steady_clock::time_point now)
    {
        if (launchNoticeOver)
            return false;
        if (!launchNoticeStart)
        {
            if (!launchNoticeDue(launchNoticeForThisRun(), firstLaunchOfTheGame))
            {
                launchNoticeOver = true;
                return false;
            }
            const EffectsRunning effects = effectsRunning();
            launchNoticeProblem          = effects.run < effects.on;
            const std::optional<std::string> fixed = fixedConfigFile();
            launchNoticeLine = fixed ? "Config file " + fixed->substr(fixed->find_last_of('/') + 1) + ", "
                                     : "Profile " + (activeProfileName.empty() ? std::string("default") : activeProfileName) + ", ";
            if (!settingsManager.getEnableOnLaunch())
                launchNoticeLine += "effects off";
            else if (launchNoticeProblem)
                launchNoticeLine += std::to_string(effects.on - effects.run) + " of " + std::to_string(effects.on) + " effects could not start";
            else
                launchNoticeLine += std::to_string(effects.on) + (effects.on == 1 ? " effect on" : " effects on");
            if (const std::optional<uint32_t> multiplier = frameGenerationMultiplierHere())
                launchNoticeLine += ", frame generation " + std::to_string(*multiplier) + "x";
            launchNoticeStart = now;
            Logger::info("launch notice shown: " + launchNoticeLine + (launchNoticeProblem ? ", an effect could not run" : ""));
        }
        const float seconds = std::chrono::duration<float>(now - *launchNoticeStart).count();
        if (seconds < (launchNoticeProblem ? launchNoticeProblemSeconds : launchNoticeSeconds))
            return true;
        launchNoticeOver = true;
        Logger::info("launch notice left");
        return false;
    }

    bool ImGuiOverlay::shortNoticeShowing(std::chrono::steady_clock::time_point now)
    {
        if (!shortNoticeStart)
            return false;
        if (std::chrono::duration<float>(now - *shortNoticeStart).count() < shortNoticeSeconds)
            return true;
        shortNoticeStart.reset();
        return false;
    }

    void ImGuiOverlay::renderShortNotice(std::chrono::steady_clock::time_point now)
    {
        renderNotice("Frame generation", shortNoticeLine, shortNoticeProblem, "Frame generation notice", *shortNoticeStart, shortNoticeSeconds, now);
    }

    void ImGuiOverlay::renderLaunchNotice(std::chrono::steady_clock::time_point now)
    {
        renderNotice("vkDaedalus loaded", launchNoticeLine, launchNoticeProblem, "Launch notice", *launchNoticeStart,
                     launchNoticeProblem ? launchNoticeProblemSeconds : launchNoticeSeconds, now);
    }

    void ImGuiOverlay::renderNotice(const char* title, const std::string& line, bool problem, const char* mapName, std::chrono::steady_clock::time_point start,
                                    float duration, std::chrono::steady_clock::time_point now)
    {
        ImGuiIO&          io     = ImGui::GetIO();
        const FrameLayout layout = frameLayout(io.DisplaySize.x, io.DisplaySize.y, desktopScale().scale);
        const float       s      = layout.scale;
        applyFrameScale(s);
        ui::beginFrame(fonts);
        const OverlayPalette& palette = activePalette();

        const NoticeLook look = noticeLookAt(std::chrono::duration<float>(now - start).count(), duration);
        const ImVec2     size(420.0f * s, 64.0f * s);
        const float      margin = ui::Size::groupGap * s;
        const ImVec2     at((io.DisplaySize.x - size.x) * 0.5f, margin - (size.y + margin) * (1.0f - look.shown));
        const ImVec2     end(at.x + size.x, at.y + size.y);

        ImGui::SetNextWindowPos(at, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("vkDaedalus notice", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar(2);

        ImDrawList*  list   = ImGui::GetWindowDrawList();
        const float  radius = ui::Size::surface * s;
        const ImVec4 ink    = problem ? palette.warn : palette.accent;
        ui::popupShadow();
        list->AddRectFilled(at, end, ImGui::GetColorU32(palette.panel), radius);

        const float  square     = 44.0f * s;
        const ImVec2 squareAt(at.x + ui::Size::groupGap * s, at.y + (size.y - square) * 0.5f);
        const ImVec4 tint       = ui::mix(palette.panel, ink, 0.18f);
        list->AddRectFilled(squareAt, ImVec2(squareAt.x + square, squareAt.y + square), ImGui::GetColorU32(tint), radius);
        const float wingHeight = 26.0f * s;
        drawWing(list, ImVec2(squareAt.x + (square - wingHeight * 0.7f) * 0.5f, squareAt.y + (square - wingHeight) * 0.5f), wingHeight, ImGui::GetColorU32(ink),
                 ImGui::GetColorU32(tint));
        const float textLeft = squareAt.x + square + ui::Size::gap * 1.5f * s;
        list->AddText(fonts.bold, FontSize::body * s, ImVec2(textLeft, at.y + 12.0f * s), ImGui::GetColorU32(palette.text), title);
        list->AddText(fonts.body, FontSize::small * s, ImVec2(textLeft, at.y + 35.0f * s), ImGui::GetColorU32(problem ? palette.warn : palette.muted),
                      line.c_str());

        const std::string key   = settingsManager.getOverlayKey();
        const ImVec2      cap   = keyCapSize(fonts.body, FontSize::caption * s, key.c_str());
        const ImVec2      capAt = ImVec2(end.x - ui::Size::groupGap * s - cap.x, at.y + (size.y - cap.y) * 0.5f);
        drawKeyCap(list, fonts.body, FontSize::caption * s, capAt, key.c_str(), palette);
        const ImVec2 press = fonts.body->CalcTextSizeA(FontSize::small * s, FLT_MAX, 0.0f, "Press");
        list->AddText(fonts.body, FontSize::small * s, ImVec2(capAt.x - ui::Size::gap * s - press.x, at.y + (size.y - press.y) * 0.5f), ImGui::GetColorU32(palette.muted),
                      "Press");

        const float track  = 3.0f * s;
        const float left   = textLeft;
        const float right  = end.x - ui::Size::groupGap * s;
        const float bottom = end.y - ui::Size::gap * s;
        list->AddRectFilled(ImVec2(left, bottom - track), ImVec2(right, bottom), ImGui::GetColorU32(palette.well), track * 0.5f);
        list->AddRectFilled(ImVec2(left, bottom - track), ImVec2(left + (right - left) * look.remaining, bottom), ImGui::GetColorU32(ink), track * 0.5f);

        ImGui::SetCursorScreenPos(at);
        UiMap::nameNextItem(mapName);
        ImGui::InvisibleButton(mapName, size);
        ImGui::End();
    }
}
