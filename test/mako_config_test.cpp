#include "mako_config.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace vkBasalt;
namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
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

static std::string refusalOf(const std::string& text)
{
    std::string problem;
    return parseMakoFile(text, problem) ? "accepted" : problem;
}

static void writeFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

static const std::string seed = "# my MAKO settings\n"
                                "version = 2\n"
                                "current_profile = \"mine\"\n"
                                "\n"
                                "[global]\n"
                                "allow_fp16 = true # keep fp16\n"
                                "\n"
                                "# the owner's own profile\n"
                                "[[profile]]\n"
                                "name = \"mine\"\n"
                                "active_in = [\"Owned.exe\"]\n"
                                "multiplier = 3 # three times\n"
                                "\n"
                                "[[profile]]\n"
                                "name = \"vkdaedalus default\"\n"
                                "active_in = \"/Game.exe\"\n"
                                "flow_scale = 0.75\n"
                                "\n"
                                "[profile.handheld]\n"
                                "target_fps = 60 # on battery\n"
                                "\n"
                                "[profile.docked]\n"
                                "target_fps = 144\n";

struct KeyChange
{
    std::string                        key;
    std::string                        text;
    std::function<void(MakoSettings&)> change;
};

static const std::vector<KeyChange> keyChanges = {
    {"gpu", "\"Some GPU\"", [](MakoSettings& s) { s.gpu = "Some GPU"; }},
    {"multiplier", "4", [](MakoSettings& s) { s.multiplier = 4; }},
    {"frame_generation_provisioned", "false", [](MakoSettings& s) { s.frameGenerationProvisioned = false; }},
    {"frame_generation_enabled", "false", [](MakoSettings& s) { s.frameGenerationEnabled = false; }},
    {"frame_generation_model", "\"lossless-scaling\"", [](MakoSettings& s) { s.frameGenerationModel = "lossless-scaling"; }},
    {"mako_fg_mode", "\"extra-performance\"", [](MakoSettings& s) { s.makoFgMode = "extra-performance"; }},
    {"scaling_enabled", "true", [](MakoSettings& s) { s.scalingEnabled = true; }},
    {"swapchain_image_count_compatibility", "true", [](MakoSettings& s) { s.swapchainImageCountCompatibility = true; }},
    {"mailbox_presentation_compatibility", "true", [](MakoSettings& s) { s.mailboxPresentationCompatibility = true; }},
    {"scaling_method", "\"ls1-performance\"", [](MakoSettings& s) { s.scalingMethod = "ls1-performance"; }},
    {"scaling_factor", "1.75", [](MakoSettings& s) { s.scalingFactor = 1.75f; }},
    {"scaling_supersampling", "true", [](MakoSettings& s) { s.scalingSupersampling = true; }},
    {"scaling_sharpness", "0.25", [](MakoSettings& s) { s.scalingSharpness = 0.25f; }},
    {"hdr_quality", "\"quality\"", [](MakoSettings& s) { s.hdrQuality = "quality"; }},
    {"frame_generation_refresh_threshold", "60", [](MakoSettings& s) { s.frameGenerationRefreshThreshold = 60; }},
    {"base_fps_cap", "72", [](MakoSettings& s) { s.baseFpsCap = 72; }},
    {"gpu_headroom_percent", "15", [](MakoSettings& s) { s.gpuHeadroomPercent = 15; }},
    {"adaptive", "true", [](MakoSettings& s) { s.adaptive = true; }},
    {"adaptive_auto_base_fps_cap", "true", [](MakoSettings& s) { s.adaptiveAutoBaseFpsCap = true; }},
    {"adaptive_fractional_real_frame_priority", "\"very-high\"", [](MakoSettings& s) { s.adaptiveFractionalRealFramePriority = "very-high"; }},
    {"target_fps", "90", [](MakoSettings& s) { s.targetFps = 90; }},
    {"adaptive_target_refresh_rate", "true", [](MakoSettings& s) { s.adaptiveTargetRefreshRate = true; }},
    {"adaptive_max_multiplier", "4", [](MakoSettings& s) { s.adaptiveMaxMultiplier = 4; }},
    {"adaptive_stable_cadence", "false", [](MakoSettings& s) { s.adaptiveStableCadence = false; }},
    {"gamescope_vrr_mode", "\"off\"", [](MakoSettings& s) { s.gamescopeVrrMode = "off"; }},
    {"dynamic_cadence_recovery", "true", [](MakoSettings& s) { s.dynamicCadenceRecovery = true; }},
    {"dynamic_cadence_probe_interval_seconds", "0.5", [](MakoSettings& s) { s.dynamicCadenceProbeIntervalSeconds = 0.5f; }},
    {"ultra_performance", "true", [](MakoSettings& s) { s.ultraPerformance = true; }},
    {"flow_scale", "0.5", [](MakoSettings& s) { s.flowScale = 0.5f; }},
    {"performance_mode", "true", [](MakoSettings& s) { s.performanceMode = true; }},
};

