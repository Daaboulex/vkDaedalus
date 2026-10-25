#include "lut_cube.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace vkBasalt
{
    namespace
    {
        constexpr uint32_t minSide = 2;
        constexpr uint32_t maxSide = 256;

        [[noreturn]] void refuse(const std::string& path, size_t lineNumber, const std::string& why)
        {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": " + why);
        }

        std::vector<std::string> wordsOf(const std::string& line)
        {
            std::vector<std::string> words;
            std::istringstream       stream(line);
            std::string              word;
            while (stream >> word)
                words.push_back(word);
            return words;
        }

        float numberOf(const std::string& word, const std::string& path, size_t lineNumber)
        {
            float      value = 0.0f;
            const auto end   = word.data() + word.size();
            const auto [at, error] = std::from_chars(word.data(), end, value);
            if (error != std::errc() || at != end || !std::isfinite(value))
                refuse(path, lineNumber, "'" + word + "' is not a number");
            return value;
        }

        std::array<float, 3> tripleOf(const std::vector<std::string>& words, const std::string& path, size_t lineNumber)
        {
            return {numberOf(words[1], path, lineNumber), numberOf(words[2], path, lineNumber), numberOf(words[3], path, lineNumber)};
        }
    }

    bool isCubeLutPath(const std::string& path)
    {
        if (path.size() < 5)
            return false;
        std::string suffix = path.substr(path.size() - 5);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return std::tolower(c); });
        return suffix == ".cube";
    }

    LutCube::LutCube(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            refuse(path, 0, "cannot be opened");

        std::array<float, 3> domainMin  = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> domainMax  = {1.0f, 1.0f, 1.0f};
        size_t               texels     = 0;
        size_t               lineNumber = 0;
        std::string          line;

        while (std::getline(file, line))
        {
            lineNumber++;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const std::vector<std::string> words = wordsOf(line);
            if (words.empty() || words[0][0] == '#' || words[0] == "TITLE")
                continue;

            const std::string& key = words[0];
            if (key == "LUT_3D_SIZE")
            {
                if (size != 0 || words.size() != 2)
                    refuse(path, lineNumber, "LUT_3D_SIZE must appear once, with one value");
                uint32_t   side = 0;
                const auto end  = words[1].data() + words[1].size();
                const auto [at, error] = std::from_chars(words[1].data(), end, side);
                if (error != std::errc() || at != end || side < minSide || side > maxSide)
                    refuse(path, lineNumber, "LUT_3D_SIZE must be a whole number from 2 to 256, not '" + words[1] + "'");
                size = side;
                colorCube.assign(static_cast<size_t>(size) * size * size * 4, 255);
                continue;
            }
            if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX")
            {
                if (words.size() != 4)
                    refuse(path, lineNumber, key + " takes three numbers");
                (key == "DOMAIN_MIN" ? domainMin : domainMax) = tripleOf(words, path, lineNumber);
                continue;
            }
            if (key == "LUT_3D_INPUT_RANGE")
            {
                if (words.size() != 3)
                    refuse(path, lineNumber, "LUT_3D_INPUT_RANGE takes two numbers");
                const float low  = numberOf(words[1], path, lineNumber);
                const float high = numberOf(words[2], path, lineNumber);
                domainMin        = {low, low, low};
                domainMax        = {high, high, high};
                continue;
            }
            if (key == "LUT_1D_SIZE")
                refuse(path, lineNumber, "is a 1D LUT; only 3D cube LUTs are read");
            if (std::isalpha(static_cast<unsigned char>(key[0])))
                refuse(path, lineNumber, "unknown keyword " + key);

            if (size == 0)
                refuse(path, lineNumber, "data comes before LUT_3D_SIZE");
            if (words.size() != 3)
                refuse(path, lineNumber, "a data line holds three numbers");
            if (texels == static_cast<size_t>(size) * size * size)
                refuse(path, lineNumber, "more data lines than a cube of side " + std::to_string(size) + " holds");
            for (size_t channel = 0; channel < 3; channel++)
            {
                if (!(domainMax[channel] > domainMin[channel]))
                    refuse(path, lineNumber, "the domain is empty or inverted");
                const float value = numberOf(words[channel], path, lineNumber);
                const float unit  = std::clamp((value - domainMin[channel]) / (domainMax[channel] - domainMin[channel]), 0.0f, 1.0f);
                colorCube[texels * 4 + channel] = static_cast<unsigned char>(std::lround(unit * 255.0f));
            }
            texels++;
        }

        if (size == 0)
            refuse(path, lineNumber, "has no LUT_3D_SIZE");
        if (texels != static_cast<size_t>(size) * size * size)
            refuse(path, lineNumber,
                   "holds " + std::to_string(texels) + " data lines, a cube of side " + std::to_string(size) + " needs "
                       + std::to_string(static_cast<size_t>(size) * size * size));
    }

}
