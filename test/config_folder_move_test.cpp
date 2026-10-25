#include "config_serializer.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

static std::string contents(const fs::path& path)
{
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), {});
}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const fs::path home = argv[1];
    fs::remove_all(home);
    const fs::path oldFolder = home / "vkBasalt-overlay";
    const fs::path newFolder = home / "vkdaedalus";
    fs::create_directories(oldFolder / "configs");
    fs::create_directories(newFolder);
    std::ofstream(oldFolder / "vkBasalt.conf") << "effects = cas\n";
    std::ofstream(oldFolder / "configs" / "Game.exe.conf") << "safeAntiCheat = false\n";
    std::ofstream(oldFolder / "settings.conf") << "overlayKey = F1\n";
    std::ofstream(newFolder / "settings.conf") << "overlayKey = End\n";
    fs::create_directories(home / "packs");
    fs::create_directory_symlink(home / "packs", oldFolder / "reshade");
    setenv("XDG_CONFIG_HOME", home.c_str(), 1);

    vkBasalt::ConfigSerializer::moveOldConfigFolderOnce();
    expect(contents(newFolder / "vkdaedalus.conf") == "effects = cas\n" && !fs::exists(oldFolder / "vkBasalt.conf"),
           "the old base config moves over under its new name");
    expect(contents(newFolder / "configs" / "Game.exe.conf") == "safeAntiCheat = false\n", "a game's profiles move over with their folder");
    expect(contents(newFolder / "settings.conf") == "overlayKey = End\n" && contents(oldFolder / "settings.conf") == "overlayKey = F1\n",
           "a file the new folder already has is kept, and the old one is left where it was");
    expect(fs::is_symlink(oldFolder / "reshade") && !fs::exists(fs::symlink_status(newFolder / "reshade")),
           "a link, such as one home-manager owns, is left in the old folder for whatever placed it");

    std::ofstream(oldFolder / "late.conf") << "x\n";
    vkBasalt::ConfigSerializer::moveOldConfigFolderOnce();
    expect(fs::exists(oldFolder / "late.conf") && !fs::exists(newFolder / "late.conf"), "the move runs once a process");

    if (failures != 0)
    {
        std::printf("%d config folder move check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: the old config folder moves over once, keeping what the new one has\n");
    return 0;
}
