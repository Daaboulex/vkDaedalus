#ifndef RESHADE_UNIFORMS_HPP_INCLUDED
#define RESHADE_UNIFORMS_HPP_INCLUDED
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <chrono>
#include <memory>

#include "vulkan_include.hpp"
#include "reshade_input_map.hpp"

#include "reshade/effect_module.hpp"
#include "logger.hpp"
#include "reshade_uniform_storage.hpp"
#include "reshade_frame.hpp"

#include <array>
#include <map>
#include <optional>

namespace vkBasalt
{
    class EffectRegistry;
    class EffectParam;

    void enumerateReshadeUniforms(reshadefx::effect_module module);

    class InstanceUniformStorage
    {
    public:
        struct Slot
        {
            std::array<uint32_t, 16> words{};
            uint64_t                 frame   = 0;
            bool                     wasDown = false;
        };

        Slot& slotFor(const reshadefx::uniform& uniform);

    private:
        std::map<std::string, Slot> slots;
    };

    class InstanceUniformStorages
    {
    public:
        InstanceUniformStorage& of(const std::string& instance) { return storages[instance]; }

    private:
        std::map<std::string, InstanceUniformStorage> storages;
    };

    class ReshadeUniform
    {
    public:
        explicit ReshadeUniform(reshadefx::uniform uniformInfo) : uniformInfo(std::move(uniformInfo)) {}
        virtual void update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame& frame) = 0;
        virtual ~ReshadeUniform() = default;

    protected:
        bool writeWords(void* mappedBuffer, size_t bufferSize, const uint32_t* words, size_t count)
        {
            if (writeReshadeUniformWords(mappedBuffer, bufferSize, uniformInfo, words, count))
                return true;
            if (!reportedOutOfRange)
                Logger::warn("uniform " + uniformInfo.name + " does not fit the uniform buffer (offset " + std::to_string(uniformInfo.offset)
                             + ", size " + std::to_string(uniformInfo.size) + ", buffer " + std::to_string(bufferSize) + "); it is not written");
            reportedOutOfRange = true;
            return false;
        }

        reshadefx::uniform uniformInfo;

    private:
        bool reportedOutOfRange = false;
    };

    std::vector<std::shared_ptr<ReshadeUniform>> createReshadeUniforms(const reshadefx::effect_module& module, EffectRegistry* effectRegistry,
                                                                       const std::string& effectName, bool liveUniforms,
                                                                       InstanceUniformStorage& storage);

    class ParameterUniform : public ReshadeUniform
    {
    public:
        ParameterUniform(reshadefx::uniform uniformInfo, EffectRegistry* effectRegistry, std::string effectName);
        void update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame& frame) override;

    private:
        EffectRegistry*    effectRegistry = nullptr;
        std::string        effectName;
        uint64_t           cachedRegistryRevision = 0;
        const EffectParam* cachedParam            = nullptr;
    };

    bool effectsReadMouse(bool effectsRun);
    void beginReshadeInputFrame(bool readMouse);

    class MouseReader
    {
    public:
        MouseReader();
        ~MouseReader();
        MouseReader(const MouseReader&)            = delete;
        MouseReader& operator=(const MouseReader&) = delete;
    };

    enum class UniformSource
    {
        FrameTime,
        FrameCount,
        Random,
        PingPong,
        Date,
        Timer,
        Key,
        MousePoint,
        MouseDelta,
        MouseButton,
        MouseWheel,
        OverlayOpen,
        OverlayActive,
        OverlayHovered,
        Screenshot,
    };

    std::optional<UniformSource> uniformSourceNamed(const std::string& source);

    class SourceUniform : public ReshadeUniform
    {
    public:
        SourceUniform(reshadefx::uniform uniformInfo, UniformSource source, InstanceUniformStorage::Slot& slot, EffectRegistry* effectRegistry,
                      std::string instance, std::vector<std::string> uniformNames);
        void update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame& frame) override;

    private:
        void compute(const ReshadeFrame& frame);
        void store(size_t component, uint32_t word) { slot.words[component] = word; }
        float loadFloat(size_t component) const { return reshadeFloatOfWord(uniformInfo.type, slot.words[component]); }
        bool  loadBool() const { return reshadeBoolOfWord(uniformInfo.type, slot.words[0]); }
        void  storeButton(KeyMode mode, bool isDown, bool pressed);
        float annotationFloat(const char* name, size_t component, float otherwise) const;
        int   annotationInt(const char* name, int otherwise) const;

        UniformSource                 source;
        InstanceUniformStorage::Slot& slot;
        EffectRegistry*               effectRegistry = nullptr;
        std::string                   instance;
        std::vector<std::string>      uniformNames;
        KeyMode                       mode   = KeyMode::held;
        uint32_t                      keysym = 0;
        int                           button = -1;
        std::optional<MouseReader>    reader;
    };

    class DepthUniform : public ReshadeUniform
    {
    public:
        explicit DepthUniform(reshadefx::uniform uniformInfo) : ReshadeUniform(std::move(uniformInfo)) {}
        void update(void* mappedBuffer, size_t bufferSize, const ReshadeFrame& frame) override;
        void setDepthAvailable(bool available) { depthAvailable = available; }

    private:
        bool depthAvailable = false;
    };
}

#endif
