#include "format_features.hpp"

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
    const VkFormatFeatureFlags filterable = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const VkFormatFeatureFlags unfiltered = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

    expect(filterForFeatures(VK_FORMAT_R8G8B8A8_UNORM, filterable) == VK_FILTER_LINEAR, "a format the GPU filters is filtered");
    expect(filterForFeatures(VK_FORMAT_R32_SFLOAT, unfiltered) == VK_FILTER_NEAREST,
           "a float format the GPU cannot filter is sampled from the nearest texel");
    expect(filterForFeatures(VK_FORMAT_R32_SINT, filterable) == VK_FILTER_NEAREST,
           "an integer format takes the nearest texel even where the GPU claims linear filtering");
    expect(filterForFeatures(VK_FORMAT_R32G32B32A32_UINT, filterable) == VK_FILTER_NEAREST, "an unsigned integer format takes the nearest texel");

    expect(blitsMipLevels(VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT), "a format the GPU blits both ways makes mip levels");
    expect(!blitsMipLevels(VK_FORMAT_FEATURE_BLIT_SRC_BIT), "a format the GPU cannot blit into makes no mip levels");
    expect(!blitsMipLevels(VK_FORMAT_FEATURE_BLIT_DST_BIT), "a format the GPU cannot blit from makes no mip levels");

    expect(formatTexelBytes(VK_FORMAT_B8G8R8A8_SRGB) == 4 && formatTexelBytes(VK_FORMAT_A2B10G10R10_UNORM_PACK32) == 4,
           "an 8-bit or 10-bit packed swapchain format is four bytes a texel");
    expect(formatTexelBytes(VK_FORMAT_R16G16B16A16_SFLOAT) == 8, "a 16-bit float swapchain format is eight bytes a texel");
    expect(formatTexelBytes(VK_FORMAT_R5G6B5_UNORM_PACK16) == 0 && formatTexelBytes(VK_FORMAT_UNDEFINED) == 0,
           "a format whose texel size the hand-off does not know is refused, not guessed");

    if (failures != 0)
        return 1;
    std::printf("OK: each format is filtered and mipped only as the GPU allows\n");
    return 0;
}
