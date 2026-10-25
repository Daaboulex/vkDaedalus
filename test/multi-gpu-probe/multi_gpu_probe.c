#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum
{
    PROBE_SKIPPED = 77,
    WARM_UP       = 20,
    SHIFT_TEXELS  = 4
};

_Static_assert(SHIFT_TEXELS * 4 % 16 == 0, "RADV copies a buffer 16 bytes per thread with unbounded loads, so a copy whose size is not a "
                                           "multiple of 16 bytes reads past its source and faults the GPU when the source ends a page");

static const uint64_t FENCE_PATIENCE_NS = 2000000000ull;

struct gpu
{
    VkPhysicalDevice                      physical;
    VkPhysicalDeviceProperties            properties;
    VkDevice                              device;
    uint32_t                              family;
    VkQueue                               queue;
    VkCommandPool                         pool;
    PFN_vkGetMemoryFdKHR                  getMemoryFd;
    PFN_vkGetMemoryFdPropertiesKHR        getMemoryFdProperties;
    PFN_vkGetSemaphoreFdKHR               getSemaphoreFd;
    PFN_vkImportSemaphoreFdKHR            importSemaphoreFd;
};

struct shared
{
    VkBuffer       onA;
    VkDeviceMemory memoryA;
    VkBuffer       onB;
    VkDeviceMemory memoryB;
};

static int fail(const char* why)
{
    fprintf(stderr, "multi-gpu-probe: %s\n", why);
    return 1;
}

static int skip(const char* why)
{
    printf("multi-gpu-probe: %s\n", why);
    return PROBE_SKIPPED;
}

static int has_extension(VkPhysicalDevice physical, const char* name)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL) != VK_SUCCESS)
        return 0;
    VkExtensionProperties* all = calloc(count ? count : 1, sizeof(*all));
    if (!all)
        return 0;
    int found = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, all) == VK_SUCCESS)
        for (uint32_t i = 0; i < count && !found; i++)
            found = !strcmp(all[i].extensionName, name);
    free(all);
    return found;
}

static const char* const deviceExtensions[] = {
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
};
enum
{
    DEVICE_EXTENSIONS = sizeof(deviceExtensions) / sizeof(deviceExtensions[0])
};

static const char* missing_extension(VkPhysicalDevice physical)
{
    for (int i = 0; i < DEVICE_EXTENSIONS; i++)
        if (!has_extension(physical, deviceExtensions[i]))
            return deviceExtensions[i];
    return NULL;
}

static int shares_dma_buf(VkPhysicalDevice physical, VkExternalMemoryFeatureFlags wanted)
{
    const VkPhysicalDeviceExternalBufferInfo info = {
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
        .usage      = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    VkExternalBufferProperties properties = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
    vkGetPhysicalDeviceExternalBufferProperties(physical, &info, &properties);
    return (properties.externalMemoryProperties.externalMemoryFeatures & wanted) == wanted;
}

static int shares_sync_fd(VkPhysicalDevice physical)
{
    const VkPhysicalDeviceExternalSemaphoreInfo info = {
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkExternalSemaphoreProperties properties = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    vkGetPhysicalDeviceExternalSemaphoreProperties(physical, &info, &properties);
    const VkExternalSemaphoreFeatureFlags both =
        VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
    return (properties.externalSemaphoreFeatures & both) == both;
}

static int make_device(struct gpu* gpu)
{
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu->physical, &families, NULL);
    VkQueueFamilyProperties* properties = calloc(families ? families : 1, sizeof(*properties));
    if (!properties)
        return fail("out of memory");
    vkGetPhysicalDeviceQueueFamilyProperties(gpu->physical, &families, properties);
    gpu->family = families;
    for (uint32_t i = 0; i < families && gpu->family == families; i++)
        if (properties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))
            gpu->family = i;
    free(properties);
    if (gpu->family == families)
        return fail("a GPU has no queue that copies");

    const float                   priority = 1.0f;
    const VkDeviceQueueCreateInfo queue    = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = gpu->family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue,
        .enabledExtensionCount   = DEVICE_EXTENSIONS,
        .ppEnabledExtensionNames = deviceExtensions,
    };
    if (vkCreateDevice(gpu->physical, &info, NULL, &gpu->device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");
    vkGetDeviceQueue(gpu->device, gpu->family, 0, &gpu->queue);
    const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = gpu->family};
    if (vkCreateCommandPool(gpu->device, &pool, NULL, &gpu->pool) != VK_SUCCESS)
        return fail("vkCreateCommandPool failed");
    gpu->getMemoryFd           = (PFN_vkGetMemoryFdKHR) vkGetDeviceProcAddr(gpu->device, "vkGetMemoryFdKHR");
    gpu->getMemoryFdProperties = (PFN_vkGetMemoryFdPropertiesKHR) vkGetDeviceProcAddr(gpu->device, "vkGetMemoryFdPropertiesKHR");
    gpu->getSemaphoreFd        = (PFN_vkGetSemaphoreFdKHR) vkGetDeviceProcAddr(gpu->device, "vkGetSemaphoreFdKHR");
    gpu->importSemaphoreFd     = (PFN_vkImportSemaphoreFdKHR) vkGetDeviceProcAddr(gpu->device, "vkImportSemaphoreFdKHR");
    if (!gpu->getMemoryFd || !gpu->getMemoryFdProperties || !gpu->getSemaphoreFd || !gpu->importSemaphoreFd)
        return fail("a promised external memory or semaphore entry point is missing");
    return 0;
}

