#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace vkBasalt
{
    inline std::string definitionTitle(std::string_view name)
    {
        constexpr std::string_view reshade = "RESHADE_";
        if (name.size() > reshade.size() && name.substr(0, reshade.size()) == reshade)
            name.remove_prefix(reshade.size());
        std::string title;
        for (const char c : name)
        {
            if (c == '_')
            {
                if (!title.empty() && title.back() != ' ')
                    title += ' ';
                continue;
            }
            title += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        while (!title.empty() && title.back() == ' ')
            title.pop_back();
        if (title.empty())
            return std::string(name);
        title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
        return title;
    }

    inline bool definitionIsSwitch(std::string_view name, std::string_view value, std::string_view defaultValue)
    {
        const auto binary = [](std::string_view text) { return text == "0" || text == "1"; };
        if (!binary(value) || !(defaultValue.empty() || binary(defaultValue)))
            return false;
        while (!name.empty())
        {
            const size_t           end  = name.find('_');
            const std::string_view word = name.substr(0, end);
            for (const std::string_view yesNo : {"IS", "USE", "USES", "ENABLE", "ENABLED", "DISABLE", "DISABLED", "HAS", "SHOW", "HIDE"})
                if (word == yesNo)
                    return true;
            if (end == std::string_view::npos)
                break;
            name.remove_prefix(end + 1);
        }
        return false;
    }
}
