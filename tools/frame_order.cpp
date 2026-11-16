#include <vulkan/vulkan.h>

#include "config_serializer.hpp"
#include "frame_generation.hpp"
#include "mako_config.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace vkBasalt;

    std::map<std::string, std::vector<std::string>> usersWith(const std::string& profile, const std::string& program)
    {
        std::map<std::string, std::vector<std::string>> users = ConfigSerializer::profileUsers();
        std::vector<std::string>&                       games = users[profile];
        if (std::find(games.begin(), games.end(), program) == games.end())
            games.push_back(program);
        return users;
    }

    void prepareLsfgVk(const std::vector<std::string>& command, const std::string& program, const ActiveProfile& profile)
    {
        if (lsfgSettingsFromEnvironment() || frameGenerationLayerDisabledBy(FrameGenerationLayer::LsfgVk)
            || lsfgLayerListedBy(vkEnumerateInstanceLayerProperties) != true)
            return;
        const std::string                                     path = lsfgConfigPath();
        const std::map<std::string, std::vector<std::string>> users = usersWith(profile.name, program);
        const std::vector<std::string>&                       games = users.at(profile.name);
        std::string                                           problem;
        if (!syncLsfgActiveIn(path, users, problem))
        {
            std::cerr << "vkdaedalus-run: lsfg-vk's file " << path
                      << " is left as it was, so frame generation may follow another profile: " << problem << "\n";
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
        const DllStatus dll = lsfgDllStatus(read.file->global);
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
            std::cerr << "vkdaedalus-run: no lsfg-vk profile was added for " << program
                      << ", so frame generation joins it from its next launch: " << problem << "\n";
    }

    void prepareMako(const std::vector<std::string>& command, const std::string& program, const ActiveProfile& profile)
    {
        if (makoSettingsFromEnvironment() || frameGenerationLayerDisabledBy(FrameGenerationLayer::Mako)
            || makoLayerListedBy(vkEnumerateInstanceLayerProperties) != true)
            return;
        const std::string                                     path = makoConfigPath();
        const std::map<std::string, std::vector<std::string>> users = usersWith(profile.name, program);
        const std::vector<std::string>&                       games = users.at(profile.name);
        std::string                                           problem;
        if (!syncMakoActiveIn(path, users, problem))
        {
            std::cerr << "vkdaedalus-run: MAKO's file " << path << " is left as it was, so frame generation may follow another profile: " << problem
                      << "\n";
            return;
        }
        const MakoRead read = readMakoFile(path);
        if (!read.file)
        {
            std::cerr << "vkdaedalus-run: MAKO refuses " << path << " (" << read.problem << "), so it is left alone and MAKO runs in no game\n";
            return;
        }
        if (makoProfileFor(*read.file, makoProcessOf(command, program)) || makoProfileNamed(*read.file, frameGenerationProfileNameOf(profile.name)))
            return;
        const DllStatus           dll = makoDllStatus(read.file->global);
        std::optional<MakoGlobal> global;
        if (!dll.found && dll.inSteamLibrary)
        {
            global      = read.file->global;
            global->dll = *dll.inSteamLibrary;
        }
        MakoFile written;
        if (!saveMakoFile(path, global, newMakoProfileFor(profile.name, games), written, problem))
            std::cerr << "vkdaedalus-run: no MAKO profile was added for " << program << ": " << problem << "\n";
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: vkdaedalus-frame-order [--profile <name>] <command...>\n"
                     "with --profile, first sets the profile the program runs, as vkdaedalus-run --profile does; then\n"
                     "prints after or before for the profile the command will run, then the program's name, then its executable as\n"
                     "the command names it, then the frame generation layer the profile runs, lsfg-vk or mako; with that layer\n"
                     "installed it lists the program in the layer's profile of its vkDaedalus profile and only there, and adds that\n"
                     "profile when the layer has none for the program, lsfg-vk's at multiplier 1 once its DLL is found and MAKO's\n"
                     "with frame generation off, so frame generation can be switched on in the game without a new launch\n";
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
    const std::string              program = ConfigSerializer::programNameFromCommand(command);
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
    const ProfileSettings      settings = ConfigSerializer::profileSettingsFor(program);
    const FrameGenerationLayer layer    = settings.frameGenerationLayer.value_or(defaultFrameGenerationLayer(
        lsfgLayerListedBy(vkEnumerateInstanceLayerProperties) == true, makoLayerListedBy(vkEnumerateInstanceLayerProperties) == true, makoEnabledByCaller()));
    const ActiveProfile        active   = ConfigSerializer::getActiveProfile(program);
    if (program != UNKNOWN_PROGRAM_NAME && !ConfigSerializer::isWineLoaderName(program) && active.kind != ProfileKind::Bypass)
    {
        if (layer == FrameGenerationLayer::Mako)
            prepareMako(command, program, active);
        else
            prepareLsfgVk(command, program, active);
    }
    std::cout << (settings.effectsAfterFrameGeneration.value_or(false) ? "after" : "before") << "\n"
              << program << "\n"
              << ConfigSerializer::programWordFromCommand(command) << "\n"
              << frameGenerationLayerName(layer) << "\n";
    return 0;
}
