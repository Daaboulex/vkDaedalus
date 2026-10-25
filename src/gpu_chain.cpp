#include "gpu_chain.hpp"

#include <unistd.h>

#include "command_buffer.hpp"
#include "effects/effect_shared_copy.hpp"
#include "effects/effect_transfer.hpp"
#include "effect_timing.hpp"
#include "fake_swapchain.hpp"
#include "format.hpp"
#include "format_features.hpp"
#include "helper_device.hpp"
#include "imgui_overlay.hpp"
#include "memory.hpp"

namespace vkBasalt
{
    namespace
    {
        constexpr VkDeviceSize overReadPadding = 4096;

        std::string named(const LogicalDevice& device)
        {
            VkPhysicalDeviceProperties properties{};
            device.vki.GetPhysicalDeviceProperties(device.physicalDevice, &properties);
            return properties.deviceName;
        }

        struct Shared
        {
            VkBuffer       exportedBuffer = VK_NULL_HANDLE;
            VkDeviceMemory exportedMemory = VK_NULL_HANDLE;
            VkBuffer       importedBuffer = VK_NULL_HANDLE;
            VkDeviceMemory importedMemory = VK_NULL_HANDLE;
        };

        void shareBuffer(LogicalDevice& exporter, LogicalDevice& importer, VkDeviceSize bytes, Shared& shared)
        {
            VkExternalMemoryBufferCreateInfo external{};
            external.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
            external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkBufferCreateInfo info{};
            info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.pNext       = &external;
            info.size        = bytes;
            info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (exporter.vkd.CreateBuffer(exporter.device, &info, nullptr, &shared.exportedBuffer) != VK_SUCCESS)
                throw ChainRefusal(named(exporter) + " could not make a buffer to share a frame in");
            VkMemoryRequirements onExporter{};
            exporter.vkd.GetBufferMemoryRequirements(exporter.device, shared.exportedBuffer, &onExporter);
            const std::optional<uint32_t> exportType = findMemoryTypeAvoiding(
                &exporter, onExporter.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (!exportType)
                throw ChainRefusal(named(exporter) + " offers no system-memory type for a buffer it shares");
            VkMemoryDedicatedAllocateInfo dedicatedExport{};
            dedicatedExport.sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicatedExport.buffer = shared.exportedBuffer;
            VkExportMemoryAllocateInfo exported{};
            exported.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
            exported.pNext       = &dedicatedExport;
            exported.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkMemoryAllocateInfo allocateExport{};
            allocateExport.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateExport.pNext           = &exported;
            allocateExport.allocationSize  = onExporter.size;
            allocateExport.memoryTypeIndex = *exportType;
            if (allocateTrackedMemory(&exporter, &allocateExport, nullptr, &shared.exportedMemory) != VK_SUCCESS)
            {
                shared.exportedMemory = VK_NULL_HANDLE;
                throw ChainRefusal(named(exporter) + " could not allocate a shared buffer");
            }
            if (exporter.vkd.BindBufferMemory(exporter.device, shared.exportedBuffer, shared.exportedMemory, 0) != VK_SUCCESS)
                throw ChainRefusal(named(exporter) + " could not bind a shared buffer");

            VkMemoryGetFdInfoKHR getFd{};
            getFd.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
            getFd.memory     = shared.exportedMemory;
            getFd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            int fd           = -1;
            if (exporter.vkd.GetMemoryFdKHR(exporter.device, &getFd, &fd) != VK_SUCCESS || fd < 0)
                throw ChainRefusal(named(exporter) + " could not export a shared buffer as a dma-buf");

            const auto refuseClosing = [&fd](const std::string& why) {
                close(fd);
                fd = -1;
                return ChainRefusal(why);
            };
            if (importer.vkd.CreateBuffer(importer.device, &info, nullptr, &shared.importedBuffer) != VK_SUCCESS)
                throw refuseClosing(named(importer) + " could not make a buffer to share a frame in");
            VkMemoryRequirements onImporter{};
            importer.vkd.GetBufferMemoryRequirements(importer.device, shared.importedBuffer, &onImporter);
            VkMemoryFdPropertiesKHR fdProperties{};
            fdProperties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
            if (importer.vkd.GetMemoryFdPropertiesKHR(importer.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProperties) != VK_SUCCESS)
            {
                VkPhysicalDeviceProperties properties{};
                importer.vki.GetPhysicalDeviceProperties(importer.physicalDevice, &properties);
                throw refuseClosing(named(importer) + " cannot read the dma-buf " + named(exporter) + " exported"
                                    + (properties.vendorID == 0x10de ? " (an NVIDIA GPU needs nvidia_drm modeset=1 for it)" : ""));
            }
            const std::optional<uint32_t> importType = findMemoryTypeAvoiding(&importer, fdProperties.memoryTypeBits & onImporter.memoryTypeBits, 0, 0);
            if (!importType || onImporter.size > onExporter.size)
                throw refuseClosing(named(importer) + " has no memory type that takes the shared buffer");
            VkMemoryDedicatedAllocateInfo dedicatedImport{};
            dedicatedImport.sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicatedImport.buffer = shared.importedBuffer;
            VkImportMemoryFdInfoKHR imported{};
            imported.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
            imported.pNext      = &dedicatedImport;
            imported.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            imported.fd         = fd;
            VkMemoryAllocateInfo allocateImport{};
            allocateImport.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateImport.pNext           = &imported;
            allocateImport.allocationSize  = onImporter.size;
            allocateImport.memoryTypeIndex = *importType;
            if (allocateTrackedMemory(&importer, &allocateImport, nullptr, &shared.importedMemory) != VK_SUCCESS)
            {
                shared.importedMemory = VK_NULL_HANDLE;
                throw refuseClosing(named(importer) + " could not import the shared buffer");
            }
            if (importer.vkd.BindBufferMemory(importer.device, shared.importedBuffer, shared.importedMemory, 0) != VK_SUCCESS)
                throw ChainRefusal(named(importer) + " could not bind the shared buffer");
        }

        VkSemaphore makeSemaphore(LogicalDevice& device, bool exportable)
        {
            VkExportSemaphoreCreateInfo exported{};
            exported.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
            exported.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
            VkSemaphoreCreateInfo info{};
            info.sType            = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            info.pNext            = exportable ? &exported : nullptr;
            VkSemaphore semaphore = VK_NULL_HANDLE;
            if (device.vkd.CreateSemaphore(device.device, &info, nullptr, &semaphore) != VK_SUCCESS)
                throw ChainRefusal("a semaphore for the hand-off could not be made");
            return semaphore;
        }

        void refuseUnusableFormat(LogicalDevice& device, const VkSwapchainCreateInfoKHR& swapchainInfo)
        {
            const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                                              | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            for (const VkFormat format : {convertToUNORM(swapchainInfo.imageFormat), convertToSRGB(swapchainInfo.imageFormat)})
                if ((optimalTilingFeatures(&device, format) & needed) != needed)
                    throw ChainRefusal(named(device) + " cannot sample, draw into and copy the swapchain format " + std::to_string(format));
        }
    }

