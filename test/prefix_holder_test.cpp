#include "prefix_holder.hpp"

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

static std::string statOf(int32_t pid, const std::string& comm, const std::string& start)
{
    return std::to_string(pid) + " (" + comm + ") S 1 4242 4242 0 -1 4194560 5129 0 0 0 31 9 0 0 20 0 1 0 " + start
           + " 12345678 900 18446744073709551615 1 1 0 0 0 0 0 4096 0 0 0 0 17 3 0 0 0 0 0\n";
}

int main()
{
    expect(processStartTicks(statOf(4242, "Game.exe", "987654321")) == 987654321u, "a process's start time is the 22nd field of its stat line");
    expect(processStartTicks(statOf(4242, "a ) b) (c", "77")) == 77u, "counted after the last closing bracket, as a name may hold spaces and brackets");
    expect(!processStartTicks("4242 (Game.exe) S 1 2 3") && !processStartTicks("4242 Game.exe S") && !processStartTicks(statOf(4242, "Game.exe", "98x7")),
           "a short, unbracketed or garbled stat line gives no start time");

    const uint64_t holder = prefixHolderOf(4242, 987654321);
    expect(holder != 0 && prefixHolderPid(holder) == 4242, "the holder carries its process id, and is never the empty holder");
    expect(prefixHolderAlive(holder, statOf(4242, "Game.exe", "987654321")), "the game that holds the prefix's raw input still holds it while it runs");
    expect(!prefixHolderAlive(holder, statOf(4242, "explorer.exe", "987654999")),
           "a process that took the id of a game that ended with its overlay open is not the holder, so explorer.exe passes raw input again");
    expect(!prefixHolderAlive(holder, std::nullopt), "a holder whose process is gone holds nothing");
    expect(!prefixHolderAlive(holder, "4242 (Game.exe"), "nor one whose stat line cannot be read");

    if (failures)
        std::printf("%d prefix holder check(s) failed\n", failures);
    return failures ? 1 : 0;
}
