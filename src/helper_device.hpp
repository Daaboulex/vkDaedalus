#ifndef HELPER_DEVICE_HPP_INCLUDED
#define HELPER_DEVICE_HPP_INCLUDED

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "vulkan_include.hpp"
#include "gpu_assignment.hpp"
#include "instance_gpus.hpp"
#include "layer_lock.hpp"
#include "logical_device.hpp"

namespace vkBasalt
{
    std::string addSharingExtensions(const std::vector<VkExtensionProperties>& offered, std::vector<const char*>& enabled);

    std::string helperPairProblem(const InstanceDispatch& vki, uint32_t instanceVersion, VkPhysicalDevice game, const InstanceGpu& helper);

    struct HelperDevice
    {
        LogicalDevice            device;
        uint32_t                 gpu     = 0;
        PFN_vkLayerDestroyDevice destroy = nullptr;

        HelperDevice()                               = default;
        HelperDevice(const HelperDevice&)            = delete;
        HelperDevice& operator=(const HelperDevice&) = delete;
        ~HelperDevice();
    };

    struct DeviceOffload
    {
        PFN_vkLayerCreateDevice                                   create  = nullptr;
        PFN_vkLayerDestroyDevice                                  destroy = nullptr;
        std::map<VkPhysicalDevice, std::shared_ptr<HelperDevice>> helpers;
        std::map<VkPhysicalDevice, std::string>                   notMade;
        std::set<std::string>                                     reported;
        bool                                                      making = false;
        bool                                                      onGameGpuUntilReload = false;
        std::string                                               lastRefusal;
        std::vector<InstanceGpu>                                  instanceGpus;
    };

    struct HelperRequest
    {
        InstanceDispatch         vki;
        VkInstance               instance        = VK_NULL_HANDLE;
        uint32_t                 instanceVersion = 0;
        PFN_vkLayerCreateDevice  create          = nullptr;
        PFN_vkLayerDestroyDevice destroy         = nullptr;
        InstanceGpu              gpu;
        uint32_t                 number = 0;
    };

    bool makingHelperDevice();

    std::variant<std::shared_ptr<HelperDevice>, std::string> makeHelperDevice(const std::unique_lock<LayerLock>& released, const HelperRequest& request);
}

#endif
