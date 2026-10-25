#ifndef RESHADE_PASS_UTILS_HPP_INCLUDED
#define RESHADE_PASS_UTILS_HPP_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    inline bool writesReshadeBackBuffer(const reshadefx::pass& pass)
    {
        return pass.cs_entry_point.empty() && pass.render_target_names[0].empty();
    }

    inline std::size_t countReshadeBackBufferWrites(const reshadefx::technique& technique)
    {
        std::size_t count = 0;
        for (const auto& pass : technique.passes)
        {
            if (writesReshadeBackBuffer(pass))
                ++count;
        }
        return count;
    }

    inline std::optional<std::array<uint32_t, 3>> spirvLocalSize(const std::vector<uint32_t>& words)
    {
        constexpr uint32_t opExecutionMode = 16;
        constexpr uint32_t executionModeLocalSize = 17;
        for (std::size_t at = 5; at < words.size();)
        {
            const uint32_t length = words[at] >> 16;
            if (length == 0 || at + length > words.size())
                return std::nullopt;
            if ((words[at] & 0xffffu) == opExecutionMode && length == 6 && words[at + 2] == executionModeLocalSize)
                return std::array<uint32_t, 3>{words[at + 3], words[at + 4], words[at + 5]};
            at += length;
        }
        return std::nullopt;
    }
}

#endif
