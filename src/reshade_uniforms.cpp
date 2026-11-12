#include "reshade_uniforms.hpp"

#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <random>
#include <utility>

#include <algorithm>
#include <atomic>

#include "logger.hpp"
#include "mouse_input.hpp"
#include "wayland_display.hpp"
#include "keyboard_input.hpp"
#include "reshade_input_map.hpp"
#include "reshade_uniform_storage.hpp"
#include "effects/effect_registry.hpp"
#include "effects/params/effect_param.hpp"
#include "chain_rows.hpp"

namespace vkBasalt
{
    namespace
    {
        thread_local std::mt19937 tlRng{std::random_device{}()};

        constexpr int windowsRandMax = 32767;

        int windowsRand()
        {
            return std::uniform_int_distribution<int>(0, windowsRandMax)(tlRng);
        }

        struct SharedMouse
        {
            MouseState current;
            MouseState previous;
            bool       primed = false;
        };
        SharedMouse sharedMouse;

        std::atomic<int> mouseReaders{0};

        const reshadefx::annotation* annotationNamed(const reshadefx::uniform& uniform, const char* name)
        {
            const auto it = std::find_if(uniform.annotations.begin(), uniform.annotations.end(), [&](const auto& a) { return a.name == name; });
            return it == uniform.annotations.end() ? nullptr : &*it;
        }

        std::string annotationText(const reshadefx::uniform& uniform, const char* name)
        {
            const reshadefx::annotation* a = annotationNamed(uniform, name);
            return a != nullptr && a->type.base == reshadefx::type::t_string ? a->value.string_data : std::string();
        }

        KeyMode modeOf(const reshadefx::uniform& uniform)
        {
            const std::string            mode   = annotationText(uniform, "mode");
            const reshadefx::annotation* toggle = annotationNamed(uniform, "toggle");
            const bool toggleSet = toggle != nullptr && toggle->type.base != reshadefx::type::t_string
                                   && (toggle->type.is_floating_point() ? toggle->value.as_float[0] != 0.0f : toggle->value.as_int[0] != 0);
            if (mode == "toggle" || toggleSet)
                return KeyMode::toggle;
            return mode == "press" ? KeyMode::press : KeyMode::held;
        }
    }

    void enumerateReshadeUniforms(reshadefx::effect_module module)
    {
        for (auto& uniform : module.uniforms)
        {
            const std::string source = annotationText(uniform, "source");
            if (source.empty())
            {
                Logger::debug("uniform without source annotation (offset: " + std::to_string(uniform.offset) + ")");
                continue;
            }
            Logger::debug(source);
            Logger::debug("size: " + std::to_string(uniform.size));
            Logger::debug("offset: " + std::to_string(uniform.offset));
        }
    }

    InstanceUniformStorage::Slot& InstanceUniformStorage::slotFor(const reshadefx::uniform& uniform)
    {
        const auto [it, made] = slots.try_emplace(uniform.name);
        if (made && uniform.has_initializer_value && !uniform.type.is_array())
            std::copy_n(uniform.initializer_value.as_uint, std::min<size_t>(it->second.words.size(), uniform.type.components()), it->second.words.begin());
        return it->second;
    }

    std::optional<UniformSource> uniformSourceNamed(const std::string& source)
    {
        static const std::map<std::string, UniformSource> sources = {
            {"frametime", UniformSource::FrameTime},       {"framecount", UniformSource::FrameCount},
            {"random", UniformSource::Random},             {"pingpong", UniformSource::PingPong},
            {"date", UniformSource::Date},                 {"timer", UniformSource::Timer},
            {"key", UniformSource::Key},                   {"mousepoint", UniformSource::MousePoint},
            {"mousedelta", UniformSource::MouseDelta},     {"mousebutton", UniformSource::MouseButton},
            {"mousewheel", UniformSource::MouseWheel},     {"ui_open", UniformSource::OverlayOpen},
            {"overlay_open", UniformSource::OverlayOpen},  {"ui_active", UniformSource::OverlayActive},
            {"overlay_active", UniformSource::OverlayActive}, {"ui_hovered", UniformSource::OverlayHovered},
            {"overlay_hovered", UniformSource::OverlayHovered}, {"screenshot", UniformSource::Screenshot},
        };
        const auto it = sources.find(source);
        return it == sources.end() ? std::nullopt : std::optional(it->second);
    }

