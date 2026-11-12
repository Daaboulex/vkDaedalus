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

static std::string defineValue(VkFormat format, VkColorSpaceKHR colorSpace, const std::string& name, const ReshadeIdentity& identity = {})
{
    for (const auto& [key, value] : reshadeCompileDefines({1920, 1080}, format, colorSpace, identity, {{"MY_DEF", "7", "", "fx"}}))
        if (key == name)
            return value;
    return "";
}

static std::string baseMacro(bool liveUniforms, const std::string& macro)
{
    reshadefx::preprocessor pp;
    addReshadeBaseMacros(pp, liveUniforms);
    pp.append_string("int value = " + macro + ";\n");
    const std::string out = pp.output();
    const size_t      at  = out.find("int value = ");
    return at == std::string::npos ? "" : out.substr(at + 12, 1);
}

static std::string number(reshade::api::format format)
{
    return std::to_string(static_cast<uint32_t>(format));
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
    for (const auto& [name, value] : reshadeCompileDefines({1920, 1080}, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, ReshadeIdentity{}, {}))
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
    expect(baseMacro(false, "__RESHADE_PERFORMANCE_MODE__") == "1", "uniforms folded to constants is ReShade's performance mode");
    expect(baseMacro(true, "__RESHADE_PERFORMANCE_MODE__") == "0", "live uniforms are not performance mode, so shaders keep their tweakable paths");
    expect(baseMacro(true, "__RESHADE_PERMUTATION__") == "0", "the one permutation, the back buffer's, is ReShade's permutation 0");

    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "BUFFER_COLOR_FORMAT") == number(reshade::api::format::b8g8r8a8_unorm),
           "a BGRA back buffer is ReShade's b8g8r8a8_unorm");
    expect(defineValue(VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "BUFFER_COLOR_FORMAT") == number(reshade::api::format::r8g8b8a8_unorm),
           "an RGBA back buffer is ReShade's r8g8b8a8_unorm");
    expect(defineValue(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT, "BUFFER_COLOR_FORMAT") == number(reshade::api::format::r10g10b10a2_unorm),
           "the HDR10 swapchain is ReShade's r10g10b10a2_unorm");
    expect(defineValue(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, "BUFFER_COLOR_FORMAT") == number(reshade::api::format::r16g16b16a16_float),
           "the scRGB swapchain is ReShade's r16g16b16a16_float");
    expect(defineValue(VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "BUFFER_COLOR_FORMAT") == std::to_string(static_cast<uint32_t>(reshadefx::texture_format::rgba8)),
           "on an RGBA back buffer, Format = BUFFER_COLOR_FORMAT names the RGBA8 texture format, as ReShade folds formats for");
    for (const reshadefx::texture_format named : {reshadefx::texture_format::r8, reshadefx::texture_format::r16, reshadefx::texture_format::r16f, reshadefx::texture_format::r32f,
                                                reshadefx::texture_format::r32u, reshadefx::texture_format::r32i, reshadefx::texture_format::rg8, reshadefx::texture_format::rg16,
                                                reshadefx::texture_format::rg16f, reshadefx::texture_format::rg32f, reshadefx::texture_format::rgba8, reshadefx::texture_format::rgba16,
                                                reshadefx::texture_format::rgba16f, reshadefx::texture_format::rgba32f, reshadefx::texture_format::rgba32u, reshadefx::texture_format::rgba32i,
                                                reshadefx::texture_format::rgb10a2, reshadefx::texture_format::rg11b10f})
        expect(vulkanFormatOf(static_cast<reshade::api::format>(named)) != VK_FORMAT_UNDEFINED, "every texture format an effect can name has a Vulkan format");
    expect(vulkanFormatOf(static_cast<reshade::api::format>(std::stoul(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "BUFFER_COLOR_FORMAT"))))
               == VK_FORMAT_B8G8R8A8_UNORM,
           "a texture declared with Format = BUFFER_COLOR_FORMAT on a BGRA back buffer, which ReShade cannot create, is created as BGRA");

    expect(reshadeRendererId(VK_API_VERSION_1_3, VK_API_VERSION_1_3) == 0x21300, "a game running Vulkan 1.3 is ReShade's renderer 0x21300");
    expect(reshadeRendererId(VK_API_VERSION_1_0, VK_API_VERSION_1_3) == 0x21100, "a game below Vulkan 1.2 reports 1.1, as ReShade does");
    expect(reshadeRendererId(VK_API_VERSION_1_2, VK_MAKE_API_VERSION(0, 1, 4, 0)) == 0x21200, "the version the game runs at decides, not the GPU's highest");
    expect(reshadeRendererId(VK_MAKE_API_VERSION(0, 1, 4, 0), VK_MAKE_API_VERSION(0, 1, 4, 309)) == 0x21400, "from Vulkan 1.4 the GPU's own version is reported");
    const ReshadeIdentity identity{0x1002, 0x7550, 0x21300, 0xABCD1234};
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "__RENDERER__", identity) == std::to_string(0x21300), "__RENDERER__ is the identity's renderer");
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "__VENDOR__", identity) == "4098", "__VENDOR__ is the GPU's PCI vendor id");
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "__DEVICE__", identity) == "30032", "__DEVICE__ is the GPU's PCI device id");
    expect(defineValue(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "__APPLICATION__", identity) == std::to_string(0xABCD1234U),
           "__APPLICATION__ is the program's hash");
    const bool wide = sizeof(size_t) == 8;
    expect(reshadeApplicationHash("") == (wide ? 0x84222325U : 0x811c9dc5U), "the empty name hashes to FNV-1a's offset basis, as MSVC's std::hash does");
    expect(reshadeApplicationHash("a") == (wide ? 0x8601ec8cU : 0xe40c292cU), "\"a\" hashes to FNV-1a's published value, cut to 32 bits");
    expect(reshadeApplicationHash("foobar") == (wide ? 0xf73967e8U : 0xbf9cf968U), "\"foobar\" hashes to FNV-1a's published value, cut to 32 bits");

    if (failures != 0)
        return 1;
    std::printf("OK: the compile defines describe the back buffer the way ReShade does\n");
    return 0;
}