    VkDeviceSize sharedBufferBytes(VkExtent2D extent, uint32_t texelBytes)
    {
        const VkDeviceSize frame = static_cast<VkDeviceSize>(extent.width) * extent.height * texelBytes;
        return (frame + 15) / 16 * 16 + overReadPadding;
    }

    std::vector<uint32_t> GpuChain::tripGpus() const
    {
        std::vector<uint32_t> gpus;
        for (const ChainSegment& segment : segments)
            if (segment.helper && std::find(gpus.begin(), gpus.end(), segment.gpu) == gpus.end())
                gpus.push_back(segment.gpu);
        return gpus;
    }

    std::unique_ptr<GpuChain> buildChain(LogicalDevice&                  game,
                                         const std::vector<ChainPart>&   parts,
                                         const VkSwapchainCreateInfoKHR& swapchainInfo,
                                         const std::vector<VkImage>&     swapchainImages,
                                         const SegmentEffectBuilder&     buildRows,
                                         bool                            timeHelpers)
    {
        const uint32_t   count  = static_cast<uint32_t>(swapchainImages.size());
        const VkExtent2D extent = swapchainInfo.imageExtent;
        const uint32_t   texel  = formatTexelBytes(swapchainInfo.imageFormat);
        if (texel == 0)
            throw ChainRefusal("the swapchain format " + std::to_string(swapchainInfo.imageFormat) + " has no texel size the hand-off knows");
        if (swapchainInfo.imageArrayLayers != 1)
            throw ChainRefusal("the swapchain has " + std::to_string(swapchainInfo.imageArrayLayers) + " layers per image");
        if (parts.empty() || parts.back().helper)
            throw ChainRefusal("the chain does not end on the game's GPU");

        auto chain = std::make_unique<GpuChain>();
        try
        {
            for (uint32_t i = 0; i < count; i++)
                chain->headDone.push_back(makeSemaphore(game, true));

            const VkDeviceSize bytes = sharedBufferBytes(extent, texel);
            LogicalDevice*     from  = &game;
            for (const ChainPart& part : parts)
            {
                ChainSegment& segment = chain->segments.emplace_back();
                segment.helper        = part.helper;
                segment.device        = part.helper ? &part.helper->device : &game;
                segment.gpu           = part.gpu;
                segment.firstRow      = part.firstRow;
                segment.rows          = part.rows;
                LogicalDevice& to     = *segment.device;
                if (part.helper)
                    refuseUnusableFormat(to, swapchainInfo);
                const bool fromExports = from == &game || &to != &game;
                for (uint32_t i = 0; i < count; i++)
                {
                    Shared shared;
                    try
                    {
                        shareBuffer(fromExports ? *from : to, fromExports ? to : *from, bytes, shared);
                    }
                    catch (...)
                    {
                        segment.in.push_back(fromExports ? SharedBuffer{shared.exportedBuffer, shared.importedBuffer, shared.exportedMemory, shared.importedMemory}
                                                         : SharedBuffer{shared.importedBuffer, shared.exportedBuffer, shared.importedMemory, shared.exportedMemory});
                        throw;
                    }
                    segment.in.push_back(fromExports ? SharedBuffer{shared.exportedBuffer, shared.importedBuffer, shared.exportedMemory, shared.importedMemory}
                                                     : SharedBuffer{shared.importedBuffer, shared.exportedBuffer, shared.importedMemory, shared.exportedMemory});
                    segment.start.push_back(makeSemaphore(to, false));
                    segment.done.push_back(makeSemaphore(to, true));
                    if (part.helper && createSignalledFence(&to, &segment.fences.emplace_back()) != VK_SUCCESS)
                        throw ChainRefusal(named(to) + " could not make a fence for the hand-off");
                }
                from = &to;
            }

            if (game.timestampValidBits > 0)
            {
                VkQueryPoolCreateInfo queries{};
                queries.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                queries.queryType  = VK_QUERY_TYPE_TIMESTAMP;
                queries.queryCount = count * 2;
                if (game.vkd.CreateQueryPool(game.device, &queries, nullptr, &chain->roundTrips) != VK_SUCCESS)
                    chain->roundTrips = VK_NULL_HANDLE;
                chain->roundTripPending.assign(count, false);
            }

            for (size_t k = 0; k < chain->segments.size(); k++)
            {
                ChainSegment&  segment = chain->segments[k];
                LogicalDevice& device  = *segment.device;
                const bool     last    = k + 1 == chain->segments.size();
                if (!last || segment.rows > 0)
                {
                    VkSwapchainCreateInfoKHR slotInfo = swapchainInfo;
                    slotInfo.imageUsage               = 0;
                    slotInfo.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
                    slotInfo.queueFamilyIndexCount    = 0;
                    slotInfo.pQueueFamilyIndices      = nullptr;
                    segment.slots = createFakeSwapchainImages(&device, slotInfo, count * static_cast<uint32_t>(segment.rows + 1), segment.slotMemory);
                    if (segment.slots.empty())
                        throw ChainRefusal(named(device) + " could not make the images the effects run between");
                }
                const auto slot = [&](size_t s) {
                    return std::vector<VkImage>(segment.slots.begin() + s * count, segment.slots.begin() + (s + 1) * count);
                };
                std::vector<VkBuffer> inBuffers;
                for (const SharedBuffer& shared : segment.in)
                    inBuffers.push_back(shared.onTo);

                if (last && chain->roundTrips != VK_NULL_HANDLE)
                    segment.effects.push_back(std::make_shared<TimestampEffect>(&device, chain->roundTrips, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));
                if (last && segment.rows == 0)
                {
                    segment.effects.push_back(std::make_shared<SharedCopyEffect>(&device, extent, swapchainImages, inBuffers, SharedCopyDirection::BufferToImage));
                }
                else
                {
                    segment.effects.push_back(std::make_shared<SharedCopyEffect>(&device, extent, slot(0), inBuffers, SharedCopyDirection::BufferToImage));
                    SegmentRows built      = buildRows(&device, segment.gpu, segment.firstRow, segment.rows, segment.slots, count);
                    segment.firstRowEffect = segment.effects.size();
                    segment.rowNames       = std::move(built.names);
                    for (auto& effect : built.effects)
                        segment.effects.push_back(std::move(effect));
                    if (last)
                    {
                        segment.effects.push_back(std::make_shared<TransferEffect>(&device, extent, slot(segment.rows), swapchainImages));
                    }
                    else
                    {
                        std::vector<VkBuffer> outBuffers;
                        for (const SharedBuffer& shared : chain->segments[k + 1].in)
                            outBuffers.push_back(shared.onFrom);
                        segment.effects.push_back(
                            std::make_shared<SharedCopyEffect>(&device, extent, slot(segment.rows), outBuffers, SharedCopyDirection::ImageToBuffer));
                    }
                }
                if (timeHelpers && segment.helper && device.timestampValidBits > 0 && device.timestampPeriodNanoseconds > 0.0f &&
                    validEffectTimingQueryAllocation(segment.effects.size(), count))
                {
                    VkQueryPoolCreateInfo queries{};
                    queries.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                    queries.queryType  = VK_QUERY_TYPE_TIMESTAMP;
                    queries.queryCount = count * 2u * static_cast<uint32_t>(segment.effects.size());
                    if (device.vkd.CreateQueryPool(device.device, &queries, nullptr, &segment.timing) != VK_SUCCESS)
                        segment.timing = VK_NULL_HANDLE;
                    segment.timingPending.assign(count, false);
                    segment.timingMs.assign(segment.effects.size(), -1.0f);
                }
                segment.commandBuffers = recordCommandBuffers(&device, count, segment.effects, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED, segment.timing,
                                                              segment.timing != VK_NULL_HANDLE ? static_cast<uint32_t>(segment.effects.size()) : 0);
            }
        }
        catch (const ChainRefusal&)
        {
            chain->destroy(game);
            throw;
        }
        catch (const std::exception& e)
        {
            chain->destroy(game);
            throw ChainRefusal(e.what());
        }
        return chain;
    }

