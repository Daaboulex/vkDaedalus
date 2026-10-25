#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PROBE_SKIPPED = 77
};

static int fail(const char* why)
{
    fprintf(stderr, "swapchain-probe: %s\n", why);
    return 1;
}

static int makeSwapchain(VkDevice device, VkSurfaceKHR surface, const VkSurfaceCapabilitiesKHR* caps, VkFormat format, VkFormat srgb,
                         uint32_t listCount)
{
    VkFormat                    listed[2] = {format, srgb};
    VkImageFormatListCreateInfo list      = {
        .sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = listCount,
        .pViewFormats    = listed,
    };
    const VkSwapchainCreateInfoKHR info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = listCount ? &list : NULL,
        .flags            = listCount ? VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR : 0,
        .surface          = surface,
        .minImageCount    = caps->minImageCount,
        .imageFormat      = format,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent      = caps->currentExtent.width != UINT32_MAX ? caps->currentExtent : (VkExtent2D){320, 240},
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps->currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &info, NULL, &swapchain) != VK_SUCCESS)
        return fail("vkCreateSwapchainKHR failed");
    if (list.viewFormatCount != listCount || list.pViewFormats != listed || listed[0] != format || listed[1] != srgb)
        return fail("the layer changed the application's own image format list");

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, NULL);
    VkImage* images = calloc(count, sizeof(*images));
    if (!images || vkGetSwapchainImagesKHR(device, swapchain, &count, images) != VK_SUCCESS || count == 0)
    {
        free(images);
        return fail("vkGetSwapchainImagesKHR failed");
    }
    const uint32_t viewFormats = listCount ? listCount : 1;
    for (uint32_t i = 0; i < viewFormats; i++)
    {
        const VkImageViewCreateInfo viewInfo = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = images[0],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = listed[i],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &viewInfo, NULL, &view) != VK_SUCCESS)
        {
            free(images);
            return fail("a view in a listed format could not be created");
        }
        vkDestroyImageView(device, view, NULL);
    }
    free(images);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    return 0;
}

int main(int argc, char** argv)
{
    const int computeOnly = argc == 2 && strcmp(argv[1], "compute-only") == 0;
    if (argc != 2 || (!computeOnly && strcmp(argv[1], "formats") != 0))
        return fail("usage: swapchain-probe formats|compute-only");

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
                                                       .pApplicationName = "swapchain-probe",
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

    const VkXcbSurfaceCreateInfoKHR surfaceInfo = {
        .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = connection,
        .window     = window,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateXcbSurfaceKHR(instance, &surfaceInfo, NULL, &surface) != VK_SUCCESS)
        return fail("vkCreateXcbSurfaceKHR failed");

    uint32_t         count    = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    const VkResult   listed   = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if ((listed != VK_SUCCESS && listed != VK_INCOMPLETE) || count == 0)
        return fail("no physical device");

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties* properties = calloc(families, sizeof(*properties));
    if (!properties)
        return fail("out of memory");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties);
    uint32_t family = families;
    for (uint32_t i = 0; i < families && family == families; i++)
    {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
        if (present && (!computeOnly || !(properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)))
            family = i;
    }
    free(properties);
    if (family == families && computeOnly)
    {
        printf("swapchain-probe: no queue family presents without graphics on this device\n");
        return PROBE_SKIPPED;
    }
    if (family == families)
        return fail("no queue family presents to the surface");

    const float                   priority  = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };
    const char* const        deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME};
    const VkDeviceCreateInfo deviceInfo         = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueInfo,
        .enabledExtensionCount   = computeOnly ? 1 : 2,
        .ppEnabledExtensionNames = deviceExtensions,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, NULL, &device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, NULL);
    VkSurfaceFormatKHR* formats = calloc(formatCount, sizeof(*formats));
    if (!formats)
        return fail("out of memory");
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats);
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat srgb   = VK_FORMAT_UNDEFINED;
    for (uint32_t i = 0; i < formatCount && format == VK_FORMAT_UNDEFINED; i++)
    {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            format = VK_FORMAT_B8G8R8A8_UNORM;
            srgb   = VK_FORMAT_B8G8R8A8_SRGB;
        }
        else if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            format = VK_FORMAT_R8G8B8A8_UNORM;
            srgb   = VK_FORMAT_R8G8B8A8_SRGB;
        }
    }
    free(formats);
    if (format == VK_FORMAT_UNDEFINED)
        return fail("the surface offers no 8-bit UNORM format");

    if (computeOnly ? makeSwapchain(device, surface, &caps, format, srgb, 0) != 0
                    : makeSwapchain(device, surface, &caps, format, srgb, 2) != 0 || makeSwapchain(device, surface, &caps, format, srgb, 1) != 0)
        return 1;

    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    xcb_disconnect(connection);
    printf(computeOnly ? "swapchain-probe: a swapchain on a queue family without graphics made, read and destroyed\n"
                       : "swapchain-probe: mutable-format swapchains with a two-format and a one-format list of the application's own, "
                         "views in every listed format\n");
    return 0;
}
