#include "memory.hpp"
#include "effect_lut.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "image.hpp"
#include "lut_cube.hpp"

#include "stb_image.h"

#include "shader_sources.hpp"

namespace vkBasalt
{
    LutEffect::LutEffect(LogicalDevice* pLogicalDevice)
    {
        this->pLogicalDevice = pLogicalDevice;
    }

    LutEffect::LutEffect(LogicalDevice*       pLogicalDevice,
                         VkFormat             format,
                         VkExtent2D           imageExtent,
                         std::vector<VkImage> inputImages,
                         std::vector<VkImage> outputImages,
                         Config*              pConfig)
        : LutEffect(pLogicalDevice)
    {
        vertexCode   = full_screen_triangle_vert;
        fragmentCode = lut_frag;

        std::string lutFile = pConfig->getOption<std::string>("lutFile");

        if (lutFile.empty())
        {
            throw std::runtime_error("LUT effect requires 'lutFile' to be set in config");
        }

        int                        height = 0;
        std::vector<unsigned char> cubePixels;
        stbi_uc*                   pixels = nullptr;
        std::unique_ptr<stbi_uc, void (*)(void*)> pngPixels(nullptr, stbi_image_free);
        int32_t                    usingPNG = static_cast<int32_t>(!isCubeLutPath(lutFile));
        if (!usingPNG)
        {
            LutCube lutCube(lutFile);
            height     = static_cast<int>(lutCube.size);
            cubePixels = std::move(lutCube.colorCube);
            pixels     = cubePixels.data();
        }
        else
        {
            int channels, width;
            pngPixels.reset(stbi_load(lutFile.c_str(), &width, &height, &channels, STBI_rgb_alpha));
            pixels = pngPixels.get();
            if (pixels == nullptr)
            {
                throw std::runtime_error("Failed to load LUT image file: " + lutFile);
            }
            if (width != height * height)
            {
                throw std::runtime_error("Invalid LUT image dimensions (width must equal height*height): " + lutFile);
            }
        }

        std::vector<VkSpecializationMapEntry> specMapEntrys(2);
        for (uint32_t i = 0; i < specMapEntrys.size(); i++)
        {
            specMapEntrys[i].constantID = i;
            specMapEntrys[i].offset     = sizeof(int32_t) * i;
            specMapEntrys[i].size       = sizeof(int32_t);
        }
        std::vector<int32_t> specData = {height, usingPNG};

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = specMapEntrys.size();
        fragmentSpecializationInfo.pMapEntries   = specMapEntrys.data();
        fragmentSpecializationInfo.dataSize      = specMapEntrys.size() * sizeof(int32_t);
        fragmentSpecializationInfo.pData         = specData.data();

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        VkExtent3D lutImageExtent = {(uint32_t) height, (uint32_t) height, (uint32_t) height};

        lutImage = createImages(pLogicalDevice,
                                1,
                                lutImageExtent,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                lutMemory,
                                1,
                                VK_IMAGE_TYPE_3D)[0];

        uploadToImage(pLogicalDevice, lutImage, lutImageExtent, height * height * height * 4, pixels);
        pngPixels.reset();

        lutImageView = createImageViews(pLogicalDevice, VK_FORMAT_R8G8B8A8_UNORM, std::vector<VkImage>(1, lutImage), VK_IMAGE_VIEW_TYPE_3D)[0];

        lutDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 1);
        extraDescriptorSetLayouts.push_back(lutDescriptorSetLayout);

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = 1;

        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};

        lutDescriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);

        lutDescriptorSet =
            allocateAndWriteImageSamplerDescriptorSets(pLogicalDevice,
                                                       lutDescriptorPool,
                                                       lutDescriptorSetLayout,
                                                       {sampler},
                                                       std::vector<std::vector<VkImageView>>(1, std::vector<VkImageView>(1, lutImageView)))[0];
    }
    LutEffect::~LutEffect()
    {
        if (!pLogicalDevice)
            return;

        pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, lutImageView, nullptr);
        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, lutImage, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, lutDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, lutDescriptorPool, nullptr);
        freeTrackedMemory(pLogicalDevice, lutMemory, nullptr);
    }
    void LutEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &(lutDescriptorSet), 0, nullptr);
        SimpleEffect::applyEffect(imageIndex, commandBuffer);
    }
}
