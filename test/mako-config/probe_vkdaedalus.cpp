#include "mako_config.hpp"
#include "mako_status.hpp"
#include "prefix_holder.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace vkBasalt;

namespace
{
    const char* yesNo(bool value)
    {
        return value ? "true" : "false";
    }

    void print(const char* set, const MakoSettings& settings)
    {
        const auto line = [&](const char* key, const std::string& value) { std::printf("%s%s %s\n", set, key, value.c_str()); };
        const auto real = [](float value) {
            char text[64];
            std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
            return std::string(text);
        };
        line("gpu", settings.gpu.value_or("unset"));
        line("multiplier", std::to_string(settings.multiplier));
        line("frame_generation_provisioned", yesNo(settings.frameGenerationProvisioned));
        line("frame_generation_enabled", yesNo(settings.frameGenerationEnabled));
        line("frame_generation_model", settings.frameGenerationModel);
        line("mako_fg_mode", settings.makoFgMode);
        line("scaling_enabled", yesNo(settings.scalingEnabled));
        line("swapchain_image_count_compatibility", yesNo(settings.swapchainImageCountCompatibility));
        line("mailbox_presentation_compatibility", yesNo(settings.mailboxPresentationCompatibility));
        line("scaling_method", settings.scalingMethod);
        line("scaling_factor", real(settings.scalingFactor));
        line("scaling_supersampling", yesNo(settings.scalingSupersampling));
        line("scaling_sharpness", real(settings.scalingSharpness));
        line("hdr_quality", settings.hdrQuality);
        line("frame_generation_refresh_threshold", std::to_string(settings.frameGenerationRefreshThreshold));
        line("base_fps_cap", std::to_string(settings.baseFpsCap));
        line("gpu_headroom_percent", std::to_string(settings.gpuHeadroomPercent));
        line("adaptive", yesNo(settings.adaptive));
        line("adaptive_auto_base_fps_cap", yesNo(settings.adaptiveAutoBaseFpsCap));
        line("adaptive_fractional_real_frame_priority", settings.adaptiveFractionalRealFramePriority);
        line("target_fps", std::to_string(settings.targetFps));
        line("adaptive_target_refresh_rate", yesNo(settings.adaptiveTargetRefreshRate));
        line("adaptive_max_multiplier", std::to_string(settings.adaptiveMaxMultiplier));
        line("adaptive_stable_cadence", yesNo(settings.adaptiveStableCadence));
        line("gamescope_vrr_mode", settings.gamescopeVrrMode);
        line("dynamic_cadence_recovery", yesNo(settings.dynamicCadenceRecovery));
        line("dynamic_cadence_probe_interval_seconds", real(settings.dynamicCadenceProbeIntervalSeconds));
        line("ultra_performance", yesNo(settings.ultraPerformance));
        line("flow_scale", real(settings.flowScale));
        line("performance_mode", yesNo(settings.performanceMode));
        line("pacing", settings.pacing);
    }

    void print(const MakoFile& file)
    {
        std::printf("current_profile %s\n", file.currentProfile.value_or("unset").c_str());
        std::printf("dll %s\n", file.global.dll.value_or("unset").c_str());
        std::printf("allow_fp16 %s\n", yesNo(file.global.allowHalfPrecision));
        for (const MakoProfile& profile : file.profiles)
        {
            std::printf("profile %s\n", profile.name.c_str());
            for (const std::string& game : profile.activeIn)
                std::printf("active_in %s\n", game.c_str());
            print("", makoSettingsInEffect(profile.settings));
            std::printf("power %s\n", yesNo(profile.power.has_value()));
            if (profile.power)
            {
                print("handheld ", makoSettingsInEffect(profile.power->handheld));
                print("docked ", makoSettingsInEffect(profile.power->docked));
            }
        }
    }

    template<size_t N>
    void printNames(const char* kind, const std::array<FrameGenerationChoice, N>& choices)
    {
        std::printf("%s", kind);
        for (const FrameGenerationChoice& choice : choices)
            std::printf(" %.*s", static_cast<int>(choice.name.size()), choice.name.data());
        std::printf("\n");
    }

    bool mapped(const char* path)
    {
        const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
        if (descriptor < 0)
            return false;
        const void* at = mmap(nullptr, 1, PROT_READ, MAP_PRIVATE, descriptor, 0);
        close(descriptor);
        return at != MAP_FAILED;
    }

