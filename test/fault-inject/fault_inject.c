#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEVICE_HOOKS(X)                                                             \
    X(CreateImage)                                                                  \
    X(CreateImageView)                                                              \
    X(CreateBuffer)                                                                 \
    X(AllocateMemory)                                                               \
    X(CreateSampler)                                                                \
    X(CreateDescriptorPool)                                                         \
    X(CreateDescriptorSetLayout)                                                    \
    X(CreatePipelineLayout)                                                         \
    X(CreateShaderModule)                                                           \
    X(CreateRenderPass)                                                             \
    X(CreateFramebuffer)                                                            \
    X(CreateCommandPool)                                                            \
    X(CreateSemaphore)                                                              \
    X(CreateFence)                                                                  \
    X(BindImageMemory)                                                              \
    X(BindBufferMemory)                                                             \
    X(AllocateDescriptorSets)                                                       \
    X(AllocateCommandBuffers)                                                       \
    X(QueueSubmit)                                                                  \
    X(WaitForFences)                                                                \
    X(BeginCommandBuffer)                                                           \
    X(EndCommandBuffer)                                                             \
    X(CreateGraphicsPipelines)                                                      \
    X(CreateComputePipelines)                                                       \
    X(MapMemory)                                                                    \
    X(AcquireNextImageKHR)                                                          \
    X(QueuePresentKHR)

#define NEXT_FIELD(name) PFN_vk##name name;

typedef struct
{
    void*                                    key;
    VkInstance                               instance;
    PFN_vkGetInstanceProcAddr                gipa;
    PFN_vkGetPhysicalDeviceFormatProperties  GetPhysicalDeviceFormatProperties;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
} InstanceRecord;

typedef struct
{
    void*                   key;
    PFN_vkGetDeviceProcAddr gdpa;
    DEVICE_HOOKS(NEXT_FIELD)
} DeviceRecord;

#define MAX_RECORDS 16
static InstanceRecord g_instances[MAX_RECORDS];
static unsigned       g_instance_count = 0;
static DeviceRecord   g_device_records[MAX_RECORDS];
static unsigned       g_device_record_count = 0;
static int            g_acquired            = 0;
static unsigned long  g_hidden              = 0;

static void* dispatch_key(const void* handle)
{
    return handle ? *(void* const*) handle : NULL;
}

static InstanceRecord* instance_record(const void* handle)
{
    void* const    key   = dispatch_key(handle);
    const unsigned count = __atomic_load_n(&g_instance_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; key && i < count; i++)
        if (g_instances[i].key == key)
            return &g_instances[i];
    return NULL;
}

static DeviceRecord* device_record(const void* handle)
{
    void* const    key   = dispatch_key(handle);
    const unsigned count = __atomic_load_n(&g_device_record_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; key && i < count; i++)
        if (g_device_records[i].key == key)
            return &g_device_records[i];
    return NULL;
}

static int keep_instance(VkInstance instance, PFN_vkGetInstanceProcAddr gipa)
{
    InstanceRecord* record = instance_record(instance);
    if (!record)
    {
        const unsigned slot = __atomic_load_n(&g_instance_count, __ATOMIC_ACQUIRE);
        if (slot >= MAX_RECORDS)
            return 0;
        record = &g_instances[slot];
        memset(record, 0, sizeof(*record));
        record->key      = dispatch_key(instance);
        record->instance = instance;
        record->gipa     = gipa;
        __atomic_store_n(&g_instance_count, slot + 1, __ATOMIC_RELEASE);
        return 1;
    }
    memset(record, 0, sizeof(*record));
    record->key      = dispatch_key(instance);
    record->instance = instance;
    record->gipa     = gipa;
    return 1;
}

static int keep_device(VkDevice device, PFN_vkGetDeviceProcAddr gdpa)
{
    DeviceRecord* record = device_record(device);
    if (!record)
    {
        const unsigned slot = __atomic_load_n(&g_device_record_count, __ATOMIC_ACQUIRE);
        if (slot >= MAX_RECORDS)
            return 0;
        record = &g_device_records[slot];
        memset(record, 0, sizeof(*record));
        record->key  = dispatch_key(device);
        record->gdpa = gdpa;
        __atomic_store_n(&g_device_record_count, slot + 1, __ATOMIC_RELEASE);
        return 1;
    }
    memset(record, 0, sizeof(*record));
    record->key  = dispatch_key(device);
    record->gdpa = gdpa;
    return 1;
}

