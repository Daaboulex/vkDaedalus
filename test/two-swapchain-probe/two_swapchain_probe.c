#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PROBE_SKIPPED = 77,
    CHAINS        = 2,
    SLOTS         = 2
};

struct chain
{
    xcb_window_t    window;
    VkSurfaceKHR    surface;
    VkSwapchainKHR  swapchain;
    VkFormat        format;
    uint32_t        imageCount;
    VkImage*        images;
    VkCommandBuffer* commands;
    VkSemaphore*    rendered;
    VkFence*        imageFence;
    VkSemaphore     acquired[SLOTS];
    VkFence         inFlight[SLOTS];
    uint32_t        presented;
};

static int fail(const char* why)
{
    fprintf(stderr, "two-swapchain-probe: %s\n", why);
    return 1;
}

static int offers(const VkSurfaceFormatKHR* formats, uint32_t count, VkFormat format)
{
    for (uint32_t i = 0; i < count; i++)
        if (formats[i].format == format && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return 1;
    return 0;
}

static int pickFormats(VkPhysicalDevice physical, const struct chain* chains, VkFormat* unorm, VkFormat* srgb)
{
    const VkFormat pairs[][2] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB},
    };
    VkSurfaceFormatKHR* lists[CHAINS] = {NULL, NULL};
    uint32_t            counts[CHAINS] = {0, 0};
    int                 found          = 0;
    for (int c = 0; c < CHAINS; c++)
    {
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, chains[c].surface, &counts[c], NULL);
        lists[c] = calloc(counts[c] ? counts[c] : 1, sizeof(*lists[c]));
        if (!lists[c])
        {
            free(lists[0]);
            return -1;
        }
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, chains[c].surface, &counts[c], lists[c]);
    }
    for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]) && !found; p++)
    {
        if (offers(lists[0], counts[0], pairs[p][0]) && offers(lists[1], counts[1], pairs[p][1]))
        {
            *unorm = pairs[p][0];
            *srgb  = pairs[p][1];
            found  = 1;
        }
    }
    free(lists[0]);
    free(lists[1]);
    return found;
}

static int recordClear(VkCommandBuffer cmd, VkImage image, float red)
{
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        return fail("vkBeginCommandBuffer failed");
    const VkImageSubresourceRange range   = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier          barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = range,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    const VkClearColorValue colour = {.float32 = {red, 0.25f, 0.5f, 1.0f}};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        return fail("vkEndCommandBuffer failed");
    return 0;
}

static int makeChain(VkPhysicalDevice physical, VkDevice device, VkCommandPool pool, struct chain* c, float red)
{
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, c->surface, &caps) != VK_SUCCESS)
        return fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return fail("the surface cannot be cleared by a transfer");
    uint32_t minImages = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && minImages > caps.maxImageCount)
        minImages = caps.maxImageCount;
    const VkCompositeAlphaFlagBitsKHR alpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                                  ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                  : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    const VkSwapchainCreateInfoKHR info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = c->surface,
        .minImageCount    = minImages,
        .imageFormat      = c->format,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent      = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : (VkExtent2D){320, 240},
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = alpha,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    };
    if (vkCreateSwapchainKHR(device, &info, NULL, &c->swapchain) != VK_SUCCESS)
        return fail("vkCreateSwapchainKHR failed");
    if (vkGetSwapchainImagesKHR(device, c->swapchain, &c->imageCount, NULL) != VK_SUCCESS || c->imageCount == 0)
        return fail("vkGetSwapchainImagesKHR gave no images");
    c->images   = calloc(c->imageCount, sizeof(*c->images));
    c->commands = calloc(c->imageCount, sizeof(*c->commands));
    c->rendered = calloc(c->imageCount, sizeof(*c->rendered));
    c->imageFence = calloc(c->imageCount, sizeof(*c->imageFence));
    if (!c->images || !c->commands || !c->rendered || !c->imageFence)
        return fail("out of memory");
    if (vkGetSwapchainImagesKHR(device, c->swapchain, &c->imageCount, c->images) != VK_SUCCESS)
        return fail("vkGetSwapchainImagesKHR failed");
    const VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = c->imageCount,
    };
    if (vkAllocateCommandBuffers(device, &allocate, c->commands) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers failed");
    const VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < c->imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &c->rendered[i]) != VK_SUCCESS)
            return fail("vkCreateSemaphore failed");
        if (recordClear(c->commands[i], c->images[i], red) != 0)
            return 1;
    }
    const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int s = 0; s < SLOTS; s++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &c->acquired[s]) != VK_SUCCESS)
            return fail("vkCreateSemaphore failed");
        if (vkCreateFence(device, &fenceInfo, NULL, &c->inFlight[s]) != VK_SUCCESS)
            return fail("vkCreateFence failed");
    }
    return 0;
}

