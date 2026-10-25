#ifndef FRAME_GENERATION_HPP_INCLUDED
#define FRAME_GENERATION_HPP_INCLUDED

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    inline constexpr uint32_t offMultiplier            = 1;
    inline constexpr uint32_t lowestOnMultiplier       = 2;
    inline constexpr uint32_t highestOfferedMultiplier = 20;
    inline constexpr float    lowestFlowScale          = 0.25f;
    inline constexpr float    highestFlowScale         = 1.0f;

    inline constexpr std::string_view lsfgLayerName = sizeof(void*) == 8 ? "VK_LAYER_LSFGVK_frame_generation" : "VK_LAYER_LSFGVK_frame_generation_x86";

    struct LsfgChoice
    {
        std::string_view name;
        std::string_view shown;
    };

    inline constexpr std::array<LsfgChoice, 4> lsfgColorSpaces = {{{"srgb", "sRGB"}, {"hdr10", "HDR10"}, {"scrgb", "scRGB"}, {"dcip3", "DCI-P3"}}};
    inline constexpr std::array<LsfgChoice, 6> lsfgTransferFunctions = {
        {{"srgb", "sRGB"}, {"linear", "Linear"}, {"pq", "HDR10 PQ"}, {"hlg", "HDR10 HLG"}, {"scrgbnl", "scRGB non-linear"}, {"dcip3", "DCI-P3"}}};
    inline constexpr std::array<LsfgChoice, 4> lsfgLogLevels = {{{"debug", "Debug"}, {"info", "Info"}, {"warning", "Warning"}, {"error", "Error"}}};

    struct FrameGenerationProfile
    {
        std::string                name;
        std::vector<std::string>   activeIn;
        uint32_t                   multiplier                  = 2;
        float                      flowScale                   = 1.0f;
        bool                       performanceMode             = false;
        std::optional<std::string> colorSpace;
        std::optional<std::string> transferFunction;
        bool                       overridePresentMode         = true;
        bool                       preserveSwapchainImageCount = false;

        bool operator==(const FrameGenerationProfile&) const = default;
    };

    struct FrameGenerationGlobal
    {
        std::optional<std::string> dll;
        bool                       allowHalfPrecision = true;
        std::string                logLevel           = "info";
        std::optional<std::string> logFile;

        bool operator==(const FrameGenerationGlobal&) const = default;
    };

    struct LsfgFile
    {
        FrameGenerationGlobal               global;
        std::vector<FrameGenerationProfile> profiles;
    };

    struct LsfgRead
    {
        bool                    exists = false;
        std::string             text;
        std::optional<LsfgFile> file;
        std::string             problem;
    };

    struct LsfgProcess
    {
        std::string                executable;
        std::vector<std::string>   mappedExes;
        std::string                processName;
        std::optional<std::string> profileNamed;
        std::optional<std::string> steamAppId;
    };

    struct LsfgColour
    {
        std::optional<std::string> colorSpace;
        std::optional<std::string> transferFunction;
    };

    std::optional<LsfgFile> parseLsfgFile(const std::string& text, std::string& problem);

    std::optional<std::string> lsfgTextWith(const std::string& text, const std::optional<FrameGenerationGlobal>& global,
                                            const std::optional<FrameGenerationProfile>& profile, std::string& problem);

    std::string lsfgConfigPath();
    LsfgRead    readLsfgFile(const std::string& path);
    bool        writeLsfgText(const std::string& path, const std::string& text, std::string& problem);
    bool        saveLsfgFile(const std::string& path, const std::optional<FrameGenerationGlobal>& global,
                             const std::optional<FrameGenerationProfile>& profile, LsfgFile& written, std::string& problem);

    const FrameGenerationProfile* profileNamed(const LsfgFile& file, const std::string& name);
    std::optional<size_t>         lsfgProfileFor(const LsfgFile& file, const LsfgProcess& process);
    LsfgProcess                   thisLsfgProcess();
    LsfgProcess                   lsfgProcessOf(const std::vector<std::string>& command, const std::string& program);
    bool                          lsfgSettingsFromEnvironment();

    std::string              lsfgProfileNameOf(const std::string& profile);
    bool                     madeByVkDaedalus(const std::string& lsfgProfileName);
    std::vector<std::string> lsfgActiveIn(const std::vector<std::string>& games);
    FrameGenerationProfile   newLsfgProfileFor(const std::string& profile, const std::vector<std::string>& games, uint32_t multiplier, const LsfgColour& colour);

    std::vector<FrameGenerationProfile> lsfgProfilesToSync(const LsfgFile& file, const std::map<std::string, std::vector<std::string>>& gamesByProfile);
    std::optional<std::string>          lsfgTextRenamed(const std::string& text, const std::string& from, const std::string& to, std::string& problem);
    bool syncLsfgActiveIn(const std::string& path, const std::map<std::string, std::vector<std::string>>& gamesByProfile, std::string& problem);
    bool renameLsfgProfile(const std::string& path, const std::string& from, const std::string& to, std::string& problem);
    LsfgColour             lsfgColourOf(VkColorSpaceKHR colorSpace);
    bool                   generatesFrames(const FrameGenerationProfile* profile);

    struct LsfgDllStatus
    {
        std::optional<std::string> path;
        bool                       found = false;
        std::optional<std::string> inSteamLibrary;
    };

    std::optional<std::string> lsfgDllPath(const FrameGenerationGlobal& global);
    LsfgDllStatus              lsfgDllStatus(const FrameGenerationGlobal& global);
    std::vector<std::string>   steamLibraryFolders(const std::string& libraryFoldersVdf);
    std::optional<std::string> lsfgDllInSteamLibraries();

    bool                frameGenerationLayerLoaded();
    std::optional<bool> lsfgLayerListedBy(PFN_vkEnumerateInstanceLayerProperties enumerate);
    std::optional<bool> frameGenerationLayerVisible();
}

#endif
