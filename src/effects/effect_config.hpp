#ifndef EFFECT_CONFIG_HPP_INCLUDED
#define EFFECT_CONFIG_HPP_INCLUDED

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <map>

#include "params/effect_param.hpp"

namespace vkBasalt
{
    enum class EffectType
    {
        BuiltIn,
        ReShade
    };

    struct PreprocessorDefinition
    {
        std::string name;
        std::string value;
        std::string defaultValue;
        std::string effectName;
    };

    struct EffectOnDevice
    {
        std::string              leftOut;
        bool                     readsDepth = false;
        std::vector<std::string> notices;
        uint32_t                 gpu = 1;
        std::string              gpuRefusal;
    };

    struct EffectConfig
    {
        std::string instanceName;
        std::string baseType;
        std::string filePath;
        EffectType type = EffectType::BuiltIn;
        bool enabled = true;
        bool usesMinPrecision = false;
        bool allowHalfPrecision = false;
        uint32_t gpu = 0;
        std::vector<std::unique_ptr<EffectParam>> parameters;
        std::vector<PreprocessorDefinition> preprocessorDefs;
        std::vector<std::pair<std::string, std::string>> parameterDefines;
        std::string compileError;
        std::map<const void*, EffectOnDevice> onDevice;
        std::filesystem::file_time_type fileModTime{};
        bool hasFailed() const { return !compileError.empty(); }
    };

}

#endif
