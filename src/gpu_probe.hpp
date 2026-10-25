#ifndef GPU_PROBE_HPP_INCLUDED
#define GPU_PROBE_HPP_INCLUDED

#include <cstdint>
#include <string>
#include <vector>

namespace vkBasalt
{
    struct LogicalDevice;

    enum class GpuVendor { Unknown, AMD, Intel, NVIDIA };

    struct GpuEntry
    {
        GpuVendor    vendor = GpuVendor::Unknown;
        std::string  name;
        std::string  pciAddress;
        std::string  drmCardPath;
        std::vector<std::string> drmNodes;
        uint16_t     pciDeviceId = 0;
        bool         vulkanUsable = false;
        bool         software     = false;
        bool         isRenderer   = false;
        uint32_t     apiVersion   = 0;
        void*        nvmlDevice   = nullptr;
        std::string  note;
        float        busyPercent = -1.0f;
        float        vramUsedMB  = -1.0f;
        float        vramTotalMB = -1.0f;
        float        gttUsedMB   = -1.0f;
        float        gttTotalMB  = -1.0f;
        std::vector<std::string> users;
    };

    std::vector<GpuEntry> buildGpuInventory(LogicalDevice* device);
    void sampleGpuStats(GpuEntry& entry);
    void sampleDrmUsers(std::vector<GpuEntry>& inventory);

    inline std::string readableProgramName(const std::string& argv0, const std::string& comm)
    {
        std::string name = argv0.empty() ? comm : argv0.substr(argv0.find_last_of('/') + 1);
        const std::string wrapped = "-wrapped";
        if (name.size() > 1 && name.front() == '.')
            name.erase(0, 1);
        if (name.size() > wrapped.size() && name.compare(name.size() - wrapped.size(), wrapped.size(), wrapped) == 0)
            name.erase(name.size() - wrapped.size());
        return name;
    }
}

#endif
