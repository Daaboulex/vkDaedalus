#include "logger.hpp"
#include "settings_manager.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vkBasalt;
namespace fs = std::filesystem;

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
    {
        const std::vector<KeyUse> others = {{"Toggle effects", "F10"}, {"Toggle overlay", "End"}};
        expect(keyTakenBy("end", others) == "Toggle overlay", "a key another action uses is refused, whatever its case");
        expect(keyTakenBy("F11", others).empty(), "a key no other action uses is free");
    }
    if (argc < 2)
        return 2;

    const fs::path configHome = argv[1];
    fs::remove_all(configHome);
    fs::create_directories(configHome / "vkdaedalus");
    setenv("XDG_CONFIG_HOME", configHome.c_str(), 1);
    const fs::path settingsFile = configHome / "vkdaedalus" / "settings.conf";
    std::ofstream(settingsFile) << "depthCapture = on\nlogLevel = warn\n";

    unsetenv("VKDAEDALUS_LOG_LEVEL");
    SettingsManager settings;
    settings.initialize();
    expect(Logger::logLevel() == LogLevel::Warn, "settings.conf's logLevel = warn sets the layer's log level when no variable does");
    expect(settings.getLogLevel() == "warn", "logLevel is read from settings.conf");
    expect(!settings.getDepthCapture(), "before any game's profile says otherwise, Safe Anti-Cheat keeps depth capture off");

    settings.setSafeAntiCheat(false);
    expect(settings.getDepthCapture(), "depth capture is on as settings.conf says, for a game that turned Safe Anti-Cheat off");

    settings.setSafeAntiCheat(true);
    expect(!settings.getDepthCapture(), "Safe Anti-Cheat turns depth capture off while it is on");

    expect(settings.save(), "the settings save");
    std::ostringstream saved;
    saved << std::ifstream(settingsFile).rdbuf();
    expect(saved.str().find("depthCapture = on\n") != std::string::npos,
           "saving under Safe Anti-Cheat keeps the user's depth capture setting for other games");

    settings.setSafeAntiCheat(false);
    expect(settings.getDepthCapture(), "depth capture comes back when Safe Anti-Cheat is off");

    {
        const std::string before = [&] { std::ifstream f(settingsFile); std::stringstream t; t << f.rdbuf(); return t.str(); }();
        fs::permissions(settingsFile.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
        const bool savedReadOnly = settings.save();
        fs::permissions(settingsFile.parent_path(), fs::perms::owner_all);
        const std::string after = [&] { std::ifstream f(settingsFile); std::stringstream t; t << f.rdbuf(); return t.str(); }();
        expect(!savedReadOnly && after == before,
               "a save that cannot write its new file says it failed and leaves settings.conf as it was, never empty or half written");
    }

    expect(depthCaptureStatus(true, false, true) == "On in this run.", "depth capture chosen and running says it is on");
    expect(depthCaptureStatus(true, false, false).find("restart") != std::string::npos,
           "depth capture chosen but not running in this run says a restart applies it");
    expect(depthCaptureStatus(false, false, true).find("restart") != std::string::npos, "depth capture unticked while it runs says a restart applies it");
    expect(depthCaptureStatus(true, true, true) == "Off while Safe Anti-Cheat is on.", "Safe Anti-Cheat on says depth capture is off, whatever runs");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: Safe Anti-Cheat stops depth capture without erasing the setting\n");
    return 0;
}
