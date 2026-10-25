#include "config.hpp"
#include "config_serializer.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

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

static void pastTheCheckInterval()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;

    const fs::path dir = argv[1];
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path profile = dir / "vkcube.conf";
    std::ofstream(profile) << "effects = cas\ncasSharpness = 0.4\n";

    Config config(profile.string());
    pastTheCheckInterval();
    expect(!config.hasConfigChanged(), "a file nobody touched is unchanged");

    pastTheCheckInterval();
    expect(ConfigSerializer::saveToPath(profile.string(), {"cas", "smaa"}, {}, {}, {}, {}),
           "the layer can write the profile it watches");
    pastTheCheckInterval();
    expect(!config.hasConfigChanged(), "the layer's own write of the watched profile is not a change to reload for");

    std::ofstream(profile, std::ios::app) << "casSharpness = 0.7\n";
    pastTheCheckInterval();
    expect(config.hasConfigChanged(), "an edit by someone else right after the layer's own write is still seen");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: own writes are not changes, other writes are\n");
    return 0;
}
