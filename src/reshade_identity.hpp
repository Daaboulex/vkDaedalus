#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    struct ReshadeIdentity
    {
        uint32_t vendorId    = 0;
        uint32_t deviceId    = 0;
        uint32_t rendererId  = 0x21100;
        uint32_t application = 0;
    };

    inline uint32_t reshadeRendererId(uint32_t enabledVersion, uint32_t deviceVersion)
    {
        uint32_t version = deviceVersion;
        if (enabledVersion < VK_API_VERSION_1_2)
            version = VK_API_VERSION_1_1;
        else if (enabledVersion < VK_API_VERSION_1_3)
            version = VK_API_VERSION_1_2;
        else if (enabledVersion < VK_MAKE_API_VERSION(0, 1, 4, 0))
            version = VK_API_VERSION_1_3;
        return 0x20000 | VK_API_VERSION_MAJOR(version) << 12 | VK_API_VERSION_MINOR(version) << 8;
    }

    inline uint32_t reshadeApplicationHash(std::string_view executableStem)
    {
        constexpr size_t fnvOffsetBasis = sizeof(size_t) == 8 ? static_cast<size_t>(14695981039346656037ULL) : static_cast<size_t>(2166136261U);
        constexpr size_t fnvPrime       = sizeof(size_t) == 8 ? static_cast<size_t>(1099511628211ULL) : static_cast<size_t>(16777619U);
        size_t           hash           = fnvOffsetBasis;
        for (const char c : executableStem)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= fnvPrime;
        }
        return static_cast<uint32_t>(hash & 0xFFFFFFFF);
    }

    uint32_t reshadeApplicationOfThisProgram();
}