static int presentOnce(VkDevice device, VkQueue queue, struct chain* c)
{
    const uint32_t slot = c->presented % SLOTS;
    if (vkWaitForFences(device, 1, &c->inFlight[slot], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return fail("vkWaitForFences failed");
    uint32_t       index    = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device, c->swapchain, UINT64_MAX, c->acquired[slot], VK_NULL_HANDLE, &index);
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
        return fail("vkAcquireNextImageKHR failed");
    if (c->imageFence[index] != VK_NULL_HANDLE && vkWaitForFences(device, 1, &c->imageFence[index], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return fail("vkWaitForFences on the frame that last drew this image failed");
    c->imageFence[index] = c->inFlight[slot];
    if (vkResetFences(device, 1, &c->inFlight[slot]) != VK_SUCCESS)
        return fail("vkResetFences failed");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo         submit    = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &c->acquired[slot],
        .pWaitDstStageMask    = &waitStage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &c->commands[index],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &c->rendered[index],
    };
    if (vkQueueSubmit(queue, 1, &submit, c->inFlight[slot]) != VK_SUCCESS)
        return fail("vkQueueSubmit failed");
    const VkPresentInfoKHR present = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &c->rendered[index],
        .swapchainCount     = 1,
        .pSwapchains        = &c->swapchain,
        .pImageIndices      = &index,
    };
    const VkResult presented = vkQueuePresentKHR(queue, &present);
    if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR)
        return fail("vkQueuePresentKHR failed");
    c->presented++;
    return 0;
}

struct depthImage
{
    VkImage        image;
    VkDeviceMemory memory;
};

static int createDepth(VkDevice device, struct depthImage* d)
{
    const VkImageCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {64, 64, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &info, NULL, &d->image) != VK_SUCCESS)
        return fail("vkCreateImage for a depth image failed");
    return 0;
}

