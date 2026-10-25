#pragma once

#include <cstddef>
#include <string>

namespace vkBasalt
{
    template <typename Width>
    std::string elideStart(const std::string& text, float room, const Width& width)
    {
        if (width(text) <= room)
            return text;
        size_t      from = 0;
        std::string shown;
        do
        {
            do
                from++;
            while (from < text.size() && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80);
            shown = "..." + text.substr(from);
        } while (from < text.size() && width(shown) > room);
        return shown;
    }
}
