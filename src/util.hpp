#ifndef UTIL_HPP_INCLUDED
#define UTIL_HPP_INCLUDED

#include <string>
#include <sstream>
#include <vector>

namespace vkBasalt
{
    void addUniqueCString(std::vector<const char*>& stringVector, const char* addString);

    bool conflictingLayerLoaded();

    const std::string& conflictingLayerPath();
    void               rescanForConflictingLayer();

    template<typename T>
    std::string convertToString(T object)
    {
        std::stringstream ss;
        ss << object;
        return ss.str();
    }
}

#endif
