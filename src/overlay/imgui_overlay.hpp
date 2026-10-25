#ifndef IMGUI_OVERLAY_HPP_INCLUDED
#define IMGUI_OVERLAY_HPP_INCLUDED

#include <array>
#include <vector>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "vulkan_include.hpp"
#include "logical_device.hpp"
#include "keyboard_input.hpp"
#include "overlay/effect_compile_jobs.hpp"
#include "effects/params/effect_param.hpp"
#include "config_serializer.hpp"
#include "gpu_assignment.hpp"
#include "frame_generation.hpp"
#include "settings_manager.hpp"
#include "reshade_parser.hpp"
#include "overlay/overlay_renderer.hpp"
#include "overlay/overlay_style.hpp"
#include "overlay/frame_layout.hpp"
#include "overlay/desktop_scale.hpp"

struct ImGuiContext;

namespace vkBasalt
{
    inline bool isPrintableAscii(unsigned int c)
    {
        return c >= 32 && c < 127;
    }

    void renderShaderTestResults(const std::vector<ShaderTestResult>& results);

    class Effect;
    class EffectRegistry;
    struct LogicalSwapchain;

    struct GlobalDefinitionDraft
    {
        char        name[64]  = "";
        char        value[64] = "";
        std::string error;
    };

    struct OverlayGpu
    {
        uint32_t    number = 0;
        std::string id;
        std::string name;
        std::string status;
        bool        runsChain = false;
    };

    struct HelperCopies
    {
        uint32_t gpu       = 0;
        float    inMs      = -1.0f;
        float    effectsMs = -1.0f;
        float    outMs     = -1.0f;
    };

    struct LatencyInputs
    {
        std::vector<double>  lastSecondMs;
        std::vector<double>  lastTenSecondsMs;
        bool                 effectsOn           = true;
        bool                 effectTimingSetting = false;
        bool                 effectTimingOn      = false;
        std::optional<float> effectsSpanMilliseconds;
        bool                 handsOff = false;
        std::optional<float> roundTripMilliseconds;
        std::vector<uint32_t> tripGpus;
        std::vector<HelperCopies> helperCopies;
        bool                 rowsAfterReturn = false;
    };

    enum class OverlaySection
    {
        Effects,
        FrameGeneration,
        Gpus,
        Performance,
        Shaders,
        Keys,
        Settings,
        Debug
    };

    struct OverlayState
    {
        std::vector<std::string> currentConfigEffects;
        std::vector<std::string> defaultConfigEffects;
        std::map<std::string, std::string> effectPaths;
        std::map<std::string, float> effectGpuMilliseconds;
        std::string configFileName;
        bool effectsEnabled = true;
        std::vector<OverlayGpu> gpus;
        std::vector<OverlayGpu> addableGpus;
    };

    class ImGuiOverlay
    {
    public:
        explicit ImGuiOverlay(LogicalDevice* device);
        ~ImGuiOverlay();

        void toggle();
        void toggleFrameGeneration();
        std::optional<uint32_t> frameGenerationMultiplierHere();
        void setPreviewing(bool held) { previewingByKey = held; }
        bool isPreviewing() const { return previewingByKey || previewingByMouse; }
        void switchOff(const std::string& why);
        bool isVisible() const { return visible; }
        bool showsCard(OverlaySection section) const
        {
            return visible && !isPreviewing() && openSection == section && !(section == OverlaySection::Effects && inSelectionMode);
        }
        bool addsEffects() const { return visible && !isPreviewing() && openSection == OverlaySection::Effects && inSelectionMode; }
        void setEffectsEnabled(bool on) { state.effectsEnabled = on; }
        void setFirstLaunchOfTheGame(bool first) { firstLaunchOfTheGame = first; }

        void updateState(OverlayState newState);

        using ChainCompiles = std::function<std::vector<EffectCompileJobs::Compile>(const std::vector<std::string>& effectNames)>;
        void setChainCompiles(ChainCompiles provider) { chainCompiles = std::move(provider); }
        void processAddedEffects();
        void cancelAddedEffects() { effectCompileJobs.cancel(); }

