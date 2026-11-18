#include "present_counter.hpp"

#include "vulkan_include.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "latency_stats.hpp"
#include "program_scope.hpp"

namespace
{
    struct CounterInstance
    {
        VkInstance                instance = VK_NULL_HANDLE;
        PFN_vkGetInstanceProcAddr next    = nullptr;
        PFN_vkDestroyInstance     destroy = nullptr;
    };

    struct CounterDevice
    {
        PFN_vkGetDeviceProcAddr  next    = nullptr;
        PFN_vkDestroyDevice      destroy = nullptr;
        PFN_vkQueuePresentKHR    present = nullptr;
        vkBasalt::PresentIntervals presents;
    };

    std::mutex                                                  counterLock;
    std::unordered_map<void*, CounterInstance>                  counterInstances;
    std::unordered_map<void*, std::unique_ptr<CounterDevice>>   counterDevices;

    template<typename Handle>
    void* keyOf(Handle handle)
    {
        return *reinterpret_cast<void**>(handle);
    }

    template<typename Info, typename LinkInfo>
    LinkInfo* linkOf(const Info* info, VkStructureType type)
    {
        auto* link = static_cast<LinkInfo*>(const_cast<void*>(info->pNext));
        while (link && (link->sType != type || link->function != VK_LAYER_LINK_INFO))
            link = static_cast<LinkInfo*>(const_cast<void*>(link->pNext));
        return link;
    }

    CounterDevice* deviceOf(void* key)
    {
        std::lock_guard<std::mutex> held(counterLock);
        const auto                  found = counterDevices.find(key);
        return found == counterDevices.end() ? nullptr : found->second.get();
    }

    VKAPI_ATTR VkResult VKAPI_CALL counterCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
    {
        auto* link = linkOf<VkInstanceCreateInfo, VkLayerInstanceCreateInfo>(pCreateInfo, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO);
        if (!link || !link->u.pLayerInfo)
            return VK_ERROR_INITIALIZATION_FAILED;
        const PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        link->u.pLayerInfo                   = link->u.pLayerInfo->pNext;
        const auto create                    = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!create)
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkResult result = create(pCreateInfo, pAllocator, pInstance);
        if (result != VK_SUCCESS)
            return result;
        std::lock_guard<std::mutex> held(counterLock);
        counterInstances[keyOf(*pInstance)] = {*pInstance, next, reinterpret_cast<PFN_vkDestroyInstance>(next(*pInstance, "vkDestroyInstance"))};
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL counterDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance)
            return;
        PFN_vkDestroyInstance destroy = nullptr;
        {
            std::lock_guard<std::mutex> held(counterLock);
            const auto                  found = counterInstances.find(keyOf(instance));
            if (found == counterInstances.end())
                return;
            destroy = found->second.destroy;
            counterInstances.erase(found);
        }
        destroy(instance, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL counterCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
    {
        auto* link = linkOf<VkDeviceCreateInfo, VkLayerDeviceCreateInfo>(pCreateInfo, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO);
        if (!link || !link->u.pLayerInfo)
            return VK_ERROR_INITIALIZATION_FAILED;
        const PFN_vkGetInstanceProcAddr nextInstance = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        const PFN_vkGetDeviceProcAddr   nextDevice   = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        link->u.pLayerInfo                           = link->u.pLayerInfo->pNext;
        VkInstance instance                          = VK_NULL_HANDLE;
        {
            std::lock_guard<std::mutex> held(counterLock);
            const auto                  found = counterInstances.find(keyOf(physicalDevice));
            if (found != counterInstances.end())
                instance = found->second.instance;
        }
        if (instance == VK_NULL_HANDLE)
            return VK_ERROR_INITIALIZATION_FAILED;
        const auto create = reinterpret_cast<PFN_vkCreateDevice>(nextInstance(instance, "vkCreateDevice"));
        if (!create)
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkResult result = create(physicalDevice, pCreateInfo, pAllocator, pDevice);
        if (result != VK_SUCCESS)
            return result;
        auto device     = std::make_unique<CounterDevice>();
        device->next    = nextDevice;
        device->destroy = reinterpret_cast<PFN_vkDestroyDevice>(nextDevice(*pDevice, "vkDestroyDevice"));
        device->present = reinterpret_cast<PFN_vkQueuePresentKHR>(nextDevice(*pDevice, "vkQueuePresentKHR"));
        std::lock_guard<std::mutex> held(counterLock);
        counterDevices[keyOf(*pDevice)] = std::move(device);
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL counterDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device)
            return;
        std::unique_ptr<CounterDevice> gone;
        {
            std::lock_guard<std::mutex> held(counterLock);
            const auto                  found = counterDevices.find(keyOf(device));
            if (found == counterDevices.end())
                return;
            gone = std::move(found->second);
            counterDevices.erase(found);
        }
        gone->destroy(device, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL counterQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        const auto            at      = std::chrono::steady_clock::now();
        PFN_vkQueuePresentKHR present = nullptr;
        {
            std::lock_guard<std::mutex> held(counterLock);
            CounterDevice&              device = *counterDevices.at(keyOf(queue));
            device.presents.record(at);
            present = device.present;
        }
        return present(queue, pPresentInfo);
    }
}

