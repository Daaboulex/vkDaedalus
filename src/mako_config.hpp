#ifndef MAKO_CONFIG_HPP_INCLUDED
#define MAKO_CONFIG_HPP_INCLUDED

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame_generation.hpp"

namespace vkBasalt
{
    inline constexpr std::string_view makoLayerName = "VK_LAYER_MAKO_render";

    inline constexpr std::array<FrameGenerationChoice, 2> makoFrameGenerationModels = {
        {{"lossless-scaling", "Lossless Scaling"}, {"mako-fg", "MAKO FG"}}};
    inline constexpr std::array<FrameGenerationChoice, 4> makoFgModes = {
        {{"quality", "Quality"}, {"balanced", "Balanced"}, {"performance", "Performance"}, {"extra-performance", "Extra performance"}}};
    inline constexpr std::array<FrameGenerationChoice, 4> makoScalingMethods = {
        {{"native", "Native"}, {"mako", "MAKO Scaler"}, {"ls1", "LS1"}, {"ls1-performance", "LS1 performance"}}};
    inline constexpr std::array<FrameGenerationChoice, 3> makoHdrQualities = {
        {{"quality", "Quality"}, {"balanced", "Balanced"}, {"performance", "Performance"}}};
    inline constexpr std::array<FrameGenerationChoice, 5> makoRealFramePriorities = {
        {{"auto", "Auto"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}, {"very-high", "Very high"}}};
    inline constexpr std::array<FrameGenerationChoice, 3> makoGamescopeVrrModes = {{{"follow-steam", "Follow Steam"}, {"on", "On"}, {"off", "Off"}}};
    inline constexpr std::array<FrameGenerationChoice, 1> makoPacings           = {{{"none", "None"}}};

    struct MakoLimits
    {
        static constexpr uint64_t                lowestMultiplier             = 2;
        static constexpr uint64_t                highestMultiplier            = 5;
        static constexpr float                   lowestScalingFactor          = 1.0f;
        static constexpr float                   highestScalingFactor         = 2.0f;
        static constexpr float                   lowestScalingSharpness       = 0.0f;
        static constexpr float                   highestScalingSharpness      = 1.0f;
        static constexpr std::array<uint32_t, 5> gpuHeadroomPercents          = {0, 5, 10, 15, 20};
        static constexpr uint32_t                highestBaseFpsCap            = 1000;
        static constexpr uint32_t                highestRefreshThreshold      = 1000;
        static constexpr uint32_t                lowestTargetFps              = 10;
        static constexpr uint32_t                highestTargetFps             = 1000;
        static constexpr uint64_t                lowestAdaptiveMaxMultiplier  = 2;
        static constexpr uint64_t                highestAdaptiveMaxMultiplier = 5;
        static constexpr float                   lowestProbeIntervalSeconds   = 0.1f;
        static constexpr float                   highestProbeIntervalSeconds  = 3.0f;
        static constexpr float                   lowestFlowScale              = 0.25f;
        static constexpr float                   highestFlowScale             = 1.0f;
    };

    struct MakoSettings
    {
        std::optional<std::string> gpu;
        uint64_t                   multiplier                          = 2;
        bool                       frameGenerationProvisioned          = true;
        bool                       frameGenerationEnabled              = true;
        std::string                frameGenerationModel                = "mako-fg";
        std::string                makoFgMode                          = "balanced";
        bool                       scalingEnabled                      = false;
        bool                       swapchainImageCountCompatibility    = false;
        bool                       mailboxPresentationCompatibility    = false;
        std::string                scalingMethod                       = "mako";
        float                      scalingFactor                       = 1.5f;
        bool                       scalingSupersampling                = false;
        float                      scalingSharpness                    = 0.8f;
        std::string                hdrQuality                          = "performance";
        uint32_t                   frameGenerationRefreshThreshold     = 0;
        uint32_t                   baseFpsCap                          = 0;
        uint32_t                   gpuHeadroomPercent                  = 0;
        bool                       adaptive                            = false;
        bool                       adaptiveAutoBaseFpsCap              = false;
        std::string                adaptiveFractionalRealFramePriority = "auto";
        uint32_t                   targetFps                           = 120;
        bool                       adaptiveTargetRefreshRate           = false;
        uint64_t                   adaptiveMaxMultiplier               = 3;
        bool                       adaptiveStableCadence               = true;
        std::string                gamescopeVrrMode                    = "follow-steam";
        bool                       dynamicCadenceRecovery              = false;
        float                      dynamicCadenceProbeIntervalSeconds  = 2.0f;
        bool                       ultraPerformance                    = false;
        float                      flowScale                           = 0.8f;
        bool                       performanceMode                     = false;
        std::string                pacing                              = "none";

