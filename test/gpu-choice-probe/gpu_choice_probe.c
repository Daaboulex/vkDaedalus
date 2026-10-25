#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_DEVICES = 16
};

struct listing
{
    uint32_t count;
    uint32_t vendor[MAX_DEVICES];
    uint32_t device[MAX_DEVICES];
    uint32_t type[MAX_DEVICES];
};

static int fail(const char* why)
{
    fprintf(stderr, "gpu-choice-probe: %s\n", why);
    return 1;
}

static int list_devices(VkInstance instance, VkPhysicalDevice* physicals, struct listing* out, const char* when)
{
    uint32_t count = MAX_DEVICES;
    const VkResult listed = vkEnumeratePhysicalDevices(instance, &count, physicals);
    if (listed != VK_SUCCESS && listed != VK_INCOMPLETE)
        return fail("vkEnumeratePhysicalDevices failed");
    out->count = count;
    for (uint32_t i = 0; i < count; i++)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicals[i], &properties);
        out->vendor[i] = properties.vendorID;
        out->device[i] = properties.deviceID;
        out->type[i]   = properties.deviceType;
        printf("gpu-choice-probe: %s device %u: %04x:%04x type %u %s\n", when, i, properties.vendorID, properties.deviceID, properties.deviceType,
               properties.deviceName);
    }
    return 0;
}

static uint32_t default_pick(const struct listing* l)
{
    for (uint32_t i = 0; i < l->count; i++)
        if (l->type[i] == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            return i;
    return 0;
}

static int present_on(VkInstance instance, VkPhysicalDevice physical, unsigned long presents)
{
    xcb_connection_t* connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection))
        return fail("no X11 display");
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    const xcb_window_t  window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 320, 240, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      0, NULL);
    xcb_map_window(connection, window);
    xcb_flush(connection);

    const VkXcbSurfaceCreateInfoKHR surfaceInfo = {.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR, .connection = connection, .window = window};
    VkSurfaceKHR                    surface     = VK_NULL_HANDLE;
    if (vkCreateXcbSurfaceKHR(instance, &surfaceInfo, NULL, &surface) != VK_SUCCESS)
        return fail("vkCreateXcbSurfaceKHR failed");

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    uint32_t family = families;
    for (uint32_t i = 0; i < families && family == families; i++)
    {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &supported);
        if (supported)
            family = i;
    }
    if (family == families)
        return fail("the chosen GPU cannot present to this window");

    const float                   priority  = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char* const        extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo deviceInfo   = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueInfo,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = extensions,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, NULL, &device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps) != VK_SUCCESS)
        return fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return fail("the surface cannot be cleared by a transfer");
    uint32_t formatCount = 1;
    VkSurfaceFormatKHR format;
    const VkResult     formats = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, &format);
    if ((formats != VK_SUCCESS && formats != VK_INCOMPLETE) || formatCount == 0)
        return fail("the surface offers no format");
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && images > caps.maxImageCount)
        images = caps.maxImageCount;
    const VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = images,
        .imageFormat      = format.format,
        .imageColorSpace  = format.colorSpace,
        .imageExtent      = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : (VkExtent2D){320, 240},
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &swapchainInfo, NULL, &swapchain) != VK_SUCCESS)
        return fail("vkCreateSwapchainKHR failed");

    uint32_t imageCount = 0;
    if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL) != VK_SUCCESS || imageCount == 0 || imageCount > 16)
        return fail("vkGetSwapchainImagesKHR gave no usable image count");
    VkImage swapImages[16];
    if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapImages) != VK_SUCCESS)
        return fail("vkGetSwapchainImagesKHR failed");
    const VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool                 pool     = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, NULL, &pool) != VK_SUCCESS)
        return fail("vkCreateCommandPool failed");
    VkCommandBuffer                   clears[16];
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = imageCount};
    if (vkAllocateCommandBuffers(device, &allocate, clears) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers failed");
    for (uint32_t i = 0; i < imageCount; i++)
    {
        const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(clears[i], &begin) != VK_SUCCESS)
            return fail("vkBeginCommandBuffer failed");
        const VkImageSubresourceRange range   = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier          barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapImages[i],
            .subresourceRange    = range,
        };
        vkCmdPipelineBarrier(clears[i], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        const VkClearColorValue colour = {.float32 = {0.25f, 0.5f, 0.75f, 1.0f}};
        vkCmdClearColorImage(clears[i], swapImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(clears[i], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        if (vkEndCommandBuffer(clears[i]) != VK_SUCCESS)
            return fail("vkEndCommandBuffer failed");
    }

    const VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo     fenceInfo     = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkSemaphore                 acquired      = VK_NULL_HANDLE;
    VkSemaphore                 rendered[16];
    VkFence                     done = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &acquired) != VK_SUCCESS || vkCreateFence(device, &fenceInfo, NULL, &done) != VK_SUCCESS)
        return fail("creating the probe's semaphores or fence failed");
    for (uint32_t i = 0; i < imageCount; i++)
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &rendered[i]) != VK_SUCCESS)
            return fail("creating the probe's semaphores or fence failed");
    for (unsigned long p = 0; p < presents; p++)
    {
        uint32_t index = 0;
        if (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index) < 0)
            return fail("vkAcquireNextImageKHR failed");
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        const VkSubmitInfo         submit    = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &acquired,
            .pWaitDstStageMask    = &waitStage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &clears[index],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &rendered[index],
        };
        if (vkQueueSubmit(queue, 1, &submit, done) != VK_SUCCESS)
            return fail("vkQueueSubmit failed");
        const VkPresentInfoKHR present = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &rendered[index],
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain,
            .pImageIndices      = &index,
        };
        const VkResult         shown   = vkQueuePresentKHR(queue, &present);
        if (shown != VK_SUCCESS && shown != VK_SUBOPTIMAL_KHR)
            return fail("vkQueuePresentKHR failed");
        if (vkWaitForFences(device, 1, &done, VK_TRUE, UINT64_MAX) != VK_SUCCESS || vkResetFences(device, 1, &done) != VK_SUCCESS)
            return fail("waiting for a frame failed");
    }
    vkDeviceWaitIdle(device);
    vkDestroyFence(device, done, NULL);
    vkDestroySemaphore(device, acquired, NULL);
    for (uint32_t i = 0; i < imageCount; i++)
        vkDestroySemaphore(device, rendered[i], NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    xcb_disconnect(connection);
    return 0;
}

