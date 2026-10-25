#include "overlay/report.hpp"

#include <cstdio>
#include <string>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

int main()
{
    expect(downloadFolderOf("# written by xdg-user-dirs-update\nXDG_DESKTOP_DIR=\"$HOME/Desktop\"\nXDG_DOWNLOAD_DIR=\"$HOME/Hent\"\n", "/home/u") == "/home/u/Hent",
           "the download folder is the one xdg-user-dirs names, $HOME expanded");
    expect(downloadFolderOf("XDG_DOWNLOAD_DIR=\"/data/dl\"\n", "/home/u") == "/data/dl", "an absolute download folder is taken as it is");
    expect(downloadFolderOf("", "/home/u") == "/home/u/Downloads" && downloadFolderOf("XDG_DOWNLOAD_DIR=\"$HOME\"\n", "/home/u") == "/home/u/Downloads",
           "with no file, or a download folder that is the home itself, the report goes to ~/Downloads");
    expect(downloadFolderOf("#XDG_DOWNLOAD_DIR=\"/x\"\n", "/home/u") == "/home/u/Downloads", "a commented-out line is not read");

    ReportFacts facts;
    facts.when                 = "2026-10-21 16:00";
    facts.game                 = "vkcube";
    facts.profile              = "default";
    facts.gpus                 = {"GPU 1: AMD Radeon RX 9070 XT, the game's"};
    facts.frameGenerationPlace = "lsfg-vk is not loaded";
    facts.files                = {{"vkdaedalus.log", "vkDaedalus info:  a line"}, {"lsfg-vk.log", ""}};
    const std::string report   = diagnosticReport(facts, true);
    expect(report.find("Game: vkcube\nProfile: default\nSafe Anti-Cheat: on\n") != std::string::npos && report.find("  GPU 1: AMD Radeon RX 9070 XT, the game's\n") != std::string::npos,
           "the report names the game, its profile, Safe Anti-Cheat and every GPU");
    expect(report.find("Layers below vkDaedalus, nearest first:\n  none\n") != std::string::npos, "no layer below says none rather than leaving the section empty");
    expect(report.find("== vkdaedalus.log\nvkDaedalus info:  a line\n") != std::string::npos && report.find("== lsfg-vk.log\n(empty or missing)\n") != std::string::npos,
           "each file gets its own section, a missing one saying so");
    return failures ? 1 : 0;
}
