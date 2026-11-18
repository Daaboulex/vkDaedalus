#ifndef STATUS_BADGE_HPP_INCLUDED
#define STATUS_BADGE_HPP_INCLUDED

#include <cstddef>
#include <optional>
#include <string>

#include "mako_status.hpp"
#include "overlay/icon.hpp"

namespace vkBasalt
{
    enum class Tone
    {
        Ok,
        Muted,
        Warn,
        Bad,
        Info
    };

    struct Badge
    {
        Tone                tone = Tone::Muted;
        std::optional<Icon> icon;
        std::string         text;
    };

    struct FrameGenerationHeader
    {
        bool        blocked         = false;
        bool        notInstalled    = false;
        bool        on              = false;
        bool        loaded          = false;
        bool        needsModel      = false;
        bool        paused          = false;
        bool        live            = false;
        bool        loadsNextLaunch = false;
        std::string amount;
    };

    inline Badge frameGenerationBadge(const FrameGenerationHeader& fg)
    {
        if (fg.blocked)
            return {Tone::Muted, Icon::Power, fg.notInstalled ? "Not installed" : "Unavailable"};
        if (!fg.on)
            return {Tone::Muted, Icon::Power, fg.loaded ? "Off" : "Off, not loaded"};
        if (fg.needsModel)
            return {Tone::Bad, Icon::Warning, fg.amount + ", needs a model"};
        if (fg.paused)
            return {Tone::Warn, Icon::Pause, fg.amount + ", paused"};
        if (fg.live)
            return {Tone::Ok, Icon::Power, fg.amount + ", on"};
        if (fg.loadsNextLaunch)
            return {Tone::Warn, Icon::Restart, fg.amount + ", next launch"};
        return {Tone::Warn, Icon::Warning, fg.amount + ", needs vkdaedalus-run"};
    }

    inline Badge scalingBadge(bool blocked, const MakoScalingShown& scaling, const std::string& runningMethod, const std::string& amount, bool loadsNextLaunch)
    {
        if (blocked)
            return {Tone::Muted, Icon::Power, "Unavailable"};
        switch (scaling.run)
        {
            case MakoScalingRun::Scaling: return {Tone::Ok, Icon::Power, runningMethod + " " + scaling.detail + ", on"};
            case MakoScalingRun::NotScaling: return {Tone::Warn, Icon::Pause, "Not scaling"};
            case MakoScalingRun::NoStatus: return {Tone::Warn, Icon::Warning, "No status file"};
            case MakoScalingRun::Off: return {Tone::Muted, Icon::Power, "Off"};
            case MakoScalingRun::NotStarted: break;
        }
        if (loadsNextLaunch)
            return {Tone::Warn, Icon::Restart, amount + ", next launch"};
        return {Tone::Warn, Icon::Warning, amount + ", needs vkdaedalus-run"};
    }

    inline Badge makoGpuClaimBadge(const MakoGpuClaim& claim)
    {
        const std::string where = makoGpusWords(claim.gpus, claim.named);
        if (claim.generating)
            return {Tone::Ok, Icon::Power, "Generating on " + where};
        return {Tone::Bad, Icon::Warning, "No frames on " + where};
    }

    inline Badge changesWaitingBadge(size_t count)
    {
        return {Tone::Warn, Icon::Warning, std::to_string(count) + (count == 1 ? " change" : " changes") + " at next launch"};
    }

    inline std::optional<Badge> nextLaunchBadge(bool pending)
    {
        if (!pending)
            return std::nullopt;
        return Badge{Tone::Warn, Icon::Warning, "Next launch"};
    }

    inline Badge allowedBadge(int count)
    {
        return {Tone::Ok, Icon::Shield, std::to_string(count) + " allowed"};
    }

    inline Badge failedBadge(int count)
    {
        if (count == 0)
            return {Tone::Muted, std::nullopt, "0 failed"};
        return {Tone::Bad, Icon::Warning, std::to_string(count) + " failed"};
    }

    inline Badge depthBadge(int count, bool blocking)
    {
        return {Tone::Info, blocking ? Icon::Shield : Icon::Eye, std::to_string(count) + (blocking ? " blocked" : " read depth")};
    }

    inline Badge warningsBadge(size_t lines)
    {
        return {Tone::Warn, Icon::Warning, std::to_string(lines) + (lines == 1 ? " warning" : " warnings")};
    }
}

#endif