    std::vector<std::shared_ptr<ReshadeUniform>> createReshadeUniforms(const reshadefx::effect_module& module, EffectRegistry* effectRegistry,
                                                                       const std::string& effectName, bool liveUniforms,
                                                                       InstanceUniformStorage& storage)
    {
        std::vector<std::string> names;
        for (const reshadefx::uniform& uniform : module.uniforms)
            names.push_back(uniform.name);

        std::vector<std::shared_ptr<ReshadeUniform>> uniforms;
        for (const reshadefx::uniform& uniform : module.uniforms)
        {
            const std::string source = annotationText(uniform, "source");
            if (source.empty())
            {
                if (liveUniforms || !uniform.has_initializer_value)
                    uniforms.push_back(std::make_shared<ParameterUniform>(uniform, effectRegistry, effectName));
                continue;
            }
            if (source == "bufready_depth")
                uniforms.push_back(std::make_shared<DepthUniform>(uniform));
            else if (const std::optional<UniformSource> known = uniformSourceNamed(source))
                uniforms.push_back(
                    std::make_shared<SourceUniform>(uniform, *known, storage.slotFor(uniform), effectRegistry, instanceOfRow(effectName), names));
            else
                Logger::warn("effect " + effectName + ": uniform " + uniform.name + " asks for source \"" + source
                             + "\", which this layer does not provide, so nothing writes it");
        }
        return uniforms;
    }

    ParameterUniform::ParameterUniform(reshadefx::uniform uniformInfo, EffectRegistry* effectRegistry, std::string effectName)
        : ReshadeUniform(std::move(uniformInfo)), effectRegistry(effectRegistry), effectName(std::move(effectName))
    {
    }