static int bindDepth(VkPhysicalDevice physical, VkDevice device, VkQueue queue, VkCommandPool pool, struct depthImage* d)
{
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, d->image, &requirements);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t type = memory.memoryTypeCount;
    for (uint32_t i = 0; i < memory.memoryTypeCount && type == memory.memoryTypeCount; i++)
        if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            type = i;
    if (type == memory.memoryTypeCount)
        return fail("no device-local memory type takes a depth image");
    const VkMemoryAllocateInfo allocation = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = type,
    };
    if (vkAllocateMemory(device, &allocation, NULL, &d->memory) != VK_SUCCESS)
        return fail("vkAllocateMemory for a depth image failed");
    if (vkBindImageMemory(device, d->image, d->memory, 0) != VK_SUCCESS)
        return fail("vkBindImageMemory for a depth image failed");

    const VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocate, &cmd) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers for the depth layout failed");
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        return fail("vkBeginCommandBuffer for the depth layout failed");
    const VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = d->image,
        .subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        return fail("vkEndCommandBuffer for the depth layout failed");
    const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence                 done      = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fenceInfo, NULL, &done) != VK_SUCCESS)
        return fail("vkCreateFence for the depth layout failed");
    const VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    if (vkQueueSubmit(queue, 1, &submit, done) != VK_SUCCESS)
        return fail("vkQueueSubmit of the depth layout failed");
    if (vkWaitForFences(device, 1, &done, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return fail("vkWaitForFences on the depth layout failed");
    vkDestroyFence(device, done, NULL);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    return 0;
}

static void destroyDepth(VkDevice device, struct depthImage* d, const char* what)
{
    fprintf(stderr, "two-swapchain-probe: destroying %s\n", what);
    vkDestroyImage(device, d->image, NULL);
    fprintf(stderr, "two-swapchain-probe: destroyed it\n");
    vkFreeMemory(device, d->memory, NULL);
}

static int presentRange(VkDevice device, VkQueue queue, struct chain* chains, unsigned long from, unsigned long to)
{
    for (unsigned long f = from; f < to; f++)
        if (presentOnce(device, queue, &chains[f % CHAINS]) != 0)
            return 1;
    return 0;
}

static void destroyChain(VkInstance instance, VkDevice device, struct chain* c)
{
    for (int s = 0; s < SLOTS; s++)
    {
        vkDestroyFence(device, c->inFlight[s], NULL);
        vkDestroySemaphore(device, c->acquired[s], NULL);
    }
    for (uint32_t i = 0; c->rendered && i < c->imageCount; i++)
        vkDestroySemaphore(device, c->rendered[i], NULL);
    vkDestroySwapchainKHR(device, c->swapchain, NULL);
    vkDestroySurfaceKHR(instance, c->surface, NULL);
    free(c->images);
    free(c->commands);
    free(c->rendered);
    free(c->imageFence);
}

int main(int argc, char** argv)
{
    char*               end    = NULL;
    const int           depth  = argc == 3 && strcmp(argv[2], "depth") == 0;
    const unsigned long frames = argc == 2 || depth ? strtoul(argv[1], &end, 10) : 0;
    if ((argc != 2 && !depth) || !end || *end != '\0' || frames < 8 || frames > 100000)
        return fail("usage: two-swapchain-probe <presents, 8 to 100000> [depth]");

    xcb_connection_t* connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection))
        return fail("no X11 display");
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;

    struct chain chains[CHAINS];
    memset(chains, 0, sizeof(chains));
    for (int c = 0; c < CHAINS; c++)
    {
        chains[c].window = xcb_generate_id(connection);
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, chains[c].window, screen->root, (int16_t)(c * 340), 0, 320, 240, 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
        xcb_map_window(connection, chains[c].window);
    }
    xcb_flush(connection);

    const char* const          instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo    app                  = {.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                                       .pApplicationName = "two-swapchain-probe",
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

    for (int c = 0; c < CHAINS; c++)
    {
        const VkXcbSurfaceCreateInfoKHR surfaceInfo = {
            .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
            .connection = connection,
            .window     = chains[c].window,
        };
        if (vkCreateXcbSurfaceKHR(instance, &surfaceInfo, NULL, &chains[c].surface) != VK_SUCCESS)
            return fail("vkCreateXcbSurfaceKHR failed");
    }

    uint32_t         count    = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    const VkResult   listed   = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if ((listed != VK_SUCCESS && listed != VK_INCOMPLETE) || count == 0)
        return fail("no physical device");

    VkFormat unorm = VK_FORMAT_UNDEFINED;
    VkFormat srgb  = VK_FORMAT_UNDEFINED;
    const int picked = pickFormats(physical, chains, &unorm, &srgb);
    if (picked < 0)
        return fail("out of memory");
    if (picked == 0)
    {
        printf("two-swapchain-probe: the surfaces offer no 8-bit UNORM and sRGB pair\n");
        return PROBE_SKIPPED;
    }
    chains[0].format = unorm;
    chains[1].format = srgb;

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties* properties = calloc(families ? families : 1, sizeof(*properties));
    if (!properties)
        return fail("out of memory");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties);
    uint32_t family = families;
    for (uint32_t i = 0; i < families && family == families; i++)
    {
        VkBool32 first  = VK_FALSE;
        VkBool32 second = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, chains[0].surface, &first);
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, chains[1].surface, &second);
        if (first && second && (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            family = i;
    }
    free(properties);
    if (family == families)
        return fail("no graphics queue family presents to both surfaces");

    const float                   priority  = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
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
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);

    const VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool                 pool     = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, NULL, &pool) != VK_SUCCESS)
        return fail("vkCreateCommandPool failed");

    for (int c = 0; c < CHAINS; c++)
        if (makeChain(physical, device, pool, &chains[c], c == 0 ? 0.75f : 0.25f) != 0)
            return 1;

    if (depth)
    {
        VkFormatProperties support;
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_D32_SFLOAT, &support);
        if (!(support.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            printf("two-swapchain-probe: this GPU has no D32_SFLOAT depth attachment\n");
            return PROBE_SKIPPED;
        }
        struct depthImage unread = {0};
        struct depthImage read   = {0};
        if (createDepth(device, &read) != 0 || createDepth(device, &unread) != 0)
            return 1;
        if (bindDepth(physical, device, queue, pool, &read) != 0 || presentRange(device, queue, chains, 0, frames / 4) != 0)
            return 1;
        if (bindDepth(physical, device, queue, pool, &unread) != 0 || presentRange(device, queue, chains, frames / 4, frames / 2) != 0)
            return 1;
        destroyDepth(device, &unread, "a depth image no effect reads");
        if (presentRange(device, queue, chains, frames / 2, frames * 3 / 4) != 0)
            return 1;
        destroyDepth(device, &read, "the depth image the effects read");
        if (presentRange(device, queue, chains, frames * 3 / 4, frames) != 0)
            return 1;
    }
    else if (presentRange(device, queue, chains, 0, frames) != 0)
        return 1;

    if (vkDeviceWaitIdle(device) != VK_SUCCESS)
        return fail("vkDeviceWaitIdle failed");
    for (int c = 0; c < CHAINS; c++)
        destroyChain(instance, device, &chains[c]);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    xcb_disconnect(connection);
    printf("two-swapchain-probe: %lu presents alternating a format %d and a format %d swapchain on one device%s\n", frames, unorm, srgb,
           depth ? ", two depth images bound and destroyed between them" : "");
    return 0;
}
