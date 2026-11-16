#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/detection.hpp"
#include "mako-common/helpers/paths.hpp"
#include "runtime_status.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace ls;

namespace
{
    const char* yesNo(bool value)
    {
        return value ? "true" : "false";
    }

    void print(const char* set, const GameConf& conf)
    {
        const auto line = [&](const char* key, const std::string& value) { std::printf("%s%s %s\n", set, key, value.c_str()); };
        const auto real = [](float value) {
            char text[64];
            std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
            return std::string(text);
        };
        line("gpu", conf.gpu.value_or("unset"));
        line("multiplier", std::to_string(conf.multiplier));
        line("frame_generation_provisioned", yesNo(conf.frame_generation_provisioned));
        line("frame_generation_enabled", yesNo(conf.frame_generation_enabled));
        line("frame_generation_model", frameGenerationModelName(conf.frame_generation_model));
        line("mako_fg_mode", makoFgModeName(conf.mako_fg_mode));
        line("scaling_enabled", yesNo(conf.scaling_enabled));
        line("swapchain_image_count_compatibility", yesNo(conf.swapchain_image_count_compatibility));
        line("mailbox_presentation_compatibility", yesNo(conf.mailbox_presentation_compatibility));
        line("scaling_method", scalingMethodName(conf.scaling_method));
        line("scaling_factor", real(conf.scaling_factor));
        line("scaling_supersampling", yesNo(conf.scaling_supersampling));
        line("scaling_sharpness", real(conf.scaling_sharpness));
        line("hdr_quality", hdrQualityName(conf.hdr_quality));
        line("frame_generation_refresh_threshold", std::to_string(conf.frame_generation_refresh_threshold));
        line("base_fps_cap", std::to_string(conf.base_fps_cap));
        line("gpu_headroom_percent", std::to_string(conf.gpu_headroom_percent));
        line("adaptive", yesNo(conf.adaptive));
        line("adaptive_auto_base_fps_cap", yesNo(conf.adaptive_auto_base_fps_cap));
        line("adaptive_fractional_real_frame_priority", adaptiveFractionalRealFramePriorityName(conf.adaptive_fractional_real_frame_priority));
        line("target_fps", std::to_string(conf.target_fps));
        line("adaptive_target_refresh_rate", yesNo(conf.adaptive_target_refresh_rate));
        line("adaptive_max_multiplier", std::to_string(conf.adaptive_max_multiplier));
        line("adaptive_stable_cadence", yesNo(conf.adaptive_stable_cadence));
        line("gamescope_vrr_mode", gamescopeVrrModeName(conf.gamescope_vrr_mode));
        line("dynamic_cadence_recovery", yesNo(conf.dynamic_cadence_recovery));
        line("dynamic_cadence_probe_interval_seconds", real(conf.dynamic_cadence_probe_interval_seconds));
        line("ultra_performance", yesNo(conf.ultra_performance));
        line("flow_scale", real(conf.flow_scale));
        line("performance_mode", yesNo(conf.performance_mode));
        line("pacing", conf.pacing == Pacing::None ? "none" : "other");
    }

    void print(const ConfigFile& config)
    {
        std::printf("current_profile %s\n", config.current_profile.value_or("unset").c_str());
        std::printf("dll %s\n", config.global().dll.value_or("unset").c_str());
        std::printf("allow_fp16 %s\n", yesNo(config.global().allow_fp16));
        for (const GameConf& profile : config.profiles())
        {
            std::printf("profile %s\n", profile.name.c_str());
            for (const std::string& game : profile.active_in)
                std::printf("active_in %s\n", game.c_str());
            print("", profile);
            std::printf("power %s\n", yesNo(!profile.power_profiles.empty()));
            if (!profile.power_profiles.empty())
            {
                print("handheld ", profile.power_profiles.at(0));
                print("docked ", profile.power_profiles.at(1));
            }
        }
    }