        std::vector<std::unique_ptr<EffectParam>> getModifiedParams();
        bool hasModifiedParams() const { return applyRequested; }
        void clearApplyRequest()
        {
            applyRequested  = false;
            paramsDirty     = false;
            waitingForApply = false;
        }

        bool hasPendingConfig() const { return !pendingConfigPath.empty(); }
        std::string getPendingConfigPath() const { return pendingConfigPath; }
        void clearPendingConfig() { pendingConfigPath.clear(); }

        bool hasToggleEffectsRequest() const { return toggleEffectsRequested; }
        bool takeCloseRequest() { return std::exchange(closeRequested, false); }
        bool takeOffInThisRunRequest() { return std::exchange(offInThisRunRequested, false); }
        void comeBackInThisRun();
        void clearToggleEffectsRequest() { toggleEffectsRequested = false; }

        void setEffectRegistry(EffectRegistry* registry) { pEffectRegistry = registry; }

        void setGameProfile(const std::string& gameName, const std::string& profileName, const std::string& profilePath)
        {
            activeGameName = gameName;
            activeProfileName = profileName;
            activeProfilePath = profilePath;
        }

        void markDirty() { paramsDirty = true; lastChangeTime = std::chrono::steady_clock::now(); }
        void markEdited() { markDirty(); markDefinitionsEdited(); }
        void markDefinitionsEdited() { profileDirty = true; saveFailed = false; lastChangeTime = std::chrono::steady_clock::now(); }
        void markDefinitionChanged() { markDefinitionsEdited(); waitingForApply = true; }
        void requestApply() { applyRequested = true; }
        void settleEdits();

        bool hasSettingsSaved() const { return settingsSaved; }
        void clearSettingsSaved() { settingsSaved = false; }

        bool hasShaderPathsChanged() const { return shaderPathsChanged; }
        void clearShaderPathsChanged() { shaderPathsChanged = false; }

        std::vector<std::string> getActiveEffects() const;

        const std::vector<std::string>& getSelectedEffects() const;

        void setSelectedEffects(const std::vector<std::string>& effects,
                                const std::vector<std::string>& disabledEffects = {});

        struct Frame
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence         fence         = VK_NULL_HANDLE;
        };
        Frame drawFrame(LogicalSwapchain& swapchain, uint32_t imageIndex);

        void     prepareSwapchain(LogicalSwapchain& swapchain);
        void     detach(const SwapchainOverlay& overlay);
        uint64_t drawPatienceNs() const;

    private:
        void waitForAllDraws();

        void renderAddEffectsView();
        void renderSettingsView();
        void renderShaderManagerView();
        void renderShaderTestSection();
        void startShaderTest();
        void processShaderTest();
        void renderMainView(const KeyboardState& keyboard);
        void buildFrame(const KeyboardState& keyboard);
        void renderGpusView();
        void renderFrameGenerationView();
        void renderFrameGenerationGlobals();
        void renderPipelineStrip();
        void exportReport();
        void renderGlobalDefinitionsView();
        void renderEffectRows(const std::string& effectName);
        void saveFrameGeneration(const std::optional<FrameGenerationGlobal>& global, const std::optional<FrameGenerationProfile>& profile);
        bool readFrameGenerationOnce();
        void refreshFrameGenerationFile(bool now);
        void frameGenerationAssignmentsChanged(const std::string& renamedFrom);
        std::vector<std::string> frameGenerationGlobalsWaiting() const;
        std::string frameGenerationBlocker(bool& notInstalled);
        const FrameGenerationProfile* frameGenerationProfileInUse() const;
        bool frameGenerationLiveHere() const;
        bool effectsAfterFrameGenerationSaved();
        void renderGpuMenu(const std::string& effectName, uint32_t gpuBefore);
        void renderGpuChoices(const std::string& effectName, uint32_t gpuBefore);
        void changeGpus(const std::vector<GpuListEntry>& list, const std::vector<std::pair<std::string, uint32_t>>& rows);
        void renderGpuReadings();
        void renderRoundTrip();
        void         renderFrameTopBar(const FrameLayout& layout, const OverlayPalette& palette);
        void         renderWarnings(const LayoutRect& rect, const OverlayPalette& palette);
        void         renderProfileTile(const LayoutRect& rect, const OverlayPalette& palette);
        void         renderEffectsSwitchTile(const LayoutRect& rect, const OverlayPalette& palette);
        void         renderSafeAntiCheatTile(const LayoutRect& rect, const OverlayPalette& palette);
        void         renderSectionTiles(const FrameLayout& layout, const OverlayPalette& palette);
        void         renderRightTiles(const FrameLayout& layout, const OverlayPalette& palette);
        void         renderKeysCard(const KeyboardState& keyboard);
        void         applyFrameScale(float scale);
        DesktopScale desktopScale() const;
        void saveOverlaySettings();
        void renderLatencyView();
        void renderDebugView();
        struct EffectsRunning
        {
            size_t on  = 0;
            size_t run = 0;
        };
        EffectsRunning effectsRunning();
        bool launchNoticeShowing(std::chrono::steady_clock::time_point now);
        void renderLaunchNotice(std::chrono::steady_clock::time_point now);
        bool hudShowing() const;
        std::optional<std::string> fixedConfigFile() const;
        void renderHud();
        bool shortNoticeShowing(std::chrono::steady_clock::time_point now);
        void renderShortNotice(std::chrono::steady_clock::time_point now);
        void renderNotice(const char* title, const std::string& line, bool problem, const char* mapName, std::chrono::steady_clock::time_point start,
                          float duration, std::chrono::steady_clock::time_point now);
        void switchFrameGeneration(bool on);