static int pick_type(VkPhysicalDevice physical, uint32_t allowed, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid)
{
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
        if ((allowed & (1u << i)) && (memory.memoryTypes[i].propertyFlags & want) == want && !(memory.memoryTypes[i].propertyFlags & avoid))
            return (int) i;
    return -1;
}

static int make_buffer(struct gpu* gpu, VkDeviceSize size, int external, VkBuffer* buffer, VkMemoryRequirements* requirements)
{
    const VkExternalMemoryBufferCreateInfo externalInfo = {
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    const VkBufferCreateInfo info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = external ? &externalInfo : NULL,
        .size        = size,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(gpu->device, &info, NULL, buffer) != VK_SUCCESS)
        return fail("vkCreateBuffer failed");
    vkGetBufferMemoryRequirements(gpu->device, *buffer, requirements);
    return 0;
}

static int make_local(struct gpu* gpu, VkDeviceSize size, VkMemoryPropertyFlags want, VkBuffer* buffer, VkDeviceMemory* memory)
{
    VkMemoryRequirements requirements;
    if (make_buffer(gpu, size, 0, buffer, &requirements))
        return 1;
    const int type = pick_type(gpu->physical, requirements.memoryTypeBits, want, 0);
    if (type < 0)
        return skip("a GPU has no memory type the probe's own buffers need");
    const VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = (uint32_t) type,
    };
    if (vkAllocateMemory(gpu->device, &allocate, NULL, memory) != VK_SUCCESS || vkBindBufferMemory(gpu->device, *buffer, *memory, 0) != VK_SUCCESS)
        return fail("allocating a probe buffer failed");
    return 0;
}

