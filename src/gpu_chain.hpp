#ifndef GPU_CHAIN_HPP_INCLUDED
#define GPU_CHAIN_HPP_INCLUDED

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkan_include.hpp"
#include "vkdispatch.hpp"
#include "effects/effect.hpp"

namespace vkBasalt
{
    struct HelperDevice;
    struct LogicalDevice;

    struct ChainRefusal : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct SharedBuffer
    {
        VkBuffer       onFrom     = VK_NULL_HANDLE;
        VkBuffer       onTo       = VK_NULL_HANDLE;
        VkDeviceMemory memoryFrom = VK_NULL_HANDLE;
        VkDeviceMemory memoryTo   = VK_NULL_HANDLE;
    };

    struct ChainSegment
    {
        std::shared_ptr<HelperDevice>        helper;
        LogicalDevice*                       device   = nullptr;
        uint32_t                             gpu      = 0;
        size_t                               firstRow = 0;
        size_t                               rows     = 0;
        std::vector<SharedBuffer>            in;
        std::vector<VkSemaphore>             start;
        std::vector<VkSemaphore>             done;
        std::vector<VkFence>                 fences;
        std::vector<VkImage>                 slots;
        VkDeviceMemory                       slotMemory = VK_NULL_HANDLE;
        std::vector<std::shared_ptr<Effect>> effects;
        std::vector<VkCommandBuffer>         commandBuffers;
        std::vector<std::string>             rowNames;
        size_t                               firstRowEffect = 0;
        VkQueryPool                          timing         = VK_NULL_HANDLE;
        std::vector<bool>                    timingPending;
        std::vector<float>                   timingMs;

        struct HelperTimes
        {
            float              copyInMs  = -1.0f;
            std::vector<float> rowsMs;
            float              copyOutMs = -1.0f;
        };

        std::optional<HelperTimes> helperTimes() const
        {
            if (!helper || timing == VK_NULL_HANDLE || firstRowEffect == 0 || firstRowEffect + rowNames.size() >= timingMs.size())
                return std::nullopt;
            HelperTimes times;
            times.copyInMs = timingMs[firstRowEffect - 1];
            times.rowsMs.assign(timingMs.begin() + static_cast<std::ptrdiff_t>(firstRowEffect),
                                timingMs.begin() + static_cast<std::ptrdiff_t>(firstRowEffect + rowNames.size()));
            times.copyOutMs = timingMs[firstRowEffect + rowNames.size()];
            return times;
        }
    };

    struct GpuChain
    {
        std::vector<VkSemaphore>  headDone;
        std::vector<ChainSegment> segments;
        bool                      failed = false;
        VkQueryPool               roundTrips = VK_NULL_HANDLE;
        std::vector<bool>         roundTripPending;
        float                     roundTripMilliseconds = -1.0f;
        uint64_t                  roundTripReads        = 0;

        std::vector<uint32_t> tripGpus() const;
        void     readRoundTrip(LogicalDevice& game, uint32_t index);
        void     readSegmentTimings(uint32_t index);
        void     markSegmentTimings(uint32_t index);
        void     destroy(LogicalDevice& game);
    };

    struct ChainPart
    {
        std::shared_ptr<HelperDevice> helper;
        uint32_t                      gpu      = 0;
        size_t                        firstRow = 0;
        size_t                        rows     = 0;
    };

    struct SegmentRows
    {
        std::vector<std::shared_ptr<Effect>> effects;
        std::vector<std::string>             names;
    };

    using SegmentEffectBuilder = std::function<SegmentRows(
        LogicalDevice* device, uint32_t gpu, size_t firstRow, size_t rows, const std::vector<VkImage>& slots, uint32_t imageCount)>;

    VkDeviceSize sharedBufferBytes(VkExtent2D extent, uint32_t texelBytes);

    std::unique_ptr<GpuChain> buildChain(LogicalDevice&                  game,
                                         const std::vector<ChainPart>&   parts,
                                         const VkSwapchainCreateInfoKHR& swapchainInfo,
                                         const std::vector<VkImage>&     swapchainImages,
                                         const SegmentEffectBuilder&     buildRows,
                                         bool                            timeHelpers);

    struct HandOffSide
    {
        const DeviceDispatch* vkd    = nullptr;
        VkDevice              device = VK_NULL_HANDLE;
        VkQueue               queue  = VK_NULL_HANDLE;
    };

    struct HandOff
    {
        bool        handedOver   = false;
        bool        headRan      = false;
        VkSemaphore fallbackWait = VK_NULL_HANDLE;
        std::string failure;
    };

    HandOff submitThroughChain(HandOffSide                     game,
                               const std::vector<HandOffSide>& segmentSides,
                               GpuChain&                       chain,
                               uint32_t                        index,
                               VkSubmitInfo                    head,
                               VkSemaphore                     presentSignal,
                               VkFence                         effectFence);
}

#endif
