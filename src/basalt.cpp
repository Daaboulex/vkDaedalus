#include "vulkan_include.hpp"

#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <cstdio>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <sys/stat.h>
#include <dlfcn.h>

#include "util.hpp"
#include "keyboard_input.hpp"
#include "mouse_input.hpp"
#include "keyboard_input_wayland.hpp"
#include "mouse_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "input_blocker.hpp"
#include "wayland_display.hpp"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#include "vulkan/vulkan_wayland.h"

#include "config_paths.hpp"
#include "logical_device.hpp"
#include "logical_swapchain.hpp"
#include "helper_device.hpp"
#include "frame_generation.hpp"
#include "frame_generation_place.hpp"
#include "latency_stats.hpp"

#include "image_view.hpp"
#include "sampler.hpp"
#include "framebuffer.hpp"
#include "descriptor_set.hpp"
#include "shader.hpp"
#include "graphics_pipeline.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "memory.hpp"
#include "config_serializer.hpp"
#include "program_scope.hpp"
#include "anticheat_files.hpp"
#include "layer_queue.hpp"
#include "shader_cache.hpp"
#include "layer_lock.hpp"
#include "reshade_parser.hpp"
#include "settings_manager.hpp"
#include "fake_swapchain.hpp"
#include "chain_rows.hpp"
#include "renderpass.hpp"
#include "format.hpp"
#include "logger.hpp"
#include "reshade_effect_files.hpp"

#include "effects/effect.hpp"
#include "effects/effect_reshade.hpp"
#include "effects/effect_shared_copy.hpp"
#include "effects/effect_transfer.hpp"
#include "effects/builtin/builtin_effects.hpp"
#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"

#define VKBASALT_NAME "VK_LAYER_VKDAEDALUS_post_processing"

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_BASALT_EXPORT __attribute__((visibility("default")))
#else
#error "Unsupported platform!"
#endif

namespace vkBasalt
{
    std::shared_ptr<Config> pBaseConfig = nullptr;
    std::shared_ptr<Config> pConfig = nullptr;
    std::shared_ptr<Config> pGameConfig = nullptr;
    EffectRegistry effectRegistry;


    struct LayerInstance
    {
        InstanceDispatch         vki;
        VkInstance               handle;
        uint32_t                 apiVersion;
        PFN_vkLayerCreateDevice  layerCreateDevice  = nullptr;
        PFN_vkLayerDestroyDevice layerDestroyDevice = nullptr;
        FrameGenerationPlace     frameGenerationPlace = FrameGenerationPlace::NotLoaded;
        bool                     presents             = true;
        std::vector<std::string> layersBelow;
    };

    std::unordered_map<void*, LayerInstance>                              instanceMap;
    std::unordered_map<void*, std::shared_ptr<LogicalDevice>>             deviceMap;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    LayerInstance* findInstance(void* key)
    {
        const auto it = instanceMap.find(key);
        return it == instanceMap.end() ? nullptr : &it->second;
    }

    LogicalDevice* findDevice(void* key)
    {
        const auto it = deviceMap.find(key);
        return it == deviceMap.end() ? nullptr : it->second.get();
    }

    LogicalSwapchain* findSwapchain(VkSwapchainKHR swapchain)
    {
        const auto it = swapchainMap.find(swapchain);
        return it == swapchainMap.end() ? nullptr : it->second.get();
    }

    struct OffInThisRun
    {
        bool                     off            = false;
        bool                     effectsWereOn  = true;
        std::set<VkSwapchainKHR> handedBack;
        std::set<VkSwapchainKHR> outOfDateDue;
    };

    OffInThisRun& offInThisRun()
    {
        static OffInThisRun state;
        return state;
    }

    LayerLock globalLock;
    using scoped_lock = std::lock_guard<LayerLock>;

    template<typename DispatchableType>
    void* GetKey(DispatchableType inst)
    {
        return *(void**) inst;
    }

    struct CachedEffectsData
    {
        std::vector<std::string> currentConfigEffects;
        std::vector<std::string> defaultConfigEffects;
        std::map<std::string, std::string> effectPaths;
        std::string configPath;
        bool initialized = false;
    };
    CachedEffectsData cachedEffects;

    struct ResizeDebounceState
    {
        std::chrono::steady_clock::time_point lastResizeTime;
        bool pending = false;
    };
    ResizeDebounceState resizeDebounce;
    bool chainBuiltOnce = false;
    constexpr int64_t RESIZE_DEBOUNCE_MS = 200;

    bool handleKeyPress(uint32_t keySymbol, bool& wasPressed)
    {
        if (isKeyPressed(keySymbol))
        {
            if (!wasPressed)
            {
                wasPressed = true;
                return true;
            }
        }
        else
        {
            wasPressed = false;
        }
        return false;
    }

    struct DepthState
    {
        VkImageView imageView = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    DepthState getDepthState(LogicalDevice* pLogicalDevice)
    {
        DepthState state;
        if (!settingsManager.getDepthCapture())
            return state;
        for (const auto& depthImage : pLogicalDevice->depthImages)
        {
            if (depthImage.view == VK_NULL_HANDLE)
                continue;

            state.imageView = depthImage.view;
            state.image = depthImage.image;
            state.format = depthImage.format;
            break;
        }
        return state;
    }

    void reallocateCommandBuffers(
        LogicalDevice* pLogicalDevice,
        LogicalSwapchain* pLogicalSwapchain,
        const DepthState& depth)
    {
        pLogicalSwapchain->waitForEffects();
        pLogicalSwapchain->freeCommandBuffers(pLogicalSwapchain->commandBuffersEffect);
        pLogicalSwapchain->effectDepthImage = depth.image;

        try
        {
            pLogicalSwapchain->commandBuffersEffect = recordCommandBuffers(
                pLogicalDevice, pLogicalSwapchain->imageCount, pLogicalSwapchain->effects,
                depth.image, depth.imageView, depth.format,
                pLogicalSwapchain->timingQueryPool,
                static_cast<uint32_t>(pLogicalSwapchain->timingEffectNames.size()));
        }
        catch (const std::exception& e)
        {
            Logger::err(std::string("recording the effect command buffers failed, so frames pass through without effects: ")
                        + e.what());
        }
    }

    static std::string detectedGameName;
    static std::string activeProfileName;
    static std::string activeProfilePath;
    static bool        firstLaunchOfTheGame = false;
    static std::string safeAntiCheatSource;

    void readSafeAntiCheat()
    {
        const GameSettings gs = safeAntiCheatSource.empty() ? GameSettings{} : ConfigSerializer::loadGameSettingsFile(safeAntiCheatSource);
        settingsManager.setSafeAntiCheat(gs.safeAntiCheat);
        const std::string& lock = settingsManager.getSafeAntiCheatLock();
        if (!lock.empty())
            Logger::info("Safe Anti-Cheat is locked on, since " + lock
                         + (gs.safeAntiCheat ? "" : "; " + safeAntiCheatSource + " says false, which is ignored"));
        else if (gs.safeAntiCheat)
            Logger::info("Safe Anti-Cheat is on: depth capture off, effects that read depth left out");
        else
            Logger::info("Safe Anti-Cheat is off for this game, as " + safeAntiCheatSource + " says");
    }

    void initConfigs()
    {
        if (pBaseConfig != nullptr)
            return;
        ConfigSerializer::moveOldConfigFolderOnce();
        ConfigSerializer::migrateToSharedProfilesOnce();

        if (conflictingLayerLoaded())
        {
            Logger::warn("the unforked vkBasalt is active in this process as well, from " + conflictingLayerPath()
                         + ": ENABLE_VKBASALT is set or a Proton build turns it on, so effects can apply twice. Leave "
                           "ENABLE_VKBASALT out of the game's launch options; this layer needs only ENABLE_VKDAEDALUS.");
        }

        {
            std::string baseDir = ConfigSerializer::getBaseConfigDir();
            if (!baseDir.empty())
                ConfigSerializer::makeFolder(baseDir);
        }

        settingsManager.initialize();

        pBaseConfig = std::make_shared<Config>();

        detectedGameName = ConfigSerializer::detectGameName();

        std::string currentConfigPath;

        const char* envConfig = std::getenv("VKDAEDALUS_CONFIG_FILE");
        if (envConfig && *envConfig)
        {
            currentConfigPath = envConfig;
            Logger::info("config from env: " + currentConfigPath);
        }
        else
        {
            if (programBypassed())
            {
                pConfig = pBaseConfig;
                Logger::info("game: " + detectedGameName + " | profile: " + BYPASS_PROFILE_NAME
                             + ", the layer stays out of this program");
                return;
            }

            std::error_code unreadable;
            firstLaunchOfTheGame = !std::filesystem::exists(ConfigSerializer::getGamePath(detectedGameName), unreadable);
            const std::string gamePath = ConfigSerializer::ensureGame(detectedGameName);
            if (!gamePath.empty())
                pGameConfig = std::make_shared<Config>(gamePath);
            if (firstLaunchOfTheGame && !gamePath.empty())
            {
                std::string problem;
                if (!syncLsfgActiveIn(lsfgConfigPath(), ConfigSerializer::profileUsers(), problem))
                    Logger::warn("lsfg-vk's file keeps its game lists, so frame generation in " + detectedGameName + " may follow another profile: " + problem);
            }

            const ActiveProfile profile = ConfigSerializer::getActiveProfile(detectedGameName);
            activeProfileName            = profile.name;
            activeProfilePath            = ConfigSerializer::getProfilePath(activeProfileName);
            if (profile.kind == ProfileKind::Named && !std::filesystem::exists(activeProfilePath, unreadable))
            {
                Logger::warn(".active_profiles names profile " + profile.name + " for " + detectedGameName + " but "
                             + activeProfilePath + " does not exist, so default runs. Profile names are case-sensitive; "
                               "only none keeps the layer out.");
                activeProfileName = DEFAULT_PROFILE_NAME;
                activeProfilePath = ConfigSerializer::getProfilePath(DEFAULT_PROFILE_NAME);
            }
            if (activeProfileName == DEFAULT_PROFILE_NAME)
                activeProfilePath = ConfigSerializer::ensureDefaultProfile();

            if (!activeProfilePath.empty())
            {
                currentConfigPath = activeProfilePath;
                Logger::info("game: " + detectedGameName + " | profile: " + activeProfileName);
            }
        }

        if (!currentConfigPath.empty())
        {
            std::ifstream file(currentConfigPath);
            if (file.good())
            {
                pConfig = std::make_shared<Config>(currentConfigPath);
                pConfig->setFallback(pBaseConfig.get());

                setMemorySoftLimitBytes(static_cast<VkDeviceSize>(pConfig->getOption<int32_t>("vramSoftLimitMB", 0))
                                        * 1024 * 1024);
                Logger::info("current config: " + currentConfigPath);
            }
            else
            {
                pConfig = pBaseConfig;
            }
        }
        else
        {
            pConfig = pBaseConfig;
        }

        if (const std::optional<ShippedAntiCheat> shipped = detectShippedAntiCheat())
            settingsManager.lockSafeAntiCheat(shipped->name + " ships with this game (" + shipped->file + ")");
        const bool configNamedByUser = envConfig && *envConfig;
        safeAntiCheatSource = configNamedByUser ? currentConfigPath : ConfigSerializer::getGamePath(detectedGameName);
        readSafeAntiCheat();

        effectRegistry.initialize(pConfig.get());
    }

    void switchConfig(const std::string& configPath)
    {
        Logger::info("switching to config: " + configPath);

        pConfig = std::make_shared<Config>(configPath);
        pConfig->setFallback(pBaseConfig.get());

        if (pBaseConfig)
            pBaseConfig->clearOverrides();

        effectRegistry.initialize(pConfig.get());

        Logger::info("switched to config: " + configPath);
    }

    void getAvailableEffects(Config* pConfig,
                             std::vector<std::string>& currentConfigEffects,
                             std::vector<std::string>& defaultConfigEffects,
                             std::map<std::string, std::string>& effectPaths)
    {
        if (cachedEffects.initialized && cachedEffects.configPath == pConfig->getConfigFilePath())
        {
            currentConfigEffects = cachedEffects.currentConfigEffects;
            defaultConfigEffects = cachedEffects.defaultConfigEffects;
            effectPaths = cachedEffects.effectPaths;
            return;
        }

        currentConfigEffects.clear();
        defaultConfigEffects.clear();
        effectPaths.clear();

        std::set<std::string> knownEffects;

        auto configEffects = pConfig->getEffectDefinitions();
        for (const auto& [name, path] : configEffects)
        {
            currentConfigEffects.push_back(name);
            effectPaths[name] = path;
            knownEffects.insert(name);
        }

        if (pBaseConfig && pBaseConfig->getConfigFilePath() != pConfig->getConfigFilePath())
        {
            auto defaultEffects = pBaseConfig->getEffectDefinitions();
            for (const auto& [name, path] : defaultEffects)
            {
                if (knownEffects.find(name) == knownEffects.end())
                {
                    defaultConfigEffects.push_back(name);
                    effectPaths[name] = path;
                    knownEffects.insert(name);
                }
            }
        }

        ShaderManagerConfig shaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();
        for (const auto& shaderPath : shaderMgrConfig.discoveredShaderPaths)
        {
            std::error_code ec;
            for (const auto& file : reshadeEffectFilesIn(shaderPath, ec))
            {
                std::string effectName = file.stem().string();
                if (knownEffects.find(effectName) != knownEffects.end())
                    continue;

                defaultConfigEffects.push_back(effectName);
                effectPaths[effectName] = file.string();
                knownEffects.insert(effectName);
            }
            if (ec)
                Logger::warn("failed to scan shader path " + shaderPath + ": " + ec.message());
        }

        std::sort(defaultConfigEffects.begin(), defaultConfigEffects.end());

        cachedEffects.currentConfigEffects = currentConfigEffects;
        cachedEffects.defaultConfigEffects = defaultConfigEffects;
        cachedEffects.effectPaths = effectPaths;
        cachedEffects.configPath = pConfig->getConfigFilePath();
        cachedEffects.initialized = true;
    }

    std::vector<std::string> leaveOutFrom(LogicalDevice* pLogicalDevice, std::vector<std::string> effects, size_t kept, const std::string& reason)
    {
        if (effects.size() <= kept)
            return effects;
        for (size_t i = kept; i < effects.size(); i++)
            effectRegistry.setEffectOnDevice(effects[i], pLogicalDevice, {reason, false, {}, gameGpu, ""});
        Logger::warn(std::to_string(effects.size() - kept) + " effect(s) from " + effects[kept] + " on are left out: " + reason);
        effects.resize(kept);
        return effects;
    }

    std::vector<std::string> withoutFailed(std::vector<std::string> effects)
    {
        effects.erase(std::remove_if(effects.begin(), effects.end(), [](const std::string& name) { return effectRegistry.hasEffectFailed(name); }),
                      effects.end());
        return effects;
    }

    std::vector<std::string> withinEffectLimit(LogicalDevice* pLogicalDevice, std::vector<std::string> effects)
    {
        const size_t limit = static_cast<size_t>(settingsManager.getMaxEffects());
        return leaveOutFrom(pLogicalDevice, std::move(effects), limit,
                            "past the limit of " + std::to_string(limit) + " effects set in Settings");
    }