static int make_shared(struct gpu* a, struct gpu* b, VkDeviceSize size, struct shared* shared)
{
    VkMemoryRequirements requirements;
    if (make_buffer(a, size, 1, &shared->onA, &requirements))
        return 1;
    const int type = pick_type(a->physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0)
        return skip("the game's GPU offers no system-memory type for a buffer it exports");
    const VkMemoryDedicatedAllocateInfo dedicatedA = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .buffer = shared->onA};
    const VkExportMemoryAllocateInfo    exportInfo = {
        .sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext       = &dedicatedA,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    const VkMemoryAllocateInfo allocateA = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &exportInfo,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = (uint32_t) type,
    };
    if (vkAllocateMemory(a->device, &allocateA, NULL, &shared->memoryA) != VK_SUCCESS
        || vkBindBufferMemory(a->device, shared->onA, shared->memoryA, 0) != VK_SUCCESS)
        return fail("allocating the exported buffer failed");
    const VkMemoryGetFdInfoKHR getFd = {
        .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory     = shared->memoryA,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    int fd = -1;
    if (a->getMemoryFd(a->device, &getFd, &fd) != VK_SUCCESS || fd < 0)
        return fail("vkGetMemoryFdKHR could not export the buffer as a dma-buf");

    VkMemoryRequirements requirementsB;
    if (make_buffer(b, size, 1, &shared->onB, &requirementsB))
    {
        close(fd);
        return 1;
    }
    VkMemoryFdPropertiesKHR fdProperties = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    if (b->getMemoryFdProperties(b->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProperties) != VK_SUCCESS)
    {
        close(fd);
        return skip(b->properties.vendorID == 0x10de
                        ? "the second GPU, an NVIDIA one, cannot read the dma-buf the first one exported (NVIDIA needs nvidia_drm modeset=1 for it)"
                        : "the second GPU cannot read the dma-buf the first one exported");
    }
    const int typeB = pick_type(b->physical, fdProperties.memoryTypeBits & requirementsB.memoryTypeBits, 0, 0);
    if (typeB < 0 || requirementsB.size > requirements.size)
    {
        close(fd);
        return skip("the second GPU has no memory type that takes the exported buffer");
    }
    const VkMemoryDedicatedAllocateInfo dedicatedB = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .buffer = shared->onB};
    const VkImportMemoryFdInfoKHR       importInfo = {
        .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext      = &dedicatedB,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd         = fd,
    };
    const VkMemoryAllocateInfo allocateB = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &importInfo,
        .allocationSize  = requirementsB.size,
        .memoryTypeIndex = (uint32_t) typeB,
    };
    if (vkAllocateMemory(b->device, &allocateB, NULL, &shared->memoryB) != VK_SUCCESS)
    {
        close(fd);
        return fail("importing the dma-buf on the second GPU failed");
    }
    if (vkBindBufferMemory(b->device, shared->onB, shared->memoryB, 0) != VK_SUCCESS)
        return fail("binding the imported dma-buf failed");
    return 0;
}

static VkBufferMemoryBarrier hand(VkBuffer buffer, uint32_t from, uint32_t to, VkAccessFlags src, VkAccessFlags dst)
{
    return (VkBufferMemoryBarrier){
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask       = src,
        .dstAccessMask       = dst,
        .srcQueueFamilyIndex = from,
        .dstQueueFamilyIndex = to,
        .buffer              = buffer,
        .offset              = 0,
        .size                = VK_WHOLE_SIZE,
    };
}

static int begin(struct gpu* gpu, VkCommandBuffer* cmd)
{
    const VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = gpu->pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    const VkCommandBufferBeginInfo info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkAllocateCommandBuffers(gpu->device, &allocate, cmd) != VK_SUCCESS || vkBeginCommandBuffer(*cmd, &info) != VK_SUCCESS)
        return fail("starting a command buffer failed");
    return 0;
}

static int export_semaphore(VkDevice device, VkSemaphore* semaphore)
{
    const VkExportSemaphoreCreateInfo exportInfo = {
        .sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    const VkSemaphoreCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &exportInfo};
    return vkCreateSemaphore(device, &info, NULL, semaphore) == VK_SUCCESS ? 0 : fail("vkCreateSemaphore failed");
}

static int pass_on(struct gpu* from, VkSemaphore signalled, struct gpu* to, VkSemaphore waiting)
{
    const VkSemaphoreGetFdInfoKHR get = {
        .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore  = signalled,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    int fd = -1;
    if (from->getSemaphoreFd(from->device, &get, &fd) != VK_SUCCESS)
        return fail("vkGetSemaphoreFdKHR could not export a SYNC_FD");
    const VkImportSemaphoreFdInfoKHR import = {
        .sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore  = waiting,
        .flags      = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        .fd         = fd,
    };
    if (to->importSemaphoreFd(to->device, &import) != VK_SUCCESS)
    {
        if (fd >= 0)
            close(fd);
        return fail("vkImportSemaphoreFdKHR could not take the SYNC_FD");
    }
    return 0;
}

static int submit(struct gpu* gpu, VkCommandBuffer cmd, VkSemaphore wait, VkSemaphore signal, VkFence fence)
{
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo         info  = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = wait ? 1 : 0,
        .pWaitSemaphores      = &wait,
        .pWaitDstStageMask    = &stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = signal ? 1 : 0,
        .pSignalSemaphores    = &signal,
    };
    const VkResult result = vkQueueSubmit(gpu->queue, 1, &info, fence);
    if (result == VK_ERROR_DEVICE_LOST)
        return fail("a GPU was lost");
    return result == VK_SUCCESS ? 0 : fail("vkQueueSubmit failed");
}

