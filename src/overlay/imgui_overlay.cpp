#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "live_uniform_policy.hpp"
#include "reshade_parser.hpp"
#include "logger.hpp"
#include "mouse_input.hpp"
#include "mouse_input_wayland.hpp"
#include "keyboard_input.hpp"
#include "input_blocker.hpp"
#include "config_serializer.hpp"
#include "wayland_input_common.hpp"
#include "overlay/hud.hpp"
#include "util.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/overlay_style.hpp"
#include "keyboard_input_x11.hpp"
#include "wayland_display.hpp"
#include "logical_swapchain.hpp"
#include "frame_generation.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "imgui.h"
#include "imgui_internal.h"

namespace vkBasalt
{
    ImGuiOverlay::ImGuiOverlay(LogicalDevice* device)
        : pLogicalDevice(device)
    {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendRendererName = "vkDaedalus";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        io.ConfigErrorRecoveryEnableAssert = false;
        context->ErrorCallback = [](ImGuiContext*, void*, const char* message) {
            Logger::err(std::string("the overlay recovered from an ImGui error, which is a layer bug: ") + message);
        };
        UiMap::enableIfAsked(context);

        std::string iniPath = ConfigSerializer::getBaseConfigDir() + "/imgui.ini";
        std::ifstream iniFile(iniPath);
        std::string iniContent((std::istreambuf_iterator<char>(iniFile)),
                                std::istreambuf_iterator<char>());

        if (!iniContent.empty())
            ImGui::LoadIniSettingsFromDisk(iniPath.c_str());

        useOverlayPalette(settingsManager.getPalette());
        applyOverlayStyle(ImGui::GetStyle(), activePalette());
        try
        {
            fonts          = loadOverlayFonts(*io.Fonts);
            io.FontDefault = fonts.body;
        }
        catch (const std::exception& e)
        {
            Logger::err(std::string("the overlay draws with ImGui's own font: ") + e.what());
            ImFont* own    = io.Fonts->AddFontDefault();
            fonts          = {own, own, own};
            io.FontDefault = own;
        }
        baseStyle = ImGui::GetStyle();

        try
        {
            renderer           = std::make_unique<OverlayRenderer>(*pLogicalDevice);
            backendInitialized = true;
        }
        catch (const std::exception& e)
        {
            Logger::err(std::string("the overlay's renderer could not be set up: ") + e.what());
        }

        initialized = true;
        if (backendInitialized)
            Logger::info("ImGui overlay initialized");
        else
            Logger::err("overlay off: its Vulkan backend could not be set up, so the overlay key does nothing");
    }

    ImGuiOverlay::~ImGuiOverlay()
    {
        stopShaderTest();
        if (!initialized) return;

        if (profileDirty && !hasPendingConfig() && !activeProfilePath.empty())
            autoSaveProfile();

        waitForAllDraws();

        ImGui::SetCurrentContext(context);
        std::string iniPath = ConfigSerializer::getBaseConfigDir() + "/imgui.ini";
        ImGui::SaveIniSettingsToDisk(iniPath.c_str());

        if (renderer)
        {
            ImVector<ImTextureData*>& textures = ImGui::GetPlatformIO().Textures;
            renderer->releaseTextures(textures.Data, textures.Size);
        }
        ImGui::DestroyContext(context);
        renderer.reset();

        if (visible)
        {
            visible = false;
            setInputBlocked(false);
        }

        Logger::info("ImGui overlay destroyed");
    }

    void ImGuiOverlay::switchOff(const std::string& why)
    {
        backendInitialized = false;
        Logger::err("overlay off: " + why);
        Logger::setHistoryEnabled(false);
        if (visible)
        {
            visible = false;
            setInputBlocked(false);
            setOverlayReadsMouse(false);
            setOverlayTyping(false);
        }
    }

    void ImGuiOverlay::comeBackInThisRun()
    {
        const std::string back = profileBeforeOff.empty() || profileBeforeOff == BYPASS_PROFILE_NAME ? DEFAULT_PROFILE_NAME : profileBeforeOff;
        if (!ConfigSerializer::setActiveProfile(activeGameName, back))
            Logger::warn("configs/.active_profiles could not be written, so " + activeGameName + " stays off from its next launch");
        comeBackProfile       = back;
        comeBackSwitchPending = true;
        if (!visible)
            toggle();
    }

