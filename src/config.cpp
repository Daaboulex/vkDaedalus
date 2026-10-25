#include "config.hpp"
#include "config_paths.hpp"
#include "config_serializer.hpp"
#include "config_value.hpp"

#include <sstream>
#include <array>

namespace vkBasalt
{
    Config::Config()
    {
        const char* homeEnv = std::getenv("HOME");
        std::string homePath = homeEnv ? homeEnv : "/tmp";

        const char* tmpHomeEnv     = std::getenv("XDG_DATA_HOME");
        std::string userConfigFile = tmpHomeEnv ? std::string(tmpHomeEnv) + "/vkdaedalus/vkdaedalus.conf"
                                                : homePath + "/.local/share/vkdaedalus/vkdaedalus.conf";

        const char* tmpConfigEnv      = std::getenv("XDG_CONFIG_HOME");
        std::string userXdgConfigFile = tmpConfigEnv ? std::string(tmpConfigEnv) + "/vkdaedalus/vkdaedalus.conf"
                                                     : homePath + "/.config/vkdaedalus/vkdaedalus.conf";

        const std::array<std::string, 5> configPaths = {
            userXdgConfigFile,
            userConfigFile,
            std::string(SYSCONFDIR) + "/vkdaedalus.conf",
            std::string(SYSCONFDIR) + "/vkdaedalus/vkdaedalus.conf",
            std::string(DATADIR) + "/vkdaedalus/vkdaedalus.conf",
        };

        for (const auto& path : configPaths)
        {
            std::ifstream file(path);
            if (file.good())
            {
                Logger::info("base config: " + path);
                configFilePath = path;
                readConfigFile(file);
                updateLastModifiedTime();
                return;
            }
        }

        Logger::err("no vkdaedalus.conf found");
    }

    Config::Config(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.good())
        {
            Logger::err("failed to load config: " + path);
            return;
        }