static void refusals()
{
    const std::string                                      profile = "version = 2\n[[profile]]\nname = \"p\"\n";
    const std::vector<std::pair<std::string, std::string>> refused = {
        {"", "unsupported configuration version"},
        {"version = 1\n", "unsupported configuration version"},
        {"version = \"2\"\n", "unsupported configuration version"},
        {"version = 2.0\n", "unsupported configuration version"},
        {profile + "frame_generation_model = 1\n", "frame_generation_model must be a string"},
        {profile + "mako_fg_mode = true\n", "mako_fg_mode must be a string"},
        {profile + "gpu_headroom_percent = 7\n", "gpu_headroom_percent must be 0, 5, 10, 15 or 20"},
        {profile + "gpu_headroom_percent = 25\n", "gpu_headroom_percent must be 0, 5, 10, 15 or 20"},
        {profile + "gpu_headroom_percent = -5\n", "gpu_headroom_percent must be 0, 5, 10, 15 or 20"},
        {profile + "gpu_headroom_percent = 5.0\n", "gpu_headroom_percent must be 0, 5, 10, 15 or 20"},
        {profile + "gpu_headroom_percent = \"5\"\n", "gpu_headroom_percent must be 0, 5, 10, 15 or 20"},
        {profile + "frame_generation_model = \"lsfg\"\n", "frame_generation_model must be 'lossless-scaling' or 'mako-fg'"},
        {profile + "frame_generation_model = \"MAKO-FG\"\n", "frame_generation_model must be 'lossless-scaling' or 'mako-fg'"},
        {profile + "mako_fg_mode = \"fast\"\n", "mako_fg_mode must be 'quality', 'balanced', 'performance' or 'extra-performance'"},
        {profile + "scaling_method = \"fsr\"\n", "unknown scaling method: fsr"},
        {profile + "hdr_quality = \"best\"\n", "unknown HDR quality: best"},
        {profile + "adaptive_fractional_real_frame_priority = \"max\"\n", "unknown Adaptive Fractional real-frame priority: max"},
        {profile + "gamescope_vrr_mode = \"auto\"\n", "unknown Gamescope VRR mode: auto"},
        {profile + "pacing = \"vsync\"\n", "unknown pacing method: vsync"},
        {profile + "multiplier = 1\n", "multiplier must be between 2 and 5"},
        {profile + "multiplier = 6\n", "multiplier must be between 2 and 5"},
        {profile + "scaling_factor = 0.5\n", "scaling_factor must be between 1.0 and 2.0"},
        {profile + "scaling_factor = nan\n", "scaling_factor must be between 1.0 and 2.0"},
        {profile + "scaling_sharpness = 1.5\n", "scaling_sharpness must be between 0.0 and 1.0"},
        {profile + "base_fps_cap = 1001\n", "base_fps_cap must be 0 or between 1 and 1000"},
        {profile + "frame_generation_refresh_threshold = 2000\n", "frame_generation_refresh_threshold must be 0 or between 1 and 1000"},
        {profile + "target_fps = 5\n", "target_fps must be between 10 and 1000"},
        {profile + "adaptive_max_multiplier = 9\n", "adaptive_max_multiplier must be between 2 and 5"},
        {profile + "dynamic_cadence_probe_interval_seconds = 5\n", "dynamic_cadence_probe_interval_seconds must be between 0.1 and 3.0"},
        {profile + "flow_scale = 0.1\n", "flow_scale must be between 0.25 and 1.0"},
        {profile + "[profile.handheld]\n", "power profiles require handheld and docked tables"},
        {profile + "handheld = 1\ndocked = {}\n", "power profiles require handheld and docked tables"},
        {profile + "[profile.handheld]\nname = \"x\"\n[profile.docked]\n", "power profile identity must remain shared"},
        {profile + "[profile.handheld]\n[profile.docked]\nactive_in = \"x\"\n", "power profile identity must remain shared"},
        {profile + "[profile.handheld]\ntarget_fps = 5\n[profile.docked]\n", "target_fps must be between 10 and 1000"},
        {profile + "multiplier = 9\nscaling_method = \"x\"\n", "unknown scaling method: x"},
        {profile + "pacing = \"x\"\nframe_generation_model = \"y\"\n", "frame_generation_model must be 'lossless-scaling' or 'mako-fg'"},
        {profile + "flow_scale = 0.1\nmultiplier = 9\n", "multiplier must be between 2 and 5"},
        {profile + "multiplier = 9\n[profile.handheld]\n", "multiplier must be between 2 and 5"},
    };
    for (const auto& [text, message] : refused)
        expect(refusalOf(text) == message, "MAKO refuses \"" + text + "\" with its own message \"" + message + "\", not \"" + refusalOf(text) + "\"");
    expect(refusalOf("version = 2\nx = = 1\n").rfind("unable to parse configuration\n- ", 0) == 0 && refusalOf("version = 2\nx = = 1\n").size() > 33,
           "a file that is not TOML is refused with MAKO's words and the parser's reason");

    for (const std::string accepted : {profile + "multiplier = -3\n",
                                       profile + "target_fps = 5000000000\n",
                                       profile + "flow_scale = nan\n",
                                       profile + "scaling_method = 5\n",
                                       profile + "unknown = 1\n",
                                       std::string("version = 2\nglobal = 5\nprofile = 5\n"),
                                       profile + "[profile.handheld]\n[profile.docked]\n",
                                       profile + "handheld = {}\ndocked.target_fps = 60\n"})
        expect(refusalOf(accepted) == "accepted", "MAKO accepts \"" + accepted + "\", so the overlay does too");
}

static void quirks()
{
    std::string problem;
    const auto  file = parseMakoFile("version = 2\ncurrent_profile = \"x\"\n[global]\nallow_fp16 = 0\ndll = 5\n[[profile]]\nname = 5\n"
                                     "active_in = [\"a\", 1, \"b\"]\nmultiplier = 3.0\ntarget_fps = 5000000000\nscaling_method = 5\ngpu = \"\"\n"
                                     "flow_scale = nan\nadaptive = 2\n[[profile]]\nactive_in = \"c\"\n",
                                    problem);
    expect(file && file->currentProfile == "x" && !file->global.allowHalfPrecision && !file->global.dll,
           "a top-level current_profile is read, an integer flag is a bool and a DLL that is not a string is unset");
    expect(file && file->profiles.size() == 2 && file->profiles[0].name == "unnamed"
               && file->profiles[0].activeIn == std::vector<std::string>{"a", "b"},
           "a name that is not a string is unnamed and entries that are not strings are skipped, as MAKO reads them");
    expect(
        file && file->profiles[0].settings.multiplier == 3 && file->profiles[0].settings.targetFps == 120
            && file->profiles[0].settings.scalingMethod == "mako" && !file->profiles[0].settings.gpu
            && std::isnan(file->profiles[0].settings.flowScale) && file->profiles[0].settings.adaptive,
        "a whole float is an integer, a value out of its type's range or of another type takes MAKO's default, an empty GPU is none and nan passes");
    expect(file && file->profiles[1].activeIn == std::vector<std::string>{"c"} && file->profiles[1].settings == MakoSettings{},
           "a profile naming nothing else runs on MAKO's defaults");

    MakoSettings recovery;
    recovery.dynamicCadenceRecovery              = true;
    recovery.baseFpsCap                          = 60;
    recovery.gpuHeadroomPercent                  = 10;
    recovery.adaptiveAutoBaseFpsCap              = true;
    recovery.adaptiveFractionalRealFramePriority = "high";
    const MakoSettings inEffect                  = makoSettingsInEffect(recovery);
    expect(inEffect.baseFpsCap == 0 && inEffect.gpuHeadroomPercent == 0 && !inEffect.adaptiveAutoBaseFpsCap
               && inEffect.adaptiveFractionalRealFramePriority == "auto" && recovery.baseFpsCap == 60,
           "with cadence recovery on, MAKO runs no base cap, auto cap, real-frame priority or headroom, while the file keeps its values");
}