    void ImGuiOverlay::toggle()
    {
        if (!visible && !backendInitialized)
        {
            Logger::warn("the overlay was switched off after a failure, so it does not open and input stays with the game");
            return;
        }
        visible = !visible;
        Logger::debug(visible ? "overlay opened" : "overlay closed");
        setOverlayReadsMouse(visible);
        setOverlayTyping(visible);
        if (visible)
            discardPendingMouseInput();
        if (visible && !launchNoticeOver)
        {
            launchNoticeOver = true;
            if (launchNoticeStart)
                Logger::info("launch notice left: the overlay opened");
        }
        if (visible)
        {
            rescanForConflictingLayer();
            frameShown     = 0.0f;
            desktopOutputs = isWayland() ? getWaylandOutputs() : std::vector<WaylandOutputFacts>{};
            desktopXftDpi  = isWayland() ? std::nullopt : xftDpiFrom(x11ResourceString());
        }
        listeningForKey = KeyBinding::None;
        if (visible)
            getKeyboardState();
        else
            Logger::setHistoryEnabled(false);
        setInputBlocked(visible);
    }

    void ImGuiOverlay::startAddedEffects(EffectAddBatch batch)
    {
        std::vector<EffectCompileJobs::Compile> parameterReads;
        for (const EffectAddition& effect : batch.effects)
            if (!effect.path.empty())
                parameterReads.push_back(
                    [path = effect.path] { testShaderCompilation(std::filesystem::path(path).stem().string(), path); });
        effectCompileJobs.start(std::move(batch), std::move(parameterReads));
    }

    void ImGuiOverlay::processAddedEffects()
    {
        if (!pEffectRegistry)
            return;

        if (const std::optional<EffectAddBatch> read = effectCompileJobs.takeParametersRead())
        {
            std::vector<std::string> names;
            for (const EffectAddition& effect : read->effects)
            {
                pEffectRegistry->ensureEffect(effect.instanceName, effect.effectType);
                names.push_back(effect.instanceName);
            }
            effectCompileJobs.warmChain(chainCompiles ? chainCompiles(names) : std::vector<EffectCompileJobs::Compile>{});
        }

        if (const std::optional<EffectAddBatch> batch = effectCompileJobs.takeReady())
        {
            std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();
            size_t position = (batch->position && *batch->position <= selectedEffects.size()) ? *batch->position : selectedEffects.size();
            for (const EffectAddition& effect : batch->effects)
            {
                if (std::find(selectedEffects.begin(), selectedEffects.end(), effect.instanceName) != selectedEffects.end())
                    continue;
                selectedEffects.insert(selectedEffects.begin() + static_cast<std::ptrdiff_t>(position), effect.instanceName);
                position++;
                pEffectRegistry->setEffectEnabled(effect.instanceName, true);
            }
            pEffectRegistry->setSelectedEffects(selectedEffects);
            applyRequested = true;
            markDefinitionsEdited();
        }
    }

    void ImGuiOverlay::settleEdits()
    {
        if (!paramsDirty && !profileDirty)
            return;
        if (std::chrono::steady_clock::now() - lastChangeTime < std::chrono::milliseconds(settingsManager.getAutoApplyDelayMs()))
            return;
        if (paramsDirty && settingsManager.getAutoApply())
        {
            applyRequested = true;
            paramsDirty    = false;
        }
        if (profileDirty && !saveFailed && !hasPendingConfig() && !activeProfilePath.empty())
        {
            autoSaveProfile();
            saveFailed = profileDirty;
        }
    }

    void ImGuiOverlay::updateState(OverlayState newState)
    {
        state = std::move(newState);

        if (!pEffectRegistry)
            return;

        const auto& selectedEffects = pEffectRegistry->getSelectedEffects();
        for (const auto& effectName : selectedEffects)
        {
            if (!pEffectRegistry->hasEffect(effectName))
                pEffectRegistry->ensureEffect(effectName);
        }

        if (addsEffects())
            for (const auto& effectName : selectedEffects)
                if (pEffectRegistry->getEffectOnDevice(effectName, pLogicalDevice).readsDepth)
                    depthEffectPaths.insert(
                        std::filesystem::path(pEffectRegistry->getEffectFilePath(effectName)).lexically_normal().string());
    }