    bool appendFakeSwapchainImages(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, size_t neededSlots)
    {
        if (neededSlots <= pLogicalSwapchain->maxEffectSlots)
            return true;

        const size_t hardLimit = static_cast<size_t>(settingsManager.getMaxEffects());
        if (neededSlots > hardLimit)
        {
            Logger::warn("refusing to allocate room for " + std::to_string(neededSlots) + " effects, maxEffects is "
                         + std::to_string(hardLimit));
            return false;
        }

        const size_t   extraSlots  = neededSlots - pLogicalSwapchain->maxEffectSlots;
        const uint32_t extraImages = pLogicalSwapchain->imageCount * extraSlots;

        VkDeviceMemory       block = VK_NULL_HANDLE;
        std::vector<VkImage> grown =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, extraImages, block);

        if (grown.empty())
        {
            Logger::warn("could not allocate room for " + std::to_string(neededSlots) + " effects, keeping "
                         + std::to_string(pLogicalSwapchain->maxEffectSlots));
            return false;
        }

        pLogicalSwapchain->fakeImageMemory.push_back(block);
        pLogicalSwapchain->fakeImages.insert(pLogicalSwapchain->fakeImages.end(), grown.begin(), grown.end());
        pLogicalSwapchain->maxEffectSlots = neededSlots;

        Logger::debug("grew the effect image pool to " + std::to_string(neededSlots) + " slots");
        return true;
    }

    std::shared_ptr<Effect> buildRow(LogicalDevice*                                   buildOn,
                                     LogicalDevice*                                   shownOn,
                                     LogicalSwapchain*                                pLogicalSwapchain,
                                     Config*                                          pConfig,
                                     const std::string&                               effectName,
                                     const std::vector<VkImage>&                      firstImages,
                                     const std::vector<VkImage>&                      secondImages,
                                     const std::shared_ptr<SharedReshadeTexturePool>& sharedTexturePool,
                                     uint32_t                                         gpu,
                                     const std::string&                               gpuRefusal)
    {
        const VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->format);
        const VkFormat srgbFormat  = convertToSRGB(pLogicalSwapchain->format);

        if (effectRegistry.hasEffectFailed(effectName))
            return nullptr;

        std::string effectType = effectRegistry.getEffectType(effectName);
        if (effectType.empty())
            effectType = effectName;

        if (const auto* def = BuiltInEffects::instance().getDef(effectType))
        {
            for (auto* param : effectRegistry.getParametersForEffect(effectName))
            {
                for (const auto& [suffix, value] : param->serialize())
                {
                    std::string key = suffix.empty() ? param->name : (param->name + suffix);
                    pConfig->setOverride(key, value);
                }
            }
            try
            {
                const VkFormat format = def->usesSrgbFormat ? srgbFormat : unormFormat;
                effectRegistry.setEffectOnDevice(effectName, shownOn, {"", false, {}, gpu, gpuRefusal});
                return def->factory(buildOn, format, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig);
            }
            catch (const std::exception& e)
            {
                Logger::err("Failed to create built-in effect " + effectName + ": " + e.what());
                effectRegistry.setEffectError(effectName, e.what());
                return nullptr;
            }
        }

