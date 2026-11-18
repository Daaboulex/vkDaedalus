#include "profile_undo.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace vkBasalt
{
    namespace
    {
        std::string trimmed(const std::string& text)
        {
            const size_t start = text.find_first_not_of(" \t\r");
            const size_t end   = text.find_last_not_of(" \t\r");
            return start == std::string::npos ? std::string() : text.substr(start, end - start + 1);
        }

        std::vector<std::pair<std::string, std::string>> settingsOf(const std::string& text)
        {
            std::vector<std::pair<std::string, std::string>> settings;
            std::istringstream                               lines(text);
            std::string                                      line;
            while (std::getline(lines, line))
            {
                const std::string kept = trimmed(line);
                if (kept.empty() || kept[0] == '#' || kept[0] == '[')
                    continue;
                const size_t equals = kept.find('=');
                if (equals == std::string::npos)
                    continue;
                settings.emplace_back(trimmed(kept.substr(0, equals)), trimmed(kept.substr(equals + 1)));
            }
            return settings;
        }
    } // namespace

    std::string profileUndoWords(const std::string& now, const std::string& before)
    {
        const auto                         previous = settingsOf(before);
        const auto                         current  = settingsOf(now);
        std::map<std::string, std::string> nowByKey(current.begin(), current.end());
        std::map<std::string, std::string> beforeByKey(previous.begin(), previous.end());
        std::vector<std::string>           changes;
        for (const auto& [key, value] : previous)
        {
            const auto found = nowByKey.find(key);
            if (found == nowByKey.end() || found->second != value)
                changes.push_back(key + " back to " + value);
        }
        for (const auto& [key, value] : current)
            if (!beforeByKey.contains(key))
                changes.push_back("remove " + key);
        if (changes.empty())
            return "Undo the last saved change";
        std::string words = "Undo: " + changes[0];
        if (changes.size() > 1)
            words += ", " + changes[1];
        if (changes.size() > 2)
            words += ", and " + std::to_string(changes.size() - 2) + " more";
        return words;
    }

    void rememberProfileState(std::vector<std::string>& undo, const std::string& before, const std::string& after)
    {
        if (before == after)
            return;
        undo.push_back(before);
        if (undo.size() > profileUndoDepth)
            undo.erase(undo.begin(), undo.begin() + static_cast<std::ptrdiff_t>(undo.size() - profileUndoDepth));
    }

    std::string readProfileText(const std::string& path)
    {
        std::ifstream      file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }
} // namespace vkBasalt