    std::vector<std::unique_ptr<EffectParam>> ImGuiOverlay::getModifiedParams()
    {
        if (!pEffectRegistry)
            return {};
        return pEffectRegistry->getAllParameters();
    }

    std::vector<std::string> ImGuiOverlay::getActiveEffects() const
    {
        return pEffectRegistry ? pEffectRegistry->getActiveEffects() : std::vector<std::string>{};
    }

    const std::vector<std::string>& ImGuiOverlay::getSelectedEffects() const
    {
        static std::vector<std::string> empty;
        return pEffectRegistry ? pEffectRegistry->getSelectedEffects() : empty;
    }

    void ImGuiOverlay::collectSaveData(
        std::vector<std::string>& effects,
        std::vector<std::string>& disabledEffects,
        std::vector<ConfigParam>& params,
        std::map<std::string, std::string>& effectPaths,
        std::vector<PreprocessorDefinition>& allDefs,
        std::set<std::string>& liveParameterKeys)
    {
        if (!pEffectRegistry)
            return;

        effects = pEffectRegistry->getSelectedEffects();
        allDefs = pEffectRegistry->getSavedGlobalDefinitions();

        for (const auto& effectName : effects)
        {
            liveParameterKeys.insert(effectName + ".halfPrecision");
            liveParameterKeys.insert(effectName + ".gpu");
            if (const uint32_t gpu = pEffectRegistry->getEffectGpu(effectName); gpu != 0)
                params.push_back({effectName, "gpu", std::to_string(gpu)});
            for (auto* p : pEffectRegistry->getParametersForEffect(effectName))
            {
                for (const auto& [suffix, value] : p->serialize())
                    liveParameterKeys.insert(effectName + "." + (suffix.empty() ? p->name : suffix));
                if (!p->hasChanged())
                    continue;

                auto serialized = p->serialize();
                for (const auto& [suffix, value] : serialized)
                {
                    ConfigParam cp;
                    cp.effectName = p->effectName;
                    cp.paramName = suffix.empty() ? p->name : suffix;
                    cp.value = value;
                    params.push_back(cp);
                }
            }

            if (!pEffectRegistry->isEffectEnabled(effectName))
                disabledEffects.push_back(effectName);

            if (pEffectRegistry->isEffectBuiltIn(effectName))
            {
                std::string effectType = pEffectRegistry->getEffectType(effectName);
                if (!effectType.empty())
                    effectPaths[effectName] = effectType;
            }
            else
            {
                std::string path = pEffectRegistry->getEffectFilePath(effectName);
                if (!path.empty())
                    effectPaths[effectName] = path;

                const auto& defs = pEffectRegistry->getPreprocessorDefs(effectName);
                for (const auto& def : defs)
                    allDefs.push_back(def);

                if (pEffectRegistry->getAllowHalfPrecision(effectName))
                {
                    ConfigParam cp;
                    cp.effectName = effectName;
                    cp.paramName  = "halfPrecision";
                    cp.value      = "true";
                    params.push_back(cp);
                }
            }
        }
    }

    void ImGuiOverlay::autoSaveProfile()
    {
        if (!pEffectRegistry || activeProfilePath.empty())
            return;

        std::vector<std::string> effects, disabledEffects;
        std::vector<ConfigParam> params;
        std::map<std::string, std::string> effectPaths;
        std::vector<PreprocessorDefinition> allDefs;
        std::set<std::string> liveParameterKeys;
        collectSaveData(effects, disabledEffects, params, effectPaths, allDefs, liveParameterKeys);

        ProfileSettings profileSettings;
        profileSettings.gpus          = pEffectRegistry->editedGpuList();
        if (frameOrderProfilePath == activeProfilePath)
            profileSettings.effectsAfterFrameGeneration = effectsAfterFrameGeneration;

        if (ConfigSerializer::saveToPath(activeProfilePath, effects, disabledEffects, params, effectPaths, allDefs, profileSettings, liveParameterKeys))
        {
            profileDirty = false;
            Logger::debug("Auto-saved profile: " + activeProfilePath);
        }
    }