static void everyKey()
{
    std::string       problem;
    const auto        file    = parseMakoFile(seed, problem);
    const MakoProfile current = file ? *makoProfileNamed(*file, "vkdaedalus default") : MakoProfile{};
    expect(file && current.power && current.settings.flowScale == 0.75f && current.power->handheld.targetFps == 60
               && current.power->docked.targetFps == 144 && current.power->handheld.flowScale == 0.75f,
           "a power table runs the profile's values with its own over them");
    if (!current.power)
        return;
    for (const KeyChange& change : keyChanges)
    {
        const std::string line = change.key + " = " + change.text;

        MakoProfile base = current;
        change.change(base.settings);
        if (change.key != "target_fps")
        {
            change.change(base.power->handheld);
            change.change(base.power->docked);
        }
        const auto        baseText = makoTextWith(seed, std::nullopt, base, problem);
        const std::string wantBase = change.key == "flow_scale" ? replacedOnce(seed, "flow_scale = 0.75", line)
                                                                : replacedOnce(seed, "flow_scale = 0.75\n", "flow_scale = 0.75\n" + line + "\n");
        expect(baseText && *baseText == wantBase,
               change.key + " set on the profile is written into its own section and changes no other byte: " + baseText.value_or(problem));

        MakoProfile handheld = current;
        change.change(handheld.power->handheld);
        const auto        handheldText = makoTextWith(seed, std::nullopt, handheld, problem);
        const std::string wantHandheld = change.key == "target_fps" ? replacedOnce(seed, "target_fps = 60 #", line + " #")
                                                                    : replacedOnce(seed, "# on battery\n", "# on battery\n" + line + "\n");
        expect(handheldText && *handheldText == wantHandheld,
               change.key + " set for the battery goes into [profile.handheld] alone: " + handheldText.value_or(problem));

        MakoProfile docked = current;
        change.change(docked.power->docked);
        const auto        dockedText = makoTextWith(seed, std::nullopt, docked, problem);
        const std::string wantDocked = change.key == "target_fps" ? replacedOnce(seed, "target_fps = 144", line) : seed + line + "\n";
        expect(dockedText && *dockedText == wantDocked,
               change.key + " set for plugged in goes into [profile.docked] alone: " + dockedText.value_or(problem));

        MakoProfile fresh = newMakoProfileFor("night", {"a.exe"});
        change.change(fresh.settings);
        const auto made = makoTextWith("", std::nullopt, fresh, problem);
        const auto back = made ? parseMakoFile(*made, problem) : std::nullopt;
        expect(back && back->profiles.size() == 1 && back->profiles[0] == fresh && made->find("\n" + line + "\n") != std::string::npos,
               change.key + " reads back from a new profile as it was written");
    }
    MakoProfile pinned         = current;
    pinned.settings.multiplier = 4;
    const auto pinnedText      = makoTextWith(seed, std::nullopt, pinned, problem);
    const auto pinnedBack      = pinnedText ? parseMakoFile(*pinnedText, problem) : std::nullopt;
    expect(pinnedText && pinnedBack && makoProfileNamed(*pinnedBack, "vkdaedalus default")->power->handheld.multiplier == 2
               && *pinnedText
                      == replacedOnce(replacedOnce(replacedOnce(seed, "flow_scale = 0.75\n", "flow_scale = 0.75\nmultiplier = 4\n"),
                                                   "# on battery\n",
                                                   "# on battery\nmultiplier = 2\n"),
                                      "target_fps = 144\n",
                                      "target_fps = 144\nmultiplier = 2\n"),
           "a change to the profile alone keeps both power sets as they were, written into each table");
}

static void powerSets()
{
    std::string       problem;
    const std::string plain         = "version = 2\n[[profile]]\nname = \"p\"\nmultiplier = 3\n\n[[profile]]\nname = \"next\"\n";
    MakoProfile       split         = *makoProfileNamed(*parseMakoFile(plain, problem), "p");
    split.power                     = MakoPowerSets{split.settings, split.settings};
    split.power->handheld.targetFps = 60;
    const auto made                 = makoTextWith(plain, std::nullopt, split, problem);
    expect(made && *made == replacedOnce(plain, "multiplier = 3\n", "multiplier = 3\n[profile.handheld]\ntarget_fps = 60\n[profile.docked]\n"),
           "separate settings on battery add both tables MAKO needs after the profile's own lines, holding only what differs");

    MakoProfile folded = split;
    folded.power.reset();
    const auto removed = made ? makoTextWith(*made, std::nullopt, folded, problem) : std::nullopt;
    expect(removed && *removed == plain, "folding them back removes both tables and nothing else");

    const std::string gpu    = "version = 2\n[[profile]]\nname = \"p\"\ngpu = \"Card\"\n[profile.handheld]\n[profile.docked]\n";
    MakoProfile       noGpu  = *makoProfileNamed(*parseMakoFile(gpu, problem), "p");
    noGpu.power->docked.gpu  = std::nullopt;
    const auto clearedDocked = makoTextWith(gpu, std::nullopt, noGpu, problem);
    expect(clearedDocked && *clearedDocked == gpu + "gpu = \"\"\n", "a power set without the profile's GPU writes the empty GPU MAKO reads as none");
    noGpu.settings.gpu        = std::nullopt;
    noGpu.power->handheld.gpu = std::nullopt;
    const auto clearedAll     = makoTextWith(gpu, std::nullopt, noGpu, problem);
    expect(clearedAll && *clearedAll == "version = 2\n[[profile]]\nname = \"p\"\n[profile.handheld]\n[profile.docked]\n",
           "the overlay's \"Use the game's GPU\" removes the GPU line");

    const std::string dotted        = "version = 2\n[[profile]]\nname = \"p\"\nhandheld.target_fps = 60\ndocked = { target_fps = 90 }\n";
    MakoProfile       inline_       = *makoProfileNamed(*parseMakoFile(dotted, problem), "p");
    inline_.power->docked.targetFps = 100;
    problem.clear();
    expect(!makoTextWith(dotted, std::nullopt, inline_, problem)
               && problem.find("docked table that is not a [profile.docked] section") != std::string::npos,
           "a power set written inline is refused, not edited into the wrong table");
    inline_.power->docked.targetFps    = 90;
    inline_.settings.multiplier        = 4;
    inline_.power->handheld.multiplier = 4;
    inline_.power->docked.multiplier   = 4;
    const auto beside                  = makoTextWith(dotted, std::nullopt, inline_, problem);
    expect(beside && *beside == replacedOnce(dotted, "docked = { target_fps = 90 }\n", "docked = { target_fps = 90 }\nmultiplier = 4\n"),
           "while the profile's own values beside it still change");

    MakoProfile both = *makoProfileNamed(*parseMakoFile(seed, problem), "vkdaedalus default");
    expect(makoSettingsFor(both, MakoPowerSource::Handheld).targetFps == 60 && makoSettingsFor(both, MakoPowerSource::Docked).targetFps == 144
               && makoSettingsFor(both, MakoPowerSource::Unknown).targetFps == 120,
           "on battery MAKO runs the handheld set, plugged in the docked one and with the source unknown the profile itself");
    both.power.reset();
    expect(&makoSettingsFor(both, MakoPowerSource::Handheld) == &both.settings, "a profile without power sets runs itself on any source");
}

static void globals()
{
    std::string problem;
    MakoGlobal  global;
    global.dll                = "/games/Lossless Scaling/Lossless.dll";
    global.allowHalfPrecision = false;
    const auto set            = makoTextWith(seed, global, std::nullopt, problem);
    expect(set
               && *set
                      == replacedOnce(seed,
                                      "allow_fp16 = true # keep fp16\n",
                                      "allow_fp16 = false # keep fp16\ndll = \"/games/Lossless Scaling/Lossless.dll\"\n"),
           "the settings for every game change in MAKO's [global] section and nowhere else");
    global.dll         = std::nullopt;
    const auto cleared = set ? makoTextWith(*set, global, std::nullopt, problem) : std::nullopt;
    expect(cleared && *cleared == replacedOnce(seed, "allow_fp16 = true", "allow_fp16 = false"), "a DLL taken away removes its line");
    const std::string none = "version = 2\n[[profile]]\nname = \"p\"\n";
    global.dll             = "/x/Lossless.dll";
    const auto added       = makoTextWith(none, global, std::nullopt, problem);
    expect(added && *added == none + "\n[global]\ndll = \"/x/Lossless.dll\"\nallow_fp16 = false\n", "a file without [global] gets one at its end");
    problem.clear();
    expect(!makoTextWith("version = 2\nglobal.dll = \"/a\"\n", global, std::nullopt, problem) && !problem.empty(),
           "a global written as dotted keys is refused");
    global.dll = "/bad\x01path";
    problem.clear();
    expect(!makoTextWith(seed, global, std::nullopt, problem) && !problem.empty(), "a DLL path with a control character is refused");
}

