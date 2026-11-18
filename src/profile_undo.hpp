#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vkBasalt
{
    inline constexpr size_t profileUndoDepth = 50;

    std::string profileUndoWords(const std::string& now, const std::string& before);
    void        rememberProfileState(std::vector<std::string>& undo, const std::string& before, const std::string& after);
    std::string readProfileText(const std::string& path);
} // namespace vkBasalt
