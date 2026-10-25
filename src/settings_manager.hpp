#ifndef SETTINGS_MANAGER_HPP_INCLUDED
#define SETTINGS_MANAGER_HPP_INCLUDED

#include <atomic>
#include <cctype>
#include <string>
#include <vector>

#include "config_serializer.hpp"

namespace vkBasalt
{
    struct KeyUse
    {
        std::string action;
        std::string key;
    };

    inline std::string depthCaptureStatus(bool chosen, bool safeAntiCheat, bool runsNow)
    {
        if (safeAntiCheat)
            return "Off while Safe Anti-Cheat is on.";
        if (chosen == runsNow)
            return runsNow ? "On in this run." : "Off in this run.";
        return runsNow ? "On in this run, off from the next launch: a restart applies it."
                       : "Off in this run, on from the next launch: a restart applies it.";
    }

    inline std::atomic<bool> depthCaptureRunning{false};

    inline std::string keyTakenBy(const std::string& key, const std::vector<KeyUse>& others)
    {
        const auto lower = [](std::string text) {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        for (const KeyUse& other : others)
            if (!key.empty() && lower(other.key) == lower(key))
                return other.action;
        return "";
    }

    class SettingsManager
    {
    public:
        void initialize();

        bool isInitialized() const { return initialized; }

        bool save();

        int getMaxEffects() const { return settings.maxEffects; }
        const std::string& getToggleKey() const { return settings.toggleKey; }
        const std::string& getFrameGenerationKey() const { return settings.frameGenerationKey; }
        const std::string& getReloadKey() const { return settings.reloadKey; }
        const std::string& getOverlayKey() const { return settings.overlayKey; }
        const std::string& getPreviewKey() const { return settings.previewKey; }
        int                getOverlayBackdropPercent() const { return settings.overlayBackdropPercent; }
        int                getOverlaySizePercent() const { return settings.overlaySizePercent; }
        bool getEnableOnLaunch() const { return settings.enableOnLaunch; }
        bool getDepthCapture() const { return settings.depthCapture && !safeAntiCheat; }
        bool getAutoApply() const { return settings.autoApply; }
        int getAutoApplyDelayMs() const { return settings.autoApplyDelayMs; }
        bool getEffectGpuTiming() const { return settings.effectGpuTiming; }
        bool getLiveReshadeUniforms() const { return settings.liveReshadeUniforms; }
        LaunchNotice getLaunchNotice() const { return settings.launchNotice; }
        bool getHud() const { return settings.hud; }
        const std::string& getHudItems() const { return settings.hudItems; }
        const std::string& getPalette() const { return settings.palette; }
        const std::string& getLogLevel() const { return settings.logLevel; }
        bool getSafeAntiCheat() const { return safeAntiCheat; }

        void setMaxEffects(int value) { settings.maxEffects = value; }
        void setToggleKey(const std::string& value) { settings.toggleKey = value; }
        void setFrameGenerationKey(const std::string& value) { settings.frameGenerationKey = value; }
        void setReloadKey(const std::string& value) { settings.reloadKey = value; }
        void setOverlayKey(const std::string& value) { settings.overlayKey = value; }
        void setPreviewKey(const std::string& value) { settings.previewKey = value; }
        void setOverlayBackdropPercent(int value) { settings.overlayBackdropPercent = value; }
        void setOverlaySizePercent(int value) { settings.overlaySizePercent = value; }
        void setEnableOnLaunch(bool value) { settings.enableOnLaunch = value; }
        void setDepthCapture(bool value) { settings.depthCapture = value; }
        void setAutoApply(bool value) { settings.autoApply = value; }
        void setAutoApplyDelayMs(int value) { settings.autoApplyDelayMs = value; }
        void setEffectGpuTiming(bool value) { settings.effectGpuTiming = value; }
        void setLiveReshadeUniforms(bool value) { settings.liveReshadeUniforms = value; }
        void setLaunchNotice(LaunchNotice value) { settings.launchNotice = value; }
        void setHud(bool value) { settings.hud = value; }
        void setHudItems(const std::string& value) { settings.hudItems = value; }
        void setPalette(const std::string& value) { settings.palette = value; }
        void setLogLevel(const std::string& value) { settings.logLevel = value; }
        void setSafeAntiCheat(bool value) { safeAntiCheat = value || !safeAntiCheatLock.empty(); }
        void lockSafeAntiCheat(const std::string& reason)
        {
            safeAntiCheatLock = reason;
            safeAntiCheat     = true;
        }
        const std::string& getSafeAntiCheatLock() const { return safeAntiCheatLock; }

        const VkBasaltSettings& getSettings() const { return settings; }

    private:
        VkBasaltSettings settings;
        bool initialized = false;
        bool safeAntiCheat = true;
        std::string safeAntiCheatLock;
    };

    extern SettingsManager settingsManager;

}

#endif
