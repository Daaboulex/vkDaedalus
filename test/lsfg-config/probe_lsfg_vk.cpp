#include "lsfg-vk-utils/config.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace lsfgvk::utils;

namespace
{
    const char* yesNo(bool value)
    {
        return value ? "true" : "false";
    }

    void print(const Configuration& config)
    {
        std::printf("dll %s\n", config.dll ? config.dll->c_str() : "unset");
        std::printf("allow_fp16 %s\n", yesNo(config.allowHalfPrecision));
        std::printf("log_level %s\n", std::string(toString(config.logLevel)).c_str());
        std::printf("log_file %s\n", config.logFile ? config.logFile->c_str() : "unset");
        for (const Profile& profile : config.profiles)
        {
            std::printf("profile %s\n", profile.name.c_str());
            for (const std::string& game : profile.activeIn)
                std::printf("active_in %s\n", game.c_str());
            std::printf("multiplier %u\n", profile.multiplier);
            std::printf("flow_scale %.9g\n", static_cast<double>(profile.flowScale));
            std::printf("performance_mode %s\n", yesNo(profile.performanceMode));
            std::printf("color_space %s\n", std::string(toString(profile.colorSpace)).c_str());
            std::printf("transfer_function %s\n", std::string(toString(profile.transferFunction)).c_str());
            std::printf("override_present_mode %s\n", yesNo(profile.overridePresentMode));
            std::printf("preserve_swapchain_image_count %s\n", yesNo(profile.preserveSwapchainImageCount));
        }
    }

    template<typename Enum>
    void printNames(const char* kind)
    {
        std::printf("%s", kind);
        for (unsigned value = 0; value < 256; value++)
            try
            {
                std::printf(" %s", std::string(toString(static_cast<Enum>(value))).c_str());
            }
            catch (const std::exception&)
            {
                continue;
            }
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
}

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    try
    {
        if (args.size() == 2 && args[0] == "read")
        {
            Configuration config;
            try
            {
                config = readConfig(args[1]);
            }
            catch (const std::runtime_error&)
            {
                std::printf("refused\n");
                return 0;
            }
            print(config);
            return 0;
        }
        if (args.size() == 3 && args[0] == "rewrite")
        {
            writeConfig(args[2], readConfig(args[1]));
            return 0;
        }
        if (args.size() == 1 && args[0] == "path")
        {
            std::printf("%s\n", defaultConfigPath().c_str());
            return 0;
        }
        if ((args.size() == 2 || args.size() == 3) && args[0] == "identify")
        {
            if (args.size() == 3 && !mapped(args[2].c_str()))
            {
                std::fprintf(stderr, "cannot map %s\n", args[2].c_str());
                return 1;
            }
            const auto chosen = identifyProcess(readConfig(args[1]));
            std::printf("%s\n", chosen ? chosen->second.name.c_str() : "none");
            return 0;
        }
        if (args.size() == 1 && args[0] == "dll")
        {
            const Configuration config = getOrDefault();
            if (config.dll)
                std::printf("%s\n", fixDllPath(*config.dll).c_str());
            else if (const auto found = findDll())
                std::printf("%s\n", found->c_str());
            else
                std::printf("none\n");
            return 0;
        }
        if (args.size() == 1 && args[0] == "names")
        {
            printNames<LogLevel>("log_level");
            printNames<PacingMode>("pacing_mode");
            printNames<ColorSpace>("color_space");
            printNames<TransferFunction>("transfer_function");
            return 0;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "lsfg-vk: %s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: read <file> | rewrite <in> <out> | path | identify <file> [exe] | dll | names\n");
    return 2;
}
