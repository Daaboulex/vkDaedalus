#ifndef MAKO_STATUS_HPP_INCLUDED
#define MAKO_STATUS_HPP_INCLUDED

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mako_gpu.hpp"

namespace vkBasalt
{
    inline constexpr std::string_view makoGenerationRole        = "frame-generation";
    inline constexpr uint64_t         makoStatusFirstPresent    = 120;
    inline constexpr uint64_t         makoStatusPresentInterval = 600;

    struct MakoStatus
    {
        std::string                role;
        uint64_t                   context = 0;
        std::string                appliedModel;
        std::optional<uint32_t>    appliedTargetFps;
        bool                       generationActive = false;
        std::optional<std::string> generationUnavailableReason;
        bool                       appliedScalingEnabled = false;
        bool                       scalingActive         = false;
        std::optional<std::string> scalingInactiveReason;
        std::string                scalingMethod;
        uint32_t                   sourceWidth        = 0;
        uint32_t                   sourceHeight       = 0;
        uint32_t                   presentationWidth  = 0;
        uint32_t                   presentationHeight = 0;
        std::optional<std::string> appliedGpu;
        std::optional<std::string> error;

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

    struct MakoGpuClaim
    {
        bool                  generating = false;
        std::vector<uint32_t> gpus;
        std::string           named;
        std::string           error;

        bool operator==(const MakoGpuClaim&) const = default;
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
    std::optional<std::string> makoGenerationPaused(const std::vector<MakoStatus>& ofSwapchain);
    std::string       makoScalingSizes(const MakoStatus& status);
    std::string       makoScalingInactiveWords(std::string_view reason);
    MakoScalingShown  makoScalingShown(bool onInFile, bool makoRunsTheProfile, const MakoStatusRead& read);
    bool              makoStatusReadDue(uint64_t presents);

    std::vector<uint32_t>       makoGpusNamedBy(std::string_view named, const MakoGpus& gpus);
    std::string                 makoGpusWords(const std::vector<uint32_t>& gpus, std::string_view named);
    std::optional<MakoGpuClaim> makoGpuClaim(const std::vector<MakoStatus>& ofSwapchain, const MakoGpus& gpus);
    std::string                 makoGpuClaimLine(const MakoGpuClaim& claim);
}

#endif
