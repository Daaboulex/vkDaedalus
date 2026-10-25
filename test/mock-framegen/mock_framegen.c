/* mirrors lsfg-vk: spare TRANSFER images, then per present an extra acquire, a blit, the generated and the original present */

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING       8
#define MAX_IMAGES 16

static PFN_vkGetInstanceProcAddr g_next_gipa = NULL;
static VkInstance                g_instance  = VK_NULL_HANDLE;
static PFN_vkGetDeviceProcAddr   g_next_gdpa = NULL;

static VkDevice g_device = VK_NULL_HANDLE;
static uint32_t g_qfamily = 0;

static PFN_vkAcquireNextImageKHR    g_AcquireNextImage    = NULL;
static PFN_vkQueuePresentKHR        g_QueuePresent        = NULL;
static PFN_vkQueueSubmit            g_QueueSubmit         = NULL;
static PFN_vkCreateSemaphore        g_CreateSemaphore     = NULL;
static PFN_vkCreateFence            g_CreateFence         = NULL;
static PFN_vkWaitForFences          g_WaitForFences       = NULL;
static PFN_vkResetFences            g_ResetFences         = NULL;
static PFN_vkCreateCommandPool      g_CreateCommandPool   = NULL;
static PFN_vkAllocateCommandBuffers g_AllocCommandBuffers = NULL;
static PFN_vkBeginCommandBuffer     g_BeginCommandBuffer  = NULL;
static PFN_vkEndCommandBuffer       g_EndCommandBuffer    = NULL;
static PFN_vkCmdPipelineBarrier     g_CmdPipelineBarrier  = NULL;
static PFN_vkCmdBlitImage           g_CmdBlitImage        = NULL;
static PFN_vkCreateSwapchainKHR     g_CreateSwapchain     = NULL;
static PFN_vkGetSwapchainImagesKHR  g_GetSwapchainImages  = NULL;
static PFN_vkDeviceWaitIdle         g_DeviceWaitIdle      = NULL;
static PFN_vkDestroySemaphore       g_DestroySemaphore    = NULL;
static PFN_vkDestroyFence           g_DestroyFence        = NULL;
static PFN_vkDestroyCommandPool     g_DestroyCommandPool  = NULL;
static PFN_vkDestroyDevice          g_DestroyDevice       = NULL;

static VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
static VkImage        g_images[MAX_IMAGES];
static uint32_t       g_image_count = 0;
static VkExtent2D     g_extent;

typedef struct
{
    VkSemaphore     acqSem;
    VkSemaphore     blitDoneForGeneratedPresent;
    VkSemaphore     blitDoneForOriginalPresent;
    VkFence         slotFreeFence;
    VkCommandBuffer cmd;
    int             used;
} Slot;

static Slot            g_ring[RING];
static VkCommandPool   g_pool = VK_NULL_HANDLE;
static unsigned        g_slot = 0;
static unsigned        g_frames = 0;
static int             g_ready = 0;
static int             g_nested_probe_done = 0;

/* frame-generation layers create a nested instance inside vkCreateSwapchainKHR, and only the public loader re-enters the layers above */
static int runNestedLoaderProbe(void)
{
    const char* enabled = getenv("MOCK_FRAMEGEN_REENTER_LOADER");
    if (enabled == NULL || strcmp(enabled, "1") != 0 || g_nested_probe_done)
        return 1;

    g_nested_probe_done = 1;

    void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (loader == NULL)
    {
        fprintf(stderr, "mock_framegen: nested loader re-entry failed: %s\n", dlerror());
        return 0;
    }

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance) dlsym(loader, "vkCreateInstance");
    PFN_vkDestroyInstance destroyInstance =
        (PFN_vkDestroyInstance) dlsym(loader, "vkDestroyInstance");
    if (createInstance == NULL || destroyInstance == NULL)
    {
        fprintf(stderr, "mock_framegen: nested loader re-entry failed: missing loader entry point\n");
        dlclose(loader);
        return 0;
    }

    VkApplicationInfo app = { 0 };
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "mock-framegen-nested-loader-probe";
    app.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo = { 0 };
    createInfo.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo     = &app;

    VkInstance nestedInstance = VK_NULL_HANDLE;
    fprintf(stderr, "mock_framegen: nested loader probe starting\n");
    VkResult result = createInstance(&createInfo, NULL, &nestedInstance);
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "mock_framegen: nested loader re-entry failed: %d\n", result);
        dlclose(loader);
        return 0;
    }

    destroyInstance(nestedInstance, NULL);
    dlclose(loader);
    fprintf(stderr, "mock_framegen: nested loader re-entry ok\n");
    return 1;
}

