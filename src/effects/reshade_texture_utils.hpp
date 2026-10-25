#ifndef RESHADE_TEXTURE_UTILS_HPP_INCLUDED
#define RESHADE_TEXTURE_UTILS_HPP_INCLUDED

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "reshade/effect_module.hpp"
#include "stb_image_resize2.h"

namespace vkBasalt
{
    inline bool hasReshadeTextureAnnotation(const reshadefx::texture& texture,
                                            std::string_view name)
    {
        return std::any_of(texture.annotations.begin(), texture.annotations.end(),
                           [name](const reshadefx::annotation& annotation) {
                               return annotation.name == name;
                           });
    }

    inline bool isGeneratedSharedReshadeTexture(const reshadefx::texture& texture)
    {
        return texture.semantic.empty() && !hasReshadeTextureAnnotation(texture, "source");
    }

    inline std::vector<uint8_t> textureUploadPixels(std::span<const uint8_t> decoded,
                                                    uint32_t                 width,
                                                    uint32_t                 height,
                                                    uint32_t                 decodedChannels,
                                                    uint32_t                 uploadChannels,
                                                    uint32_t                 targetWidth,
                                                    uint32_t                 targetHeight)
    {
        if (targetWidth == 0 || targetHeight == 0 || uploadChannels > decodedChannels
            || decoded.size() != static_cast<size_t>(width) * height * decodedChannels)
            return {};
        stbir_pixel_layout layout;
        switch (uploadChannels)
        {
            case 1: layout = STBIR_1CHANNEL; break;
            case 2: layout = STBIR_2CHANNEL; break;
            case 4: layout = STBIR_4CHANNEL; break;
            default: return {};
        }
        const size_t         pixelCount = static_cast<size_t>(width) * height;
        std::vector<uint8_t> picked(pixelCount * uploadChannels);
        for (size_t pixel = 0; pixel < pixelCount; pixel++)
            std::copy_n(decoded.begin() + pixel * decodedChannels, uploadChannels, picked.begin() + pixel * uploadChannels);
        if (width == targetWidth && height == targetHeight)
            return picked;
        std::vector<uint8_t> resized(static_cast<size_t>(targetWidth) * targetHeight * uploadChannels);
        if (stbir_resize_uint8_linear(picked.data(), static_cast<int>(width), static_cast<int>(height), 0, resized.data(),
                                      static_cast<int>(targetWidth), static_cast<int>(targetHeight), 0, layout)
            == nullptr)
            return {};
        return resized;
    }
}

#endif
