#ifndef SAMPLER_HPP_INCLUDED
#define SAMPLER_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

#include "reshade/effect_module.hpp"
namespace vkBasalt
{
    VkSampler createSampler(LogicalDevice* pLogicalDevice);

    VkSampler createReshadeSampler(LogicalDevice*                pLogicalDevice,
                                   const reshadefx::sampler&     samplerInfo,
                                   VkFormat                      format,
                                   std::vector<std::string>&     notices);

    VkSamplerAddressMode convertReshadeAddressMode(const reshadefx::texture_address_mode& addressMode);

    void
    convertReshadeFilter(const reshadefx::filter_mode& textureFilter, VkFilter& minFilter, VkFilter& magFilter, VkSamplerMipmapMode& mipmapMode);
}

#endif