static void editorRefusals()
{
    std::string problem;
    MakoProfile wrong         = newMakoProfileFor("default", {"Game.exe"});
    wrong.settings.multiplier = 9;
    problem.clear();
    expect(!makoTextWith(seed, std::nullopt, wrong, problem)
               && problem == "MAKO would refuse its file after this change: multiplier must be between 2 and 5",
           "a value MAKO refuses is never written, and MAKO's reason is given");
    MakoProfile badName = newMakoProfileFor("default", {"Bad\x7f.exe"});
    problem.clear();
    expect(!makoTextWith(seed, std::nullopt, badName, problem) && !problem.empty(), "a game name with a control character is refused");
    MakoProfile empty  = newMakoProfileFor("default", {});
    empty.settings.gpu = "";
    problem.clear();
    expect(!makoTextWith(seed, std::nullopt, empty, problem) && problem == "the change did not read back from MAKO's file as it was written",
           "a value MAKO reads otherwise than written is refused");
    problem.clear();
    expect(!makoTextWith("version = 2\n[[profile]]\nname = \"x\"\nmultiplier = 9\n", std::nullopt, newMakoProfileFor("default", {"a"}), problem)
               && problem == "MAKO refuses its file: multiplier must be between 2 and 5",
           "a file MAKO refuses is not edited");
    const std::string inlineProfile = "version = 2\nprofile = [{ name = \"vkdaedalus default\" }]\n";
    problem.clear();
    expect(!makoTextWith(inlineProfile, std::nullopt, newMakoProfileFor("default", {"a"}), problem)
               && problem.find("is not a [[profile]] section") != std::string::npos,
           "a profile written inline is refused");
}

static void newProfiles()
{
    std::string       problem;
    const MakoProfile mine = newMakoProfileFor("default", {"Game.exe", "vkcube", "Game.exe"});
    expect(mine.name == "vkdaedalus default" && mine.activeIn == std::vector<std::string>{"/Game.exe", "/vkcube"}
               && !mine.settings.frameGenerationEnabled && mine.settings.frameGenerationProvisioned,
           "a new MAKO profile is named for the vkDaedalus profile, lists each game once as /name and starts with frame generation off but ready");
    const auto made = makoTextWith("", std::nullopt, mine, problem);
    expect(made
               && *made
                      == "version = 2\n\n[global]\n\n[[profile]]\nname = \"vkdaedalus default\"\nactive_in = [\"/Game.exe\", \"/vkcube\"]\n"
                         "frame_generation_enabled = false\n",
           "with no file yet, the file made is one MAKO reads, holding only what differs from MAKO's defaults");
    const auto added = makoTextWith(seed, std::nullopt, newMakoProfileFor("night", {"b"}), problem);
    expect(added && added->rfind(seed, 0) == 0, "a new profile is added after every other, which stay byte for byte");
    expect(makoActiveIn({"a.exe", "A.EXE"}) == std::vector<std::string>{"/a.exe", "/A.EXE"},
           "an .exe in any case is listed as /name, which MAKO matches by its mapped path");
}

static void matching()
{
    std::string    problem;
    const MakoFile file = *parseMakoFile("version = 2\ncurrent_profile = \"current\"\n"
                                         "[[profile]]\nname = \"first\"\nactive_in = [\"/bin/game\", \"/Game.exe\"]\n"
                                         "[[profile]]\nname = \"second\"\nactive_in = [\"game\", \"comm-name\"]\n"
                                         "[[profile]]\nname = \"current\"\n[[profile]]\nname = \"named\"\n[[profile]]\nname = \"fallback\"\n",
                                         problem);
    MakoProcess    native;
    native.executable = "/usr/bin/game";
    expect(makoProfileFor(file, native) == 0u,
           "a native program is matched by the end of its executable's path, the first profile in the file winning");
    MakoProcess wine;
    wine.executable     = "/proton/files/bin/wine64-preloader";
    wine.wineExecutable = "/games/Game.exe";
    wine.processName    = "Game.exe";
    expect(makoProfileFor(file, wine) == 0u, "a Wine game is matched by the .exe it maps");
    wine.wineExecutable = "/games/GAME.exe";
    expect(!makoProfileFor(file, wine), "with a case-sensitive suffix");
    MakoProcess comm;
    comm.executable  = "/usr/bin/other";
    comm.processName = "comm-name";
    expect(makoProfileFor(file, comm) == 1u, "then by the exact process name");
    comm.profileNamed = "named";
    expect(makoProfileFor(file, comm) == 3u, "MAKO_PROFILE naming a profile wins over every match");
    comm.followCurrentProfile = true;
    expect(makoProfileFor(file, comm) == 2u, "and MAKO_FOLLOW_CURRENT_PROFILE=1 with a current_profile wins over it");
    MakoProcess fallback;
    fallback.executable      = "/usr/bin/unknown";
    fallback.profileNamed    = "missing";
    fallback.fallbackProfile = "fallback";
    expect(makoProfileFor(file, fallback) == 4u, "a missing MAKO_PROFILE falls back to the matches and MAKO_PROFILE_FALLBACK comes last");
    fallback.fallbackProfile = std::nullopt;
    expect(!makoProfileFor(file, fallback), "with no match at all MAKO has no profile for the process");
    MakoProcess launcher;
    launcher.executable     = "/proton/files/bin/wine64-preloader";
    launcher.wineExecutable = "/games/UPC.EXE";
    launcher.profileNamed   = "named";
    expect(!makoProfileFor(file, launcher), "a launcher MAKO leaves alone gets no profile, even one named");
    const MakoFile anything = *parseMakoFile("version = 2\n[[profile]]\nname = \"all\"\nactive_in = \"\"\n", problem);
    expect(makoProfileFor(anything, native) == 0u, "an empty entry matches every program, as MAKO's suffix match does");

    const std::optional<MakoFile> switched =
        parseMakoFile("version = 2\n[[profile]]\nname = \"vkdaedalus a\"\n[[profile]]\nname = \"vkdaedalus b\"\nactive_in = \"/bin/game\"\n", problem);
    const std::optional<MakoFile> renamed = parseMakoFile("version = 2\n[[profile]]\nname = \"vkdaedalus c\"\nactive_in = \"/bin/game\"\n", problem);
    expect(makoProfileAfterRead(switched, native, "vkdaedalus a") == "vkdaedalus b" && makoProfileAfterRead(renamed, native, "vkdaedalus b") == "vkdaedalus c",
           "MAKO picks its profile again from every file it reads, so a profile switch or rename made while the game runs is the profile it runs");
    expect(!makoProfileAfterRead(switched, fallback, "vkdaedalus b"), "a file that matches the game no more leaves MAKO with no profile");
    expect(makoProfileAfterRead(std::nullopt, native, "vkdaedalus b") == "vkdaedalus b", "a file MAKO refuses leaves it on the profile it ran");

    unsetenv("MAKO_PROFILE");
    unsetenv("MAKO_PROFILE_FALLBACK");
    unsetenv("MAKO_FOLLOW_CURRENT_PROFILE");
    const MakoProcess self = thisMakoProcess();
    expect(self.executable.ends_with("/mako-config-test") && !self.wineExecutable && self.processName == "mako-config-tes",
           "this process is named by its executable and its kernel name, and its maps are read only under Wine");
    setenv("MAKO_PROFILE", "named", 1);
    setenv("MAKO_FOLLOW_CURRENT_PROFILE", "yes", 1);
    const MakoProcess proton =
        makoProcessOf({"/steam/reaper", "SteamLaunch", "AppId=1", "--", "/steam/proton", "waitforexitandrun", "/games/PEAK/PEAK.exe"}, "PEAK.exe");
    expect(proton.wineExecutable == "/games/PEAK/PEAK.exe" && proton.executable.empty() && proton.processName == "PEAK.exe"
               && proton.profileNamed == "named" && !proton.followCurrentProfile,
           "before a Proton game starts, the launcher sees its .exe, its name and MAKO_PROFILE, and only 1 follows the current profile");
    unsetenv("MAKO_PROFILE");
    unsetenv("MAKO_FOLLOW_CURRENT_PROFILE");
    expect(makoProcessOf({"gamemoderun", "/games/cube/vkcube"}, "vkcube").executable == "/games/cube/vkcube", "and a native game's executable path");
}

