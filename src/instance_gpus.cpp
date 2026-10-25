#include "instance_gpus.hpp"

#include <algorithm>
#include <cstring>

namespace vkBasalt
{
    std::vector<VkExtensionProperties> deviceExtensions(const InstanceDispatch& vki, VkPhysicalDevice physical)
    {
        uint32_t count = 0;
        if (vki.EnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS)
            return {};
        std::vector<VkExtensionProperties> offered(count);
        if (vki.EnumerateDeviceExtensionProperties(physical, nullptr, &count, offered.data()) < 0)
            return {};
        offered.resize(count);
        return offered;
    }

    bool offersExtension(const std::vector<VkExtensionProperties>& offered, const char* name)
    {
        return std::any_of(offered.begin(), offered.end(), [name](const VkExtensionProperties& p) { return !std::strcmp(p.extensionName, name); });
    }

    namespace
    {
        std::optional<PciAddress> pciAddress(const InstanceDispatch& vki, VkPhysicalDevice physical, const VkPhysicalDeviceProperties& properties)
        {
            if (properties.apiVersion < VK_API_VERSION_1_1 || !vki.GetPhysicalDeviceProperties2
                || !offersExtension(deviceExtensions(vki, physical), VK_EXT_PCI_BUS_INFO_EXTENSION_NAME))
                return std::nullopt;
            VkPhysicalDevicePCIBusInfoPropertiesEXT pci{};
            pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
            VkPhysicalDeviceProperties2 properties2{};
            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties2.pNext = &pci;
            vki.GetPhysicalDeviceProperties2(physical, &properties2);
            return PciAddress{pci.pciDomain, pci.pciBus, pci.pciDevice, pci.pciFunction};
        }
    }

    std::vector<InstanceGpu> listInstanceGpus(const InstanceDispatch& vki, VkInstance instance)
    {
        uint32_t count = 0;
        if (!vki.EnumeratePhysicalDevices || vki.EnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS)
            return {};
        std::vector<VkPhysicalDevice> physicals(count);
        if (vki.EnumeratePhysicalDevices(instance, &count, physicals.data()) < 0)
            return {};
        physicals.resize(count);

        std::vector<InstanceGpu> gpus;
        for (VkPhysicalDevice physical : physicals)
        {
            InstanceGpu gpu;
            gpu.physical = physical;
            vki.GetPhysicalDeviceProperties(physical, &gpu.properties);
            gpu.id = {gpu.properties.vendorID, gpu.properties.deviceID, pciAddress(vki, physical, gpu.properties)};
            gpus.push_back(gpu);
        }
        return gpus;
    }
}
