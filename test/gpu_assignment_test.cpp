#include "gpu_assignment.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

static bool mentions(const std::vector<std::string>& problems, const std::string& text)
{
    for (const auto& problem : problems)
        if (problem.find(text) != std::string::npos)
            return true;
    return false;
}

int main()
{
    const auto amd = parseGpuId("1002:7550");
    expect(amd && amd->vendor == 0x1002 && amd->device == 0x7550 && !amd->pci, "vendor:device in hex names a GPU");
    const auto upper = parseGpuId("10DE:2184");
    expect(upper && upper->vendor == 0x10de && upper->device == 0x2184, "hex in capitals names the same GPU");
    const auto mesa = parseGpuId("10005:0");
    expect(mesa && mesa->vendor == 0x10005, "a vendor id past four digits, as Mesa's software renderers report, is taken");
    const auto slot = parseGpuId("1002:7550@0000:73:00.0");
    expect(slot && slot->pci && slot->pci->bus == 0x73 && slot->pci->device == 0 && slot->pci->function == 0,
           "a PCI address after @ tells identical cards apart");
    expect(slot && formatGpuId(*slot) == "1002:7550@0000:73:00.0", "a GPU with its PCI address is written as it was read");
    expect(upper && formatGpuId(*upper) == "10de:2184", "a GPU is written in lower-case hex");

    for (const char* bad : {"", "1002", "1002:", ":7550", "1002:7550:1", "100g:7550", "0x1002:7550", "-1:7550", "123456789:1",
                            "1002:7550@", "1002:7550@0000:73:00", "1002:7550@0000:73:20.0", "1002:7550@0000:73:00.8",
                            "1002:7550@000:073:00.0", "1002:7550@0000:73:00.0@0000:74:00.0"})
    {
        const std::string what = std::string("'") + bad + "' is refused as a GPU id";
        expect(!parseGpuId(bad), what.c_str());
    }

    const auto two = parseGpuList("10de:2184, 1002:7550@0000:73:00.0");
    expect(two.size() == 2 && two[0].id && two[0].id->vendor == 0x10de && two[1].id && two[1].id->pci,
           "a comma list names GPU 2 then GPU 3, spaces around entries ignored");
    const auto holed = parseGpuList("nvidia,1002:7550");
    expect(holed.size() == 2 && !holed[0].id && holed[0].text == "nvidia" && holed[1].id,
           "a malformed entry keeps its place, so the GPUs after it keep their numbers");
    expect(parseGpuList("").empty() && parseGpuList("   ").empty(), "an empty list names no GPU");
    const auto trailing = parseGpuList("10de:2184,");
    expect(trailing.size() == 2 && !trailing[1].id, "a trailing comma is an empty, malformed entry");

    expect(effectiveGpus({0, 0, 2, 0, 0}) == std::vector<uint32_t>{1, 1, 2, 2, 2}, "a row's GPU applies to it and the rows after it");
    expect(effectiveGpus({2, 0, 1, 0}) == std::vector<uint32_t>{2, 2, 1, 1}, "a later row can name the game's GPU again");
    expect(effectiveGpus({}).empty(), "no rows, no GPUs");

    std::string problem;
    expect(rowGpuNumber(0, problem) == 0 && problem.empty(), "an unset row number inherits");
    expect(rowGpuNumber(64, problem) == 64 && problem.empty(), "GPU 64 is the highest number taken");
    expect(rowGpuNumber(65, problem) == gameGpu && !problem.empty(), "a number past 64 is refused to the game's GPU");
    problem.clear();
    expect(rowGpuNumber(-2, problem) == gameGpu && !problem.empty(), "a negative number is refused to the game's GPU");

    const auto list = parseGpuList("10de:2184");
    expect(gpuAssignmentProblems({"cas", "smaa"}, {0, 2}, list).empty(), "a row on a listed GPU is no problem");
    const auto unlisted = gpuAssignmentProblems({"cas", "smaa", "lut"}, {0, 3, 0}, list);
    expect(unlisted.size() == 1 && mentions(unlisted, "smaa runs on GPU 3") && mentions(unlisted, "only GPU 2"),
           "a row past the list is reported once, naming the first row on that GPU");
    const auto none = gpuAssignmentProblems({"cas"}, {2}, {});
    expect(none.size() == 1 && mentions(none, "names no GPU"), "a row on GPU 2 with no gpus list is reported");
    const auto malformed = gpuAssignmentProblems({"cas"}, {0}, parseGpuList("nvidia"));
    expect(malformed.size() == 1 && mentions(malformed, "'nvidia' for GPU 2"), "a malformed entry is reported with its text and number");
    expect(gpuAssignmentProblems({"cas"}, {0}, {}).empty(), "every row on the game's GPU with no list is no problem");

    expect(assignsAnotherGpu({"cas", "smaa"}, {0, 2}, list), "a row on a listed GPU asks for sharing with another GPU");
    expect(!assignsAnotherGpu({"cas", "smaa"}, {0, 0}, list), "a list with every row on the game's GPU asks for nothing");
    expect(!assignsAnotherGpu({"cas", "smaa"}, {0, 1}, list), "a row naming GPU 1 asks for nothing");
    expect(!assignsAnotherGpu({"cas"}, {3}, list), "a row past the list asks for nothing, as every effect stays on the game's GPU");
    expect(!assignsAnotherGpu({"cas"}, {2}, parseGpuList("10de:2184,nvidia")),
           "a malformed entry anywhere in the list asks for nothing, as every effect stays on the game's GPU");

    expect(formatPciAddress({0, 0x73, 0x1f, 7}) == "0000:73:1f.7", "a PCI address is written as lspci and sysfs write it");

    const std::vector<GpuCandidate> machine = {{0x1002, 0x7550, PciAddress{0, 3, 0, 0}},
                                               {0x10de, 0x21c4, PciAddress{0, 5, 0, 0}},
                                               {0x1002, 0x13c0, PciAddress{0, 0x7c, 0, 0}}};
    const auto                      nvidia  = matchGpu(*parseGpuId("10de:21c4"), machine, 0);
    expect(nvidia.kind == GpuMatchKind::Found && nvidia.index == 1, "a listed GPU is found in the instance by vendor:device");
    expect(matchGpu(*parseGpuId("ffff:ffff"), machine, 0).kind == GpuMatchKind::NotFound, "a GPU the instance lacks is not found");
    expect(matchGpu(*parseGpuId("1002:7550"), machine, 0).kind == GpuMatchKind::IsTheGame, "the game's own GPU is refused as GPU 2");
    expect(matchGpu(*parseGpuId("10de:21c4@0000:06:00.0"), machine, 0).kind == GpuMatchKind::NotFound,
           "a PCI address that names no card is not found, even when vendor:device would match");
    const std::vector<GpuCandidate> twins = {{0x1002, 0x7550, PciAddress{0, 3, 0, 0}}, {0x1002, 0x7550, PciAddress{0, 0x73, 0, 0}}};
    expect(matchGpu(*parseGpuId("1002:7550"), twins, 0).kind == GpuMatchKind::Ambiguous,
           "identical cards named without a PCI address are ambiguous, the game's own included");
    const auto second = matchGpu(*parseGpuId("1002:7550@0000:73:00.0"), twins, 0);
    expect(second.kind == GpuMatchKind::Found && second.index == 1, "the PCI address picks one of two identical cards");
    const std::vector<GpuCandidate> oneCardTwoDrivers = {{0x1002, 0x7550, PciAddress{0, 3, 0, 0}}, {0x1002, 0x7550, PciAddress{0, 3, 0, 0}}};
    expect(matchGpu(*parseGpuId("1002:7550@0000:03:00.0"), oneCardTwoDrivers, 0).kind == GpuMatchKind::ListedTwice,
           "a card named by its PCI address that two drivers list is told apart from identical cards");
    const std::vector<GpuCandidate> noPci = {{0x1002, 0x7550, std::nullopt}, {0x10de, 0x21c4, std::nullopt}};
    expect(matchGpu(*parseGpuId("10de:21c4@0000:05:00.0"), noPci, 0).kind == GpuMatchKind::NotFound,
           "a PCI address cannot match a card that reports none");

    expect(otherGpusUsed({0, 2, 0, 1, 3}) == std::vector<uint32_t>{2, 3}, "the other GPUs a chain uses are listed once each, in order");
    expect(otherGpusUsed({0, 1}).empty(), "a chain on the game's GPU uses no other GPU");

    const std::vector<std::pair<std::string, std::string>> names = {
        {"NVIDIA GeForce GTX 1660 SUPER", "GTX 1660 SUPER"},
        {"AMD Radeon RX 9070 XT (RADV GFX1201)", "RX 9070 XT"},
        {"AMD Ryzen 9 9950X3D 16-Core Processor (RADV RAPHAEL_MENDOCINO)", "Ryzen 9 9950X3D graphics"},
        {"Apple M1 Max (G13C C0)", "M1 Max"},
        {"Intel(R) Arc(tm) A770 Graphics (DG2)", "Arc A770 Graphics"},
        {"AMD Radeon Graphics (RADV RENOIR)", "Radeon Graphics"},
        {"llvmpipe (LLVM 21.1.8, 256 bits)", "llvmpipe"},
        {"NVIDIA RTX A2000", "RTX A2000"},
        {"Some Future GPU", "Some Future GPU"},
        {"NVIDIA", "NVIDIA"},
    };
    for (const auto& [device, model] : names)
    {
        const std::string got = shortGpuName(device);
        if (got != model)
            std::printf("  %s -> %s, want %s\n", device.c_str(), got.c_str(), model.c_str());
        expect(got == model, "a GPU is named by its model, without vendor, driver or codename");
    }

    expect(ownGpuAfterRemove(2, 2) == gameGpu && ownGpuAfterRemove(3, 2) == 2 && ownGpuAfterRemove(0, 2) == 0 && ownGpuAfterRemove(1, 2) == 1,
           "removing GPU 2 puts its rows on GPU 1, numbers the GPUs above it down one and leaves the rest");
    expect(ownGpuAfterSwap(2, 2, 3) == 3 && ownGpuAfterSwap(3, 2, 3) == 2 && ownGpuAfterSwap(0, 2, 3) == 0 && ownGpuAfterSwap(4, 2, 3) == 4,
           "swapping GPUs 2 and 3 swaps their rows' numbers and leaves the rest");

    expect(gpusOfRows({"a", "b", "c"}, {0, 2, 0}, {"a", "c"}) == std::vector<uint32_t>{1, 2},
           "a row after a switched-off row that says GPU 2 still runs on GPU 2, as the row tags show");
    expect(gpusOfRows({"a", "b"}, {0, 3}, {"b", "missing"}) == std::vector<uint32_t>{3, 1}, "a chosen row not in the list runs on GPU 1");
    expect(otherGpusAmong({1, 2, 2, 3, 1}) == std::vector<uint32_t>{2, 3}, "the other GPUs a chain uses are listed once each, in order");
    expect(gpusInUseLine(1, 3) == "1 of 3 in use" && gpusInUseLine(1, 1) == "1 GPU", "the GPUs tile counts the GPUs a chain runs on, out of every GPU");

    {
        GpuStatusFacts made;
        made.rowsAssigned = true;
        expect(gpuStatus(made) != "in use", "a GPU whose device exists but runs no chain is not shown in use");
        made.runsChain = true;
        expect(gpuStatus(made) == "in use", "a GPU a chain runs on is shown in use");
        GpuStatusFacts refused;
        refused.rowsAssigned = true;
        refused.refusal      = "NVIDIA needs modeset";
        expect(gpuStatus(refused).find("NVIDIA needs modeset") != std::string::npos, "a GPU whose rows were refused shows why");
        GpuStatusFacts listedLate;
        listedLate.rowsAssigned = true;
        expect(gpuStatus(listedLate).find("next launch") == std::string::npos, "a GPU the game's device shares with is not put off to the next launch");
        listedLate.sharingEnabled = false;
        expect(gpuStatus(listedLate).find("next launch") != std::string::npos, "a GPU listed after the game's device was made says it is used from the next launch");
        GpuStatusFacts waiting;
        waiting.rowsAssigned = true;
        expect(gpuStatus(waiting) != "starting", "a GPU no device is being made for is not shown starting");
        waiting.beingMade = true;
        expect(gpuStatus(waiting) == "starting", "a GPU whose device is being made says it is starting");
        GpuStatusFacts unused;
        unused.runsChain = true;
        expect(gpuStatus(unused).find("no effect runs on it") == 0 && gpuStatus(unused).find("GPU menu") != std::string::npos,
               "a GPU no effect is on says so, and where to put one on it");
    }

    if (failures)
        std::printf("%d gpu assignment check(s) failed\n", failures);
    expect(tripLabel({2}) == "Round trip to GPU 2", "one other GPU names its trip");
    expect(tripLabel({2, 3}) == "Round trip through GPUs 2 and 3", "two other GPUs are both named");
    expect(tripLabel({3, 2, 4}) == "Round trip through GPUs 3, 2 and 4", "three other GPUs keep the chain's order");
    expect(tripLabel({}).empty(), "no other GPU has no trip");


    return failures ? 1 : 0;
}