static void syncAndRename(const fs::path& dir)
{
    std::string                                           problem;
    const std::string                                     text         = "version = 2\n[[profile]]\nname = \"mine\"\nactive_in = \"Owned.exe\"\n"
                                                                         "\n[[profile]]\nname = \"vkdaedalus night\"\nactive_in = [\"/a.exe\", \"/moved\", \"/stranger\"]\n"
                                                                         "\n[[profile]]\nname = \"vkdaedalus day\"\nactive_in = \"/b\"\n";
    const auto                                            file         = parseMakoFile(text, problem);
    const std::map<std::string, std::vector<std::string>> users        = {{"night", {"a.exe", "Owned.exe"}}, {"day", {"b", "moved"}}};
    const std::vector<MakoProfile>                        changed      = file ? makoProfilesToSync(*file, users) : std::vector<MakoProfile>{};
    const auto                                            changedNamed = [&](const std::string& name) -> const MakoProfile* {
        for (const MakoProfile& profile : changed)
            if (profile.name == name)
                return &profile;
        return nullptr;
    };
    expect(changedNamed("vkdaedalus night") && changedNamed("vkdaedalus night")->activeIn == std::vector<std::string>{"/a.exe", "/stranger"},
           "a game moved to another profile leaves this one's list, a game vkDaedalus does not know stays, and one the owner's profile matches is "
           "not added");
    expect(changedNamed("vkdaedalus day") && changedNamed("vkdaedalus day")->activeIn == std::vector<std::string>{"/b", "/moved"}
               && changed.size() == 2,
           "it joins the list of the profile it runs now, and the owner's own profiles are never changed");

    const fs::path path = dir / "sync" / "conf.toml";
    expect(syncMakoActiveIn(path.string(), users, problem) && !fs::exists(path), "with no MAKO file, the sync makes none");
    writeFile(path, text);
    expect(syncMakoActiveIn(path.string(), users, problem), "the sync writes MAKO's file");
    const auto synced = parseMakoFile(contents(path), problem);
    expect(synced && makoProfilesToSync(*synced, users).empty()
               && contents(path)
                      == replacedOnce(replacedOnce(text, "[\"/a.exe\", \"/moved\", \"/stranger\"]", "[\"/a.exe\", \"/stranger\"]"),
                                      "active_in = \"/b\"",
                                      "active_in = [\"/b\", \"/moved\"]"),
           "changing only the two lists, after which a second sync changes nothing");

    const auto renamed = makoTextRenamed(text, "vkdaedalus night", "vkdaedalus evening", problem);
    expect(renamed && *renamed == replacedOnce(text, "name = \"vkdaedalus night\"", "name = \"vkdaedalus evening\""),
           "renaming a profile changes its name and not another byte");
    problem.clear();
    expect(!makoTextRenamed(text, "vkdaedalus night", "vkdaedalus day", problem) && problem == "MAKO already has a profile named 'vkdaedalus day'",
           "renaming onto a taken name is refused");
    expect(makoTextRenamed(text, "vkdaedalus gone", "vkdaedalus new", problem) == text, "renaming a profile MAKO does not have changes nothing");
    expect(renameMakoProfile(path.string(), "vkdaedalus day", "vkdaedalus dawn", problem)
               && makoProfileNamed(*parseMakoFile(contents(path), problem), "vkdaedalus dawn"),
           "a rename is saved into MAKO's file");
}

static void saving(const fs::path& dir)
{
    std::string    problem;
    const fs::path file = dir / "save" / "conf.toml";
    MakoFile       written;
    writeFile(file, seed);
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    MakoProfile mine         = *makoProfileNamed(*parseMakoFile(seed, problem), "mine");
    mine.settings.multiplier = 4;
    expect(saveMakoFile(file.string(), std::nullopt, mine, written, problem)
               && contents(file) == replacedOnce(seed, "multiplier = 3 #", "multiplier = 4 #"),
           "a profile is saved into MAKO's file with only its changed value written");
    expect((fs::status(file).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read)
               && !fs::exists(file.string() + ".vkdaedalus.tmp"),
           "the file keeps its permissions and no temporary is left");
    const std::string bad = "version = 2\n[[profile]]\nname = \"a\"\nflow_scale = 2\n";
    writeFile(file, bad);
    problem.clear();
    expect(!saveMakoFile(file.string(), std::nullopt, mine, written, problem)
               && problem == "MAKO refuses " + file.string() + ": flow_scale must be between 0.25 and 1.0" && contents(file) == bad,
           "a file MAKO refuses is left alone, with MAKO's reason");
    const fs::path target = dir / "save" / "elsewhere.toml";
    writeFile(target, seed);
    fs::remove(file);
    fs::create_symlink("elsewhere.toml", file);
    expect(saveMakoFile(file.string(), std::nullopt, mine, written, problem) && fs::is_symlink(file)
               && contents(target) == replacedOnce(seed, "multiplier = 3 #", "multiplier = 4 #"),
           "a file that is a link is written where the link points and stays a link, as MAKO's own writer does");
    fs::remove(file);
    writeFile(file, seed);
    fs::permissions(file, fs::perms::owner_read | fs::perms::group_read);
    problem.clear();
    expect(!saveMakoFile(file.string(), std::nullopt, mine, written, problem)
               && problem == file.string() + " is read-only, so the overlay leaves MAKO's file as it is" && contents(file) == seed,
           "a read-only file is refused with the reason, as MAKO's own writer refuses it");
    const fs::path fresh = dir / "fresh" / "conf.toml";
    expect(saveMakoFile(fresh.string(), std::nullopt, newMakoProfileFor("default", {"a"}), written, problem)
               && (fs::status(fresh).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write),
           "a new MAKO file is readable and writable by its owner alone, as MAKO makes it");
    fs::permissions(file, fs::perms::none);
    problem.clear();
    const bool overUnreadable = saveMakoFile(file.string(), std::nullopt, mine, written, problem);
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
    expect(!overUnreadable && problem.find("cannot read") != std::string::npos && contents(file) == seed,
           "a file that cannot be read is refused, not replaced");
    const MakoRead missing = readMakoFile((dir / "nowhere.toml").string());
    expect(!missing.exists && missing.file && missing.file->profiles.empty() && missing.problem.empty(),
           "a missing file reads as one with no profile");
}

