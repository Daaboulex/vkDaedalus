#include "profile_undo.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace vkBasalt;

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
    expect(profileUndoWords("effects = smaa:cas\n", "effects = cas:smaa\n") == "Undo: effects back to cas:smaa",
           "a changed value is named with the value it goes back to");
    expect(profileUndoWords("effects = cas\ncas.casSharpness = 0.8\n", "effects = cas\n") == "Undo: remove cas.casSharpness",
           "a key the change added is named as removed");
    expect(profileUndoWords("effects = cas\n", "effects = cas\ndisabledEffects = cas\n") == "Undo: disabledEffects back to cas",
           "a key the change removed is named with the value it comes back with");
    expect(profileUndoWords("a = 2\nb = 2\nc = 2\nd = 2\n", "a = 1\nb = 1\nc = 1\nd = 1\n") == "Undo: a back to 1, b back to 1, and 2 more",
           "past two changes the rest is counted");
    expect(profileUndoWords("# comment\neffects = cas\n", "effects = cas\n") == "Undo the last saved change",
           "a change outside the settings, such as a comment, is not named");
    expect(profileUndoWords("x =   5  \n", "x = 5\n") == "Undo the last saved change", "spaces around a value change nothing");

    std::vector<std::string> undo;
    rememberProfileState(undo, "effects = cas\n", "effects = cas\n");
    expect(undo.empty(), "a save that wrote the same text keeps no step");
    rememberProfileState(undo, "effects = cas\n", "effects = smaa\n");
    rememberProfileState(undo, "effects = smaa\n", "effects = smaa:cas\n");
    expect(undo.size() == 2 && undo.back() == "effects = smaa\n" && undo.front() == "effects = cas\n",
           "each save keeps the text before it, the newest last, so Undo takes back the latest change first");
    for (size_t i = 0; i < profileUndoDepth + 5; i++)
        rememberProfileState(undo, "step " + std::to_string(i) + "\n", "step " + std::to_string(i + 1) + "\n");
    expect(undo.size() == profileUndoDepth && undo.back() == "step " + std::to_string(profileUndoDepth + 4) + "\n",
           "the oldest steps are dropped once the depth is reached, the newest kept");

    if (failures == 0)
        std::printf("profile undo: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
