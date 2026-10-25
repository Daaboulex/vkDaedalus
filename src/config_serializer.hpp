#ifndef CONFIG_SERIALIZER_HPP_INCLUDED
#define CONFIG_SERIALIZER_HPP_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <set>

#include "effects/effect_config.hpp"
#include "launch_notice.hpp"

namespace vkBasalt
{
    struct ConfigParam
    {
        std::string effectName;
        std::string paramName;
        std::string value;
    };

    struct SettingRange
    {
        int lowest;
        int highest;
    };

    constexpr SettingRange MAX_EFFECTS_RANGE        = {1, 200};
    constexpr SettingRange AUTO_APPLY_DELAY_MS_RANGE = {20, 1000};
    constexpr SettingRange OVERLAY_BACKDROP_PERCENT_RANGE = {0, 80};
    constexpr SettingRange OVERLAY_SIZE_PERCENT_RANGE     = {0, 300};

    struct VkBasaltSettings
    {
        int maxEffects = 10;
        std::string toggleKey = "Home";
        std::string reloadKey = "F10";
        std::string overlayKey = "End";
        std::string previewKey = "F8";
        std::string frameGenerationKey = "Insert";
        int overlayBackdropPercent = 42;
        int overlaySizePercent = 0;
        bool enableOnLaunch = true;
        bool depthCapture = false;
        bool autoApply = true;
        int autoApplyDelayMs = 200;
        bool effectGpuTiming = false;
        bool liveReshadeUniforms = true;
        std::string logLevel = "info";
        LaunchNotice launchNotice = LaunchNotice::EveryLaunch;
        bool hud = false;
        std::string hudItems = "fps";
        std::string palette = "soft";
    };

    struct ProfileSettings
    {
        std::optional<std::string> gpus;
        std::optional<bool>        effectsAfterFrameGeneration;
    };

    struct GameSettings
    {
        bool safeAntiCheat = true;
    };

    constexpr const char* DEFAULT_PROFILE_NAME = "default";
    constexpr const char* BYPASS_PROFILE_NAME = "none";
    constexpr const char* UNKNOWN_PROGRAM_NAME = "unknown";

    enum class ProfileKind
    {
        Default,
        Named,
        Bypass
    };

    struct ActiveProfile
    {
        ProfileKind kind = ProfileKind::Default;
        std::string name = "default";
    };

    struct ShaderManagerConfig
    {
        std::vector<std::string> parentDirectories;
        std::vector<std::string> discoveredShaderPaths;
        std::vector<std::string> discoveredTexturePaths;
    };

    class ConfigSerializer
    {
    public:
        static std::string getBaseConfigDir();
        static void        moveOldConfigFolderOnce();
        static bool        makeFolder(const std::string& path);

        static std::string getConfigsDir();
        static std::string getProfilesDir();
        static std::string migrateToSharedProfilesOnce();
        static std::string migrateToSharedProfiles();

        static VkBasaltSettings loadSettings();
        static bool saveSettings(const VkBasaltSettings& settings);

        static ShaderManagerConfig loadShaderManagerConfig();
        static void scanShaderTree(const std::string& dir, std::vector<std::string>& shaderPaths, std::vector<std::string>& texturePaths);
        static bool saveShaderManagerConfig(const ShaderManagerConfig& config);

        static std::string detectGameName();
        static std::string programNameFrom(const std::string& exePath, const std::vector<std::string>& arguments);
        static std::string programNameFromCommand(const std::vector<std::string>& command);
        static std::string programWordFromCommand(const std::vector<std::string>& command);
        static bool        isWineLoaderName(const std::string& name);
        static std::string wineExecutableFrom(const std::string& exePath, const std::vector<std::string>& arguments);
        static std::vector<std::string> processArguments();
        static std::string wineLoaderProfile();

        static std::string getProfilePath(const std::string& profileName);
        static std::string getGamePath(const std::string& gameName);
        static std::string ensureGame(const std::string& gameName);
        static std::string ensureDefaultProfile();

        static std::vector<std::string>                           listProfiles();
        static std::map<std::string, std::vector<std::string>> profileUsers();

        static ActiveProfile getActiveProfile(const std::string& gameName);
        static bool          setActiveProfile(const std::string& gameName, const std::string& profileName);

        static std::string profileNameProblem(const std::string& profileName);
        static std::string createProfile(const std::string& profileName, const std::string& copyFromProfile = "");
        static std::string renameProfile(const std::string& from, const std::string& to);
        static std::string deleteProfile(const std::string& profileName);

        static ProfileSettings loadProfileSettings(const std::string& filePath);
        static GameSettings    loadGameSettings(const std::string& gameName);
        static GameSettings    loadGameSettingsFile(const std::string& path);
        static bool            saveGameSettings(const std::string& gameName, const GameSettings& settings);
        static bool            effectsAfterFrameGenerationFor(const std::string& program);

        static bool saveToPath(
            const std::string& filePath,
            const std::vector<std::string>& effects,
            const std::vector<std::string>& disabledEffects,
            const std::vector<ConfigParam>& params,
            const std::map<std::string, std::string>& effectPaths = {},
            const std::vector<PreprocessorDefinition>& preprocessorDefs = {},
            const ProfileSettings& profileSettings = {},
            const std::optional<std::set<std::string>>& liveParameterKeys = std::nullopt);

        static bool fileIsAsWeLeftIt(const std::string& filePath);
    };

}

#endif
