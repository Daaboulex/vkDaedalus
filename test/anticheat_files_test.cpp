#include "anticheat_files.hpp"
#include "config_serializer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static void touch(const fs::path& file)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

static std::string found(const fs::path& folder, size_t limit = 100000)
{
    size_t     scanned = 0;
    const auto hit     = findShippedAntiCheat(folder.string(), shippedAntiCheatRules().rules, limit, scanned);
    return hit ? hit->name + " " + hit->file : "";
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: anticheat-files-test <scratch folder>\n");
        return 2;
    }
    const fs::path scratch = argv[1];
    fs::remove_all(scratch);

    const AntiCheatRules& rules = shippedAntiCheatRules();
    expect(rules.rules.size() >= 10, "the embedded rules hold " + std::to_string(rules.rules.size()) + " anti-cheat patterns, want at least 10");
    expect(rules.refused.empty(), "an embedded anti-cheat rule did not parse: " + (rules.refused.empty() ? std::string() : rules.refused.front()));

    touch(scratch / "eac" / "EasyAntiCheat" / "EasyAntiCheat_x64.dll");
    expect(found(scratch / "eac") == "EasyAntiCheat EasyAntiCheat/EasyAntiCheat_x64.dll",
           "Easy Anti-Cheat in a game folder is found, got: " + found(scratch / "eac"));
    touch(scratch / "eaclower" / "easyanticheat" / "EASYANTICHEAT_X64.DLL");
    expect(found(scratch / "eaclower").rfind("EasyAntiCheat ", 0) == 0, "Easy Anti-Cheat is found whatever the case of its file name");
    touch(scratch / "be" / "BEService_x64.exe");
    expect(found(scratch / "be") == "BattlEye BEService_x64.exe", "BattlEye's service is found, got: " + found(scratch / "be"));
    touch(scratch / "clean" / "bin" / "game.exe");
    touch(scratch / "clean" / "data" / "level.pak");
    expect(found(scratch / "clean").empty(), "a game folder with no anti-cheat file is reported clean, got: " + found(scratch / "clean"));
    touch(scratch / "deep" / "a.txt");
    touch(scratch / "deep" / "b.txt");
    touch(scratch / "deep" / "z" / "EasyAntiCheat_x64.dll");
    size_t scanned = 0;
    expect(!findShippedAntiCheat((scratch / "deep").string(), rules.rules, 1, scanned) && scanned > 1,
           "a scan stops at its entry limit and says so");

    expect(gameFolderFor("/lib/steamapps/common/Game/bin/x64/game.exe") == "/lib/steamapps/common/Game",
           "a Steam game's folder is the one under steamapps/common, got " + gameFolderFor("/lib/steamapps/common/Game/bin/x64/game.exe"));
    expect(gameFolderFor("/g/Root/Project/Binaries/Win64/Project-Win64-Shipping.exe") == "/g/Root",
           "an Unreal game's folder is the one above the project, got " + gameFolderFor("/g/Root/Project/Binaries/Win64/Project-Win64-Shipping.exe"));
    expect(gameFolderFor("/home/u/Games/G/Binaries/g.exe") == "/home/u/Games/G",
           "an exe directly in Binaries climbs only out of Binaries, got " + gameFolderFor("/home/u/Games/G/Binaries/g.exe"));
    expect(gameFolderFor("/home/u/Games/G/game.exe") == "/home/u/Games/G",
           "an exe at a game's top keeps its own folder and never scans sibling games, got " + gameFolderFor("/home/u/Games/G/game.exe"));
    expect(gameFolderFor("/home/u/Games/G/bin/x64/game.exe") == "/home/u/Games/G",
           "binary folders are climbed out of, got " + gameFolderFor("/home/u/Games/G/bin/x64/game.exe"));

    const fs::path prefix = scratch / "pfx";
    touch(prefix / "drive_c" / "Games" / "g.exe");
    fs::create_directories(prefix / "dosdevices");
    fs::create_directory_symlink("../drive_c", prefix / "dosdevices" / "c:");
    fs::create_directory_symlink("/", prefix / "dosdevices" / "z:");
    const std::string onC = unixPathOfWindowsPath("C:\\Games\\g.exe", prefix.string());
    expect(onC == fs::canonical(prefix / "drive_c" / "Games" / "g.exe").string(), "a C: path resolves through the prefix, got " + onC);
    std::string zPath = "Z:" + fs::canonical(prefix / "drive_c" / "Games" / "g.exe").string();
    for (char& c : zPath)
        if (c == '/')
            c = '\\';
    expect(unixPathOfWindowsPath(zPath, prefix.string()) == onC, "a Z: path resolves to the same file");
    expect(unixPathOfWindowsPath("g.exe", prefix.string()).empty(), "a path with no drive does not resolve");
    expect(unixPathOfWindowsPath("C:\\Games\\missing.exe", prefix.string()).empty(), "a file missing under the prefix does not resolve");

    expect(ConfigSerializer::wineExecutableFrom("/x/wine64-preloader", {"Z:\\g\\Game.exe", "-x"}) == "Z:\\g\\Game.exe",
           "a Wine process's game is the first .exe argument");
    expect(ConfigSerializer::wineExecutableFrom("/usr/bin/vkcube", {"vkcube"}).empty(), "a native program has no Wine executable");

    if (failures != 0)
        return 1;
    std::printf("OK: shipped anti-cheat files are found from the embedded rules in every game folder shape\n");
    return 0;
}
