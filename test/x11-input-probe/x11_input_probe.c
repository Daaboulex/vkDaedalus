#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SLOTS = 2
};

static int fail(const char* why)
{
    fprintf(stderr, "x11-input-probe: %s\n", why);
    return 1;
}

static int recordClear(VkCommandBuffer cmd, VkImage image)
{
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        return fail("vkBeginCommandBuffer failed");
    const VkImageSubresourceRange range   = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier          barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = range,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    const VkClearColorValue colour = {.float32 = {0.2f, 0.25f, 0.5f, 1.0f}};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    return vkEndCommandBuffer(cmd) == VK_SUCCESS ? 0 : fail("vkEndCommandBuffer failed");
}

static void report(const xcb_generic_event_t* event, unsigned long* keys, unsigned long* buttons, unsigned long* motions)
{
    const uint8_t type = event->response_type & 0x7F;
    if (type == XCB_KEY_PRESS)
    {
        (*keys)++;
        fprintf(stderr, "x11-input-probe: key press %u\n", ((const xcb_key_press_event_t*)event)->detail);
    }
    else if (type == XCB_BUTTON_PRESS)
    {
        (*buttons)++;
        fprintf(stderr, "x11-input-probe: button press %u\n", ((const xcb_button_press_event_t*)event)->detail);
    }
    else if (type == XCB_MOTION_NOTIFY)
    {
        (*motions)++;
        fprintf(stderr, "x11-input-probe: motion\n");
    }
}

