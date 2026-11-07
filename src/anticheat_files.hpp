#pragma once

#include <cstddef>
#include <functional>
#include <future>
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

    struct AntiCheatScan
    {
        std::optional<ShippedAntiCheat> found;
        size_t                          scanned = 0;
        bool                            stopped = false;
    };

    AntiCheatRules                  parseAntiCheatRules(std::string_view ini);
    const AntiCheatRules&           shippedAntiCheatRules();
    std::string                     gameFolderFor(const std::string& executable);
    std::string                     unixPathOfWindowsPath(const std::string& windowsPath, const std::string& winePrefix);
    std::string                     winePrefixOfThisProcess();
    std::string                     programExecutable(std::string& problem);
    bool                            inSystemPlace(const std::string& path);
    AntiCheatScan                   findShippedAntiCheat(const std::string& folder, const std::vector<AntiCheatRule>& rules, size_t entryLimit,
                                                         const std::function<bool()>& stop);
    std::string                     safeAntiCheatLockFor(const std::string& folder, const std::function<bool()>& stop);
    std::shared_future<std::string> checkFolderForAntiCheat(const std::string& folder);
    std::shared_future<std::string> checkForShippedAntiCheat();
}
