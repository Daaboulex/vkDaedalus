#include "logical_swapchain.hpp"

#include <cstdio>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main()
{
    LogicalSwapchain concurrent{};
    {
        uint32_t                    families[2] = {0, 1};
        VkImageFormatListCreateInfo appChain    = {};
        appChain.sType                          = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;

        VkSwapchainCreateInfoKHR info = {};
        info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.pNext                    = &appChain;
        info.imageSharingMode         = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount    = 2;
        info.pQueueFamilyIndices      = families;
        concurrent.keepCreateInfo(info);

        families[0] = 7;
        families[1] = 7;
    }
    const VkSwapchainCreateInfoKHR& kept = concurrent.swapchainCreateInfo;
    expect(kept.pNext == nullptr, "the kept create info points at none of the application's chain, which dies with the call");
    expect(kept.queueFamilyIndexCount == 2 && kept.pQueueFamilyIndices != nullptr && kept.pQueueFamilyIndices[0] == 0
               && kept.pQueueFamilyIndices[1] == 1,
           "the kept queue families survive the application reusing its array");

    LogicalSwapchain exclusive{};
    VkSwapchainCreateInfoKHR info = {};
    info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    info.queueFamilyIndexCount    = 3;
    info.pQueueFamilyIndices      = reinterpret_cast<const uint32_t*>(0x8);
    exclusive.keepCreateInfo(info);
    expect(exclusive.swapchainCreateInfo.queueFamilyIndexCount == 0 && exclusive.swapchainCreateInfo.pQueueFamilyIndices == nullptr,
           "exclusive sharing ignores the queue family array, so none is read or kept");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: the swapchain keeps nothing that points into the application\n");
    return 0;
}
