#ifndef FORMAT_HPP_INCLUDED
#define FORMAT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"
#include "format_features.hpp"

namespace vkBasalt
{
    VkFormat convertToSRGB(VkFormat format);
    VkFormat convertToUNORM(VkFormat format);
    bool isSRGB(VkFormat format);

    VkFormat getSupportedFormat(LogicalDevice*        pLogicalDevice,
                                std::vector<VkFormat> formats,
                                VkFormatFeatureFlags  features,
                                VkImageTiling         tiling = VK_IMAGE_TILING_OPTIMAL);

    VkFormat getStencilFormat(LogicalDevice* pLogicalDevice);

    bool isDepthFormat(VkFormat format);

    bool isStencilFormat(VkFormat format);

    VkFormatFeatureFlags optimalTilingFeatures(LogicalDevice* pLogicalDevice, VkFormat format);

    VkFilter filterFor(LogicalDevice* pLogicalDevice, VkFormat format);
}

#endif
