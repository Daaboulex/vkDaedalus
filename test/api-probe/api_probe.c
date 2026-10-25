#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t live_allocations = 0;

static size_t aligned_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

static void* VKAPI_PTR probe_alloc(void* user, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    (void) user;
    (void) scope;
    const size_t align = alignment < 2 * sizeof(void*) ? 2 * sizeof(void*) : alignment;
    char*        raw   = aligned_alloc(align, aligned_up(align + size, align));
    if (!raw)
        return NULL;
    char* block               = raw + align;
    ((size_t*) block)[-1]     = size;
    ((void**) block)[-2]      = raw;
    live_allocations++;
    return block;
}

static void VKAPI_PTR probe_free(void* user, void* block)
{
    (void) user;
    if (!block)
        return;
    live_allocations--;
    free(((void**) block)[-2]);
}

static void* VKAPI_PTR probe_realloc(void* user, void* block, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    if (!block)
        return probe_alloc(user, size, alignment, scope);
    if (size == 0)
    {
        probe_free(user, block);
        return NULL;
    }
    void* moved = probe_alloc(user, size, alignment, scope);
    if (!moved)
        return NULL;
    const size_t old = ((size_t*) block)[-1];
    memcpy(moved, block, old < size ? old : size);
    probe_free(user, block);
    return moved;
}

int main(void)
{
    const VkAllocationCallbacks callbacks = {NULL, probe_alloc, probe_realloc, probe_free, NULL, NULL};

    const VkApplicationInfo app = {
        .sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "api-probe",
        .apiVersion = VK_API_VERSION_1_1,
    };
    const char* const          surfaceExtension = VK_KHR_SURFACE_EXTENSION_NAME;
    const VkInstanceCreateInfo instanceInfo     = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = &surfaceExtension,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, &callbacks, &instance) != VK_SUCCESS)
    {
        fprintf(stderr, "api-probe: vkCreateInstance failed\n");
        return 1;
    }

    uint32_t         count    = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    const VkResult   listed   = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if ((listed != VK_SUCCESS && listed != VK_INCOMPLETE) || count == 0)
    {
        fprintf(stderr, "api-probe: no physical device\n");
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties* properties = calloc(families, sizeof(*properties));
    if (!properties)
    {
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties);
    uint32_t graphics = families;
    for (uint32_t i = 0; i < families; i++)
    {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphics = i;
            break;
        }
    }
    free(properties);
    if (graphics == families)
    {
        fprintf(stderr, "api-probe: no graphics queue family\n");
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }

    const float                   priority  = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(physical, &supported);
    if (!supported.shaderImageGatherExtended && !supported.shaderStorageImageWriteWithoutFormat
        && !supported.shaderStorageImageReadWithoutFormat)
    {
        fprintf(stderr, "api-probe: the device supports none of the features the layer requests, so nothing shows whether it writes into the application's features\n");
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }
    VkPhysicalDeviceFeatures2 requested = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

    const char* const        swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    const VkDeviceCreateInfo deviceInfo         = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &requested,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueInfo,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = &swapchainExtension,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, &callbacks, &device) != VK_SUCCESS)
    {
        fprintf(stderr, "api-probe: vkCreateDevice failed\n");
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }
    if (requested.features.shaderImageGatherExtended || requested.features.shaderStorageImageWriteWithoutFormat
        || requested.features.shaderStorageImageReadWithoutFormat)
    {
        fprintf(stderr, "api-probe: the layer wrote features into the application's own VkPhysicalDeviceFeatures2\n");
        vkDestroyDevice(device, &callbacks);
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }

    const PFN_vkDestroySwapchainKHR destroySwapchain =
        (PFN_vkDestroySwapchainKHR) vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
    if (!destroySwapchain)
    {
        fprintf(stderr, "api-probe: no vkDestroySwapchainKHR\n");
        vkDestroyDevice(device, &callbacks);
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }
    destroySwapchain(device, VK_NULL_HANDLE, &callbacks);
    vkDestroyImage(device, VK_NULL_HANDLE, &callbacks);

    const VkImageCreateInfo transientDepth = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {64, 64, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage depth = VK_NULL_HANDLE;
    if (vkCreateImage(device, &transientDepth, &callbacks, &depth) != VK_SUCCESS)
    {
        fprintf(stderr, "api-probe: a transient depth attachment could not be created\n");
        vkDestroyDevice(device, &callbacks);
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }
    vkDestroyImage(device, depth, &callbacks);

    VkImageCreateInfo boundDepthInfo = transientDepth;
    boundDepthInfo.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImage boundDepth               = VK_NULL_HANDLE;
    VkDeviceMemory boundMemory       = VK_NULL_HANDLE;
    VkMemoryRequirements requirements;
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t memoryType = memory.memoryTypeCount;
    if (vkCreateImage(device, &boundDepthInfo, &callbacks, &boundDepth) == VK_SUCCESS)
    {
        vkGetImageMemoryRequirements(device, boundDepth, &requirements);
        for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
        {
            if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memoryType = i;
                break;
            }
        }
    }
    VkResult bound = VK_ERROR_INITIALIZATION_FAILED;
    if (boundDepth && memoryType != memory.memoryTypeCount)
    {
        const VkMemoryAllocateInfo allocation = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = requirements.size,
            .memoryTypeIndex = memoryType,
        };
        bound = vkAllocateMemory(device, &allocation, &callbacks, &boundMemory);
    }
    if (bound == VK_SUCCESS)
    {
        const VkBindImageMemoryInfo bind = {
            .sType  = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
            .image  = boundDepth,
            .memory = boundMemory,
        };
        bound = vkBindImageMemory2(device, 1, &bind);
    }
    if (bound != VK_SUCCESS)
    {
        fprintf(stderr, "api-probe: a depth image could not be bound with vkBindImageMemory2\n");
        vkDestroyImage(device, boundDepth, &callbacks);
        vkFreeMemory(device, boundMemory, &callbacks);
        vkDestroyDevice(device, &callbacks);
        vkDestroyInstance(instance, &callbacks);
        return 1;
    }
    vkDestroyImage(device, boundDepth, &callbacks);
    vkFreeMemory(device, boundMemory, &callbacks);

    vkDestroyDevice(device, &callbacks);
    vkDestroyInstance(instance, &callbacks);
    printf("api-probe: null swapchain and image destroyed, a transient depth attachment made, a depth image bound with vkBindImageMemory2, the application's features left as given, device and instance made and destroyed with application "
           "allocation callbacks, %zu blocks left\n",
           live_allocations);
    return live_allocations == 0 ? 0 : 1;
}
