#pragma once

#include <utility>

#include "vulkan_include.hpp"
#include "include/reshade_api_format.hpp"

namespace vkBasalt
{
    inline constexpr std::pair<VkFormat, reshade::api::format> reshadeFormats[] = {
        {VK_FORMAT_R8_UNORM, reshade::api::format::r8_unorm},
        {VK_FORMAT_R16_UNORM, reshade::api::format::r16_unorm},
        {VK_FORMAT_R16_SFLOAT, reshade::api::format::r16_float},
        {VK_FORMAT_R32_SFLOAT, reshade::api::format::r32_float},
        {VK_FORMAT_R32_UINT, reshade::api::format::r32_uint},
        {VK_FORMAT_R32_SINT, reshade::api::format::r32_sint},
        {VK_FORMAT_R8G8_UNORM, reshade::api::format::r8g8_unorm},
        {VK_FORMAT_R16G16_UNORM, reshade::api::format::r16g16_unorm},
        {VK_FORMAT_R16G16_SFLOAT, reshade::api::format::r16g16_float},
        {VK_FORMAT_R32G32_SFLOAT, reshade::api::format::r32g32_float},
        {VK_FORMAT_R8G8B8A8_UNORM, reshade::api::format::r8g8b8a8_unorm},
        {VK_FORMAT_A8B8G8R8_UNORM_PACK32, reshade::api::format::r8g8b8a8_unorm},
        {VK_FORMAT_B8G8R8A8_UNORM, reshade::api::format::b8g8r8a8_unorm},
        {VK_FORMAT_R16G16B16A16_UNORM, reshade::api::format::r16g16b16a16_unorm},
        {VK_FORMAT_R16G16B16A16_SNORM, reshade::api::format::r16g16b16a16_snorm},
        {VK_FORMAT_R16G16B16A16_SFLOAT, reshade::api::format::r16g16b16a16_float},
        {VK_FORMAT_R32G32B32A32_SFLOAT, reshade::api::format::r32g32b32a32_float},
        {VK_FORMAT_R32G32B32A32_UINT, reshade::api::format::r32g32b32a32_uint},
        {VK_FORMAT_R32G32B32A32_SINT, reshade::api::format::r32g32b32a32_sint},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, reshade::api::format::r10g10b10a2_unorm},
        {VK_FORMAT_A2R10G10B10_UNORM_PACK32, reshade::api::format::b10g10r10a2_unorm},
        {VK_FORMAT_B10G11R11_UFLOAT_PACK32, reshade::api::format::r11g11b10_float},
        {VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, reshade::api::format::r9g9b9e5},
        {VK_FORMAT_R5G6B5_UNORM_PACK16, reshade::api::format::b5g6r5_unorm},
    };

    inline reshade::api::format reshadeFormatOf(VkFormat format)
    {
        for (const auto& [vulkan, reshadeFormat] : reshadeFormats)
            if (vulkan == format)
                return reshadeFormat;
        return reshade::api::format::unknown;
    }

    inline VkFormat vulkanFormatOf(reshade::api::format format)
    {
        for (const auto& [vulkan, reshadeFormat] : reshadeFormats)
            if (reshadeFormat == format)
                return vulkan;
        return VK_FORMAT_UNDEFINED;
    }
}
