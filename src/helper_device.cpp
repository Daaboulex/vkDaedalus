#include "helper_device.hpp"

#include <algorithm>
#include <cstring>
#include <optional>

#include <dlfcn.h>

#include "imgui_overlay.hpp"
#include "util.hpp"

namespace vkBasalt
{
    namespace
    {
        thread_local bool helperBeingMade = false;

        std::optional<uint32_t> graphicsFamily(const InstanceDispatch& vki, VkPhysicalDevice physical)
        {
            uint32_t count = 0;
            vki.GetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vki.GetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
            for (uint32_t i = 0; i < count; i++)
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && families[i].queueCount > 0)
                    return i;
            return std::nullopt;
        }

        bool sharesDmaBuf(const InstanceDispatch& vki, VkPhysicalDevice physical, VkExternalMemoryFeatureFlags wanted)
        {
            VkPhysicalDeviceExternalBufferInfo info{};
            info.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
            info.usage      = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkExternalBufferProperties properties{};
            properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
            vki.GetPhysicalDeviceExternalBufferProperties(physical, &info, &properties);
            return (properties.externalMemoryProperties.externalMemoryFeatures & wanted) == wanted;
        }

        bool sharesSyncFd(const InstanceDispatch& vki, VkPhysicalDevice physical)
        {
            VkPhysicalDeviceExternalSemaphoreInfo info{};
            info.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
            info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
            VkExternalSemaphoreProperties properties{};
            properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
            vki.GetPhysicalDeviceExternalSemaphoreProperties(physical, &info, &properties);
            const VkExternalSemaphoreFeatureFlags both = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
            return (properties.externalSemaphoreFeatures & both) == both;
        }

