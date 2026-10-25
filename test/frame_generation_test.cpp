#include "frame_generation.hpp"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <algorithm>

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

static std::string contents(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), {});
}

static std::string replacedOnce(std::string text, const std::string& from, const std::string& to)
{
    const size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos)
        return "";
    return text.replace(at, from.size(), to);
}

static const std::string lsfgDefault = "version = 2\n"
                                       "\n"
                                       "[global]\n"
                                       "allow_fp16 = true\n"
                                       "log_level = \"info\"\n"
                                       "\n"
                                       "[[profile]]\n"
                                       "active_in = [ \"vkcube\", \"vkcubepp\" ]\n"
                                       "color_space = \"srgb\"\n"
                                       "flow_scale = 0.85\n"
                                       "multiplier = 4\n"
                                       "name = \"4x FG / 85% [Performance]\"\n"
                                       "override_present_mode = true\n"
                                       "pacing_mode = \"vsync\"\n"
                                       "performance_mode = true\n"
                                       "preserve_swapchain_image_count = false\n"
                                       "transfer_function = \"srgb\"\n"
                                       "\n"
                                       "[[profile]]\n"
                                       "active_in = \"GenshinImpact.exe\"\n"
                                       "color_space = \"srgb\"\n"
                                       "flow_scale = 1.0\n"
                                       "multiplier = 2\n"
                                       "name = \"2x FG / 100%\"\n"
                                       "override_present_mode = true\n"
                                       "pacing_mode = \"vsync\"\n"
                                       "performance_mode = false\n"
                                       "preserve_swapchain_image_count = false\n"
                                       "transfer_function = \"srgb\"\n";

