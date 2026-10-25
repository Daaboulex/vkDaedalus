#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "vulkan_include.hpp"
#include "effects/effect_config.hpp"
#include "include/reshade_api_format.hpp"

namespace vkBasalt
{
    inline uint32_t reshadeColorBitDepth(VkFormat unormFormat)
    {
        switch (unormFormat)
        {
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return 10;
            case VK_FORMAT_R16G16B16A16_UNORM:
            case VK_FORMAT_R16G16B16A16_SFLOAT: return 16;
            default: return 8;
        }
    }

    inline reshade::api::color_space reshadeColorSpace(VkColorSpaceKHR colorSpace)
    {
        switch (colorSpace)
        {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return reshade::api::color_space::srgb;
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return reshade::api::color_space::scrgb;
            case VK_COLOR_SPACE_HDR10_ST2084_EXT: return reshade::api::color_space::hdr10_pq;
            case VK_COLOR_SPACE_HDR10_HLG_EXT: return reshade::api::color_space::hdr10_hlg;
            default: return reshade::api::color_space::unknown;
        }
    }

    enum class DefinitionScope
    {
        Runtime,
        Global,
        Effect,
    };

    inline DefinitionScope reshadeDefinitionScope(const std::string& name)
    {
        if (name.starts_with('_') || name.starts_with("BUFFER_"))
            return DefinitionScope::Runtime;
        if (name.starts_with("RESHADE_"))
            return DefinitionScope::Global;
        return DefinitionScope::Effect;
    }

    inline std::string globalDefinitionProblem(const std::string& name, const std::string& value)
    {
        const auto identifierChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0])) || !std::all_of(name.begin(), name.end(), identifierChar))
            return "a name is letters, digits and underscores, starting with a letter";
        if (reshadeDefinitionScope(name) == DefinitionScope::Runtime)
            return name + " is set by the layer from the swapchain";
        if (value.find_first_not_of(" \t") == std::string::npos)
            return "a definition needs a value";
        return "";
    }

    inline std::vector<std::pair<std::string, std::string>> reshadeCompileDefines(
        VkExtent2D extent, VkFormat unormFormat, VkColorSpaceKHR colorSpace, const std::vector<PreprocessorDefinition>& customDefs)
    {
        std::vector<std::pair<std::string, std::string>> defines = {
            {"BUFFER_WIDTH", std::to_string(extent.width)},
            {"BUFFER_HEIGHT", std::to_string(extent.height)},
            {"BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)"},
            {"BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)"},
            {"BUFFER_COLOR_DEPTH", std::to_string(reshadeColorBitDepth(unormFormat))},
            {"BUFFER_COLOR_BIT_DEPTH", "BUFFER_COLOR_DEPTH"},
            {"BUFFER_COLOR_SPACE", std::to_string(static_cast<uint32_t>(reshadeColorSpace(colorSpace)))},
        };
        for (const auto& def : customDefs)
            defines.push_back({def.name, def.value});
        return defines;
    }
}