static void paths(const fs::path& dir)
{
    for (const char* name : {"MAKO_CONFIG", "XDG_CONFIG_HOME", "HOME"})
        unsetenv(name);
    expect(makoConfigPath() == "/etc/mako-render/conf.toml", "with nothing set MAKO reads the system file");
    setenv("HOME", "/home/a", 1);
    expect(makoConfigPath() == "/home/a/.config/mako-render/conf.toml", "then the home's .config");
    setenv("XDG_CONFIG_HOME", "", 1);
    expect(makoConfigPath() == "/home/a/.config/mako-render/conf.toml", "an empty XDG_CONFIG_HOME counts as unset");
    setenv("XDG_CONFIG_HOME", "/config", 1);
    expect(makoConfigPath() == "/config/mako-render/conf.toml", "then XDG_CONFIG_HOME");
    setenv("MAKO_CONFIG", "/named.toml", 1);
    expect(makoConfigPath() == "/named.toml", "and MAKO_CONFIG over everything");
    unsetenv("MAKO_CONFIG");
    unsetenv("XDG_CONFIG_HOME");

    const fs::path home = dir / "home";
    const fs::path work = dir / "work";
    fs::create_directories(work);
    fs::current_path(work);
    setenv("HOME", home.c_str(), 1);
    unsetenv("XDG_DATA_HOME");
    expect(!makoDllPath({}), "with no Lossless.dll where MAKO looks, none is found");
    writeFile(work / "Lossless.dll", "dll");
    expect(makoDllPath({}) == (work / "Lossless.dll").string(), "the working folder is the last place MAKO looks");
    writeFile(home / ".steam/steam/steamapps/common/Lossless Scaling/Lossless.dll", "dll");
    expect(makoDllPath({}) == (home / ".steam/steam/steamapps/common/Lossless Scaling/Lossless.dll").string(),
           "a Steam folder in the home comes before it");
    writeFile(dir / "data/Steam/steamapps/common/Lossless Scaling/Lossless.dll", "dll");
    setenv("XDG_DATA_HOME", (dir / "data").c_str(), 1);
    expect(makoDllPath({}) == (dir / "data/Steam/steamapps/common/Lossless Scaling/Lossless.dll").string(), "and XDG_DATA_HOME's Steam before that");
    MakoGlobal named;
    named.dll = "~/elsewhere/Lossless.dll";
    expect(makoDllPath(named) == "~/elsewhere/Lossless.dll" && !makoDllStatus(named).found,
           "a DLL named in MAKO's file wins as written, found or not");
    unsetenv("XDG_DATA_HOME");

    const fs::path library = dir / "games/SteamLibrary";
    writeFile(library / "steamapps/common/Lossless Scaling/Lossless.dll", "dll");
    writeFile(home / ".local/share/Steam/steamapps/libraryfolders.vdf",
              "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + library.string() + "\"\n\t}\n}\n");
    expect(makoDllStatus(named).inSteamLibrary == (library / "steamapps/common/Lossless Scaling/Lossless.dll").string(),
           "Lossless.dll in a Steam library outside the home is found, where MAKO itself does not look");
}

static void powerSupplies(const fs::path& dir)
{
    const auto supply = [&](const std::string& tree, const std::string& entry, const std::map<std::string, std::string>& files) {
        fs::create_directories(dir / tree / entry);
        for (const auto& [name, value] : files)
            writeFile(dir / tree / entry / name, value + "\n");
    };
    supply("plugged", "AC", {{"type", "Mains"}, {"online", "1"}});
    supply("plugged", "BAT0", {{"type", "Battery"}, {"present", "1"}});
    supply("battery", "AC", {{"type", "Mains"}, {"online", "0"}});
    supply("battery", "BAT0", {{"type", "Battery"}, {"present", "1"}});
    fs::create_directories(dir / "desktop");
    supply("incomplete", "BAT0", {{"type", "Battery"}, {"present", "1"}});
    supply("incomplete", "ucsi-source-psy", {{"type", "USB"}});
    supply("device", "AC", {{"type", "Mains"}, {"online", "0"}});
    supply("device", "hidpp_battery_0", {{"type", "Battery"}, {"present", "1"}, {"scope", "Device"}});
    supply("untyped", "AC", {{"type", "Mains"}, {"online", "0"}});
    supply("untyped", "BAT0", {{"type", "Battery"}, {"present", "1"}});
    supply("untyped", "odd", {});
    supply("plugged-incomplete", "AC", {{"type", "Mains"}, {"online", "1"}});
    supply("plugged-incomplete", "USB", {{"type", "USB_C"}});
    for (int i = 0; i < 65; i++)
        supply("crowded", "AC" + std::to_string(i), {{"type", "Mains"}, {"online", "1"}});

    const auto source = [&](const std::string& tree) { return readPowerSupplies(dir / tree).source; };
    expect(source("plugged") == MakoPowerSource::Docked && readPowerSupplies(dir / "plugged").battery,
           "a laptop plugged in is docked to MAKO, with a battery");
    expect(source("battery") == MakoPowerSource::Handheld, "unplugged it is handheld");
    expect(source("desktop") == MakoPowerSource::Unknown && !readPowerSupplies(dir / "desktop").battery,
           "a desktop with no supply is unknown, with no battery");
    expect(source("nowhere") == MakoPowerSource::Unknown, "so is a missing folder");
    expect(source("incomplete") == MakoPowerSource::Unknown, "a supply that does not say whether it is online leaves the source unknown");
    expect(source("device") == MakoPowerSource::Unknown && !readPowerSupplies(dir / "device").battery, "a mouse's battery is not the machine's");
    expect(source("untyped") == MakoPowerSource::Unknown, "an entry with no type leaves it unknown");
    expect(source("plugged-incomplete") == MakoPowerSource::Docked, "one supply online is enough for docked");
    expect(source("crowded") == MakoPowerSource::Unknown, "more than 64 entries are not read");
}

