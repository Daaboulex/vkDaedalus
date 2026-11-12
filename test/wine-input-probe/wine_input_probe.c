#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <windows.h>
#include <windowsx.h>

#include <fcntl.h>
#include <io.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SLOTS  = 2,
    WIDTH  = 640,
    HEIGHT = 480
};

#define INSTANCE_FUNCTIONS(X)                     \
    X(vkDestroyInstance)                          \
    X(vkEnumeratePhysicalDevices)                 \
    X(vkGetPhysicalDeviceQueueFamilyProperties)   \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)       \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)  \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)       \
    X(vkCreateWin32SurfaceKHR)                    \
    X(vkDestroySurfaceKHR)                        \
    X(vkCreateDevice)                             \
    X(vkGetDeviceProcAddr)

#define DEVICE_FUNCTIONS(X)    \
    X(vkDestroyDevice)         \
    X(vkGetDeviceQueue)        \
    X(vkCreateSwapchainKHR)    \
    X(vkDestroySwapchainKHR)   \
    X(vkGetSwapchainImagesKHR) \
    X(vkCreateCommandPool)     \
    X(vkDestroyCommandPool)    \
    X(vkAllocateCommandBuffers) \
    X(vkBeginCommandBuffer)    \
    X(vkEndCommandBuffer)      \
    X(vkCmdPipelineBarrier)    \
    X(vkCmdClearColorImage)    \
    X(vkCreateSemaphore)       \
    X(vkDestroySemaphore)      \
    X(vkCreateFence)           \
    X(vkDestroyFence)          \
    X(vkWaitForFences)         \
    X(vkResetFences)           \
    X(vkAcquireNextImageKHR)   \
    X(vkQueueSubmit)           \
    X(vkQueuePresentKHR)       \
    X(vkDeviceWaitIdle)

#define DECLARE(name) static PFN_##name name;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static PFN_vkCreateInstance      vkCreateInstance;
INSTANCE_FUNCTIONS(DECLARE)
DEVICE_FUNCTIONS(DECLARE)

static int fail(const char* why)
{
    fprintf(stderr, "wine-input-probe: %s\n", why);
    return 1;
}

static int missing(const char* name)
{
    fprintf(stderr, "wine-input-probe: vulkan-1.dll gives no %s\n", name);
    return 1;
}

