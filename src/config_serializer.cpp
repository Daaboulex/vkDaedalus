#include "config_serializer.hpp"
#include "config_paths.hpp"
#include "config_value.hpp"
#include "logger.hpp"
#include "overlay/hud.hpp"
#include "overlay/palette_names.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <mutex>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <array>
#include <set>
#include <unistd.h>
#include <climits>
#include <fcntl.h>
#include <sys/file.h>

namespace vkBasalt
{
    namespace
    {
        bool replaceFileWith(const std::string& path, const std::string& content)
        {
            const std::string temporary = path + ".vkdaedalus.tmp";
            {
                std::ofstream file(temporary, std::ios::trunc);
                if (!file.is_open())
                {
                    Logger::err("could not open " + temporary + " for writing, so " + path + " is left as it was");
                    return false;
                }
                file << content;
                file.close();
                if (file.fail())
                {
                    Logger::err("writing " + temporary + " failed, so " + path + " is left as it was");
                    std::remove(temporary.c_str());
                    return false;
                }
            }
            if (std::rename(temporary.c_str(), path.c_str()) != 0)
            {
                Logger::err("could not replace " + path + " with " + temporary);
                std::remove(temporary.c_str());
                return false;
            }
            return true;
        }
    }

    namespace
    {
        void trimWhitespace(std::string& s)
        {
            const size_t start = s.find_first_not_of(" \t\r\n");
            const size_t end   = s.find_last_not_of(" \t\r\n");
            s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
        }

        std::mutex                                              ownWritesMutex;
        std::map<std::string, std::filesystem::file_time_type> ownWrites;

        void rememberOwnWrite(const std::string& path)
        {
            std::error_code             mtimeError;
            const auto                  written = std::filesystem::last_write_time(path, mtimeError);
            std::lock_guard<std::mutex> lock(ownWritesMutex);
            if (mtimeError)
                ownWrites.erase(path);
            else
                ownWrites[path] = written;
        }

        enum class InlineComments
        {
            Kept,
            Stripped,
        };

        const std::array<const char*, 4> wineLoaderNames = {"wine64-preloader", "wine-preloader", "wine64", "wine"};

        bool endsWithExe(const std::string& name)
        {
            if (name.size() <= 4)
                return false;
            std::string suffix = name.substr(name.size() - 4);
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return suffix == ".exe";
        }

        std::string firstWindowsExecutable(const std::vector<std::string>& arguments)
        {
            for (const std::string& candidate : arguments)
            {
                if (!endsWithExe(candidate))
                    continue;
                const size_t separator = candidate.find_last_of("/\\");
                if (candidate.size() - (separator == std::string::npos ? 0 : separator + 1) > 4)
                    return candidate;
            }
            return "";
        }

        template<typename Visit>
        void forEachSetting(std::ifstream& file, InlineComments comments, Visit visit)
        {
            std::string line;
            while (std::getline(file, line))
            {
                const size_t start = line.find_first_not_of(" \t");
                if (start == std::string::npos || line[start] == '#')
                    continue;
                if (comments == InlineComments::Stripped)
                    line.erase(std::min(line.find('#'), line.size()));
                const size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key   = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                trimWhitespace(key);
                trimWhitespace(value);
                if (!key.empty())
                    visit(key, value);
            }
        }

        void readSettingBool(bool& setting, const std::string& key, const std::string& value)
        {
            if (!parseBool(value, setting))
                Logger::warn("setting " + key + " = " + value + " is neither true nor false, so it stays "
                             + (setting ? "true" : "false"));
        }

        void readLaunchNotice(LaunchNotice& setting, const std::string& value)
        {
            if (const std::optional<LaunchNotice> read = launchNoticeFrom(value))
                setting = *read;
            else
                Logger::warn(std::string("setting launchNotice = ") + value + " is not every, first or never, so it stays " + launchNoticeWord(setting));
        }

        void readSettingInt(int& setting, const std::string& key, const std::string& value, SettingRange range)
        {
            int parsed = 0;
            if (parseWhole(value, parsed) && parsed >= range.lowest && parsed <= range.highest)
                setting = parsed;
            else
                Logger::warn("setting " + key + " = " + value + " is not a whole number from "
                             + std::to_string(range.lowest) + " to " + std::to_string(range.highest)
                             + ", so it stays " + std::to_string(setting));
        }

        bool keepsHiddenParameter(const std::string& key, const std::vector<std::string>& effects,
                                  const std::optional<std::set<std::string>>& liveParameterKeys)
        {
            if (!liveParameterKeys || liveParameterKeys->count(key) != 0)
                return false;
            return std::any_of(effects.begin(), effects.end(), [&](const std::string& effect) {
                return key.size() > effect.size() + 1 && key.starts_with(effect + ".") && key.find('.', effect.size() + 1) == std::string::npos
                       && !std::isdigit(static_cast<unsigned char>(key[effect.size() + 1]));
            });
        }

        std::vector<std::string> linesThisSaveDoesNotWrite(const std::string& filePath, const std::set<std::string>& written,
                                                           const std::vector<std::string>& effects,
                                                           const std::optional<std::set<std::string>>& liveParameterKeys)
        {
            std::vector<std::string> carried;
            std::ifstream            existing(filePath);
            std::string              line;
            while (std::getline(existing, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const size_t start  = line.find_first_not_of(" \t");
                const size_t equals = line.find('=');
                if (start == std::string::npos || line[start] == '#' || equals == std::string::npos || equals <= start)
                    continue;
                const std::string key = line.substr(start, line.find_last_not_of(" \t", equals - 1) - start + 1);
                if (key.find('@') != std::string::npos || written.count(key) != 0)
                    continue;
                if (key.find('.') != std::string::npos && !keepsHiddenParameter(key, effects, liveParameterKeys))
                    continue;
                carried.push_back(line);
            }
            return carried;
        }

        std::string quoteForConfig(const std::string& value)
        {
            if (value.find_first_of(" \t#\"") == std::string::npos)
                return value;
            std::string quoted = "\"";
            for (char c : value)
                if (c != '"')
                    quoted += c;
            quoted += '"';
            return quoted;
        }
    }

