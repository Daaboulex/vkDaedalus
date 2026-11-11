#include "anticheat_files.hpp"
#include "config_serializer.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>

#include <sys/wait.h>

namespace fs = std::filesystem;
using namespace vkBasalt;

static int                             failures = 0;
static std::shared_future<std::string> checkLeftRunning;

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

static bool never()
{
    return false;
}

static std::string found(const fs::path& folder, size_t limit = 100000)
{
    const AntiCheatScan scan = findShippedAntiCheat(folder.string(), shippedAntiCheatRules().rules, limit, never);
    return scan.found ? scan.found->name + " " + scan.found->file : "";
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--exit-while-checking")
    {
        checkLeftRunning   = checkFolderForAntiCheat(argv[2]);
        const bool running = checkLeftRunning.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout;
        std::printf("%s\n", running ? "checking at exit" : "finished before exit");
        std::fflush(stdout);
        return 0;
    }
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
    const AntiCheatScan limited = findShippedAntiCheat((scratch / "deep").string(), rules.rules, 1, never);
    expect(!limited.found && !limited.stopped && limited.scanned > 1, "a scan stops at its entry limit and says so");

    expect(safeAntiCheatLockFor((scratch / "eac").string(), never) == "EasyAntiCheat ships with this game (EasyAntiCheat/EasyAntiCheat_x64.dll)",
           "a shipped anti-cheat locks Safe Anti-Cheat on, naming its file, got: " + safeAntiCheatLockFor((scratch / "eac").string(), never));
    expect(safeAntiCheatLockFor((scratch / "clean").string(), never).empty(), "a clean game folder leaves Safe Anti-Cheat unlocked");
    const std::string stopped = safeAntiCheatLockFor((scratch / "clean").string(), [] { return true; });
    expect(!stopped.empty(), "a check stopped before it finished locks Safe Anti-Cheat on rather than calling the folder clean");

    expect(checkFolderForAntiCheat((scratch / "eac").string()).get() == safeAntiCheatLockFor((scratch / "eac").string(), never),
           "the check on its own thread gives the lock the check in place gives");
    for (int folder = 0; folder < 20; folder++)
        for (int file = 0; file < 1000; file++)
            touch(scratch / "large" / ("content_folder_with_a_long_name_" + std::to_string(folder)) / ("texture_asset_with_a_long_name_" + std::to_string(file) + ".dds"));
    const std::string exitCommand = fs::read_symlink("/proc/self/exe").string() + " --exit-while-checking '" + (scratch / "large").string() + "' 2>&1";
    FILE*             child       = popen(exitCommand.c_str(), "r");
    expect(child != nullptr, "the program that exits while the check runs could not start");
    if (child)
    {
        std::string said;
        char        line[256];
        while (std::fgets(line, sizeof(line), child))
            said += line;
        const int status = pclose(child);
        expect(said.find("checking at exit") != std::string::npos, "the check had finished before the program exited, so nothing was proven: " + said);
        expect(WIFEXITED(status) && WEXITSTATUS(status) == 0,
               "a program exiting while the anti-cheat check runs must exit cleanly, got status " + std::to_string(status) + ": " + said);
    }

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
    expect(unixPathOfWindowsPath(R"(\\?\C:\Games\g.exe)", prefix.string()) == onC,
           "a path with Windows' \\\\?\\ namespace prefix, as Proton passes its own helpers, resolves like the plain one");
    expect(unixPathOfWindowsPath(R"(\??\C:\Games\g.exe)", prefix.string()) == onC, "a path with the \\??\\ namespace prefix resolves like the plain one");
    expect(unixPathOfWindowsPath(R"(\\?\UNC\server\share\g.exe)", prefix.string()).empty(), "a network path behind the namespace prefix does not resolve");
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
