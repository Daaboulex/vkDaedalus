#include "effects/device_limits.hpp"

#include <cstdio>
#include <string>

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
    VkPhysicalDeviceLimits limits{};
    limits.maxComputeWorkGroupCount[0]     = 2147483647u;
    limits.maxComputeWorkGroupCount[1]     = 65535u;
    limits.maxComputeWorkGroupCount[2]     = 65535u;
    limits.maxComputeWorkGroupSize[0]      = 1024u;
    limits.maxComputeWorkGroupSize[1]      = 1024u;
    limits.maxComputeWorkGroupSize[2]      = 64u;
    limits.maxComputeWorkGroupInvocations  = 1024u;
    limits.maxSamplerLodBias               = 16.0f;

    expect(dispatchLimitProblem(limits, {1u, 65535u, 1u}).empty(), "a dispatch at the GPU's group count is taken");
    expect(dispatchLimitProblem(limits, {1u, 65536u, 1u}).find("along axis 1") != std::string::npos,
           "a dispatch one group past the GPU's count is refused, naming the axis");
    expect(!dispatchLimitProblem(limits, {1u, 1u, 70000u}).empty(), "a dispatch past the count on the third axis is refused");

    expect(threadGroupLimitProblem(limits, {1024u, 1u, 1u}).empty(), "a thread group at the GPU's size is taken");
    expect(!threadGroupLimitProblem(limits, {1025u, 1u, 1u}).empty(), "a thread group wider than the GPU runs is refused");
    expect(!threadGroupLimitProblem(limits, {32u, 32u, 2u}).empty(),
           "a thread group within each axis but over the GPU's total is refused");

    VkImageFormatProperties properties{};
    properties.maxExtent    = {16384u, 16384u, 1u};
    properties.maxMipLevels = 15u;
    expect(imageLimitProblem(VK_SUCCESS, properties, {16384u, 4u, 1u}, 15u).empty(), "a texture at the GPU's size and levels is taken");
    expect(imageLimitProblem(VK_SUCCESS, properties, {16385u, 4u, 1u}, 1u).find("larger than this GPU makes") != std::string::npos,
           "a texture one texel wider than the GPU makes is refused");
    expect(!imageLimitProblem(VK_SUCCESS, properties, {16u, 16u, 1u}, 16u).empty(), "more mip levels than the GPU makes are refused");
    expect(!imageLimitProblem(VK_ERROR_FORMAT_NOT_SUPPORTED, properties, {16u, 16u, 1u}, 1u).empty(),
           "a texture the GPU cannot make at all is refused");

    expect(heldLodBias(limits, 100.0f) == 16.0f, "a MipLODBias past the GPU's limit is held at it");
    expect(heldLodBias(limits, -100.0f) == -16.0f, "a negative MipLODBias past the limit is held at its negative");
    expect(heldLodBias(limits, 3.0f) == 3.0f, "a MipLODBias within the limit is kept");

    if (failures != 0)
        return 1;
    std::printf("OK: each GPU limit refuses what is past it and takes what is at it\n");
    return 0;
}