    void ParameterUniform::update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame&)
    {
        if (mappedBuffer == nullptr)
            return;

        if (effectRegistry == nullptr)
        {
            writeReshadeUniformInitializer(mappedBuffer, bufferSize, uniformInfo);
            return;
        }

        uint32_t words[16] = {};
        size_t   wordCount = 0;
        if (effectRegistry->readParameterWords(effectName, uniformInfo.name, cachedRegistryRevision, cachedParam, words, std::size(words), wordCount))
            writeReshadeUniformWords(mappedBuffer, bufferSize, uniformInfo, words, wordCount);
        else
            writeReshadeUniformInitializer(mappedBuffer, bufferSize, uniformInfo);
    }

    MouseReader::MouseReader()
    {
        mouseReaders.fetch_add(1, std::memory_order_relaxed);
    }

    MouseReader::~MouseReader()
    {
        mouseReaders.fetch_sub(1, std::memory_order_relaxed);
    }

    bool effectsReadMouse(bool effectsRun)
    {
        return effectsRun && mouseReaders.load(std::memory_order_relaxed) > 0;
    }

    void beginReshadeInputFrame(bool readMouse)
    {
        if (!readMouse)
        {
            sharedMouse.primed = false;
            return;
        }
        MouseState now = getMouseState();
        if (!sharedMouse.primed)
        {
            sharedMouse.previous = now;
            sharedMouse.primed   = true;
        }
        else
        {
            sharedMouse.previous = sharedMouse.current;
        }
        sharedMouse.current = now;
    }

    SourceUniform::SourceUniform(reshadefx::uniform uniformInfo, UniformSource source, InstanceUniformStorage::Slot& slot,
                                 EffectRegistry* effectRegistry, std::string instance, std::vector<std::string> uniformNames)
        : ReshadeUniform(std::move(uniformInfo)), source(source), slot(slot), effectRegistry(effectRegistry), instance(std::move(instance)),
          uniformNames(std::move(uniformNames)), mode(modeOf(this->uniformInfo))
    {
        if (source == UniformSource::Key)
        {
            const int keycode = annotationInt("keycode", 0);
            if (keycode > 7 && keycode < 256)
                keysym = keysymForVirtualKey(keycode);
            if (keysym == 0)
                Logger::warn("ReShade key uniform " + this->uniformInfo.name + " asks for virtual-key " + std::to_string(keycode)
                             + ", which has no key here, so it keeps its value");
        }
        if (source == UniformSource::MouseButton)
        {
            const int keycode = annotationInt("keycode", -1);
            button            = keycode >= 0 && keycode < 5 ? keycode : -1;
        }
        if (source == UniformSource::MousePoint || source == UniformSource::MouseDelta || source == UniformSource::MouseButton
            || source == UniformSource::MouseWheel)
            reader.emplace();
    }

    float SourceUniform::annotationFloat(const char* name, size_t component, float otherwise) const
    {
        const reshadefx::annotation* a = annotationNamed(uniformInfo, name);
        if (a == nullptr || a->type.base == reshadefx::type::t_string)
            return otherwise;
        return a->type.is_floating_point() ? a->value.as_float[component] : static_cast<float>(a->value.as_int[component]);
    }

    int SourceUniform::annotationInt(const char* name, int otherwise) const
    {
        const reshadefx::annotation* a = annotationNamed(uniformInfo, name);
        if (a == nullptr || a->type.base == reshadefx::type::t_string)
            return otherwise;
        return a->type.is_floating_point() ? static_cast<int>(a->value.as_float[0]) : a->value.as_int[0];
    }

    void SourceUniform::storeButton(KeyMode keyMode, bool isDown, bool pressed)
    {
        switch (keyMode)
        {
            case KeyMode::toggle:
                if (pressed)
                    store(0, reshadeWordFromBool(uniformInfo.type, !loadBool()));
                break;
            case KeyMode::press: store(0, reshadeWordFromBool(uniformInfo.type, pressed)); break;
            case KeyMode::held: store(0, reshadeWordFromBool(uniformInfo.type, isDown)); break;
        }
    }

    void SourceUniform::compute(const ReshadeFrame& frame)
    {
        const reshadefx::type& type = uniformInfo.type;
        switch (source)
        {
            case UniformSource::FrameTime: store(0, reshadeWordFromFloat(type, frame.frameTimeMs)); break;
            case UniformSource::FrameCount:
                store(0, type.is_boolean() ? reshadeWordFromBool(type, frame.frameCount % 2 == 0)
                                           : reshadeWordFromUint(type, static_cast<uint32_t>(frame.frameCount % UINT32_MAX)));
                break;
            case UniformSource::Random:
            {
                const long long low  = annotationInt("min", 0);
                const long long high = annotationInt("max", windowsRandMax);
                store(0, reshadeWordFromInt(type, static_cast<int32_t>(low + windowsRand() % (std::llabs(high - low) + 1))));
                break;
            }
            case UniformSource::PingPong:
            {
                const float low       = annotationFloat("min", 0, 0.0f);
                const float high      = annotationFloat("max", 0, 1.0f);
                const float stepMin   = annotationFloat("step", 0, 0.0f);
                const float stepMax   = annotationFloat("step", 1, 0.0f);
                const float smoothing = annotationFloat("smoothing", 0, 0.0f);
                const float seconds   = frame.frameTimeMs / 1000.0f;
                float increment = stepMax == 0.0f ? stepMin : stepMin + std::fmod(static_cast<float>(windowsRand()), stepMax - stepMin + 1.0f);
                float value[2]  = {loadFloat(0), loadFloat(1)};
                if (value[1] >= 0.0f)
                {
                    increment = std::max(increment - std::max(0.0f, smoothing - (high - value[0])), 0.05f) * seconds;
                    if ((value[0] += increment) >= high)
                        value[0] = high, value[1] = -1.0f;
                }
                else
                {
                    increment = std::max(increment - std::max(0.0f, smoothing - (value[0] - low)), 0.05f) * seconds;
                    if ((value[0] -= increment) <= low)
                        value[0] = low, value[1] = 1.0f;
                }
                store(0, reshadeWordFromFloat(type, value[0]));
                store(1, reshadeWordFromFloat(type, value[1]));
                break;
            }
            case UniformSource::Date:
            {
                const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                struct tm         local;
                localtime_r(&now, &local);
                const int32_t value[4] = {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec};
                for (size_t i = 0; i < 4; i++)
                    store(i, reshadeWordFromInt(type, value[i]));
                break;
            }
            case UniformSource::Timer: store(0, reshadeWordFromUint(type, frame.timerMs)); break;
            case UniformSource::Key:
            {
                if (keysym == 0)
                    break;
                const bool isDown  = isKeyPressed(keysym);
                const bool pressed = isDown && !slot.wasDown;
                slot.wasDown       = isDown;
                storeButton(mode, isDown, pressed);
                break;
            }
            case UniformSource::MouseButton:
            {
                if (button < 0)
                    break;
                const bool isDown  = (button == 0 && sharedMouse.current.leftButton) || (button == 1 && sharedMouse.current.rightButton)
                                    || (button == 2 && sharedMouse.current.middleButton);
                const bool pressed = isDown && !slot.wasDown;
                slot.wasDown       = isDown;
                storeButton(mode, isDown, pressed);
                break;
            }
            case UniformSource::MousePoint:
                store(0, reshadeWordFromFloat(type, static_cast<float>(sharedMouse.current.x)));
                store(1, reshadeWordFromFloat(type, static_cast<float>(sharedMouse.current.y)));
                break;
            case UniformSource::MouseDelta:
                store(0, reshadeWordFromFloat(type, static_cast<float>(sharedMouse.current.x - sharedMouse.previous.x)));
                store(1, reshadeWordFromFloat(type, static_cast<float>(sharedMouse.current.y - sharedMouse.previous.y)));
                break;
            case UniformSource::MouseWheel:
            {
                const float low   = annotationFloat("min", 0, 0.0f);
                const float high  = annotationFloat("max", 0, 0.0f);
                const float given = annotationFloat("step", 0, 0.0f);
                const float step  = given == 0.0f ? 1.0f : given;
                const float delta = sharedMouse.current.scrollUpDelta;
                float       value = loadFloat(0) + delta * step;
                if (low != high)
                    value = std::min(std::max(value, low), high);
                store(0, reshadeWordFromFloat(type, value));
                store(1, reshadeWordFromFloat(type, delta));
                break;
            }
            case UniformSource::OverlayOpen: store(0, reshadeWordFromBool(type, frame.overlayOpen)); break;
            case UniformSource::OverlayActive:
            case UniformSource::OverlayHovered:
            {
                uint32_t index = 0;
                if (frame.overlayOpen && effectRegistry != nullptr)
                {
                    const auto [active, hovered] = effectRegistry->editedParameter(instance);
                    const std::string& edited    = source == UniformSource::OverlayActive ? active : hovered;
                    if (const auto at = std::find(uniformNames.begin(), uniformNames.end(), edited); !edited.empty() && at != uniformNames.end())
                        index = static_cast<uint32_t>(at - uniformNames.begin()) + 1;
                }
                store(0, reshadeWordFromUint(type, index));
                break;
            }
            case UniformSource::Screenshot: store(0, reshadeWordFromBool(type, false)); break;
        }
    }

    void SourceUniform::update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame& frame)
    {
        if (slot.frame != frame.id)
        {
            compute(frame);
            slot.frame = frame.id;
        }
        writeWords(mappedBuffer, bufferSize, slot.words.data(), std::min<size_t>(slot.words.size(), uniformInfo.type.components()));
    }

    void DepthUniform::update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame&)
    {
        const uint32_t word = reshadeWordFromBool(uniformInfo.type, depthAvailable);
        writeWords(mappedBuffer, bufferSize, &word, 1);
    }
}
