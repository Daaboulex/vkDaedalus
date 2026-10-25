#include "sampler.hpp"

#include <stdexcept>

#include "effects/device_limits.hpp"
#include "format.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    VkSampler createSampler(LogicalDevice* pLogicalDevice)
    {
        VkSampler sampler;

        VkSamplerCreateInfo samplerCreateInfo;
        samplerCreateInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.pNext                   = nullptr;
        samplerCreateInfo.flags                   = 0;
        samplerCreateInfo.magFilter               = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter               = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.mipLodBias              = 0.0f;
        samplerCreateInfo.anisotropyEnable        = VK_FALSE;
        samplerCreateInfo.maxAnisotropy           = 16;
        samplerCreateInfo.compareEnable           = VK_FALSE;
        samplerCreateInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
        samplerCreateInfo.minLod                  = 0.0f;
        samplerCreateInfo.maxLod                  = 0.0f;
        samplerCreateInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

        VkResult result = pLogicalDevice->vkd.CreateSampler(pLogicalDevice->device, &samplerCreateInfo, nullptr, &sampler);
        ASSERT_VULKAN(result);
        return sampler;
    }

    VkSampler createReshadeSampler(LogicalDevice*                pLogicalDevice,
                                   const reshadefx::sampler&     samplerInfo,
                                   VkFormat                      format,
                                   std::vector<std::string>&     notices)
    {
        VkSampler sampler;

        if (samplerInfo.min_lod > samplerInfo.max_lod)
            throw std::runtime_error("sampler " + samplerInfo.name + " asks for MinLOD " + std::to_string(samplerInfo.min_lod)
                                     + " above MaxLOD " + std::to_string(samplerInfo.max_lod));
        const float lodBias = heldLodBias(pLogicalDevice->limits, samplerInfo.lod_bias);
        if (lodBias != samplerInfo.lod_bias)
        {
            notices.push_back("sampler " + samplerInfo.name + ": MipLODBias " + std::to_string(samplerInfo.lod_bias)
                              + " is held at " + std::to_string(lodBias) + ", the most this GPU takes");
            Logger::info(notices.back());
        }

        VkFilter            minFilter  = VK_FILTER_LINEAR;
        VkFilter            magFilter  = VK_FILTER_LINEAR;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        convertReshadeFilter(samplerInfo.filter, minFilter, magFilter, mipmapMode);
        const bool linear = minFilter == VK_FILTER_LINEAR || magFilter == VK_FILTER_LINEAR || mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (linear && format != VK_FORMAT_UNDEFINED && filterFor(pLogicalDevice, format) == VK_FILTER_NEAREST)
        {
            minFilter  = VK_FILTER_NEAREST;
            magFilter  = VK_FILTER_NEAREST;
            mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            if (isIntegerFormat(format))
                Logger::info("sampler " + samplerInfo.name + " reads integers, which have no linear filter, so it samples the nearest texel");
            else
            {
                notices.push_back("sampler " + samplerInfo.name + ": format " + std::to_string(format)
                                  + " cannot be filtered on this GPU, so it is sampled without filtering");
                Logger::info(notices.back());
            }
        }

        VkSamplerCreateInfo samplerCreateInfo;
        samplerCreateInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.pNext                   = nullptr;
        samplerCreateInfo.flags                   = 0;
        samplerCreateInfo.magFilter               = magFilter;
        samplerCreateInfo.minFilter               = minFilter;
        samplerCreateInfo.mipmapMode              = mipmapMode;
        samplerCreateInfo.addressModeU            = convertReshadeAddressMode(samplerInfo.address_u);
        samplerCreateInfo.addressModeV            = convertReshadeAddressMode(samplerInfo.address_v);
        samplerCreateInfo.addressModeW            = convertReshadeAddressMode(samplerInfo.address_w);
        samplerCreateInfo.mipLodBias              = lodBias;
        samplerCreateInfo.anisotropyEnable        = VK_FALSE;
        samplerCreateInfo.maxAnisotropy           = 16;
        samplerCreateInfo.compareEnable           = VK_FALSE;
        samplerCreateInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
        samplerCreateInfo.minLod                  = samplerInfo.min_lod;
        samplerCreateInfo.maxLod                  = samplerInfo.max_lod;
        samplerCreateInfo.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

        VkResult result = pLogicalDevice->vkd.CreateSampler(pLogicalDevice->device, &samplerCreateInfo, nullptr, &sampler);
        ASSERT_VULKAN(result);
        return sampler;
    }

    VkSamplerAddressMode convertReshadeAddressMode(const reshadefx::texture_address_mode& addressMode)
    {
        switch (addressMode)
        {
            case reshadefx::texture_address_mode::wrap: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case reshadefx::texture_address_mode::mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case reshadefx::texture_address_mode::clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case reshadefx::texture_address_mode::border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        }
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    void
    convertReshadeFilter(const reshadefx::filter_mode& textureFilter, VkFilter& minFilter, VkFilter& magFilter, VkSamplerMipmapMode& mipmapMode)
    {
        switch (textureFilter)
        {
            case reshadefx::filter_mode::min_mag_mip_point:
                minFilter  = VK_FILTER_NEAREST;
                magFilter  = VK_FILTER_NEAREST;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                return;
            case reshadefx::filter_mode::min_mag_point_mip_linear:
                minFilter  = VK_FILTER_NEAREST;
                magFilter  = VK_FILTER_NEAREST;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                return;
            case reshadefx::filter_mode::min_point_mag_linear_mip_point:
                minFilter  = VK_FILTER_NEAREST;
                magFilter  = VK_FILTER_LINEAR;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                return;
            case reshadefx::filter_mode::min_point_mag_mip_linear:
                minFilter  = VK_FILTER_NEAREST;
                magFilter  = VK_FILTER_LINEAR;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                return;
            case reshadefx::filter_mode::min_linear_mag_mip_point:
                minFilter  = VK_FILTER_LINEAR;
                magFilter  = VK_FILTER_NEAREST;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                return;
            case reshadefx::filter_mode::min_linear_mag_point_mip_linear:
                minFilter  = VK_FILTER_LINEAR;
                magFilter  = VK_FILTER_NEAREST;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                return;
            case reshadefx::filter_mode::min_mag_linear_mip_point:
                minFilter  = VK_FILTER_LINEAR;
                magFilter  = VK_FILTER_LINEAR;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                return;
            case reshadefx::filter_mode::min_mag_mip_linear:
            case reshadefx::filter_mode::anisotropic:
                minFilter  = VK_FILTER_LINEAR;
                magFilter  = VK_FILTER_LINEAR;
                mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                return;
        }

        minFilter  = VK_FILTER_LINEAR;
        magFilter  = VK_FILTER_LINEAR;
        mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}