        PFN_vkGetInstanceProcAddr ownGetInstanceProcAddr()
        {
            Dl_info info{};
            if (!dladdr(reinterpret_cast<void*>(&makingHelperDevice), &info) || !info.dli_fname)
                return nullptr;
            void* self = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
            if (!self)
                return nullptr;
            const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(self, "vkDaedalus_GetInstanceProcAddr"));
            dlclose(self);
            return gipa;
        }
    }

    std::string addSharingExtensions(const std::vector<VkExtensionProperties>& offered, std::vector<const char*>& enabled)
    {
        const char* const sharing[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                                       VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
        for (const char* name : sharing)
            if (!offersExtension(offered, name))
                return name;
        for (const char* name : sharing)
            addUniqueCString(enabled, name);
        return {};
    }

    std::string helperPairProblem(const InstanceDispatch& vki, uint32_t instanceVersion, VkPhysicalDevice game, const InstanceGpu& helper)
    {
        const std::string name = helper.properties.deviceName;
        if (std::min(helper.properties.apiVersion, instanceVersion) < VK_API_VERSION_1_1)
            return name + " offers Vulkan below 1.1";
        const std::vector<VkExtensionProperties> offered = deviceExtensions(vki, helper.physical);
        if (!offersExtension(offered, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            return name + " lacks " VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        std::vector<const char*> unused;
        if (const std::string missing = addSharingExtensions(offered, unused); !missing.empty())
            return name + " lacks " + missing;
        if (!sharesDmaBuf(vki, game, VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT))
            return "the game's GPU cannot export a buffer as a dma-buf";
        if (!sharesDmaBuf(vki, helper.physical, VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
            return name + " cannot import a dma-buf buffer";
        if (!sharesSyncFd(vki, game))
            return "the game's GPU cannot hand over a SYNC_FD semaphore";
        if (!sharesSyncFd(vki, helper.physical))
            return name + " cannot hand over a SYNC_FD semaphore";
        if (!graphicsFamily(vki, helper.physical))
            return name + " has no graphics queue";
        return {};
    }

    HelperDevice::~HelperDevice()
    {
        if (device.device == VK_NULL_HANDLE || !destroy)
            return;
        device.vkd.DeviceWaitIdle(device.device);
        if (device.commandPool != VK_NULL_HANDLE)
            device.vkd.DestroyCommandPool(device.device, device.commandPool, nullptr);
        destroy(device.device, nullptr, device.vkd.DestroyDevice);
    }

    bool makingHelperDevice()
    {
        return helperBeingMade;
    }

    std::variant<std::shared_ptr<HelperDevice>, std::string> makeHelperDevice(const std::unique_lock<LayerLock>& released, const HelperRequest& request)
    {
        if (released.owns_lock())
            return std::string("it was asked for while the layer lock was held, which the loader's callback into the layer would deadlock on");
        const PFN_vkGetInstanceProcAddr layerGipa = ownGetInstanceProcAddr();
        if (!layerGipa)
            return std::string("the layer could not find its own vkGetInstanceProcAddr to hand the loader");
        const std::optional<uint32_t> family = graphicsFamily(request.vki, request.gpu.physical);
        if (!family)
            return std::string("it has no graphics queue");

        const std::vector<VkExtensionProperties> offered    = deviceExtensions(request.vki, request.gpu.physical);
        std::vector<const char*>                 extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (const std::string missing = addSharingExtensions(offered, extensions); !missing.empty())
            return "it lacks " + missing;
        if (std::min(request.gpu.properties.apiVersion, request.instanceVersion) < VK_API_VERSION_1_2
            && offersExtension(offered, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME))
            extensions.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);

        VkPhysicalDeviceFeatures supported{};
        request.vki.GetPhysicalDeviceFeatures(request.gpu.physical, &supported);
        VkPhysicalDeviceFeatures features{};
        features.shaderImageGatherExtended            = supported.shaderImageGatherExtended;
        features.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
        features.shaderStorageImageReadWithoutFormat  = supported.shaderStorageImageReadWithoutFormat;

        const float             priority = 1.0f;
        VkDeviceQueueCreateInfo queue{};
        queue.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = *family;
        queue.queueCount       = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo info{};
        info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount    = 1;
        info.pQueueCreateInfos       = &queue;
        info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.pEnabledFeatures        = &features;

        VkDevice                device   = VK_NULL_HANDLE;
        PFN_vkGetDeviceProcAddr nextGdpa = nullptr;
        helperBeingMade                  = true;
        const VkResult made              = request.create(request.instance, request.gpu.physical, &info, nullptr, &device, layerGipa, &nextGdpa);
        helperBeingMade                  = false;
        if (made != VK_SUCCESS)
            return "the loader's device callback returned " + std::to_string(made);
        if (!nextGdpa)
            return std::string("the loader made it without a next vkGetDeviceProcAddr, so the layer cannot reach it and leaves it to the loader");

        auto           helper = std::make_shared<HelperDevice>();
        LogicalDevice& d      = helper->device;
        helper->gpu           = request.number;
        helper->destroy       = request.destroy;
        d.isHelper            = true;
        d.vki                 = request.vki;
        d.device              = device;
        d.physicalDevice      = request.gpu.physical;
        d.instance            = request.instance;
        fillDispatchTableDevice(device, nextGdpa, &d.vkd);
        d.vkd.GetDeviceQueue(device, *family, 0, &d.queue);
        if (d.queue == VK_NULL_HANDLE)
            return std::string("it gave no queue");
        initializeDispatchTable(d.queue, device);
        d.queueFamilyIndex                  = *family;
        d.queueFamilies[d.queue]            = *family;
        d.limits                            = request.gpu.properties.limits;
        d.timestampPeriodNanoseconds        = request.gpu.properties.limits.timestampPeriod;
        {
            uint32_t familyCount = 0;
            request.vki.GetPhysicalDeviceQueueFamilyProperties(request.gpu.physical, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            request.vki.GetPhysicalDeviceQueueFamilyProperties(request.gpu.physical, &familyCount, families.data());
            d.timestampValidBits = *family < familyCount ? families[*family].timestampValidBits : 0;
        }
        d.supportsStorageImageWithoutFormat = features.shaderStorageImageWriteWithoutFormat && features.shaderStorageImageReadWithoutFormat;

        VkCommandPoolCreateInfo pool{};
        pool.sType               = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex    = *family;
        const VkResult poolMade  = d.vkd.CreateCommandPool(device, &pool, nullptr, &d.commandPool);
        if (poolMade != VK_SUCCESS)
        {
            d.commandPool = VK_NULL_HANDLE;
            return "its command pool could not be made (" + std::to_string(poolMade) + ")";
        }
        return helper;
    }
}
