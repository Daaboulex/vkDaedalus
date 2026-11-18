#ifndef INSTANCE_GPUS_HPP_INCLUDED
#define INSTANCE_GPUS_HPP_INCLUDED

#include <vector>

#include "vulkan_include.hpp"
#include "vkdispatch.hpp"
#include "gpu_assignment.hpp"
#include "mako_gpu.hpp"

namespace vkBasalt
{
    struct InstanceGpu
    {
        VkPhysicalDevice           physical = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties properties{};
        GpuCandidate               id;
    };

    std::vector<VkExtensionProperties> deviceExtensions(const InstanceDispatch& vki, VkPhysicalDevice physical);

    bool offersExtension(const std::vector<VkExtensionProperties>& offered, const char* name);

    std::vector<InstanceGpu> listInstanceGpus(const InstanceDispatch& vki, VkInstance instance);

    std::vector<NumberedGpu> numberedInstanceGpus(const std::vector<InstanceGpu>& gpus, VkPhysicalDevice game, const std::vector<GpuListEntry>& list);

    MakoGpus makoGpusOf(const std::vector<InstanceGpu>& gpus, VkPhysicalDevice game, const std::vector<GpuListEntry>& list);
}

#endif