        Logger::info("config: " + path);
        configFilePath = path;
        readConfigFile(file);
        updateLastModifiedTime();
    }

    Config::Config(const Config& other)
    {
        this->options          = other.options;
        this->overrides        = other.overrides;
        this->configFilePath   = other.configFilePath;
        this->lastModifiedTime = other.lastModifiedTime;
    }

    void Config::updateLastModifiedTime()
    {
        if (configFilePath.empty())
            return;

        std::error_code mtimeError;
        const auto      mtime = std::filesystem::last_write_time(configFilePath, mtimeError);
        if (!mtimeError)
            lastModifiedTime = mtime;
    }

    bool Config::hasConfigChanged()
    {
        if (configFilePath.empty())
            return false;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastConfigCheckTime).count() < 500)
            return false;
        lastConfigCheckTime = now;

        std::error_code mtimeError;
        const auto      mtime = std::filesystem::last_write_time(configFilePath, mtimeError);
        if (mtimeError || mtime == lastModifiedTime)
            return false;

        if (ConfigSerializer::fileIsAsWeLeftIt(configFilePath))
        {
            reload();
            return false;
        }
        return true;
    }

    void Config::reload()
    {
        if (configFilePath.empty())
            return;

        std::ifstream file(configFilePath);
        if (!file.good())
        {
            Logger::err("failed to reload config: " + configFilePath);
            return;
        }

        Logger::info("reloading config: " + configFilePath);
        auto oldOptions = std::move(options);
        options.clear();
        readConfigFile(file);

        updateLastModifiedTime();
        if (options.empty() && !oldOptions.empty())
        {
            Logger::warn("config reload produced empty options, restoring previous");
            options = std::move(oldOptions);
        }
    }

    void Config::readConfigFile(std::ifstream& stream)
    {
        std::string line;
        while (std::getline(stream, line))
            readConfigLine(line);
    }

    void Config::readConfigLine(std::string line)
    {
        std::string key;
        std::string value;
        bool inQuotes    = false;
        bool foundEquals = false;

        auto appendChar = [&key, &value, &foundEquals](const char& c) {
            if (foundEquals)
                value += c;
            else
                key += c;
        };

        for (const char& c : line)
        {
            if (inQuotes)
            {
                if (c == '"')
                    inQuotes = false;
                else
                    appendChar(c);
                continue;
            }
            switch (c)
            {
                case '#': goto DONE;
                case '"': inQuotes = true; break;
                case '\t':
                case ' ': break;
                case '=': foundEquals = true; break;
                default: appendChar(c); break;
            }
        }

    DONE:
        if (!key.empty() && (!value.empty() || foundEquals))
        {
            Logger::info(key + " = " + value);
            options[key] = value;
        }
    }

    void Config::parseOption(const std::string& option, int32_t& result)
    {
        auto found = options.find(option);
        if (found != options.end() && !parseWhole(found->second, result))
            Logger::warn("invalid int32_t value for " + option + ": " + found->second);
    }

    void Config::parseOption(const std::string& option, uint32_t& result)
    {
        auto found = options.find(option);
        if (found != options.end() && !parseWhole(found->second, result))
            Logger::warn("invalid uint32_t value for " + option + ": " + found->second);
    }

    void Config::parseOption(const std::string& option, float& result)
    {
        auto found = options.find(option);
        if (found != options.end() && !parseWhole(found->second, result))
            Logger::warn("invalid float value for " + option + ": " + found->second);
    }

    void Config::parseOption(const std::string& option, bool& result)
    {
        auto found = options.find(option);
        if (found != options.end() && !parseBool(found->second, result))
            Logger::warn("invalid bool value for " + option + ": " + found->second);
    }

    void Config::parseOption(const std::string& option, std::string& result)
    {
        auto found = options.find(option);
        if (found != options.end())
            result = found->second;
    }

    void Config::parseOption(const std::string& option, std::vector<std::string>& result)
    {
        auto found = options.find(option);
        if (found != options.end())
        {
            result = {};
            std::stringstream ss(found->second);
            std::string item;
            while (std::getline(ss, item, ':'))
                result.push_back(item);
        }
    }

    void Config::setOverride(const std::string& option, const std::string& value)
    {
        overrides[option] = value;
    }

    void Config::clearOverrides()
    {
        overrides.clear();
    }

    void Config::parseOverride(const std::string& value, int32_t& result)
    {
        if (!parseWhole(value, result))
            Logger::warn("invalid int32_t override value: " + value);
    }

    void Config::parseOverride(const std::string& value, uint32_t& result)
    {
        if (!parseWhole(value, result))
            Logger::warn("invalid uint32_t override value: " + value);
    }

    void Config::parseOverride(const std::string& value, float& result)
    {
        if (!parseWhole(value, result))
            Logger::warn("invalid float override value: " + value);
    }

    void Config::parseOverride(const std::string& value, bool& result)
    {
        if (!parseBool(value, result))
            Logger::warn("invalid bool override value: " + value);
    }

    void Config::parseOverride(const std::string& value, std::string& result)
    {
        result = value;
    }

    void Config::parseOverride(const std::string& value, std::vector<std::string>& result)
    {
        result = {};
        std::stringstream ss(value);
        std::string item;
        while (std::getline(ss, item, ':'))
            result.push_back(item);
    }

    std::unordered_map<std::string, std::string> Config::getEffectDefinitions() const
    {
        std::unordered_map<std::string, std::string> effects;
        for (const auto& [key, value] : options)
        {
            if (value.size() >= 3 && value.substr(value.size() - 3) == ".fx")
                effects[key] = value;
        }
        return effects;
    }

    std::map<std::string, std::string> Config::getGlobalDefinitions() const
    {
        std::map<std::string, std::string> definitions = pFallback ? pFallback->getGlobalDefinitions() : std::map<std::string, std::string>{};
        for (const auto& [key, value] : options)
        {
            if (key.size() > 1 && key[0] == '@')
                definitions[key.substr(1)] = value;
        }
        return definitions;
    }

}
