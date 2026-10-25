#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace vkBasalt
{
    inline std::string loweredSearch(std::string_view typed)
    {
        std::string lowered(typed);
        for (char& c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lowered;
    }

    inline bool matchesSearch(std::string_view message, std::string_view lowered)
    {
        return std::search(message.begin(), message.end(), lowered.begin(), lowered.end(),
                           [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b); }) != message.end();
    }
}
