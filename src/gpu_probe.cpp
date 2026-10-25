#include "gpu_probe.hpp"

#include "instance_gpus.hpp"
#include "logical_device.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>

#include <dlfcn.h>

namespace vkBasalt
{
    namespace
    {
        using nvmlReturn_t = unsigned int;
        using nvmlDevice_t = void*;
        struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
        struct nvmlMemory_t { unsigned long long total; unsigned long long free; unsigned long long used; };
        struct nvmlPciInfo_t
        {
            char busIdLegacy[16];
            unsigned int domain;
            unsigned int bus;
            unsigned int device;
            unsigned int pciDeviceId;
            unsigned int pciSubSystemId;
            char busId[32];
        };

        static constexpr nvmlReturn_t NVML_SUCCESS = 0;

        struct NvmlState
        {
            void* lib = nullptr;
            bool initialized = false;

            nvmlReturn_t (*Init)() = nullptr;
            nvmlReturn_t (*Shutdown)() = nullptr;
            nvmlReturn_t (*DeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*) = nullptr;
            nvmlReturn_t (*DeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*) = nullptr;
            nvmlReturn_t (*DeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*) = nullptr;
            nvmlReturn_t (*DeviceGetCount)(unsigned int*) = nullptr;
            nvmlReturn_t (*DeviceGetPciInfo)(nvmlDevice_t, nvmlPciInfo_t*) = nullptr;
        };

        static NvmlState nvml;

        static bool initNvml()
        {
            if (nvml.initialized)
                return nvml.lib != nullptr;
            nvml.initialized = true;

            nvml.lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
            if (!nvml.lib)
                nvml.lib = dlopen("libnvidia-ml.so", RTLD_LAZY);
            if (!nvml.lib)
            {
                Logger::debug("NVML not available: " + std::string(dlerror()));
                return false;
            }

            nvml.Init = (decltype(nvml.Init))dlsym(nvml.lib, "nvmlInit_v2");
            if (!nvml.Init)
                nvml.Init = (decltype(nvml.Init))dlsym(nvml.lib, "nvmlInit");
            nvml.Shutdown = (decltype(nvml.Shutdown))dlsym(nvml.lib, "nvmlShutdown");
            nvml.DeviceGetHandleByIndex = (decltype(nvml.DeviceGetHandleByIndex))dlsym(nvml.lib, "nvmlDeviceGetHandleByIndex_v2");
            if (!nvml.DeviceGetHandleByIndex)
                nvml.DeviceGetHandleByIndex = (decltype(nvml.DeviceGetHandleByIndex))dlsym(nvml.lib, "nvmlDeviceGetHandleByIndex");
            nvml.DeviceGetUtilizationRates = (decltype(nvml.DeviceGetUtilizationRates))dlsym(nvml.lib, "nvmlDeviceGetUtilizationRates");
            nvml.DeviceGetMemoryInfo = (decltype(nvml.DeviceGetMemoryInfo))dlsym(nvml.lib, "nvmlDeviceGetMemoryInfo");
            nvml.DeviceGetCount = (decltype(nvml.DeviceGetCount))dlsym(nvml.lib, "nvmlDeviceGetCount_v2");
            if (!nvml.DeviceGetCount)
                nvml.DeviceGetCount = (decltype(nvml.DeviceGetCount))dlsym(nvml.lib, "nvmlDeviceGetCount");
            nvml.DeviceGetPciInfo = (decltype(nvml.DeviceGetPciInfo))dlsym(nvml.lib, "nvmlDeviceGetPciInfo_v3");

            if (!nvml.Init || !nvml.DeviceGetHandleByIndex || !nvml.DeviceGetUtilizationRates || !nvml.DeviceGetMemoryInfo)
            {
                Logger::debug("NVML: missing required symbols");
                dlclose(nvml.lib);
                nvml.lib = nullptr;
                return false;
            }

            if (nvml.Init() != NVML_SUCCESS)
            {
                Logger::debug("NVML: nvmlInit failed");
                dlclose(nvml.lib);
                nvml.lib = nullptr;
                return false;
            }

            Logger::info("NVML initialized for GPU diagnostics");
            return true;
        }

        template<typename T>
        bool readSysfs(const std::string& path, T& value)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;
            file >> value;
            return !file.fail();
        }

        static uint16_t readHexId(const std::string& path)
        {
            std::ifstream f(path);
            if (!f.is_open())
                return 0;
            uint16_t id = 0;
            f >> std::hex >> id;
            return id;
        }

        static std::string pciSlotFromUevent(const std::string& devicePath)
        {
            std::ifstream f(devicePath + "/uevent");
            std::string line;
            while (std::getline(f, line))
            {
                if (line.rfind("PCI_SLOT_NAME=", 0) == 0)
                {
                    std::string slot = line.substr(14);
                    std::transform(slot.begin(), slot.end(), slot.begin(), [](unsigned char c) { return std::tolower(c); });
                    return slot;
                }
            }
            return "";
        }

