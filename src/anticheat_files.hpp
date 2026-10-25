#pragma once

#include <cstddef>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace vkBasalt
{
    struct AntiCheatRule
    {
        std::string name;
        std::regex  pattern;
    };

    struct ShippedAntiCheat
    {
        std::string name;
        std::string file;
    };

    struct AntiCheatRules
    {
        std::vector<AntiCheatRule> rules;
        std::vector<std::string>   refused;
    };

    AntiCheatRules                  parseAntiCheatRules(std::string_view ini);
    const AntiCheatRules&           shippedAntiCheatRules();
    std::string                     gameFolderFor(const std::string& executable);
    std::string                     unixPathOfWindowsPath(const std::string& windowsPath, const std::string& winePrefix);
    std::string                     winePrefixOfThisProcess();
    std::string                     programExecutable(std::string& problem);
    bool                            inSystemPlace(const std::string& path);
    std::optional<ShippedAntiCheat> findShippedAntiCheat(const std::string& folder, const std::vector<AntiCheatRule>& rules, size_t entryLimit,
                                                         size_t& scanned);
    std::optional<ShippedAntiCheat> detectShippedAntiCheat();
}