namespace vkBasalt
{
    std::optional<std::vector<double>> presentsBelowFrameGeneration(VkDevice device, std::chrono::steady_clock::time_point now, std::chrono::milliseconds span)
    {
        std::lock_guard<std::mutex> held(counterLock);
        const auto                  found = counterDevices.find(keyOf(device));
        if (found == counterDevices.end() || !found->second->present)
            return std::nullopt;
        return found->second->presents.within(now, span);
    }

    std::optional<PresentTotals> presentTotalsBelowFrameGeneration(VkDevice device)
    {
        std::lock_guard<std::mutex> held(counterLock);
        const auto                  found = counterDevices.find(keyOf(device));
        if (found == counterDevices.end() || !found->second->present)
            return std::nullopt;
        return PresentTotals{found->second->presents.intervalCount(), found->second->presents.intervalSumMs()};
    }
}

extern "C"
{
    __attribute__((visibility("default"))) PFN_vkVoidFunction VKAPI_CALL vkDaedalusCounter_GetDeviceProcAddr(VkDevice device, const char* pName);
    __attribute__((visibility("default"))) PFN_vkVoidFunction VKAPI_CALL vkDaedalusCounter_GetInstanceProcAddr(VkInstance instance, const char* pName);

    PFN_vkVoidFunction VKAPI_CALL vkDaedalusCounter_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        if (!std::strcmp(pName, "vkGetDeviceProcAddr"))
            return reinterpret_cast<PFN_vkVoidFunction>(&vkDaedalusCounter_GetDeviceProcAddr);
        if (!std::strcmp(pName, "vkDestroyDevice"))
            return reinterpret_cast<PFN_vkVoidFunction>(&counterDestroyDevice);
        CounterDevice* counted = deviceOf(keyOf(device));
        if (!counted)
            return nullptr;
        if (!std::strcmp(pName, "vkQueuePresentKHR") && counted->present && !vkBasalt::programBypassed())
            return reinterpret_cast<PFN_vkVoidFunction>(&counterQueuePresentKHR);
        return counted->next(device, pName);
    }

    PFN_vkVoidFunction VKAPI_CALL vkDaedalusCounter_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        if (!std::strcmp(pName, "vkGetInstanceProcAddr"))
            return reinterpret_cast<PFN_vkVoidFunction>(&vkDaedalusCounter_GetInstanceProcAddr);
        if (!std::strcmp(pName, "vkCreateInstance"))
            return reinterpret_cast<PFN_vkVoidFunction>(&counterCreateInstance);
        if (!std::strcmp(pName, "vkDestroyInstance"))
            return reinterpret_cast<PFN_vkVoidFunction>(&counterDestroyInstance);
        if (!std::strcmp(pName, "vkCreateDevice"))
            return reinterpret_cast<PFN_vkVoidFunction>(&counterCreateDevice);
        if (!std::strcmp(pName, "vkGetDeviceProcAddr"))
            return reinterpret_cast<PFN_vkVoidFunction>(&vkDaedalusCounter_GetDeviceProcAddr);
        if (!std::strcmp(pName, "vkDestroyDevice"))
            return reinterpret_cast<PFN_vkVoidFunction>(&counterDestroyDevice);
        if (instance == VK_NULL_HANDLE)
            return nullptr;
        PFN_vkGetInstanceProcAddr next = nullptr;
        {
            std::lock_guard<std::mutex> held(counterLock);
            const auto                  found = counterInstances.find(keyOf(instance));
            if (found == counterInstances.end())
                return nullptr;
            next = found->second.next;
        }
        return next(instance, pName);
    }
}
