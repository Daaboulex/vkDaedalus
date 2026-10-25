#include "config_serializer.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace vkBasalt;

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "usage: vkdaedalus-profile <game> [<profile>]\n"
                     "prints the profile vkDaedalus runs in the game, or sets it from the game's next launch; the game is named as\n"
                     "vkDaedalus names it, its executable or, under Wine and Proton, its .exe; none keeps vkDaedalus out of it\n";
        return 2;
    }
    ConfigSerializer::moveOldConfigFolderOnce();
    ConfigSerializer::migrateToSharedProfilesOnce();
    const std::string game = argv[1];
    if (argc == 3)
    {
        const std::string              profile  = argv[2];
        const std::vector<std::string> profiles = ConfigSerializer::listProfiles();
        if (std::find(profiles.begin(), profiles.end(), profile) == profiles.end())
        {
            std::cerr << "vkdaedalus-profile: there is no profile named " << profile << "; the profiles are:";
            for (const std::string& known : profiles)
                std::cerr << " " << known;
            std::cerr << "\n";
            return 1;
        }
        if (!ConfigSerializer::setActiveProfile(game, profile))
        {
            std::cerr << "vkdaedalus-profile: configs/.active_profiles could not be written, so " << game << " keeps its profile\n";
            return 1;
        }
    }
    std::cout << ConfigSerializer::getActiveProfile(game).name << "\n";
    return 0;
}
