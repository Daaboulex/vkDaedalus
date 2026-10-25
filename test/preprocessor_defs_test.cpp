#include "reshade_parser.hpp"

#include <cstdio>
#include <string>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static const PreprocessorDefinition* find(const std::vector<PreprocessorDefinition>& defs, const std::string& name)
{
    for (const auto& def : defs)
        if (def.name == name)
            return &def;
    return nullptr;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: preprocessor-defs-test <layer_set_define.fx>\n");
        return 2;
    }

    const std::vector<PreprocessorDefinition> defs = extractPreprocessorDefinitions("layer_set_define", argv[1]);

    const PreprocessorDefinition* strength = find(defs, "USER_STRENGTH");
    expect(strength != nullptr && strength->effectName == "layer_set_define",
           "the effect's own definition USER_STRENGTH is listed under the effect");
    expect(strength == nullptr || strength->defaultValue == "3", "USER_STRENGTH keeps the effect's default 3");

    const PreprocessorDefinition* reversed = find(defs, "RESHADE_DEPTH_INPUT_IS_REVERSED");
    expect(reversed != nullptr && reversed->effectName.empty(),
           "ReShade's depth setting RESHADE_DEPTH_INPUT_IS_REVERSED is offered as a global definition, under no effect");
    expect(reversed == nullptr || reversed->defaultValue == "1", "the global definition keeps the effect's default 1");

    for (const char* name : {"BUFFER_COLOR_SPACE", "BUFFER_COLOR_BIT_DEPTH"})
        expect(find(defs, name) == nullptr, std::string(name) + " is the runtime's, so nothing offers it for editing");

    if (failures != 0)
        return 1;
    std::printf("OK: an effect lists its own definitions and offers ReShade's settings as global ones\n");
    return 0;
}
