#include "shader_cache.hpp"

#include <cstdio>
#include <map>
#include <string>

namespace
{
    constexpr uint32_t opDecorate        = 71;
    constexpr uint32_t decorationBinding = 33;

    struct LayerSlot
    {
        std::string name;
        size_t      index;
    };
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::printf("usage: spirv-binding-test <fixture.fx> <sampler and storage bindings across its entry point modules>\n");
        return 2;
    }

    const auto compiled = vkBasalt::getOrCompileReshadeEffect(argv[1], {}, {});
    if (!compiled->ok())
    {
        std::printf("the fixture does not compile: %s\n", compiled->error.c_str());
        return 1;
    }

    std::map<uint32_t, LayerSlot> layerSlots;
    for (size_t i = 0; i < compiled->module.samplers.size(); i++)
        layerSlots[compiled->module.samplers[i].id] = {compiled->module.samplers[i].unique_name, i};
    for (size_t i = 0; i < compiled->module.storages.size(); i++)
        layerSlots[compiled->module.storages[i].id] = {compiled->module.storages[i].unique_name, i};

    int checked  = 0;
    int failures = 0;
    for (const auto& [entryPoint, words] : compiled->entryPointSpirv)
    {
        size_t at = 5;
        while (at < words.size())
        {
            const uint32_t count = words[at] >> 16;
            if (count == 0 || at + count > words.size())
            {
                std::printf("entry point %s is not well-formed SPIR-V at word %zu\n", entryPoint.c_str(), at);
                return 1;
            }
            if ((words[at] & 0xffff) == opDecorate && count == 4 && words[at + 2] == decorationBinding)
            {
                const auto slot = layerSlots.find(words[at + 1]);
                if (slot != layerSlots.end())
                {
                    checked++;
                    if (words[at + 3] != slot->second.index)
                    {
                        std::printf("entry point %s reads %s at binding %u, but the layer writes it at binding %zu\n",
                                    entryPoint.c_str(), slot->second.name.c_str(), words[at + 3], slot->second.index);
                        failures++;
                    }
                }
            }
            at += count;
        }
    }

    if (checked != std::stoi(argv[2]))
    {
        std::printf("found %d sampler and storage bindings across the entry point modules, the fixture carries %s\n", checked, argv[2]);
        return 1;
    }
    if (failures != 0)
        return 1;

    std::printf("every sampler and storage sits at the binding the layer writes it to\n");
    return 0;
}
