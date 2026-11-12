#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    struct TechniqueInfo
    {
        std::string name;
        std::string label;
        std::string tooltip;
        bool        hidden        = false;
        bool        enabledAlways = false;
        uint32_t    timeoutMs     = 0;

        bool operator==(const TechniqueInfo&) const = default;
    };
}
