#include "effects/reshade_pass_utils.hpp"

#include <cstdio>
#include <vector>

using namespace vkBasalt;

int main()
{
    reshadefx::pass compute;
    compute.cs_entry_point = "Compute";

    reshadefx::pass backbuffer;
    backbuffer.vs_entry_point = "VS";
    backbuffer.ps_entry_point = "PS";

    reshadefx::pass auxiliary = backbuffer;
    auxiliary.render_target_names[0] = "AuxiliaryTexture";

    reshadefx::technique technique;
    technique.passes = {compute, compute, compute, compute, compute, backbuffer, auxiliary};

    if (writesReshadeBackBuffer(compute))
    {
        std::puts("compute pass was incorrectly counted as a backbuffer write");
        return 1;
    }
    if (!writesReshadeBackBuffer(backbuffer))
    {
        std::puts("graphics backbuffer pass was not counted");
        return 1;
    }
    if (writesReshadeBackBuffer(auxiliary))
    {
        std::puts("auxiliary render-target pass was incorrectly counted as a backbuffer write");
        return 1;
    }
    if (countReshadeBackBufferWrites(technique) != 1)
    {
        std::puts("mixed technique did not report exactly one backbuffer write");
        return 1;
    }

    const std::vector<uint32_t> header = {0x07230203u, 0x00010300u, 0u, 100u, 0u};
    std::vector<uint32_t> computeModule = header;
    computeModule.insert(computeModule.end(), {(2u << 16) | 17u, 1u});
    computeModule.insert(computeModule.end(), {(6u << 16) | 16u, 4u, 17u, 2048u, 4u, 1u});
    const auto localSize = spirvLocalSize(computeModule);
    if (!localSize || (*localSize)[0] != 2048u || (*localSize)[1] != 4u || (*localSize)[2] != 1u)
    {
        std::puts("the compute module's LocalSize 2048 4 1 was not read");
        return 1;
    }
    std::vector<uint32_t> truncated = header;
    truncated.insert(truncated.end(), {(6u << 16) | 16u, 4u, 17u});
    if (spirvLocalSize(truncated))
    {
        std::puts("a LocalSize instruction running past the module's end was read");
        return 1;
    }

    std::puts("ReShade pass accounting: all assertions passed");
    return 0;
}