int main(int argc, char** argv)
{
    char*               end      = NULL;
    const unsigned long presents = argc >= 2 ? strtoul(argv[1], &end, 10) : 0;
    if ((argc != 2 && argc != 3) || !end || *end != '\0' || presents < 1 || presents > 100000)
        return fail("usage: gpu-choice-probe <presents> [vendor:device of the GPU the game picks]");

    const char* const          extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo    app          = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "gpu-choice-probe", .apiVersion = VK_API_VERSION_1_1};
    const VkInstanceCreateInfo info         = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS)
        return fail("vkCreateInstance failed");

    VkPhysicalDevice physicals[MAX_DEVICES];
    struct listing   before, after;
    if (list_devices(instance, physicals, &before, "before") != 0)
        return 1;
    if (before.count == 0)
        return fail("no physical device");
    uint32_t picked = default_pick(&before);
    if (argc == 3)
    {
        char*               sep    = NULL;
        const unsigned long vendor = strtoul(argv[2], &sep, 16);
        const unsigned long device = sep && *sep == ':' ? strtoul(sep + 1, NULL, 16) : 0;
        picked = before.count;
        for (uint32_t i = 0; i < before.count && picked == before.count; i++)
            if (before.vendor[i] == vendor && before.device[i] == device)
                picked = i;
        if (picked == before.count)
            return fail("the GPU named is not in this Vulkan instance");
    }
    printf("gpu-choice-probe: default pick %u, game picks %u: %04x:%04x\n", default_pick(&before), picked, before.vendor[picked], before.device[picked]);
    if (present_on(instance, physicals[picked], presents) != 0)
        return 1;
    if (list_devices(instance, physicals, &after, "after") != 0)
        return 1;
    if (after.count != before.count || memcmp(after.vendor, before.vendor, before.count * sizeof(*before.vendor)) != 0
        || memcmp(after.device, before.device, before.count * sizeof(*before.device)) != 0)
        return fail("the device list changed while the game ran");
    vkDestroyInstance(instance, NULL);
    printf("gpu-choice-probe: %lu presents on %04x:%04x, the device list the same before and after\n", presents, before.vendor[picked],
           before.device[picked]);
    return 0;
}