int main(int argc, char** argv)
{
    char*               end    = NULL;
    const unsigned long frames = argc == 2 ? strtoul(argv[1], &end, 10) : 0;
    if (argc != 2 || !end || *end != '\0' || frames < 8 || frames > 100000)
        return fail("usage: x11-input-probe <presents, 8 to 100000>");

    xcb_connection_t* connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection))
        return fail("no X11 display");
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    const xcb_window_t  window = xcb_generate_id(connection);
    const uint32_t      events = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                            XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_EXPOSURE;
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 640, 480, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_EVENT_MASK, &events);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen("x11-input-probe"), "x11-input-probe");
    xcb_map_window(connection, window);
    xcb_flush(connection);

    const char* const          extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo    app          = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "x11-input-probe", .apiVersion = VK_API_VERSION_1_1};
    const VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
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
        VkBool32 presents = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &presents);
        if (presents && (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            family = i;
    }
    free(properties);
    if (family == families)
        return fail("no graphics queue family presents to the window");
    const float                   priority  = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char* const        deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo deviceInfo         = {.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                                   .queueCreateInfoCount    = 1,
                                                   .pQueueCreateInfos       = &queueInfo,
                                                   .enabledExtensionCount   = 1,
                                                   .ppEnabledExtensionNames = deviceExtensions};
    VkDevice                 device             = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, NULL, &device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps) != VK_SUCCESS || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return fail("the surface cannot be cleared by a transfer");
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, NULL);
    VkSurfaceFormatKHR* formats = calloc(formatCount ? formatCount : 1, sizeof(*formats));
    if (!formats)
        return fail("out of memory");
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats);
    const VkSurfaceFormatKHR chosen = formats[0];
    free(formats);
    uint32_t minImages = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && minImages > caps.maxImageCount)
        minImages = caps.maxImageCount;
    const VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = minImages,
        .imageFormat      = chosen.format,
        .imageColorSpace  = chosen.colorSpace,
        .imageExtent      = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : (VkExtent2D){640, 480},
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
    if (!images || !commands || !rendered)
        return fail("out of memory");
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images);
    const VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool                 pool     = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, NULL, &pool) != VK_SUCCESS)
        return fail("vkCreateCommandPool failed");
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = imageCount};
    if (vkAllocateCommandBuffers(device, &allocate, commands) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers failed");
    const VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < imageCount; i++)
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &rendered[i]) != VK_SUCCESS || recordClear(commands[i], images[i]) != 0)
            return fail("a frame could not be prepared");
    VkSemaphore             acquired[SLOTS];
    VkFence                 inFlight[SLOTS];
    const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int s = 0; s < SLOTS; s++)
        if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &acquired[s]) != VK_SUCCESS || vkCreateFence(device, &fenceInfo, NULL, &inFlight[s]) != VK_SUCCESS)
            return fail("vkCreateSemaphore or vkCreateFence failed");

    unsigned long keys = 0, buttons = 0, motions = 0;
    int           holding = 0;
    for (unsigned long frame = 0; frame < frames; frame++)
    {
        if (!holding)
        {
            const uint16_t                pointerEvents = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
            xcb_grab_pointer_reply_t* grab = xcb_grab_pointer_reply(connection,
                                                                     xcb_grab_pointer(connection, 0, window, pointerEvents, XCB_GRAB_MODE_ASYNC,
                                                                                      XCB_GRAB_MODE_ASYNC, window, XCB_NONE, XCB_CURRENT_TIME),
                                                                     NULL);
            if (grab && grab->status == XCB_GRAB_STATUS_SUCCESS)
            {
                holding = 1;
                fprintf(stderr, "x11-input-probe: holds the pointer\n");
            }
            free(grab);
        }
        for (xcb_generic_event_t* event = xcb_poll_for_event(connection); event; event = xcb_poll_for_event(connection))
        {
            report(event, &keys, &buttons, &motions);
            free(event);
        }
        const uint32_t slot = (uint32_t)(frame % SLOTS);
        if (vkWaitForFences(device, 1, &inFlight[slot], VK_TRUE, UINT64_MAX) != VK_SUCCESS || vkResetFences(device, 1, &inFlight[slot]) != VK_SUCCESS)
            return fail("vkWaitForFences failed");
        uint32_t       index = 0;
        const VkResult got   = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired[slot], VK_NULL_HANDLE, &index);
        if (got != VK_SUCCESS && got != VK_SUBOPTIMAL_KHR)
            return fail("vkAcquireNextImageKHR failed");
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        const VkSubmitInfo         submit    = {.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                                .waitSemaphoreCount   = 1,
                                                .pWaitSemaphores      = &acquired[slot],
                                                .pWaitDstStageMask    = &waitStage,
                                                .commandBufferCount   = 1,
                                                .pCommandBuffers      = &commands[index],
                                                .signalSemaphoreCount = 1,
                                                .pSignalSemaphores    = &rendered[index]};
        if (vkQueueSubmit(queue, 1, &submit, inFlight[slot]) != VK_SUCCESS)
            return fail("vkQueueSubmit failed");
        const VkPresentInfoKHR present = {.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                          .waitSemaphoreCount = 1,
                                          .pWaitSemaphores    = &rendered[index],
                                          .swapchainCount     = 1,
                                          .pSwapchains        = &swapchain,
                                          .pImageIndices      = &index};
        const VkResult         shown   = vkQueuePresentKHR(queue, &present);
        if (shown != VK_SUCCESS && shown != VK_SUBOPTIMAL_KHR)
            return fail("vkQueuePresentKHR failed");
    }
    vkDeviceWaitIdle(device);
    fprintf(stderr, "x11-input-probe: counted %lu key presses, %lu button presses and %lu motions over %lu presents%s\n", keys, buttons, motions, frames,
            holding ? ", holding the pointer" : ", never holding the pointer");
    for (int s = 0; s < SLOTS; s++)
    {
        vkDestroySemaphore(device, acquired[s], NULL);
        vkDestroyFence(device, inFlight[s], NULL);
    }
    for (uint32_t i = 0; i < imageCount; i++)
        vkDestroySemaphore(device, rendered[i], NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    free(images);
    free(commands);
    free(rendered);
    xcb_disconnect(connection);
    return 0;
}
