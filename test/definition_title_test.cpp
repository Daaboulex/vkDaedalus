#include "overlay/definition_title.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

int main()
{
    expect(vkBasalt::definitionTitle("RESHADE_DEPTH_INPUT_IS_REVERSED") == "Depth input is reversed", "ReShade's own prefix goes and the words read as a sentence");
    expect(vkBasalt::definitionTitle("USE_GATE") == "Use gate", "an effect's definition reads as words");
    expect(vkBasalt::definitionTitle("_FAST__MODE_") == "Fast mode", "leading, doubled and trailing underscores leave single spaces only");
    expect(vkBasalt::definitionTitle("RESHADE_") == "Reshade", "a name that is only the prefix keeps it");
    expect(vkBasalt::definitionTitle("___") == "___", "a name with no letters is shown as it is");

    expect(vkBasalt::definitionIsSwitch("RESHADE_DEPTH_INPUT_IS_REVERSED", "1", "1"), "a 0 or 1 whose name says IS is a switch");
    expect(vkBasalt::definitionIsSwitch("USE_GATE", "0", "0"), "a 0 or 1 whose name says USE is a switch");
    expect(vkBasalt::definitionIsSwitch("MY_FLAG_ENABLED", "1", ""), "an added 0 or 1 whose name says ENABLED is a switch");
    expect(!vkBasalt::definitionIsSwitch("QUALITY", "1", "1"), "a 0 or 1 whose name says no yes or no is a number");
    expect(!vkBasalt::definitionIsSwitch("USE_GATE", "2", "0"), "a value other than 0 or 1 is no switch");
    expect(!vkBasalt::definitionIsSwitch("USE_LEVEL", "1", "3"), "a default other than 0 or 1 is no switch");
    expect(!vkBasalt::definitionIsSwitch("ISOLATE_SKY", "1", "1"), "a word that only starts with IS is no yes or no");
    return failures == 0 ? 0 : 1;
}
