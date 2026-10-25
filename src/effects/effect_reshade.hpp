#ifndef EFFECT_RESHADE_HPP_INCLUDED
#define EFFECT_RESHADE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <set>
#include <string>
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <stdexcept>

#include "vulkan_include.hpp"

#include "effect.hpp"
#include "config.hpp"
#include "effect_config.hpp"
#include "effect_registry.hpp"
#include "reshade_uniforms.hpp"

#include "logical_device.hpp"

#include "reshade/effect_module.hpp"
#include "reshade_compile_defines.hpp"

namespace vkBasalt
{
    struct SharedReshadeTexture
    {
        VkImage           image = VK_NULL_HANDLE;
        VkDeviceMemory    memory = VK_NULL_HANDLE;
        VkFormat          format = VK_FORMAT_UNDEFINED;
        VkExtent3D        extent = {0, 0, 0};
        VkImageType       type = VK_IMAGE_TYPE_2D;
        uint32_t          mipLevels = 1;
        VkImageUsageFlags usage = 0;
    };

    // ReShade shares a same-named texture across every effect of one runtime
    class SharedReshadeTexturePool
    {
    public:
        explicit SharedReshadeTexturePool(LogicalDevice* pLogicalDevice);
        ~SharedReshadeTexturePool();

        SharedReshadeTexture* tryAcquire(const std::string& uniqueName,
                                      VkExtent3D        extent,
                                      VkImageType       type,
                                      VkFormat          format,
                                      VkImageUsageFlags usage,
                                      uint32_t          mipLevels,
                                      bool&             created);

        std::set<std::string> names() const
        {
            std::set<std::string> held;
            for (const auto& [name, texture] : textures)
                held.insert(name);
            return held;
        }

        SharedReshadeTexturePool(const SharedReshadeTexturePool&) = delete;
        SharedReshadeTexturePool& operator=(const SharedReshadeTexturePool&) = delete;

    private:
        LogicalDevice* pLogicalDevice;
        std::unordered_map<std::string, SharedReshadeTexture> textures;
    };

    struct SafeAntiCheatRefusal : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct DeviceRefusal : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct DepthOnOtherGpuRefusal : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    class ReshadeEffect : public Effect
    {
    public:
        ReshadeEffect(LogicalDevice*       pLogicalDevice,
                      VkFormat             format,
                      VkExtent2D           imageExtent,
                      VkColorSpaceKHR      colorSpace,
                      std::vector<VkImage> inputImages,
                      std::vector<VkImage> outputImages,
                      EffectRegistry*      pEffectRegistry,
                      std::shared_ptr<SharedReshadeTexturePool> texturePool,
                      std::string          effectName,
                      std::string          effectPath = "",
                      std::vector<PreprocessorDefinition> customDefs = {});
        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void virtual updateEffect(uint32_t imageIndex) override;
        void virtual useDepthImage(VkImageView depthImageView) override;
        virtual ~ReshadeEffect();

        const std::vector<std::string>& deviceNotices() const { return notices; }

    private:
        // once this delegating constructor completes, a throw in the public one runs ~ReshadeEffect
        explicit ReshadeEffect(LogicalDevice* pLogicalDevice) noexcept
            : pLogicalDevice(pLogicalDevice)
        {
        }

        LogicalDevice*           pLogicalDevice = nullptr;
        std::vector<std::string> notices;
        std::vector<VkImage>     inputImages;
        std::vector<VkImage>     outputImages;
        std::vector<VkImageView> inputImageViewsSRGB;
        std::vector<VkImageView> inputImageViewsUNORM;
        std::vector<VkImageView> outputImageViewsSRGB;
        std::vector<VkImageView> outputImageViewsUNORM;

        std::unordered_map<std::string, std::vector<VkImage>>     textureImages;
        std::unordered_map<std::string, std::vector<VkImageView>> textureImageViewsUNORM;
        std::unordered_map<std::string, std::vector<VkImageView>> textureImageViewsSRGB;
        std::unordered_map<std::string, std::vector<VkImageView>> renderImageViewsSRGB;
        std::unordered_map<std::string, std::vector<VkImageView>> renderImageViewsUNORM;

