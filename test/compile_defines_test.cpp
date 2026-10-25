#include "reshade_compile_defines.hpp"
#include "reshade_fx_env.hpp"

#include <cstdio>
#include <string>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

static std::string defineValue(VkFormat format, VkColorSpaceKHR colorSpace, const std::string& name)
{
    for (const auto& [key, value] : reshadeCompileDefines({1920, 1080}, format, colorSpace, {{"MY_DEF", "7", "", "fx"}}))
        if (key == name)
            return value;
    return "";
}

static std::string performanceMode(bool liveUniforms)
{
    reshadefx::preprocessor pp;
    addReshadeBaseMacros(pp, liveUniforms);
    pp.append_string("int mode = __RESHADE_PERFORMANCE_MODE__;\n");
    const std::string out = pp.output();
    const size_t      at  = out.find("int mode = ");
    return at == std::string::npos ? "" : out.substr(at + 11, 1);
}

int main()
{
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "BUFFER_COLOR_DEPTH") == "8",
           "an 8-bit back buffer reports 8 bits");
    expect(defineValue(VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "BUFFER_COLOR_DEPTH") == "10",
           "an A2R10G10B10 back buffer reports 10 bits");
    expect(defineValue(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT, "BUFFER_COLOR_DEPTH") == "10",
           "an A2B10G10R10 back buffer, the usual HDR10 swapchain, reports 10 bits");
    expect(defineValue(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, "BUFFER_COLOR_DEPTH") == "16",
           "a 16-bit float back buffer, the scRGB swapchain, reports 16 bits");
    expect(defineValue(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT, "BUFFER_COLOR_SPACE") == "3",
           "HDR10 PQ is ReShade's colour space 3");
    expect(defineValue(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, "BUFFER_COLOR_SPACE") == "2",
           "scRGB is ReShade's colour space 2");
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT, "BUFFER_COLOR_SPACE") == "0",
           "a colour space ReShade does not name is its unknown, 0");
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "MY_DEF") == "7",
           "the effect's own definitions come after the buffer ones");
    for (const auto& [name, value] : reshadeCompileDefines({1920, 1080}, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, {}))
        expect(reshadeDefinitionScope(name) == DefinitionScope::Runtime,
               "every definition the layer sets is in the runtime's namespace, so no effect lists one for editing");
    expect(reshadeDefinitionScope("RESHADE_DEPTH_INPUT_IS_REVERSED") == DefinitionScope::Global,
           "ReShade's depth settings are global definitions");
    expect(reshadeDefinitionScope("USER_STRENGTH") == DefinitionScope::Effect, "a name of the effect's own is the effect's");
    expect(globalDefinitionProblem("RESHADE_DEPTH_INPUT_IS_REVERSED", "0").empty(), "a global definition with a name and a value is taken");
    expect(!globalDefinitionProblem("RESHADE_DEPTH_INPUT_IS_REVERSED", "").empty(),
           "an empty global definition is refused, as #if NAME would not compile in any effect");
    expect(!globalDefinitionProblem("RESHADE_DEPTH_INPUT_IS_REVERSED", "  ").empty(), "a global definition of only spaces is refused");
    expect(!globalDefinitionProblem("1ST", "1").empty(), "a global definition named with a leading digit is refused");
    expect(!globalDefinitionProblem("BUFFER_WIDTH", "800").empty(), "a global definition of a name the layer sets is refused");
    expect(performanceMode(false) == "1", "uniforms folded to constants is ReShade's performance mode");
    expect(performanceMode(true) == "0", "live uniforms are not performance mode, so shaders keep their tweakable paths");

    if (failures != 0)
        return 1;
    std::printf("OK: the compile defines describe the back buffer the way ReShade does\n");
    return 0;
}
