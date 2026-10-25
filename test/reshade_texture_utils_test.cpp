#include <cstdint>
#include <cstdio>
#include <vector>

#include "effects/reshade_texture_utils.hpp"

using namespace vkBasalt;

static bool everyPixelIs(const std::vector<uint8_t>& pixels, const std::vector<uint8_t>& expected)
{
    if (pixels.empty() || pixels.size() % expected.size() != 0)
        return false;
    for (size_t i = 0; i < pixels.size(); i++)
        if (pixels[i] != expected[i % expected.size()])
            return false;
    return true;
}

static std::vector<uint8_t> solidImage(uint32_t width, uint32_t height, const std::vector<uint8_t>& pixel)
{
    std::vector<uint8_t> image;
    for (uint32_t i = 0; i < width * height; i++)
        image.insert(image.end(), pixel.begin(), pixel.end());
    return image;
}

static int checkUploadPixels()
{
    const std::vector<uint8_t> rgba = {10, 20, 30, 40};
    const std::vector<uint8_t> decoded = solidImage(2, 2, rgba);

    std::vector<uint8_t> same = textureUploadPixels(decoded, 2, 2, 4, 4, 2, 2);
    if (same.size() != 2 * 2 * 4 || !everyPixelIs(same, rgba))
        return std::puts("an RGBA texture at its own size must upload unchanged"), 1;

    std::vector<uint8_t> grown = textureUploadPixels(decoded, 2, 2, 4, 4, 8, 8);
    if (grown.size() != 8 * 8 * 4 || !everyPixelIs(grown, rgba))
        return std::puts("an RGBA texture resized up must fill the declared extent with the same colour"), 1;

    std::vector<uint8_t> shrunk = textureUploadPixels(solidImage(8, 8, rgba), 8, 8, 4, 4, 2, 2);
    if (shrunk.size() != 2 * 2 * 4 || !everyPixelIs(shrunk, rgba))
        return std::puts("an RGBA texture resized down must fill the declared extent with the same colour"), 1;

    std::vector<uint8_t> redGreen = textureUploadPixels(decoded, 2, 2, 4, 2, 2, 2);
    if (redGreen.size() != 2 * 2 * 2 || !everyPixelIs(redGreen, {10, 20}))
        return std::puts("an R8G8 texture must take the red and green channels of every pixel"), 1;

    std::vector<uint8_t> redGreenGrown = textureUploadPixels(decoded, 2, 2, 4, 2, 16, 16);
    if (redGreenGrown.size() != 16 * 16 * 2 || !everyPixelIs(redGreenGrown, {10, 20}))
        return std::puts("an R8G8 texture smaller than its declared extent must compact, then resize"), 1;

    if (!textureUploadPixels(decoded, 2, 2, 4, 4, 0, 4).empty())
        return std::puts("a zero target extent must be refused"), 1;
    if (!textureUploadPixels(decoded, 4, 4, 4, 4, 4, 4).empty())
        return std::puts("a decoded buffer shorter than its stated size must be refused"), 1;
    if (!textureUploadPixels(decoded, 2, 2, 1, 4, 2, 2).empty())
        return std::puts("asking for more channels than were decoded must be refused"), 1;
    if (!textureUploadPixels(decoded, 2, 2, 4, 3, 4, 4).empty())
        return std::puts("an upload channel count no texture format uses must be refused"), 1;
    return 0;
}

int main()
{
    if (checkUploadPixels() != 0)
        return 1;

    reshadefx::texture frameworkTexture;
    frameworkTexture.unique_name = "V__SharedFramework__Albedo";
    if (!isGeneratedSharedReshadeTexture(frameworkTexture))
        return 1;

    reshadefx::texture colorReference;
    colorReference.semantic = "COLOR";
    if (isGeneratedSharedReshadeTexture(colorReference))
        return 1;

    reshadefx::texture depthReference;
    depthReference.semantic = "DEPTH";
    if (isGeneratedSharedReshadeTexture(depthReference))
        return 1;

    reshadefx::texture sourceTexture;
    reshadefx::annotation source;
    source.name = "source";
    sourceTexture.annotations.push_back(source);
    if (isGeneratedSharedReshadeTexture(sourceTexture))
        return 1;

    std::puts("ReShade generated texture sharing: all assertions passed");
    return 0;
}