        bool operator==(const MakoSettings& other) const;
    };

    struct MakoPowerSets
    {
        MakoSettings handheld;
        MakoSettings docked;

        bool operator==(const MakoPowerSets&) const = default;
    };

    struct MakoProfile
    {
        std::string                  name = "unnamed";
        std::vector<std::string>     activeIn;
        MakoSettings                 settings;
        std::optional<MakoPowerSets> power;

        bool operator==(const MakoProfile&) const = default;
    };

    struct MakoGlobal
    {
        std::optional<std::string> dll;
        bool                       allowHalfPrecision = true;

        bool operator==(const MakoGlobal&) const = default;
    };

    struct MakoFile
    {
        std::optional<std::string> currentProfile;
        MakoGlobal                 global;
        std::vector<MakoProfile>   profiles;
    };

    struct MakoRead
    {
        bool                    exists = false;
        std::string             text;
        std::optional<MakoFile> file;
        std::string             problem;
    };

    struct MakoProcess
    {
        std::string                executable;
        std::optional<std::string> wineExecutable;
        std::string                processName;
        std::optional<std::string> profileNamed;
        std::optional<std::string> fallbackProfile;
        bool                       followCurrentProfile = false;
    };

    enum class MakoPowerSource
    {
        Unknown,
        Handheld,
        Docked,
    };

    struct PowerSupplies
    {
        bool            battery = false;
        MakoPowerSource source  = MakoPowerSource::Unknown;
    };

    std::optional<MakoFile> parseMakoFile(const std::string& text, std::string& problem);
    MakoSettings            makoSettingsInEffect(MakoSettings settings);
    const MakoSettings&     makoSettingsFor(const MakoProfile& profile, MakoPowerSource source);
    PowerSupplies           readPowerSupplies(const std::filesystem::path& root);

    std::optional<std::string>
    makoTextWith(const std::string& text, const std::optional<MakoGlobal>& global, const std::optional<MakoProfile>& profile, std::string& problem);

    std::string makoConfigPath();
    MakoRead    readMakoFile(const std::string& path);
    bool        saveMakoFile(const std::string&                path,
                             const std::optional<MakoGlobal>&  global,
                             const std::optional<MakoProfile>& profile,
                             MakoFile&                         written,
                             std::string&                      problem);

    const MakoProfile*    makoProfileNamed(const MakoFile& file, const std::string& name);
    std::optional<size_t> makoProfileFor(const MakoFile& file, const MakoProcess& process);
    MakoProcess           thisMakoProcess();
    MakoProcess           makoProcessOf(const std::vector<std::string>& command, const std::string& program);

    std::vector<std::string> makoActiveIn(const std::vector<std::string>& games);
    MakoProfile              newMakoProfileFor(const std::string& profile, const std::vector<std::string>& games);

    std::vector<MakoProfile>   makoProfilesToSync(const MakoFile& file, const std::map<std::string, std::vector<std::string>>& gamesByProfile);
    std::optional<std::string> makoTextRenamed(const std::string& text, const std::string& from, const std::string& to, std::string& problem);
    bool syncMakoActiveIn(const std::string& path, const std::map<std::string, std::vector<std::string>>& gamesByProfile, std::string& problem);
    bool renameMakoProfile(const std::string& path, const std::string& from, const std::string& to, std::string& problem);

    std::optional<std::string> makoDllPath(const MakoGlobal& global);
    DllStatus                  makoDllStatus(const MakoGlobal& global);

    bool                makoLayerLoaded();
    std::optional<bool> makoLayerListedBy(PFN_vkEnumerateInstanceLayerProperties enumerate);
    std::optional<bool> makoLayerVisible();
    bool                makoSettingsFromEnvironment();
    std::string_view    loadedFrameGenerationLayerName();
}

#endif
