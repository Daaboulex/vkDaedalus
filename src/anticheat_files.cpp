#include "anticheat_files.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <filesystem>

#include <unistd.h>

#include "anticheat_rules.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    namespace
    {
        constexpr size_t entryLimitForGames = 100000;

        std::string trimmed(std::string_view text)
        {
            const size_t start = text.find_first_not_of(" \t\r");
            if (start == std::string_view::npos)
                return "";
            const size_t end = text.find_last_not_of(" \t\r");
            return std::string(text.substr(start, end - start + 1));
        }

        std::string lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        bool isBinaryFolder(const std::string& name)
        {
            static const std::array<const char*, 10> binaryFolders = {"bin", "bin32", "bin64", "x64", "x86", "win32", "win64", "binaries",
                                                                      "shipping", "retail"};
            return std::find(binaryFolders.begin(), binaryFolders.end(), lowered(name)) != binaryFolders.end();
        }
    }

    AntiCheatRules parseAntiCheatRules(std::string_view ini)
    {
        AntiCheatRules parsed;
        size_t         start = 0;
        while (start <= ini.size())
        {
            const size_t      end  = std::min(ini.find('\n', start), ini.size());
            const std::string line = trimmed(ini.substr(start, end - start));
            start                  = end + 1;
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            const size_t equals = line.find('=');
            if (equals == std::string::npos)
            {
                parsed.refused.push_back(line);
                continue;
            }
            std::string name = trimmed(std::string_view(line).substr(0, equals));
            if (name.size() > 2 && name.compare(name.size() - 2, 2, "[]") == 0)
                name.resize(name.size() - 2);
            const std::string pattern = trimmed(std::string_view(line).substr(equals + 1));
            try
            {
                parsed.rules.push_back({name, std::regex(pattern, std::regex::ECMAScript | std::regex::icase | std::regex::optimize)});
            }
            catch (const std::regex_error&)
            {
                parsed.refused.push_back(line);
            }
        }
        return parsed;
    }

    const AntiCheatRules& shippedAntiCheatRules()
    {
        static const AntiCheatRules rules = parseAntiCheatRules(shippedAntiCheatRulesIni);
        return rules;
    }

    std::string gameFolderFor(const std::string& executable)
    {
        const std::filesystem::path path(executable);
        const std::string           text   = path.generic_string();
        const std::string           steam  = "/steamapps/common/";
        const size_t                common = text.find(steam);
        if (common != std::string::npos)
        {
            const size_t end = text.find('/', common + steam.size());
            if (end != std::string::npos)
                return text.substr(0, end);
        }

        std::filesystem::path folder = path.parent_path();
        const std::string     platform = lowered(folder.filename().string());
        if ((platform == "win64" || platform == "win32" || platform == "linux") && lowered(folder.parent_path().filename().string()) == "binaries")
            return folder.parent_path().parent_path().parent_path().string();
        for (int up = 0; up < 3 && isBinaryFolder(folder.filename().string()) && folder.has_parent_path() && folder != folder.parent_path(); up++)
            folder = folder.parent_path();
        return folder.string();
    }

    std::string unixPathOfWindowsPath(const std::string& windowsPath, const std::string& winePrefix)
    {
        std::error_code ec;
        if (!windowsPath.empty() && windowsPath[0] == '/')
        {
            const std::filesystem::path real = std::filesystem::canonical(windowsPath, ec);
            return ec ? "" : real.string();
        }
        if (windowsPath.size() < 3 || windowsPath[1] != ':' || !std::isalpha(static_cast<unsigned char>(windowsPath[0])) || winePrefix.empty())
            return "";
        std::string rest = windowsPath.substr(2);
        std::replace(rest.begin(), rest.end(), '\\', '/');
        const char        drive = static_cast<char>(std::tolower(static_cast<unsigned char>(windowsPath[0])));
        const std::string onDrive = winePrefix + "/dosdevices/" + drive + ":" + rest;
        const std::filesystem::path real = std::filesystem::canonical(onDrive, ec);
        return ec ? "" : real.string();
    }

    std::optional<ShippedAntiCheat> findShippedAntiCheat(const std::string& folder, const std::vector<AntiCheatRule>& rules, size_t entryLimit,
                                                         size_t& scanned)
    {
        scanned = 0;
        std::error_code ec;
        const std::string root = std::filesystem::path(folder).generic_string();
        std::filesystem::recursive_directory_iterator entry(folder, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator done;
        for (; !ec && entry != done; entry.increment(ec))
        {
            if (++scanned > entryLimit)
                return std::nullopt;
            std::string relative = entry->path().generic_string();
            relative             = relative.size() > root.size() ? relative.substr(root.size() + (root.back() == '/' ? 0 : 1)) : relative;
            std::error_code typeError;
            if (entry->is_directory(typeError))
                relative += '/';
            for (const AntiCheatRule& rule : rules)
                if (std::regex_search(relative, rule.pattern))
                    return ShippedAntiCheat{rule.name, relative};
        }
        return std::nullopt;
    }

    std::string winePrefixOfThisProcess()
    {
        const char* prefix = std::getenv("WINEPREFIX");
        const char* home   = std::getenv("HOME");
        return prefix && *prefix ? prefix : (home && *home ? std::string(home) + "/.wine" : "");
    }

    std::string programExecutable(std::string& problem)
    {
        char    buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
        {
            problem = "the layer cannot read its program's path";
            return "";
        }
        buf[len] = '\0';
        const std::string wine = ConfigSerializer::wineExecutableFrom(buf, ConfigSerializer::processArguments());
        if (wine.empty())
            return buf;
        const std::string executable = unixPathOfWindowsPath(wine, winePrefixOfThisProcess());
        if (executable.empty())
            problem = "the Wine program " + wine + " was not found under its prefix";
        return executable;
    }

    bool inSystemPlace(const std::string& path)
    {
        static const std::array<const char*, 8> systemPlaces = {"/usr/", "/bin/", "/sbin/", "/lib/", "/lib64/", "/nix/store/",
                                                               "/run/current-system/", "/snap/"};
        for (const char* place : systemPlaces)
            if (path.rfind(place, 0) == 0)
                return true;
        return false;
    }

    std::optional<ShippedAntiCheat> detectShippedAntiCheat()
    {
        std::string       problem;
        const std::string executable = programExecutable(problem);
        if (executable.empty())
        {
            Logger::warn(problem + ", so no game folder is checked for an anti-cheat");
            return std::nullopt;
        }
        if (inSystemPlace(executable))
        {
            Logger::debug(executable + " is a system program, so no game folder is checked for an anti-cheat");
            return std::nullopt;
        }

        const AntiCheatRules& rules = shippedAntiCheatRules();
        if (!rules.refused.empty())
            Logger::err(std::to_string(rules.refused.size()) + " anti-cheat file rules could not be read, first: " + rules.refused.front());
        const std::string folder  = gameFolderFor(executable);
        size_t            scanned = 0;
        const auto        found   = findShippedAntiCheat(folder, rules.rules, entryLimitForGames, scanned);
        if (found)
            Logger::info(found->name + " ships with this game (" + folder + "/" + found->file + ")");
        else if (scanned > entryLimitForGames)
            Logger::info("the game folder " + folder + " holds more than " + std::to_string(entryLimitForGames)
                         + " entries, so the anti-cheat check stopped without a finding");
        else
            Logger::debug("no anti-cheat file in " + folder + " (" + std::to_string(scanned) + " entries)");
        return found;
    }
}