static DeviceRecord* known_device(const void* handle, const char* function)
{
    DeviceRecord* record = device_record(handle);
    if (!record)
        fprintf(stderr, "fault_inject: %s reached a device this layer never made\n", function);
    return record;
}

static const char* faulted_function(void)
{
    const char* name = getenv("FAULT_INJECT_FUNCTION");
    return name ? name : "";
}

static const char* counting_start(void)
{
    const char* from = getenv("FAULT_INJECT_FROM");
    return from && *from ? from : "start";
}

static VkLayerDeviceLink* g_refused_link = NULL;
static VkDevice           g_devices[8];
static unsigned           g_device_count = 0;

static int on_target_device(VkDevice d)
{
    const char* want = getenv("FAULT_INJECT_DEVICE");
    if (!want || !*want)
        return 1;
    const unsigned long n     = strtoul(want, NULL, 10);
    const unsigned      count = __atomic_load_n(&g_device_count, __ATOMIC_SEQ_CST);
    return n >= 1 && n <= count && g_devices[n - 1] == d;
}

static int refusing(void)
{
    const char* refuse = getenv("FAULT_INJECT_REFUSE");
    return refuse && *refuse;
}

static int called_by_target(const void* address, const char* function, const char* what)
{
    const char* target = getenv("FAULT_INJECT_CALLER");
    Dl_info     info;
    if (!target || !*target || !dladdr(address, &info) || !info.dli_fname || !strstr(info.dli_fname, target))
        return 0;
    if (!strcmp(counting_start(), "acquire") && !__atomic_load_n(&g_acquired, __ATOMIC_SEQ_CST))
        return 0;
    static unsigned long calls = 0;
    const unsigned long  call  = __atomic_add_fetch(&calls, 1, __ATOMIC_SEQ_CST);
    const char*          nth   = getenv("FAULT_INJECT_NTH");
    if (nth && *nth && strtoul(nth, NULL, 10) != call)
        return 0;
    fprintf(stderr, "fault_inject: %s %s called from %s\n", what, function, info.dli_fname);
    return 1;
}

