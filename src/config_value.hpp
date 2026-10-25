#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <type_traits>

namespace vkBasalt
{
    template<typename Number>
    bool parseWhole(const std::string& text, Number& out)
    {
        const char* first = text.data();
        const char* last  = first + text.size();
        if constexpr (std::is_floating_point_v<Number>)
        {
            if (last != first && *(last - 1) == 'f')
                --last;
        }
        Number value{};
        const auto [end, error] = std::from_chars(first, last, value);
        if (error != std::errc() || end != last || first == last)
            return false;
        if constexpr (std::is_floating_point_v<Number>)
        {
            if (!std::isfinite(value))
                return false;
        }
        out = value;
        return true;
    }

    inline bool parseBool(const std::string& text, bool& out)
    {
        if (text == "True" || text == "true" || text == "1" || text == "on")
            out = true;
        else if (text == "False" || text == "false" || text == "0" || text == "off")
            out = false;
        else
            return false;
        return true;
    }
}