static int wait_fence(struct gpu* gpu, VkFence fence)
{
    const VkResult result = vkWaitForFences(gpu->device, 1, &fence, VK_TRUE, FENCE_PATIENCE_NS);
    if (result == VK_TIMEOUT)
        return fail("a round trip did not finish within 2 s");
    if (result != VK_SUCCESS)
        return fail(result == VK_ERROR_DEVICE_LOST ? "a GPU was lost" : "vkWaitForFences failed");
    return vkResetFences(gpu->device, 1, &fence) == VK_SUCCESS ? 0 : fail("vkResetFences failed");
}

static int by_value(const void* x, const void* y)
{
    const double a = *(const double*) x, b = *(const double*) y;
    return (a > b) - (a < b);
}

static int measure(struct gpu* a, struct gpu* b, uint32_t width, uint32_t height, unsigned long trips)
{
    const VkDeviceSize texels = (VkDeviceSize) width * height;
    const VkDeviceSize size   = texels * 4;
    struct shared      outbound, inbound;
    VkBuffer           source, held;
    VkDeviceMemory     sourceMemory, heldMemory;
    int                status;
    if ((status = make_shared(a, b, size, &outbound)) || (status = make_shared(a, b, size, &inbound)))
        return status;
    if ((status = make_local(a, size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &source, &sourceMemory))
        || (status = make_local(b, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &held, &heldMemory)))
        return status;

    uint32_t* pattern = NULL;
    if (vkMapMemory(a->device, sourceMemory, 0, VK_WHOLE_SIZE, 0, (void**) &pattern) != VK_SUCCESS)
        return fail("mapping the source buffer failed");
    for (VkDeviceSize i = 0; i < texels; i++)
        pattern[i] = (uint32_t) i;
    vkUnmapMemory(a->device, sourceMemory);
    uint32_t* result = NULL;
    if (vkMapMemory(a->device, inbound.memoryA, 0, VK_WHOLE_SIZE, 0, (void**) &result) != VK_SUCCESS)
        return fail("mapping the inbound buffer failed");

    const uint32_t foreign = VK_QUEUE_FAMILY_FOREIGN_EXT;
    VkCommandBuffer out, there, back;
    if (begin(a, &out) || begin(b, &there) || begin(a, &back))
        return 1;
    vkCmdFillBuffer(out, inbound.onA, 0, VK_WHOLE_SIZE, 0xffffffffu);
    const VkBufferCopy whole = {0, 0, size};
    vkCmdCopyBuffer(out, source, outbound.onA, 1, &whole);
    const VkBufferMemoryBarrier release[2] = {hand(outbound.onA, a->family, foreign, VK_ACCESS_TRANSFER_WRITE_BIT, 0),
                                              hand(inbound.onA, a->family, foreign, VK_ACCESS_TRANSFER_WRITE_BIT, 0)};
    vkCmdPipelineBarrier(out, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 2, release, 0, NULL);

    const VkBufferMemoryBarrier acquireB[2] = {hand(outbound.onB, foreign, b->family, 0, VK_ACCESS_TRANSFER_READ_BIT),
                                               hand(inbound.onB, foreign, b->family, 0, VK_ACCESS_TRANSFER_WRITE_BIT)};
    vkCmdPipelineBarrier(there, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 2, acquireB, 0, NULL);
    vkCmdCopyBuffer(there, outbound.onB, held, 1, &whole);
    const VkBufferMemoryBarrier readHeld = hand(held, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                VK_ACCESS_TRANSFER_READ_BIT);
    vkCmdPipelineBarrier(there, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &readHeld, 0, NULL);
    const VkBufferCopy shifted = {SHIFT_TEXELS * 4, 0, size - SHIFT_TEXELS * 4};
    vkCmdCopyBuffer(there, held, inbound.onB, 1, &shifted);
    const VkBufferMemoryBarrier releaseB[2] = {hand(outbound.onB, b->family, foreign, 0, 0),
                                               hand(inbound.onB, b->family, foreign, VK_ACCESS_TRANSFER_WRITE_BIT, 0)};
    vkCmdPipelineBarrier(there, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 2, releaseB, 0, NULL);

    const VkBufferMemoryBarrier acquireA[2] = {hand(outbound.onA, foreign, a->family, 0, VK_ACCESS_TRANSFER_WRITE_BIT),
                                               hand(inbound.onA, foreign, a->family, 0, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)};
    vkCmdPipelineBarrier(back, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 2,
                         acquireA, 0, NULL);
    if (vkEndCommandBuffer(out) != VK_SUCCESS || vkEndCommandBuffer(there) != VK_SUCCESS || vkEndCommandBuffer(back) != VK_SUCCESS)
        return fail("recording a round trip failed");

    VkSemaphore toB, waitOnB, toA, waitOnA;
    if (export_semaphore(a->device, &toB) || export_semaphore(b->device, &waitOnB) || export_semaphore(b->device, &toA)
        || export_semaphore(a->device, &waitOnA))
        return 1;
    const VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence                 doneB, doneA;
    if (vkCreateFence(b->device, &fenceInfo, NULL, &doneB) != VK_SUCCESS || vkCreateFence(a->device, &fenceInfo, NULL, &doneA) != VK_SUCCESS)
        return fail("vkCreateFence failed");

    double* timings = calloc(trips, sizeof(*timings));
    if (!timings)
        return fail("out of memory");
    const VkDeviceSize checked[3] = {0, texels / 2, texels - SHIFT_TEXELS - 1};
    for (unsigned long trip = 0; trip < WARM_UP + trips; trip++)
    {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (submit(a, out, VK_NULL_HANDLE, toB, VK_NULL_HANDLE) || pass_on(a, toB, b, waitOnB) || submit(b, there, waitOnB, toA, doneB)
            || pass_on(b, toA, a, waitOnA) || submit(a, back, waitOnA, VK_NULL_HANDLE, doneA) || wait_fence(b, doneB) || wait_fence(a, doneA))
            return 1;
        clock_gettime(CLOCK_MONOTONIC, &end);
        for (int c = 0; c < 3; c++)
            if (result[checked[c]] != (uint32_t) checked[c] + SHIFT_TEXELS)
                return fail("a texel came back wrong, so the data did not make the round trip");
        if (result[texels - SHIFT_TEXELS] != 0xffffffffu)
            return fail("a texel past the copied range was written, so the second GPU copied more than it was given");
        if (trip >= WARM_UP)
            timings[trip - WARM_UP] = (double) (end.tv_sec - start.tv_sec) * 1e3 + (double) (end.tv_nsec - start.tv_nsec) / 1e6;
    }
    qsort(timings, trips, sizeof(*timings), by_value);
    printf("multi-gpu-probe: %ux%u from %s to %s and back: median %.3f ms, fastest %.3f, slowest %.3f over %lu round trips\n", width, height,
           a->properties.deviceName, b->properties.deviceName, timings[trips / 2], timings[0], timings[trips - 1], trips);
    free(timings);

    vkDeviceWaitIdle(a->device);
    vkDeviceWaitIdle(b->device);
    vkDestroyFence(a->device, doneA, NULL);
    vkDestroyFence(b->device, doneB, NULL);
    vkDestroySemaphore(a->device, toB, NULL);
    vkDestroySemaphore(a->device, waitOnA, NULL);
    vkDestroySemaphore(b->device, waitOnB, NULL);
    vkDestroySemaphore(b->device, toA, NULL);
    vkUnmapMemory(a->device, inbound.memoryA);
    vkResetCommandPool(a->device, a->pool, 0);
    vkResetCommandPool(b->device, b->pool, 0);
    vkDestroyBuffer(a->device, source, NULL);
    vkFreeMemory(a->device, sourceMemory, NULL);
    vkDestroyBuffer(b->device, held, NULL);
    vkFreeMemory(b->device, heldMemory, NULL);
    struct shared* both[2] = {&outbound, &inbound};
    for (int i = 0; i < 2; i++)
    {
        vkDestroyBuffer(b->device, both[i]->onB, NULL);
        vkFreeMemory(b->device, both[i]->memoryB, NULL);
        vkDestroyBuffer(a->device, both[i]->onA, NULL);
        vkFreeMemory(a->device, both[i]->memoryA, NULL);
    }
    return 0;
}