    std::vector<MakoProfile> editsToWrite()
    {
        const std::vector<std::string>              names = {"plain",
                                                             "vkdaedalus Default",
                                                             "quote \" and back\\slash",
                                                             "\xc3\x9cn\xc3\xaf"
                                                                          "code \xe6\xb8\xb8\xe6\x88\x8f"};
        const std::vector<std::vector<std::string>> games = {
            {}, {"/usr/bin/game"}, {"Game.EXE", "123456", "/opt/a b/run"}, {"C:\\Games\\Odd \"Name\".exe"}, {"\xe6\xb8\xb8\xe6\x88\x8f.exe"}};
        const std::vector<std::optional<std::string>> gpus        = {std::nullopt, "AMD Radeon RX 9070 XT", "1002:7550", "pci-0000:03:00.0"};
        const std::vector<uint64_t>                   multipliers = {2, 3, 4, 5};
        const std::vector<float>                      factors     = {1.0f, 1.25f, 1.5f, 2.0f, 1.0f + 1.0f / 3.0f};
        const std::vector<float>                      sharpness   = {0.0f, 1.0f / 3.0f, 0.8f, 1.0f};
        const std::vector<uint32_t>                   thresholds  = {0, 1, 60, 1000};
        const std::vector<uint32_t>                   caps        = {0, 1, 30, 1000};
        const std::vector<uint32_t>                   targets     = {10, 60, 120, 1000};
        const std::vector<float>                      intervals   = {0.1f, 0.25f, 2.0f, 3.0f, 0.7f};
        const std::vector<float>                      flowScales  = {0.25f, 0.5f, 0.85f, 1.0f, 1.0f / 3.0f};
        std::vector<MakoProfile>                      edits;
        for (size_t i = 0; i < 24; i++)
        {
            MakoProfile   profile;
            MakoSettings& s                       = profile.settings;
            profile.name                          = names[i % names.size()];
            profile.activeIn                      = games[i % games.size()];
            s.gpu                                 = gpus[i % gpus.size()];
            s.multiplier                          = multipliers[i % multipliers.size()];
            s.frameGenerationProvisioned          = (i & 1) == 0;
            s.frameGenerationEnabled              = (i & 2) == 0;
            s.frameGenerationModel                = std::string(makoFrameGenerationModels[i % makoFrameGenerationModels.size()].name);
            s.makoFgMode                          = std::string(makoFgModes[i % makoFgModes.size()].name);
            s.scalingEnabled                      = (i & 4) != 0;
            s.swapchainImageCountCompatibility    = (i & 8) != 0;
            s.mailboxPresentationCompatibility    = (i & 16) != 0;
            s.scalingMethod                       = std::string(makoScalingMethods[(i / 2) % makoScalingMethods.size()].name);
            s.scalingFactor                       = factors[i % factors.size()];
            s.scalingSupersampling                = (i & 1) != 0;
            s.scalingSharpness                    = sharpness[i % sharpness.size()];
            s.hdrQuality                          = std::string(makoHdrQualities[i % makoHdrQualities.size()].name);
            s.frameGenerationRefreshThreshold     = thresholds[i % thresholds.size()];
            s.baseFpsCap                          = caps[(i / 2) % caps.size()];
            s.gpuHeadroomPercent                  = MakoLimits::gpuHeadroomPercents[i % MakoLimits::gpuHeadroomPercents.size()];
            s.adaptive                            = (i & 2) != 0;
            s.adaptiveAutoBaseFpsCap              = (i & 4) == 0;
            s.adaptiveFractionalRealFramePriority = std::string(makoRealFramePriorities[i % makoRealFramePriorities.size()].name);
            s.targetFps                           = targets[i % targets.size()];
            s.adaptiveTargetRefreshRate           = (i & 8) == 0;
            s.adaptiveMaxMultiplier               = multipliers[(i + 1) % multipliers.size()];
            s.adaptiveStableCadence               = (i & 16) == 0;
            s.gamescopeVrrMode                    = std::string(makoGamescopeVrrModes[i % makoGamescopeVrrModes.size()].name);
            s.dynamicCadenceRecovery              = i % 5 == 0;
            s.dynamicCadenceProbeIntervalSeconds  = intervals[i % intervals.size()];
            s.ultraPerformance                    = (i & 4) == 0;
            s.flowScale                           = flowScales[i % flowScales.size()];
            s.performanceMode                     = (i & 1) != 0;
            if (i % 3 != 0)
            {
                MakoPowerSets sets{s, s};
                sets.handheld.targetFps            = targets[(i + 1) % targets.size()];
                sets.handheld.flowScale            = flowScales[(i + 2) % flowScales.size()];
                sets.handheld.makoFgMode           = std::string(makoFgModes[(i + 1) % makoFgModes.size()].name);
                sets.docked.multiplier             = multipliers[(i + 2) % multipliers.size()];
                sets.docked.gpu                    = gpus[(i + 1) % gpus.size()];
                sets.docked.dynamicCadenceRecovery = !s.dynamicCadenceRecovery;
                profile.power                      = sets;
            }
            edits.push_back(profile);
        }
        return edits;
    }