    template<typename Enum, typename Name, typename FromName>
    void printNames(const char* kind, Name name, FromName fromName)
    {
        std::printf("%s", kind);
        for (unsigned value = 0; value < 256; value++)
            if (const auto parsed = fromName(name(static_cast<Enum>(value))); parsed && *parsed == static_cast<Enum>(value))
                std::printf(" %s", name(static_cast<Enum>(value)));
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

    int status(const std::string& which, const std::string& reader, const std::string& readerOut)
    {
        mako::layer::RuntimeStatusRecord record;
        record.requestedProfile.name                   = "vkdaedalus \"default\"\t\\";
        record.requestedProfile.frame_generation_model = FrameGenerationModel::LosslessScaling;
        record.requestedProfile.scaling_enabled        = true;
        record.appliedProfile                          = record.requestedProfile;
        record.spatialSourceWidth                      = 1280;
        record.spatialSourceHeight                     = 720;
        record.spatialPresentationWidth                = 1280;
        record.spatialPresentationHeight               = 720;
        uint64_t context                               = 1;
        if (which == "scaling")
        {
            context                     = 3;
            record.spatialScalingActive = true;
            record.spatialSourceWidth   = 853;
            record.spatialSourceHeight  = 480;
            record.spatialActiveMethod  = ScalingMethod::Mako;
        }
        else if (which == "native")
        {
            context                             = 18446744073709551615ull;
            record.spatialScalingInactiveReason = "application-extent-mismatch";
        }
        else if (which == "fallback")
        {
            record.appliedProfile.frame_generation_model = FrameGenerationModel::MakoFg;
            record.appliedProfile.scaling_enabled        = false;
        }
        else
            return 2;
        mako::layer::RuntimeStatusPublisher publisher(context, "frame-generation");
        publisher.publish(record);
        if (publisher.path().empty() || !std::filesystem::exists(publisher.path()))
        {
            std::fprintf(stderr, "MAKO published no status file\n");
            return 1;
        }
        std::printf("role frame-generation\ncontext %llu\napplied_model %s\napplied_scaling %s\nactive %s\ninactive_reason %s\nsource %ux%u\npresentation %ux%u\nmethod %s\n",
                    static_cast<unsigned long long>(context), frameGenerationModelName(record.appliedProfile.frame_generation_model),
                    yesNo(record.appliedProfile.scaling_enabled), yesNo(record.spatialScalingActive && record.appliedProfile.scaling_enabled),
                    record.spatialScalingInactiveReason ? record.spatialScalingInactiveReason->c_str() : "-", record.spatialSourceWidth, record.spatialSourceHeight,
                    record.spatialPresentationWidth, record.spatialPresentationHeight, std::string(scalingMethodName(record.spatialActiveMethod)).c_str());
        std::fflush(stdout);
        const pid_t child = fork();
        if (child < 0)
            return 1;
        if (child == 0)
        {
            if (std::freopen(readerOut.c_str(), "w", stdout) == nullptr)
                _exit(126);
            execl(reader.c_str(), reader.c_str(), "status", std::to_string(getppid()).c_str(), std::to_string(record.spatialPresentationWidth).c_str(),
                  std::to_string(record.spatialPresentationHeight).c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        int ended = 0;
        if (waitpid(child, &ended, 0) != child || !WIFEXITED(ended))
            return 1;
        return WEXITSTATUS(ended);
    }

    const char* sourceName(PowerSource source)
    {
        switch (source)
        {
            case PowerSource::Handheld: return "handheld";
            case PowerSource::Docked: return "docked";
            case PowerSource::Unknown: break;
        }
        return "unknown";
    }
}

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    try
    {
        if (args.size() == 2 && args[0] == "read")
        {
            ConfigFile config;
            try
            {
                config = ConfigFile(args[1]);
            }
            catch (const std::exception& error)
            {
                std::printf("refused\n%s\n", error.what());
                return 0;
            }
            print(config);
            return 0;
        }
        if (args.size() == 3 && args[0] == "read-as")
        {
            const ConfigFile config(args[1]);
            PowerSource      source = PowerSource::Unknown;
            if (args[2] == "handheld")
                source = PowerSource::Handheld;
            else if (args[2] == "docked")
                source = PowerSource::Docked;
            else if (args[2] != "unknown")
                return 2;
            for (const GameConf& profile : config.profiles())
            {
                std::printf("profile %s\n", profile.name.c_str());
                print("", profileForPowerSource(profile, source));
            }
            return 0;
        }
        if (args.size() == 3 && args[0] == "rewrite")
        {
            ConfigFile(args[1]).write(args[2]);
            return 0;
        }
        if (args.size() == 1 && args[0] == "path")
        {
            std::printf("%s\n", findConfigurationFile().c_str());
            return 0;
        }
        if ((args.size() == 2 || args.size() == 3) && args[0] == "identify")
        {
            if (args.size() == 3 && !mapped(args[2].c_str()))
            {
                std::fprintf(stderr, "cannot map %s\n", args[2].c_str());
                return 1;
            }
            const auto chosen = findProfile(ConfigFile(args[1]), identify(), false);
            std::printf("%s\n", chosen ? chosen->second.name.c_str() : "none");
            return 0;
        }
        if (args.size() == 1 && args[0] == "dll")
        {
            const ConfigFile config(findConfigurationFile());
            if (config.global().dll)
                std::printf("%s\n", config.global().dll->c_str());
            else
                try
                {
                    std::printf("%s\n", findShaderDll().c_str());
                }
                catch (const std::exception&)
                {
                    std::printf("none\n");
                }
            return 0;
        }
        if (args.size() == 4 && args[0] == "status")
            return status(args[1], args[2], args[3]);
        if (args.size() == 2 && args[0] == "power")
        {
            std::printf("%s\n", sourceName(detectPowerSource(args[1])));
            return 0;
        }
        if (args.size() == 1 && args[0] == "names")
        {
            printNames<FrameGenerationModel>("frame_generation_model", frameGenerationModelName, frameGenerationModelFromName);
            printNames<MakoFgMode>("mako_fg_mode", makoFgModeName, makoFgModeFromName);
            printNames<ScalingMethod>("scaling_method", scalingMethodName, scalingMethodFromName);
            printNames<HdrQuality>("hdr_quality", hdrQualityName, hdrQualityFromName);
            printNames<AdaptiveFractionalRealFramePriority>(
                "adaptive_fractional_real_frame_priority", adaptiveFractionalRealFramePriorityName, adaptiveFractionalRealFramePriorityFromName);
            printNames<GamescopeVrrMode>("gamescope_vrr_mode", gamescopeVrrModeName, gamescopeVrrModeFromName);
            return 0;
        }
        if (args.size() == 1 && args[0] == "limits")
        {
            using L = GameConfLimits;
            std::printf("multiplier %zu %zu\n", L::minimumMultiplier, L::maximumMultiplier);
            std::printf("scaling_factor %.9g %.9g\n", static_cast<double>(L::minimumScalingFactor), static_cast<double>(L::maximumScalingFactor));
            std::printf(
                "scaling_sharpness %.9g %.9g\n", static_cast<double>(L::minimumScalingSharpness), static_cast<double>(L::maximumScalingSharpness));
            std::printf("gpu_headroom_percent");
            for (const uint32_t percent : L::gpuHeadroomPercentPresets)
                std::printf(" %u", percent);
            std::printf("\nbase_fps_cap %u %u\n", L::minimumBaseFpsCap, L::maximumBaseFpsCap);
            std::printf(
                "frame_generation_refresh_threshold %u %u\n", L::minimumFrameGenerationRefreshThreshold, L::maximumFrameGenerationRefreshThreshold);
            std::printf("target_fps %u %u\n", L::minimumTargetFps, L::maximumTargetFps);
            std::printf("adaptive_max_multiplier %zu %zu\n", L::minimumAdaptiveMaxMultiplier, L::maximumAdaptiveMaxMultiplier);
            std::printf("dynamic_cadence_probe_interval_seconds %.9g %.9g\n",
                        static_cast<double>(L::minimumDynamicCadenceProbeIntervalSeconds),
                        static_cast<double>(L::maximumDynamicCadenceProbeIntervalSeconds));
            std::printf("flow_scale %.9g %.9g\n", static_cast<double>(L::minimumFlowScale), static_cast<double>(L::maximumFlowScale));
            return 0;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "MAKO: %s\n", error.what());
        return 1;
    }
    std::fprintf(stderr,
                 "usage: read <file> | read-as <file> <handheld|docked|unknown> | rewrite <in> <out> | path | identify <file> [exe] | dll | "
                 "power <folder> | status <scaling|native|fallback> <vkDaedalus probe> <its output> | names | limits\n");
    return 2;
}
