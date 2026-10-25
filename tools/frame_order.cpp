#include <vulkan/vulkan.h>

#include "config_serializer.hpp"
#include "frame_generation.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace vkBasalt;

    void prepareFrameGeneration(const std::vector<std::string>& command, const std::string& program)
    {
        if (program == UNKNOWN_PROGRAM_NAME || ConfigSerializer::isWineLoaderName(program) || lsfgSettingsFromEnvironment() ||
            std::getenv("DISABLE_LSFGVK") != nullptr || lsfgLayerListedBy(vkEnumerateInstanceLayerProperties) != true)
            return;
        const ActiveProfile profile = ConfigSerializer::getActiveProfile(program);
        if (profile.kind == ProfileKind::Bypass)
            return;
        const std::string                               path  = lsfgConfigPath();
        std::map<std::string, std::vector<std::string>> users = ConfigSerializer::profileUsers();
        std::vector<std::string>&                       games = users[profile.name];
        if (std::find(games.begin(), games.end(), program) == games.end())
            games.push_back(program);
        std::string problem;
        if (!syncLsfgActiveIn(path, users, problem))
        {
            std::cerr << "vkdaedalus-run: lsfg-vk's file " << path << " is left as it was, so frame generation may follow another profile: " << problem << "\n";
            return;
        }
        const LsfgRead read = readLsfgFile(path);
        if (!read.file)
        {
            std::cerr << "vkdaedalus-run: lsfg-vk refuses " << path << " (" << read.problem << "), so it is left alone and lsfg-vk runs in no game\n";
            return;
        }
        if (lsfgProfileFor(*read.file, lsfgProcessOf(command, program)))
            return;
        const LsfgDllStatus dll = lsfgDllStatus(read.file->global);
        if (!dll.found && !dll.inSteamLibrary)
            return;
        std::optional<FrameGenerationGlobal> global;
        if (!dll.found)
        {
            global      = read.file->global;
            global->dll = *dll.inSteamLibrary;
        }
        LsfgFile written;
        if (!saveLsfgFile(path, global, newLsfgProfileFor(profile.name, games, offMultiplier, {}), written, problem))
            std::cerr << "vkdaedalus-run: no lsfg-vk profile was added for " << program << ", so frame generation joins it from its next launch: " << problem
                      << "\n";
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: vkdaedalus-frame-order [--profile <name>] <command...>\n"
                     "with --profile, first sets the profile the program runs, as vkdaedalus-run --profile does; then\n"
                     "prints after or before for the profile the command will run, then the program's name, then its executable as\n"
                     "the command names it; with lsfg-vk installed it lists the program in the lsfg-vk profile of its vkDaedalus profile\n"
                     "and only there, and with lsfg-vk's DLL found it adds that profile at multiplier 1 when lsfg-vk has none for the\n"
                     "program, so frame generation can be switched on in the game without a new launch\n";
        return 2;
    }
    ConfigSerializer::moveOldConfigFolderOnce();
    ConfigSerializer::migrateToSharedProfilesOnce();
    std::string profile;
    int         first = 1;
    if (std::string(argv[1]) == "--profile")
    {
        if (argc < 4)
        {
            std::cerr << "usage: vkdaedalus-frame-order --profile <name> <command...>\n";
            return 2;
        }
        profile = argv[2];
        first   = 3;
    }
    const std::vector<std::string> command(argv + first, argv + argc);
    const std::string             program = ConfigSerializer::programNameFromCommand(command);
    if (!profile.empty())
    {
        const std::vector<std::string> profiles = ConfigSerializer::listProfiles();
        if (std::find(profiles.begin(), profiles.end(), profile) == profiles.end())
            std::cerr << "vkdaedalus-run: there is no profile named " << profile << ", so " << program << " keeps "
                      << ConfigSerializer::getActiveProfile(program).name << "\n";
        else if (!ConfigSerializer::setActiveProfile(program, profile))
            std::cerr << "vkdaedalus-run: configs/.active_profiles could not be written, so " << program << " keeps "
                      << ConfigSerializer::getActiveProfile(program).name << "\n";
    }
    prepareFrameGeneration(command, program);
    std::cout << (ConfigSerializer::effectsAfterFrameGenerationFor(program) ? "after" : "before") << "\n" << program << "\n"
              << ConfigSerializer::programWordFromCommand(command) << "\n";
    return 0;
}