static const std::string handWritten = "# my lsfg-vk settings\n"
                                       "version = 2\n"
                                       "\n"
                                       "[ global ]\n"
                                       "allow_fp16 = true   # fp16 is fine here\n"
                                       "\n"
                                       "# a profile for Peak\n"
                                       "[[ profile ]]\n"
                                       "name = 'Peak, tuned'\n"
                                       "active_in = [\n"
                                       "    \"PEAK.exe\", # the game\n"
                                       "    \"3527290\",\n"
                                       "]\n"
                                       "multiplier = 2 # two times\n"
                                       "flow_scale = 0.75\n"
                                       "pacing = \"VSYNC\"\n"
                                       "\n"
                                       "# and one for the cube\n"
                                       "[[profile]]\n"
                                       "name = \"cube\"\n"
                                       "active_in = 'vkcube'\n"
                                       "multiplier = 3\n";

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    const fs::path dir = argv[1];
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string problem;

    {
        const auto file = parseLsfgFile(lsfgDefault, problem);
        expect(file && file->profiles.size() == 2, "the file lsfg-vk writes for itself on its first run is read");
        expect(file && file->profiles[0].name == "4x FG / 85% [Performance]" && file->profiles[0].activeIn == std::vector<std::string>{"vkcube", "vkcubepp"} &&
                   file->profiles[0].multiplier == 4 && file->profiles[0].flowScale == 0.85f && file->profiles[0].performanceMode &&
                   file->profiles[0].colorSpace == "srgb" && file->profiles[0].overridePresentMode && !file->profiles[0].preserveSwapchainImageCount,
               "with every option of its first profile");
        const auto hand = parseLsfgFile(handWritten, problem);
        expect(hand && hand->profiles.size() == 2 && hand->profiles[0].name == "Peak, tuned" &&
                   hand->profiles[0].activeIn == std::vector<std::string>{"PEAK.exe", "3527290"} && hand->profiles[0].flowScale == 0.75f &&
                   !hand->profiles[0].colorSpace && hand->profiles[1].multiplier == 3,
               "a file written by hand, with comments, spaced headers, literal strings and a list over lines, is read as lsfg-vk reads it");
    }

    {
        FrameGenerationProfile peak = *profileNamed(*parseLsfgFile(handWritten, problem), "Peak, tuned");
        peak.multiplier             = 3;
        const auto edited           = lsfgTextWith(handWritten, std::nullopt, peak, problem);
        expect(edited && *edited == replacedOnce(handWritten, "multiplier = 2 # two times", "multiplier = 3 # two times"),
               "changing a profile's multiplier changes only that value: every other byte of lsfg-vk's file stays, its comments too");
        peak.colorSpace          = "hdr10";
        peak.transferFunction    = "pq";
        const auto withColour    = lsfgTextWith(handWritten, std::nullopt, peak, problem);
        expect(withColour && *withColour == replacedOnce(replacedOnce(handWritten, "multiplier = 2 # two times", "multiplier = 3 # two times"),
                                                          "pacing = \"VSYNC\"\n", "pacing = \"VSYNC\"\ncolor_space = \"hdr10\"\ntransfer_function = \"pq\"\n"),
               "an option the profile does not name yet is added inside that profile, after its last line, and nothing else moves");
        const auto back = withColour ? parseLsfgFile(*withColour, problem) : std::nullopt;
        expect(back && back->profiles[0].colorSpace == "hdr10" && back->profiles[1].name == "cube" && back->profiles[1].multiplier == 3,
               "and lsfg-vk reads it as that profile's own, the next profile untouched");
    }

    {
        const FrameGenerationProfile mine = newLsfgProfileFor("default", {"Game.exe"}, lowestOnMultiplier, lsfgColourOf(VK_COLOR_SPACE_HDR10_ST2084_EXT));
        const auto                   added = lsfgTextWith(lsfgDefault, std::nullopt, mine, problem);
        expect(added && added->rfind(lsfgDefault, 0) == 0, "a profile for a new game is added after lsfg-vk's own, which stay byte for byte");
        const auto back = added ? parseLsfgFile(*added, problem) : std::nullopt;
        expect(back && back->profiles.size() == 3 && back->profiles[2] == mine, "and reads back with every option it was given");
        expect(mine.colorSpace == "hdr10" && mine.transferFunction == "pq", "a game's HDR10 swapchain gives its new profile HDR10 with PQ");
        expect(!lsfgColourOf(VK_COLOR_SPACE_PASS_THROUGH_EXT).colorSpace, "a colour space lsfg-vk has no name for leaves lsfg-vk's own default");
        const auto fromNothing = lsfgTextWith("", std::nullopt, mine, problem);
        const auto nothingBack = fromNothing ? parseLsfgFile(*fromNothing, problem) : std::nullopt;
        expect(nothingBack && nothingBack->profiles.size() == 1 && nothingBack->profiles[0] == mine,
               "with no file yet, the file made is one lsfg-vk reads, holding the new profile");
    }

    {
        FrameGenerationGlobal global = parseLsfgFile(handWritten, problem)->global;
        global.dll                   = "~/Games/Lossless Scaling/lsfg-vk.dll";
        global.allowHalfPrecision    = false;
        const auto set               = lsfgTextWith(handWritten, global, std::nullopt, problem);
        expect(set && *set == replacedOnce(handWritten, "allow_fp16 = true   # fp16 is fine here\n",
                                           "allow_fp16 = false   # fp16 is fine here\ndll = \"~/Games/Lossless Scaling/lsfg-vk.dll\"\n"),
               "the settings for every game change in lsfg-vk's [global] section and nowhere else");
        global.dll         = std::nullopt;
        const auto cleared = set ? lsfgTextWith(*set, global, std::nullopt, problem) : std::nullopt;
        expect(cleared && *cleared == replacedOnce(handWritten, "allow_fp16 = true", "allow_fp16 = false"), "a DLL path taken away removes its line");
    }

    {
        FrameGenerationProfile peak = *profileNamed(*parseLsfgFile(handWritten, problem), "Peak, tuned");
        for (const auto& [why, change] : std::vector<std::pair<const char*, void (*)(FrameGenerationProfile&)>>{
                 {"multiplier 0", [](FrameGenerationProfile& p) { p.multiplier = 0; }},
                 {"flow scale 0.1", [](FrameGenerationProfile& p) { p.flowScale = 0.1f; }},
                 {"flow scale 1.5", [](FrameGenerationProfile& p) { p.flowScale = 1.5f; }},
                 {"an unknown colour space", [](FrameGenerationProfile& p) { p.colorSpace = "p3"; }},
                 {"a name with a control character", [](FrameGenerationProfile& p) { p.activeIn = {"Bad\x7f.exe"}; }},
                 {"a name that is not UTF-8", [](FrameGenerationProfile& p) { p.activeIn = {"Bad\xff.exe"}; }}})
        {
            FrameGenerationProfile refused = peak;
            change(refused);
            problem.clear();
            expect(!lsfgTextWith(handWritten, std::nullopt, refused, problem) && !problem.empty(), why);
        }
    }

    {
        const fs::path file = dir / "conf.toml";
        LsfgFile       written;
        std::ofstream(file) << handWritten;
        fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
        FrameGenerationProfile peak = *profileNamed(*parseLsfgFile(handWritten, problem), "Peak, tuned");
        peak.multiplier             = 4;
        expect(saveLsfgFile(file.string(), std::nullopt, peak, written, problem), "a profile is saved into lsfg-vk's file");
        expect(contents(file) == replacedOnce(handWritten, "multiplier = 2 # two times", "multiplier = 4 # two times"), "with only its changed value written");
        expect((fs::status(file).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read),
               "the file keeps its permissions");
        expect(!fs::exists(file.string() + ".vkdaedalus.tmp"), "the file is replaced whole, with no temporary left");
        FrameGenerationProfile cube = *profileNamed(written, "cube");
        cube.multiplier             = 2;
        expect(saveLsfgFile(file.string(), std::nullopt, cube, written, problem) && profileNamed(written, "Peak, tuned")->multiplier == 4,
               "each save reads the file first, so another editor's change made meanwhile is kept");

        for (const char* bad : {"version = 1\n[global]\n[[profile]]\nname = \"a\"\n", "version = 2\n[[profile]]\nname = \"a\"\n", "version = 2\n[global]\n",
                                "version = 2\n[global]\nfoo = 1\n[[profile]]\nname = \"a\"\n", "version = 2\n[global]\n[[profile]]\nname = \"a\"\nmultiplier = 0\n",
                                "version = 2\n[global]\n[[profile]]\nname = \"a\"\nmultiplier = \"2\"\n", "version = 2\n[global\n"})
        {
            std::ofstream(file, std::ios::trunc) << bad;
            problem.clear();
            expect(!saveLsfgFile(file.string(), std::nullopt, peak, written, problem) && !problem.empty() && contents(file) == bad,
                   "a file lsfg-vk itself refuses is left alone, with lsfg-vk's reason");
        }

        const fs::path target = dir / "elsewhere.toml";
        std::ofstream(target) << lsfgDefault;
        fs::remove(file);
        fs::create_symlink(target, file);
        problem.clear();
        expect(!saveLsfgFile(file.string(), std::nullopt, peak, written, problem) && problem.find("link") != std::string::npos &&
                   fs::is_symlink(file) && contents(target) == lsfgDefault,
               "a file that is a link is not replaced by a plain file, and the file it links to is left alone");
        fs::remove(file);
        std::ofstream(file) << handWritten;
        fs::permissions(file, fs::perms::none);
        problem.clear();
        const bool overUnreadable = saveLsfgFile(file.string(), std::nullopt, peak, written, problem);
        fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
        expect(!overUnreadable && problem.find("cannot read") != std::string::npos && contents(file) == handWritten,
               "a file that exists but cannot be read is refused, not replaced as if it were empty");
    }

    if (const char* comma = std::getenv("VKDAEDALUS_TEST_COMMA_LOCALE"); comma != nullptr && *comma != '\0')
    {
        expect(std::setlocale(LC_NUMERIC, comma) != nullptr, "the comma-decimal locale the build names can be set");
        FrameGenerationProfile tuned = newLsfgProfileFor("default", {"Game.exe"}, 2, {});
        tuned.flowScale              = 0.75f;
        const auto written           = lsfgTextWith("", std::nullopt, tuned, problem);
        expect(written && written->find("flow_scale = 0.75\n") != std::string::npos,
               "in a comma-decimal locale, as a Wine game sets, a flow scale is still written with a decimal point, which TOML and lsfg-vk need");
        std::setlocale(LC_NUMERIC, "C");
    }
    else
        std::printf("note: VKDAEDALUS_TEST_COMMA_LOCALE is unset, so the comma-decimal locale case did not run\n");

    {
        const LsfgFile file = *parseLsfgFile(handWritten + "\n[[profile]]\nname = \"named\"\nactive_in = \"/x\"\n", problem);
        LsfgProcess    process;
        process.executable = "/usr/bin/wine64-preloader";
        process.mappedExes = {"/games/steamapps/common/PEAK/PEAK.exe"};
        process.processName = "wine64-preloade";
        expect(lsfgProfileFor(file, process) == 0u, "a Wine game is matched by the .exe it maps, as lsfg-vk matches it");
        process.profileNamed = "named";
        expect(lsfgProfileFor(file, process) == 2u, "LSFGVK_PROFILE naming a profile wins over every other match");
        process.profileNamed = "no such profile";
        expect(lsfgProfileFor(file, process) == 0u, "and naming a missing one falls back to the other matches");
        LsfgProcess cube;
        cube.executable = "/nix/store/x/bin/vkcube";
        expect(lsfgProfileFor(file, cube) == 1u, "a native program is matched by the end of its executable's path");
        LsfgProcess steam;
        steam.executable = "/games/start.sh";
        steam.steamAppId = "3527290";
        expect(lsfgProfileFor(file, steam) == 0u, "a Steam game nothing else matches is matched by its app id");
        steam.steamAppId.reset();
        expect(!lsfgProfileFor(file, steam), "and with no match at all lsfg-vk has no profile for the process");
        unsetenv("LSFGVK_PROFILE");
        unsetenv("SteamAppId");
        const LsfgProcess proton =
            lsfgProcessOf({"/steam/reaper", "SteamLaunch", "AppId=3527290", "--", "/steam/proton", "waitforexitandrun", "/games/PEAK/PEAK.exe"}, "PEAK.exe");
        expect(proton.mappedExes == std::vector<std::string>{"/games/PEAK/PEAK.exe"} && proton.executable.empty() && lsfgProfileFor(file, proton) == 0u,
               "before a Proton game starts, the launcher sees the profile lsfg-vk will match it by its .exe");
        const LsfgProcess native = lsfgProcessOf({"gamemoderun", "/games/cube/vkcube"}, "vkcube");
        expect(native.executable == "/games/cube/vkcube" && lsfgProfileFor(file, native) == 1u, "and a native game's by its executable's path");
        expect(lsfgProcessOf({"/games/game-01234567890123"}, "game-01234567890123").processName == "game-0123456789",
               "its process name is cut to the kernel's 15 characters, as lsfg-vk reads it");
        expect(!lsfgProfileFor(file, lsfgProcessOf({"/games/New.exe"}, "New.exe")), "a game with no profile is seen to have none");
    }

    {
        const std::string written = *lsfgTextWith("", std::nullopt, newLsfgProfileFor("default", {"Launcher.exe"}, 2, {}), problem);
        expect(written.find("active_in = \"/Launcher.exe\"\n") != std::string::npos,
               "a game is written as /name, since lsfg-vk matches the end of a path and Launcher.exe would also catch GameLauncher.exe");
        expect(newLsfgProfileFor("default", {"GAME.EXE"}, 2, {}).activeIn == std::vector<std::string>{"GAME.EXE"},
               "an .exe in another case stays bare, since lsfg-vk finds it only by its process name");
        expect(newLsfgProfileFor("night", {"a.exe", "b"}, 2, {}).name == lsfgProfileNameOf("night") && lsfgProfileNameOf("night") != lsfgProfileNameOf("day") &&
                   madeByVkDaedalus(lsfgProfileNameOf("night")) && !madeByVkDaedalus("night"),
               "the overlay's lsfg-vk profile is named for the vkDaedalus profile, which tells it from the owner's own");
        expect(lsfgActiveIn({"a.exe", "b", "a.exe"}) == std::vector<std::string>{"/a.exe", "/b"}, "every game of a profile is listed once, in order");
        const std::string listed = *lsfgTextWith("", std::nullopt, newLsfgProfileFor("night", {"a.exe", "b"}, 2, {}), problem);
        expect(listed.find("active_in = [\"/a.exe\", \"/b\"]\n") != std::string::npos, "a profile two games run lists both, as lsfg-vk reads a list");
    }

    {
        const std::string text = lsfgDefault + "\n[[profile]]\nname = \"mine\"\nactive_in = \"Owned.exe\"\nmultiplier = 3\n"
                                               "\n[[profile]]\nname = \"vkdaedalus night\"\nactive_in = [\"/a.exe\", \"/moved\", \"/stranger\"]\nmultiplier = 2\n"
                                               "\n[[profile]]\nname = \"vkdaedalus day\"\nactive_in = \"/b\"\nmultiplier = 4\n";
        const auto file = parseLsfgFile(text, problem);
        expect(file.has_value(), "the sync fixture parses");
        const std::map<std::string, std::vector<std::string>> users = {{"night", {"a.exe", "Owned.exe"}}, {"day", {"b", "moved"}}};
        const std::vector<FrameGenerationProfile> changed = file ? lsfgProfilesToSync(*file, users) : std::vector<FrameGenerationProfile>{};
        const auto changedNamed = [&](const std::string& name) -> const FrameGenerationProfile* {
            for (const FrameGenerationProfile& profile : changed)
                if (profile.name == name)
                    return &profile;
            return nullptr;
        };
        expect(changedNamed("vkdaedalus night") && changedNamed("vkdaedalus night")->activeIn == std::vector<std::string>{"/a.exe", "/stranger"},
               "a game moved to another profile leaves this one's list, a game vkDaedalus does not know stays, and a game the owner's own profile matches is not added");
        expect(changedNamed("vkdaedalus day") && changedNamed("vkdaedalus day")->activeIn == std::vector<std::string>{"/b", "/moved"},
               "and it joins the list of the profile it runs now");
        expect(!changedNamed("mine") && changed.size() == 2, "the owner's own lsfg-vk profiles are never changed");
        std::string synced = text;
        for (const FrameGenerationProfile& profile : changed)
            synced = lsfgTextWith(synced, std::nullopt, profile, problem).value_or(synced);
        const auto again = parseLsfgFile(synced, problem);
        expect(again && lsfgProfilesToSync(*again, users).empty(), "a second sync changes nothing");
        const std::map<std::string, std::vector<std::string>> owned = {{"night", {"a.exe"}}, {"day", {"Owned.exe", "b", "moved"}}};
        const auto ownedFile = parseLsfgFile(text + "\n[[profile]]\nname = \"vkdaedalus x\"\nactive_in = \"/Owned.exe\"\n", problem);
        const std::vector<FrameGenerationProfile> ownedChanged = ownedFile ? lsfgProfilesToSync(*ownedFile, owned) : std::vector<FrameGenerationProfile>{};
        const auto ownedNamed = [&](const std::string& name) -> const FrameGenerationProfile* {
            for (const FrameGenerationProfile& profile : ownedChanged)
                if (profile.name == name)
                    return &profile;
            return nullptr;
        };
        expect(ownedNamed("vkdaedalus x") && ownedNamed("vkdaedalus x")->activeIn.empty() && ownedNamed("vkdaedalus day") &&
                   ownedNamed("vkdaedalus day")->activeIn == std::vector<std::string>{"/b", "/moved"},
               "a game the owner's own profile matches leaves every list vkDaedalus made and joins none, so his profile wins whatever the file's order");

        const auto renamed = lsfgTextRenamed(text, "vkdaedalus night", "vkdaedalus evening", problem);
        expect(renamed && replacedOnce(text, "name = \"vkdaedalus night\"", "name = \"vkdaedalus evening\"") == *renamed,
               "renaming a profile changes its name and not another byte");
        expect(!lsfgTextRenamed(text, "vkdaedalus night", "vkdaedalus day", problem), "renaming onto a taken name is refused");
        expect(lsfgTextRenamed(text, "vkdaedalus gone", "vkdaedalus new", problem) == text, "renaming a profile lsfg-vk does not have changes nothing");
    }

    {
        const fs::path home = dir / "home";
        fs::create_directories(home / ".config");
        setenv("HOME", home.c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("LSFGVK_CONFIG");
        unsetenv("LSFGVK_FLATPAK");
        expect(lsfgConfigPath() == (home / ".config/lsfg-vk/conf.toml").string(), "lsfg-vk's file is found in the home's .config by default");
        setenv("XDG_CONFIG_HOME", (dir / "xdg").c_str(), 1);
        expect(lsfgConfigPath() == (dir / "xdg/lsfg-vk/conf.toml").string(), "then in XDG_CONFIG_HOME");
        setenv("LSFGVK_FLATPAK", "1", 1);
        expect(lsfgConfigPath() == (home / ".config/lsfg-vk/conf.toml").string(), "but not in a Flatpak, as lsfg-vk decides");
        setenv("LSFGVK_CONFIG", (dir / "named.toml").c_str(), 1);
        expect(lsfgConfigPath() == (dir / "named.toml").string(), "and LSFGVK_CONFIG names it over everything");
        unsetenv("LSFGVK_CONFIG");
        unsetenv("LSFGVK_FLATPAK");
        unsetenv("XDG_CONFIG_HOME");

        unsetenv("XDG_DATA_HOME");
        unsetenv("LSFGVK_DLL_PATH");
        const fs::path work = dir / "work";
        fs::create_directories(work);
        fs::current_path(work);
        expect(!lsfgDllPath({}), "with no lsfg-vk.dll where lsfg-vk looks, none is found");
        const fs::path steam = home / ".steam/steam/steamapps/common/Lossless Scaling";
        fs::create_directories(steam);
        std::ofstream(steam / "lsfg-vk.dll") << "dll";
        expect(lsfgDllPath({}) == (steam / "lsfg-vk.dll").string(), "lsfg-vk.dll is found in a Steam folder lsfg-vk searches");
        FrameGenerationGlobal named;
        named.dll = "~/Lossless Scaling";
        expect(lsfgDllPath(named) == (home / "Lossless Scaling/lsfg-vk.dll").string(),
               "a DLL path in lsfg-vk's file wins, with ~ for the home and a Lossless Scaling folder taking the DLL in it, as lsfg-vk reads it");
        setenv("LSFGVK_DLL_PATH", (dir / "nowhere/Lossless.dll").c_str(), 1);
        expect(lsfgDllPath(named) == (dir / "nowhere/lsfg-vk.dll").string(), "LSFGVK_DLL_PATH wins over the file, as lsfg-vk reads it, found or not");
        unsetenv("LSFGVK_DLL_PATH");

        const fs::path library = dir / "games/SteamLibrary";
        fs::create_directories(library / "steamapps/common/Lossless Scaling");
        std::ofstream(library / "steamapps/common/Lossless Scaling/lsfg-vk.dll") << "dll";
        fs::create_directories(home / ".local/share/Steam/steamapps");
        std::ofstream(home / ".local/share/Steam/steamapps/libraryfolders.vdf")
            << "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" << (home / ".local/share/Steam").string()
            << "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"1\"\n\t\t}\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" << library.string()
            << "\"\n\t\t\"label\"\t\t\"\"\n\t}\n}\n";
        expect(steamLibraryFolders(contents(home / ".local/share/Steam/steamapps/libraryfolders.vdf")) ==
                   std::vector<std::string>{(home / ".local/share/Steam").string(), library.string()},
               "every library folder Steam lists is read from its libraryfolders.vdf");
        expect(lsfgDllInSteamLibraries() == (library / "steamapps/common/Lossless Scaling/lsfg-vk.dll").string(),
               "lsfg-vk.dll in a Steam library outside the home is found, where lsfg-vk itself does not look");
    }

    if (failures)
        std::printf("%d frame generation check(s) failed\n", failures);
    return failures ? 1 : 0;
}