    MakoGlobal globalToWrite(size_t i)
    {
        const std::vector<std::optional<std::string>> dlls = {std::nullopt, "/games/Lossless Scaling/Lossless.dll", "~/odd \"path\"/Lossless.dll"};
        MakoGlobal                                    global;
        global.dll                = dlls[i % dlls.size()];
        global.allowHalfPrecision = (i & 4) != 0;
        return global;
    }

    int write(const char* seedPath, const std::filesystem::path& folder)
    {
        std::ifstream     seedFile(seedPath, std::ios::binary);
        const std::string seed((std::istreambuf_iterator<char>(seedFile)), std::istreambuf_iterator<char>());
        if (!seedFile.good() && !seedFile.eof())
        {
            std::fprintf(stderr, "cannot read the seed %s\n", seedPath);
            return 1;
        }
        const std::vector<MakoProfile> edits = editsToWrite();
        for (size_t i = 0; i < edits.size(); i++)
        {
            std::string                      problem;
            const std::optional<std::string> text =
                makoTextWith(seed, i % 3 == 0 ? std::nullopt : std::optional(globalToWrite(i)), edits[i], problem);
            if (!text)
            {
                std::fprintf(stderr, "the editor refused edit %zu on %s: %s\n", i, seedPath, problem.c_str());
                return 1;
            }
            std::ofstream out(folder / (std::to_string(i) + ".toml"), std::ios::binary);
            out << *text;
            if (!out.flush())
            {
                std::fprintf(stderr, "cannot write edit %zu\n", i);
                return 1;
            }
        }
        return 0;
    }

    std::optional<MakoPowerSource> sourceNamed(const std::string& name)
    {
        if (name == "handheld")
            return MakoPowerSource::Handheld;
        if (name == "docked")
            return MakoPowerSource::Docked;
        if (name == "unknown")
            return MakoPowerSource::Unknown;
        return std::nullopt;
    }

