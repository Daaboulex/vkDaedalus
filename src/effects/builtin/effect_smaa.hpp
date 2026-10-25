#ifndef EFFECT_SMAA_HPP_INCLUDED
#define EFFECT_SMAA_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"

#include "../effect.hpp"
#include "config.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    class SmaaEffect : public Effect
    {
    public:
        SmaaEffect(LogicalDevice*       pLogicalDevice,
                   VkFormat             format,
                   VkExtent2D           imageExtent,
                   std::vector<VkImage> inputImages,
                   std::vector<VkImage> outputImages,
                   Config*              pConfig);
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        ~SmaaEffect();

    private:
        explicit SmaaEffect(LogicalDevice* pLogicalDevice);

        LogicalDevice*               pLogicalDevice = nullptr;
        std::vector<VkImage>         inputImages;
        std::vector<VkImage>         edgeImages;
        std::vector<VkImage>         blendImages;
        std::vector<VkImage>         outputImages;
        std::vector<VkImageView>     inputImageViews;
        std::vector<VkImageView>     edgeImageViews;
        std::vector<VkImageView>     blendImageViews;
        std::vector<VkImageView>     outputImageViews;
        std::vector<VkDescriptorSet> imageDescriptorSets;
        std::vector<VkFramebuffer>   edgeFramebuffers;
        std::vector<VkFramebuffer>   blendFramebuffers;
        std::vector<VkFramebuffer>   neignborFramebuffers;
        VkImage                      areaImage = VK_NULL_HANDLE;
        VkImage                      searchImage = VK_NULL_HANDLE;
        VkImageView                  areaImageView = VK_NULL_HANDLE;
        VkImageView                  searchImageView = VK_NULL_HANDLE;
        VkDescriptorSetLayout        imageSamplerDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool             descriptorPool = VK_NULL_HANDLE;
        VkShaderModule               edgeVertexModule = VK_NULL_HANDLE;
        VkShaderModule               edgeFragmentModule = VK_NULL_HANDLE;
        VkShaderModule               blendVertexModule = VK_NULL_HANDLE;
        VkShaderModule               blendFragmentModule = VK_NULL_HANDLE;
        VkShaderModule               neighborVertexModule = VK_NULL_HANDLE;
        VkShaderModule               neignborFragmentModule = VK_NULL_HANDLE;
        VkRenderPass                 renderPass = VK_NULL_HANDLE;
        VkRenderPass                 unormRenderPass = VK_NULL_HANDLE;
        VkPipelineLayout             pipelineLayout = VK_NULL_HANDLE;
        VkPipeline                   edgePipeline = VK_NULL_HANDLE;
        VkPipeline                   blendPipeline = VK_NULL_HANDLE;
        VkPipeline                   neighborPipeline = VK_NULL_HANDLE;
        VkExtent2D                   imageExtent = {};
        VkFormat                     format = VK_FORMAT_UNDEFINED;
        VkDeviceMemory               imageMemory = VK_NULL_HANDLE;
        VkDeviceMemory               areaMemory = VK_NULL_HANDLE;
        VkDeviceMemory               searchMemory = VK_NULL_HANDLE;
        VkSampler                    sampler = VK_NULL_HANDLE;

        Config* pConfig = nullptr;
    };
}

#endif
