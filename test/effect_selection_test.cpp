#include "effect_selection.hpp"

#include <cstdio>
#include <vector>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main()
{
    const std::vector<std::string> selected{
        "Framework", "ZenRT", "Framework", "Disabled", "ZenRT", "Unknown"};
    std::map<std::string, bool> states{
        {"Framework", true}, {"ZenRT", true}, {"Disabled", false}};

    expect(enabledEffectNames(selected, states)
               == std::vector<std::string>({"Framework", "ZenRT", "Framework", "ZenRT"}),
           "only enabled entries are built, repeated instances kept in their order");

    expect(enabledEffectNames(selected, {{"Framework", false}, {"ZenRT", false}, {"Disabled", false}}).empty(),
           "no enabled entry is a bypass-only chain, not a return of the whole selected list");

    states["Disabled"] = true;
    expect(enabledEffectNames(selected, states)
               == std::vector<std::string>({"Framework", "ZenRT", "Framework", "Disabled", "ZenRT"}),
           "an entry enabled again comes back in its selected place");
    expect(selected == std::vector<std::string>({"Framework", "ZenRT", "Framework", "Disabled", "ZenRT", "Unknown"}),
           "toggling changes only the construction list, never the selected one");

    const std::vector<std::string> allSelected{"cas", "cas.1", "cas", "Framework", "ZenRT"};
    const std::map<std::string, bool> allEnabled{{"cas", true}, {"cas.1", true}, {"Framework", true}, {"ZenRT", true}};
    expect(enabledEffectNames(allSelected, allEnabled) == allSelected, "with every entry enabled the selected list is built as it is");

    if (failures != 0)
        return 1;
    std::printf("OK: the construction list follows the selection and the enabled states\n");
    return 0;
}