    const char* sourceName(MakoPowerSource source)
    {
        switch (source)
        {
            case MakoPowerSource::Handheld: return "handheld";
            case MakoPowerSource::Docked: return "docked";
            case MakoPowerSource::Unknown: break;
        }
        return "unknown";
    }
}

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 3 && args[0] == "write")
        return write(args[1].c_str(), args[2]);
    if (args.size() == 2 && args[0] == "read")
    {
        const MakoRead read = readMakoFile(args[1]);
        if (read.file && read.exists)
            print(*read.file);
        else
            std::printf("refused\n%s\n", read.problem.c_str());
        return 0;
    }
    if (args.size() == 3 && args[0] == "read-as" && sourceNamed(args[2]))
    {
        const MakoRead read = readMakoFile(args[1]);
        if (!read.file || !read.exists)
        {
            std::fprintf(stderr, "refused %s: %s\n", args[1].c_str(), read.problem.c_str());
            return 1;
        }
        for (const MakoProfile& profile : read.file->profiles)
        {
            std::printf("profile %s\n", profile.name.c_str());
            print("", makoSettingsInEffect(makoSettingsFor(profile, *sourceNamed(args[2]))));
        }
        return 0;
    }
    if (args.size() == 1 && args[0] == "path")
    {
        std::printf("%s\n", makoConfigPath().c_str());
        return 0;
    }
    if ((args.size() == 2 || args.size() == 3) && args[0] == "identify")
    {
        if (args.size() == 3 && !mapped(args[2].c_str()))
        {
            std::fprintf(stderr, "cannot map %s\n", args[2].c_str());
            return 1;
        }
        const MakoRead read = readMakoFile(args[1]);
        if (!read.file || !read.exists)
        {
            std::fprintf(stderr, "refused %s: %s\n", args[1].c_str(), read.problem.c_str());
            return 1;
        }
        const std::optional<size_t> chosen = makoProfileFor(*read.file, thisMakoProcess());
        std::printf("%s\n", chosen ? read.file->profiles[*chosen].name.c_str() : "none");
        return 0;
    }
    if (args.size() == 1 && args[0] == "dll")
    {
        const MakoRead read = readMakoFile(makoConfigPath());
        if (!read.file || !read.exists)
        {
            std::fprintf(stderr, "refused %s: %s\n", makoConfigPath().c_str(), read.problem.c_str());
            return 1;
        }
        std::printf("%s\n", makoDllPath(read.file->global).value_or("none").c_str());
        return 0;
    }
    if (args.size() == 4 && args[0] == "status")
    {
        std::ifstream                 stat("/proc/" + args[1] + "/stat", std::ios::binary);
        const std::string             line((std::istreambuf_iterator<char>(stat)), {});
        const std::optional<uint64_t> started = processStartTicks(line);
        if (!started)
        {
            std::fprintf(stderr, "cannot read the start time of process %s\n", args[1].c_str());
            return 1;
        }
        const MakoStatusRead read = readMakoStatus(makoStatusFolder(makoConfigPath()), std::stoull(args[1]), *started, static_cast<uint32_t>(std::stoul(args[2])),
                                                   static_cast<uint32_t>(std::stoul(args[3])));
        if (!read.problem.empty())
            std::printf("problem %s\n", read.problem.c_str());
        if (read.ofSwapchain.size() != 1)
            std::printf("%zu status files of this swapchain\n", read.ofSwapchain.size());
        for (const MakoStatus& status : read.ofSwapchain)
            std::printf("role %s\ncontext %llu\napplied_model %s\ngeneration_active %s\ngeneration_unavailable %s\napplied_scaling %s\nactive %s\n"
                        "inactive_reason %s\nsource %ux%u\npresentation %ux%u\nmethod %s\n",
                        status.role.c_str(), static_cast<unsigned long long>(status.context), status.appliedModel.c_str(), yesNo(status.generationActive),
                        status.generationUnavailableReason ? status.generationUnavailableReason->c_str() : "-", yesNo(status.appliedScalingEnabled),
                        yesNo(status.scalingActive), status.scalingInactiveReason ? status.scalingInactiveReason->c_str() : "-", status.sourceWidth,
                        status.sourceHeight, status.presentationWidth, status.presentationHeight, status.scalingMethod.c_str());
        return 0;
    }
    if (args.size() == 2 && args[0] == "power")
    {
        std::printf("%s\n", sourceName(readPowerSupplies(args[1]).source));
        return 0;
    }
    if (args.size() == 1 && args[0] == "names")
    {
        printNames("frame_generation_model", makoFrameGenerationModels);
        printNames("mako_fg_mode", makoFgModes);
        printNames("scaling_method", makoScalingMethods);
        printNames("hdr_quality", makoHdrQualities);
        printNames("adaptive_fractional_real_frame_priority", makoRealFramePriorities);
        printNames("gamescope_vrr_mode", makoGamescopeVrrModes);
        return 0;
    }
    if (args.size() == 1 && args[0] == "limits")
    {
        using L = MakoLimits;
        std::printf(
            "multiplier %llu %llu\n", static_cast<unsigned long long>(L::lowestMultiplier), static_cast<unsigned long long>(L::highestMultiplier));
        std::printf("scaling_factor %.9g %.9g\n", static_cast<double>(L::lowestScalingFactor), static_cast<double>(L::highestScalingFactor));
        std::printf("scaling_sharpness %.9g %.9g\n", static_cast<double>(L::lowestScalingSharpness), static_cast<double>(L::highestScalingSharpness));
        std::printf("gpu_headroom_percent");
        for (const uint32_t percent : L::gpuHeadroomPercents)
            std::printf(" %u", percent);
        std::printf("\nbase_fps_cap 0 %u\n", L::highestBaseFpsCap);
        std::printf("frame_generation_refresh_threshold 0 %u\n", L::highestRefreshThreshold);
        std::printf("target_fps %u %u\n", L::lowestTargetFps, L::highestTargetFps);
        std::printf("adaptive_max_multiplier %llu %llu\n",
                    static_cast<unsigned long long>(L::lowestAdaptiveMaxMultiplier),
                    static_cast<unsigned long long>(L::highestAdaptiveMaxMultiplier));
        std::printf("dynamic_cadence_probe_interval_seconds %.9g %.9g\n",
                    static_cast<double>(L::lowestProbeIntervalSeconds),
                    static_cast<double>(L::highestProbeIntervalSeconds));
        std::printf("flow_scale %.9g %.9g\n", static_cast<double>(L::lowestFlowScale), static_cast<double>(L::highestFlowScale));
        return 0;
    }
    std::fprintf(stderr,
                 "usage: write <seed> <folder> | read <file> | read-as <file> <handheld|docked|unknown> | path | identify <file> [exe] | dll | "
                 "power <folder> | status <process id> <width> <height> | names | limits\n");
    return 2;
}
