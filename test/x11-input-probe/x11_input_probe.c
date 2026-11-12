#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SLOTS = 2,
    WINE_XI_MAJOR = 2,
    WINE_XI_MINOR = 2
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

static Bool anyEvent(Display* display, XEvent* event, XPointer unused)
{
    return True;
}

static void report(Display* display, int xinput, XEvent* event)
{
    if (event->type == KeyPress)
        fprintf(stderr, "x11-input-probe: key press %u\n", event->xkey.keycode);
    else if (event->type == ButtonPress)
        fprintf(stderr, "x11-input-probe: button press %u\n", event->xbutton.button);
    else if (event->type == MotionNotify)
        fprintf(stderr, "x11-input-probe: motion %d %d\n", event->xmotion.x, event->xmotion.y);
    else if (event->type == GenericEvent && event->xcookie.extension == xinput && XGetEventData(display, &event->xcookie))
    {
        if (event->xcookie.evtype == XI_RawMotion)
            fprintf(stderr, "x11-input-probe: raw motion\n");
        else if (event->xcookie.evtype == XI_RawButtonPress)
            fprintf(stderr, "x11-input-probe: raw button press %d\n", ((const XIRawEvent*)event->xcookie.data)->detail);
        else if (event->xcookie.evtype == XI_RawKeyPress)
            fprintf(stderr, "x11-input-probe: raw key press %d\n", ((const XIRawEvent*)event->xcookie.data)->detail);
        XFreeEventData(display, &event->xcookie);
    }
}

int main(int argc, char** argv)
{
    char*               end    = NULL;
    const unsigned long frames   = argc >= 2 ? strtoul(argv[1], &end, 10) : 0;
    const int           recenter = argc == 3 && strcmp(argv[2], "recenter") == 0;
    if (argc < 2 || argc > 3 || (argc == 3 && !recenter) || !end || *end != '\0' || frames < 8 || frames > 100000)
        return fail("usage: x11-input-probe <presents, 8 to 100000> [recenter]");

    Display* display = XOpenDisplay(NULL);
    if (!display)
        return fail("no X11 display");
    const Window         root       = DefaultRootWindow(display);
    XSetWindowAttributes attributes = {.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask};
    const Window window = XCreateWindow(display, root, 0, 0, 640, 480, 0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask, &attributes);
    XStoreName(display, window, "x11-input-probe");
    XMapWindow(display, window);

    int xinput = 0, firstEvent = 0, firstError = 0, major = WINE_XI_MAJOR, minor = WINE_XI_MINOR;
    if (!XQueryExtension(display, "XInputExtension", &xinput, &firstEvent, &firstError) || XIQueryVersion(display, &major, &minor) != Success)
        return fail("no XInput2 on the display");
    unsigned char rawMask[XIMaskLen(XI_LASTEVENT)] = {0};
    XISetMask(rawMask, XI_RawMotion);
    XISetMask(rawMask, XI_RawButtonPress);
    XISetMask(rawMask, XI_RawKeyPress);
    XIEventMask selection = {.deviceid = XIAllMasterDevices, .mask_len = sizeof(rawMask), .mask = rawMask};
    XISelectEvents(display, root, &selection, 1);
    XFlush(display);

    const char* const          extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo    app          = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "x11-input-probe", .apiVersion = VK_API_VERSION_1_1};
    const VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS)
        return fail("vkCreateInstance failed");
    const VkXlibSurfaceCreateInfoKHR surfaceInfo = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, .dpy = display, .window = window};
    VkSurfaceKHR                     surface     = VK_NULL_HANDLE;
    if (vkCreateXlibSurfaceKHR(instance, &surfaceInfo, NULL, &surface) != VK_SUCCESS)
        return fail("vkCreateXlibSurfaceKHR failed");
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
    VkFence*         lastUse  = calloc(imageCount ? imageCount : 1, sizeof(*lastUse));
    if (!images || !commands || !rendered || !lastUse)
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

    int holding = 0, seenX = -1, seenY = -1;
    for (unsigned long frame = 0; frame < frames; frame++)
    {
        if (!holding
            && XGrabPointer(display, window, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, window, None,
                            CurrentTime) == GrabSuccess)
        {
            holding = 1;
            fprintf(stderr, "x11-input-probe: holds the pointer\n");
        }
        for (XEvent event; XCheckIfEvent(display, &event, anyEvent, NULL);)
            report(display, xinput, &event);
        Window       rootSeen = None, child = None;
        int          x = 0, y = 0, windowX = 0, windowY = 0;
        unsigned int buttonsHeld = 0;
        if (XQueryPointer(display, root, &rootSeen, &child, &x, &y, &windowX, &windowY, &buttonsHeld) && (x != seenX || y != seenY))
        {
            if (seenX >= 0)
                fprintf(stderr, "x11-input-probe: pointer moved %d %d\n", x, y);
            seenX = x;
            seenY = y;
        }
        if (recenter)
        {
            XWarpPointer(display, None, window, 0, 0, 0, 0, 320, 240);
            Window       warpedRoot = None, warpedChild = None;
            int          warpedX = 0, warpedY = 0, warpedWindowX = 0, warpedWindowY = 0;
            unsigned int warpedButtons = 0;
            if (XQueryPointer(display, root, &warpedRoot, &warpedChild, &warpedX, &warpedY, &warpedWindowX, &warpedWindowY, &warpedButtons))
            {
                seenX = warpedX;
                seenY = warpedY;
            }
        }
        const uint32_t slot = (uint32_t)(frame % SLOTS);
        if (vkWaitForFences(device, 1, &inFlight[slot], VK_TRUE, UINT64_MAX) != VK_SUCCESS || vkResetFences(device, 1, &inFlight[slot]) != VK_SUCCESS)
            return fail("vkWaitForFences failed");
        uint32_t       index = 0;
        const VkResult got   = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired[slot], VK_NULL_HANDLE, &index);
        if (got != VK_SUCCESS && got != VK_SUBOPTIMAL_KHR)
            return fail("vkAcquireNextImageKHR failed");
        if (lastUse[index] != VK_NULL_HANDLE && lastUse[index] != inFlight[slot]
            && vkWaitForFences(device, 1, &lastUse[index], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            return fail("vkWaitForFences on the image's last frame failed");
        lastUse[index] = inFlight[slot];
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
    fprintf(stderr, "x11-input-probe: done after %lu presents%s\n", frames, holding ? ", holding the pointer" : ", never holding the pointer");
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
    free(lastUse);
    XCloseDisplay(display);
    return 0;
}