static int loadInstanceFunctions(VkInstance instance)
{
#define LOAD_INSTANCE(name)                                             \
    if (!(name = (PFN_##name)vkGetInstanceProcAddr(instance, #name))) \
        return missing(#name);
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
    return 0;
}

static int loadDeviceFunctions(VkDevice device)
{
#define LOAD_DEVICE(name)                                             \
    if (!(name = (PFN_##name)vkGetDeviceProcAddr(device, #name))) \
        return missing(#name);
    DEVICE_FUNCTIONS(LOAD_DEVICE)
    return 0;
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

static void reportRawInput(HRAWINPUT handle)
{
    RAWINPUT raw;
    UINT     size = sizeof(raw);
    if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    if (raw.header.dwType == RIM_TYPEMOUSE)
    {
        const USHORT buttons = raw.data.mouse.usButtonFlags;
        if (raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0)
            fprintf(stderr, "wine-input-probe: raw motion\n");
        if (buttons & RI_MOUSE_LEFT_BUTTON_DOWN)
            fprintf(stderr, "wine-input-probe: raw button press 1\n");
        if (buttons & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            fprintf(stderr, "wine-input-probe: raw button press 2\n");
        if (buttons & RI_MOUSE_RIGHT_BUTTON_DOWN)
            fprintf(stderr, "wine-input-probe: raw button press 3\n");
    }
    else if (raw.header.dwType == RIM_TYPEKEYBOARD && !(raw.data.keyboard.Flags & RI_KEY_BREAK))
        fprintf(stderr, "wine-input-probe: raw key press %u\n", raw.data.keyboard.VKey);
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_KEYDOWN: fprintf(stderr, "wine-input-probe: key press %u\n", (unsigned)wParam); return 0;
        case WM_LBUTTONDOWN: fprintf(stderr, "wine-input-probe: button press 1\n"); return 0;
        case WM_MBUTTONDOWN: fprintf(stderr, "wine-input-probe: button press 2\n"); return 0;
        case WM_RBUTTONDOWN: fprintf(stderr, "wine-input-probe: button press 3\n"); return 0;
        case WM_MOUSEMOVE: fprintf(stderr, "wine-input-probe: motion %d %d\n", GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_INPUT: reportRawInput((HRAWINPUT)lParam); break;
        case WM_CLOSE: return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static void reportAsyncKeys(int down[3])
{
    static const int keys[3] = {'A', 'B', VK_LBUTTON};
    for (int k = 0; k < 3; k++)
    {
        const int now = (GetAsyncKeyState(keys[k]) & 0x8001) != 0;
        if (now && !down[k])
            fprintf(stderr, "wine-input-probe: async key %d\n", keys[k]);
        down[k] = now;
    }
}

static POINT clientCentre(HWND window)
{
    POINT centre = {WIDTH / 2, HEIGHT / 2};
    ClientToScreen(window, &centre);
    return centre;
}

static int clipToClient(HWND window)
{
    RECT  client = {0, 0, 0, 0};
    POINT corners[2];
    if (!GetClientRect(window, &client))
        return 0;
    corners[0] = (POINT){client.left, client.top};
    corners[1] = (POINT){client.right, client.bottom};
    ClientToScreen(window, &corners[0]);
    ClientToScreen(window, &corners[1]);
    const RECT clip = {corners[0].x, corners[0].y, corners[1].x, corners[1].y};
    RECT       held = {0, 0, 0, 0};
    return ClipCursor(&clip) && GetClipCursor(&held) && memcmp(&held, &clip, sizeof(clip)) == 0;
}

int main(int argc, char** argv)
{
    _setmode(_fileno(stderr), _O_BINARY);
    char*               end      = NULL;
    const unsigned long frames   = argc == 3 ? strtoul(argv[1], &end, 10) : 0;
    const int           recenter = argc == 3 && strcmp(argv[2], "recenter") == 0;
    if (argc != 3 || (!recenter && strcmp(argv[2], "poll") != 0) || !end || *end != '\0' || frames < 8 || frames > 100000)
        return fail("usage: wine-input-probe <presents, 8 to 100000> poll|recenter");

    HINSTANCE        module  = GetModuleHandleA(NULL);
    const WNDCLASSA  kind    = {.lpfnWndProc = windowProcedure, .hInstance = module, .hCursor = LoadCursor(NULL, IDC_ARROW), .lpszClassName = "wine-input-probe"};
    RECT             outside = {0, 0, WIDTH, HEIGHT};
    const DWORD      style   = WS_OVERLAPPEDWINDOW;
    if (!RegisterClassA(&kind) || !AdjustWindowRect(&outside, style, FALSE))
        return fail("the window class could not be registered");
    HWND window = CreateWindowExA(0, "wine-input-probe", "wine-input-probe", style, 0, 0, outside.right - outside.left, outside.bottom - outside.top, NULL,
                                  NULL, module, NULL);
    if (!window)
        return fail("CreateWindowExA failed");
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    const RAWINPUTDEVICE devices[2] = {{0x01, 0x02, 0, window}, {0x01, 0x06, 0, window}};
    if (!RegisterRawInputDevices(devices, 2, sizeof(devices[0])))
        return fail("RegisterRawInputDevices failed");
    fprintf(stderr, "wine-input-probe: window shown\n");

    HMODULE vulkan = LoadLibraryA("vulkan-1.dll");
    if (!vulkan)
        return fail("no vulkan-1.dll");
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void (*)(void))GetProcAddress(vulkan, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr)
        return missing("vkGetInstanceProcAddr");
    if (!(vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance")))
        return missing("vkCreateInstance");

    const char* const          extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo    app          = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "wine-input-probe", .apiVersion = VK_API_VERSION_1_1};
    const VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS)
        return fail("vkCreateInstance failed");
    if (loadInstanceFunctions(instance) != 0)
        return 1;
    const VkWin32SurfaceCreateInfoKHR surfaceInfo = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, .hinstance = module, .hwnd = window};
    VkSurfaceKHR                      surface     = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(instance, &surfaceInfo, NULL, &surface) != VK_SUCCESS)
        return fail("vkCreateWin32SurfaceKHR failed");
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
    if (loadDeviceFunctions(device) != 0)
        return 1;
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
        .imageExtent      = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : (VkExtent2D){WIDTH, HEIGHT},
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

    int  foreground = 0, clipped = 0, down[3] = {0, 0, 0};
    LONG seenX = LONG_MIN, seenY = LONG_MIN;
    for (unsigned long frame = 0; frame < frames; frame++)
    {
        for (MSG message; PeekMessageA(&message, NULL, 0, 0, PM_REMOVE);)
            DispatchMessageA(&message);
        if (!foreground && GetForegroundWindow() == window)
        {
            foreground = 1;
            fprintf(stderr, "wine-input-probe: foreground\n");
        }
        if (recenter && foreground && !clipped && clipToClient(window))
        {
            clipped = 1;
            fprintf(stderr, "wine-input-probe: clips the pointer\n");
        }
        reportAsyncKeys(down);
        POINT at = {0, 0};
        if (GetCursorPos(&at) && (at.x != seenX || at.y != seenY))
        {
            fprintf(stderr, seenX != LONG_MIN ? "wine-input-probe: pointer moved %ld %ld\n" : "wine-input-probe: pointer at %ld %ld\n", at.x, at.y);
            seenX = at.x;
            seenY = at.y;
        }
        if (clipped)
        {
            const POINT centre = clientCentre(window);
            SetCursorPos(centre.x, centre.y);
            POINT warped = {0, 0};
            if (GetCursorPos(&warped))
            {
                seenX = warped.x;
                seenY = warped.y;
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
    ClipCursor(NULL);
    fprintf(stderr, "wine-input-probe: done after %lu presents%s\n", frames, foreground ? "" : ", never in the foreground");
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
    DestroyWindow(window);
    return 0;
}
