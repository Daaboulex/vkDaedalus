#include "config_serializer.hpp"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace vkBasalt;
namespace fs = std::filesystem;

namespace
{
    int failures = 0;

    void expect(bool ok, const std::string& what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what.c_str());
            failures++;
        }
    }

    std::string contents(const fs::path& path)
    {
        std::ifstream      file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::map<std::string, std::string> tree(const fs::path& root)
    {
        std::map<std::string, std::string> files;
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if (entry.is_regular_file())
                files[fs::relative(entry.path(), root).string()] = contents(entry.path());
        return files;
    }

    void write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    void todaysLayout(const fs::path& configs)
    {
        write(configs / "vkcube.conf", "effects = cas\nsafeAntiCheat = false\n");
        write(configs / "vkcube@night.conf", "effects = smaa\nsafeAntiCheat = true\n");
        write(configs / "PEAK.exe.conf", "effects = lut\n");
        write(configs / "PEAK.exe@night.conf", "effects = deband\n");
        write(configs / "other@solo.conf", "effects = fxaa\n");
        write(configs / "alacritty@none.conf", "effects = cas\n");
        write(configs / ".active_profiles", "vkcube=night\nPEAK.exe=default\nother=solo\ngamescope=none\nstray=gone\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 1;
    const fs::path home = argv[1];
    fs::remove_all(home);
    const fs::path base     = home / "vkdaedalus";
    const fs::path configs  = base / "configs";
    const fs::path profiles = base / "profiles";
    setenv("XDG_CONFIG_HOME", home.c_str(), 1);

    todaysLayout(configs);
    const std::map<std::string, std::string> before = tree(configs);
    const std::string                        moved  = ConfigSerializer::migrateToSharedProfiles();
    expect(moved.empty(), "the move to shared profiles reports no problem, got: " + moved);

    expect(tree(base / "configs.before-shared-profiles") == before, "configs.before-shared-profiles holds every file of configs/ as it was");

    const std::map<std::string, std::string> wantProfiles = {
        {"vkcube.conf", "effects = cas\n"},
        {"PEAK.exe.conf", "effects = lut\n"},
        {"vkcube night.conf", "effects = smaa\n"},
        {"PEAK.exe night.conf", "effects = deband\n"},
        {"solo.conf", "effects = fxaa\n"},
    };
    expect(tree(profiles) == wantProfiles, "each old profile file becomes one profile without its safeAntiCheat line, a name two games used taking the game's name in front");

    expect(ConfigSerializer::getActiveProfile("vkcube").name == "vkcube night", "a game that ran a named profile runs it under its new name");
    expect(ConfigSerializer::getActiveProfile("PEAK.exe").name == "PEAK.exe", "a game that ran its own default file runs the profile made from it");
    expect(ConfigSerializer::getActiveProfile("other").name == "solo", "a game with only a named profile keeps running it");
    expect(ConfigSerializer::getActiveProfile("gamescope").kind == ProfileKind::Bypass, "a game set to none stays none");
    expect(ConfigSerializer::getActiveProfile("stray").name == "gone", "an entry for a game with no file is left as it was");

    expect(ConfigSerializer::loadGameSettings("vkcube").safeAntiCheat, "vkcube keeps Safe Anti-Cheat on: the profile it ran said true, its old default file said false");
    expect(ConfigSerializer::loadGameSettings("PEAK.exe").safeAntiCheat, "a profile that never named Safe Anti-Cheat leaves it on");
    expect(ConfigSerializer::loadGameSettings("other").safeAntiCheat && fs::exists(configs / "other.conf"), "a game known only by a named profile gets its own settings file");
    expect(contents(configs / "vkcube.conf").find("effects") == std::string::npos, "a game's file holds only the game's own settings once its profile moved out");

    for (const char* gone : {"vkcube@night.conf", "PEAK.exe@night.conf", "other@solo.conf"})
        expect(!fs::exists(configs / gone), std::string(gone) + " is removed once its profile is in profiles/");
    expect(fs::exists(configs / "alacritty@none.conf"), "a file named for none is not a profile and stays where it was");

    const std::map<std::string, std::string> once = tree(base);
    expect(ConfigSerializer::migrateToSharedProfiles().empty() && tree(base) == once, "a second move changes nothing once profiles/ exists");

    for (const char* fresh : {"empty", "no-configs"})
    {
        const fs::path freshHome = home / fresh;
        fs::create_directories(freshHome);
        if (std::string(fresh) == "no-configs")
            write(freshHome / "vkdaedalus" / "vkdaedalus.conf", "effects = cas\n");
        setenv("XDG_CONFIG_HOME", freshHome.c_str(), 1);
        const std::string first = ConfigSerializer::migrateToSharedProfiles();
        expect(first.empty() && fs::is_directory(freshHome / "vkdaedalus" / "profiles"),
               std::string("a home with ") + (std::string(fresh) == "empty" ? "nothing" : "no configs/") + " has nothing to move: profiles/ is made and no problem reported, got: " + first);
    }

    const fs::path heldHome = home / "held";
    const fs::path heldBase = heldHome / "vkdaedalus";
    setenv("XDG_CONFIG_HOME", heldHome.c_str(), 1);
    todaysLayout(heldBase / "configs");
    const int held = open(heldBase.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    expect(held >= 0 && flock(held, LOCK_EX) == 0, "the test holds the folder's lock");
    std::fflush(nullptr);
    const pid_t mover = fork();
    if (mover == 0)
    {
        close(held);
        _exit(ConfigSerializer::migrateToSharedProfilesOnce().empty() ? 0 : 1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    expect(!fs::exists(heldBase / "profiles"), "a second program moves nothing while another holds the folder's lock");
    close(held);
    int moverStatus = 0;
    waitpid(mover, &moverStatus, 0);
    expect(WIFEXITED(moverStatus) && WEXITSTATUS(moverStatus) == 0 && fs::exists(heldBase / "profiles" / "solo.conf"),
           "once the lock is let go, the waiting program moves the profiles");

    const fs::path refusedHome = home / "refused";
    const fs::path refusedBase = refusedHome / "vkdaedalus";
    setenv("XDG_CONFIG_HOME", refusedHome.c_str(), 1);
    todaysLayout(refusedBase / "configs");
    const std::map<std::string, std::string> untouched = tree(refusedBase / "configs");
    fs::permissions(refusedBase, fs::perms::owner_read | fs::perms::owner_exec);
    const std::string refused = ConfigSerializer::migrateToSharedProfiles();
    fs::permissions(refusedBase, fs::perms::owner_all);
    expect(refused.find("backup") != std::string::npos, "a backup that cannot be written stops the move and says so, got: " + refused);
    expect(tree(refusedBase / "configs") == untouched && !fs::exists(refusedBase / "profiles"), "a move that cannot back up leaves configs/ as it was and makes no profiles/");

    return failures == 0 ? 0 : 1;
}