    void GpuChain::readRoundTrip(LogicalDevice& game, uint32_t index)
    {
        if (roundTrips == VK_NULL_HANDLE || index >= roundTripPending.size() || !roundTripPending[index])
            return;
        roundTripPending[index] = false;
        uint64_t       stamps[2] = {};
        const VkResult read      = game.vkd.GetQueryPoolResults(game.device, roundTrips, index * 2, 2, sizeof(stamps), stamps, sizeof(uint64_t),
                                                                VK_QUERY_RESULT_64_BIT);
        if (read != VK_SUCCESS)
            return;
        const float sample    = effectTimestampMilliseconds(stamps[0], stamps[1], game.timestampValidBits, game.timestampPeriodNanoseconds);
        roundTripMilliseconds = smoothEffectTiming(roundTripMilliseconds, sample, roundTripMilliseconds >= 0.0f);
        roundTripReads++;
    }

    void GpuChain::markSegmentTimings(uint32_t index)
    {
        for (ChainSegment& segment : segments)
            if (segment.timing != VK_NULL_HANDLE && index < segment.timingPending.size())
                segment.timingPending[index] = true;
    }

    void GpuChain::readSegmentTimings(uint32_t index)
    {
        for (ChainSegment& segment : segments)
        {
            if (segment.timing == VK_NULL_HANDLE || index >= segment.timingPending.size() || !segment.timingPending[index])
                continue;
            segment.timingPending[index] = false;
            const uint32_t        effects = static_cast<uint32_t>(segment.effects.size());
            std::vector<uint64_t> stamps(effects * 2u, 0);
            const VkResult read = segment.device->vkd.GetQueryPoolResults(segment.device->device, segment.timing, effectTimingQueryBase(index, effects), effects * 2u,
                                                                          stamps.size() * sizeof(uint64_t), stamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            if (read != VK_SUCCESS)
                continue;
            for (uint32_t e = 0; e < effects; e++)
            {
                const float sample  = effectTimestampMilliseconds(stamps[e * 2u], stamps[e * 2u + 1u], segment.device->timestampValidBits,
                                                                  segment.device->timestampPeriodNanoseconds);
                segment.timingMs[e] = smoothEffectTiming(segment.timingMs[e], sample, segment.timingMs[e] >= 0.0f);
            }
        }
    }

    void GpuChain::destroy(LogicalDevice& game)
    {
        for (ChainSegment& segment : segments)
            if (segment.helper)
                segment.device->vkd.DeviceWaitIdle(segment.device->device);

        LogicalDevice* from = &game;
        for (ChainSegment& segment : segments)
        {
            LogicalDevice& device = *segment.device;
            segment.effects.clear();
            if (!segment.commandBuffers.empty())
                device.vkd.FreeCommandBuffers(device.device, device.commandPool, static_cast<uint32_t>(segment.commandBuffers.size()),
                                              segment.commandBuffers.data());
            segment.commandBuffers.clear();
            if (segment.timing != VK_NULL_HANDLE)
                segment.device->vkd.DestroyQueryPool(segment.device->device, segment.timing, nullptr);
            segment.timing = VK_NULL_HANDLE;
            for (VkImage image : segment.slots)
                device.vkd.DestroyImage(device.device, image, nullptr);
            segment.slots.clear();
            if (segment.slotMemory != VK_NULL_HANDLE)
                freeTrackedMemory(&device, segment.slotMemory, nullptr);
            segment.slotMemory = VK_NULL_HANDLE;
            for (VkFence fence : segment.fences)
                device.vkd.DestroyFence(device.device, fence, nullptr);
            for (VkSemaphore semaphore : segment.start)
                device.vkd.DestroySemaphore(device.device, semaphore, nullptr);
            for (VkSemaphore semaphore : segment.done)
                device.vkd.DestroySemaphore(device.device, semaphore, nullptr);
            segment.fences.clear();
            segment.start.clear();
            segment.done.clear();
            for (const SharedBuffer& shared : segment.in)
            {
                if (shared.onTo != VK_NULL_HANDLE)
                    device.vkd.DestroyBuffer(device.device, shared.onTo, nullptr);
                if (shared.memoryTo != VK_NULL_HANDLE)
                    freeTrackedMemory(&device, shared.memoryTo, nullptr);
                if (shared.onFrom != VK_NULL_HANDLE)
                    from->vkd.DestroyBuffer(from->device, shared.onFrom, nullptr);
                if (shared.memoryFrom != VK_NULL_HANDLE)
                    freeTrackedMemory(from, shared.memoryFrom, nullptr);
            }
            segment.in.clear();
            from = &device;
        }
        for (VkSemaphore semaphore : headDone)
            game.vkd.DestroySemaphore(game.device, semaphore, nullptr);
        headDone.clear();
        if (roundTrips != VK_NULL_HANDLE)
            game.vkd.DestroyQueryPool(game.device, roundTrips, nullptr);
        roundTrips = VK_NULL_HANDLE;
        segments.clear();
    }
}