#define FAULT(name, handle, params, args, clear)                                    \
    static VKAPI_ATTR VkResult VKAPI_CALL fault_##name params                       \
    {                                                                               \
        DeviceRecord* record = known_device(handle, "vk" #name);                    \
        if (!record)                                                                \
            return VK_ERROR_DEVICE_LOST;                                            \
        if (called_by_target(__builtin_return_address(0), "vk" #name, "failed"))    \
        {                                                                           \
            clear;                                                                  \
            return VK_ERROR_OUT_OF_HOST_MEMORY;                                     \
        }                                                                           \
        return record->name args;                                                   \
    }

#define CREATE(name, info, handle)                                                  \
    FAULT(name, d, (VkDevice d, const info* i, const VkAllocationCallbacks* a, handle* o), (d, i, a, o), (void) 0)

CREATE(CreateImage, VkImageCreateInfo, VkImage)
CREATE(CreateImageView, VkImageViewCreateInfo, VkImageView)
CREATE(CreateBuffer, VkBufferCreateInfo, VkBuffer)
CREATE(AllocateMemory, VkMemoryAllocateInfo, VkDeviceMemory)
CREATE(CreateSampler, VkSamplerCreateInfo, VkSampler)
CREATE(CreateDescriptorPool, VkDescriptorPoolCreateInfo, VkDescriptorPool)
CREATE(CreateDescriptorSetLayout, VkDescriptorSetLayoutCreateInfo, VkDescriptorSetLayout)
CREATE(CreatePipelineLayout, VkPipelineLayoutCreateInfo, VkPipelineLayout)
CREATE(CreateShaderModule, VkShaderModuleCreateInfo, VkShaderModule)
CREATE(CreateRenderPass, VkRenderPassCreateInfo, VkRenderPass)
CREATE(CreateFramebuffer, VkFramebufferCreateInfo, VkFramebuffer)
CREATE(CreateCommandPool, VkCommandPoolCreateInfo, VkCommandPool)
CREATE(CreateSemaphore, VkSemaphoreCreateInfo, VkSemaphore)
CREATE(CreateFence, VkFenceCreateInfo, VkFence)

FAULT(BindImageMemory, d, (VkDevice d, VkImage im, VkDeviceMemory m, VkDeviceSize off), (d, im, m, off), (void) 0)
FAULT(BindBufferMemory, d, (VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize off), (d, b, m, off), (void) 0)
FAULT(AllocateDescriptorSets,
      d,
      (VkDevice d, const VkDescriptorSetAllocateInfo* i, VkDescriptorSet* o),
      (d, i, o),
      memset(o, 0, sizeof(*o) * i->descriptorSetCount))
FAULT(AllocateCommandBuffers,
      d,
      (VkDevice d, const VkCommandBufferAllocateInfo* i, VkCommandBuffer* o),
      (d, i, o),
      memset(o, 0, sizeof(*o) * i->commandBufferCount))
FAULT(QueueSubmit, q, (VkQueue q, uint32_t n, const VkSubmitInfo* s, VkFence f), (q, n, s, f), (void) 0)
static VKAPI_ATTR VkResult VKAPI_CALL fault_WaitForFences(VkDevice d, uint32_t n, const VkFence* f, VkBool32 all, uint64_t t)
{
    DeviceRecord* record = known_device(d, "vkWaitForFences");
    if (!record)
        return VK_ERROR_DEVICE_LOST;
    if (on_target_device(d) && called_by_target(__builtin_return_address(0), "vkWaitForFences", "failed"))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    return record->WaitForFences(d, n, f, all, t);
}
FAULT(BeginCommandBuffer, c, (VkCommandBuffer c, const VkCommandBufferBeginInfo* i), (c, i), (void) 0)
FAULT(EndCommandBuffer, c, (VkCommandBuffer c), (c), (void) 0)
FAULT(CreateGraphicsPipelines,
      d,
      (VkDevice d, VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo* i, const VkAllocationCallbacks* a, VkPipeline* o),
      (d, c, n, i, a, o),
      memset(o, 0, sizeof(*o) * n))
FAULT(CreateComputePipelines,
      d,
      (VkDevice d, VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo* i, const VkAllocationCallbacks* a, VkPipeline* o),
      (d, c, n, i, a, o),
      memset(o, 0, sizeof(*o) * n))

static int poisoning(void)
{
    const char* poison = getenv("FAULT_INJECT_POISON");
    return poison && *poison;
}

static VKAPI_ATTR VkResult VKAPI_CALL fault_MapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize off, VkDeviceSize size,
                                                      VkMemoryMapFlags flags, void** data)
{
    DeviceRecord* record = known_device(d, "vkMapMemory");
    if (!record)
        return VK_ERROR_DEVICE_LOST;
    if (!poisoning())
    {
        if (called_by_target(__builtin_return_address(0), "vkMapMemory", "failed"))
            return VK_ERROR_MEMORY_MAP_FAILED;
        return record->MapMemory(d, m, off, size, flags, data);
    }
    const VkResult r = record->MapMemory(d, m, off, size, flags, data);
    if (r == VK_SUCCESS && size != VK_WHOLE_SIZE && called_by_target(__builtin_return_address(0), "vkMapMemory", "poisoned"))
        memset(*data, 0xC3, (size_t) size);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL mark_AcquireNextImageKHR(VkDevice d, VkSwapchainKHR s, uint64_t t, VkSemaphore sem,
                                                               VkFence f, uint32_t* i)
{
    DeviceRecord* record = known_device(d, "vkAcquireNextImageKHR");
    if (!record)
        return VK_ERROR_DEVICE_LOST;
    const char* arm = getenv("FAULT_INJECT_ARM");
    if (!arm || !*arm || !access(arm, F_OK))
        __atomic_store_n(&g_acquired, 1, __ATOMIC_SEQ_CST);
    return record->AcquireNextImageKHR(d, s, t, sem, f, i);
}

static int counting_presents(void)
{
    const char* count = getenv("FAULT_INJECT_COUNT_PRESENTS");
    return count && *count;
}

static unsigned long   g_presents      = 0;
static struct timespec g_first_present = {0, 0};
static struct timespec g_last_present  = {0, 0};

static VKAPI_ATTR VkResult VKAPI_CALL count_QueuePresentKHR(VkQueue q, const VkPresentInfoKHR* p)
{
    DeviceRecord* record = known_device(q, "vkQueuePresentKHR");
    if (!record)
        return VK_ERROR_DEVICE_LOST;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (__atomic_add_fetch(&g_presents, 1, __ATOMIC_SEQ_CST) == 1)
        g_first_present = now;
    g_last_present = now;
    if (!strcmp(getenv("FAULT_INJECT_COUNT_PRESENTS"), "each"))
        fprintf(stderr, "fault_inject: present %lld\n", (long long) now.tv_sec * 1000000000LL + now.tv_nsec);
    return record->QueuePresentKHR(q, p);
}

__attribute__((destructor)) static void report_presents(void)
{
    if (!counting_presents())
        return;
    const double span = (double) (g_last_present.tv_sec - g_first_present.tv_sec) * 1000.0
                      + (double) (g_last_present.tv_nsec - g_first_present.tv_nsec) / 1000000.0;
    fprintf(stderr, "fault_inject: counted %lu presents over %.3f ms\n", __atomic_load_n(&g_presents, __ATOMIC_SEQ_CST), span);
}

static VKAPI_ATTR VkResult VKAPI_CALL fault_CreateInstance(const VkInstanceCreateInfo*  info,
                                                           const VkAllocationCallbacks* alloc,
                                                           VkInstance*                  inst)
{
    VkLayerInstanceCreateInfo* lci = (VkLayerInstanceCreateInfo*) info->pNext;
    while (lci && (lci->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || lci->function != VK_LAYER_LINK_INFO))
        lci = (VkLayerInstanceCreateInfo*) lci->pNext;
    if (!lci)
        return VK_ERROR_INITIALIZATION_FAILED;

    const PFN_vkGetInstanceProcAddr gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    lci->u.pLayerInfo                    = lci->u.pLayerInfo->pNext;

    PFN_vkCreateInstance create = (PFN_vkCreateInstance) gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult             r      = create(info, alloc, inst);
    if (r != VK_SUCCESS)
        return r;
    if (!keep_instance(*inst, gipa))
    {
        fprintf(stderr, "fault_inject: more than %d instances, so this one is refused\n", MAX_RECORDS);
        PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance) gipa(*inst, "vkDestroyInstance");
        if (destroy)
            destroy(*inst, alloc);
        *inst = VK_NULL_HANDLE;
        return VK_ERROR_TOO_MANY_OBJECTS;
    }
    return r;
}

static const char* hidden_extension(void)
{
    const char* name = getenv("FAULT_INJECT_HIDE_EXTENSION");
    return name ? name : "";
}

static int hidden_linear_filter_format(void)
{
    const char* format = getenv("FAULT_INJECT_HIDE_LINEAR_FILTER");
    return format ? atoi(format) : 0;
}

static VKAPI_ATTR void VKAPI_CALL hide_GetPhysicalDeviceFormatProperties(VkPhysicalDevice phys, VkFormat format, VkFormatProperties* props)
{
    InstanceRecord* record = instance_record(phys);
    if (!record)
    {
        fprintf(stderr, "fault_inject: vkGetPhysicalDeviceFormatProperties reached an instance this layer never made\n");
        memset(props, 0, sizeof(*props));
        return;
    }
    record->GetPhysicalDeviceFormatProperties(phys, format, props);
    if ((int) format == hidden_linear_filter_format() && (props->optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        props->optimalTilingFeatures &= ~VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        fprintf(stderr, "fault_inject: hid linear filtering of format %d\n", (int) format);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL hide_EnumerateDeviceExtensionProperties(VkPhysicalDevice       phys,
                                                                              const char*            layer,
                                                                              uint32_t*              count,
                                                                              VkExtensionProperties* props)
{
    InstanceRecord* record = instance_record(phys);
    if (!record)
    {
        fprintf(stderr, "fault_inject: vkEnumerateDeviceExtensionProperties reached an instance this layer never made\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkEnumerateDeviceExtensionProperties next = record->EnumerateDeviceExtensionProperties;
    if (layer)
        return next(phys, layer, count, props);
    uint32_t all = 0;
    VkResult r   = next(phys, NULL, &all, NULL);
    if (r != VK_SUCCESS)
        return r;
    VkExtensionProperties* every = calloc(all ? all : 1, sizeof(*every));
    if (!every)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    r = next(phys, NULL, &all, every);
    if (r < 0)
    {
        free(every);
        return r;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < all; i++)
        if (strcmp(every[i].extensionName, hidden_extension()))
            every[kept++] = every[i];
    if (kept < all)
        __atomic_add_fetch(&g_hidden, 1, __ATOMIC_SEQ_CST);
    if (!props)
    {
        *count = kept;
        free(every);
        return VK_SUCCESS;
    }
    const uint32_t copied = kept < *count ? kept : *count;
    memcpy(props, every, copied * sizeof(*every));
    *count = copied;
    free(every);
    return copied < kept ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL fault_CreateDevice(VkPhysicalDevice             phys,
                                                         const VkDeviceCreateInfo*    info,
                                                         const VkAllocationCallbacks* alloc,
                                                         VkDevice*                    dev)
{
    for (uint32_t i = 0; *hidden_extension() && i < info->enabledExtensionCount; i++)
        if (!strcmp(info->ppEnabledExtensionNames[i], hidden_extension()))
        {
            fprintf(stderr, "fault_inject: refused a device that enables the hidden %s\n", hidden_extension());
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    VkLayerDeviceCreateInfo* ldi = (VkLayerDeviceCreateInfo*) info->pNext;
    while (ldi && (ldi->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || ldi->function != VK_LAYER_LINK_INFO))
        ldi = (VkLayerDeviceCreateInfo*) ldi->pNext;
    if (!ldi)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (strcmp(counting_start(), "start") && strcmp(counting_start(), "acquire"))
    {
        fprintf(stderr, "fault_inject: FAULT_INJECT_FROM=%s is neither start nor acquire\n", counting_start());
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const char* arm = getenv("FAULT_INJECT_ARM");
    if (arm && *arm && strcmp(counting_start(), "acquire"))
    {
        fprintf(stderr, "fault_inject: FAULT_INJECT_ARM needs FAULT_INJECT_FROM=acquire\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (poisoning() && strcmp(faulted_function(), "vkMapMemory"))
    {
        fprintf(stderr, "fault_inject: FAULT_INJECT_POISON needs FAULT_INJECT_FUNCTION=vkMapMemory\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (refusing() && strcmp(faulted_function(), "vkCreateDevice"))
    {
        fprintf(stderr, "fault_inject: FAULT_INJECT_REFUSE needs FAULT_INJECT_FUNCTION=vkCreateDevice\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const InstanceRecord* instance = instance_record(phys);
    if (!instance)
    {
        fprintf(stderr, "fault_inject: vkCreateDevice reached an instance this layer never made\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkLayerDeviceLink* const entryLink = ldi->u.pLayerInfo;
    if (g_refused_link && entryLink != g_refused_link)
    {
        fprintf(stderr, "fault_inject: vkCreateDevice came back on a moved chain link\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    g_refused_link                     = NULL;
    const PFN_vkGetInstanceProcAddr gipa = ldi->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr   gdpa = ldi->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    ldi->u.pLayerInfo                    = ldi->u.pLayerInfo->pNext;

    PFN_vkCreateDevice create = (PFN_vkCreateDevice) gipa(instance->instance, "vkCreateDevice");
    if (refusing() && called_by_target(__builtin_return_address(0), "vkCreateDevice", "failed"))
    {
        g_refused_link = entryLink;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult r = create(phys, info, alloc, dev);
    if (r == VK_SUCCESS && !strcmp(faulted_function(), "vkCreateDevice")
        && called_by_target(__builtin_return_address(0), "vkCreateDevice", "failed"))
    {
        PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice) gdpa(*dev, "vkDestroyDevice");
        if (destroy)
            destroy(*dev, alloc);
        *dev = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (r != VK_SUCCESS)
        return r;
    if (!keep_device(*dev, gdpa))
    {
        fprintf(stderr, "fault_inject: more than %d devices, so this one is refused\n", MAX_RECORDS);
        PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice) gdpa(*dev, "vkDestroyDevice");
        if (destroy)
            destroy(*dev, alloc);
        *dev = VK_NULL_HANDLE;
        return VK_ERROR_TOO_MANY_OBJECTS;
    }
    fprintf(stderr, "fault_inject: armed for %s\n", *faulted_function() ? faulted_function() : "nothing");
    if (*hidden_extension())
        fprintf(stderr, "fault_inject: hid %s from %lu extension lists\n", hidden_extension(), __atomic_load_n(&g_hidden, __ATOMIC_SEQ_CST));
    const unsigned slot = __atomic_load_n(&g_device_count, __ATOMIC_SEQ_CST);
    if (slot < sizeof(g_devices) / sizeof(g_devices[0]))
    {
        g_devices[slot] = *dev;
        __atomic_store_n(&g_device_count, slot + 1, __ATOMIC_SEQ_CST);
    }
    return r;
}

#define ROUTE(name, hook)                                                           \
    if (!strcmp(pName, "vk" #name))                                                 \
    {                                                                               \
        record->name = (PFN_vk##name) record->gdpa(device, pName);                  \
        return record->name ? (PFN_vkVoidFunction) hook##_##name : NULL;            \
    }

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fault_GetDeviceProcAddr(VkDevice device, const char* pName)
{
    if (!strcmp(pName, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction) fault_GetDeviceProcAddr;
    DeviceRecord* const record = device_record(device);
    if (!record)
        return NULL;
    ROUTE(AcquireNextImageKHR, mark)
    if (counting_presents())
        ROUTE(QueuePresentKHR, count)
    if (strcmp(pName, faulted_function()))
        return record->gdpa(device, pName);
    ROUTE(MapMemory, fault)
    ROUTE(CreateImage, fault)
    ROUTE(CreateImageView, fault)
    ROUTE(CreateBuffer, fault)
    ROUTE(AllocateMemory, fault)
    ROUTE(CreateSampler, fault)
    ROUTE(CreateDescriptorPool, fault)
    ROUTE(CreateDescriptorSetLayout, fault)
    ROUTE(CreatePipelineLayout, fault)
    ROUTE(CreateShaderModule, fault)
    ROUTE(CreateRenderPass, fault)
    ROUTE(CreateFramebuffer, fault)
    ROUTE(CreateCommandPool, fault)
    ROUTE(CreateSemaphore, fault)
    ROUTE(CreateFence, fault)
    ROUTE(BindImageMemory, fault)
    ROUTE(BindBufferMemory, fault)
    ROUTE(AllocateDescriptorSets, fault)
    ROUTE(AllocateCommandBuffers, fault)
    ROUTE(QueueSubmit, fault)
    ROUTE(WaitForFences, fault)
    ROUTE(BeginCommandBuffer, fault)
    ROUTE(EndCommandBuffer, fault)
    ROUTE(CreateGraphicsPipelines, fault)
    ROUTE(CreateComputePipelines, fault)
    fprintf(stderr, "fault_inject: %s cannot be faulted\n", pName);
    return record->gdpa(device, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fault_GetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if (!strcmp(pName, "vkCreateInstance"))
        return (PFN_vkVoidFunction) fault_CreateInstance;
    if (!strcmp(pName, "vkCreateDevice"))
        return (PFN_vkVoidFunction) fault_CreateDevice;
    if (!strcmp(pName, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction) fault_GetInstanceProcAddr;
    if (!strcmp(pName, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction) fault_GetDeviceProcAddr;
    InstanceRecord* const record = instance_record(instance);
    if (!record)
        return NULL;
    if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties") && hidden_linear_filter_format())
    {
        record->GetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties) record->gipa(instance, pName);
        return record->GetPhysicalDeviceFormatProperties ? (PFN_vkVoidFunction) hide_GetPhysicalDeviceFormatProperties : NULL;
    }
    if (!strcmp(pName, "vkEnumerateDeviceExtensionProperties") && *hidden_extension())
    {
        record->EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties) record->gipa(instance, pName);
        return record->EnumerateDeviceExtensionProperties ? (PFN_vkVoidFunction) hide_EnumerateDeviceExtensionProperties : NULL;
    }
    return record->gipa(instance, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v)
{
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || v->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion  = 2;
    v->pfnGetInstanceProcAddr       = fault_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr         = fault_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
