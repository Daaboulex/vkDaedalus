#pragma once

#include <array>
#include <string_view>

namespace vkBasalt
{
    struct PaletteName
    {
        std::string_view word;
        std::string_view shown;
    };

    inline constexpr std::array<PaletteName, 9> paletteNames = {{
        {"soft", "Daaboulex, soft"},
        {"vivid", "Daaboulex, vivid"},
        {"violet", "Violet on plain panels"},
        {"blue", "Blue, the original"},
        {"deepblue", "Deep blue"},
        {"steel", "Steel"},
        {"cyan", "Cyan"},
        {"magenta", "Magenta"},
        {"lightblue", "Blue on lighter panels"},
    }};

    inline bool isPaletteWord(std::string_view word)
    {
        for (const PaletteName& name : paletteNames)
            if (name.word == word)
                return true;
        return false;
    }
}
