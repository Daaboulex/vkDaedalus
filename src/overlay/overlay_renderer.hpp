#ifndef OVERLAY_RENDERER_HPP_INCLUDED
#define OVERLAY_RENDERER_HPP_INCLUDED

#include <memory>
#include <vector>

#include "vulkan_include.hpp"
#include "overlay/swapchain_overlay.hpp"

struct ImDrawData;
struct ImTextureData;

namespace vkBasalt
{
    class OverlayRenderer
    {
    public:
        explicit OverlayRenderer(LogicalDevice& device);
        ~OverlayRenderer();
        OverlayRenderer(const OverlayRenderer&)            = delete;
        OverlayRenderer& operator=(const OverlayRenderer&) = delete;

        void prepare(SwapchainOverlay& overlay, VkFormat format, VkExtent2D extent, const std::vector<VkImageView>& views);
        void updateTextures(ImDrawData& drawData);
        void record(OverlayDraw& draw, const SwapchainOverlay& overlay, const ImDrawData& drawData);
        void releaseTextures(ImTextureData* const* textures, int count);

        static bool texturesPending(const ImDrawData& drawData);

    private:
        struct StoreOnly
        {
        };
        OverlayRenderer(LogicalDevice& device, StoreOnly);

        struct FormatPipeline
        {
            VkFormat     format;
            VkRenderPass renderPass;
            VkPipeline   pipeline;
        };

        struct Texture
        {
            VkImage         image  = VK_NULL_HANDLE;
            VkDeviceMemory  memory = VK_NULL_HANDLE;
            VkImageView     view   = VK_NULL_HANDLE;
            VkDescriptorSet set    = VK_NULL_HANDLE;
        };

        const FormatPipeline& pipelineFor(VkFormat format);
        void                  createTexture(ImTextureData& texture);
        void                  destroyTexture(ImTextureData& texture);
        void                  releaseTexture(Texture& texture);
        VkDescriptorSet       takeSet(VkImageView view);
        const Texture*        findTexture(unsigned long long id) const;
        void                  reserve(OverlayBuffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage);

        LogicalDevice&                        device;
        VkSampler                             sampler        = VK_NULL_HANDLE;
        VkDescriptorSetLayout                 setLayout      = VK_NULL_HANDLE;
        VkPipelineLayout                      pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorPool                      descriptorPool = VK_NULL_HANDLE;
        VkShaderModule                        vertexShader   = VK_NULL_HANDLE;
        VkShaderModule                        fragmentShader = VK_NULL_HANDLE;
        std::vector<FormatPipeline>           pipelines;
        std::vector<std::unique_ptr<Texture>> textures;
        std::vector<VkDescriptorSet>          spareSets;
    };
}

#endif
