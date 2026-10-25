#include "overlay/overlay_renderer.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "imgui.h"

#include "buffer.hpp"
#include "descriptor_set.hpp"
#include "image.hpp"
#include "image_view.hpp"
#include "logical_device.hpp"
#include "memory.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "shader_sources.hpp"

namespace vkBasalt
{
    static_assert(std::is_same_v<ImTextureID, unsigned long long>, "the renderer keys its textures by ImTextureID");

    namespace
    {
        constexpr uint32_t     textureSetCapacity = 8;
        constexpr VkDeviceSize minimumBufferBytes = 64 * 1024;

        VkRenderPass createOverlayRenderPass(LogicalDevice& device, VkFormat format)
        {
            VkAttachmentDescription attachment = {};
            attachment.format                  = format;
            attachment.samples                 = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp                  = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            attachment.finalLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorRef = {};
            colorRef.attachment            = 0;
            colorRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &colorRef;

            VkSubpassDependency dependencies[2] = {};
            dependencies[0].srcSubpass          = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass          = 0;
            dependencies[0].srcStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].dstStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask       = 0;
            dependencies[0].dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            // MEMORY_READ, not the 0 the spec allows for present, so DMA-BUF readers such as PipeWire see flushed caches
            dependencies[1].srcSubpass    = 0;
            dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

            VkRenderPassCreateInfo renderPassInfo = {};
            renderPassInfo.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount        = 1;
            renderPassInfo.pAttachments           = &attachment;
            renderPassInfo.subpassCount           = 1;
            renderPassInfo.pSubpasses             = &subpass;
            renderPassInfo.dependencyCount        = 2;
            renderPassInfo.pDependencies          = dependencies;

            VkRenderPass   renderPass = VK_NULL_HANDLE;
            const VkResult result     = device.vkd.CreateRenderPass(device.device, &renderPassInfo, nullptr, &renderPass);
            ASSERT_VULKAN(result);
            return renderPass;
        }

