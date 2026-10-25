#include "gpu_probe.hpp"

#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

using namespace vkBasalt;

int main()
{
    const std::vector<std::tuple<std::string, std::string, std::string>> cases = {
        {"/nix/store/abc-coolercontrol-2.0/bin/coolercontrold", "coolercontrold", "coolercontrold"},
        {"/nix/store/def-plasma-workspace/bin/.plasmashell-wrapped", ".plasmashell-wr", "plasmashell"},
        {"Xwayland", "Xwayland", "Xwayland"},
        {"/usr/bin/weston", "weston", "weston"},
        {"", "kworker/u64:1", "kworker/u64:1"},
    };
    int failures = 0;
    for (const auto& [argv0, comm, want] : cases)
    {
        const std::string got = readableProgramName(argv0, comm);
        if (got != want)
        {
            std::printf("FAIL: argv0 '%s', comm '%s' gave '%s', want '%s'\n", argv0.c_str(), comm.c_str(), got.c_str(), want.c_str());
            failures++;
        }
    }
    if (failures)
        std::printf("%d program name check(s) failed\n", failures);
    return failures ? 1 : 0;
}