static VkResult listsBoth(uint32_t* count, VkLayerProperties* layers)
{
    if (layers != nullptr)
    {
        std::snprintf(layers[0].layerName, VK_MAX_EXTENSION_NAME_SIZE, "%s", "VK_LAYER_LSFGVK_frame_generation");
        std::snprintf(layers[1].layerName, VK_MAX_EXTENSION_NAME_SIZE, "%s", "VK_LAYER_MAKO_render");
    }
    *count = 2;
    return VK_SUCCESS;
}

static VkResult listsLsfgVk(uint32_t* count, VkLayerProperties* layers)
{
    if (layers != nullptr)
        std::snprintf(layers[0].layerName, VK_MAX_EXTENSION_NAME_SIZE, "%s", "VK_LAYER_LSFGVK_frame_generation");
    *count = 1;
    return VK_SUCCESS;
}

static VkResult listsNothing(uint32_t*, VkLayerProperties*)
{
    return VK_ERROR_INITIALIZATION_FAILED;
}

static void detection()
{
    expect(makoLayerListedBy(listsBoth) == true && makoLayerListedBy(listsLsfgVk) == false,
           "MAKO is installed when the game's loader lists VK_LAYER_MAKO_render");
    expect(!makoLayerListedBy(listsNothing) && !makoLayerListedBy(nullptr), "a loader that cannot list its layers leaves it unknown");
    expect(!makoLayerLoaded() && loadedFrameGenerationLayerName() == "lsfg-vk",
           "MAKO is not loaded in a process that never mapped its library, so the log names lsfg-vk");
    unsetenv("MAKO_ENV");
    expect(!makoSettingsFromEnvironment(), "without MAKO_ENV MAKO reads its file");
    setenv("MAKO_ENV", "", 1);
    expect(makoSettingsFromEnvironment(), "with MAKO_ENV set at all MAKO takes its settings from the environment, as MAKO checks");
    unsetenv("MAKO_ENV");
}

static void polls()
{
    using std::chrono::milliseconds;
    const std::chrono::steady_clock::time_point start{std::chrono::hours(1)};
    Poll                                        file(makoFilePollInterval);
    expect(file.due(start), "a reader with the overlay closed reads MAKO's file");
    expect(!file.due(start + milliseconds(249)) && file.due(start + milliseconds(250)), "and again no sooner than MAKO looks at it, every 250 ms");
    expect(!file.due(start + milliseconds(499)) && file.due(start + milliseconds(500)), "counted from the last look");
    Poll power(makoPowerPollInterval);
    expect(power.due(start) && !power.due(start + milliseconds(1999)) && power.due(start + milliseconds(2000)), "and the power source every 2 s, as MAKO does");
    Poll lsfg(lsfgFilePollInterval);
    expect(lsfg.due(start) && !lsfg.due(start + milliseconds(499)) && lsfg.due(start + milliseconds(500)), "lsfg-vk's file every half second");
}

static void overlayFacts(const fs::path& dir)
{
    MakoProfile plain;
    plain.settings.targetFps   = 90;
    const MakoProfile separate = makoProfileWithPowerSets(plain, true, MakoPowerSource::Docked);
    expect(separate.power && separate.power->handheld == plain.settings && separate.power->docked == plain.settings && separate.settings == plain.settings,
           "separate settings on battery start as two copies of the profile's own, as MAKO needs both tables");
    MakoProfile tuned            = separate;
    tuned.power->handheld.targetFps = 40;
    tuned.power->docked.targetFps   = 144;
    expect(makoProfileWithPowerSets(tuned, false, MakoPowerSource::Handheld).settings.targetFps == 40
               && !makoProfileWithPowerSets(tuned, false, MakoPowerSource::Handheld).power
               && makoProfileWithPowerSets(tuned, false, MakoPowerSource::Docked).settings.targetFps == 144
               && makoProfileWithPowerSets(tuned, false, MakoPowerSource::Unknown).settings.targetFps == 90,
           "folding the sets back keeps the set MAKO runs now, the profile's own when the power source is unknown");
    expect(makoProfileWithPowerSets(tuned, true, MakoPowerSource::Docked) == tuned && makoProfileWithPowerSets(plain, false, MakoPowerSource::Docked) == plain,
           "a profile already as asked is left as it is");
    const MakoProfile off = makoProfileSwitched(tuned, MakoPowerSource::Docked, &MakoSettings::frameGenerationEnabled, false);
    expect(!off.power->docked.frameGenerationEnabled && off.power->handheld.frameGenerationEnabled && off.settings.frameGenerationEnabled,
           "the frame generation key and switch act on the set MAKO runs now, plugged in here, whichever tab the card edits");
    expect(!makoProfileSwitched(tuned, MakoPowerSource::Unknown, &MakoSettings::scalingEnabled, true).power->handheld.scalingEnabled
               && makoProfileSwitched(tuned, MakoPowerSource::Unknown, &MakoSettings::scalingEnabled, true).settings.scalingEnabled
               && makoProfileSwitched(plain, MakoPowerSource::Handheld, &MakoSettings::scalingEnabled, true).settings.scalingEnabled,
           "the profile's own values while the power source is unknown or the profile has no power sets");

    expect(frameGenerationTabs.size() == 3 && frameGenerationTabs[0].shown == "lsfg-vk" && frameGenerationTabs[1].shown == "MAKO LSFG"
               && frameGenerationTabs[2].shown == "MAKO FG",
           "the card's layer choice is lsfg-vk, MAKO LSFG and MAKO FG, in that order");
    expect(frameGenerationTabs[0].layer == FrameGenerationLayer::LsfgVk && frameGenerationTabs[0].makoModel.empty()
               && frameGenerationTabs[1].layer == FrameGenerationLayer::Mako && frameGenerationTabs[1].makoModel == "lossless-scaling"
               && frameGenerationTabs[2].layer == FrameGenerationLayer::Mako && frameGenerationTabs[2].makoModel == "mako-fg",
           "lsfg-vk writes the profile's layer alone, and each MAKO choice writes the layer mako with its frame_generation_model");
    for (const FrameGenerationTab& tab : frameGenerationTabs)
        if (tab.layer == FrameGenerationLayer::Mako)
            expect(std::any_of(makoFrameGenerationModels.begin(), makoFrameGenerationModels.end(),
                               [&](const FrameGenerationChoice& model) { return model.name == tab.makoModel; }),
                   "the model " + std::string(tab.makoModel) + " a MAKO choice writes is one MAKO's file takes");
    expect(&frameGenerationTabOf(FrameGenerationLayer::LsfgVk, "lossless-scaling") == &frameGenerationTabs[0]
               && &frameGenerationTabOf(FrameGenerationLayer::Mako, "lossless-scaling") == &frameGenerationTabs[1]
               && &frameGenerationTabOf(FrameGenerationLayer::Mako, "mako-fg") == &frameGenerationTabs[2],
           "a profile shows the choice its layer and MAKO's model name, and MAKO's model counts for nothing while it runs lsfg-vk");
    const MakoProfile lossless = makoProfileWithModel(tuned, MakoPowerSource::Handheld, "lossless-scaling");
    expect(lossless.power->handheld.frameGenerationModel == "lossless-scaling" && lossless.power->docked.frameGenerationModel == "mako-fg"
               && lossless.settings.frameGenerationModel == "mako-fg",
           "a MAKO choice writes its model into the power set the card edits and leaves the others");
    expect(makoProfileWithModel(plain, MakoPowerSource::Handheld, "lossless-scaling").settings.frameGenerationModel == "lossless-scaling"
               && makoProfileWithModel(tuned, MakoPowerSource::Unknown, "lossless-scaling").settings.frameGenerationModel == "lossless-scaling",
           "and into the profile's own values when it has no power sets or the card edits those");
    expect(frameGenerationTabWaits(frameGenerationTabs[1], FrameGenerationLayer::LsfgVk, "lossless-scaling")
               && frameGenerationTabWaits(frameGenerationTabs[0], FrameGenerationLayer::Mako, "mako-fg"),
           "a choice of the layer that is not loaded waits for the next launch");
    expect(frameGenerationTabWaits(frameGenerationTabs[1], FrameGenerationLayer::Mako, "mako-fg")
               && frameGenerationTabWaits(frameGenerationTabs[2], FrameGenerationLayer::Mako, "lossless-scaling"),
           "and so does a choice of the model MAKO did not start with");
    expect(!frameGenerationTabWaits(frameGenerationTabs[1], FrameGenerationLayer::Mako, "lossless-scaling")
               && !frameGenerationTabWaits(frameGenerationTabs[2], FrameGenerationLayer::Mako, "mako-fg")
               && !frameGenerationTabWaits(frameGenerationTabs[0], FrameGenerationLayer::LsfgVk, "lossless-scaling")
               && !frameGenerationTabWaits(frameGenerationTabs[0], std::nullopt, "mako-fg"),
           "the choice this run started with waits for nothing, whatever MAKO's model is while lsfg-vk runs");

    MakoGpu game{"AMD Radeon RX 9070 XT", 0x1002, 0x7550, PciAddress{0, 3, 0, 0}};
    expect(!makoGpuNamesAnother("AMD Radeon RX 9070 XT", game) && !makoGpuNamesAnother("0x1002:0x7550", game) && !makoGpuNamesAnother("3:0.0", game),
           "MAKO's gpu naming the game's GPU by name, by MAKO's vendor:device text or by its bus:device.function is the game's");
    expect(makoGpuNamesAnother("NVIDIA GeForce GTX 1660 SUPER", game) && makoGpuNamesAnother("0x10DE:0x21C4", game) && makoGpuNamesAnother("4:0.0", game)
               && makoGpuNamesAnother("0x1002:0x7551", game) && makoGpuNamesAnother("1002:7550", game),
           "a name, an id pair or an address that is not the game's names another GPU, compared exactly as MAKO compares");
    game.pci.reset();
    expect(!makoGpuNamesAnother("4:0.0", game) && makoGpuNamesAnother("4:0.0x", game),
           "an address cannot be told from the game's when the game's GPU reports none, so it is not called another GPU");

    unsetenv("GAMESCOPE_WAYLAND_DISPLAY");
    const bool outside = underGamescope();
    setenv("GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0", 1);
    expect(!outside && underGamescope(), "a game is under gamescope when gamescope names its Wayland display");
    unsetenv("GAMESCOPE_WAYLAND_DISPLAY");

#if defined(__x86_64__)
    const fs::path host = dir / "mako-fg-host";
    writeFile(host, "");
    setenv("MAKO_FG_HOST", host.c_str(), 1);
    const std::optional<std::string> present = makoFgMissing();
    setenv("MAKO_FG_HOST", (dir / "no-host").c_str(), 1);
    expect(!present && makoFgMissing() == "MAKO FG is not installed: mako-fg-host is missing",
           "MAKO FG is there when the mako-fg-host MAKO would start exists, and missing with MAKO's own words when it does not");
    unsetenv("MAKO_FG_HOST");
#else
    static_cast<void>(dir);
    expect(makoFgMissing() == "MAKO FG is published for x86-64 only", "MAKO FG is missing on any machine but x86-64, for which alone it is published");
#endif
}

