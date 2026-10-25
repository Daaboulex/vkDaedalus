#include "layer_queue.hpp"

#include <cstdio>
#include <string>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static VkQueueFamilyProperties family(VkQueueFlags flags, uint32_t queues)
{
    VkQueueFamilyProperties properties = {};
    properties.queueFlags              = flags;
    properties.queueCount              = queues;
    return properties;
}

static VkDeviceQueueCreateInfo asked(uint32_t familyIndex, uint32_t count, const float* priorities)
{
    VkDeviceQueueCreateInfo info = {};
    info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    info.queueFamilyIndex        = familyIndex;
    info.queueCount              = count;
    info.pQueuePriorities        = priorities;
    return info;
}

int main()
{
    const float                                low[2]     = {0.25f, 0.5f};
    const std::vector<VkQueueFamilyProperties> nvidiaLike = {family(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 16),
                                                             family(VK_QUEUE_COMPUTE_BIT, 8)};
    {
        const VkDeviceQueueCreateInfo game[2] = {asked(1, 1, low), asked(0, 2, low)};
        const LayerQueueRequest       request = requestLayerQueue(game, 2, nvidiaLike);
        expect(request.own && request.own->family == 0 && request.own->index == 2,
               "a graphics family with room gives the layer the queue after the game's");
        expect(request.infos[1].queueCount == 3 && request.infos[1].pQueuePriorities == request.priorities.data(),
               "the graphics family is asked for one more queue with the layer's priority list");
        expect(request.priorities.size() == 3 && request.priorities[0] == 0.25f && request.priorities[1] == 0.5f && request.priorities[2] == 0.5f,
               "the game's priorities are kept and the layer's queue takes the game's highest");
        expect(request.infos[0].queueCount == 1 && request.infos[0].pQueuePriorities == low, "a compute family the game asked for is untouched");
    }
    {
        const std::vector<VkQueueFamilyProperties> radvLike = {family(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 1), family(VK_QUEUE_COMPUTE_BIT, 4)};
        const VkDeviceQueueCreateInfo              game[1]  = {asked(0, 1, low)};
        const LayerQueueRequest                    request  = requestLayerQueue(game, 1, radvLike);
        expect(!request.own && request.infos[0].queueCount == 1, "a full graphics family gives the layer no queue and asks for nothing more");
    }
    {
        VkDeviceQueueCreateInfo protectedQueue = asked(0, 1, low);
        protectedQueue.flags                   = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
        const LayerQueueRequest request        = requestLayerQueue(&protectedQueue, 1, nvidiaLike);
        expect(!request.own && request.infos[0].queueCount == 1, "a protected queue family is left as the game asked");
    }
    {
        const VkDeviceQueueCreateInfo game[1] = {asked(1, 1, low)};
        const LayerQueueRequest       request = requestLayerQueue(game, 1, nvidiaLike);
        expect(!request.own, "a game with no graphics queue gives the layer none");
    }
    if (failures != 0)
        return 1;
    std::printf("OK: the layer asks for a queue of its own only in a graphics family with room\n");
    return 0;
}
