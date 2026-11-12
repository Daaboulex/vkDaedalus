#include "frame_generation.hpp"

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

    void print(const LsfgFile& file)
    {
        std::printf("dll %s\n", file.global.dll ? file.global.dll->c_str() : "unset");
        std::printf("allow_fp16 %s\n", yesNo(file.global.allowHalfPrecision));
        std::printf("log_level %s\n", file.global.logLevel.c_str());
        std::printf("log_file %s\n", file.global.logFile ? file.global.logFile->c_str() : "unset");
        for (const FrameGenerationProfile& profile : file.profiles)
        {
            std::printf("profile %s\n", profile.name.c_str());
            for (const std::string& game : profile.activeIn)
                std::printf("active_in %s\n", game.c_str());
            std::printf("multiplier %u\n", profile.multiplier);
            std::printf("flow_scale %.9g\n", static_cast<double>(profile.flowScale));
            std::printf("performance_mode %s\n", yesNo(profile.performanceMode));
            std::printf("color_space %s\n", profile.colorSpace.value_or(std::string(lsfgColorSpaces.front().name)).c_str());
            std::printf("transfer_function %s\n", profile.transferFunction.value_or(std::string(lsfgTransferFunctions.front().name)).c_str());
            std::printf("override_present_mode %s\n", yesNo(profile.overridePresentMode));
            std::printf("preserve_swapchain_image_count %s\n", yesNo(profile.preserveSwapchainImageCount));
        }
    }

    template<size_t N>
    void printNames(const char* kind, const std::array<LsfgChoice, N>& choices)
    {
        std::printf("%s", kind);
        for (const LsfgChoice& choice : choices)
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

    std::vector<FrameGenerationProfile> editsToWrite()
    {
        const std::vector<std::string> names = {"plain", "vkdaedalus Default", "quote \" and back\\slash", "\xc3\x9cn\xc3\xaf" "code \xe6\xb8\xb8\xe6\x88\x8f"};
        const std::vector<std::vector<std::string>> games = {
            {}, {"/usr/bin/game"}, {"Game.EXE", "123456", "/opt/a b/run"}, {"C:\\Games\\Odd \"Name\".exe"}, {"\xe6\xb8\xb8\xe6\x88\x8f.exe"}};
        const std::vector<uint32_t> multipliers = {1, 2, 3, 20, 4294967295u};
        const std::vector<float>    flowScales  = {0.25f, 0.5f, 0.85f, 1.0f, 1.0f / 3.0f};
        std::vector<FrameGenerationProfile> edits;
        for (size_t colour = 0; colour < lsfgColorSpaces.size(); colour++)
            for (size_t transfer = 0; transfer < lsfgTransferFunctions.size(); transfer++)
            {
                const size_t           i = edits.size();
                FrameGenerationProfile profile;
                profile.name                        = names[i % names.size()];
                profile.activeIn                    = games[i % games.size()];
                profile.multiplier                  = multipliers[i % multipliers.size()];
                profile.flowScale                   = flowScales[i % flowScales.size()];
                profile.performanceMode             = (i & 1) != 0;
                profile.colorSpace                  = std::string(lsfgColorSpaces[colour].name);
                profile.transferFunction            = std::string(lsfgTransferFunctions[transfer].name);
                profile.overridePresentMode         = (i & 2) != 0;
                profile.preserveSwapchainImageCount = (i & 4) != 0;
                edits.push_back(profile);
            }
        return edits;
    }

    FrameGenerationGlobal globalToWrite(size_t i)
    {
        const std::vector<std::optional<std::string>> dlls     = {std::nullopt, "/games/Lossless Scaling/lsfg-vk.dll", "~/steam/lsfg-vk.dll"};
        const std::vector<std::optional<std::string>> logFiles = {std::nullopt, "/var/log/lsfg vk.txt"};
        FrameGenerationGlobal                         global;
        global.dll                = dlls[i % dlls.size()];
        global.allowHalfPrecision = (i & 8) != 0;
        global.logLevel           = std::string(lsfgLogLevels[i % lsfgLogLevels.size()].name);
        global.logFile            = logFiles[i % logFiles.size()];
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
        const std::vector<FrameGenerationProfile> edits = editsToWrite();
        for (size_t i = 0; i < edits.size(); i++)
        {
            std::string                      problem;
            const std::optional<std::string> text = lsfgTextWith(seed, i % 3 == 0 ? std::nullopt : std::optional(globalToWrite(i)), edits[i], problem);
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
}

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 3 && args[0] == "write")
        return write(args[1].c_str(), args[2]);
    if (args.size() == 2 && args[0] == "read")
    {
        const LsfgRead read = readLsfgFile(args[1]);
        if (read.file)
            print(*read.file);
        else
            std::printf("refused\n");
        return 0;
    }
    if (args.size() == 1 && args[0] == "path")
    {
        std::printf("%s\n", lsfgConfigPath().c_str());
        return 0;
    }
    if ((args.size() == 2 || args.size() == 3) && args[0] == "identify")
    {
        if (args.size() == 3 && !mapped(args[2].c_str()))
        {
            std::fprintf(stderr, "cannot map %s\n", args[2].c_str());
            return 1;
        }
        const LsfgRead read = readLsfgFile(args[1]);
        if (!read.file)
        {
            std::fprintf(stderr, "refused %s: %s\n", args[1].c_str(), read.problem.c_str());
            return 1;
        }
        const std::optional<size_t> chosen = lsfgProfileFor(*read.file, thisLsfgProcess());
        std::printf("%s\n", chosen ? read.file->profiles[*chosen].name.c_str() : "none");
        return 0;
    }
    if (args.size() == 1 && args[0] == "dll")
    {
        const LsfgRead read = readLsfgFile(lsfgConfigPath());
        if (!read.file)
        {
            std::fprintf(stderr, "refused %s: %s\n", lsfgConfigPath().c_str(), read.problem.c_str());
            return 1;
        }
        const std::optional<std::string> dll = lsfgDllPath(read.file->global);
        std::printf("%s\n", dll ? dll->c_str() : "none");
        return 0;
    }
    if (args.size() == 1 && args[0] == "names")
    {
        printNames("log_level", lsfgLogLevels);
        printNames("pacing_mode", lsfgPacingModes);
        printNames("color_space", lsfgColorSpaces);
        printNames("transfer_function", lsfgTransferFunctions);
        return 0;
    }
    std::fprintf(stderr, "usage: write <seed> <folder> | read <file> | path | identify <file> [exe] | dll | names\n");
    return 2;
}
