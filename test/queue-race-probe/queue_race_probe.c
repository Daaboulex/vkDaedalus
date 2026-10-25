#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PROBE_SKIPPED = 77,
    SLOTS         = 2
};

struct busyQueue
{
    VkDevice    device;
    VkQueue     queue;
    atomic_bool stop;
    unsigned    submits;
    int         failed;
};

static int fail(const char* why)
{
    fprintf(stderr, "queue-race-probe: %s\n", why);
    return 1;
}

static void* keepQueueBusy(void* argument)
{
    struct busyQueue* busy = argument;
    VkFence           fence = VK_NULL_HANDLE;
    const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(busy->device, &fenceInfo, NULL, &fence) != VK_SUCCESS)
    {
        busy->failed = 1;
        return NULL;
    }
    while (!atomic_load(&busy->stop))
    {
        if (vkQueueSubmit(busy->queue, 0, NULL, fence) != VK_SUCCESS || vkWaitForFences(busy->device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS
            || vkResetFences(busy->device, 1, &fence) != VK_SUCCESS)
        {
            busy->failed = 1;
            break;
        }
        busy->submits++;
    }
    vkDestroyFence(busy->device, fence, NULL);
    return NULL;
}

int main(int argc, char** argv)
{
    char*               end    = NULL;
    const unsigned long frames = argc == 2 ? strtoul(argv[1], &end, 10) : 0;
    if (argc != 2 || !end || *end != '\0' || frames < 2 || frames > 100000)
        return fail("usage: queue-race-probe <presents, 2 to 100000>");

    xcb_connection_t* connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection))
        return fail("no X11 display");
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    const xcb_window_t  window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 320, 240, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, 0, NULL);
    xcb_map_window(connection, window);
    xcb_flush(connection);

    const char* const          instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo    app                  = {.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                                       .pApplicationName = "queue-race-probe",
                                                       .apiVersion       = VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instanceInfo         = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app,
        .enabledExtensionCount   = 2,
        .ppEnabledExtensionNames = instanceExtensions,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS)
        return fail("vkCreateInstance failed");
    const VkXcbSurfaceCreateInfoKHR surfaceInfo = {.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR, .connection = connection, .window = window};
    VkSurfaceKHR                    surface     = VK_NULL_HANDLE;
    if (vkCreateXcbSurfaceKHR(instance, &surfaceInfo, NULL, &surface) != VK_SUCCESS)
        return fail("vkCreateXcbSurfaceKHR failed");

    uint32_t         count    = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    const VkResult   listed   = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if ((listed != VK_SUCCESS && listed != VK_INCOMPLETE) || count == 0)
        return fail("no physical device");

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties* properties = calloc(families ? families : 1, sizeof(*properties));
    if (!properties)
        return fail("out of memory");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties);
    uint32_t family = families;
    for (uint32_t i = 0; i < families && family == families; i++)
    {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
        if (present && (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && properties[i].queueCount >= 3)
            family = i;
    }
    free(properties);
    if (family == families)
    {
        printf("queue-race-probe: no graphics queue family with three queues, two for it and one for the layer, presents to the surface\n");
        return PROBE_SKIPPED;
    }

    const float                   priorities[2] = {1.0f, 1.0f};
    const VkDeviceQueueCreateInfo queueInfo     = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount       = 2,
        .pQueuePriorities = priorities,
    };
    const char* const        deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo deviceInfo         = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueInfo,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = deviceExtensions,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, NULL, &device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");
    VkQueue busyQueue = VK_NULL_HANDLE, presentQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &busyQueue);
    vkGetDeviceQueue(device, family, 1, &presentQueue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return fail("the surface cannot be cleared by a transfer");
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, NULL);
    VkSurfaceFormatKHR* formats = calloc(formatCount ? formatCount : 1, sizeof(*formats));
    if (!formats)
        return fail("out of memory");
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats);
    const VkFormat format = formatCount ? formats[0].format : VK_FORMAT_UNDEFINED;
    free(formats);
    if (format == VK_FORMAT_UNDEFINED)
        return fail("the surface offers no format");
    uint32_t minImages = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && minImages > caps.maxImageCount)
        minImages = caps.maxImageCount;
    const VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = minImages,
        .imageFormat      = format,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
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
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL);
    VkImage*         images   = calloc(imageCount ? imageCount : 1, sizeof(*images));
    VkCommandBuffer* commands = calloc(imageCount ? imageCount : 1, sizeof(*commands));
    VkSemaphore*     rendered = calloc(imageCount ? imageCount : 1, sizeof(*rendered));
    VkFence*         imageFence = calloc(imageCount ? imageCount : 1, sizeof(*imageFence));
    if (!images || !commands || !rendered || !imageFence || vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images) != VK_SUCCESS)
        return fail("vkGetSwapchainImagesKHR failed");

    const VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool                 pool     = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, NULL, &pool) != VK_SUCCESS)
        return fail("vkCreateCommandPool failed");
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = imageCount};
    if (vkAllocateCommandBuffers(device, &allocate, commands) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers failed");
    const VkSemaphoreCreateInfo   semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkImageSubresourceRange range         = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (uint32_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &rendered[i]) != VK_SUCCESS)
            return fail("vkCreateSemaphore failed");
        const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(commands[i], &begin);
        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = images[i],
            .subresourceRange    = range,
        };
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        const VkClearColorValue colour = {.float32 = {0.2f, 0.4f, 0.6f, 1.0f}};
        vkCmdClearColorImage(commands[i], images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        if (vkEndCommandBuffer(commands[i]) != VK_SUCCESS)
            return fail("vkEndCommandBuffer failed");
    }
    VkSemaphore             acquired[SLOTS];
    VkFence                 inFlight[SLOTS];
    const VkFenceCreateInfo signalled = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int s = 0; s < SLOTS; s++)
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &acquired[s]) != VK_SUCCESS || vkCreateFence(device, &signalled, NULL, &inFlight[s]) != VK_SUCCESS)
            return fail("creating the frame semaphores and fences failed");

    struct busyQueue busy = {.device = device, .queue = busyQueue, .submits = 0, .failed = 0};
    atomic_init(&busy.stop, false);
    pthread_t worker;
    if (pthread_create(&worker, NULL, keepQueueBusy, &busy) != 0)
        return fail("the second thread could not start");

    int failed = 0;
    for (unsigned long f = 0; f < frames && !failed; f++)
    {
        const uint32_t slot  = (uint32_t)(f % SLOTS);
        uint32_t       index = 0;
        if (vkWaitForFences(device, 1, &inFlight[slot], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            failed = fail("vkWaitForFences failed");
        const VkResult acquiredResult = failed ? VK_ERROR_UNKNOWN : vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired[slot], VK_NULL_HANDLE, &index);
        if (!failed && acquiredResult != VK_SUCCESS && acquiredResult != VK_SUBOPTIMAL_KHR)
            failed = fail("vkAcquireNextImageKHR failed");
        if (failed)
            break;
        if (imageFence[index] != VK_NULL_HANDLE && vkWaitForFences(device, 1, &imageFence[index], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        {
            failed = fail("vkWaitForFences on the frame that last drew this image failed");
            break;
        }
        imageFence[index] = inFlight[slot];
        vkResetFences(device, 1, &inFlight[slot]);
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        const VkSubmitInfo         submit    = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &acquired[slot],
            .pWaitDstStageMask    = &waitStage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &commands[index],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &rendered[index],
        };
        if (vkQueueSubmit(presentQueue, 1, &submit, inFlight[slot]) != VK_SUCCESS)
            failed = fail("vkQueueSubmit on the present queue failed");
        const VkPresentInfoKHR present = {.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                          .waitSemaphoreCount = 1,
                                          .pWaitSemaphores    = &rendered[index],
                                          .swapchainCount     = 1,
                                          .pSwapchains        = &swapchain,
                                          .pImageIndices      = &index};
        const VkResult presented = failed ? VK_ERROR_UNKNOWN : vkQueuePresentKHR(presentQueue, &present);
        if (!failed && presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR)
            failed = fail("vkQueuePresentKHR failed");
    }
    atomic_store(&busy.stop, true);
    pthread_join(worker, NULL);
    if (busy.failed)
        failed = fail("the second thread's submits on queue 0 failed");
    if (vkDeviceWaitIdle(device) != VK_SUCCESS)
        failed = fail("vkDeviceWaitIdle failed");
    for (int s = 0; s < SLOTS; s++)
    {
        vkDestroyFence(device, inFlight[s], NULL);
        vkDestroySemaphore(device, acquired[s], NULL);
    }
    for (uint32_t i = 0; i < imageCount; i++)
        vkDestroySemaphore(device, rendered[i], NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    xcb_disconnect(connection);
    free(images);
    free(commands);
    free(rendered);
    free(imageFence);
    if (failed)
        return 1;
    printf("queue-race-probe: %lu presents on queue 1 while another thread made %u submits on queue 0\n", frames, busy.submits);
    return 0;
}