static VkImageMemoryBarrier barrier(VkImage img, VkAccessFlags src, VkAccessFlags dst,
                                    VkImageLayout old, VkImageLayout nw)
{
    VkImageMemoryBarrier b = { 0 };
    b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask               = src;
    b.dstAccessMask               = dst;
    b.oldLayout                   = old;
    b.newLayout                   = nw;
    b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    b.image                       = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    return b;
}

static int ensureResources(void)
{
    if (g_ready)
        return 1;
    if (g_device == VK_NULL_HANDLE || g_image_count == 0)
        return 0;

    VkCommandPoolCreateInfo pci = { 0 };
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g_qfamily;
    if (g_CreateCommandPool(g_device, &pci, NULL, &g_pool) != VK_SUCCESS)
    {
        fprintf(stderr, "mock_framegen: command pool creation failed\n");
        return 0;
    }

    VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    for (int i = 0; i < RING; i++)
    {
        VkCommandBufferAllocateInfo ai = { 0 };
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = g_pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (g_AllocCommandBuffers(g_device, &ai, &g_ring[i].cmd) != VK_SUCCESS)
            return 0;
        *(void**) g_ring[i].cmd = *(void**) g_device;
        g_CreateSemaphore(g_device, &si, NULL, &g_ring[i].acqSem);
        g_CreateSemaphore(g_device, &si, NULL, &g_ring[i].blitDoneForGeneratedPresent);
        g_CreateSemaphore(g_device, &si, NULL, &g_ring[i].blitDoneForOriginalPresent);
        g_CreateFence(g_device, &fi, NULL, &g_ring[i].slotFreeFence);
        g_ring[i].used = 0;
    }
    g_ready = 1;
    fprintf(stderr, "mock_framegen: ready (%u images, %ux%u)\n",
            g_image_count, g_extent.width, g_extent.height);
    return 1;
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_CreateSwapchainKHR(VkDevice                        device,
                                                              const VkSwapchainCreateInfoKHR* info,
                                                              const VkAllocationCallbacks*    alloc,
                                                              VkSwapchainKHR*                 swapchain)
{
    if (!runNestedLoaderProbe())
        return VK_ERROR_INITIALIZATION_FAILED;

    VkSwapchainCreateInfoKHR mod = *info;
    mod.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    mod.minImageCount += 1;

    VkResult r = g_CreateSwapchain(device, &mod, alloc, swapchain);
    if (r != VK_SUCCESS)
        return r;

    uint32_t count = 0;
    g_GetSwapchainImages(device, *swapchain, &count, NULL);
    if (count > MAX_IMAGES)
        count = MAX_IMAGES;
    g_GetSwapchainImages(device, *swapchain, &count, g_images);

    g_swapchain   = *swapchain;
    g_image_count = count;
    g_extent      = mod.imageExtent;
    g_ready       = 0;
    g_frames      = 0;
    fprintf(stderr, "mock_framegen: swapchain created (asked %u, got %u images, usage 0x%x)\n",
            mod.minImageCount, count, mod.imageUsage);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    if (info->swapchainCount != 1 || info->pSwapchains[0] != g_swapchain || !ensureResources())
        return g_QueuePresent(queue, info);

    Slot* s = &g_ring[g_slot];
    g_slot  = (g_slot + 1) % RING;

    if (s->used)
    {
        if (g_WaitForFences(g_device, 1, &s->slotFreeFence, VK_TRUE, 2000000000ULL) != VK_SUCCESS)
            fprintf(stderr, "mock_framegen: slot fence wait timed out (2s) -- STALL\n");
        g_ResetFences(g_device, 1, &s->slotFreeFence);
    }
    s->used = 1;

    const uint32_t origIdx = info->pImageIndices[0];

    uint32_t aqIdx = 0;
    VkResult r     = g_AcquireNextImage(g_device, g_swapchain, 3000000000ULL, s->acqSem,
                                        VK_NULL_HANDLE, &aqIdx);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        fprintf(stderr, "mock_framegen: extra acquire failed: %d%s\n", r,
                r == VK_TIMEOUT ? " (TIMEOUT -- no spare swapchain image: this is the deadlock)" : "");
        return g_QueuePresent(queue, info);
    }

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    g_BeginCommandBuffer(s->cmd, &bi);

    VkImageMemoryBarrier pre[2] = {
        barrier(g_images[origIdx], 0, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
        barrier(g_images[aqIdx], 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
    };
    g_CmdPipelineBarrier(s->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 2, pre);

    VkImageBlit region = { 0 };
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.srcOffsets[1].x           = (int32_t) g_extent.width;
    region.srcOffsets[1].y           = (int32_t) g_extent.height;
    region.srcOffsets[1].z           = 1;
    region.dstOffsets[1]             = region.srcOffsets[1];
    g_CmdBlitImage(s->cmd, g_images[origIdx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   g_images[aqIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_NEAREST);

    VkImageMemoryBarrier post[2] = {
        barrier(g_images[origIdx], VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
        barrier(g_images[aqIdx], VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
    };
    g_CmdPipelineBarrier(s->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, NULL, 0, NULL, 2, post);
    g_EndCommandBuffer(s->cmd);

    VkSemaphore          waits[9];
    VkPipelineStageFlags stages[9];
    uint32_t             nwait = 0;
    for (uint32_t i = 0; i < info->waitSemaphoreCount && nwait < 8; i++)
    {
        waits[nwait]    = info->pWaitSemaphores[i];
        stages[nwait++] = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    waits[nwait]    = s->acqSem;
    stages[nwait++] = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSemaphore  signals[2] = { s->blitDoneForGeneratedPresent, s->blitDoneForOriginalPresent };
    VkSubmitInfo sub        = { 0 };
    sub.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.waitSemaphoreCount   = nwait;
    sub.pWaitSemaphores      = waits;
    sub.pWaitDstStageMask    = stages;
    sub.commandBufferCount   = 1;
    sub.pCommandBuffers      = &s->cmd;
    sub.signalSemaphoreCount = 2;
    sub.pSignalSemaphores    = signals;
    r                        = g_QueueSubmit(queue, 1, &sub, s->slotFreeFence);
    if (r != VK_SUCCESS)
    {
        fprintf(stderr, "mock_framegen: blit submit failed: %d\n", r);
        return g_QueuePresent(queue, info);
    }

    VkPresentInfoKHR gen = { 0 };
    gen.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    gen.waitSemaphoreCount = 1;
    gen.pWaitSemaphores    = &s->blitDoneForGeneratedPresent;
    gen.swapchainCount     = 1;
    gen.pSwapchains        = info->pSwapchains;
    gen.pImageIndices      = &aqIdx;
    r                      = g_QueuePresent(queue, &gen);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        fprintf(stderr, "mock_framegen: generated present failed: %d\n", r);

    VkPresentInfoKHR orig = { 0 };
    orig.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    orig.pNext              = info->pNext;
    orig.waitSemaphoreCount = 1;
    orig.pWaitSemaphores    = &s->blitDoneForOriginalPresent;
    orig.swapchainCount     = 1;
    orig.pSwapchains        = info->pSwapchains;
    orig.pImageIndices      = &origIdx;
    r                       = g_QueuePresent(queue, &orig);

    ++g_frames;
    fprintf(stderr, "mock_framegen: frame %u ok (orig idx %u, extra idx %u)\n",
            g_frames, origIdx, aqIdx);
    if (info->pResults)
        info->pResults[0] = r;
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_CreateInstance(const VkInstanceCreateInfo*  info,
                                                          const VkAllocationCallbacks* alloc,
                                                          VkInstance*                  inst)
{
    VkLayerInstanceCreateInfo* lci = (VkLayerInstanceCreateInfo*) info->pNext;
    while (lci
           && (lci->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || lci->function != VK_LAYER_LINK_INFO))
        lci = (VkLayerInstanceCreateInfo*) lci->pNext;
    if (!lci)
        return VK_ERROR_INITIALIZATION_FAILED;

    g_next_gipa       = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;

    PFN_vkCreateInstance create = (PFN_vkCreateInstance) g_next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult             r      = create(info, alloc, inst);
    if (r == VK_SUCCESS)
        g_instance = *inst;
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_CreateDevice(VkPhysicalDevice             phys,
                                                        const VkDeviceCreateInfo*    info,
                                                        const VkAllocationCallbacks* alloc,
                                                        VkDevice*                    dev)
{
    VkLayerDeviceCreateInfo* ldi = (VkLayerDeviceCreateInfo*) info->pNext;
    while (ldi
           && (ldi->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || ldi->function != VK_LAYER_LINK_INFO))
        ldi = (VkLayerDeviceCreateInfo*) ldi->pNext;
    if (!ldi)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = ldi->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    g_next_gdpa                    = ldi->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    ldi->u.pLayerInfo              = ldi->u.pLayerInfo->pNext;

    PFN_vkCreateDevice create = (PFN_vkCreateDevice) gipa(g_instance, "vkCreateDevice");
    VkResult           r      = create(phys, info, alloc, dev);
    if (r != VK_SUCCESS)
        return r;

    g_device  = *dev;
    g_qfamily = info->queueCreateInfoCount ? info->pQueueCreateInfos[0].queueFamilyIndex : 0;

#define GD(name) g_##name = (PFN_vk##name) g_next_gdpa(*dev, "vk" #name)
    GD(QueueSubmit);
    GD(CreateSemaphore);
    GD(CreateFence);
    GD(WaitForFences);
    GD(ResetFences);
    GD(CreateCommandPool);
    GD(BeginCommandBuffer);
    GD(EndCommandBuffer);
    GD(CmdPipelineBarrier);
    GD(CmdBlitImage);
    GD(DeviceWaitIdle);
    GD(DestroySemaphore);
    GD(DestroyFence);
    GD(DestroyCommandPool);
    GD(DestroyDevice);
#undef GD
    g_AllocCommandBuffers = (PFN_vkAllocateCommandBuffers) g_next_gdpa(*dev, "vkAllocateCommandBuffers");
    g_AcquireNextImage    = (PFN_vkAcquireNextImageKHR) g_next_gdpa(*dev, "vkAcquireNextImageKHR");
    g_QueuePresent        = (PFN_vkQueuePresentKHR) g_next_gdpa(*dev, "vkQueuePresentKHR");
    g_CreateSwapchain     = (PFN_vkCreateSwapchainKHR) g_next_gdpa(*dev, "vkCreateSwapchainKHR");
    g_GetSwapchainImages  = (PFN_vkGetSwapchainImagesKHR) g_next_gdpa(*dev, "vkGetSwapchainImagesKHR");

    fprintf(stderr, "mock_framegen: device hooked (queue family %u)\n", g_qfamily);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL mock_DestroyDevice(VkDevice dev, const VkAllocationCallbacks* alloc)
{
    if (dev == g_device)
    {
        g_DeviceWaitIdle(dev);
        for (int i = 0; i < RING; i++)
        {
            if (g_ring[i].acqSem)
                g_DestroySemaphore(dev, g_ring[i].acqSem, NULL);
            if (g_ring[i].blitDoneForGeneratedPresent)
                g_DestroySemaphore(dev, g_ring[i].blitDoneForGeneratedPresent, NULL);
            if (g_ring[i].blitDoneForOriginalPresent)
                g_DestroySemaphore(dev, g_ring[i].blitDoneForOriginalPresent, NULL);
            if (g_ring[i].slotFreeFence)
                g_DestroyFence(dev, g_ring[i].slotFreeFence, NULL);
            memset(&g_ring[i], 0, sizeof(g_ring[i]));
        }
        if (g_pool)
            g_DestroyCommandPool(dev, g_pool, NULL);
        g_pool   = VK_NULL_HANDLE;
        g_ready  = 0;
        g_device = VK_NULL_HANDLE;
        fprintf(stderr, "mock_framegen: device released its ring\n");
    }
    g_DestroyDevice(dev, alloc);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetDeviceProcAddr(VkDevice dev, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetInstanceProcAddr(VkInstance inst, const char* name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetDeviceProcAddr(VkDevice dev, const char* name)
{
    if (!strcmp(name, "vkDestroyDevice"))
        return (PFN_vkVoidFunction) mock_DestroyDevice;
    if (!strcmp(name, "vkQueuePresentKHR"))
        return (PFN_vkVoidFunction) mock_QueuePresentKHR;
    if (!strcmp(name, "vkCreateSwapchainKHR"))
        return (PFN_vkVoidFunction) mock_CreateSwapchainKHR;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction) mock_GetDeviceProcAddr;
    return g_next_gdpa ? g_next_gdpa(dev, name) : NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetInstanceProcAddr(VkInstance inst, const char* name)
{
    if (!inst && strcmp(name, "vkCreateInstance") && strcmp(name, "vkGetInstanceProcAddr") && strncmp(name, "vkEnumerateInstance", 19))
        return NULL;
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction) mock_CreateInstance;
    if (!strcmp(name, "vkCreateDevice"))
        return (PFN_vkVoidFunction) mock_CreateDevice;
    if (!strcmp(name, "vkQueuePresentKHR"))
        return (PFN_vkVoidFunction) mock_QueuePresentKHR;
    if (!strcmp(name, "vkCreateSwapchainKHR"))
        return (PFN_vkVoidFunction) mock_CreateSwapchainKHR;
    if (!strcmp(name, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction) mock_GetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction) mock_GetDeviceProcAddr;
    return g_next_gipa ? g_next_gipa(inst, name) : NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v)
{
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || v->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion  = 2;
    v->pfnGetInstanceProcAddr       = mock_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr         = mock_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
