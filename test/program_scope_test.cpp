#include "program_scope.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: program-scope-test <config folder>\n");
        return 2;
    }
    const std::string peak = "/games/steamapps/common/PEAK";
    const std::string halo = "/games/steamapps/common/Halo The Master Chief Collection";
    const std::string system32 = "/games/steamapps/compatdata/3527290/pfx/drive_c/windows/system32";

    expect(partOfTheGame("PEAK.exe", peak, "PEAK.exe", peak), "the program vkdaedalus-run named is the game");
    expect(partOfTheGame("peak.EXE", "", "PEAK.exe", ""), "a Windows program's name matches in any case");
    expect(partOfTheGame("MCC-Win64-Shipping.exe", halo, "mcclauncher.exe", halo), "a game the launcher starts from its own folder is the game");
    expect(!partOfTheGame("explorer.exe", system32, "PEAK.exe", peak), "Wine's explorer.exe is not the game");
    expect(!partOfTheGame("xalia.exe", system32, "PEAK.exe", peak), "Wine's xalia.exe is not the game");
    expect(!partOfTheGame("python3.13", "", "PEAK.exe", peak), "Proton's python script is not the game");
    expect(!partOfTheGame("wine64-preloader", "", "PEAK.exe", peak), "a Wine loader running no program is not the game");
    expect(!partOfTheGame("Python3", "", "python3", ""), "a native program's name matches only exactly");
    expect(!partOfTheGame("other.exe", "", "PEAK.exe", ""), "two programs with no game folder are not one game");
    expect(partOfTheGame("vkcube", "", "", ""), "without vkdaedalus-run every program keeps the layer");

    setenv("XDG_CONFIG_HOME", argv[1], 1);
    std::filesystem::create_directories(argv[1]);
    setenv("VKDAEDALUS_RUN_PROGRAM", "another-game.exe", 1);
    unsetenv("VKDAEDALUS_RUN_EXECUTABLE");
    expect(programBypassed(), "a program vkdaedalus-run did not name stays out of the layer");

    if (failures != 0)
        return 1;
    std::printf("program-scope: every check passed\n");
    return 0;
}
