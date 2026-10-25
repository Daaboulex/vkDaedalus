#pragma once

#include <string>

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
    };

    inline bool shaderTestAllowed(const ShaderTestResult& result, bool safeAntiCheat)
    {
        return result.success && !(safeAntiCheat && result.usesDepth);
    }
}