    std::string ConfigSerializer::getBaseConfigDir()
    {
        const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfig)
            return std::string(xdgConfig) + "/vkdaedalus";

        const char* home = std::getenv("HOME");
        if (home)
            return std::string(home) + "/.config/vkdaedalus";

        return "";
    }

    bool ConfigSerializer::makeFolder(const std::string& path)
    {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (!error)
            return true;
        Logger::err("could not make the folder " + path + ": " + error.message());
        return false;
    }

    void ConfigSerializer::moveOldConfigFolderOnce()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            namespace fs             = std::filesystem;
            const std::string target = getBaseConfigDir();
            if (target.empty())
                return;
            const fs::path  oldFolder = fs::path(target).parent_path() / "vkBasalt-overlay";
            std::error_code error;
            if (!fs::is_directory(fs::symlink_status(oldFolder, error)))
                return;
            if (!makeFolder(target))
            {
                Logger::warn("the old config folder " + oldFolder.string() + " was not moved, since " + target + " could not be made");
                return;
            }
            std::vector<fs::path> entries;
            for (fs::directory_iterator it(oldFolder, error), end; !error && it != end; it.increment(error))
                entries.push_back(it->path());
            size_t                   moved = 0;
            std::vector<std::string> kept;
            for (const fs::path& entry : entries)
            {
                const std::string name = entry.filename().string();
                const fs::path    to   = fs::path(target) / (name == "vkBasalt.conf" ? "vkdaedalus.conf" : name);
                std::error_code   why;
                if (fs::is_symlink(fs::symlink_status(entry, why)))
                    kept.push_back(name + " (a link)");
                else if (fs::exists(fs::symlink_status(to, why)))
                    kept.push_back(name + " (" + to.string() + " exists)");
                else if (fs::rename(entry, to, why); why)
                    kept.push_back(name + " (" + why.message() + ")");
                else
                    moved++;
            }
            std::string left;
            for (const std::string& name : kept)
                left += (left.empty() ? "" : ", ") + name;
            if (moved > 0 || !kept.empty())
                Logger::info("moved " + std::to_string(moved) + " of " + std::to_string(entries.size()) + " entries of the old config folder "
                             + oldFolder.string() + " to " + target + (left.empty() ? "" : "; left in place: " + left));
        });
    }

    std::string ConfigSerializer::getConfigsDir()
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
            return "";
        return baseDir + "/configs";
    }

    static std::string joinEffects(const std::vector<std::string>& effects)
    {
        std::string result;
        for (size_t i = 0; i < effects.size(); i++)
        {
            if (i > 0)
                result += ":";
            result += effects[i];
        }
        return result;
    }

    VkBasaltSettings ConfigSerializer::loadSettings()
    {
        VkBasaltSettings settings;

        std::string baseDir     = getBaseConfigDir();
        std::string homePath    = std::getenv("HOME") ? std::getenv("HOME") : "";
        const char* dataHomeEnv = std::getenv("XDG_DATA_HOME");
        std::string dataHome    = dataHomeEnv ? std::string(dataHomeEnv) + "/vkdaedalus" : homePath + "/.local/share/vkdaedalus";

        const std::array<std::string, 6> configPaths = {
            baseDir + "/settings.conf",
            baseDir + "/vkdaedalus.conf",
            dataHome + "/vkdaedalus.conf",
            std::string(SYSCONFDIR) + "/vkdaedalus/vkdaedalus.conf",
            std::string(SYSCONFDIR) + "/vkdaedalus.conf",
            std::string(DATADIR) + "/vkdaedalus/vkdaedalus.conf",
        };

        std::string configPath;
        for (const auto& path : configPaths)
        {
            std::ifstream test(path);
            if (test.is_open())
            {
                configPath = path;
                break;
            }
        }

        if (configPath.empty())
            return settings;

        Logger::info("SettingsManager loading from: " + configPath);

        std::ifstream file(configPath);
        if (!file.is_open())
            return settings;

        forEachSetting(file, InlineComments::Stripped, [&settings](const std::string& key, const std::string& value) {
            if (key == "maxEffects")
                readSettingInt(settings.maxEffects, key, value, MAX_EFFECTS_RANGE);
            else if (key == "toggleKey")
                settings.toggleKey = value;
            else if (key == "frameGenerationKey")
                settings.frameGenerationKey = value;
            else if (key == "reloadKey")
                settings.reloadKey = value;
            else if (key == "overlayKey")
                settings.overlayKey = value;
            else if (key == "previewKey")
                settings.previewKey = value;
            else if (key == "overlayBackdrop")
                readSettingInt(settings.overlayBackdropPercent, key, value, OVERLAY_BACKDROP_PERCENT_RANGE);
            else if (key == "overlaySize")
                readSettingInt(settings.overlaySizePercent, key, value, OVERLAY_SIZE_PERCENT_RANGE);
            else if (key == "enableOnLaunch")
                readSettingBool(settings.enableOnLaunch, key, value);
            else if (key == "depthCapture")
                readSettingBool(settings.depthCapture, key, value);
            else if (key == "launchNotice")
                readLaunchNotice(settings.launchNotice, value);
            else if (key == "autoApply")
                readSettingBool(settings.autoApply, key, value);
            else if (key == "autoApplyDelay")
                readSettingInt(settings.autoApplyDelayMs, key, value, AUTO_APPLY_DELAY_MS_RANGE);
            else if (key == "effectGpuTiming")
                readSettingBool(settings.effectGpuTiming, key, value);
            else if (key == "hud")
                readSettingBool(settings.hud, key, value);
            else if (key == "palette")
            {
                if (isPaletteWord(value))
                    settings.palette = value;
                else
                    Logger::warn("setting palette = " + std::string(value) + " names no colour scheme vkDaedalus has, so it stays " + settings.palette);
            }
            else if (key == "hudItems")
            {
                if (const std::optional<std::vector<HudItem>> items = hudItemsFrom(value))
                    settings.hudItems = hudItemsWord(*items);
                else
                    Logger::warn("setting hudItems = " + std::string(value) + " is not a list of fps, frametime, slowest and effects, so it stays " + settings.hudItems);
            }
            else if (key == "liveReshadeUniforms")
                readSettingBool(settings.liveReshadeUniforms, key, value);
            else if (key == "logLevel")
            {
                if (Logger::parseLevel(value))
                    settings.logLevel = value;
                else
                    Logger::warn("settings.conf: logLevel = " + value + " is not trace, debug, info, warn, error or none, so it stays " + settings.logLevel);
            }
        });

        return settings;
    }

    bool ConfigSerializer::saveSettings(const VkBasaltSettings& settings)
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
        {
            Logger::err("Could not determine config directory");
            return false;
        }

        if (!makeFolder(baseDir))
            return false;

        std::string        configPath = baseDir + "/settings.conf";
        std::ostringstream file;

        file << "# vkDaedalus overlay settings -- written by the overlay.\n";
        file << "# Effects and shader paths live in vkdaedalus.conf, which this never rewrites.\n\n";

        file << "# Overlay settings\n";
        file << "maxEffects = " << settings.maxEffects << "\n";
        file << "autoApply = " << (settings.autoApply ? "true" : "false") << "\n";
        file << "autoApplyDelay = " << settings.autoApplyDelayMs << "\n";

        file << "\n# Key bindings\n";
        file << "toggleKey = " << settings.toggleKey << "\n";
        file << "frameGenerationKey = " << settings.frameGenerationKey << "\n";
        file << "reloadKey = " << settings.reloadKey << "\n";
        file << "overlayKey = " << settings.overlayKey << "\n";
        file << "previewKey = " << settings.previewKey << "\n";

        file << "\n# Overlay look\n";
        file << "overlayBackdrop = " << settings.overlayBackdropPercent << "\n";
        file << "overlaySize = " << settings.overlaySizePercent << "\n";

        file << "\n# Startup behavior\n";
        file << "enableOnLaunch = " << (settings.enableOnLaunch ? "true" : "false") << "\n";
        file << "depthCapture = " << (settings.depthCapture ? "on" : "off") << "\n";
        file << "launchNotice = " << launchNoticeWord(settings.launchNotice) << "\n";
        file << "hud = " << (settings.hud ? "true" : "false") << "\n";
        file << "hudItems = " << settings.hudItems << "\n";
        file << "palette = " << settings.palette << "\n";

        file << "\n# Debug\n";
        file << "effectGpuTiming = " << (settings.effectGpuTiming ? "true" : "false") << "\n";
        file << "liveReshadeUniforms = " << (settings.liveReshadeUniforms ? "true" : "false") << "\n";
        file << "logLevel = " << settings.logLevel << "\n";

        if (!replaceFileWith(configPath, file.str()))
            return false;
        Logger::info("Saved settings to: " + configPath);
        return true;
    }

    std::string ConfigSerializer::detectGameName()
    {
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
            return UNKNOWN_PROGRAM_NAME;
        buf[len] = '\0';
        const std::string name = programNameFrom(buf, processArguments());
        return name.empty() ? UNKNOWN_PROGRAM_NAME : name;
    }

    std::vector<std::string> ConfigSerializer::processArguments()
    {
        std::ifstream            cmdline("/proc/self/cmdline", std::ios::binary);
        std::vector<std::string> arguments;
        std::string              argument;
        while (std::getline(cmdline, argument, '\0'))
            arguments.push_back(argument);
        return arguments;
    }

    std::string ConfigSerializer::wineExecutableFrom(const std::string& exePath, const std::vector<std::string>& arguments)
    {
        const std::string exe = std::filesystem::path(exePath).filename().string();
        if (std::find(wineLoaderNames.begin(), wineLoaderNames.end(), exe) == wineLoaderNames.end())
            return "";
        return firstWindowsExecutable(arguments);
    }

    std::string ConfigSerializer::programWordFromCommand(const std::vector<std::string>& command)
    {
        if (command.empty())
            return "";
        const std::string wine = firstWindowsExecutable(command);
        if (!wine.empty())
            return wine;
        const auto last = std::find(command.rbegin(), command.rend(), std::string("--"));
        const auto run  = last == command.rend() ? command.begin() : last.base();
        if (run == command.end())
            return "";
        for (auto word = run; word != command.end(); ++word)
        {
            const std::string name = std::filesystem::path(*word).filename().string();
            std::error_code   unreadable;
            if (!name.empty() && std::filesystem::exists(getGamePath(name), unreadable))
                return *word;
        }
        return *run;
    }

    std::string ConfigSerializer::programNameFromCommand(const std::vector<std::string>& command)
    {
        const std::string word = programWordFromCommand(command);
        if (word.empty())
            return UNKNOWN_PROGRAM_NAME;
        if (word != firstWindowsExecutable(command))
            return std::filesystem::path(word).filename().string();
        const size_t separator = word.find_last_of("/\\");
        return separator == std::string::npos ? word : word.substr(separator + 1);
    }

    std::string ConfigSerializer::programNameFrom(const std::string& exePath, const std::vector<std::string>& arguments)
    {
        const std::string wine = wineExecutableFrom(exePath, arguments);
        if (wine.empty())
            return std::filesystem::path(exePath).filename().string();
        const size_t separator = wine.find_last_of("/\\");
        return separator == std::string::npos ? wine : wine.substr(separator + 1);
    }

    static bool equalsIgnoreCaseLocal(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    void ConfigSerializer::scanShaderTree(
        const std::string& dir,
        std::vector<std::string>& shaderPaths,
        std::vector<std::string>& texturePaths)
    {
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                dir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_directory())
                    continue;

                std::error_code relEc;
                const auto relativePath = std::filesystem::relative(entry.path(), dir, relEc);
                if (relEc)
                    continue;

                bool underShaders  = false;
                bool underTextures = false;
                for (const auto& part : relativePath)
                {
                    if (equalsIgnoreCaseLocal(part.string(), "Shaders"))
                        underShaders = true;
                    else if (equalsIgnoreCaseLocal(part.string(), "Textures"))
                        underTextures = true;
                }

                if (underShaders)
                    shaderPaths.push_back(entry.path().string());
                else if (underTextures)
                    texturePaths.push_back(entry.path().string());
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            Logger::err("Error scanning directory " + dir + ": " + e.what());
        }
    }

    ShaderManagerConfig ConfigSerializer::loadShaderManagerConfig()
    {
        static std::mutex                      cacheMutex;
        static ShaderManagerConfig             cachedConfig;
        static std::filesystem::file_time_type cachedMTime;
        static bool                            cacheValid = false;

        const std::string configPath = getBaseConfigDir() + "/shader_manager.conf";
        std::error_code   mtimeError;
        const auto        mtime = std::filesystem::last_write_time(configPath, mtimeError);

        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cacheValid && !mtimeError && mtime == cachedMTime)
            return cachedConfig;

        ShaderManagerConfig config;

        std::ifstream file(configPath);
        if (!file.is_open())
        {
            std::string defaultReshadeDir = getBaseConfigDir() + "/reshade";

            makeFolder(defaultReshadeDir + "/Shaders");
            makeFolder(defaultReshadeDir + "/Textures");

            config.parentDirectories.push_back(defaultReshadeDir);

            scanShaderTree(defaultReshadeDir,
                config.discoveredShaderPaths, config.discoveredTexturePaths);

            saveShaderManagerConfig(config);
            Logger::info("Created default shader manager config with reshade directory");
            cachedConfig = config;
            cachedMTime  = std::filesystem::last_write_time(configPath, mtimeError);
            cacheValid   = !mtimeError;
            return config;
        }

        forEachSetting(file, InlineComments::Kept, [&config](const std::string& key, const std::string& value) {
            if (key == "parentDir" && !value.empty())
                config.parentDirectories.push_back(value);
            else if (key == "shaderPath" && !value.empty())
                config.discoveredShaderPaths.push_back(value);
            else if (key == "texturePath" && !value.empty())
                config.discoveredTexturePaths.push_back(value);
        });

        cachedConfig = config;
        cachedMTime  = std::filesystem::last_write_time(configPath, mtimeError);
        cacheValid   = !mtimeError;
        return config;
    }

    bool ConfigSerializer::saveShaderManagerConfig(const ShaderManagerConfig& config)
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
        {
            Logger::err("Could not determine config directory");
            return false;
        }

        if (!makeFolder(baseDir))
            return false;

        std::string        configPath = baseDir + "/shader_manager.conf";
        std::ostringstream file;

        file << "# Shader Manager configuration\n";
        file << "# Parent directories are scanned recursively for Shaders/ and Textures/ subdirs\n\n";

        file << "# Parent directories (user-added)\n";
        for (const auto& dir : config.parentDirectories)
            file << "parentDir = " << dir << "\n";

        file << "\n# Discovered shader paths (auto-generated on scan)\n";
        for (const auto& path : config.discoveredShaderPaths)
            file << "shaderPath = " << path << "\n";

        file << "\n# Discovered texture paths (auto-generated on scan)\n";
        for (const auto& path : config.discoveredTexturePaths)
            file << "texturePath = " << path << "\n";

        if (!replaceFileWith(configPath, file.str()))
            return false;
        Logger::info("Saved shader manager config to: " + configPath);
        return true;
    }


    std::string ConfigSerializer::getProfilesDir()
    {
        const std::string base = getBaseConfigDir();
        return base.empty() ? "" : base + "/profiles";
    }

    std::string ConfigSerializer::getProfilePath(const std::string& profileName)
    {
        const std::string dir = getProfilesDir();
        if (dir.empty() || profileName == BYPASS_PROFILE_NAME)
            return "";
        return dir + "/" + (profileName.empty() ? std::string(DEFAULT_PROFILE_NAME) : profileName) + ".conf";
    }

    std::string ConfigSerializer::getGamePath(const std::string& gameName)
    {
        const std::string configsDir = getConfigsDir();
        if (configsDir.empty() || gameName.empty())
            return "";
        return configsDir + "/" + gameName + ".conf";
    }

    bool ConfigSerializer::isWineLoaderName(const std::string& name)
    {
        return std::find(wineLoaderNames.begin(), wineLoaderNames.end(), name) != wineLoaderNames.end();
    }

    std::string ConfigSerializer::wineLoaderProfile()
    {
        for (const char* loader : wineLoaderNames)
        {
            const ActiveProfile active = getActiveProfile(loader);
            if (active.kind == ProfileKind::Named)
                return active.name;
        }
        return "";
    }

    namespace
    {
        std::string gameFileText(const GameSettings& settings)
        {
            return std::string("# vkDaedalus: this game's own settings; its look is the profile configs/.active_profiles names\n")
                 + "# Safe Anti-Cheat keeps the depth buffer away from every effect; set it to\n"
                 + "# false only for a game with no anti-cheat that could see the layer.\n"
                 + "safeAntiCheat = " + (settings.safeAntiCheat ? "true" : "false") + "\n";
        }

        bool readSafeAntiCheatFrom(const std::string& path, bool& safeAntiCheat)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;
            forEachSetting(file, InlineComments::Stripped, [&safeAntiCheat](const std::string& key, const std::string& value) {
                if (key == "safeAntiCheat")
                    readSettingBool(safeAntiCheat, key, value);
            });
            return true;
        }

        std::map<std::string, std::string> readAssignments(const std::string& activePath)
        {
            std::map<std::string, std::string> entries;
            std::ifstream                      file(activePath);
            if (file.is_open())
                forEachSetting(file, InlineComments::Kept, [&entries](const std::string& key, const std::string& value) {
                    entries[key] = value;
                });
            return entries;
        }

        bool writeAssignments(const std::string& activePath, const std::map<std::string, std::string>& entries)
        {
            std::string text;
            for (const auto& [game, profile] : entries)
                text += game + "=" + profile + "\n";
            if (replaceFileWith(activePath, text))
                return true;
            Logger::err("could not write " + activePath + ", so the games keep the profiles they had");
            return false;
        }

        std::string withoutSafeAntiCheat(const std::string& path)
        {
            std::ifstream source(path, std::ios::binary);
            std::string   kept;
            std::string   line;
            while (std::getline(source, line))
            {
                const size_t start = line.find_first_not_of(" \t");
                if (start != std::string::npos && line.compare(start, 13, "safeAntiCheat") == 0)
                    continue;
                kept += line + "\n";
            }
            return kept;
        }
    }

    std::string ConfigSerializer::ensureGame(const std::string& gameName)
    {
        const std::string gamePath = getGamePath(gameName);
        if (gamePath.empty() || !makeFolder(getConfigsDir()))
            return "";

        std::error_code unreadable;
        if (std::filesystem::exists(gamePath, unreadable))
            return gamePath;

        if (!replaceFileWith(gamePath, gameFileText({})))
        {
            Logger::err("Could not create the settings of " + gameName + ": " + gamePath);
            return "";
        }
        const std::string loaderProfile = endsWithExe(gameName) ? wineLoaderProfile() : "";
        if (!loaderProfile.empty() && getActiveProfile(gameName).kind == ProfileKind::Default)
        {
            setActiveProfile(gameName, loaderProfile);
            Logger::info("first launch of " + gameName + ": it runs " + loaderProfile + ", the profile the Wine loader runs, with Safe Anti-Cheat on");
        }
        else
            Logger::info("first launch of " + gameName + ": it runs " + getActiveProfile(gameName).name + " with Safe Anti-Cheat on");
        return gamePath;
    }

    std::vector<std::string> ConfigSerializer::listProfiles()
    {
        std::set<std::string> names;
        const std::string     dir = getProfilesDir();
        std::error_code       unreadable;
        if (!dir.empty() && std::filesystem::is_directory(dir, unreadable))
            for (const auto& entry : std::filesystem::directory_iterator(dir, unreadable))
            {
                const std::string file = entry.path().filename().string();
                if (file.empty() || file.front() == '.' || entry.path().extension() != ".conf")
                    continue;
                const std::string name = entry.path().stem().string();
                if (name != DEFAULT_PROFILE_NAME && name != BYPASS_PROFILE_NAME)
                    names.insert(name);
            }
        std::vector<std::string> profiles = {DEFAULT_PROFILE_NAME};
        profiles.insert(profiles.end(), names.begin(), names.end());
        profiles.push_back(BYPASS_PROFILE_NAME);
        return profiles;
    }

    std::map<std::string, std::vector<std::string>> ConfigSerializer::profileUsers()
    {
        std::map<std::string, std::vector<std::string>> users;
        const std::string                               configsDir = getConfigsDir();
        std::error_code                                 unreadable;
        if (configsDir.empty() || !std::filesystem::is_directory(configsDir, unreadable))
            return users;
        std::set<std::string> games;
        for (const auto& entry : std::filesystem::directory_iterator(configsDir, unreadable))
        {
            const std::string file = entry.path().filename().string();
            if (!file.empty() && file.front() != '.' && entry.path().extension() == ".conf" && file.find('@') == std::string::npos)
                games.insert(entry.path().stem().string());
        }
        for (const std::string& game : games)
            users[getActiveProfile(game).name].push_back(game);
        return users;
    }

    ActiveProfile ConfigSerializer::getActiveProfile(const std::string& gameName)
    {
        if (gameName.empty())
            return {};

        const bool    compositor = gameName == "gamescope" || gameName == ".gamescope-wrapped";
        std::string   activePath = getConfigsDir() + "/.active_profiles";
        std::ifstream file(activePath);
        if (!file.is_open())
            return compositor ? ActiveProfile{ProfileKind::Bypass, BYPASS_PROFILE_NAME} : ActiveProfile{};

        std::string name;
        bool        loaderBypassed = false;
        forEachSetting(file, InlineComments::Kept, [&gameName, &name, &loaderBypassed](const std::string& key, const std::string& value) {
            if (key == gameName)
                name = value;
            if (std::find(wineLoaderNames.begin(), wineLoaderNames.end(), key) != wineLoaderNames.end())
                loaderBypassed = loaderBypassed || value == BYPASS_PROFILE_NAME;
        });

        if (name.empty() && ((loaderBypassed && endsWithExe(gameName)) || compositor))
            return {ProfileKind::Bypass, BYPASS_PROFILE_NAME};
        if (name == BYPASS_PROFILE_NAME)
            return {ProfileKind::Bypass, name};
        if (name.empty() || name == DEFAULT_PROFILE_NAME)
            return {};
        return {ProfileKind::Named, name};
    }

    bool ConfigSerializer::setActiveProfile(const std::string& gameName, const std::string& profileName)
    {
        if (gameName.empty() || !makeFolder(getConfigsDir()))
            return false;
        const std::string activePath = getConfigsDir() + "/.active_profiles";
        std::map<std::string, std::string> entries = readAssignments(activePath);
        if (profileName.empty() || profileName == DEFAULT_PROFILE_NAME)
            entries.erase(gameName);
        else
            entries[gameName] = profileName;
        return writeAssignments(activePath, entries);
    }

    std::string ConfigSerializer::profileNameProblem(const std::string& profileName)
    {
        if (profileName.empty())
            return "a profile needs a name";
        if (profileName == DEFAULT_PROFILE_NAME || profileName == BYPASS_PROFILE_NAME)
            return "'default' and '" + std::string(BYPASS_PROFILE_NAME) + "' are taken by vkDaedalus";
        if (profileName == "." || profileName == "..")
            return "'" + profileName + "' cannot be a profile name";
        if (profileName.front() == ' ' || profileName.back() == ' ')
            return "a profile name cannot start or end with a space";
        for (const char c : profileName)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '-' && c != '_' && c != '.')
                return "a profile name holds only letters, digits, spaces, '-', '_' and '.'";
        return "";
    }

    std::string ConfigSerializer::createProfile(const std::string& profileName, const std::string& copyFromProfile)
    {
        if (const std::string problem = profileNameProblem(profileName); !problem.empty())
            return problem;
        const std::string newPath = getProfilePath(profileName);
        if (newPath.empty() || !makeFolder(getProfilesDir()))
            return "there is no profiles folder to put it in";

        std::error_code unreadable;
        if (std::filesystem::exists(newPath, unreadable))
            return "'" + profileName + "' already exists";
        if (unreadable)
            return newPath + " cannot be checked: " + unreadable.message();

        std::string copied;
        if (!copyFromProfile.empty() && copyFromProfile != BYPASS_PROFILE_NAME)
            copied = withoutSafeAntiCheat(getProfilePath(copyFromProfile));
        if (copied.empty())
            copied = "# vkDaedalus profile '" + profileName + "'\n\n";
        if (!replaceFileWith(newPath, copied))
            return newPath + " could not be written";

        Logger::info("Created profile " + profileName + (copyFromProfile.empty() ? "" : " from " + copyFromProfile));
        return "";
    }

    std::string ConfigSerializer::renameProfile(const std::string& from, const std::string& to)
    {
        if (from == DEFAULT_PROFILE_NAME || from == BYPASS_PROFILE_NAME)
            return "'" + from + "' belongs to vkDaedalus and keeps its name";
        if (const std::string problem = profileNameProblem(to); !problem.empty())
            return problem;
        const std::string fromPath = getProfilePath(from);
        const std::string toPath   = getProfilePath(to);
        std::error_code   unreadable;
        if (!std::filesystem::exists(fromPath, unreadable))
            return "'" + from + "' does not exist";
        if (std::filesystem::exists(toPath, unreadable))
            return "'" + to + "' already exists";
        std::filesystem::rename(fromPath, toPath, unreadable);
        if (unreadable)
            return fromPath + " could not be renamed: " + unreadable.message();

        const std::string activePath = getConfigsDir() + "/.active_profiles";
        std::map<std::string, std::string> entries = readAssignments(activePath);
        for (auto& [game, profile] : entries)
            if (profile == from)
                profile = to;
        if (!writeAssignments(activePath, entries))
            return "renamed, but the games that ran '" + from + "' could not be moved to '" + to + "'";
        Logger::info("Renamed profile " + from + " to " + to);
        return "";
    }

    std::string ConfigSerializer::deleteProfile(const std::string& profileName)
    {
        if (profileName.empty() || profileName == DEFAULT_PROFILE_NAME || profileName == BYPASS_PROFILE_NAME)
            return "'" + profileName + "' belongs to vkDaedalus and stays";
        const std::string path = getProfilePath(profileName);
        if (path.empty() || std::remove(path.c_str()) != 0)
            return path + " could not be removed";

        const std::string activePath = getConfigsDir() + "/.active_profiles";
        std::map<std::string, std::string> entries = readAssignments(activePath);
        for (auto it = entries.begin(); it != entries.end();)
            it = it->second == profileName ? entries.erase(it) : std::next(it);
        if (!writeAssignments(activePath, entries))
            return "deleted, but its games could not be moved to default";
        Logger::info("Deleted profile " + profileName + "; its games run default");
        return "";
    }

    GameSettings ConfigSerializer::loadGameSettings(const std::string& gameName)
    {
        return loadGameSettingsFile(getGamePath(gameName));
    }

    GameSettings ConfigSerializer::loadGameSettingsFile(const std::string& path)
    {
        GameSettings settings;
        readSafeAntiCheatFrom(path, settings.safeAntiCheat);
        return settings;
    }

    std::string ConfigSerializer::ensureDefaultProfile()
    {
        const std::string path = getProfilePath(DEFAULT_PROFILE_NAME);
        std::error_code   unreadable;
        if (path.empty() || std::filesystem::exists(path, unreadable))
            return path;
        if (!makeFolder(getProfilesDir())
            || !replaceFileWith(path, "# vkDaedalus profile 'default', run by every game without a profile of its own.\n"
                                      "# No effects key here means it inherits the vkdaedalus.conf chain; saving from the\n"
                                      "# overlay writes an explicit list, and an explicit empty sticks.\n"))
        {
            Logger::err("Could not create the default profile: " + path);
            return "";
        }
        Logger::info("Created the default profile: " + path);
        return path;
    }

    bool ConfigSerializer::saveGameSettings(const std::string& gameName, const GameSettings& settings)
    {
        const std::string path = getGamePath(gameName);
        if (path.empty() || !makeFolder(getConfigsDir()))
            return false;
        if (replaceFileWith(path, gameFileText(settings)))
        {
            rememberOwnWrite(path);
            return true;
        }
        Logger::err("could not save the settings of " + gameName + " to " + path);
        return false;
    }

    std::string ConfigSerializer::migrateToSharedProfilesOnce()
    {
        static std::mutex           once;
        static std::string          outcome;
        static bool                 done = false;
        std::lock_guard<std::mutex> lock(once);
        if (!done)
        {
            done                   = true;
            const std::string base = std::filesystem::path(getConfigsDir()).parent_path().string();
            const int folder = base.empty() ? -1 : open(base.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (folder >= 0 && flock(folder, LOCK_EX) != 0)
                Logger::warn("could not lock " + base + " for the move to shared profiles, so another program starting now may move them too");
            outcome = migrateToSharedProfiles();
            if (folder >= 0)
                close(folder);
        }
        return outcome;
    }

    std::string ConfigSerializer::migrateToSharedProfiles()
    {
        std::string       outcome;
        const std::string configsDir  = getConfigsDir();
        const std::string profilesDir = getProfilesDir();
        std::error_code   unreadable;
        if (configsDir.empty() || profilesDir.empty() || std::filesystem::exists(profilesDir, unreadable))
            return outcome;

        struct OldFile
        {
            std::string game;
            std::string profile;
            std::string path;
        };
        std::vector<OldFile> old;
        if (std::filesystem::exists(configsDir, unreadable) && std::filesystem::is_directory(configsDir, unreadable))
            for (const auto& entry : std::filesystem::directory_iterator(configsDir, unreadable))
            {
                const std::string file = entry.path().filename().string();
                if (file.empty() || file.front() == '.' || entry.path().extension() != ".conf" || !entry.is_regular_file(unreadable))
                    continue;
                const std::string stem = entry.path().stem().string();
                const size_t      at   = stem.find('@');
                if (at == std::string::npos)
                    old.push_back({stem, "", entry.path().string()});
                else if (stem.substr(at + 1) != BYPASS_PROFILE_NAME && at + 1 < stem.size())
                    old.push_back({stem.substr(0, at), stem.substr(at + 1), entry.path().string()});
            }
        if (unreadable)
        {
            outcome = configsDir + " could not be read: " + unreadable.message();
            Logger::err("the move to shared profiles did not start: " + outcome);
            return outcome;
        }
        if (old.empty())
        {
            if (!makeFolder(profilesDir))
                outcome = profilesDir + " could not be made";
            return outcome;
        }

        std::string backup = configsDir + ".before-shared-profiles";
        for (int n = 2; std::filesystem::exists(backup, unreadable); n++)
            backup = configsDir + ".before-shared-profiles-" + std::to_string(n);
        std::filesystem::copy(configsDir, backup, std::filesystem::copy_options::recursive, unreadable);
        if (unreadable)
        {
            outcome = "the backup " + backup + " could not be written: " + unreadable.message();
            Logger::err("the move to shared profiles did not start, nothing in " + configsDir + " changed: " + outcome);
            return outcome;
        }

        std::map<std::string, int> namedBy;
        for (const OldFile& file : old)
            if (!file.profile.empty())
                namedBy[file.profile]++;
        std::set<std::string> gameProfiles;
        for (const OldFile& file : old)
            if (file.profile.empty())
                gameProfiles.insert(file.game);
        const auto newName = [&](const OldFile& file) {
            if (file.profile.empty())
                return file.game;
            if (namedBy[file.profile] > 1 || gameProfiles.count(file.profile))
                return file.game + " " + file.profile;
            return file.profile;
        };

        const std::string partial = profilesDir + ".partial";
        std::filesystem::remove_all(partial, unreadable);
        if (!makeFolder(partial))
        {
            outcome = partial + " could not be made";
            Logger::err("the move to shared profiles stopped, nothing in " + configsDir + " changed: " + outcome);
            return outcome;
        }
        for (const OldFile& file : old)
        {
            const std::string target = partial + "/" + newName(file) + ".conf";
            if (!replaceFileWith(target, withoutSafeAntiCheat(file.path)))
            {
                outcome = target + " could not be written";
                Logger::err("the move to shared profiles stopped, nothing in " + configsDir + " changed: " + outcome);
                return outcome;
            }
        }
        std::filesystem::rename(partial, profilesDir, unreadable);
        if (unreadable)
        {
            outcome = profilesDir + " could not be put in place: " + unreadable.message();
            Logger::err("the move to shared profiles stopped, nothing in " + configsDir + " changed: " + outcome);
            return outcome;
        }

        const std::string                  activePath = configsDir + "/.active_profiles";
        std::map<std::string, std::string> entries    = readAssignments(activePath);
        std::set<std::string>              games;
        for (const OldFile& file : old)
            games.insert(file.game);
        for (const std::string& game : games)
        {
            const auto entry = entries.find(game);
            std::string ran  = entry == entries.end() || entry->second.empty() || entry->second == DEFAULT_PROFILE_NAME ? "" : entry->second;
            if (ran == BYPASS_PROFILE_NAME)
                continue;
            const auto ranFile = std::find_if(old.begin(), old.end(), [&](const OldFile& file) { return file.game == game && file.profile == ran; });
            const bool hasOwn = std::any_of(old.begin(), old.end(), [&](const OldFile& file) { return file.game == game && file.profile.empty(); });
            if (ranFile != old.end())
                entries[game] = newName(*ranFile);
            else if (hasOwn)
                entries[game] = game;
            else
                entries.erase(game);
            GameSettings settings;
            if (ranFile != old.end())
                readSafeAntiCheatFrom(ranFile->path, settings.safeAntiCheat);
            if (!replaceFileWith(getGamePath(game), gameFileText(settings)))
                Logger::err("the move to shared profiles could not write the settings of " + game + "; the backup " + backup + " holds them");
        }
        if (!writeAssignments(activePath, entries))
            outcome = activePath + " could not be written; the backup " + backup + " holds it";
        for (const OldFile& file : old)
            if (!file.profile.empty())
                std::filesystem::remove(file.path, unreadable);
        Logger::info("moved " + std::to_string(old.size()) + " profile files to " + profilesDir + ", one profile per file, shared by the games that pick it; the old files are in " + backup);
        return outcome;
    }

    ProfileSettings ConfigSerializer::loadProfileSettings(const std::string& filePath)
    {
        ProfileSettings settings;
        std::ifstream file(filePath);
        if (!file.is_open())
            return settings;

        forEachSetting(file, InlineComments::Stripped, [&settings](const std::string& key, const std::string& value) {
            if (key == "effectsAfterFrameGeneration")
            {
                bool after = false;
                if (parseBool(value, after))
                    settings.effectsAfterFrameGeneration = after;
                else
                    Logger::warn("setting " + key + " = " + value + " is neither true nor false, so effects stay before frame generation");
            }
        });

        return settings;
    }

    bool ConfigSerializer::effectsAfterFrameGenerationFor(const std::string& program)
    {
        const ActiveProfile active = getActiveProfile(program);
        if (active.kind == ProfileKind::Bypass)
            return false;
        std::string path = getProfilePath(active.name);
        if (active.kind == ProfileKind::Named && !std::filesystem::exists(path))
            path = getProfilePath(DEFAULT_PROFILE_NAME);
        return loadProfileSettings(path).effectsAfterFrameGeneration.value_or(false);
    }

    bool ConfigSerializer::saveToPath(
        const std::string& filePath,
        const std::vector<std::string>& effects,
        const std::vector<std::string>& disabledEffects,
        const std::vector<ConfigParam>& params,
        const std::map<std::string, std::string>& effectPaths,
        const std::vector<PreprocessorDefinition>& preprocessorDefs,
        const ProfileSettings& profileSettings,
        const std::optional<std::set<std::string>>& liveParameterKeys)
    {
        std::set<std::string> writtenKeys = {"effects", "disabledEffects", "safeAntiCheat"};
        if (profileSettings.gpus)
            writtenKeys.insert("gpus");
        if (profileSettings.effectsAfterFrameGeneration)
            writtenKeys.insert("effectsAfterFrameGeneration");
        for (const auto& [effectName, path] : effectPaths)
        {
            if (!path.empty())
                writtenKeys.insert(effectName);
        }
        const std::vector<std::string> carried = linesThisSaveDoesNotWrite(filePath, writtenKeys, effects, liveParameterKeys);

        std::string tmpPath = filePath + ".tmp";
        std::ofstream file(tmpPath);
        if (!file.is_open())
        {
            Logger::err("Could not open for writing: " + tmpPath);
            return false;
        }

        if (profileSettings.gpus && !profileSettings.gpus->empty())
            file << "gpus = " << *profileSettings.gpus << "\n";
        if (profileSettings.effectsAfterFrameGeneration)
            file << "effectsAfterFrameGeneration = " << (*profileSettings.effectsAfterFrameGeneration ? "true" : "false") << "\n";
        file << "\n";

        for (const auto& line : carried)
            file << line << "\n";
        if (!carried.empty())
            file << "\n";

        std::map<std::string, std::vector<const ConfigParam*>> paramsByEffect;
        for (const auto& param : params)
            paramsByEffect[param.effectName].push_back(&param);

        std::map<std::string, std::vector<const PreprocessorDefinition*>> defsByEffect;
        for (const auto& def : preprocessorDefs)
            defsByEffect[def.effectName].push_back(&def);

        if (const auto globals = defsByEffect.find(""); globals != defsByEffect.end())
        {
            file << "# global definitions\n";
            for (const auto* def : globals->second)
                file << "@" << def->name << " = " << quoteForConfig(def->value) << "\n";
            file << "\n";
            defsByEffect.erase(globals);
        }

        for (const auto& [effectName, effectParams] : paramsByEffect)
        {
            file << "# " << effectName << "\n";
            auto pathIt = effectPaths.find(effectName);
            if (pathIt != effectPaths.end() && !pathIt->second.empty())
                file << effectName << " = " << quoteForConfig(pathIt->second) << "\n";
            for (const auto* param : effectParams)
                file << param->effectName << "." << param->paramName << " = " << param->value << "\n";
            auto defsIt = defsByEffect.find(effectName);
            if (defsIt != defsByEffect.end())
            {
                for (const auto* def : defsIt->second)
                    file << def->effectName << "@" << def->name << " = " << quoteForConfig(def->value) << "\n";
            }
            file << "\n";
        }

        for (const auto& [effectName, effectDefs] : defsByEffect)
        {
            if (paramsByEffect.find(effectName) != paramsByEffect.end())
                continue;
            file << "# " << effectName << "\n";
            auto pathIt = effectPaths.find(effectName);
            if (pathIt != effectPaths.end() && !pathIt->second.empty())
                file << effectName << " = " << quoteForConfig(pathIt->second) << "\n";
            for (const auto* def : effectDefs)
                file << def->effectName << "@" << def->name << " = " << quoteForConfig(def->value) << "\n";
            file << "\n";
        }

        for (const auto& [effectName, path] : effectPaths)
        {
            if (!path.empty() &&
                paramsByEffect.find(effectName) == paramsByEffect.end() &&
                defsByEffect.find(effectName) == defsByEffect.end())
            {
                file << "# " << effectName << "\n";
                file << effectName << " = " << quoteForConfig(path) << "\n\n";
            }
        }

        file << "effects = " << joinEffects(effects) << "\n";

        if (!disabledEffects.empty())
            file << "disabledEffects = " << joinEffects(disabledEffects) << "\n";

        file.close();

        if (file.fail())
        {
            Logger::err("Failed to write config to: " + tmpPath);
            std::remove(tmpPath.c_str());
            return false;
        }

        if (std::rename(tmpPath.c_str(), filePath.c_str()) != 0)
        {
            Logger::err("Failed to rename temp config to: " + filePath);
            std::remove(tmpPath.c_str());
            return false;
        }

        rememberOwnWrite(filePath);
        return true;
    }

    bool ConfigSerializer::fileIsAsWeLeftIt(const std::string& filePath)
    {
        std::error_code mtimeError;
        const auto      current = std::filesystem::last_write_time(filePath, mtimeError);
        if (mtimeError)
            return false;
        std::lock_guard<std::mutex> lock(ownWritesMutex);
        const auto                  written = ownWrites.find(filePath);
        return written != ownWrites.end() && written->second == current;
    }

}
