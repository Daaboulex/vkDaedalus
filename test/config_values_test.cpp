#include "config.hpp"
#include "config_serializer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

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
    if (argc < 2)
        return 2;

    const fs::path dir = argv[1];
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path base    = dir / "vkdaedalus.conf";
    const fs::path profile = dir / "vkcube.conf";
    std::ofstream(base) << "effects = cas:smaa\n";
    std::ofstream(profile) << "effects = \n";
    Config baseConfig(base.string());
    Config profileConfig(profile.string());
    profileConfig.setFallback(&baseConfig);
    expect(profileConfig.getOption<std::vector<std::string>>("effects", {"unset"}).empty(),
           "an empty effects line in a profile means no effects, not the base config's");

    const std::string awkwardPath = (dir / "a pack" / "shader#1.fx").string();
    const fs::path    saved       = dir / "saved.conf";
    expect(ConfigSerializer::saveToPath(saved.string(), {"awkward"}, {}, {}, {{"awkward", awkwardPath}}),
           "the profile holding an awkward path can be written");
    Config reread(saved.string());
    expect(reread.getOption<std::string>("awkward") == awkwardPath,
           "an effect path with a space and a '#' reads back exactly as it was written");

    const fs::path globalBase    = dir / "global-base.conf";
    const fs::path globalProfile = dir / "global-profile.conf";
    std::ofstream(globalBase) << "@RESHADE_DEPTH_INPUT_IS_REVERSED = 0\n@SHARED = 1\n";
    std::ofstream(globalProfile) << "@SHARED = 2\nfx@SHARED = 9\n";
    Config globalBaseConfig(globalBase.string());
    Config globalProfileConfig(globalProfile.string());
    globalProfileConfig.setFallback(&globalBaseConfig);
    const auto globals = globalProfileConfig.getGlobalDefinitions();
    expect(globals.size() == 2 && globals.count("RESHADE_DEPTH_INPUT_IS_REVERSED") && globals.at("RESHADE_DEPTH_INPUT_IS_REVERSED") == "0",
           "a global definition in the base config reaches every profile, and an effect's own is not global");
    expect(globals.count("SHARED") && globals.at("SHARED") == "2", "a profile's global definition wins over the base config's");

    const fs::path globalSaved = dir / "global-saved.conf";
    expect(ConfigSerializer::saveToPath(globalSaved.string(), {"fx"}, {}, {}, {{"fx", "/fx.fx"}},
                                        {{"RESHADE_DEPTH_INPUT_IS_REVERSED", "0", "1", ""}, {"STRENGTH", "3", "1", "fx"}}),
           "a profile with a global and an effect definition can be written");
    Config globalReread(globalSaved.string());
    const auto savedGlobals = globalReread.getGlobalDefinitions();
    expect(savedGlobals.size() == 1 && savedGlobals.count("RESHADE_DEPTH_INPUT_IS_REVERSED") &&
               savedGlobals.at("RESHADE_DEPTH_INPUT_IS_REVERSED") == "0",
           "a saved global definition reads back as global, and only it");
    expect(globalReread.getOption<std::string>("fx@STRENGTH") == "3", "a saved effect definition reads back under its effect");

    const fs::path hidden = dir / "hidden.conf";
    std::ofstream(hidden) << "effects = fx\nfx = /fx.fx\nfx@USE_GATE = 0\nfx.GateBlue = 0.5\nfx.Strength = 2\nfx.1.Strength = 3\ngone.Strength = 4\nfx.2 = /p/fx.fx\n";
    expect(ConfigSerializer::saveToPath(hidden.string(), {"fx"}, {}, {}, {{"fx", "/fx.fx"}}, {{"USE_GATE", "0", "0", "fx"}}, {},
                                        std::set<std::string>{"fx.Strength", "fx.halfPrecision"}),
           "a profile whose effect hides a parameter can be written");
    Config hiddenReread(hidden.string());
    expect(hiddenReread.getOption<std::string>("fx.GateBlue") == "0.5",
           "the saved value of a parameter its definition hides is kept for when it shows again");
    expect(hiddenReread.getOption<std::string>("fx.Strength").empty(), "a live parameter reset to its default is not carried");
    expect(hiddenReread.getOption<std::string>("fx.1.Strength").empty() && hiddenReread.getOption<std::string>("gone.Strength").empty(),
           "a parameter line of an instance that left the chain is not carried");
    expect(hiddenReread.getOption<std::string>("fx.2").empty(), "the definition line of an instance removed from the chain is not carried");

    const fs::path    gpus     = dir / "gpus.conf";
    const std::string gpuList  = "10de:2184,1002:7550@0000:73:00.0";
    std::ofstream(gpus) << "effects = fx\nfx = /fx.fx\ngpus = " << gpuList << "\nfx.gpu = 2\n";
    expect(ConfigSerializer::saveToPath(gpus.string(), {"fx"}, {}, {}, {{"fx", "/fx.fx"}}, {}, {}, std::set<std::string>{"fx.halfPrecision"}),
           "a profile that assigns GPUs can be written");
    Config gpusReread(gpus.string());
    expect(gpusReread.getOption<std::string>("gpus") == gpuList, "a profile's GPU list survives an overlay save as written");
    expect(gpusReread.getInstanceOption<int32_t>("fx", "gpu", 0) == 2, "a row's GPU number survives an overlay save");

    ProfileSettings edited;
    edited.gpus = "1002:13c0";
    expect(ConfigSerializer::saveToPath(gpus.string(), {"fx"}, {}, {}, {{"fx", "/fx.fx"}}, {}, edited, std::set<std::string>{"fx.gpu"}),
           "a profile whose GPU list was edited can be written");
    Config editedReread(gpus.string());
    expect(editedReread.getOption<std::string>("gpus") == "1002:13c0", "an edited GPU list replaces the saved one");
    expect(editedReread.getInstanceOption<int32_t>("fx", "gpu", 0) == 0, "a row put back on the game's GPU loses its saved number");

    const fs::path numbers = dir / "numbers.conf";
    std::ofstream(numbers) << "good = 42\nmaxEffects = -1\ncount = 42abc\nhuge = 99999999999\n"
                              "ratio = 0.5f\nbad = 1.5xyz\nunbounded = infinity\nmissing = nan\nflag = yes\nswitch = on\n";
    Config numberConfig(numbers.string());
    expect(numberConfig.getOption<int32_t>("good", 7) == 42, "a whole number is read");
    expect(numberConfig.getOption<uint32_t>("maxEffects", 10) == 10,
           "a negative count keeps the default instead of wrapping to four billion");
    expect(numberConfig.getOption<int32_t>("count", 7) == 7, "a number with text after it keeps the default");
    expect(numberConfig.getOption<uint32_t>("huge", 3) == 3,
           "a count too large for 32 bits keeps the default instead of being truncated");
    expect(numberConfig.getOption<float>("ratio", 0.0f) == 0.5f, "a float written with a trailing f is read");
    expect(numberConfig.getOption<float>("bad", 2.0f) == 2.0f, "a float with text after it keeps the default");
    expect(numberConfig.getOption<float>("unbounded", 1.0f) == 1.0f, "an infinite float keeps the default");
    expect(numberConfig.getOption<float>("missing", 1.0f) == 1.0f, "a NaN float keeps the default");
    expect(numberConfig.getOption<bool>("flag", true), "a bool that is not a known word keeps the default");
    expect(numberConfig.getOption<bool>("switch", false),
           "on reads as true here too, the word the overlay's own settings file writes");

    numberConfig.setOverride("good", "12x");
    expect(numberConfig.getOption<int32_t>("good", 7) == 7, "an override with text after the number keeps the default");
    numberConfig.setOverride("ratio", "1.5xyz");
    expect(numberConfig.getOption<float>("ratio", 2.0f) == 2.0f, "a float override with text after it keeps the default");
    numberConfig.setOverride("maxEffects", "-1");
    expect(numberConfig.getOption<uint32_t>("maxEffects", 10) == 10, "a negative count override keeps the default");
    numberConfig.setOverride("count", "9");
    expect(numberConfig.getOption<int32_t>("count", 7) == 9, "a whole number override is read");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: empty values stay empty, written paths read back whole and malformed numbers are refused\n");
    return 0;
}