        LogicalDevice* pLogicalDevice;
        ImGuiContext*  context = nullptr;
        EffectRegistry* pEffectRegistry = nullptr;
        std::unique_ptr<OverlayRenderer>     renderer;
        OverlayFonts                         fonts;
        ImGuiStyle                           baseStyle;
        float                                appliedScale      = 0.0f;
        bool                                 previewingByKey   = false;
        bool                                 previewingByMouse = false;
        bool                                 profileMenuWasOpen = false;
        bool                                 profileMenuClosedByPress = false;
        float                                frameShown        = 1.0f;
        float                                cardShown         = 1.0f;
        std::optional<OverlaySection>        shownSection;
        std::optional<std::chrono::steady_clock::time_point> appliedAt;
        std::vector<WaylandOutputFacts>      desktopOutputs;
        std::optional<float>                 desktopXftDpi;
        std::vector<const SwapchainOverlay*> attached;
        float                                averageFrameMs = 8.0f;
        OverlayState state;
        LatencyInputs latency;
        struct PendingEffect
        {
            std::string instanceName;
            std::string effectType;
        };
        std::vector<PendingEffect> pendingAddEffects;
        void startAddedEffects(EffectAddBatch batch);
        bool inSelectionMode = false;
        std::optional<size_t> insertPosition;
        char addEffectsSearch[64] = "";
        GlobalDefinitionDraft globalDefinitionDraft;

        std::vector<std::string> shaderMgrParentDirs;
        std::string              shaderMgrRescanProblem;
        bool                     shaderMgrSaveFailed = false;
        std::vector<std::string> shaderMgrShaderPaths;
        std::vector<std::string> shaderMgrTexturePaths;
        bool shaderMgrInitialized = false;

        bool shaderTestRunning = false;
        bool shaderTestComplete = false;
        size_t shaderTestCurrentIndex = 0;
        int shaderTestDuplicateCount = 0;
        struct ShaderTestJob
        {
            std::string effectName;
            std::string filePath;
        };
        std::vector<ShaderTestJob> shaderTestQueue;
        std::vector<ShaderTestResult> shaderTestResults;
        struct ShaderTestRun
        {
            std::mutex                    mutex;
            std::vector<ShaderTestResult> finished;
            std::atomic<bool>             cancelled{false};
        };
        std::shared_ptr<ShaderTestRun> shaderTestRun;
        std::future<void>              shaderTestDone;
        void stopShaderTest();
        std::set<std::string> depthEffectPaths;

