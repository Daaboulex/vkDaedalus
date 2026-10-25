#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    struct IncludeResolution
    {
        std::string source;
        std::string name;
        std::string path;
        bool        found = false;

        bool operator==(const IncludeResolution&) const = default;
    };

    struct CompiledReshadeEffect
    {
        reshadefx::effect_module module;
        std::map<std::string, std::vector<uint32_t>> entryPointSpirv;
        std::vector<std::pair<std::string, uint64_t>> includedFiles;
        std::vector<IncludeResolution> includeResolutions;
        std::vector<std::pair<std::string, std::string>> usedMacros;
        bool usesDepth = false;
        bool usesMinPrecision = false;
        std::string warnings;
        std::string error;

        bool ok() const { return error.empty(); }
    };

    std::shared_ptr<const CompiledReshadeEffect> getOrCompileReshadeEffect(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& includePaths,
        bool relaxMinPrecision = false,
        bool liveUniforms = false);

    std::shared_ptr<const CompiledReshadeEffect> compileReshadeEffectUncached(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& includePaths,
        bool relaxMinPrecision,
        bool liveUniforms);

    void clearShaderCache();

    std::string compiledEffectsFolder();

    std::string cacheRoundTripMismatch(const CompiledReshadeEffect& e);

    std::string cacheEntryTempPath(const std::string& entryPath);


}
