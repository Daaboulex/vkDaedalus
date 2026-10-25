#pragma once

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>

namespace vkBasalt
{
    enum class LaunchNotice
    {
        EveryLaunch,
        FirstLaunch,
        Never
    };

    inline std::optional<LaunchNotice> launchNoticeFrom(const std::string& word)
    {
        if (word == "every")
            return LaunchNotice::EveryLaunch;
        if (word == "first")
            return LaunchNotice::FirstLaunch;
        if (word == "never")
            return LaunchNotice::Never;
        return std::nullopt;
    }

    inline std::optional<LaunchNotice> launchNoticeFromEnvironment()
    {
        const char* asked = std::getenv("VKDAEDALUS_LAUNCH_NOTICE");
        return asked == nullptr ? std::nullopt : launchNoticeFrom(asked);
    }

    inline const char* launchNoticeWord(LaunchNotice notice)
    {
        switch (notice)
        {
            case LaunchNotice::EveryLaunch: return "every";
            case LaunchNotice::FirstLaunch: return "first";
            case LaunchNotice::Never: return "never";
        }
        return "every";
    }

    inline bool launchNoticeDue(LaunchNotice setting, bool firstLaunchOfTheGame)
    {
        return setting == LaunchNotice::EveryLaunch || (setting == LaunchNotice::FirstLaunch && firstLaunchOfTheGame);
    }

    constexpr float shortNoticeSeconds         = 2.5f;
    constexpr float launchNoticeSeconds        = 5.0f;
    constexpr float launchNoticeProblemSeconds = 8.0f;
    constexpr float launchNoticeMoveSeconds    = 0.35f;

    struct NoticeLook
    {
        float shown     = 0.0f;
        float remaining = 1.0f;
        bool  done      = false;
    };

    inline NoticeLook noticeLookAt(float seconds, float duration)
    {
        NoticeLook look;
        if (seconds >= duration)
        {
            look.done      = true;
            look.remaining = 0.0f;
            return look;
        }
        const float in  = std::clamp(seconds / launchNoticeMoveSeconds, 0.0f, 1.0f);
        const float out = std::clamp((duration - seconds) / launchNoticeMoveSeconds, 0.0f, 1.0f);
        const float easeIn  = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);
        const float easeOut = out * out;
        look.shown          = std::min(easeIn, easeOut);
        look.remaining      = 1.0f - seconds / duration;
        return look;
    }
}
