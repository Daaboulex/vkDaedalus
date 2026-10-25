#include "util.hpp"

#include <cstring>
#include <link.h>

namespace vkBasalt
{
    namespace
    {
        std::string conflictingLayerSeen;
        bool        conflictingLayerScanned = false;
    }

    void rescanForConflictingLayer()
    {
        conflictingLayerScanned = true;
        if (!conflictingLayerSeen.empty())
            return;
        dl_iterate_phdr(
            [](struct dl_phdr_info* info, size_t, void* out) {
                if (!info->dlpi_name || !*info->dlpi_name)
                    return 0;
                const char* base = std::strrchr(info->dlpi_name, '/');
                base             = base ? base + 1 : info->dlpi_name;
                if (std::strcmp(base, "libvkbasalt.so") == 0 || std::strncmp(base, "libvkbasalt.so.", 15) == 0)
                {
                    *static_cast<std::string*>(out) = info->dlpi_name;
                    return 1;
                }
                return 0;
            },
            &conflictingLayerSeen);
    }

    const std::string& conflictingLayerPath()
    {
        if (!conflictingLayerScanned)
            rescanForConflictingLayer();
        return conflictingLayerSeen;
    }

    bool conflictingLayerLoaded()
    {
        return !conflictingLayerPath().empty();
    }

    void addUniqueCString(std::vector<const char*>& stringVector, const char* addString)
    {
        for (const char* other : stringVector)
        {
            if (other == std::string(addString))
            {
                return;
            }
        }
        stringVector.push_back(addString);
    }
}