        enum class KeyBinding
        {
            None,
            Toggle,
            Reload,
            Overlay,
            Preview,
            FrameGeneration
        };
        KeyBinding listeningForKey = KeyBinding::None;
        std::string keyBindingProblem;
        bool settingsSaved = false;
        bool settingsSaveFailed = false;
        bool shaderPathsChanged = false;

        int debugWindowTab = 0;
        bool  debugLogFilters[static_cast<size_t>(LogLevel::None)] = {false, false, true, true, true};
        bool& logFilter(LogLevel level)
        {
            return debugLogFilters[static_cast<size_t>(level)];
        }
        char debugLogSearch[128] = "";
        std::optional<size_t> dragSourceIndex;
        std::optional<size_t> dragTargetIndex;
        bool isDragging = false;
        bool applyRequested = false;
        bool toggleEffectsRequested = false;
        bool closeRequested         = false;
        bool        hudLogged             = false;
        std::string exportedReport;
        bool        offInThisRunRequested = false;
        bool        comeBackSwitchPending = false;
        std::string profileBeforeOff;
        std::string comeBackProfile;
        bool paramsDirty = false;
        bool waitingForApply = false;
        std::chrono::steady_clock::time_point lastChangeTime;
        bool visible = false;
        bool firstLaunchOfTheGame = false;
        bool launchNoticeOver = false;
        bool launchNoticeProblem = false;
        std::optional<std::chrono::steady_clock::time_point> launchNoticeStart;
        std::string launchNoticeLine;
        std::optional<std::chrono::steady_clock::time_point> shortNoticeStart;
        std::string shortNoticeLine;
        bool shortNoticeProblem = false;
        uint32_t frameGenerationOnMultiplier = 0;
        std::string frameGenerationGpuName;
        bool initialized = false;
        bool backendInitialized = false;
        std::optional<OverlaySection> openSection = OverlaySection::Effects;
        uint32_t currentWidth = 1920;
        uint32_t currentHeight = 1080;
        VkFormat currentFormat = VK_FORMAT_UNDEFINED;
        uint32_t currentImageCount = 0;
        std::string pendingConfigPath;

        std::string activeGameName;
        bool                                  frameGenerationRead = false;
        std::optional<LsfgFile>               frameGenerationFile;
        std::optional<std::filesystem::file_time_type> frameGenerationFileTime;
        std::chrono::steady_clock::time_point          frameGenerationCheckedAt{};
        std::string                           frameGenerationFileProblem;
        std::optional<std::string>            frameGenerationProfileHere;
        std::string                           frameGenerationProblem;
        std::optional<FrameGenerationProfile> frameGenerationDraft;
        std::optional<FrameGenerationProfile> frameGenerationAtStart;
        std::optional<FrameGenerationGlobal>  frameGenerationGlobalAtStart;
        std::optional<LsfgDllStatus>          frameGenerationDll;
        VkColorSpaceKHR                       swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkPresentModeKHR                      swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkImageUsageFlags                     swapchainAppUsage    = 0;
        VkImageUsageFlags                     swapchainUsage       = 0;
        uint32_t                              swapchainMinImages   = 0;
        bool                                  swapchainMutable     = false;
        std::string chainCrossings;
        std::string                                        frameOrderProfilePath;
        bool                                               effectsAfterFrameGeneration = false;
        std::string activeProfileName;
        std::string activeProfilePath;
        bool profileDirty = false;
        bool saveFailed = false;
        std::vector<std::string>                         profileList;
        std::map<std::string, std::vector<std::string>> profileGames;
        bool                                             profileListStale = true;
        void                                             refreshProfileList();

        void autoSaveProfile();
        void markLiveValuesChanged();
        void markEffectValuesChanged(const std::string& effectName);
        void collectSaveData(
            std::vector<std::string>& effects,
            std::vector<std::string>& disabledEffects,
            std::vector<ConfigParam>& params,
            std::map<std::string, std::string>& effectPaths,
            std::vector<PreprocessorDefinition>& allDefs,
            std::set<std::string>& liveParameterKeys);

        ChainCompiles     chainCompiles;
        EffectCompileJobs effectCompileJobs;
        std::optional<std::chrono::steady_clock::time_point> compilingSince;
    };

}

#endif