    void ImGuiOverlay::markLiveValuesChanged()
    {
        markDefinitionsEdited();
    }

    void ImGuiOverlay::markEffectValuesChanged(const std::string& effectName)
    {
        const bool builtIn = pEffectRegistry != nullptr
            && pEffectRegistry->isEffectBuiltIn(effectName);
        if (effectValueApplyMode(
                settingsManager.getLiveReshadeUniforms(), builtIn)
            == EffectValueApplyMode::LiveUniform)
        {
            markLiveValuesChanged();
            return;
        }

        markEdited();
    }

    void ImGuiOverlay::setSelectedEffects(const std::vector<std::string>& effects,
                                          const std::vector<std::string>& disabledEffects)
    {
        if (!pEffectRegistry)
            return;

        pEffectRegistry->setSelectedEffects(effects);

        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        for (const auto& effectName : effects)
        {
            bool enabled = (disabledSet.find(effectName) == disabledSet.end());
            pEffectRegistry->setEffectEnabled(effectName, enabled);
        }
    }

    void ImGuiOverlay::prepareSwapchain(LogicalSwapchain& swapchain)
    {
        if (!backendInitialized)
            return;
        try
        {
            renderer->prepare(swapchain.overlay, swapchain.format, swapchain.imageExtent, swapchain.overlayImageViews);
        }
        catch (const std::exception& e)
        {
            swapchain.overlay.destroy(*pLogicalDevice);
            switchOff(std::string("its draws for a swapchain could not be made: ") + e.what());
            return;
        }
        attached.push_back(&swapchain.overlay);
    }

    void ImGuiOverlay::detach(const SwapchainOverlay& overlay)
    {
        attached.erase(std::remove(attached.begin(), attached.end(), &overlay), attached.end());
    }