int main(int argc, char** argv)
{
    char*               end   = NULL;
    const unsigned long trips = argc >= 2 ? strtoul(argv[1], &end, 10) : 0;
    if ((argc != 2 && argc != 4) || !end || *end != '\0' || trips < 1 || trips > 100000)
        return fail("usage: multi-gpu-probe <round trips, 1 to 100000> [index of the game's GPU, index of the second GPU]");

    const VkApplicationInfo    app      = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "multi-gpu-probe", .apiVersion = VK_API_VERSION_1_3};
    const VkInstanceCreateInfo instInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance                 instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instInfo, NULL, &instance) != VK_SUCCESS)
        return fail("vkCreateInstance for Vulkan 1.3 failed");
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, NULL) != VK_SUCCESS)
        return fail("vkEnumeratePhysicalDevices failed");
    VkPhysicalDevice* physicals = calloc(count ? count : 1, sizeof(*physicals));
    if (!physicals)
        return fail("out of memory");
    if (vkEnumeratePhysicalDevices(instance, &count, physicals) != VK_SUCCESS)
        return fail("vkEnumeratePhysicalDevices failed");

    struct gpu a = {0}, b = {0};
    uint32_t   first = count, second = count;
    if (argc == 4)
    {
        first  = (uint32_t) strtoul(argv[2], NULL, 10);
        second = (uint32_t) strtoul(argv[3], NULL, 10);
        if (first >= count || second >= count || first == second)
            return skip("the GPUs named are not two different devices of this Vulkan instance");
    }
    for (uint32_t i = 0; i < count; i++)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicals[i], &properties);
        if (argc == 4 || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;
        if (first == count)
            first = i;
        else if (second == count)
        {
            VkPhysicalDeviceProperties firstProperties;
            vkGetPhysicalDeviceProperties(physicals[first], &firstProperties);
            if (properties.vendorID != firstProperties.vendorID)
                second = i;
        }
    }
    if (first == count || second == count)
        return skip("this machine has no two GPUs of different vendors in one Vulkan instance");
    a.physical = physicals[first];
    b.physical = physicals[second];
    free(physicals);
    vkGetPhysicalDeviceProperties(a.physical, &a.properties);
    vkGetPhysicalDeviceProperties(b.physical, &b.properties);

    const char* missing = missing_extension(a.physical);
    if (missing || (missing = missing_extension(b.physical)))
    {
        printf("multi-gpu-probe: %s or %s lacks %s\n", a.properties.deviceName, b.properties.deviceName, missing);
        return PROBE_SKIPPED;
    }
    if (!shares_dma_buf(a.physical, VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)
        || !shares_dma_buf(b.physical, VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
        return skip("the two GPUs cannot share a buffer as a dma-buf");
    if (!shares_sync_fd(a.physical) || !shares_sync_fd(b.physical))
        return skip("the two GPUs cannot hand each other a SYNC_FD semaphore");
    if (make_device(&a) || make_device(&b))
        return 1;

    const uint32_t sizes[2][2] = {{1920, 1080}, {3840, 2160}};
    for (int s = 0; s < 2; s++)
    {
        const int status = measure(&a, &b, sizes[s][0], sizes[s][1], trips);
        if (status)
            return status;
    }
    vkDestroyCommandPool(a.device, a.pool, NULL);
    vkDestroyCommandPool(b.device, b.pool, NULL);
    vkDestroyDevice(a.device, NULL);
    vkDestroyDevice(b.device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
