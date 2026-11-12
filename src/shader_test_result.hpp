#pragma once

#include <string>
#include <vector>

#include "technique_info.hpp"

namespace vkBasalt
{
    struct ShaderTestResult
    {
        std::string effectName;
        std::string filePath;
        bool        success          = false;
        bool        usesDepth        = false;
        bool        usesMinPrecision = false;
        std::string errorMessage;
        std::vector<TechniqueInfo> techniques;
    };

    inline bool shaderTestAllowed(const ShaderTestResult& result, bool safeAntiCheat)
    {
        return result.success && !(safeAntiCheat && result.usesDepth);
    }
}