        const std::string effectPath = effectRegistry.getEffectFilePath(effectName);
        effectRegistry.refreshParameters(effectName);
        const auto customDefs = effectRegistry.getCompilePreprocessorDefs(effectName);
        try
        {
            auto effect = std::make_shared<ReshadeEffect>(buildOn, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                                                          pLogicalSwapchain->swapchainCreateInfo.imageColorSpace, firstImages, secondImages,
                                                          &effectRegistry, sharedTexturePool, effectName, effectPath, customDefs);
            effectRegistry.setEffectOnDevice(effectName, shownOn, {"", false, effect->deviceNotices(), gpu, gpuRefusal});
            return effect;
        }
        catch (const SafeAntiCheatRefusal& e)
        {
            Logger::info(e.what());
            effectRegistry.setEffectOnDevice(effectName, shownOn, {e.what(), true, {}, gpu, gpuRefusal});
            return nullptr;
        }
        catch (const DepthOnOtherGpuRefusal& e)
        {
            throw ChainRefusal(e.what());
        }
        catch (const DeviceRefusal& e)
        {
            Logger::warn(e.what());
            effectRegistry.setEffectOnDevice(effectName, shownOn, {e.what(), false, {}, gpu, gpuRefusal});
            return nullptr;
        }
        catch (const std::exception& e)
        {
            Logger::err("Failed to create ReshadeEffect " + effectName + ": " + e.what());
            effectRegistry.setEffectError(effectName, e.what());
            return nullptr;
        }
    }

    struct OffloadPlan
    {
        size_t                 headRows = 0;
        std::vector<ChainPart> parts;
    };

    std::optional<OffloadPlan> offloadPlan(LogicalDevice* pLogicalDevice, const std::vector<std::string>& effectStrings)
    {
        if (!pLogicalDevice->offload || pLogicalDevice->offload->onGameGpuUntilReload || pLogicalDevice->offload->helpers.empty())
            return std::nullopt;
        const GpuPlan               rows = effectRegistry.gpuPlan();
        const std::vector<uint32_t> gpus = gpusOfRows(rows.rows, rows.ownGpus, effectStrings);
        if (std::all_of(gpus.begin(), gpus.end(), [](uint32_t gpu) { return gpu == gameGpu; }))
            return std::nullopt;

        OffloadPlan plan;
        size_t      row = 0;
        while (row < gpus.size() && gpus[row] == gameGpu)
            row++;
        plan.headRows = row;
        while (row < gpus.size())
        {
            const uint32_t gpu   = gpus[row];
            size_t         end   = row;
            while (end < gpus.size() && gpus[end] == gpu)
                end++;
            ChainPart part{nullptr, gpu, row, end - row};
            if (gpu != gameGpu)
            {
                for (const auto& [physical, helper] : pLogicalDevice->offload->helpers)
                    if (helper->gpu == gpu)
                        part.helper = helper;
                if (!part.helper)
                    return std::nullopt;
            }
            plan.parts.push_back(part);
            row = end;
        }
        if (plan.parts.back().helper)
            plan.parts.push_back({nullptr, gameGpu, gpus.size(), 0});
        return plan;
    }

    std::vector<std::string> createEffectsForSwapchain(
        LogicalSwapchain* pLogicalSwapchain,
        LogicalDevice* pLogicalDevice,
        Config* pConfig,
        const std::vector<std::string>& effectStrings)
    {
        const uint32_t count = pLogicalSwapchain->imageCount;
        const auto     slot  = [&](size_t s) {
            return std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin() + count * s, pLogicalSwapchain->fakeImages.begin() + count * (s + 1));
        };

        if (effectStrings.empty())
        {
            pLogicalSwapchain->effects.push_back(
                std::shared_ptr<Effect>(new TransferEffect(pLogicalDevice, pLogicalSwapchain->imageExtent, slot(0), pLogicalSwapchain->images)));
            return {};
        }

        const auto sharedTexturePool = std::make_shared<SharedReshadeTexturePool>(pLogicalDevice);
        std::vector<std::shared_ptr<SharedReshadeTexturePool>> segmentPools;
        std::vector<std::string>                               dropped;
        bool                                                   segmentEmptied = false;
        size_t      headRows = effectStrings.size();
        std::string chainRefusal;
        if (const std::optional<OffloadPlan> plan = offloadPlan(pLogicalDevice, effectStrings))
        {
            try
            {
                const SegmentEffectBuilder buildRows = [&](LogicalDevice* device, uint32_t gpu, size_t firstRow, size_t rows,
                                                           const std::vector<VkImage>& slots, uint32_t imageCount) {
                    segmentPools.push_back(std::make_shared<SharedReshadeTexturePool>(device));
                    const auto local = [&](size_t s) {
                        return std::vector<VkImage>(slots.begin() + imageCount * s, slots.begin() + imageCount * (s + 1));
                    };
                    const auto build = [&](size_t row, const std::vector<VkImage>& from, const std::vector<VkImage>& to) {
                        return buildRow(device, pLogicalDevice, pLogicalSwapchain, pConfig, effectStrings[row], from, to, segmentPools.back(), gpu, "");
                    };
                    BuiltRows<std::shared_ptr<Effect>> built = buildRowsSkippingDropped<std::shared_ptr<Effect>>(effectStrings, firstRow, rows, local, local(rows), build);
                    dropped.insert(dropped.end(), built.dropped.begin(), built.dropped.end());
                    if (built.effects.empty() && rows > 0)
                        segmentEmptied = true;
                    SegmentRows segmentRows{built.effects, {}};
                    for (const size_t row : built.rows)
                        segmentRows.names.push_back(effectStrings[row]);
                    return segmentRows;
                };
                pLogicalSwapchain->chain = buildChain(*pLogicalDevice, plan->parts, pLogicalSwapchain->swapchainCreateInfo, pLogicalSwapchain->images,
                                                      buildRows, settingsManager.getEffectGpuTiming());
                headRows                 = plan->headRows;
            }
            catch (const ChainRefusal& e)
            {
                chainRefusal             = e.what();
                const std::string reason = "rows from " + effectStrings[plan->parts.front().firstRow] + " stay on the game's GPU: " + e.what();
                if (pLogicalDevice->offload->reported.insert(reason).second)
                    Logger::warn(reason);
                pLogicalSwapchain->chain.reset();
                segmentPools.clear();
            }
        }

        size_t      refusedFrom = effectStrings.size();
        std::string refusal;
        if (!pLogicalSwapchain->chain)
        {
            const GpuPlan               rows    = effectRegistry.gpuPlan();
            const std::vector<uint32_t> planned = gpusOfRows(rows.rows, rows.ownGpus, effectStrings);
            const auto first = std::find_if(planned.begin(), planned.end(), [](uint32_t gpu) { return gpu != gameGpu; });
            if (first != planned.end())
            {
                refusedFrom = static_cast<size_t>(first - planned.begin());
                refusal     = !chainRefusal.empty() ? chainRefusal
                            : pLogicalDevice->offload && pLogicalDevice->offload->onGameGpuUntilReload
                                ? std::string("a hand-off to GPU ") + std::to_string(*first) + " failed, so its rows wait for a reload"
                            : pLogicalDevice->offload && !pLogicalDevice->offload->lastRefusal.empty() ? pLogicalDevice->offload->lastRefusal
                                                                                                      : "no device on GPU " + std::to_string(*first) + " was made";
            }
        }

        const std::vector<VkImage> finalImages =
            pLogicalDevice->supportsMutableFormat
                ? pLogicalSwapchain->images
                : std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - count, pLogicalSwapchain->fakeImages.end());
        const auto buildHead = [&](size_t row, const std::vector<VkImage>& from, const std::vector<VkImage>& to) {
            Logger::debug("creating effect " + std::to_string(row) + ": " + effectStrings[row]);
            return buildRow(pLogicalDevice, pLogicalDevice, pLogicalSwapchain, pConfig, effectStrings[row], from, to, sharedTexturePool, gameGpu,
                            row >= refusedFrom ? refusal : "");
        };
        const BuiltRows<std::shared_ptr<Effect>> head = buildRowsSkippingDropped<std::shared_ptr<Effect>>(effectStrings, 0, headRows, slot, pLogicalSwapchain->chain ? slot(headRows) : finalImages, buildHead);
        dropped.insert(dropped.end(), head.dropped.begin(), head.dropped.end());
        std::vector<std::string> builtNames;
        for (size_t row : head.rows)
            builtNames.push_back(effectStrings[row]);

        if (!dropped.empty())
        {
            std::string names;
            for (const std::string& name : dropped)
                names += (names.empty() ? "" : ", ") + name;
            Logger::info("left out of the chain, as they failed or were refused: " + names);
        }
        if (segmentEmptied)
        {
            Logger::info("a GPU segment lost every row it had, so the chain is planned again without them");
            if (pLogicalSwapchain->chain)
                pLogicalSwapchain->chain->destroy(*pLogicalDevice);
            pLogicalSwapchain->chain.reset();
            pLogicalSwapchain->effects.clear();
            std::vector<std::string> kept;
            for (const std::string& name : effectStrings)
                if (std::find(dropped.begin(), dropped.end(), name) == dropped.end())
                    kept.push_back(name);
            return createEffectsForSwapchain(pLogicalSwapchain, pLogicalDevice, pConfig, kept);
        }
        if (!pLogicalSwapchain->chain && head.effects.empty())
        {
            pLogicalSwapchain->effects.push_back(
                std::shared_ptr<Effect>(new TransferEffect(pLogicalDevice, pLogicalSwapchain->imageExtent, slot(0), pLogicalSwapchain->images)));
            return {};
        }
        pLogicalSwapchain->effects.insert(pLogicalSwapchain->effects.end(), head.effects.begin(), head.effects.end());
        const size_t handOffSlot = head.effects.empty() ? 0 : headRows;

        if (pLogicalSwapchain->chain)
        {
            segmentPools.push_back(sharedTexturePool);
            std::set<std::string> seen, crossing;
            for (const auto& pool : segmentPools)
            {
                const std::set<std::string> names = pool->names();
                for (const std::string& name : names)
                    if (seen.count(name))
                        crossing.insert(name);
                seen.insert(names.begin(), names.end());
            }
            if (!crossing.empty())
            {
                const std::string reason = "rows from " + effectStrings[headRows] + " stay on the game's GPU: the texture " + *crossing.begin()
                                         + " is shared by effects in two segments of the chain";
                if (pLogicalDevice->offload->reported.insert(reason).second)
                    Logger::warn(reason);
                pLogicalSwapchain->chain->destroy(*pLogicalDevice);
                pLogicalSwapchain->chain.reset();
                pLogicalSwapchain->effects.clear();
                pLogicalDevice->offload->onGameGpuUntilReload = true;
                return createEffectsForSwapchain(pLogicalSwapchain, pLogicalDevice, pConfig, effectStrings);
            }
            std::vector<VkBuffer> outBuffers;
            for (const SharedBuffer& shared : pLogicalSwapchain->chain->segments.front().in)
                outBuffers.push_back(shared.onFrom);
            pLogicalSwapchain->effects.push_back(std::make_shared<SharedCopyEffect>(
                pLogicalDevice, pLogicalSwapchain->imageExtent, slot(handOffSlot), outBuffers, SharedCopyDirection::ImageToBuffer));
            if (pLogicalSwapchain->chain->roundTrips != VK_NULL_HANDLE)
                pLogicalSwapchain->effects.push_back(
                    std::make_shared<TimestampEffect>(pLogicalDevice, pLogicalSwapchain->chain->roundTrips, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT));
            for (const ChainSegment& segment : pLogicalSwapchain->chain->segments)
            {
                if (!segment.helper)
                    continue;
                VkPhysicalDeviceProperties properties{};
                segment.device->vki.GetPhysicalDeviceProperties(segment.device->physicalDevice, &properties);
                Logger::info("rows from " + effectStrings[segment.firstRow] + " run on GPU " + std::to_string(segment.gpu) + " ("
                             + properties.deviceName + ")");
            }
            return builtNames;
        }

        if (!pLogicalDevice->supportsMutableFormat)
        {
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - count, pLogicalSwapchain->fakeImages.end()),
                pLogicalSwapchain->images)));
        }
        return builtNames;
    }

    void reloadEffectsForSwapchain(LogicalSwapchain* pLogicalSwapchain, Config* pConfig,
                                   const std::vector<std::string>& activeEffects = {})
    {
        LogicalDevice* pLogicalDevice = pLogicalSwapchain->pLogicalDevice;

        pLogicalSwapchain->waitForEffects();

        pLogicalSwapchain->destroyEffectTimings();

        if (pLogicalSwapchain->chain)
            pLogicalSwapchain->chain->destroy(*pLogicalDevice);
        pLogicalSwapchain->chain.reset();
        pLogicalSwapchain->effects.clear();

        std::vector<std::string> effectStrings = withinEffectLimit(pLogicalDevice, withoutFailed(activeEffects));

        if (effectStrings.size() > pLogicalSwapchain->maxEffectSlots
            && !appendFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain, effectStrings.size()))
        {
            effectStrings = leaveOutFrom(pLogicalDevice, std::move(effectStrings), pLogicalSwapchain->maxEffectSlots,
                                         "the GPU had no memory for more effect images");
        }

        Logger::info("reloading " + std::to_string(effectStrings.size()) + " effects");

        const std::vector<std::string> onGameGpu = createEffectsForSwapchain(pLogicalSwapchain, pLogicalDevice, pConfig, effectStrings);

        if (settingsManager.getEffectGpuTiming())
            pLogicalSwapchain->initializeEffectTimings(onGameGpu);

        DepthState depth = getDepthState(pLogicalDevice);
        reallocateCommandBuffers(pLogicalDevice, pLogicalSwapchain, depth);

        Logger::info("effects reloaded successfully");
    }

    void reloadAllSwapchains(const std::vector<std::string>& activeEffects)
    {
        for (auto& [_, pLogicalSwapchain] : swapchainMap)
        {
            if (!pLogicalSwapchain->fakeImages.empty())
                reloadEffectsForSwapchain(pLogicalSwapchain.get(), pConfig.get(), activeEffects);
        }
    }

    void fillGpuState(LogicalDevice* pLogicalDevice, OverlayState& overlayState, bool rowTimes)
    {
        const std::shared_ptr<DeviceOffload> offload   = pLogicalDevice->offload;
        const LayerInstance*                 pInstance = findInstance(GetKey(pLogicalDevice->instance));
        if (!offload || !pInstance)
            return;
        if (offload->instanceGpus.empty())
            offload->instanceGpus = listInstanceGpus(pInstance->vki, pInstance->handle);
        const std::vector<InstanceGpu>& gpus = offload->instanceGpus;
        std::vector<GpuCandidate>       candidates;
        size_t                          gameIndex = gpus.size();
        for (size_t i = 0; i < gpus.size(); i++)
        {
            candidates.push_back(gpus[i].id);
            if (gpus[i].physical == pLogicalDevice->physicalDevice)
                gameIndex = i;
        }
        if (gameIndex == gpus.size())
            return;

        overlayState.gpus.push_back({gameGpu, formatGpuId({gpus[gameIndex].id.vendor, gpus[gameIndex].id.device, std::nullopt}),
                                     gpus[gameIndex].properties.deviceName, "the game draws on this GPU"});
        const GpuPlan      plan = effectRegistry.gpuPlan();
        std::set<size_t>   listed;
        const std::vector<uint32_t> used = otherGpusAmong(gpusOfRows(plan.rows, plan.ownGpus, effectRegistry.getActiveEffects()));
        for (size_t k = 0; k < plan.list.size(); k++)
        {
            const uint32_t     number = static_cast<uint32_t>(k) + firstListedGpu;
            const GpuListEntry& entry = plan.list[k];
            OverlayGpu          shown{number, entry.id ? formatGpuId(*entry.id) : entry.text, "", ""};
            const bool          inUse = std::find(used.begin(), used.end(), number) != used.end();
            if (!entry.id)
                shown.status = "not a GPU id: it needs vendor:device in hex";
            else
            {
                const GpuMatch match = matchGpu(*entry.id, candidates, gameIndex);
                if (match.kind == GpuMatchKind::NotFound)
                    shown.status = "not found: the game's Vulkan has no GPU with this id";
                else if (match.kind == GpuMatchKind::Ambiguous)
                    shown.status = "two identical cards match it: add its PCI address to tell them apart";
                else if (match.kind == GpuMatchKind::ListedTwice)
                    shown.status = "listed by two drivers: keep one of them for this card";
                else if (match.kind == GpuMatchKind::IsTheGame)
                    shown.status = "is GPU 1, the one the game draws on";
                else
                {
                    listed.insert(match.index);
                    const VkPhysicalDevice physical = gpus[match.index].physical;
                    shown.name                      = gpus[match.index].properties.deviceName;
                    GpuStatusFacts facts;
                    if (const auto failed = offload->notMade.find(physical); failed != offload->notMade.end())
                        facts.notMade = failed->second;
                    facts.rowsAssigned   = inUse;
                    facts.sharingEnabled = pLogicalDevice->sharingEnabled;
                    facts.backOnGameGpu  = offload->onGameGpuUntilReload;
                    facts.beingMade      = offload->making;
                    facts.refusal        = offload->lastRefusal;
                    for (const auto& [_, swapchain] : swapchainMap)
                        if (swapchain && swapchain->pLogicalDevice == pLogicalDevice && swapchain->chain && !swapchain->chain->failed)
                            for (const ChainSegment& segment : swapchain->chain->segments)
                                if (segment.helper && segment.device && segment.device->physicalDevice == physical)
                                    facts.runsChain = true;
                    shown.status    = gpuStatus(facts);
                    shown.runsChain = facts.runsChain;
                }
            }
            overlayState.gpus.push_back(shown);
        }
        for (size_t i = 0; i < gpus.size(); i++)
        {
            if (i == gameIndex || listed.count(i) || gpus[i].properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
                continue;
            const bool twin = std::count_if(gpus.begin(), gpus.end(), [&](const InstanceGpu& other) {
                                  return other.id.vendor == gpus[i].id.vendor && other.id.device == gpus[i].id.device;
                              }) > 1;
            const GpuId id{gpus[i].id.vendor, gpus[i].id.device, twin ? gpus[i].id.pci : std::nullopt};
            const std::string offered = formatGpuId(id);
            if (std::none_of(overlayState.addableGpus.begin(), overlayState.addableGpus.end(), [&](const OverlayGpu& g) { return g.id == offered; }))
                overlayState.addableGpus.push_back({0, offered, gpus[i].properties.deviceName, ""});
        }
        for (const auto& [_, swapchain] : swapchainMap)
        {
            if (rowTimes && swapchain && swapchain->pLogicalDevice == pLogicalDevice && swapchain->chain && !swapchain->chain->failed)
            {
                for (const ChainSegment& segment : swapchain->chain->segments)
                    if (const std::optional<ChainSegment::HelperTimes> times = segment.helperTimes())
                        for (size_t j = 0; j < segment.rowNames.size(); j++)
                            if (times->rowsMs[j] >= 0.0f)
                                overlayState.effectGpuMilliseconds[segment.rowNames[j]] = times->rowsMs[j];
                break;
            }
        }
    }

    void updateOverlayState(LogicalDevice* pLogicalDevice, bool effectsEnabled)
    {
        ImGuiOverlay* overlay = pLogicalDevice->imguiOverlay.get();
        if (!overlay)
            return;
        overlay->setEffectsEnabled(effectsEnabled);
        if (!overlay->isVisible() || overlay->isPreviewing())
            return;

        OverlayState                    overlayState;
        const std::vector<std::string>& selected = overlay->getSelectedEffects();
        const bool                      missing  = std::any_of(selected.begin(), selected.end(), [](const std::string& name) { return !effectRegistry.hasEffect(name); });
        if (overlay->addsEffects() || missing)
            getAvailableEffects(pConfig.get(), overlayState.currentConfigEffects, overlayState.defaultConfigEffects, overlayState.effectPaths);
        if (overlay->addsEffects())
        {
            static std::string cachedConfigPath;
            static std::string cachedConfigName;
            const std::string  configPath = pConfig->getConfigFilePath();
            if (configPath != cachedConfigPath)
            {
                cachedConfigPath = configPath;
                cachedConfigName = std::filesystem::path(cachedConfigPath).filename().string();
            }
            overlayState.configFileName = cachedConfigName;
        }
        overlayState.effectsEnabled = effectsEnabled;
        const bool rowTimes         = overlay->showsCard(OverlaySection::Effects);
        fillGpuState(pLogicalDevice, overlayState, rowTimes);

        const LogicalSwapchain* timingSwapchain = nullptr;
        uint64_t timingSurfaceArea = 0;
        for (const auto& [_, swapchain] : swapchainMap)
        {
            if (!rowTimes)
                break;
            if (!swapchain || swapchain->pLogicalDevice != pLogicalDevice
                || !swapchain->timingEnabled || swapchain->timingQueryPool == VK_NULL_HANDLE)
                continue;

            const uint64_t area = static_cast<uint64_t>(swapchain->imageExtent.width)
                * static_cast<uint64_t>(swapchain->imageExtent.height);
            if (timingSwapchain == nullptr || area > timingSurfaceArea)
            {
                timingSwapchain = swapchain.get();
                timingSurfaceArea = area;
            }
        }
        if (timingSwapchain != nullptr)
        {
            for (size_t i = 0; i < timingSwapchain->timingEffectNames.size(); ++i)
            {
                if (i >= timingSwapchain->timingMilliseconds.size()
                    || i >= timingSwapchain->timingValid.size()
                    || !timingSwapchain->timingValid[i])
                    continue;
                overlayState.effectGpuMilliseconds[timingSwapchain->timingEffectNames[i]] =
                    timingSwapchain->timingMilliseconds[i];
            }
        }

        for (const auto& effectName : selected)
        {
            if (effectRegistry.hasEffect(effectName))
                continue;
            auto pathIt = overlayState.effectPaths.find(effectName);
            std::string effectPath = (pathIt != overlayState.effectPaths.end()) ? pathIt->second : "";
            effectRegistry.ensureEffect(effectName, effectPath);
        }

        overlay->updateState(std::move(overlayState));
    }

    constexpr uint64_t helperFrameDeadlineNs = 1'000'000'000;

    VkSemaphore submitOverlayFrame(LogicalDevice* pLogicalDevice, LogicalSwapchain* pSwapchain, uint32_t index, VkQueue submitQueue)
    {
        if (!pLogicalDevice->imguiOverlay || pSwapchain->overlayImageViews[index] == VK_NULL_HANDLE)
            return pSwapchain->semaphores[index];

        const ImGuiOverlay::Frame frame = pLogicalDevice->imguiOverlay->drawFrame(*pSwapchain, index);
        if (frame.commandBuffer == VK_NULL_HANDLE)
            return pSwapchain->semaphores[index];

        VkPipelineStageFlags overlayWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo overlaySubmit = {};
        overlaySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        overlaySubmit.waitSemaphoreCount = 1;
        overlaySubmit.pWaitSemaphores = &pSwapchain->semaphores[index];
        overlaySubmit.pWaitDstStageMask = &overlayWaitStage;
        overlaySubmit.commandBufferCount = 1;
        overlaySubmit.pCommandBuffers = &frame.commandBuffer;
        overlaySubmit.signalSemaphoreCount = 1;
        overlaySubmit.pSignalSemaphores = &pSwapchain->overlaySemaphores[index];

        const VkResult vr = pLogicalDevice->vkd.QueueSubmit(submitQueue, 1, &overlaySubmit, frame.fence);
        if (vr != VK_SUCCESS)
        {
            Logger::err("the overlay submission failed for image " + std::to_string(index) + ": " + std::to_string(vr)
                        + ", presenting the frame without the overlay");
            if (!pSwapchain->overlay.restoreFence(*pLogicalDevice, index))
                pLogicalDevice->imguiOverlay->switchOff("its fence could not be recreated after a failed submission");
            return pSwapchain->semaphores[index];
        }
        return pSwapchain->overlaySemaphores[index];
    }

    VkResult VKAPI_CALL vkBasalt_CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance*                  pInstance)
    {
        VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*) pCreateInfo->pNext;

        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerInstanceCreateInfo*) layerCreateInfo->pNext;
        }

        Logger::trace("vkCreateInstance");

        // XWayland clients inherit WAYLAND_DISPLAY, so only the enabled surface extension tells X11 from Wayland
        {
            bool wantsWayland = false;
            bool wantsX11     = false;
            for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i)
            {
                const char* ext = pCreateInfo->ppEnabledExtensionNames[i];
                if (!ext)
                    continue;
                if (!std::strcmp(ext, "VK_KHR_wayland_surface"))
                    wantsWayland = true;
                else if (!std::strcmp(ext, "VK_KHR_xlib_surface") || !std::strcmp(ext, "VK_KHR_xcb_surface"))
                    wantsX11 = true;
            }
            if (wantsX11 && !wantsWayland)
                useX11InputBackend();
        }
        bool presents = false;
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i)
            if (pCreateInfo->ppEnabledExtensionNames[i] && !std::strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_SURFACE_EXTENSION_NAME))
                presents = true;
        const bool hooked = presents && !programBypassed();
        if (!presents && !programBypassed())
        {
            const char* named = pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pApplicationName ? pCreateInfo->pApplicationInfo->pApplicationName : nullptr;
            Logger::info(std::string("the layer stays out of the Vulkan instance ") + (named ? "of " + std::string(named) : "made without an application name")
                         + ": it enables no surface, so nothing in it presents");
        }

        if (layerCreateInfo == nullptr)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkLayerCreateDevice  layerCreateDevice  = nullptr;
        PFN_vkLayerDestroyDevice layerDestroyDevice = nullptr;
        for (auto* link = (const VkLayerInstanceCreateInfo*) pCreateInfo->pNext; link; link = (const VkLayerInstanceCreateInfo*) link->pNext)
        {
            if (link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LOADER_LAYER_CREATE_DEVICE_CALLBACK)
            {
                layerCreateDevice  = link->u.layerDevice.pfnLayerCreateDevice;
                layerDestroyDevice = link->u.layerDevice.pfnLayerDestroyDevice;
            }
        }

        const VkLayerInstanceLink* const ownLink = layerCreateInfo->u.pLayerInfo;
        PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance) gpa(VK_NULL_HANDLE, "vkCreateInstance");

        VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;
        VkApplicationInfo    appInfo;
        if (modifiedCreateInfo.pApplicationInfo)
        {
            appInfo = *(modifiedCreateInfo.pApplicationInfo);
            if (appInfo.apiVersion < VK_API_VERSION_1_1)
            {
                appInfo.apiVersion = VK_API_VERSION_1_1;
            }
        }
        else
        {
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext              = nullptr;
            appInfo.pApplicationName   = nullptr;
            appInfo.applicationVersion = 0;
            appInfo.pEngineName        = nullptr;
            appInfo.engineVersion      = 0;
            appInfo.apiVersion         = VK_API_VERSION_1_1;
        }

        modifiedCreateInfo.pApplicationInfo = &appInfo;
        VkResult ret = createFunc(hooked ? &modifiedCreateInfo : pCreateInfo, pAllocator, pInstance);

        if (ret != VK_SUCCESS)
            return ret;

        std::vector<std::string> librariesBelow;
        for (const VkLayerInstanceLink* link = ownLink; link; link = link->pNext)
        {
            Dl_info below{};
            if (dladdr(reinterpret_cast<void*>(link->pfnNextGetInstanceProcAddr), &below) && below.dli_fname)
                librariesBelow.emplace_back(below.dli_fname);
        }
        const FrameGenerationPlace place = frameGenerationPlace(librariesBelow, frameGenerationLayerLoaded());
        if (place != FrameGenerationPlace::NotLoaded && hooked)
            Logger::info(frameGenerationPlaceWords(place));

        InstanceDispatch dispatchTable;
        fillDispatchTableInstance(*pInstance, gpa, &dispatchTable);

        {
            scoped_lock l(globalLock);
            instanceMap.insert_or_assign(GetKey(*pInstance),
                                         LayerInstance{dispatchTable, *pInstance, modifiedCreateInfo.pApplicationInfo->apiVersion,
                                                       layerCreateDevice, layerDestroyDevice, place, presents, layerLibrariesBelow(librariesBelow)});
        }

        return ret;
    }

    void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyInstance");

        const LayerInstance* pInstance = findInstance(GetKey(instance));
        if (!pInstance)
        {
            Logger::err("vkDestroyInstance was given an instance the layer never created, so it is left alone");
            return;
        }

        pInstance->vki.DestroyInstance(instance, pAllocator);

        instanceMap.erase(GetKey(instance));
    }

    VkResult VKAPI_CALL vkBasalt_CreateDevice(VkPhysicalDevice             physicalDevice,
                                              const VkDeviceCreateInfo*    pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice*                    pDevice)
    {
        if (makingHelperDevice())
        {
            Logger::err("the device on another GPU was being made through the layer itself, so it is refused");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        scoped_lock l(globalLock);
        Logger::trace("vkCreateDevice");
        VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*) pCreateInfo->pNext;

        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerDeviceCreateInfo*) layerCreateInfo->pNext;
        }

        if (layerCreateInfo == nullptr)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        const LayerInstance* pInstance = findInstance(GetKey(physicalDevice));
        if (!pInstance)
        {
            Logger::err("vkCreateDevice was given a physical device of an instance the layer never created");
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr   gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        layerCreateInfo->u.pLayerInfo      = layerCreateInfo->u.pLayerInfo->pNext;
        VkLayerDeviceLink* const linkBelow = layerCreateInfo->u.pLayerInfo;

        PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice) gipa(pInstance->handle, "vkCreateDevice");
        if (!createFunc)
        {
            Logger::err("the layer below gave no vkCreateDevice for this instance, so the device is refused");
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        const auto newLogicalDevice = [&]() {
            auto created            = std::make_shared<LogicalDevice>();
            created->vki            = pInstance->vki;
            created->device         = *pDevice;
            created->physicalDevice = physicalDevice;
            created->instance       = pInstance->handle;
            created->frameGenerationPlace = pInstance->frameGenerationPlace;
            created->layersBelow          = pInstance->layersBelow;
            fillDispatchTableDevice(*pDevice, gdpa, &created->vkd);
            return created;
        };

        if (programBypassed() || !pInstance->presents)
        {
            VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
            if (ret != VK_SUCCESS)
                return ret;

            deviceMap.insert_or_assign(GetKey(*pDevice), newLogicalDevice());
            return VK_SUCCESS;
        }

        uint32_t extensionCount = 0;

        pInstance->vki.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        pInstance->vki.EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data());

        bool supportsMutableFormat   = false;
        bool supportsSwapchain       = false;
        bool supportsMaintenance2    = false;
        bool supportsImageFormatList = false;
        for (VkExtensionProperties properties : extensionProperties)
        {
            if (properties.extensionName == std::string("VK_KHR_swapchain_mutable_format"))
            {
                Logger::debug("device supports VK_KHR_swapchain_mutable_format");
                supportsMutableFormat = true;
            }
            else if (properties.extensionName == std::string("VK_KHR_swapchain"))
            {
                supportsSwapchain = true;
            }
            else if (properties.extensionName == std::string("VK_KHR_maintenance2"))
            {
                supportsMaintenance2 = true;
            }
            else if (properties.extensionName == std::string("VK_KHR_image_format_list"))
            {
                supportsImageFormatList = true;
            }
        }

        VkPhysicalDeviceProperties deviceProps;
        pInstance->vki.GetPhysicalDeviceProperties(physicalDevice, &deviceProps);

        VkDeviceCreateInfo       modifiedCreateInfo = *pCreateInfo;
        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount)
        {
            enabledExtensionNames = std::vector<const char*>(modifiedCreateInfo.ppEnabledExtensionNames,
                                                             modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        const uint32_t usableVersion      = std::min(deviceProps.apiVersion, pInstance->apiVersion);
        const bool     maintenance2IsCore = usableVersion >= VK_API_VERSION_1_1;
        const bool     formatListIsCore   = usableVersion >= VK_API_VERSION_1_2;
        const bool     hasFormatList      = formatListIsCore || supportsImageFormatList;

        if (supportsSwapchain)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain");
        }
        if (!formatListIsCore && supportsImageFormatList)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_image_format_list");
        }
        if (!hasFormatList)
        {
            Logger::debug("the device has no VK_KHR_image_format_list, so the swapchain is not mutable-format");
        }
        supportsMutableFormat = supportsMutableFormat && supportsSwapchain && hasFormatList
                             && (maintenance2IsCore || supportsMaintenance2);
        if (supportsMutableFormat)
        {
            Logger::debug("activating mutable_format");
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain_mutable_format");
            if (!maintenance2IsCore)
            {
                addUniqueCString(enabledExtensionNames, "VK_KHR_maintenance2");
            }
        }
        bool sharingEnabled = false;
        if (effectRegistry.listsAnotherGpu())
        {
            const std::string gameGpuName = deviceProps.deviceName;
            if (usableVersion < VK_API_VERSION_1_1)
                Logger::warn("this profile lists another GPU, but " + gameGpuName + " offers Vulkan below 1.1, so every effect runs on it");
            else if (const std::string missing = addSharingExtensions(extensionProperties, enabledExtensionNames); !missing.empty())
                Logger::warn("this profile lists another GPU, but " + gameGpuName + " lacks " + missing + ", so every effect runs on it");
            else
            {
                sharingEnabled = true;
                Logger::info("the game's device on " + gameGpuName + " enables the sharing extensions, as this profile lists another GPU");
            }
        }
        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        VkPhysicalDeviceFeatures supportedFeatures = {};
        pInstance->vki.GetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

        VkPhysicalDeviceFeatures2* appFeatures2 = nullptr;
        for (VkBaseOutStructure* s = (VkBaseOutStructure*) modifiedCreateInfo.pNext; s; s = s->pNext)
        {
            if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            {
                appFeatures2 = (VkPhysicalDeviceFeatures2*) s;
                break;
            }
        }

        VkPhysicalDeviceFeatures deviceFeatures = {};
        bool                     ownEnabledFeatures = false;

        using FeatureField = VkBool32 VkPhysicalDeviceFeatures::*;
        std::vector<FeatureField> setInApplicationFeatures;
        auto requestFeature = [&](FeatureField field, const char* name, const char* consequence) {
            const bool alreadyRequested = appFeatures2
                                              ? appFeatures2->features.*field == VK_TRUE
                                              : (modifiedCreateInfo.pEnabledFeatures && modifiedCreateInfo.pEnabledFeatures->*field == VK_TRUE);
            if (alreadyRequested)
                return true;

            if (!(supportedFeatures.*field))
            {
                Logger::warn(std::string("device does not support ") + name + " -- " + consequence);
                return false;
            }

            if (appFeatures2)
            {
                appFeatures2->features.*field = VK_TRUE;
                setInApplicationFeatures.push_back(field);
                return true;
            }

            if (!ownEnabledFeatures)
            {
                if (modifiedCreateInfo.pEnabledFeatures)
                    deviceFeatures = *(modifiedCreateInfo.pEnabledFeatures);
                modifiedCreateInfo.pEnabledFeatures = &deviceFeatures;
                ownEnabledFeatures                  = true;
            }
            deviceFeatures.*field = VK_TRUE;
            return true;
        };

        requestFeature(&VkPhysicalDeviceFeatures::shaderImageGatherExtended,
                       "shaderImageGatherExtended",
                       "ReShade effects that gather with offsets will fail to compile");

        const bool storageWrite = requestFeature(&VkPhysicalDeviceFeatures::shaderStorageImageWriteWithoutFormat,
                                                 "shaderStorageImageWriteWithoutFormat",
                                                 "ReShade compute effects will be refused");
        const bool storageRead  = requestFeature(&VkPhysicalDeviceFeatures::shaderStorageImageReadWithoutFormat,
                                                "shaderStorageImageReadWithoutFormat",
                                                "ReShade compute effects will be refused");

        uint32_t familyCount = 0;
        pInstance->vki.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        pInstance->vki.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
        LayerQueueRequest layerQueue = requestLayerQueue(pCreateInfo->pQueueCreateInfos, pCreateInfo->queueCreateInfoCount, families);
        if (layerQueue.own)
            modifiedCreateInfo.pQueueCreateInfos = layerQueue.infos.data();

        const VkDevice loaderDevice = *pDevice;
        VkResult       ret          = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);
        if (ret != VK_SUCCESS && layerQueue.own && *pDevice != loaderDevice)
            Logger::err("creating the device with the layer's own queue failed (" + std::to_string(ret)
                        + ") after the driver had made it, and the loader counts that device until it is destroyed, so it is not asked for again");
        else if (ret != VK_SUCCESS && layerQueue.own)
        {
            Logger::warn("creating the device with the layer's own queue failed (" + std::to_string(ret)
                         + "), so it is created again with the queues the game asked for");
            layerQueue.own.reset();
            modifiedCreateInfo.pQueueCreateInfos = pCreateInfo->pQueueCreateInfos;
            layerCreateInfo->u.pLayerInfo        = linkBelow;
            ret = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);
        }
        for (FeatureField field : setInApplicationFeatures)
            appFeatures2->features.*field = VK_FALSE;

        if (ret != VK_SUCCESS)
            return ret;

        std::shared_ptr<LogicalDevice> pLogicalDevice = newLogicalDevice();
        pLogicalDevice->supportsStorageImageWithoutFormat = storageWrite && storageRead;
        pLogicalDevice->supportsMutableFormat             = supportsMutableFormat;
        pLogicalDevice->sharingEnabled                    = sharingEnabled;
        pLogicalDevice->offload                           = std::make_shared<DeviceOffload>();
        pLogicalDevice->offload->create                   = pInstance->layerCreateDevice;
        pLogicalDevice->offload->destroy                  = pInstance->layerDestroyDevice;

        uint32_t count;

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> queueProperties(count);

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, queueProperties.data());
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            auto& queueInfo = pCreateInfo->pQueueCreateInfos[i];
            if ((queueProperties[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, queueInfo.queueFamilyIndex, 0, &pLogicalDevice->queue);

                VkCommandPoolCreateInfo commandPoolCreateInfo;
                commandPoolCreateInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolCreateInfo.pNext            = nullptr;
                commandPoolCreateInfo.flags            = 0;
                commandPoolCreateInfo.queueFamilyIndex = queueInfo.queueFamilyIndex;

                Logger::debug("Found graphics capable queue");
                const VkResult poolResult = pLogicalDevice->vkd.CreateCommandPool(
                    pLogicalDevice->device, &commandPoolCreateInfo, nullptr, &pLogicalDevice->commandPool);
                if (poolResult != VK_SUCCESS)
                {
                    Logger::err("creating the layer's command pool failed (" + std::to_string(poolResult)
                                + "), so the layer leaves this device's swapchains alone");
                    pLogicalDevice->commandPool = VK_NULL_HANDLE;
                }
                pLogicalDevice->queueFamilyIndex = queueInfo.queueFamilyIndex;
                pLogicalDevice->timestampPeriodNanoseconds = deviceProps.limits.timestampPeriod;
                pLogicalDevice->limits = deviceProps.limits;
                pLogicalDevice->timestampValidBits = queueProperties[queueInfo.queueFamilyIndex].timestampValidBits;

                if (pLogicalDevice->timestampValidBits == 0)
                    Logger::info("selected graphics queue does not support timestamp queries; per-effect GPU timing is unavailable");

                initializeDispatchTable(pLogicalDevice->queue, pLogicalDevice->device);

                break;
            }
        }

        if (!pLogicalDevice->queue)
        {
            Logger::err("Did not find a graphics queue! vkDaedalus requires a graphics-capable queue.");
        }

        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            const VkDeviceQueueCreateInfo& asked = pCreateInfo->pQueueCreateInfos[i];
            if (asked.flags != 0)
                continue;
            for (uint32_t index = 0; index < asked.queueCount; index++)
            {
                VkQueue gameQueue = VK_NULL_HANDLE;
                pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, asked.queueFamilyIndex, index, &gameQueue);
                if (gameQueue != VK_NULL_HANDLE)
                    pLogicalDevice->queueFamilies[gameQueue] = asked.queueFamilyIndex;
            }
        }
        if (layerQueue.own && pLogicalDevice->queue && layerQueue.own->family == pLogicalDevice->queueFamilyIndex)
        {
            pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, layerQueue.own->family, layerQueue.own->index, &pLogicalDevice->ownQueue);
            if (pLogicalDevice->ownQueue != VK_NULL_HANDLE)
            {
                initializeDispatchTable(pLogicalDevice->ownQueue, pLogicalDevice->device);
                Logger::info("the layer has a queue of its own (family " + std::to_string(layerQueue.own->family) + ", index "
                             + std::to_string(layerQueue.own->index) + ") for its uploads and for presents on another queue family");
            }
        }

        deviceMap.insert_or_assign(GetKey(*pDevice), pLogicalDevice);

        return VK_SUCCESS;
    }

    void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyDevice");

        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice)
        {
            Logger::err("vkDestroyDevice was given a device the layer never created, so it is left alone");
            return;
        }

        pLogicalDevice->imguiOverlay.reset();
        pLogicalDevice->offload.reset();
        effectRegistry.forgetDevice(pLogicalDevice);

        for (const auto& depthImage : pLogicalDevice->depthImages)
            if (depthImage.view != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(device, depthImage.view, nullptr);
        pLogicalDevice->depthImages.clear();

        // the keyboard and pointer proxies live on the shared queue, so it is released after both
        cleanupWaylandKeyboard();
        cleanupWaylandMouse();
        cleanupWaylandInputCommon();

        if (pLogicalDevice->commandPool != VK_NULL_HANDLE)
        {
            Logger::debug("DestroyCommandPool");
            pLogicalDevice->vkd.DestroyCommandPool(device, pLogicalDevice->commandPool, nullptr);
        }

        pLogicalDevice->vkd.DestroyDevice(device, pAllocator);

        deviceMap.erase(GetKey(device));
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(VkDevice                        device,
                                                               const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                               const VkAllocationCallbacks*    pAllocator,
                                                               VkSwapchainKHR*                 pSwapchain)
    {
        std::unique_lock<LayerLock> l(globalLock);

        Logger::trace("vkCreateSwapchainKHR");

        const auto deviceIt = deviceMap.find(GetKey(device));
        if (deviceIt == deviceMap.end() || !deviceIt->second)
            return VK_ERROR_DEVICE_LOST;

        const std::shared_ptr<LogicalDevice> deviceHeldAcrossReentry = deviceIt->second;
        LogicalDevice* pLogicalDevice = deviceHeldAcrossReentry.get();

        if (pLogicalDevice->commandPool == VK_NULL_HANDLE)
        {
            PFN_vkCreateSwapchainKHR untouchedCreate = pLogicalDevice->vkd.CreateSwapchainKHR;
            l.unlock();
            return untouchedCreate(device, pCreateInfo, pAllocator, pSwapchain);
        }
        if (offInThisRun().off)
        {
            PFN_vkCreateSwapchainKHR untouchedCreate = pLogicalDevice->vkd.CreateSwapchainKHR;
            l.unlock();
            const VkResult created = untouchedCreate(device, pCreateInfo, pAllocator, pSwapchain);
            l.lock();
            if (created == VK_SUCCESS)
            {
                offInThisRun().handedBack.insert(*pSwapchain);
                Logger::info("the game's new swapchain stays its own, with its images and no copy, while vkDaedalus is off in this run");
            }
            return created;
        }

        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;

        VkFormat format = modifiedCreateInfo.imageFormat;

        VkFormat srgbFormat  = convertToSRGB(format);
        VkFormat unormFormat = convertToUNORM(format);
        Logger::debug(std::to_string(srgbFormat) + " " + std::to_string(unormFormat));

        VkFormat formats[] = {unormFormat, srgbFormat};

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        VkImageFormatListCreateInfo*   appFormatList = nullptr;
        const VkFormat*                appFormats     = nullptr;
        uint32_t                       appFormatCount = 0;
        std::vector<VkFormat>          mergedFormats;
        if (pLogicalDevice->supportsMutableFormat)
        {
            modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

            for (VkBaseOutStructure* s = (VkBaseOutStructure*) modifiedCreateInfo.pNext; s && !appFormatList; s = s->pNext)
                if (s->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
                    appFormatList = (VkImageFormatListCreateInfo*) s;

            if (appFormatList)
            {
                appFormats     = appFormatList->pViewFormats;
                appFormatCount = appFormatList->viewFormatCount;
                mergedFormats.assign(appFormats, appFormats + appFormatCount);
                for (VkFormat format : formats)
                    if (std::find(mergedFormats.begin(), mergedFormats.end(), format) == mergedFormats.end())
                        mergedFormats.push_back(format);
                appFormatList->viewFormatCount = static_cast<uint32_t>(mergedFormats.size());
                appFormatList->pViewFormats    = mergedFormats.data();
            }
            else
            {
                imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
                imageFormatListCreateInfo.pNext           = modifiedCreateInfo.pNext;
                imageFormatListCreateInfo.viewFormatCount = (srgbFormat == unormFormat) ? 1 : 2;
                imageFormatListCreateInfo.pViewFormats    = formats;

                modifiedCreateInfo.pNext = &imageFormatListCreateInfo;
            }
        }

        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        Logger::debug("swapchain create: app usage=" + convertToString(pCreateInfo->imageUsage)
                      + " our usage=" + convertToString(modifiedCreateInfo.imageUsage)
                      + " flags=" + convertToString(modifiedCreateInfo.flags)
                      + " (decimal bit values)"
                      + " minImageCount=" + std::to_string(modifiedCreateInfo.minImageCount)
                      + " presentMode=" + std::to_string(modifiedCreateInfo.presentMode)
                      + " extent=" + std::to_string(modifiedCreateInfo.imageExtent.width) + "x"
                      + std::to_string(modifiedCreateInfo.imageExtent.height));

        Logger::debug("format " + std::to_string(modifiedCreateInfo.imageFormat));
        std::shared_ptr<LogicalSwapchain> pLogicalSwapchain(new LogicalSwapchain());
        pLogicalSwapchain->pLogicalDevice      = pLogicalDevice;
        pLogicalSwapchain->appImageUsage       = pCreateInfo->imageUsage;
        pLogicalSwapchain->keepCreateInfo(*pCreateInfo);
        pLogicalSwapchain->imageExtent         = modifiedCreateInfo.imageExtent;
        pLogicalSwapchain->format              = modifiedCreateInfo.imageFormat;
        pLogicalSwapchain->imageCount          = 0;

        PFN_vkCreateSwapchainKHR downchainCreate = pLogicalDevice->vkd.CreateSwapchainKHR;
        l.unlock();
        VkResult result = downchainCreate(device, &modifiedCreateInfo, pAllocator, pSwapchain);
        if (appFormatList)
        {
            appFormatList->viewFormatCount = appFormatCount;
            appFormatList->pViewFormats    = appFormats;
        }
        l.lock();

        const auto currentDeviceIt = deviceMap.find(GetKey(device));
        if (currentDeviceIt == deviceMap.end() || currentDeviceIt->second != deviceHeldAcrossReentry)
        {
            *pSwapchain = VK_NULL_HANDLE;
            return VK_ERROR_DEVICE_LOST;
        }

        if (result != VK_SUCCESS)
            return result;

        swapchainMap.insert_or_assign(*pSwapchain, pLogicalSwapchain);

        return result;
    }

    struct EffectCompileRequest
    {
        std::string                                      shaderPath;
        std::vector<std::pair<std::string, std::string>> defines;
        bool                                             relaxMinPrecision = false;
        bool                                             liveUniforms = false;
    };

    std::vector<EffectCompileRequest> gatherEffectCompileRequestsLocked(const std::vector<std::string>& effectNames,
                                                                 VkExtent2D                      extent,
                                                                 VkFormat                        unormFormat,
                                                                 VkColorSpaceKHR                 colorSpace)
    {
        std::vector<EffectCompileRequest> requests;

        for (const std::string& name : effectNames)
        {
            std::string path = effectRegistry.getEffectFilePath(name);
            if (path.empty())
                continue;

            if (const auto stale = effectRegistry.staleParameterReadDefinitions(name))
                requests.push_back({path, parameterReadDefines(*stale)});
            requests.push_back({std::move(path),
                                reshadeCompileDefines(extent, unormFormat, colorSpace, effectRegistry.getCompilePreprocessorDefs(name)),
                                effectRegistry.getAllowHalfPrecision(name),
                                settingsManager.getLiveReshadeUniforms()});
        }

        return requests;
    }

    void warmEffectCompile(const EffectCompileRequest& request, const std::vector<std::string>& includePaths)
    {
        getOrCompileReshadeEffect(request.shaderPath, request.defines, includePaths, request.relaxMinPrecision, request.liveUniforms);
    }

    std::vector<EffectCompileJobs::Compile> chainCompilesForLocked(const std::vector<std::string>& effectNames)
    {
        std::vector<EffectCompileJobs::Compile> compiles;
        for (const auto& [_, pSwapchain] : swapchainMap)
        {
            if (!pSwapchain || pSwapchain->fakeImages.empty())
                continue;
            for (EffectCompileRequest& request : gatherEffectCompileRequestsLocked(effectNames, pSwapchain->imageExtent,
                                                                                   convertToUNORM(pSwapchain->format),
                                                                                   pSwapchain->swapchainCreateInfo.imageColorSpace))
                compiles.push_back([request = std::move(request)] {
                    warmEffectCompile(request, ConfigSerializer::loadShaderManagerConfig().discoveredShaderPaths);
                });
        }
        return compiles;
    }

    void runEffectCompileWarmUp(const std::unique_lock<LayerLock>& released, const std::vector<EffectCompileRequest>& requests)
    {
        if (released.owns_lock())
        {
            Logger::err("a compile warm-up was asked for while globalLock was held, so it is skipped and the effects compile when built");
            return;
        }
        Logger::debug("compile cache warm-up: " + std::to_string(requests.size()) + " effects");
        if (requests.empty())
            return;

        const std::vector<std::string> includePaths = ConfigSerializer::loadShaderManagerConfig().discoveredShaderPaths;

        for (const EffectCompileRequest& request : requests)
        {
            try
            {
                warmEffectCompile(request, includePaths);
            }
            catch (const std::exception& e)
            {
                Logger::debug(std::string("compile cache warm-up skipped an effect: ") + e.what());
            }
        }
    }

    void warmEffectCompileCache(VkSwapchainKHR swapchain)
    {
        std::unique_lock<LayerLock> l(globalLock);

        LogicalSwapchain* pLogicalSwapchain = findSwapchain(swapchain);
        if (!pLogicalSwapchain || !pLogicalSwapchain->fakeImages.empty())
            return;

        effectRegistry.initializeSelectedEffectsFromConfig();

        const std::vector<EffectCompileRequest> requests =
            gatherEffectCompileRequestsLocked(effectRegistry.getActiveEffects(), pLogicalSwapchain->imageExtent,
                                              convertToUNORM(pLogicalSwapchain->format),
                                              pLogicalSwapchain->swapchainCreateInfo.imageColorSpace);
        l.unlock();
        runEffectCompileWarmUp(l, requests);
    }

    void prepareHelpers(std::unique_lock<LayerLock>& l, LogicalDevice* game)
    {
        const VkDevice device = game->device;
        if (game->isHelper || !game->offload)
            return;
        for (const auto& [physical, helper] : game->offload->helpers)
            helper->gpu = 0;
        const GpuPlan plan = effectRegistry.gpuPlan();
        if (!plan.anotherGpu)
            return;

        const std::vector<uint32_t>          others  = otherGpusAmong(gpusOfRows(plan.rows, plan.ownGpus, effectRegistry.getActiveEffects()));
        const std::vector<uint32_t>          rowGpus = effectiveGpus(plan.ownGpus);
        const std::shared_ptr<DeviceOffload> offload = game->offload;
        const auto firstRowOn = [&](uint32_t number) {
            return plan.rows[std::find(rowGpus.begin(), rowGpus.end(), number) - rowGpus.begin()];
        };
        const auto refuse = [&](uint32_t number, const std::string& reason) {
            offload->lastRefusal = reason;
            if (offload->reported.insert(reason).second)
                Logger::warn("rows from " + firstRowOn(number) + " stay on the game's GPU: " + reason);
        };

        if (!game->sharingEnabled)
            return refuse(others.front(), "the game's device was made before this profile listed another GPU, so it shares nothing until the game restarts");
        if (!offload->create || !offload->destroy)
            return refuse(others.front(), "the Vulkan loader handed the layer no device callback, so it cannot make a device on another GPU");
        const LayerInstance* pInstance = findInstance(GetKey(game->instance));
        if (!pInstance)
            return refuse(others.front(), "the game's instance is unknown to the layer");

        const std::vector<InstanceGpu> gpus = listInstanceGpus(pInstance->vki, pInstance->handle);
        std::vector<GpuCandidate>      candidates;
        size_t                         gameIndex = gpus.size();
        for (size_t i = 0; i < gpus.size(); i++)
        {
            candidates.push_back(gpus[i].id);
            if (gpus[i].physical == game->physicalDevice)
                gameIndex = i;
        }
        if (gameIndex == gpus.size())
            return refuse(others.front(), "the game's GPU is not in the instance's GPU list");

        for (const uint32_t number : others)
        {
            const GpuId&      id    = *plan.list[number - firstListedGpu].id;
            const std::string named = "GPU " + std::to_string(number) + " (" + formatGpuId(id) + ")";
            const GpuMatch    match = matchGpu(id, candidates, gameIndex);
            if (match.kind == GpuMatchKind::NotFound)
            {
                refuse(number, named + " is not in this Vulkan instance: unplugged, without a driver, or hidden by a device filter");
                continue;
            }
            if (match.kind == GpuMatchKind::Ambiguous)
            {
                refuse(number, named + " matches more than one card, so name it with its PCI address as vendor:device@dddd:bb:dd.f");
                continue;
            }
            if (match.kind == GpuMatchKind::ListedTwice)
            {
                refuse(number, named + " is listed by two Vulkan drivers, so the layer cannot tell which to use: keep one of them for this card");
                continue;
            }
            if (match.kind == GpuMatchKind::IsTheGame)
            {
                refuse(number, named + " is the game's own GPU");
                continue;
            }

            const InstanceGpu& helperGpu  = gpus[match.index];
            const std::string  helperName = "GPU " + std::to_string(number) + " (" + formatGpuId(id) + ", " + helperGpu.properties.deviceName + ")";
            if (const auto made = offload->helpers.find(helperGpu.physical); made != offload->helpers.end())
            {
                made->second->gpu = number;
                continue;
            }
            if (offload->notMade.count(helperGpu.physical) || offload->making)
                continue;
            if (const std::string problem = helperPairProblem(pInstance->vki, pInstance->apiVersion, game->physicalDevice, helperGpu); !problem.empty())
            {
                refuse(number, problem);
                continue;
            }

            const HelperRequest request{pInstance->vki, pInstance->handle, pInstance->apiVersion, offload->create, offload->destroy, helperGpu, number};
            offload->making = true;
            l.unlock();
            auto made = makeHelperDevice(l, request);
            l.lock();
            offload->making = false;
            if (findDevice(GetKey(device)) != game)
                return;
            if (const std::string* problem = std::get_if<std::string>(&made))
            {
                offload->notMade[helperGpu.physical] = *problem;
                refuse(number, "a device on " + helperName + " could not be made: " + *problem);
                continue;
            }
            offload->helpers[helperGpu.physical] = std::get<std::shared_ptr<HelperDevice>>(made);
            Logger::info("made a device on " + helperName + " for rows from " + firstRowOn(number));
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(VkDevice       device,
                                                                  VkSwapchainKHR swapchain,
                                                                  uint32_t*      pCount,
                                                                  VkImage*       pSwapchainImages)
    {
        if (pSwapchainImages != nullptr)
        {
            warmEffectCompileCache(swapchain);
            std::unique_lock<LayerLock> l(globalLock);
            if (LogicalDevice* game = findDevice(GetKey(device)))
                prepareHelpers(l, game);
        }

        scoped_lock l(globalLock);
        Logger::trace("vkGetSwapchainImagesKHR " + std::to_string(*pCount));

        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice)
            return VK_ERROR_UNKNOWN;

        LogicalSwapchain* pLogicalSwapchain = findSwapchain(swapchain);
        if (pSwapchainImages == nullptr || !pLogicalSwapchain)
        {
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }

        const auto handBack = [&](const std::vector<VkImage>& handles) {
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, handles.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        };

        if (pLogicalSwapchain->imageCount > 0)
            return handBack(pLogicalSwapchain->fakeImages.empty() ? pLogicalSwapchain->images : pLogicalSwapchain->fakeImages);

        uint32_t downChainCount = 0;
        VkResult downChain = pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &downChainCount, nullptr);
        std::vector<VkImage> downChainImages(downChainCount);
        if (downChain == VK_SUCCESS)
            downChain = pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &downChainCount, downChainImages.data());
        if (downChain != VK_SUCCESS || downChainCount == 0)
        {
            Logger::err("the swapchain's own images could not be read (" + std::to_string(downChain)
                        + "), so the call goes down unchanged");
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }
        pLogicalSwapchain->images     = std::move(downChainImages);
        pLogicalSwapchain->imageCount = downChainCount;

        Logger::debug("down-chain swapchain imageCount=" + std::to_string(pLogicalSwapchain->imageCount)
                      + " (app requested minImageCount=" + std::to_string(pLogicalSwapchain->swapchainCreateInfo.minImageCount) + ")");

        pLogicalSwapchain->overlayImageViews.assign(pLogicalSwapchain->imageCount, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            try
            {
                pLogicalSwapchain->overlayImageViews[i] =
                    createImageViews(pLogicalDevice, pLogicalSwapchain->format, {pLogicalSwapchain->images[i]})[0];
            }
            catch (const std::exception& e)
            {
                Logger::err("the overlay is not drawn on swapchain image " + std::to_string(i) + ", its view failed: " + e.what());
            }
        }

        const bool isFirstRun = !chainBuiltOnce;
        chainBuiltOnce = true;
        effectRegistry.initializeSelectedEffectsFromConfig();

        const std::vector<std::string> activeEffects = withinEffectLimit(pLogicalSwapchain->pLogicalDevice, withoutFailed(effectRegistry.getActiveEffects()));

        size_t effectSlots = std::max<size_t>(activeEffects.size(), 1);
        pLogicalSwapchain->maxEffectSlots = effectSlots;

        VkDeviceMemory fakeImageBlock = VK_NULL_HANDLE;
        uint32_t fakeImageCount = pLogicalSwapchain->imageCount * (effectSlots + !pLogicalDevice->supportsMutableFormat);

        pLogicalSwapchain->fakeImages =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, fakeImageCount, fakeImageBlock);
        if (fakeImageBlock != VK_NULL_HANDLE)
            pLogicalSwapchain->fakeImageMemory.push_back(fakeImageBlock);
        Logger::debug("created fake swapchain images");

        if (pLogicalSwapchain->fakeImages.empty())
            return handBack(pLogicalSwapchain->images);

        const auto abandon = [&](const std::string& why) {
            Logger::err(why + ", so the application keeps the real swapchain images: no effects and no overlay");
            pLogicalSwapchain->destroy();
            return handBack(pLogicalSwapchain->images);
        };

        std::vector<std::string> timingEffectNames;
        if (!isFirstRun && !activeEffects.empty())
        {
            Logger::debug("using pass-through during resize, will restore effects after debounce");
            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin(),
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->imageExtent,
                firstImages, pLogicalSwapchain->images)));

            resizeDebounce.pending = true;
            resizeDebounce.lastResizeTime = std::chrono::steady_clock::now();
        }
        else
        {
            timingEffectNames = createEffectsForSwapchain(pLogicalSwapchain, pLogicalDevice, pConfig.get(), activeEffects);
        }

        DepthState depth = getDepthState(pLogicalDevice);

        Logger::debug("active effect count: " + std::to_string(activeEffects.size()));
        Logger::debug("effect count: " + std::to_string(pLogicalSwapchain->effects.size()));

        pLogicalSwapchain->effectFences.resize(pLogicalSwapchain->imageCount);
        pLogicalSwapchain->fencelessEffectQueues.assign(pLogicalSwapchain->imageCount, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            if (createSignalledFence(pLogicalDevice, &pLogicalSwapchain->effectFences[i]) != VK_SUCCESS)
                Logger::err("failed to create effect fence " + std::to_string(i));
        }

        if (settingsManager.getEffectGpuTiming())
            pLogicalSwapchain->initializeEffectTimings(timingEffectNames);

        try
        {
            pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
            pLogicalSwapchain->overlaySemaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        }
        catch (const std::exception& e)
        {
            return abandon(std::string("creating the present semaphores failed (") + e.what() + ")");
        }

        pLogicalSwapchain->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
            pLogicalDevice,
            pLogicalSwapchain->imageExtent,
            std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin(), pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
            pLogicalSwapchain->images));

        try
        {
            pLogicalSwapchain->commandBuffersNoEffect = recordCommandBuffers(
                pLogicalDevice, pLogicalSwapchain->imageCount, {pLogicalSwapchain->defaultTransfer},
                VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED);
        }
        catch (const std::exception& e)
        {
            return abandon(std::string("no pass-through command buffers could be recorded (") + e.what() + ")");
        }
        reallocateCommandBuffers(pLogicalDevice, pLogicalSwapchain, depth);

        if (!pLogicalDevice->imguiOverlay)
        {
            pLogicalDevice->imguiOverlay = std::make_unique<ImGuiOverlay>(pLogicalDevice);
            pLogicalDevice->imguiOverlay->setEffectRegistry(&effectRegistry);
            pLogicalDevice->imguiOverlay->setChainCompiles(chainCompilesForLocked);

            pLogicalDevice->imguiOverlay->setGameProfile(detectedGameName, activeProfileName, activeProfilePath);
            pLogicalDevice->imguiOverlay->setFirstLaunchOfTheGame(firstLaunchOfTheGame);

        }

        pLogicalDevice->imguiOverlay->prepareSwapchain(*pLogicalSwapchain);

        *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
        std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
        return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
    }

    struct PresentControls
    {
        uint32_t toggleKey      = 0;
        uint32_t reloadKey      = 0;
        uint32_t overlayKey     = 0;
        uint32_t previewKey     = 0;
        uint32_t frameGenerationKey     = 0;
        bool     frameGenerationPressed = false;
        bool     previewBinding = false;
        bool     togglePressed  = false;
        bool     reloadPressed  = false;
        bool     overlayPressed = false;
        bool     effectsOn      = false;
    };

    void bindControlKey(uint32_t& key, bool& wasPressed, const std::string& name)
    {
        const uint32_t next = convertToKeySym(name);
        if (next != key)
            wasPressed = isKeyPressed(next);
        key = next;
    }

    void readControlKeys(PresentControls& controls)
    {
        bindControlKey(controls.toggleKey, controls.togglePressed, settingsManager.getToggleKey());
        bindControlKey(controls.reloadKey, controls.reloadPressed, settingsManager.getReloadKey());
        bindControlKey(controls.overlayKey, controls.overlayPressed, settingsManager.getOverlayKey());
        bindControlKey(controls.previewKey, controls.previewBinding, settingsManager.getPreviewKey());
        bindControlKey(controls.frameGenerationKey, controls.frameGenerationPressed, settingsManager.getFrameGenerationKey());
    }

    bool previewHeld(PresentControls& controls)
    {
        const bool held = isKeyPressed(controls.previewKey);
        if (controls.previewBinding)
        {
            controls.previewBinding = held;
            return false;
        }
        return held;
    }

    PresentControls& presentControls()
    {
        static PresentControls controls = [] {
            PresentControls initial;
            readControlKeys(initial);
            initial.effectsOn = settingsManager.getEnableOnLaunch();
            Logger::info("hot-reload initialized, config: " + pConfig->getConfigFilePath());
            return initial;
        }();
        return controls;
    }

    void toggleEffects(PresentControls& controls)
    {
        controls.effectsOn = !controls.effectsOn;
        Logger::debug(controls.effectsOn ? "effects on" : "effects off");
    }

    enum class ReloadReason
    {
        None,
        Rebuild,
        FileChanged,
    };

    ReloadReason pollControls(LogicalDevice* pLogicalDevice, PresentControls& controls)
    {
        ImGuiOverlay* overlay = pLogicalDevice->imguiOverlay.get();

        if (overlay && overlay->hasSettingsSaved())
        {
            readControlKeys(controls);
            overlay->clearSettingsSaved();
            Logger::info("Settings reloaded from SettingsManager");
        }

        if (overlay && overlay->hasShaderPathsChanged())
        {
            cachedEffects.initialized = false;
            overlay->clearShaderPathsChanged();
            Logger::info("Shader paths changed, effect list refreshed");
        }

        if (handleKeyPress(controls.toggleKey, controls.togglePressed))
            toggleEffects(controls);
        if (handleKeyPress(controls.frameGenerationKey, controls.frameGenerationPressed) && overlay)
            overlay->toggleFrameGeneration();

        bool rebuild     = false;
        bool fileChanged = false;
        if (handleKeyPress(controls.reloadKey, controls.reloadPressed))
        {
            Logger::debug("reload key pressed");
            rebuild = true;
        }
        if (pConfig->hasConfigChanged() || (pGameConfig && pGameConfig->hasConfigChanged()))
        {
            Logger::debug("config file changed detected");
            fileChanged = true;
        }

        OffInThisRun& off = offInThisRun();
        if (overlay && overlay->takeOffInThisRunRequest())
        {
            off.off           = true;
            off.effectsWereOn = controls.effectsOn;
            if (controls.effectsOn)
                toggleEffects(controls);
            for (const auto& [handle, pSwapchain] : swapchainMap)
                if (pSwapchain && pSwapchain->pLogicalDevice == pLogicalDevice)
                    off.outOfDateDue.insert(handle);
            if (overlay->isVisible())
                overlay->toggle();
            Logger::info("vkDaedalus is off in this run: the effects stopped, and the game's swapchain goes back to its own images when the game "
                         "rebuilds it; the overlay key brings vkDaedalus back until the game closes");
        }

        const bool closeClicked = overlay && overlay->takeCloseRequest();
        if (handleKeyPress(controls.overlayKey, controls.overlayPressed))
        {
            if (off.off)
            {
                off.off = false;
                off.outOfDateDue.insert(off.handedBack.begin(), off.handedBack.end());
                off.handedBack.clear();
                if (off.effectsWereOn && !controls.effectsOn)
                    toggleEffects(controls);
                if (overlay)
                    overlay->comeBackInThisRun();
                Logger::info("vkDaedalus is back in this run: it takes the game's swapchain over again when the game rebuilds it");
            }
            else if (overlay)
                overlay->toggle();
        }
        else if (closeClicked && overlay->isVisible())
        {
            Logger::debug("the overlay's close button was clicked, closing it and releasing input");
            overlay->toggle();
        }
        else if (inputFocusLost())
        {
            if (overlay && overlay->isVisible())
            {
                Logger::debug("focus left the application, closing the overlay and releasing input");
                overlay->toggle();
            }
        }

        if (overlay)
            overlay->setPreviewing(overlay->isVisible() && previewHeld(controls));

        if (overlay && overlay->hasToggleEffectsRequest())
        {
            toggleEffects(controls);
            overlay->clearToggleEffectsRequest();
        }

        if (overlay && !overlay->hasPendingConfig())
            overlay->processAddedEffects();
        if (overlay)
            overlay->settleEdits();

        if (overlay && overlay->hasModifiedParams())
            rebuild = true;
        if (overlay && (rebuild || fileChanged))
            overlay->clearApplyRequest();

        if (fileChanged)
            return ReloadReason::FileChanged;
        return rebuild ? ReloadReason::Rebuild : ReloadReason::None;
    }

    bool deviceSurvivedUnlock(VkQueue queue, LogicalDevice* pLogicalDevice)
    {
        return findDevice(GetKey(queue)) == pLogicalDevice;
    }

    VkResult reloadConfigAndEffects(std::unique_lock<LayerLock>& l, VkQueue queue, LogicalDevice* pLogicalDevice, bool selectFromFile)
    {
        Logger::info("hot-reloading config and effects...");
        effectRegistry.clearEffectErrors();
        if (pLogicalDevice->offload)
        {
            pLogicalDevice->offload->onGameGpuUntilReload = false;
            pLogicalDevice->offload->reported.clear();
        }
        ImGuiOverlay* overlay = pLogicalDevice->imguiOverlay.get();

        if (overlay && overlay->hasPendingConfig())
        {
            overlay->cancelAddedEffects();
            const std::string nextConfig = overlay->getPendingConfigPath();
            Config            upcoming(nextConfig);
            upcoming.setFallback(pBaseConfig.get());
            std::vector<EffectCompileRequest> reads;
            for (std::string& path : EffectRegistry::reshadeEffectFilesOf(&upcoming))
                reads.push_back({std::move(path), parameterReadDefines({})});
            if (!reads.empty())
            {
                l.unlock();
                runEffectCompileWarmUp(l, reads);
                l.lock();
                if (!deviceSurvivedUnlock(queue, pLogicalDevice))
                {
                    Logger::err("device was destroyed while the next profile's effects were being compiled");
                    return VK_ERROR_DEVICE_LOST;
                }
                overlay = pLogicalDevice->imguiOverlay.get();
                if (!overlay || !overlay->hasPendingConfig() || overlay->getPendingConfigPath() != nextConfig)
                    return VK_SUCCESS;
            }
            switchConfig(nextConfig);
            if (settingsManager.getSafeAntiCheat())
                for (auto& [_, pSwapchain] : swapchainMap)
                    if (pSwapchain && !pSwapchain->fakeImages.empty())
                        reallocateCommandBuffers(pSwapchain->pLogicalDevice, pSwapchain.get(),
                                                 getDepthState(pSwapchain->pLogicalDevice));
            std::vector<std::string> newEffects = pConfig->getOption<std::vector<std::string>>("effects", {});
            std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects", {});
            overlay->setSelectedEffects(newEffects, disabledEffects);
            overlay->clearPendingConfig();
            overlay->requestApply();
            return VK_SUCCESS;
        }

        pConfig->reload();
        if (selectFromFile)
        {
            if (pGameConfig)
                pGameConfig->reload();
            readSafeAntiCheat();
            effectRegistry.selectEffectsFromConfig();
        }
        cachedEffects.initialized = false;

        std::vector<std::string> activeEffects = overlay ? overlay->getActiveEffects() : effectRegistry.getActiveEffects();

        std::vector<EffectCompileRequest> requests;
        for (const auto& [_, pSwapchain] : swapchainMap)
        {
            if (!pSwapchain || pSwapchain->fakeImages.empty())
                continue;

            std::vector<EffectCompileRequest> forSwapchain = gatherEffectCompileRequestsLocked(
                activeEffects, pSwapchain->imageExtent, convertToUNORM(pSwapchain->format),
                pSwapchain->swapchainCreateInfo.imageColorSpace);
            requests.insert(requests.end(), forSwapchain.begin(), forSwapchain.end());
        }

        if (!requests.empty())
        {
            l.unlock();
            runEffectCompileWarmUp(l, requests);
            l.lock();

            if (!deviceSurvivedUnlock(queue, pLogicalDevice))
            {
                Logger::err("device was destroyed while effects were being compiled");
                return VK_ERROR_DEVICE_LOST;
            }
        }

        prepareHelpers(l, pLogicalDevice);
        if (!deviceSurvivedUnlock(queue, pLogicalDevice))
        {
            Logger::err("device was destroyed while a device on another GPU was being made");
            return VK_ERROR_DEVICE_LOST;
        }
        reloadAllSwapchains(activeEffects);
        return VK_SUCCESS;
    }

    void reloadAfterResize()
    {
        if (!resizeDebounce.pending)
            return;

        auto resizeElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - resizeDebounce.lastResizeTime).count();
        if (resizeElapsed < RESIZE_DEBOUNCE_MS)
            return;

        Logger::info("debounced resize reload after " + std::to_string(resizeElapsed) + "ms");
        resizeDebounce.pending = false;

        const std::vector<std::string> activeEffects = effectRegistry.getActiveEffects();
        for (auto& [_, pSwapchain] : swapchainMap)
        {
            if (pSwapchain->fakeImages.empty())
                continue;
            reloadEffectsForSwapchain(pSwapchain.get(), pConfig.get(), activeEffects);
        }
    }

    VkFence ensureEffectFence(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, uint32_t index)
    {
        if (index >= pLogicalSwapchain->effectFences.size())
            return VK_NULL_HANDLE;
        if (pLogicalSwapchain->effectFences[index] != VK_NULL_HANDLE)
            return pLogicalSwapchain->effectFences[index];

        createSignalledFence(pLogicalDevice, &pLogicalSwapchain->effectFences[index]);
        return pLogicalSwapchain->effectFences[index];
    }

    VkResult waitForPreviousSubmissions(std::unique_lock<LayerLock>& l,
                                        LogicalDevice*                pLogicalDevice,
                                        const VkPresentInfoKHR*       pPresentInfo)
    {
        static thread_local std::vector<VkFence> fences;
        static thread_local std::vector<VkQueue> fenceless;
        std::vector<std::pair<std::shared_ptr<HelperDevice>, VkFence>> helperFences;
        VkFence                                  overlayDraw = VK_NULL_HANDLE;
        fences.clear();
        fenceless.clear();
        const bool overlayVisible = pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->isVisible();
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
        {
            LogicalSwapchain* pLogicalSwapchain = findSwapchain(pPresentInfo->pSwapchains[i]);
            const uint32_t    index             = pPresentInfo->pImageIndices[i];
            if (!pLogicalSwapchain || pLogicalSwapchain->fakeImages.empty() || index >= pLogicalSwapchain->commandBuffersNoEffect.size())
                continue;
            const VkFence fence = ensureEffectFence(pLogicalDevice, pLogicalSwapchain, index);
            if (fence != VK_NULL_HANDLE)
                fences.push_back(fence);
            if (index < pLogicalSwapchain->fencelessEffectQueues.size() && pLogicalSwapchain->fencelessEffectQueues[index] != VK_NULL_HANDLE)
                fenceless.push_back(pLogicalSwapchain->fencelessEffectQueues[index]);
            if (overlayVisible && overlayDraw == VK_NULL_HANDLE && index < pLogicalSwapchain->overlay.draws.size())
                overlayDraw = pLogicalSwapchain->overlay.draws[index].fence;
            if (const GpuChain* chain = pLogicalSwapchain->chain.get())
                for (const ChainSegment& segment : chain->segments)
                    if (index < segment.fences.size() && segment.fences[index] != VK_NULL_HANDLE)
                        helperFences.emplace_back(segment.helper, segment.fences[index]);
        }
        if (fences.empty() && fenceless.empty() && overlayDraw == VK_NULL_HANDLE && helperFences.empty())
            return VK_SUCCESS;

        const PFN_vkWaitForFences waitForFences     = pLogicalDevice->vkd.WaitForFences;
        const VkDevice            device            = pLogicalDevice->device;
        const uint64_t            overlayPatienceNs = overlayDraw == VK_NULL_HANDLE ? 0 : pLogicalDevice->imguiOverlay->drawPatienceNs();
        l.unlock();
        const VkResult waited       = waitForOurFences(waitForFences, device, fences, UINT64_MAX);
        const VkResult overlayReady = overlayDraw == VK_NULL_HANDLE ? VK_SUCCESS : waitForFences(device, 1, &overlayDraw, VK_TRUE, overlayPatienceNs);
        VkResult       helperReady  = VK_SUCCESS;
        for (const auto& [helper, fence] : helperFences)
            if (const VkResult r = helper->device.vkd.WaitForFences(helper->device.device, 1, &fence, VK_TRUE, helperFrameDeadlineNs); r != VK_SUCCESS)
                helperReady = r;
        l.lock();
        if (helperReady != VK_SUCCESS)
        {
            if (pLogicalDevice->offload)
                pLogicalDevice->offload->onGameGpuUntilReload = true;
            Logger::err(std::string(helperReady == VK_TIMEOUT ? "another GPU did not finish its previous frame within a second"
                                                              : "the wait for another GPU's previous frame failed (" + std::to_string(helperReady) + ")")
                        + ", so the rows on other GPUs move to the game's GPU until a reload");
            for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
                if (LogicalSwapchain* pLogicalSwapchain = findSwapchain(pPresentInfo->pSwapchains[i]); pLogicalSwapchain && pLogicalSwapchain->chain)
                    pLogicalSwapchain->chain->failed = true;
        }
        if (overlayReady != VK_SUCCESS && overlayReady != VK_TIMEOUT)
            Logger::err("the wait for the overlay's previous draw failed: " + std::to_string(overlayReady));
        if (waited != VK_SUCCESS)
            return waited;
        const VkResult waitResult = drainQueues(pLogicalDevice->vkd.QueueWaitIdle, fenceless);
        return waitResult;
    }

    static constexpr double frameRatesReportMs = 60000.0;

    static void logFrameRates(LogicalDevice& device, const std::string& over, uint64_t intervals, double milliseconds)
    {
        if (intervals == 0 || milliseconds <= 0.0 || (Logger::logLevel() > LogLevel::Info && !Logger::isHistoryEnabled()))
            return;
        const double                  timed      = static_cast<double>(intervals) * 1000.0 / milliseconds;
        const std::optional<uint32_t> multiplier = device.imguiOverlay ? device.imguiOverlay->frameGenerationMultiplierHere() : std::nullopt;
        const FrameRates              rates      = frameRatesOf(timed, multiplier, effectsRunAfterFrameGeneration(device.frameGenerationPlace));
        char                          line[320];
        std::snprintf(line, sizeof(line), "frame rates %s: game %.1f FPS %s, shown %.1f FPS %s; %s", over.c_str(), rates.gameFps,
                      rates.gameMeasured ? "measured" : "estimated", rates.shownFps, rates.shownMeasured ? "measured" : "estimated",
                      frameGenerationPlaceWords(device.frameGenerationPlace));
        Logger::info(std::string(line) + (multiplier ? ", " + std::to_string(*multiplier) + "x" : std::string()));
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        const auto                  presentedAt = std::chrono::steady_clock::now();
        std::unique_lock<LayerLock> l(globalLock);

        beginWaylandInputFrame();
        const bool readMouse = effectsReadMouse(presentControls().effectsOn);
        beginMouseInputFrame(readMouse);
        beginReshadeInputFrame(readMouse);

        LogicalDevice* pLogicalDevice = findDevice(GetKey(queue));
        if (!pLogicalDevice)
            return VK_ERROR_DEVICE_LOST;
        if (!pLogicalDevice->queue)
        {
            PFN_vkQueuePresentKHR downchainPresent = pLogicalDevice->vkd.QueuePresentKHR;
            l.unlock();
            return downchainPresent(queue, pPresentInfo);
        }

        PresentControls& controls       = presentControls();
        const ReloadReason reloadReason = pollControls(pLogicalDevice, controls);
        if (reloadReason != ReloadReason::None)
        {
            const VkResult reloaded = reloadConfigAndEffects(l, queue, pLogicalDevice, reloadReason == ReloadReason::FileChanged);
            if (reloaded != VK_SUCCESS)
                return reloaded;
        }
        reloadAfterResize();
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
        {
            LogicalSwapchain* pLogicalSwapchain = findSwapchain(pPresentInfo->pSwapchains[i]);
            if (pLogicalSwapchain)
            {
                PresentIntervals& seen = pLogicalSwapchain->presentIntervals;
                seen.record(presentedAt);
                const double window = seen.intervalSumMs() - pLogicalSwapchain->ratesReportedMs;
                if (window >= frameRatesReportMs)
                {
                    logFrameRates(*pLogicalDevice, "over the last " + std::to_string(static_cast<int>(window / 1000.0)) + " s",
                                  seen.intervalCount() - pLogicalSwapchain->ratesReportedIntervals, window);
                    pLogicalSwapchain->ratesReportedIntervals = seen.intervalCount();
                    pLogicalSwapchain->ratesReportedMs        = seen.intervalSumMs();
                }
            }
            if (pLogicalSwapchain && pLogicalSwapchain->chain && pLogicalSwapchain->chain->failed)
                reloadEffectsForSwapchain(pLogicalSwapchain, pConfig.get(), effectRegistry.getActiveEffects());
        }

        static thread_local std::vector<VkSemaphore> presentSemaphores;
        static thread_local std::vector<VkPipelineStageFlags> waitStages;
        presentSemaphores.clear();
        presentSemaphores.reserve(pPresentInfo->swapchainCount);
        waitStages.assign(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

        updateOverlayState(pLogicalDevice, controls.effectsOn);

        const VkQueue  submitQueue = submitQueueFor(*pLogicalDevice, queue);
        const VkResult waited      = waitForPreviousSubmissions(l, pLogicalDevice, pPresentInfo);
        if (!deviceSurvivedUnlock(queue, pLogicalDevice))
        {
            Logger::err("device was destroyed while the present waited for the layer's previous frame");
            return VK_ERROR_DEVICE_LOST;
        }
        if (waited != VK_SUCCESS)
            return waited;

        bool appWaitsConsumed = false;
        bool overlayOffered   = false;

        for (unsigned int i = 0; i < pPresentInfo->swapchainCount; i++)
        {
            uint32_t       index     = pPresentInfo->pImageIndices[i];
            VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[i];
            LogicalSwapchain* pLogicalSwapchain = findSwapchain(swapchain);
            if (!pLogicalSwapchain || pLogicalSwapchain->fakeImages.empty() || index >= pLogicalSwapchain->commandBuffersNoEffect.size())
                continue;

            const bool chainFailed = pLogicalSwapchain->chain && pLogicalSwapchain->chain->failed;
            const bool runEffects = controls.effectsOn && index < pLogicalSwapchain->commandBuffersEffect.size() && !chainFailed;

            VkFence effectFence = index < pLogicalSwapchain->effectFences.size()
                ? pLogicalSwapchain->effectFences[index]
                : VK_NULL_HANDLE;
            if (effectFence != VK_NULL_HANDLE)
            {
                pLogicalSwapchain->collectEffectTimings(index);
                if (pLogicalSwapchain->chain)
                {
                    const ImGuiOverlay* overlay = pLogicalDevice->imguiOverlay.get();
                    if (overlay && (overlay->showsCard(OverlaySection::Performance) || overlay->showsCard(OverlaySection::Gpus)))
                        pLogicalSwapchain->chain->readRoundTrip(*pLogicalDevice, index);
                    else
                        pLogicalSwapchain->chain->roundTripMilliseconds = -1.0f;
                    pLogicalSwapchain->chain->readSegmentTimings(index);
                }

                const VkResult resetResult =
                    pLogicalDevice->vkd.ResetFences(pLogicalDevice->device, 1, &effectFence);
                if (resetResult != VK_SUCCESS)
                {
                    Logger::err("effect fence reset failed for image " + std::to_string(index) + ": "
                                + std::to_string(resetResult));
                    return resetResult;
                }
            }

            GpuChain* chain = runEffects && pLogicalSwapchain->chain && !pLogicalSwapchain->chain->failed ? pLogicalSwapchain->chain.get() : nullptr;
            if (runEffects)
            {
                for (auto& effect : pLogicalSwapchain->effects)
                    effect->updateEffect(index);
                if (chain)
                    for (ChainSegment& segment : chain->segments)
                        for (auto& effect : segment.effects)
                            effect->updateEffect(index);
            }

            VkSubmitInfo submitInfo = {};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = appWaitsConsumed ? 0 : pPresentInfo->waitSemaphoreCount;
            submitInfo.pWaitSemaphores    = appWaitsConsumed ? nullptr : pPresentInfo->pWaitSemaphores;
            submitInfo.pWaitDstStageMask  = appWaitsConsumed ? nullptr : waitStages.data();
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = runEffects
                ? &pLogicalSwapchain->commandBuffersEffect[index]
                : &pLogicalSwapchain->commandBuffersNoEffect[index];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &pLogicalSwapchain->semaphores[index];

            VkResult vr               = VK_SUCCESS;
            bool     effectsSubmitted = false;
            if (chain)
            {
                static thread_local std::vector<HandOffSide> sides;
                sides.clear();
                for (const ChainSegment& segment : chain->segments)
                    sides.push_back(segment.helper ? HandOffSide{&segment.device->vkd, segment.device->device, segment.device->queue}
                                                   : HandOffSide{&pLogicalDevice->vkd, pLogicalDevice->device, submitQueue});
                const HandOff handOff = submitThroughChain({&pLogicalDevice->vkd, pLogicalDevice->device, submitQueue}, sides, *chain, index, submitInfo,
                                                                   pLogicalSwapchain->semaphores[index], effectFence);
                effectsSubmitted = handOff.handedOver;
                if (handOff.handedOver && index < chain->roundTripPending.size())
                    chain->roundTripPending[index] = true;
                if (handOff.handedOver)
                    chain->markSegmentTimings(index);
                if (!handOff.handedOver)
                {
                    chain->failed                                 = true;
                    pLogicalDevice->offload->onGameGpuUntilReload = true;
                    Logger::err("the rows on other GPUs move to the game's GPU until a reload: " + handOff.failure);
                    const VkPipelineStageFlags allCommands  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                    VkSemaphore                fallbackWait = handOff.fallbackWait;
                    VkSubmitInfo               passThrough  = submitInfo;
                    passThrough.pCommandBuffers             = &pLogicalSwapchain->commandBuffersNoEffect[index];
                    if (handOff.headRan)
                    {
                        passThrough.waitSemaphoreCount = fallbackWait != VK_NULL_HANDLE ? 1 : 0;
                        passThrough.pWaitSemaphores    = fallbackWait != VK_NULL_HANDLE ? &fallbackWait : nullptr;
                        passThrough.pWaitDstStageMask  = fallbackWait != VK_NULL_HANDLE ? &allCommands : nullptr;
                    }
                    vr = pLogicalDevice->vkd.QueueSubmit(submitQueue, 1, &passThrough, effectFence);
                }
            }
            else
            {
                vr               = pLogicalDevice->vkd.QueueSubmit(submitQueue, 1, &submitInfo, effectFence);
                effectsSubmitted = runEffects && vr == VK_SUCCESS;
            }
            if (vr != VK_SUCCESS && runEffects && !chain)
            {
                Logger::err("the effect submission failed for image " + std::to_string(index) + ": " + std::to_string(vr)
                            + ", presenting it without effects");
                submitInfo.pCommandBuffers = &pLogicalSwapchain->commandBuffersNoEffect[index];
                vr = pLogicalDevice->vkd.QueueSubmit(submitQueue, 1, &submitInfo, effectFence);
            }
            if (vr != VK_SUCCESS)
            {
                Logger::err("the frame submission failed for image " + std::to_string(index) + ": " + std::to_string(vr));
                pLogicalSwapchain->markTimingSubmission(index, false);
                // the reset fence saw no submission, so it is put back signalled or the next wait never returns
                if (effectFence != VK_NULL_HANDLE)
                {
                    pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, effectFence, nullptr);
                    createSignalledFence(pLogicalDevice, &pLogicalSwapchain->effectFences[index]);
                }
                return vr;
            }
            appWaitsConsumed = true;
            if (index < pLogicalSwapchain->fencelessEffectQueues.size())
                pLogicalSwapchain->fencelessEffectQueues[index] = effectFence == VK_NULL_HANDLE ? submitQueue : VK_NULL_HANDLE;
            Logger::trace("the layer submitted a frame");
            pLogicalSwapchain->markTimingSubmission(index, effectsSubmitted && effectFence != VK_NULL_HANDLE);

            presentSemaphores.push_back(overlayOffered ? pLogicalSwapchain->semaphores[index]
                                                       : submitOverlayFrame(pLogicalDevice, pLogicalSwapchain, index, submitQueue));
            overlayOffered = true;
        }

        VkPresentInfoKHR presentInfo = *pPresentInfo;
        if (appWaitsConsumed)
        {
            presentInfo.waitSemaphoreCount = presentSemaphores.size();
            presentInfo.pWaitSemaphores    = presentSemaphores.data();
        }

        PFN_vkQueuePresentKHR downchainPresent = pLogicalDevice->vkd.QueuePresentKHR;
        l.unlock();

        VkResult presentResult = downchainPresent(queue, &presentInfo);
        if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR)
        {
            l.lock();
            bool rebuild = false;
            for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
                if (offInThisRun().outOfDateDue.erase(pPresentInfo->pSwapchains[i]) > 0)
                {
                    rebuild = true;
                    if (pPresentInfo->pResults)
                        pPresentInfo->pResults[i] = VK_ERROR_OUT_OF_DATE_KHR;
                }
            l.unlock();
            if (rebuild)
            {
                Logger::info("the frame was presented and VK_ERROR_OUT_OF_DATE_KHR returned, so the game rebuilds its swapchain");
                presentResult = VK_ERROR_OUT_OF_DATE_KHR;
            }
        }

        static std::atomic<uint64_t> presentCycles{0};
        uint64_t cycle = ++presentCycles;
        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("present cycle " + std::to_string(cycle) + " -> result " + std::to_string(presentResult));
        else if (Logger::logLevel() <= LogLevel::Debug && (cycle <= 5 || cycle % 600 == 0))
            Logger::debug("present cycle " + std::to_string(cycle) + " -> result " + std::to_string(presentResult));
        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
        {
            static std::atomic<uint64_t> badResults{0};
            uint64_t bad = ++badResults;
            if (bad <= 20 || bad % 300 == 0)
                Logger::warn("down-chain present returned " + std::to_string(presentResult)
                             + " (occurrence " + std::to_string(bad) + ")");
        }

        return presentResult;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
    {
        if (!swapchain)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroySwapchainKHR " + convertToString(swapchain));
        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice)
        {
            Logger::err("vkDestroySwapchainKHR was given a device the layer never created, so it is left alone");
            return;
        }
        if (LogicalSwapchain* pLogicalSwapchain = findSwapchain(swapchain))
        {
            Logger::info("frame statistics: " + std::to_string(pLogicalSwapchain->presentIntervals.intervalCount()) + " present intervals over "
                         + std::to_string(pLogicalSwapchain->presentIntervals.intervalSumMs()) + " ms, "
                         + std::to_string(pLogicalSwapchain->chain ? pLogicalSwapchain->chain->roundTripReads : 0) + " round trip reads, " + std::to_string(mousePointerQueries())
                         + " mouse queries");
            logFrameRates(*pLogicalDevice, "over the swapchain's life", pLogicalSwapchain->presentIntervals.intervalCount(),
                          pLogicalSwapchain->presentIntervals.intervalSumMs());
            pLogicalSwapchain->destroy();
            swapchainMap.erase(swapchain);
        }
        offInThisRun().handedBack.erase(swapchain);
        offInThisRun().outOfDateDue.erase(swapchain);

        pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImage(VkDevice                     device,
                                                        const VkImageCreateInfo*     pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkImage*                     pImage)
    {
        depthCaptureRunning.store(true, std::memory_order_relaxed);
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice)
            return VK_ERROR_UNKNOWN;
        if (isDepthFormat(pCreateInfo->format) && pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT
            && ((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            && (pCreateInfo->usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) == 0)
        {
            Logger::debug("detected depth image with format: " + convertToString(pCreateInfo->format));
            Logger::debug(std::to_string(pCreateInfo->extent.width) + "x" + std::to_string(pCreateInfo->extent.height));
            Logger::debug(
                std::to_string((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

            VkImageCreateInfo modifiedCreateInfo = *pCreateInfo;
            modifiedCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            VkResult result = pLogicalDevice->vkd.CreateImage(device, &modifiedCreateInfo, pAllocator, pImage);
            if (result == VK_SUCCESS)
                pLogicalDevice->depthImages.push_back({*pImage, pCreateInfo->format, VK_NULL_HANDLE});

            return result;
        }
        else
        {
            return pLogicalDevice->vkd.CreateImage(device, pCreateInfo, pAllocator, pImage);
        }
    }

    void viewBoundDepthImage(LogicalDevice* pLogicalDevice, VkImage image)
    {
        const auto tracked = std::find_if(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(),
                                          [image](const auto& depthImage) { return depthImage.image == image; });
        if (tracked == pLogicalDevice->depthImages.end() || tracked->view != VK_NULL_HANDLE)
            return;

        try
        {
            tracked->view = createImageViews(pLogicalDevice, tracked->format, {image},
                                             VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT)[0];
        }
        catch (const std::exception& e)
        {
            Logger::err(std::string("creating the depth image view failed, so effects run without this depth image: ") + e.what());
            return;
        }
        Logger::debug("created depth image view");

        const size_t viewCount = std::count_if(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(),
                                               [](const auto& depthImage) { return depthImage.view != VK_NULL_HANDLE; });
        if (viewCount > 1)
            return;

        DepthState depth = getDepthState(pLogicalDevice);
        for (auto& [swapchainHandle, pLogicalSwapchain] : swapchainMap)
        {
            if (pLogicalSwapchain->pLogicalDevice != pLogicalDevice)
                continue;
            if (pLogicalSwapchain->fakeImages.empty())
                continue;

            reallocateCommandBuffers(pLogicalDevice, pLogicalSwapchain.get(), depth);
            Logger::debug("reallocated CommandBuffers for swapchain " + convertToString(swapchainHandle));
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice)
            return VK_ERROR_UNKNOWN;
        const VkResult result = pLogicalDevice->vkd.BindImageMemory(device, image, memory, memoryOffset);
        if (result == VK_SUCCESS)
            viewBoundDepthImage(pLogicalDevice, image);
        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice || !pLogicalDevice->vkd.BindImageMemory2)
            return VK_ERROR_UNKNOWN;
        const VkResult result = pLogicalDevice->vkd.BindImageMemory2(device, bindInfoCount, pBindInfos);
        if (result != VK_SUCCESS)
            return result;
        for (uint32_t i = 0; i < bindInfoCount; i++)
            viewBoundDepthImage(pLogicalDevice, pBindInfos[i].image);
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
    {
        if (!image)
            return;

        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = findDevice(GetKey(device));
        if (!pLogicalDevice)
        {
            Logger::err("vkDestroyImage was given a device the layer never created, so the image is left alone");
            return;
        }

        auto it = std::find_if(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(),
                               [image](const auto& depthImage) { return depthImage.image == image; });
        if (it != pLogicalDevice->depthImages.end())
        {
            const VkImageView removedView = it->view;
            pLogicalDevice->depthImages.erase(it);

            DepthState depth = getDepthState(pLogicalDevice);
            for (auto& [swapchainHandle, pLogicalSwapchain] : swapchainMap)
            {
                if (pLogicalSwapchain->pLogicalDevice != pLogicalDevice)
                    continue;
                if (pLogicalSwapchain->fakeImages.empty())
                    continue;
                if (pLogicalSwapchain->effectDepthImage != image)
                    continue;

                reallocateCommandBuffers(pLogicalDevice, pLogicalSwapchain.get(), depth);
                Logger::debug("reallocated CommandBuffers for swapchain " + convertToString(swapchainHandle));
            }

            if (removedView != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, removedView, nullptr);
        }

        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateWaylandSurfaceKHR(
        VkInstance                              instance,
        const VkWaylandSurfaceCreateInfoKHR*    pCreateInfo,
        const VkAllocationCallbacks*            pAllocator,
        VkSurfaceKHR*                           pSurface)
    {
        scoped_lock l(globalLock);

        Logger::trace("vkCreateWaylandSurfaceKHR");

        const LayerInstance* pInstance = findInstance(GetKey(instance));
        if (!pInstance)
            return VK_ERROR_UNKNOWN;

        if (pCreateInfo && pCreateInfo->display)
            setWaylandDisplay(pCreateInfo->display);
        if (pCreateInfo && pCreateInfo->surface)
            setWaylandSurface(pCreateInfo->surface);

        auto nextFunc = (PFN_vkCreateWaylandSurfaceKHR) pInstance->vki.GetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR");
        if (!nextFunc)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        return nextFunc(instance, pCreateInfo, pAllocator, pSurface);
    }

    typedef VkResult(VKAPI_PTR* PFN_vkCreateOpaqueSurfaceKHR)(VkInstance, const void*, const VkAllocationCallbacks*, VkSurfaceKHR*);

    static VkResult createX11Surface(VkInstance                   instance,
                                     const void*                  pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkSurfaceKHR*                pSurface,
                                     const char*                  name)
    {
        scoped_lock l(globalLock);

        Logger::trace(name);

        const LayerInstance* pInstance = findInstance(GetKey(instance));
        if (!pInstance)
            return VK_ERROR_UNKNOWN;

        useX11InputBackend();

        auto nextFunc = (PFN_vkCreateOpaqueSurfaceKHR) pInstance->vki.GetInstanceProcAddr(instance, name);
        if (!nextFunc)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        return nextFunc(instance, pCreateInfo, pAllocator, pSurface);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateXlibSurfaceKHR(VkInstance                   instance,
                                                                 const void*                  pCreateInfo,
                                                                 const VkAllocationCallbacks* pAllocator,
                                                                 VkSurfaceKHR*                pSurface)
    {
        return createX11Surface(instance, pCreateInfo, pAllocator, pSurface, "vkCreateXlibSurfaceKHR");
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateXcbSurfaceKHR(VkInstance                   instance,
                                                                const void*                  pCreateInfo,
                                                                const VkAllocationCallbacks* pAllocator,
                                                                VkSurfaceKHR*                pSurface)
    {
        return createX11Surface(instance, pCreateInfo, pAllocator, pSurface, "vkCreateXcbSurfaceKHR");
    }


    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        if (pPropertyCount)
            *pPropertyCount = 1;

        if (pProperties)
        {
            std::strcpy(pProperties->layerName, VKBASALT_NAME);
            std::strcpy(pProperties->description, "a post processing layer with overlay UI");
            pProperties->implementationVersion = 1;
            pProperties->specVersion =
                VK_MAKE_VERSION(VKBASALT_LAYER_API_MAJOR, VKBASALT_LAYER_API_MINOR, VKBASALT_LAYER_API_PATCH);
        }

        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(VkPhysicalDevice   physicalDevice,
                                                                uint32_t*          pPropertyCount,
                                                                VkLayerProperties* pProperties)
    {
        return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(const char*            pLayerName,
                                                                      uint32_t*              pPropertyCount,
                                                                      VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
                                                                    const char*            pLayerName,
                                                                    uint32_t*              pPropertyCount,
                                                                    VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            if (physicalDevice == VK_NULL_HANDLE)
            {
                return VK_SUCCESS;
            }

            scoped_lock l(globalLock);
            const LayerInstance* pInstance = findInstance(GetKey(physicalDevice));
            if (!pInstance)
                return VK_ERROR_UNKNOWN;
            return pInstance->vki.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
        }

        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }
}

extern "C"
{

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkDaedalus_GetDeviceProcAddr(VkDevice device, const char* pName);
    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkDaedalus_GetInstanceProcAddr(VkInstance instance, const char* pName);

#define GETPROCADDR(func) \
    if (!std::strcmp(pName, "vk" #func)) \
        return (PFN_vkVoidFunction) &vkBasalt::vkBasalt_##func;

    // some games ask vkGetDeviceProcAddr for instance-level functions
#define INTERCEPT_CALLS \
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) \
        return (PFN_vkVoidFunction) &vkDaedalus_GetInstanceProcAddr; \
    GETPROCADDR(EnumerateInstanceLayerProperties); \
    GETPROCADDR(EnumerateInstanceExtensionProperties); \
    GETPROCADDR(CreateInstance); \
    GETPROCADDR(DestroyInstance); \
\
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) \
        return (PFN_vkVoidFunction) &vkDaedalus_GetDeviceProcAddr; \
    GETPROCADDR(EnumerateDeviceLayerProperties); \
    GETPROCADDR(EnumerateDeviceExtensionProperties); \
    GETPROCADDR(CreateDevice); \
    GETPROCADDR(DestroyDevice); \
\
    if (!vkBasalt::programBypassed()) \
    { \
        GETPROCADDR(CreateWaylandSurfaceKHR); \
        GETPROCADDR(CreateXlibSurfaceKHR); \
        GETPROCADDR(CreateXcbSurfaceKHR); \
        GETPROCADDR(CreateSwapchainKHR); \
        GETPROCADDR(GetSwapchainImagesKHR); \
        GETPROCADDR(QueuePresentKHR); \
        GETPROCADDR(DestroySwapchainKHR); \
\
        if (vkBasalt::settingsManager.getDepthCapture()) \
        { \
            GETPROCADDR(CreateImage); \
            GETPROCADDR(DestroyImage); \
            GETPROCADDR(BindImageMemory); \
            GETPROCADDR(BindImageMemory2); \
            if (!std::strcmp(pName, "vkBindImageMemory2KHR")) \
                return (PFN_vkVoidFunction) &vkBasalt::vkBasalt_BindImageMemory2; \
        } \
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkDaedalus_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        vkBasalt::initConfigs();

        INTERCEPT_CALLS

        if (!device)
            return nullptr;

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            const vkBasalt::LogicalDevice* pLogicalDevice = vkBasalt::findDevice(vkBasalt::GetKey(device));
            if (!pLogicalDevice || !pLogicalDevice->vkd.GetDeviceProcAddr)
                return nullptr;
            return pLogicalDevice->vkd.GetDeviceProcAddr(device, pName);
        }
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkDaedalus_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        vkBasalt::initConfigs();

        INTERCEPT_CALLS

        if (!instance)
            return nullptr;

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            const vkBasalt::LayerInstance* pInstance = vkBasalt::findInstance(vkBasalt::GetKey(instance));
            if (!pInstance || !pInstance->vki.GetInstanceProcAddr)
                return nullptr;
            return pInstance->vki.GetInstanceProcAddr(instance, pName);
        }
    }

}
