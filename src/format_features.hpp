#pragma once

#include "vulkan_include.hpp"

namespace vkBasalt
{
    inline bool isIntegerFormat(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_R32_SINT:
            case VK_FORMAT_R32_UINT:
            case VK_FORMAT_R32G32B32A32_SINT:
            case VK_FORMAT_R32G32B32A32_UINT: return true;
            default: return false;
        }
    }

    inline VkFilter filterForFeatures(VkFormat format, VkFormatFeatureFlags features)
    {
        return !isIntegerFormat(format) && (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    }

    inline uint32_t formatTexelBytes(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return 4;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
            case VK_FORMAT_R16G16B16A16_UNORM: return 8;
            default: return 0;
        }
    }

    inline bool blitsMipLevels(VkFormatFeatureFlags features)
    {
        constexpr VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        return (features & blit) == blit;
    }
}
