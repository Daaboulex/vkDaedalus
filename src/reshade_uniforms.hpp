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

namespace vkBasalt
{
    class EffectRegistry;
    class EffectParam;

    void enumerateReshadeUniforms(reshadefx::effect_module module);

    class ReshadeUniform
    {
    public:
        explicit ReshadeUniform(reshadefx::uniform uniformInfo) : uniformInfo(std::move(uniformInfo)) {}
        void virtual update(void* mapedBuffer, size_t bufferSize) = 0;
        virtual ~ReshadeUniform(){};

    protected:
        template<typename T>
        bool writeValues(void* mappedBuffer, size_t bufferSize, const T* values, size_t count)
        {
            static_assert(sizeof(T) == sizeof(uint32_t), "ReShade uniforms are made of 32-bit words");
            uint32_t words[16];
            if (count > std::size(words))
                return false;
            std::memcpy(words, values, count * sizeof(uint32_t));
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

    std::vector<std::shared_ptr<ReshadeUniform>> createReshadeUniforms(
        reshadefx::effect_module module, EffectRegistry* effectRegistry,
        const std::string& effectName, bool liveUniforms);

    class ParameterUniform : public ReshadeUniform
    {
    public:
        ParameterUniform(reshadefx::uniform uniformInfo,
                         EffectRegistry* effectRegistry,
                         std::string effectName);
        void update(void* mappedBuffer, size_t bufferSize) override;

    private:
        EffectRegistry* effectRegistry = nullptr;
        std::string effectName;
        uint64_t cachedRegistryRevision = 0;
        const EffectParam* cachedParam = nullptr;
    };

    class FrameTimeUniform : public ReshadeUniform
    {
    public:
        FrameTimeUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~FrameTimeUniform();

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> lastFrame;
    };

    class FrameCountUniform : public ReshadeUniform
    {
    public:
        FrameCountUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~FrameCountUniform();

    private:
        int32_t count = 0;
    };

    class DateUniform : public ReshadeUniform
    {
    public:
        DateUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~DateUniform();
    };

    class TimerUniform : public ReshadeUniform
    {
    public:
        TimerUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~TimerUniform();

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> start;
    };

    class PingPongUniform : public ReshadeUniform
    {
    public:
        PingPongUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~PingPongUniform();

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> lastFrame;

        float min             = 0.0f;
        float max             = 0.0f;
        float stepMin         = 0.0f;
        float stepMax         = 0.0f;
        float smoothing       = 0.0f;
        float currentValue[2] = {0.0f, 1.0f};
    };

    class RandomUniform : public ReshadeUniform
    {
    public:
        RandomUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~RandomUniform();

    private:
        int max = 0;
        int min = 0;
    };

    bool effectsReadMouse(bool effectsRun);
    void beginReshadeInputFrame(bool readMouse);

    class KeyUniform : public ReshadeUniform
    {
    public:
        KeyUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~KeyUniform();

    private:
        uint32_t keysym = 0;
        KeyMode  mode   = KeyMode::held;
        bool     wasDown = false;
        bool     toggled = false;
    };

    class MouseReader
    {
    public:
        MouseReader();
        ~MouseReader();
        MouseReader(const MouseReader&)            = delete;
        MouseReader& operator=(const MouseReader&) = delete;
    };

    class MouseButtonUniform : public ReshadeUniform
    {
    public:
        MouseButtonUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~MouseButtonUniform();

    private:
        int         button  = 0;
        KeyMode     mode    = KeyMode::held;
        bool        wasDown = false;
        bool        toggled = false;
        MouseReader reader;
    };

    class MousePointUniform : public ReshadeUniform
    {
    public:
        MousePointUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~MousePointUniform();

    private:
        MouseReader reader;
    };

    class MouseDeltaUniform : public ReshadeUniform
    {
    public:
        MouseDeltaUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        virtual ~MouseDeltaUniform();

    private:
        MouseReader reader;
    };

    class DepthUniform : public ReshadeUniform
    {
    public:
        DepthUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer, size_t bufferSize) override;
        void setDepthAvailable(bool available) { depthAvailable = available; }
        virtual ~DepthUniform();

    private:
        bool depthAvailable = false;
    };
}

#endif
