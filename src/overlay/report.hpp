#pragma once

#include <string>
#include <utility>
#include <vector>

namespace vkBasalt
{
    struct ReportFacts
    {
        std::string                                      when;
        std::string                                      game;
        std::string                                      profile;
        std::vector<std::string>                         gpus;
        std::vector<std::string>                         layersBelow;
        std::string                                      frameGenerationPlace;
        std::vector<std::pair<std::string, std::string>> files;
    };

    inline std::string diagnosticReport(const ReportFacts& facts, bool safeAntiCheat)
    {
        std::string out = "vkDaedalus report, " + facts.when + "\n\n";
        out += "Game: " + facts.game + "\nProfile: " + facts.profile + "\nSafe Anti-Cheat: " + (safeAntiCheat ? "on" : "off") + "\n";
        out += "\nGPUs:\n";
        for (const std::string& gpu : facts.gpus)
            out += "  " + gpu + "\n";
        out += "\nLayers below vkDaedalus, nearest first:\n";
        for (const std::string& layer : facts.layersBelow)
            out += "  " + layer + "\n";
        if (facts.layersBelow.empty())
            out += "  none\n";
        out += "lsfg-vk: " + facts.frameGenerationPlace + "\n";
        for (const auto& [name, text] : facts.files)
            out += "\n== " + name + "\n" + (text.empty() ? std::string("(empty or missing)\n") : text) + (text.empty() || text.back() == '\n' ? "" : "\n");
        return out;
    }

    inline std::string downloadFolderOf(const std::string& userDirs, const std::string& home)
    {
        const std::string key = "XDG_DOWNLOAD_DIR=";
        size_t            at  = 0;
        while ((at = userDirs.find(key, at)) != std::string::npos)
        {
            if (at > 0 && userDirs[at - 1] != '\n')
            {
                at += key.size();
                continue;
            }
            const size_t      end   = userDirs.find('\n', at);
            std::string       value = userDirs.substr(at + key.size(), end == std::string::npos ? std::string::npos : end - at - key.size());
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (value.rfind("$HOME", 0) == 0)
                value = home + value.substr(5);
            if (!value.empty() && value.front() == '/' && value != home)
                return value;
            break;
        }
        return home.empty() ? "" : home + "/Downloads";
    }
}