static void greyedControls()
{
    MakoSettings followed;
    followed.adaptive                     = true;
    followed.adaptiveTargetRefreshRate    = true;
    MakoSettings fixedTarget              = followed;
    fixedTarget.adaptiveTargetRefreshRate = false;

    const MakoGreyedWhy underGamescopeFollowed = makoGreyedWhy(followed, true);
    expect(!underGamescopeFollowed.target.empty() && underGamescopeFollowed.target.find("fallback") != std::string::npos
               && underGamescopeFollowed.displayRefresh.empty(),
           "under gamescope with Display refresh on, Target is greyed as the fallback and Display refresh stays editable");
    const MakoGreyedWhy underGamescopeTarget = makoGreyedWhy(fixedTarget, true);
    expect(underGamescopeTarget.target.empty() && underGamescopeTarget.displayRefresh.empty(),
           "under gamescope with Display refresh off, both stay editable");
    for (const MakoSettings& set : {followed, fixedTarget})
    {
        const MakoGreyedWhy outside = makoGreyedWhy(set, false);
        expect(outside.target.empty() && outside.displayRefresh.find("gamescope") != std::string::npos,
               "outside gamescope Display refresh is greyed as working only under gamescope and Target stays editable, whatever the file says");
    }
}

static void nextLaunchChanges()
{
    const MakoSettings started;
    expect(makoNextLaunchChanges(started, started).empty(), "a profile as it was at the start waits for nothing");
    MakoSettings named = started;
    named.gpu          = "Another GPU";
    expect(makoNextLaunchChanges(named, started) == std::vector<std::string>{"GPU"} && makoNextLaunchChanges(started, named) == std::vector<std::string>{"GPU"},
           "MAKO reads its GPU once per process, so naming one or clearing it waits for the next start");
    MakoSettings every                     = named;
    every.frameGenerationProvisioned       = !started.frameGenerationProvisioned;
    every.ultraPerformance                 = !started.ultraPerformance;
    every.swapchainImageCountCompatibility = !started.swapchainImageCountCompatibility;
    every.mailboxPresentationCompatibility = !started.mailboxPresentationCompatibility;
    every.multiplier                       = started.multiplier + 1;
    expect(makoNextLaunchChanges(every, started)
               == std::vector<std::string>{"Ready at start", "Ultra performance", "Keep swapchain image count", "Mailbox compatibility", "GPU"},
           "each process-start setting changed is named by its row, and a live one such as the multiplier is not");
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    const fs::path dir = fs::weakly_canonical(fs::absolute(argv[1]));
    fs::remove_all(dir);
    fs::create_directories(dir);
    refusals();
    quirks();
    everyKey();
    powerSets();
    globals();
    editorRefusals();
    newProfiles();
    matching();
    syncAndRename(dir);
    saving(dir);
    powerSupplies(dir);
    detection();
    paths(dir);
    overlayFacts(dir);
    polls();
    greyedControls();
    nextLaunchChanges();
    if (failures)
        std::printf("%d MAKO file check(s) failed\n", failures);
    return failures ? 1 : 0;
}
