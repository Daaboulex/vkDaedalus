#ifndef LUT_CUBE_HPP_INCLUDED
#define LUT_CUBE_HPP_INCLUDED

#include <cstdint>
#include <string>
#include <vector>

namespace vkBasalt
{
    bool isCubeLutPath(const std::string& path);

    class LutCube
    {
    public:
        uint32_t                   size = 0;
        std::vector<unsigned char> colorCube;

        explicit LutCube(const std::string& path);
    };

}
#endif