        VkPipeline createOverlayPipeline(
            LogicalDevice& device, VkRenderPass renderPass, VkPipelineLayout layout, VkShaderModule vertexShader, VkShaderModule fragmentShader)
        {
            VkPipelineShaderStageCreateInfo stages[2] = {};
            stages[0].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module                          = vertexShader;
            stages[0].pName                           = "main";
            stages[1].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module                          = fragmentShader;
            stages[1].pName                           = "main";

            VkVertexInputBindingDescription binding = {};
            binding.binding                         = 0;
            binding.stride                          = sizeof(ImDrawVert);
            binding.inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attributes[3] = {};
            attributes[0]                                   = {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(ImDrawVert, pos))};
            attributes[1]                                   = {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(ImDrawVert, uv))};
            attributes[2]                                   = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(offsetof(ImDrawVert, col))};

            VkPipelineVertexInputStateCreateInfo vertexInput = {};
            vertexInput.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount        = 1;
            vertexInput.pVertexBindingDescriptions           = &binding;
            vertexInput.vertexAttributeDescriptionCount      = 3;
            vertexInput.pVertexAttributeDescriptions         = attributes;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
            inputAssembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewport = {};
            viewport.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount                     = 1;
            viewport.scissorCount                      = 1;

            VkPipelineRasterizationStateCreateInfo rasterization = {};
            rasterization.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterization.polygonMode                            = VK_POLYGON_MODE_FILL;
            rasterization.cullMode                               = VK_CULL_MODE_NONE;
            rasterization.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterization.lineWidth                              = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample = {};
            multisample.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState blendAttachment = {};
            blendAttachment.blendEnable                         = VK_TRUE;
            blendAttachment.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor                 = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp                        = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp                        = VK_BLEND_OP_ADD;
            blendAttachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            VkPipelineColorBlendStateCreateInfo blend = {};
            blend.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount                     = 1;
            blend.pAttachments                        = &blendAttachment;

            const VkDynamicState             dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic          = {};
            dynamic.sType                                     = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount                         = 2;
            dynamic.pDynamicStates                            = dynamicStates;

            VkGraphicsPipelineCreateInfo pipelineInfo = {};
            pipelineInfo.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount                   = 2;
            pipelineInfo.pStages                      = stages;
            pipelineInfo.pVertexInputState            = &vertexInput;
            pipelineInfo.pInputAssemblyState          = &inputAssembly;
            pipelineInfo.pViewportState               = &viewport;
            pipelineInfo.pRasterizationState          = &rasterization;
            pipelineInfo.pMultisampleState            = &multisample;
            pipelineInfo.pColorBlendState             = &blend;
            pipelineInfo.pDynamicState                = &dynamic;
            pipelineInfo.layout                       = layout;
            pipelineInfo.renderPass                   = renderPass;

            VkPipeline     pipeline = VK_NULL_HANDLE;
            const VkResult result   = device.vkd.CreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
            ASSERT_VULKAN(result);
            return pipeline;
        }
    }

    void destroyOverlayBuffer(LogicalDevice& device, OverlayBuffer& buffer)
    {
        if (buffer.memory != VK_NULL_HANDLE)
            freeTrackedMemory(&device, buffer.memory, nullptr);
        if (buffer.buffer != VK_NULL_HANDLE)
            device.vkd.DestroyBuffer(device.device, buffer.buffer, nullptr);
        buffer = OverlayBuffer{};
    }

    std::vector<VkFence> SwapchainOverlay::fences() const
    {
        std::vector<VkFence> pending;
        for (const OverlayDraw& draw : draws)
            if (draw.fence != VK_NULL_HANDLE)
                pending.push_back(draw.fence);
        return pending;
    }

    bool SwapchainOverlay::restoreFence(LogicalDevice& device, uint32_t imageIndex)
    {
        if (imageIndex >= draws.size())
            return false;
        device.vkd.DestroyFence(device.device, draws[imageIndex].fence, nullptr);
        return createSignalledFence(&device, &draws[imageIndex].fence) == VK_SUCCESS;
    }

    void SwapchainOverlay::destroy(LogicalDevice& device)
    {
        for (OverlayDraw& draw : draws)
        {
            if (draw.framebuffer != VK_NULL_HANDLE)
                device.vkd.DestroyFramebuffer(device.device, draw.framebuffer, nullptr);
            if (draw.fence != VK_NULL_HANDLE)
                device.vkd.DestroyFence(device.device, draw.fence, nullptr);
            destroyOverlayBuffer(device, draw.vertices);
            destroyOverlayBuffer(device, draw.indices);
        }
        draws.clear();
        if (commandPool != VK_NULL_HANDLE)
            device.vkd.DestroyCommandPool(device.device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
        format      = VK_FORMAT_UNDEFINED;
        extent      = {0, 0};
    }

    OverlayRenderer::OverlayRenderer(LogicalDevice& device, StoreOnly) : device(device)
    {
    }

    OverlayRenderer::OverlayRenderer(LogicalDevice& device) : OverlayRenderer(device, StoreOnly{})
    {
        sampler   = createSampler(&device);
        setLayout = createImageSamplerDescriptorSetLayout(&device, 1);

        VkPushConstantRange transform = {};
        transform.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;
        transform.offset              = 0;
        transform.size                = sizeof(float) * 4;

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount             = 1;
        layoutInfo.pSetLayouts                = &setLayout;
        layoutInfo.pushConstantRangeCount     = 1;
        layoutInfo.pPushConstantRanges        = &transform;
        VkPipelineLayout layout               = VK_NULL_HANDLE;
        const VkResult   laidOut              = device.vkd.CreatePipelineLayout(device.device, &layoutInfo, nullptr, &layout);
        ASSERT_VULKAN(laidOut);
        pipelineLayout = layout;

        descriptorPool = createDescriptorPool(&device, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureSetCapacity}});

        VkShaderModule vertex = VK_NULL_HANDLE;
        createShaderModule(&device, overlay_vert, &vertex);
        vertexShader            = vertex;
        VkShaderModule fragment = VK_NULL_HANDLE;
        createShaderModule(&device, overlay_frag, &fragment);
        fragmentShader = fragment;
    }

    OverlayRenderer::~OverlayRenderer()
    {
        for (const std::unique_ptr<Texture>& texture : textures)
            releaseTexture(*texture);
        textures.clear();
        for (const FormatPipeline& entry : pipelines)
        {
            device.vkd.DestroyPipeline(device.device, entry.pipeline, nullptr);
            device.vkd.DestroyRenderPass(device.device, entry.renderPass, nullptr);
        }
        if (fragmentShader != VK_NULL_HANDLE)
            device.vkd.DestroyShaderModule(device.device, fragmentShader, nullptr);
        if (vertexShader != VK_NULL_HANDLE)
            device.vkd.DestroyShaderModule(device.device, vertexShader, nullptr);
        if (descriptorPool != VK_NULL_HANDLE)
            device.vkd.DestroyDescriptorPool(device.device, descriptorPool, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE)
            device.vkd.DestroyPipelineLayout(device.device, pipelineLayout, nullptr);
        if (setLayout != VK_NULL_HANDLE)
            device.vkd.DestroyDescriptorSetLayout(device.device, setLayout, nullptr);
        if (sampler != VK_NULL_HANDLE)
            device.vkd.DestroySampler(device.device, sampler, nullptr);
    }

    const OverlayRenderer::FormatPipeline& OverlayRenderer::pipelineFor(VkFormat format)
    {
        for (const FormatPipeline& entry : pipelines)
            if (entry.format == format)
                return entry;

        const VkRenderPass renderPass = createOverlayRenderPass(device, format);
        VkPipeline         pipeline   = VK_NULL_HANDLE;
        try
        {
            pipeline = createOverlayPipeline(device, renderPass, pipelineLayout, vertexShader, fragmentShader);
        }
        catch (...)
        {
            device.vkd.DestroyRenderPass(device.device, renderPass, nullptr);
            throw;
        }
        pipelines.push_back({format, renderPass, pipeline});
        return pipelines.back();
    }

    void OverlayRenderer::prepare(SwapchainOverlay& overlay, VkFormat format, VkExtent2D extent, const std::vector<VkImageView>& views)
    {
        if (std::find(views.begin(), views.end(), VK_NULL_HANDLE) != views.end())
            throw std::runtime_error("a swapchain image has no view to draw on");
        const VkRenderPass renderPass = pipelineFor(format).renderPass;

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex        = device.queueFamilyIndex;
        VkCommandPool  pool              = VK_NULL_HANDLE;
        const VkResult pooled            = device.vkd.CreateCommandPool(device.device, &poolInfo, nullptr, &pool);
        ASSERT_VULKAN(pooled);
        overlay.commandPool = pool;
        overlay.format      = format;
        overlay.extent      = extent;
        overlay.draws.resize(views.size());
        if (views.empty())
            return;

        std::vector<VkCommandBuffer> commandBuffers(views.size(), VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo  allocateInfo = {};
        allocateInfo.sType                        = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool                  = pool;
        allocateInfo.level                        = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount           = static_cast<uint32_t>(views.size());
        const VkResult allocated                  = allocateDispatchableCommandBuffers(&device, allocateInfo, commandBuffers.data());
        ASSERT_VULKAN(allocated);

        for (size_t i = 0; i < views.size(); i++)
        {
            OverlayDraw& draw  = overlay.draws[i];
            draw.commandBuffer = commandBuffers[i];
            ASSERT_VULKAN(createSignalledFence(&device, &draw.fence));

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass              = renderPass;
            framebufferInfo.attachmentCount         = 1;
            framebufferInfo.pAttachments            = &views[i];
            framebufferInfo.width                   = extent.width;
            framebufferInfo.height                  = extent.height;
            framebufferInfo.layers                  = 1;
            VkFramebuffer  framebuffer              = VK_NULL_HANDLE;
            const VkResult made                     = device.vkd.CreateFramebuffer(device.device, &framebufferInfo, nullptr, &framebuffer);
            ASSERT_VULKAN(made);
            draw.framebuffer = framebuffer;
        }
    }

    bool OverlayRenderer::texturesPending(const ImDrawData& drawData)
    {
        if (drawData.Textures == nullptr)
            return false;
        return std::any_of(drawData.Textures->begin(), drawData.Textures->end(), [](const ImTextureData* texture) {
            return texture->Status != ImTextureStatus_OK && texture->Status != ImTextureStatus_Destroyed;
        });
    }

    void OverlayRenderer::updateTextures(ImDrawData& drawData)
    {
        if (drawData.Textures == nullptr)
            return;
        for (ImTextureData* texture : *drawData.Textures)
        {
            switch (texture->Status)
            {
                case ImTextureStatus_OK:
                case ImTextureStatus_Destroyed: break;
                case ImTextureStatus_WantCreate: createTexture(*texture); break;
                case ImTextureStatus_WantUpdates:
                {
                    const Texture* record = static_cast<const Texture*>(texture->BackendUserData);
                    if (record == nullptr || texture->Pixels == nullptr)
                        throw std::runtime_error("ImGui asked to update a texture the overlay never made");
                    uploadToImage(&device,
                                  record->image,
                                  {static_cast<uint32_t>(texture->Width), static_cast<uint32_t>(texture->Height), 1},
                                  static_cast<uint32_t>(texture->GetSizeInBytes()),
                                  texture->Pixels);
                    texture->SetStatus(ImTextureStatus_OK);
                    break;
                }
                case ImTextureStatus_WantDestroy:
                    if (texture->UnusedFrames > 0)
                        destroyTexture(*texture);
                    break;
            }
        }
    }

    void OverlayRenderer::createTexture(ImTextureData& texture)
    {
        if (texture.Format != ImTextureFormat_RGBA32)
            throw std::runtime_error("ImGui asked for a texture in format " + std::to_string(texture.Format) + ", which the overlay does not draw");
        if (texture.Pixels == nullptr || texture.Width <= 0 || texture.Height <= 0)
            throw std::runtime_error("ImGui asked for a texture with no pixels");

        const VkExtent3D         extent = {static_cast<uint32_t>(texture.Width), static_cast<uint32_t>(texture.Height), 1};
        std::unique_ptr<Texture> record = std::make_unique<Texture>();
        try
        {
            record->image = createImages(&device,
                                         1,
                                         extent,
                                         VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                         record->memory)[0];
            record->view  = createImageViews(&device, VK_FORMAT_R8G8B8A8_UNORM, {record->image})[0];
            uploadToImage(&device, record->image, extent, static_cast<uint32_t>(texture.GetSizeInBytes()), texture.Pixels);
            record->set = takeSet(record->view);
        }
        catch (...)
        {
            releaseTexture(*record);
            throw;
        }
        texture.SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(record.get())));
        texture.BackendUserData = record.get();
        textures.push_back(std::move(record));
        texture.SetStatus(ImTextureStatus_OK);
    }

    void OverlayRenderer::destroyTexture(ImTextureData& texture)
    {
        const auto found = std::find_if(textures.begin(), textures.end(), [&](const std::unique_ptr<Texture>& record) {
            return record.get() == texture.BackendUserData;
        });
        if (found != textures.end())
        {
            releaseTexture(**found);
            textures.erase(found);
        }
        texture.SetTexID(ImTextureID_Invalid);
        texture.BackendUserData = nullptr;
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    void OverlayRenderer::releaseTexture(Texture& texture)
    {
        if (texture.set != VK_NULL_HANDLE)
            spareSets.push_back(texture.set);
        if (texture.view != VK_NULL_HANDLE)
            device.vkd.DestroyImageView(device.device, texture.view, nullptr);
        if (texture.image != VK_NULL_HANDLE)
            device.vkd.DestroyImage(device.device, texture.image, nullptr);
        if (texture.memory != VK_NULL_HANDLE)
            freeTrackedMemory(&device, texture.memory, nullptr);
        texture = Texture{};
    }

    void OverlayRenderer::releaseTextures(ImTextureData* const* all, int count)
    {
        for (int i = 0; i < count; i++)
            if (all[i]->BackendUserData != nullptr)
                destroyTexture(*all[i]);
    }

    VkDescriptorSet OverlayRenderer::takeSet(VkImageView view)
    {
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!spareSets.empty())
        {
            set = spareSets.back();
            spareSets.pop_back();
        }
        else
        {
            VkDescriptorSetAllocateInfo allocateInfo = {};
            allocateInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocateInfo.descriptorPool              = descriptorPool;
            allocateInfo.descriptorSetCount          = 1;
            allocateInfo.pSetLayouts                 = &setLayout;
            const VkResult allocated                 = device.vkd.AllocateDescriptorSets(device.device, &allocateInfo, &set);
            ASSERT_VULKAN(allocated);
        }

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler               = sampler;
        imageInfo.imageView             = view;
        imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write = {};
        write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet               = set;
        write.dstBinding           = 0;
        write.descriptorCount      = 1;
        write.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo           = &imageInfo;
        device.vkd.UpdateDescriptorSets(device.device, 1, &write, 0, nullptr);
        return set;
    }

    const OverlayRenderer::Texture* OverlayRenderer::findTexture(unsigned long long id) const
    {
        for (const std::unique_ptr<Texture>& record : textures)
            if (static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(record.get())) == id)
                return record.get();
        return nullptr;
    }

    void OverlayRenderer::reserve(OverlayBuffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage)
    {
        if (buffer.buffer != VK_NULL_HANDLE && buffer.capacity >= bytes)
            return;

        OverlayBuffer grown;
        grown.capacity = std::max(minimumBufferBytes, std::bit_ceil(bytes));
        createBuffer(&device,
                     grown.capacity,
                     usage,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     grown.buffer,
                     grown.memory);
        const VkResult mapped = device.vkd.MapMemory(device.device, grown.memory, 0, VK_WHOLE_SIZE, 0, &grown.mapped);
        if (mapped != VK_SUCCESS)
        {
            destroyOverlayBuffer(device, grown);
            ASSERT_VULKAN(mapped);
        }
        destroyOverlayBuffer(device, buffer);
        buffer = grown;
    }

    void OverlayRenderer::record(OverlayDraw& draw, const SwapchainOverlay& overlay, const ImDrawData& drawData)
    {
        const FormatPipeline& pipeline = pipelineFor(overlay.format);
        if (drawData.TotalVtxCount > 0)
        {
            reserve(draw.vertices, sizeof(ImDrawVert) * static_cast<VkDeviceSize>(drawData.TotalVtxCount), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            reserve(draw.indices, sizeof(ImDrawIdx) * static_cast<VkDeviceSize>(drawData.TotalIdxCount), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            ImDrawVert* vertexOut = static_cast<ImDrawVert*>(draw.vertices.mapped);
            ImDrawIdx*  indexOut  = static_cast<ImDrawIdx*>(draw.indices.mapped);
            for (const ImDrawList* list : drawData.CmdLists)
            {
                std::memcpy(vertexOut, list->VtxBuffer.Data, sizeof(ImDrawVert) * static_cast<size_t>(list->VtxBuffer.Size));
                std::memcpy(indexOut, list->IdxBuffer.Data, sizeof(ImDrawIdx) * static_cast<size_t>(list->IdxBuffer.Size));
                vertexOut += list->VtxBuffer.Size;
                indexOut += list->IdxBuffer.Size;
            }
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_VULKAN(device.vkd.BeginCommandBuffer(draw.commandBuffer, &beginInfo));

        VkRenderPassBeginInfo passBegin = {};
        passBegin.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passBegin.renderPass            = pipeline.renderPass;
        passBegin.framebuffer           = draw.framebuffer;
        passBegin.renderArea.extent     = overlay.extent;
        device.vkd.CmdBeginRenderPass(draw.commandBuffer, &passBegin, VK_SUBPASS_CONTENTS_INLINE);

        const float width  = static_cast<float>(overlay.extent.width);
        const float height = static_cast<float>(overlay.extent.height);
        if (drawData.TotalVtxCount > 0 && drawData.DisplaySize.x > 0.0f && drawData.DisplaySize.y > 0.0f)
        {
            device.vkd.CmdBindPipeline(draw.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
            const VkDeviceSize origin = 0;
            device.vkd.CmdBindVertexBuffers(draw.commandBuffer, 0, 1, &draw.vertices.buffer, &origin);
            device.vkd.CmdBindIndexBuffer(
                draw.commandBuffer, draw.indices.buffer, 0, sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

            const VkViewport viewport = {0.0f, 0.0f, width, height, 0.0f, 1.0f};
            device.vkd.CmdSetViewport(draw.commandBuffer, 0, 1, &viewport);

            const float scaleX       = 2.0f / drawData.DisplaySize.x;
            const float scaleY       = 2.0f / drawData.DisplaySize.y;
            const float transform[4] = {scaleX, scaleY, -1.0f - drawData.DisplayPos.x * scaleX, -1.0f - drawData.DisplayPos.y * scaleY};
            device.vkd.CmdPushConstants(draw.commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(transform), transform);

            VkDescriptorSet bound        = VK_NULL_HANDLE;
            uint32_t        vertexOffset = 0;
            uint32_t        indexOffset  = 0;
            for (const ImDrawList* list : drawData.CmdLists)
            {
                for (const ImDrawCmd& command : list->CmdBuffer)
                {
                    if (command.UserCallback != nullptr || command.ElemCount == 0)
                        continue;
                    const float left   = std::max(0.0f, command.ClipRect.x - drawData.DisplayPos.x);
                    const float top    = std::max(0.0f, command.ClipRect.y - drawData.DisplayPos.y);
                    const float right  = std::min(width, command.ClipRect.z - drawData.DisplayPos.x);
                    const float bottom = std::min(height, command.ClipRect.w - drawData.DisplayPos.y);
                    if (right <= left || bottom <= top)
                        continue;
                    const Texture* texture = findTexture(command.TexRef._TexData ? command.TexRef._TexData->TexID : command.TexRef._TexID);
                    if (texture == nullptr)
                        continue;
                    if (texture->set != bound)
                    {
                        device.vkd.CmdBindDescriptorSets(
                            draw.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &texture->set, 0, nullptr);
                        bound = texture->set;
                    }
                    const VkRect2D scissor = {{static_cast<int32_t>(left), static_cast<int32_t>(top)},
                                              {static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top)}};
                    device.vkd.CmdSetScissor(draw.commandBuffer, 0, 1, &scissor);
                    device.vkd.CmdDrawIndexed(draw.commandBuffer,
                                              command.ElemCount,
                                              1,
                                              command.IdxOffset + indexOffset,
                                              static_cast<int32_t>(command.VtxOffset + vertexOffset),
                                              0);
                }
                vertexOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
                indexOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
            }
        }

        device.vkd.CmdEndRenderPass(draw.commandBuffer);
        ASSERT_VULKAN(device.vkd.EndCommandBuffer(draw.commandBuffer));
    }
}
