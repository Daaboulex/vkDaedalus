#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace vkBasalt
{
    inline constexpr std::array<std::string_view, 4> wineLoaderNames = {"wine64-preloader", "wine-preloader", "wine64", "wine"};

    inline bool isWineLoader(std::string_view fileName)
    {
        return std::find(wineLoaderNames.begin(), wineLoaderNames.end(), fileName) != wineLoaderNames.end();
    }

    inline std::string winePrefixFrom(const char* prefix, const char* home)
    {
        if (prefix != nullptr && *prefix != '\0')
            return prefix;
        return home != nullptr && *home != '\0' ? std::string(home) + "/.wine" : "";
    }
}
