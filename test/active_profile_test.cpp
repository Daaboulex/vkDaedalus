#include "config_serializer.hpp"
#include "program_scope.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace vkBasalt;
namespace fs = std::filesystem;

namespace
{
    std::string readFile(const fs::path& path)
    {
        std::ifstream      file(path);
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 1;

    const std::string native = ConfigSerializer::programNameFrom("/usr/bin/vkcube", {"vkcube", "--wsi", "xcb"});
    if (native != "vkcube")
    {
        std::printf("FAIL: a native program is named %s, want vkcube\n", native.c_str());
        return 1;
    }
    const std::string proton = ConfigSerializer::programNameFrom(
        "/home/u/.steam/proton/files/lib/wine/x86_64-unix/wine-preloader",
        {"Z:\\home\\u\\.steam\\steamapps\\common\\Witcher 3\\bin\\x64\\witcher3.exe", "-launch.exe"});
    if (proton != "witcher3.exe")
    {
        std::printf("FAIL: a Proton game is named %s, want witcher3.exe, so every Proton game would share one profile\n", proton.c_str());
        return 1;
    }
    const std::string unixPath = ConfigSerializer::programNameFrom("/usr/bin/wine", {"/usr/bin/wine", "Z:/home/u/Game.EXE"});
    if (unixPath != "Game.EXE")
    {
        std::printf("FAIL: a Wine game started by a Unix path is named %s, want Game.EXE\n", unixPath.c_str());
        return 1;
    }
    const std::string noExe = ConfigSerializer::programNameFrom("/usr/bin/wine64-preloader", {"wine64-preloader", "/usr/bin/wine64", "setup.msi", ".exe"});
    if (noExe != "wine64-preloader")
    {
        std::printf("FAIL: a Wine process running no .exe is named %s, want wine64-preloader\n", noExe.c_str());
        return 1;
    }

    const fs::path configHome = argv[1];
    fs::remove_all(configHome);
    const fs::path configs = configHome / "vkdaedalus" / "configs";
    fs::create_directories(configs);
    setenv("XDG_CONFIG_HOME", configHome.c_str(), 1);

    std::ofstream(configs / ".active_profiles") << "alacritty=none\n"
                                                   "wine-preloader=default\n"
                                                   "vkcube=None\n"
                                                   "mygame=perf\n"
                                                   " spaced = none \r\n"
                                                   "twice=first\n"
                                                   "twice=second\n"
                                                   "active-profile-test=none\n"
                                                   ".gamescope-wrapped=default\n";
    std::ofstream(configs / "alacritty@none.conf") << "effects = cas\n";
    std::ofstream(configs / "alacritty.conf") << "";
    std::ofstream(configs / "wine-preloader.conf") << "";

    const ActiveProfile bypassed = ConfigSerializer::getActiveProfile("alacritty");
    if (bypassed.kind != ProfileKind::Bypass || bypassed.name != "none")
        return 1;

    if (!programBypassed())
        return 1;

    const ActiveProfile defaulted = ConfigSerializer::getActiveProfile("wine-preloader");
    if (defaulted.kind != ProfileKind::Default || defaulted.name != "default")
        return 1;

    const ActiveProfile absent = ConfigSerializer::getActiveProfile("glxgears");
    if (absent.kind != ProfileKind::Default || absent.name != "default")
        return 1;

    if (ConfigSerializer::getActiveProfile("gamescope").kind != ProfileKind::Bypass)
    {
        std::printf("FAIL: gamescope runs the layer in its own process by default, though the game inside it runs it too, so effects would apply twice\n");
        return 1;
    }
    if (ConfigSerializer::getActiveProfile(".gamescope-wrapped").kind != ProfileKind::Default)
    {
        std::printf("FAIL: a profile named for gamescope in .active_profiles does not run the layer there\n");
        return 1;
    }

    const ActiveProfile capitalised = ConfigSerializer::getActiveProfile("vkcube");
    if (capitalised.kind != ProfileKind::Named || capitalised.name != "None")
        return 1;

    const ActiveProfile named = ConfigSerializer::getActiveProfile("mygame");
    if (named.kind != ProfileKind::Named || named.name != "perf")
        return 1;

    const ActiveProfile handEdited = ConfigSerializer::getActiveProfile("spaced");
    if (handEdited.kind != ProfileKind::Bypass)
    {
        std::printf("FAIL: a hand-edited line with spaces and a CR around none must still bypass\n");
        return 1;
    }

    if (ConfigSerializer::getActiveProfile("twice").name != "second")
    {
        std::printf("FAIL: a program listed twice must read as its last line, the one a rewrite keeps\n");
        return 1;
    }

    const std::vector<std::string> defaultAndBypass{"default", "none"};
    if (ConfigSerializer::listProfiles() != defaultAndBypass)
    {
        std::printf("FAIL: with no profiles folder the profiles are default and none\n");
        return 1;
    }

    if (!ConfigSerializer::isWineLoaderName("wine64-preloader") || !ConfigSerializer::isWineLoaderName("wine") ||
        ConfigSerializer::isWineLoaderName("witcher3.exe"))
    {
        std::printf("FAIL: the Wine loader names are told apart from a game's name\n");
        return 1;
    }
    Logger::setHistoryEnabled(true);
    Logger::err("an error the debug window must keep");
    for (int i = 0; i < 3000; i++)
        Logger::trace("a line logged every frame");
    Logger::setHistoryEnabled(false);
    const std::vector<LogEntry> kept = Logger::getHistory();
    if (std::none_of(kept.begin(), kept.end(), [](const LogEntry& e) { return e.message == "an error the debug window must keep"; }))
    {
        std::printf("FAIL: an error is pushed out of the debug window's history by lines logged every frame\n");
        return 1;
    }
    Logger::clearHistory();
    const fs::path profiles = configHome / "vkdaedalus" / "profiles";
    if (ConfigSerializer::createProfile("none").empty() || ConfigSerializer::deleteProfile("none").find("stays") == std::string::npos)
    {
        std::printf("FAIL: none can be neither made nor deleted\n");
        return 1;
    }
    if (!ConfigSerializer::createProfile("fresh", "none").empty() || readFile(profiles / "fresh.conf").find("cas") != std::string::npos)
        return 1;
    for (const std::string bad : {"a/b", "../up", " lead", "trail ", "..", ".", "a@b", "tab\there", "x=y"})
    {
        const std::string problem = ConfigSerializer::createProfile(bad, "fresh");
        if (problem.empty() || ConfigSerializer::profileNameProblem(bad).empty())
        {
            std::printf("FAIL: the profile name '%s' was taken, though it cannot be a file vkDaedalus reads back\n", bad.c_str());
            return 1;
        }
    }
    if (ConfigSerializer::createProfile("fresh", "fresh").find("already exists") == std::string::npos)
    {
        std::printf("FAIL: making a profile that exists must say it already exists\n");
        return 1;
    }
    if (!ConfigSerializer::createProfile("Night mode-2_b.1", "fresh").empty())
    {
        std::printf("FAIL: a profile name of letters, digits, a space, '-', '_' and '.' was refused\n");
        return 1;
    }
    std::ofstream(profiles / "risky.conf") << "effects = smaa\nsafeAntiCheat = false\n";
    const std::string copiedRisky = ConfigSerializer::createProfile("from risky", "risky");
    const std::string copiedText  = readFile(profiles / "from risky.conf");
    if (!copiedRisky.empty() || copiedText.find("effects = smaa") == std::string::npos || copiedText.find("safeAntiCheat") != std::string::npos)
    {
        std::printf("FAIL: a new profile copies its source's look and never a Safe Anti-Cheat line, got:\n%s\n", copiedText.c_str());
        return 1;
    }
    fs::permissions(profiles, fs::perms::owner_read | fs::perms::owner_exec);
    const std::string unwritable = ConfigSerializer::createProfile("unwritable", "fresh");
    fs::permissions(profiles, fs::perms::owner_all);
    if (unwritable.find("could not be written") == std::string::npos || fs::exists(profiles / "unwritable.conf"))
    {
        std::printf("FAIL: a profile that could not be written must say so, and leave no file: '%s'\n", unwritable.c_str());
        return 1;
    }

    if (!ConfigSerializer::setActiveProfile("alacritty", "fresh") || ConfigSerializer::getActiveProfile("alacritty").kind != ProfileKind::Named)
        return 1;
    if (ConfigSerializer::getActiveProfile("wine-preloader").kind != ProfileKind::Default)
        return 1;
    if (ConfigSerializer::getActiveProfile("twice").name != "second")
        return 1;
    std::ofstream(configs / "othergame.conf") << "safeAntiCheat = true\n";
    ConfigSerializer::setActiveProfile("othergame", "fresh");
    const std::map<std::string, std::vector<std::string>> users = ConfigSerializer::profileUsers();
    const auto usersOf = [&users](const std::string& profile) {
        const auto found = users.find(profile);
        return found == users.end() ? std::vector<std::string>{} : found->second;
    };
    if (usersOf("fresh") != std::vector<std::string>{"alacritty", "othergame"} || usersOf("default") != std::vector<std::string>{"wine-preloader"})
    {
        std::printf("FAIL: a profile lists the games that run it, a game with no entry under default\n");
        return 1;
    }
    if (ConfigSerializer::renameProfile("default", "x").empty() || ConfigSerializer::renameProfile("fresh", "risky").find("already exists") == std::string::npos
        || ConfigSerializer::renameProfile("fresh", "a/b").empty())
    {
        std::printf("FAIL: renaming default, onto a taken name or to a bad name is refused\n");
        return 1;
    }
    if (!ConfigSerializer::renameProfile("fresh", "evening").empty() || fs::exists(profiles / "fresh.conf") || !fs::exists(profiles / "evening.conf")
        || ConfigSerializer::getActiveProfile("alacritty").name != "evening" || ConfigSerializer::getActiveProfile("othergame").name != "evening")
    {
        std::printf("FAIL: a renamed profile moves its file and every game that ran it\n");
        return 1;
    }
    if (!ConfigSerializer::deleteProfile("evening").empty() || fs::exists(profiles / "evening.conf")
        || ConfigSerializer::getActiveProfile("alacritty").kind != ProfileKind::Default || ConfigSerializer::getActiveProfile("othergame").kind != ProfileKind::Default)
    {
        std::printf("FAIL: a deleted profile sends every game that ran it to default\n");
        return 1;
    }
    fs::remove(configs / "othergame.conf");

    const fs::path    userConfig  = configHome / "vkdaedalus" / "vkdaedalus.conf";
    const std::string userEffects = "effects = cas:smaa\ncasSharpness = 0.7\n";
    std::ofstream(userConfig) << userEffects;
    if (!ConfigSerializer::saveSettings(VkBasaltSettings{}))
        return 1;
    if (readFile(userConfig) != userEffects)
        return 1;
    if (readFile(configHome / "vkdaedalus" / "settings.conf").find("maxEffects = ") == std::string::npos)
        return 1;

    const fs::path         settingsFile = configHome / "vkdaedalus" / "settings.conf";
    const VkBasaltSettings defaults;
    for (const char* refused : {"maxEffects = 0", "maxEffects = -3", "maxEffects = 201", "maxEffects = 42abc", "autoApplyDelay = 5",
                                "autoApplyDelay = 1e3"})
    {
        std::ofstream(settingsFile) << refused << "\n";
        const VkBasaltSettings loaded = ConfigSerializer::loadSettings();
        if (loaded.maxEffects != defaults.maxEffects || loaded.autoApplyDelayMs != defaults.autoApplyDelayMs)
        {
            std::printf("FAIL: the settings.conf line \"%s\" must keep the default, as the settings view would refuse it\n", refused);
            return 1;
        }
    }
    for (const LaunchNotice notice : {LaunchNotice::FirstLaunch, LaunchNotice::Never, LaunchNotice::EveryLaunch})
    {
        VkBasaltSettings chosen;
        chosen.launchNotice = notice;
        if (!ConfigSerializer::saveSettings(chosen) || ConfigSerializer::loadSettings().launchNotice != notice)
        {
            std::printf("FAIL: launchNotice = %s did not come back from settings.conf\n", launchNoticeWord(notice));
            return 1;
        }
    }
    {
        const VkBasaltSettings      keys;
        const std::set<std::string> distinct = {keys.overlayKey, keys.toggleKey, keys.reloadKey, keys.previewKey, keys.frameGenerationKey};
        if (distinct.size() != 5)
        {
            std::printf("FAIL: two default key bindings are the same key, so one press would run both\n");
            return 1;
        }
        VkBasaltSettings chosen;
        chosen.frameGenerationKey = "F7";
        if (!ConfigSerializer::saveSettings(chosen) || ConfigSerializer::loadSettings().frameGenerationKey != "F7")
        {
            std::printf("FAIL: frameGenerationKey = F7 did not come back from settings.conf\n");
            return 1;
        }
    }
    std::ofstream(settingsFile) << "launchNotice = sometimes\n";
    if (ConfigSerializer::loadSettings().launchNotice != defaults.launchNotice)
    {
        std::printf("FAIL: launchNotice = sometimes must keep the default, every launch\n");
        return 1;
    }
    std::ofstream(settingsFile) << "maxEffects = 50 # room for a long chain\n"
                                   "autoApplyDelay = 400\n"
                                   "toggleKey = F9 # the other key\n";
    const VkBasaltSettings handWritten = ConfigSerializer::loadSettings();
    if (handWritten.maxEffects != 50 || handWritten.autoApplyDelayMs != 400 || handWritten.toggleKey != "F9")
    {
        std::printf("FAIL: hand-written settings with a comment after the value must be read\n");
        return 1;
    }

    const fs::path profile = configs / "keys.conf";
    std::ofstream(profile) << "lutFile = \"/luts/film grade.cube\"\n"
                              "vramSoftLimitMB = 512\n"
                              "casSharpness = 0.4\n"
                              "cas.casSharpness = 0.5\n"
                              "effects = cas\n";
    if (!ConfigSerializer::saveToPath(profile.string(), {"cas", "smaa"}, {}, {}, {}, {}))
        return 1;
    const std::string saved = readFile(profile);
    for (const char* kept : {"lutFile = \"/luts/film grade.cube\"\n", "vramSoftLimitMB = 512\n", "casSharpness = 0.4\n"})
        if (saved.find(kept) == std::string::npos)
            return 1;
    if (saved.find("cas.casSharpness") != std::string::npos || saved.find("effects = cas:smaa\n") == std::string::npos
        || saved.find("effects = cas\n") != std::string::npos)
        return 1;

    fs::remove(configHome / "vkdaedalus" / "settings.conf");
    std::ofstream(userConfig) << "enableOnLaunch = True\n"
                                 "depthCapture = True\n"
                                 "autoApply = False\n"
                                 "effectGpuTiming = maybe\n";
    const VkBasaltSettings fallback = ConfigSerializer::loadSettings();
    if (!fallback.enableOnLaunch || !fallback.depthCapture || fallback.autoApply || fallback.effectGpuTiming)
        return 1;

    const fs::path packs = configHome / "packs";
    fs::create_directories(packs / "Shaders" / "SomePack");
    fs::create_directories(packs / "Textures");
    std::ofstream(packs / "Shaders" / "SomePack" / "effect.fx") << "";
    std::vector<std::string> shaderPaths, texturePaths;
    ConfigSerializer::scanShaderTree(packs.string(), shaderPaths, texturePaths);
    const auto has = [](const std::vector<std::string>& paths, const fs::path& want) {
        return std::find(paths.begin(), paths.end(), want.string()) != paths.end();
    };
    if (!has(shaderPaths, packs / "Shaders") || !has(shaderPaths, packs / "Shaders" / "SomePack")
        || !has(texturePaths, packs / "Textures"))
    {
        std::printf("FAIL: a scan finds the Shaders folder, a pack folder below it and the Textures folder\n");
        return 1;
    }

    std::ofstream(configs / "unspoken.conf") << "# a game file that does not mention Safe Anti-Cheat\n";
    if (!ConfigSerializer::loadGameSettings("unspoken").safeAntiCheat)
    {
        std::printf("FAIL: a game whose file does not mention Safe Anti-Cheat must keep it on\n");
        return 1;
    }
    std::ofstream(configs / "trusted.conf") << "safeAntiCheat = false\n";
    if (ConfigSerializer::loadGameSettings("trusted").safeAntiCheat)
    {
        std::printf("FAIL: safeAntiCheat = false must turn Safe Anti-Cheat off for that game\n");
        return 1;
    }
    if (!ConfigSerializer::saveGameSettings("trusted", {true}) || !ConfigSerializer::loadGameSettings("trusted").safeAntiCheat
        || !ConfigSerializer::saveGameSettings("trusted", {false}) || ConfigSerializer::loadGameSettings("trusted").safeAntiCheat)
    {
        std::printf("FAIL: a game's Safe Anti-Cheat comes back as it was saved, on and off\n");
        return 1;
    }
    const fs::path stale = profiles / "stale.conf";
    std::ofstream(stale) << "safeAntiCheat = false\neffects = cas\n";
    if (!ConfigSerializer::saveToPath(stale.string(), {"cas"}, {}, {}, {}, {}) || readFile(stale).find("safeAntiCheat") != std::string::npos)
    {
        std::printf("FAIL: saving a profile drops a Safe Anti-Cheat line left in it, since a profile no longer holds one: %s\n", readFile(stale).c_str());
        return 1;
    }
    const std::string newGame = ConfigSerializer::ensureGame("brand-new-game");
    if (newGame.empty() || readFile(newGame).find("safeAntiCheat = true\n") == std::string::npos)
    {
        std::printf("FAIL: the file made for a new game must say safeAntiCheat = true\n");
        return 1;
    }
    std::ofstream(configs / "guarded.conf") << "safeAntiCheat = true  # keep depth away from this game\n";
    if (!ConfigSerializer::loadGameSettings("guarded").safeAntiCheat)
        return 1;

    std::ofstream(configs / ".active_profiles") << "wine64-preloader=none\n";
    if (ConfigSerializer::getActiveProfile("bypassed.exe").kind != ProfileKind::Bypass)
    {
        std::printf("FAIL: a Wine game with no entry of its own must stay bypassed when the shared Wine name was\n");
        return 1;
    }
    if (ConfigSerializer::getActiveProfile("vkcube").kind == ProfileKind::Bypass)
    {
        std::printf("FAIL: bypassing the shared Wine name must not bypass a native program\n");
        return 1;
    }
    std::ofstream(configs / ".active_profiles", std::ios::app) << "witcher3.exe=default\n";
    if (ConfigSerializer::getActiveProfile("witcher3.exe").kind != ProfileKind::Default)
    {
        std::printf("FAIL: a Wine game's own .active_profiles entry must win over the shared name's bypass\n");
        return 1;
    }

    std::ofstream(configs / ".active_profiles") << "wine-preloader=perf\n";
    std::ofstream(profiles / "perf.conf") << "effects = smaa\n";
    if (ConfigSerializer::ensureGame("other.exe").empty() || ConfigSerializer::getActiveProfile("other.exe").name != "perf"
        || !ConfigSerializer::loadGameSettings("other.exe").safeAntiCheat)
    {
        std::printf("FAIL: a new Wine game runs the profile the shared Wine name runs, with Safe Anti-Cheat on\n");
        return 1;
    }
    if (ConfigSerializer::ensureGame("nativegame").empty() || ConfigSerializer::getActiveProfile("nativegame").kind != ProfileKind::Default)
    {
        std::printf("FAIL: a native program's first launch must not take the shared Wine profile\n");
        return 1;
    }

    const std::string launched = ConfigSerializer::programNameFromCommand(
        {"/home/u/.steam/steamapps/common/Proton 9.0/proton", "waitforexitandrun", "/home/u/.steam/steamapps/common/Witcher 3/bin/x64/witcher3.exe", "-launch.exe"});
    if (launched != proton)
    {
        std::printf("FAIL: vkdaedalus-run names the Proton command %s, the layer names its process %s\n", launched.c_str(), proton.c_str());
        return 1;
    }
    if (ConfigSerializer::programNameFromCommand({"/usr/bin/vkcube", "--wsi", "xcb"}) != native || ConfigSerializer::programNameFromCommand({}) != UNKNOWN_PROGRAM_NAME)
    {
        std::printf("FAIL: vkdaedalus-run must name a native command by its program, and an empty one unknown\n");
        return 1;
    }

    std::ofstream(configs / "wrapped.x86_64.conf") << "safeAntiCheat = true\n";
    const std::string steamRuntime = ConfigSerializer::programNameFromCommand(
        {"/home/u/.steam/steamapps/common/SteamLinuxRuntime_sniper/_v2-entry-point", "--verb=waitforexitandrun", "--", "/games/Native/native.x86_64", "-arg"});
    const std::string wrapped = ConfigSerializer::programNameFromCommand({"gamemoderun", "/games/wrapped.x86_64", "--fullscreen"});
    if (steamRuntime != "native.x86_64" || wrapped != "wrapped.x86_64")
    {
        std::printf("FAIL: vkdaedalus-run names Steam's runtime command %s, want native.x86_64, and a wrapped game %s, want wrapped.x86_64, which has a profile\n",
                    steamRuntime.c_str(), wrapped.c_str());
        return 1;
    }

    std::ofstream(profiles / "after.conf") << "effectsAfterFrameGeneration = true\n";
    std::ofstream(configs / ".active_profiles", std::ios::app) << "orderafter.exe=after\norderfallback=gone\norderbypassed=none\n";
    if (!ConfigSerializer::effectsAfterFrameGenerationFor("orderafter.exe") || ConfigSerializer::effectsAfterFrameGenerationFor("orderunset"))
    {
        std::printf("FAIL: a profile saying effectsAfterFrameGeneration = true must give after, and default with no file before\n");
        return 1;
    }
    std::ofstream(profiles / "default.conf") << "effectsAfterFrameGeneration = true\n";
    if (!ConfigSerializer::effectsAfterFrameGenerationFor("orderfallback"))
    {
        std::printf("FAIL: a named profile whose file is missing must fall back to the default profile, as the layer does\n");
        return 1;
    }
    if (ConfigSerializer::effectsAfterFrameGenerationFor("orderbypassed"))
    {
        std::printf("FAIL: a bypassed program must keep the default order\n");
        return 1;
    }
    ProfileSettings after;
    after.effectsAfterFrameGeneration = true;
    const std::string orderFile = (configs / "ordersaved.conf").string();
    if (!ConfigSerializer::saveToPath(orderFile, {}, {}, {}, {}, {}, after, std::nullopt)
        || !ConfigSerializer::saveToPath(orderFile, {}, {}, {}, {}, {}, ProfileSettings{}, std::nullopt)
        || ConfigSerializer::loadProfileSettings(orderFile).effectsAfterFrameGeneration != std::optional<bool>(true))
    {
        std::printf("FAIL: a save that does not set the order must keep the saved one: %s\n", readFile(orderFile).c_str());
        return 1;
    }

    return 0;
}
