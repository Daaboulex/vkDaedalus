#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vkBasalt
{
    inline std::optional<uint64_t> processStartTicks(std::string_view stat)
    {
        const size_t comm = stat.rfind(')');
        if (comm == std::string_view::npos)
            return std::nullopt;
        std::string_view rest = stat.substr(comm + 1);
        for (int field = 3; field <= 22; field++)
        {
            const size_t begin = rest.find_first_not_of(' ');
            if (begin == std::string_view::npos)
                return std::nullopt;
            rest.remove_prefix(begin);
            const size_t end = std::min(rest.find_first_of(" \n"), rest.size());
            if (field == 22)
            {
                uint64_t ticks         = 0;
                const auto [at, error] = std::from_chars(rest.data(), rest.data() + end, ticks);
                return error == std::errc() && at == rest.data() + end ? std::optional<uint64_t>(ticks) : std::nullopt;
            }
            rest.remove_prefix(end);
        }
        return std::nullopt;
    }

    inline uint64_t prefixHolderOf(int32_t pid, uint64_t startTicks)
    {
        return startTicks << 32 | static_cast<uint32_t>(pid);
    }

    inline int32_t prefixHolderPid(uint64_t holder)
    {
        return static_cast<int32_t>(holder & 0xFFFFFFFFu);
    }

    inline bool prefixHolderAlive(uint64_t holder, std::optional<std::string_view> statOfItsPid)
    {
        const std::optional<uint64_t> start = statOfItsPid ? processStartTicks(*statOfItsPid) : std::nullopt;
        return start && prefixHolderOf(prefixHolderPid(holder), *start) == holder;
    }
}