    void ImGuiOverlay::waitForAllDraws()
    {
        std::vector<VkFence> pending;
        for (const SwapchainOverlay* overlay : attached)
            for (VkFence fence : overlay->fences())
                pending.push_back(fence);
        waitForOurFences(pLogicalDevice->vkd.WaitForFences, pLogicalDevice->device, pending, 2'000'000'000ull);
    }

    std::optional<std::string> ImGuiOverlay::fixedConfigFile() const
    {
        const char* fixed = std::getenv("VKDAEDALUS_CONFIG_FILE");
        if (activeGameName.empty() || !activeProfileName.empty() || fixed == nullptr || *fixed == '\0')
            return std::nullopt;
        return std::string(fixed);
    }

    uint64_t ImGuiOverlay::drawPatienceNs() const
    {
        return static_cast<uint64_t>(std::clamp(averageFrameMs * 4.0f, 2.0f, 50.0f) * 1'000'000.0f);
    }

    void ImGuiOverlay::saveOverlaySettings()
    {
        settingsSaveFailed = !settingsManager.save();
        settingsSaved      = true;
    }

    ImGuiOverlay::Frame ImGuiOverlay::drawFrame(LogicalSwapchain& swapchain, uint32_t imageIndex)
    {
        const auto noticeNow = std::chrono::steady_clock::now();
        const bool noticeShowing = !visible && (shortNoticeShowing(noticeNow) || launchNoticeShowing(noticeNow));
        if (!backendInitialized || (!visible && !noticeShowing && !hudShowing()))
            return {};
        if (visible && previewingByMouse && !getMouseState().leftButton)
            previewingByMouse = false;
        if (visible && isPreviewing())
            return {};

        SwapchainOverlay& overlay = swapchain.overlay;
        if (imageIndex >= overlay.draws.size() || overlay.draws[imageIndex].fence == VK_NULL_HANDLE)
        {
            Logger::err("ImGui overlay: image index outside the allocated command buffers");
            return {};
        }
        OverlayDraw& draw = overlay.draws[imageIndex];

        const VkResult ready = pLogicalDevice->vkd.WaitForFences(pLogicalDevice->device, 1, &draw.fence, VK_TRUE, 0);
        if (ready == VK_TIMEOUT)
        {
            Logger::warn("ImGui fence wait timed out for image " + std::to_string(imageIndex));
            return {};
        }
        if (ready != VK_SUCCESS)
        {
            Logger::err("ImGui fence wait failed: " + std::to_string(ready));
            return {};
        }

        const uint32_t width  = overlay.extent.width;
        const uint32_t height = overlay.extent.height;
        currentWidth          = width;
        currentHeight         = height;

        ImGui::SetCurrentContext(context);
        currentFormat         = overlay.format;
        currentImageCount     = static_cast<uint32_t>(overlay.draws.size());

        const GpuChain* chain           = swapchain.chain && !swapchain.chain->failed ? swapchain.chain.get() : nullptr;
        swapchainColorSpace             = swapchain.swapchainCreateInfo.imageColorSpace;
        swapchainPresentMode            = swapchain.swapchainCreateInfo.presentMode;
        swapchainAppUsage               = swapchain.appImageUsage;
        swapchainUsage                  = swapchain.swapchainCreateInfo.imageUsage;
        swapchainMinImages              = swapchain.swapchainCreateInfo.minImageCount;
        swapchainMutable                = (swapchain.swapchainCreateInfo.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) != 0;
        const auto drawnAt              = std::chrono::steady_clock::now();
        const HudWindows windows        = visible                       ? HudWindows{true, showsCard(OverlaySection::Performance)}
                                          : !noticeShowing && hudShowing() ? hudWindowsFor(*hudItemsFrom(settingsManager.getHudItems()))
                                                                           : HudWindows{};
        latency.lastSecondMs            = windows.lastSecond ? swapchain.presentIntervals.within(drawnAt, fpsWindow) : std::vector<double>{};
        latency.lastTenSecondsMs        = windows.lastTenSeconds ? swapchain.presentIntervals.within(drawnAt, statsWindow) : std::vector<double>{};
        latency.effectsOn               = state.effectsEnabled;
        latency.effectTimingSetting     = settingsManager.getEffectGpuTiming();
        latency.effectTimingOn          = swapchain.timingEnabled;
        latency.effectsSpanMilliseconds = swapchain.timingSpanMilliseconds;
        latency.handsOff                = chain != nullptr && state.effectsEnabled;
        latency.roundTripMilliseconds   = chain && chain->roundTripMilliseconds >= 0.0f ? std::optional<float>(chain->roundTripMilliseconds) : std::nullopt;
        latency.tripGpus = chain && (showsCard(OverlaySection::Performance) || showsCard(OverlaySection::Gpus)) ? chain->tripGpus() : std::vector<uint32_t>{};
        latency.rowsAfterReturn         = chain && !chain->segments.empty() && chain->segments.back().rows > 0;
        latency.helperCopies.clear();
        if (chain && showsCard(OverlaySection::Gpus))
            for (const ChainSegment& segment : chain->segments)
                if (const std::optional<ChainSegment::HelperTimes> times = segment.helperTimes())
                {
                    float effectsMs = 0.0f;
                    for (const float ms : times->rowsMs)
                        effectsMs = ms >= 0.0f && effectsMs >= 0.0f ? effectsMs + ms : -1.0f;
                    latency.helperCopies.push_back({segment.gpu, times->copyInMs, effectsMs, times->copyOutMs});
                }
        chainCrossings.clear();
        if (chain && showsCard(OverlaySection::Debug) && debugWindowTab == 2)
            for (const ChainSegment& segment : chain->segments)
            {
                std::string runs;
                for (size_t i = 0; i < segment.rowNames.size(); i++)
                    runs += (i == 0 ? "" : i + 1 == segment.rowNames.size() ? " and " : ", ") + segment.rowNames[i];
                const std::string step = segment.helper ? "GPU " + std::to_string(segment.gpu) + " runs " + (runs.empty() ? "nothing" : runs)
                                                        : "GPU " + std::to_string(segment.gpu) + " copies it back" + (runs.empty() ? "" : " and runs " + runs);
                chainCrossings += (chainCrossings.empty() ? "" : "; ") + step;
            }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);

        static auto lastFrameTime = std::chrono::steady_clock::now();
        static float lastGoodDt = 0.008f;
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        if (dt > 0.0f && dt < 1.0f)
        {
            io.DeltaTime = dt;
            lastGoodDt = dt;
            averageFrameMs = averageFrameMs * 0.9f + (dt * 1000.0f) * 0.1f;
        }
        else
        {
            io.DeltaTime = lastGoodDt;
        }

        KeyboardState keyboard;
        if (!visible)
        {
            io.MousePos        = ImVec2(-FLT_MAX, -FLT_MAX);
            io.MouseDown[0]    = false;
            io.MouseDown[1]    = false;
            io.MouseDown[2]    = false;
            io.MouseWheel      = 0.0f;
            io.MouseDrawCursor = false;
        }
        else
        {
            setWaylandPointerBounds((int)width, (int)height);
            beginWaylandInputFrame();

            MouseState mouse = getMouseState();
            io.MousePos = ImVec2((float)mouse.x, (float)mouse.y);

            io.MouseDown[0] = mouse.leftButton;
            io.MouseDown[1] = mouse.rightButton;
            io.MouseDown[2] = mouse.middleButton;
            io.MouseWheel = mouse.scrollUpDelta;
            io.MouseDrawCursor = true;

            keyboard = getKeyboardState();
            for (char c : keyboard.typedChars)
                io.AddInputCharacter(c);
            if (keyboard.backspace) { io.AddKeyEvent(ImGuiKey_Backspace, true); io.AddKeyEvent(ImGuiKey_Backspace, false); }
            if (keyboard.del) { io.AddKeyEvent(ImGuiKey_Delete, true); io.AddKeyEvent(ImGuiKey_Delete, false); }
            if (keyboard.enter) { io.AddKeyEvent(ImGuiKey_Enter, true); io.AddKeyEvent(ImGuiKey_Enter, false); }
            if (keyboard.left) { io.AddKeyEvent(ImGuiKey_LeftArrow, true); io.AddKeyEvent(ImGuiKey_LeftArrow, false); }
            if (keyboard.right) { io.AddKeyEvent(ImGuiKey_RightArrow, true); io.AddKeyEvent(ImGuiKey_RightArrow, false); }
            if (keyboard.home) { io.AddKeyEvent(ImGuiKey_Home, true); io.AddKeyEvent(ImGuiKey_Home, false); }
            if (keyboard.end) { io.AddKeyEvent(ImGuiKey_End, true); io.AddKeyEvent(ImGuiKey_End, false); }
            if (keyboard.tab) { io.AddKeyEvent(ImGuiKey_Tab, true); io.AddKeyEvent(ImGuiKey_Tab, false); }
            io.AddKeyEvent(ImGuiMod_Ctrl, keyboard.ctrl);
            io.AddKeyEvent(ImGuiMod_Shift, keyboard.shift);
            io.AddKeyEvent(ImGuiMod_Alt, keyboard.alt);
        }

        ImGui::NewFrame();
        UiMap::beginFrame();
        ImGuiErrorRecoveryState frameStart;
        ImGui::ErrorRecoveryStoreState(&frameStart);
        try
        {
            if (visible)
                buildFrame(keyboard);
            else if (shortNoticeStart)
                renderShortNotice(noticeNow);
            else if (noticeShowing)
                renderLaunchNotice(noticeNow);
            else
                renderHud();
        }
        catch (const std::exception& e)
        {
            ImGui::ErrorRecoveryTryToRecoverState(&frameStart);
            ImGui::EndFrame();
            UiMap::endFrame();
            switchOff(std::string("a view failed while the overlay drew: ") + e.what());
            return {};
        }
        ImGui::Render();
        UiMap::endFrame();
        ImDrawData* drawData = ImGui::GetDrawData();
        try
        {
            if (OverlayRenderer::texturesPending(*drawData))
            {
                waitForAllDraws();
                renderer->updateTextures(*drawData);
            }
            renderer->record(draw, overlay, *drawData);
        }
        catch (const std::exception& e)
        {
            switchOff(e.what());
            return {};
        }

        const VkResult reset = pLogicalDevice->vkd.ResetFences(pLogicalDevice->device, 1, &draw.fence);
        if (reset != VK_SUCCESS)
        {
            switchOff("its fence could not be reset: " + std::to_string(reset));
            return {};
        }

        Logger::trace(visible            ? "overlay recorded a frame"
                      : shortNoticeStart ? "frame generation notice drew a frame"
                      : noticeShowing    ? "launch notice drew a frame"
                                         : "HUD drew a frame");
        return {draw.commandBuffer, draw.fence};
    }

}