        static void enumerateDrmCards(std::vector<GpuEntry>& inventory)
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm"))
                {
                    std::string node = entry.path().filename().string();
                    bool isCard = node.rfind("card", 0) == 0 && node.find('-') == std::string::npos;
                    bool isRender = node.rfind("renderD", 0) == 0;
                    if (!isCard && !isRender)
                        continue;

                    std::string devicePath = entry.path().string() + "/device";
                    std::string pci = pciSlotFromUevent(devicePath);
                    if (pci.empty())
                        continue;

                    GpuEntry* target = nullptr;
                    for (auto& e : inventory)
                        if (e.pciAddress == pci)
                            target = &e;
                    if (!target)
                    {
                        GpuEntry e;
                        e.pciAddress = pci;
                        uint16_t vendorId = readHexId(devicePath + "/vendor");
                        e.pciDeviceId = readHexId(devicePath + "/device");
                        switch (vendorId)
                        {
                            case 0x1002: e.vendor = GpuVendor::AMD; e.name = "AMD GPU"; break;
                            case 0x8086: e.vendor = GpuVendor::Intel; e.name = "Intel GPU"; break;
                            case 0x10de: e.vendor = GpuVendor::NVIDIA; e.name = "NVIDIA GPU"; break;
                            default: e.name = "GPU " + pci; break;
                        }
                        e.note = "not visible to this game's Vulkan";
                        inventory.push_back(std::move(e));
                        target = &inventory.back();
                    }

                    if (isCard)
                        target->drmCardPath = entry.path().string();
                    target->drmNodes.push_back("/dev/dri/" + node);
                }
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                Logger::debug(std::string("GPU inventory: sysfs scan failed: ") + e.what());
            }
        }

        static void attachVulkanDevices(LogicalDevice* device, std::vector<GpuEntry>& inventory)
        {
            if (!device || !device->instance)
                return;

            for (const InstanceGpu& gpu : listInstanceGpus(device->vki, device->instance))
            {
                const VkPhysicalDevice            pd    = gpu.physical;
                const VkPhysicalDeviceProperties& props = gpu.properties;
                const std::string                 pci   = gpu.id.pci ? formatPciAddress(*gpu.id.pci) : std::string();

                GpuEntry* match = nullptr;
                if (!pci.empty())
                    for (auto& e : inventory)
                        if (e.pciAddress == pci)
                            match = &e;
                if (!match)
                {
                    int candidates = 0;
                    GpuEntry* byId = nullptr;
                    for (auto& e : inventory)
                        if (!e.vulkanUsable && e.pciDeviceId != 0 && e.pciDeviceId == (props.deviceID & 0xFFFF))
                        {
                            byId = &e;
                            candidates++;
                        }
                    if (candidates == 1)
                        match = byId;
                }
                if (!match)
                {
                    inventory.push_back({});
                    match = &inventory.back();
                }

                match->vulkanUsable = true;
                match->name = props.deviceName;
                match->apiVersion = props.apiVersion;
                match->software = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
                match->isRenderer = (pd == device->physicalDevice);
                match->note = match->software ? "software rasterizer" : "";
                if (match->vendor == GpuVendor::Unknown)
                {
                    switch (props.vendorID)
                    {
                        case 0x1002: match->vendor = GpuVendor::AMD; break;
                        case 0x8086: match->vendor = GpuVendor::Intel; break;
                        case 0x10de: match->vendor = GpuVendor::NVIDIA; break;
                        default: break;
                    }
                }
            }
        }

        static void attachNvmlDevices(std::vector<GpuEntry>& inventory)
        {
            bool anyNvidia = false;
            for (const auto& e : inventory)
                anyNvidia = anyNvidia || e.vendor == GpuVendor::NVIDIA;
            if (!anyNvidia || !initNvml())
                return;

            unsigned int count = 0;
            if (nvml.DeviceGetCount && nvml.DeviceGetCount(&count) == NVML_SUCCESS && nvml.DeviceGetPciInfo)
            {
                for (unsigned int i = 0; i < count; i++)
                {
                    nvmlDevice_t handle = nullptr;
                    if (nvml.DeviceGetHandleByIndex(i, &handle) != NVML_SUCCESS)
                        continue;
                    nvmlPciInfo_t info = {};
                    if (nvml.DeviceGetPciInfo(handle, &info) != NVML_SUCCESS)
                        continue;
                    char buf[24];
                    snprintf(buf, sizeof(buf), "%04x:%02x:%02x.0", info.domain & 0xFFFF, info.bus, info.device);
                    for (auto& e : inventory)
                        if (e.pciAddress == buf)
                            e.nvmlDevice = handle;
                }
                return;
            }

            nvmlDevice_t handle = nullptr;
            if (nvml.DeviceGetHandleByIndex(0, &handle) == NVML_SUCCESS)
                for (auto& e : inventory)
                    if (e.vendor == GpuVendor::NVIDIA && !e.nvmlDevice)
                    {
                        e.nvmlDevice = handle;
                        break;
                    }
        }

    }

    void sampleDrmUsers(std::vector<GpuEntry>& inventory)
    {
        std::unordered_map<std::string, GpuEntry*> nodeToEntry;
        for (auto& e : inventory)
        {
            e.users.clear();
            for (const auto& n : e.drmNodes)
                nodeToEntry[n] = &e;
        }
        if (nodeToEntry.empty())
            return;

        std::error_code procEc;
        std::filesystem::directory_iterator procIt("/proc", procEc);
        for (const std::filesystem::directory_iterator end; !procEc && procIt != end; procIt.increment(procEc))
        {
            {
                const std::filesystem::directory_entry& proc = *procIt;
                const std::string pid = proc.path().filename().string();
                if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos)
                    continue;

                std::error_code ec;
                std::set<GpuEntry*> hits;
                for (const auto& fd : std::filesystem::directory_iterator(proc.path() / "fd", ec))
                {
                    std::error_code lec;
                    const std::filesystem::path target = std::filesystem::read_symlink(fd.path(), lec);
                    if (lec)
                        continue;
                    const auto it = nodeToEntry.find(target.string());
                    if (it != nodeToEntry.end())
                        hits.insert(it->second);
                }
                if (ec || hits.empty())
                    continue;

                std::ifstream cmdline(proc.path() / "cmdline");
                std::string   argv0;
                std::getline(cmdline, argv0, '\0');
                std::ifstream comm(proc.path() / "comm");
                std::string   commName;
                std::getline(comm, commName);
                std::string name = readableProgramName(argv0, commName);
                if (name.empty())
                    name = pid;
                for (GpuEntry* e : hits)
                    if (e->users.size() < 6 && std::find(e->users.begin(), e->users.end(), name) == e->users.end())
                        e->users.push_back(name);
            }
        }
        if (procEc)
        {
            static bool reported = false;
            if (!reported)
                Logger::warn(std::string("GPU users: cannot scan /proc: ") + procEc.message());
            reported = true;
        }
    }

    void sampleGpuStats(GpuEntry& e)
    {
        e.busyPercent = -1.0f;
        e.vramUsedMB = e.vramTotalMB = -1.0f;
        e.gttUsedMB = e.gttTotalMB = -1.0f;

        if (!e.drmCardPath.empty() && (e.vendor == GpuVendor::AMD || e.vendor == GpuVendor::Intel))
        {
            const std::string dev = e.drmCardPath + "/device";
            if (e.vendor == GpuVendor::AMD)
            {
                int busy = 0;
                if (readSysfs(dev + "/gpu_busy_percent", busy))
                    e.busyPercent = static_cast<float>(busy);
            }
            else
            {
                int actFreq = 0, maxFreq = 0;
                if (readSysfs(dev + "/gt_act_freq_mhz", actFreq) && readSysfs(dev + "/gt_max_freq_mhz", maxFreq) && maxFreq > 0)
                    e.busyPercent = std::min(100.0f, (static_cast<float>(actFreq) / static_cast<float>(maxFreq)) * 100.0f);
            }

            uint64_t used = 0, total = 0;
            if (readSysfs(dev + "/mem_info_vram_used", used) && readSysfs(dev + "/mem_info_vram_total", total) && total > 0)
            {
                e.vramUsedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                e.vramTotalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
            }
            if (readSysfs(dev + "/mem_info_gtt_used", used) && readSysfs(dev + "/mem_info_gtt_total", total) && total > 0)
            {
                e.gttUsedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                e.gttTotalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
            }
            return;
        }

        if (e.nvmlDevice && nvml.lib)
        {
            nvmlUtilization_t util = {};
            if (nvml.DeviceGetUtilizationRates(e.nvmlDevice, &util) == NVML_SUCCESS)
                e.busyPercent = static_cast<float>(util.gpu);
            nvmlMemory_t mem = {};
            if (nvml.DeviceGetMemoryInfo(e.nvmlDevice, &mem) == NVML_SUCCESS && mem.total > 0)
            {
                e.vramUsedMB = static_cast<float>(mem.used) / (1024.0f * 1024.0f);
                e.vramTotalMB = static_cast<float>(mem.total) / (1024.0f * 1024.0f);
            }
        }
    }


    std::vector<GpuEntry> buildGpuInventory(LogicalDevice* device)
    {
        std::vector<GpuEntry> inventory;
        enumerateDrmCards(inventory);
        attachVulkanDevices(device, inventory);
        attachNvmlDevices(inventory);

        std::stable_sort(inventory.begin(), inventory.end(), [](const GpuEntry& a, const GpuEntry& b) {
            if (a.isRenderer != b.isRenderer)
                return a.isRenderer;
            if (a.software != b.software)
                return b.software;
            return a.pciAddress < b.pciAddress;
        });

        for (const auto& e : inventory)
            Logger::info("GPU inventory: " + e.name + (e.pciAddress.empty() ? "" : " at " + e.pciAddress)
                         + (e.vulkanUsable ? " (Vulkan)" : " (no Vulkan)") + (e.isRenderer ? " [renderer]" : ""));
        return inventory;
    }
}