        std::unordered_map<std::string, VkFormat>   textureFormatsUNORM;
        std::unordered_map<std::string, VkFormat>   textureFormatsSRGB;
        std::unordered_map<std::string, uint32_t>   textureMipLevels;
        std::unordered_map<std::string, VkFilter>   textureMipFilters;
        std::unordered_map<std::string, VkExtent3D> textureExtents;
        std::unordered_set<std::string>             sharedTextureNames;

        std::vector<VkDescriptorSet> inputDescriptorSets;
        std::vector<VkDescriptorSet> outputDescriptorSets;
        std::vector<VkDescriptorSet> backBufferDescriptorSets;

        std::vector<std::vector<VkFramebuffer>> framebuffers;

        VkDescriptorSetLayout                 uniformDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout                 imageSamplerDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout                 storageImageDescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet>          storageDescriptorSets;
        std::vector<std::vector<VkImageView>> storageImageViewVector;
        std::vector<std::string>              storageTextureNames;
        std::map<std::string, VkShaderModule>  shaderModules;
        VkShaderModule shaderModuleFor(const std::string& entryPoint) const
        {
            const auto it = shaderModules.find(entryPoint);
            return it != shaderModules.end() ? it->second : VK_NULL_HANDLE;
        }
        VkDescriptorPool                      descriptorPool = VK_NULL_HANDLE;
        std::vector<VkRenderPass>             renderPasses;
        std::vector<std::vector<std::string>> renderTargets;
        std::vector<std::vector<VkClearValue>> renderPassClearValues;
        std::vector<VkRenderPassBeginInfo>    renderPassBeginInfos;
        VkPipelineLayout                      pipelineLayout = VK_NULL_HANDLE;
        std::vector<VkPipeline>               graphicsPipelines;
        std::vector<bool>                     switchSamplers;
        VkExtent2D                            imageExtent;
        std::vector<VkSampler>                samplers;
        std::shared_ptr<SharedReshadeTexturePool> sharedTexturePool;
        EffectRegistry*                       pEffectRegistry = nullptr;
        std::string                           effectName;
        std::string                           effectPath;
        std::vector<PreprocessorDefinition>   customPreprocessorDefs;
        reshadefx::effect_module                     module;

        std::vector<VkDeviceMemory>           textureMemory;

        VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkFormat    inputOutputFormatUNORM = VK_FORMAT_UNDEFINED;
        VkFormat    inputOutputFormatSRGB = VK_FORMAT_UNDEFINED;
        VkFormat    stencilFormat = VK_FORMAT_UNDEFINED;
        VkImage     stencilImage = VK_NULL_HANDLE;
        VkImageView stencilImageView = VK_NULL_HANDLE;
        int                      backBufferWriteCount = 0;
        std::vector<VkImage>     backBufferImages;
        std::vector<VkImageView> backBufferImageViewsUNORM;
        std::vector<VkImageView> backBufferImageViewsSRGB;
        std::vector<VkBuffer>       stagingBuffers;
        std::vector<VkDeviceMemory> stagingBufferMemories;
        uint32_t                    bufferSize = 0;
        std::vector<void*>          stagingBuffersMapped;
        std::vector<VkDescriptorSet> bufferDescriptorSets;

        std::vector<std::shared_ptr<ReshadeUniform>> uniforms;

        void          createReshadeModule();
        void          destroyResources() noexcept;
        VkFormat      convertReshadeFormat(reshadefx::texture_format texFormat);
        VkCompareOp   convertReshadeCompareOp(reshadefx::stencil_func compareOp);
        VkStencilOp   convertReshadeStencilOp(reshadefx::stencil_op stencilOp);
        VkBlendOp     convertReshadeBlendOp(reshadefx::blend_op blendOp);
        VkBlendFactor convertReshadeBlendFactor(reshadefx::blend_factor blendFactor);
        bool          resourcesDestroyed = false;
    };
}

#endif
