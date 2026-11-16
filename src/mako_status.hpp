#ifndef MAKO_STATUS_HPP_INCLUDED
#define MAKO_STATUS_HPP_INCLUDED

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vkBasalt
{
    inline constexpr std::string_view makoGenerationRole = "frame-generation";

    struct MakoStatus
    {
        std::string                role;
        uint64_t                   context = 0;
        std::string                appliedModel;
        bool                       appliedScalingEnabled = false;
        bool                       scalingActive         = false;
        std::optional<std::string> scalingInactiveReason;
        std::string                scalingMethod;
        uint32_t                   sourceWidth        = 0;
        uint32_t                   sourceHeight       = 0;
        uint32_t                   presentationWidth  = 0;
        uint32_t                   presentationHeight = 0;

        bool operator==(const MakoStatus&) const = default;
    };

    struct MakoStatusRead
    {
        std::vector<MakoStatus> ofSwapchain;
        std::string             problem;
    };

    enum class MakoScalingRun
    {
        Off,
        Scaling,
        NotScaling,
        NoStatus,
        NotStarted,
    };

    struct MakoScalingShown
    {
        MakoScalingRun run = MakoScalingRun::Off;
        std::string    detail;
        std::string    method;

        bool operator==(const MakoScalingShown&) const = default;
    };

    std::optional<MakoStatus> parseMakoStatus(const std::string& text, std::string& problem);
    std::string               makoStatusFolder(const std::string& configPath);
    bool                      isMakoStatusFileOf(std::string_view fileName, uint64_t pid, uint64_t startTicks);
    bool                      makoStatusIsOfSwapchain(const MakoStatus& status, uint32_t width, uint32_t height);
    MakoStatusRead            readMakoStatus(const std::string& folder, uint64_t pid, uint64_t startTicks, uint32_t width, uint32_t height);
    uint64_t                  thisProcessStartTicks();

    const MakoStatus* makoScalingStatus(const std::vector<MakoStatus>& ofSwapchain);
    const MakoStatus* makoGenerationStatus(const std::vector<MakoStatus>& ofSwapchain);
    bool              makoFellBackToMakoFg(std::string_view modelAtStart, const std::vector<MakoStatus>& ofSwapchain);
    std::string       makoScalingSizes(const MakoStatus& status);
    std::string       makoScalingInactiveWords(std::string_view reason);
    MakoScalingShown  makoScalingShown(bool onInFile, bool makoRunsTheProfile, const MakoStatusRead& read);
}

#endif
