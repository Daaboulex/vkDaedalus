#ifndef RESHADE_EFFECT_FILES_HPP_INCLUDED
#define RESHADE_EFFECT_FILES_HPP_INCLUDED

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace vkBasalt
{
    inline bool isReshadeEffectFile(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        return extension == ".fx";
    }

    inline std::vector<std::filesystem::path> reshadeEffectFilesIn(const std::filesystem::path& dir, std::error_code& ec)
    {
        std::vector<std::filesystem::path> files;
        std::filesystem::directory_iterator it(dir, ec);
        for (const std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
        {
            std::error_code entryEc;
            if (it->is_regular_file(entryEc) && isReshadeEffectFile(it->path()))
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        return files;
    }
}

#endif
