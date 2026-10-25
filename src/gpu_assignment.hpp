#ifndef GPU_ASSIGNMENT_HPP_INCLUDED
#define GPU_ASSIGNMENT_HPP_INCLUDED

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace vkBasalt
{
    inline constexpr uint32_t gameGpu          = 1;
    inline constexpr uint32_t highestGpuNumber = 64;
    inline constexpr uint32_t firstListedGpu   = 2;

    struct PciAddress
    {
        uint32_t domain   = 0;
        uint32_t bus      = 0;
        uint32_t device   = 0;
        uint32_t function = 0;

        bool operator==(const PciAddress&) const = default;
    };

    struct GpuId
    {
        uint32_t                  vendor = 0;
        uint32_t                  device = 0;
        std::optional<PciAddress> pci;

        bool operator==(const GpuId&) const = default;
    };

    struct GpuListEntry
    {
        std::string          text;
        std::optional<GpuId> id;

        bool operator==(const GpuListEntry&) const = default;
    };

    inline std::optional<uint32_t> parseHexField(std::string_view text, size_t fewestDigits, size_t mostDigits)
    {
        if (text.size() < fewestDigits || text.size() > mostDigits)
            return std::nullopt;
        uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (error != std::errc() || end != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    inline std::optional<PciAddress> parsePciAddress(std::string_view text)
    {
        if (text.size() != 12 || text[4] != ':' || text[7] != ':' || text[10] != '.')
            return std::nullopt;
        const auto domain   = parseHexField(text.substr(0, 4), 4, 4);
        const auto bus      = parseHexField(text.substr(5, 2), 2, 2);
        const auto device   = parseHexField(text.substr(8, 2), 2, 2);
        const auto function = parseHexField(text.substr(11, 1), 1, 1);
        if (!domain || !bus || !device || !function || *device > 0x1f || *function > 7)
            return std::nullopt;
        return PciAddress{*domain, *bus, *device, *function};
    }

    inline std::optional<GpuId> parseGpuId(std::string_view text)
    {
        std::optional<PciAddress> pci;
        if (const size_t at = text.find('@'); at != std::string_view::npos)
        {
            pci = parsePciAddress(text.substr(at + 1));
            if (!pci)
                return std::nullopt;
            text = text.substr(0, at);
        }
        const size_t colon = text.find(':');
        if (colon == std::string_view::npos)
            return std::nullopt;
        const auto vendor = parseHexField(text.substr(0, colon), 1, 8);
        const auto device = parseHexField(text.substr(colon + 1), 1, 8);
        if (!vendor || !device)
            return std::nullopt;
        return GpuId{*vendor, *device, pci};
    }

    inline std::string formatPciAddress(const PciAddress& pci)
    {
        char      text[32];
        const int length = std::snprintf(text, sizeof(text), "%04x:%02x:%02x.%x", pci.domain, pci.bus, pci.device, pci.function);
        return std::string(text, length);
    }

    inline std::string formatGpuId(const GpuId& id)
    {
        char      text[24];
        const int length = std::snprintf(text, sizeof(text), "%04x:%04x", id.vendor, id.device);
        return std::string(text, length) + (id.pci ? "@" + formatPciAddress(*id.pci) : std::string());
    }

    inline std::vector<GpuListEntry> parseGpuList(std::string_view list)
    {
        std::vector<GpuListEntry> entries;
        const auto                trimmed = [](std::string_view s) {
            const size_t first = s.find_first_not_of(" \t");
            if (first == std::string_view::npos)
                return std::string_view();
            return s.substr(first, s.find_last_not_of(" \t") - first + 1);
        };
        if (trimmed(list).empty())
            return entries;
        while (true)
        {
            const size_t           comma = list.find(',');
            const std::string_view entry = trimmed(list.substr(0, comma));
            entries.push_back({std::string(entry), parseGpuId(entry)});
            if (comma == std::string_view::npos)
                break;
            list = list.substr(comma + 1);
        }
        return entries;
    }

    inline uint32_t rowGpuNumber(int32_t asked, std::string& problem)
    {
        if (asked >= 0 && static_cast<uint32_t>(asked) <= highestGpuNumber)
            return static_cast<uint32_t>(asked);
        problem = "GPU " + std::to_string(asked) + " is outside 1 to " + std::to_string(highestGpuNumber);
        return gameGpu;
    }

    inline std::vector<uint32_t> effectiveGpus(const std::vector<uint32_t>& ownNumbers)
    {
        std::vector<uint32_t> effective;
        effective.reserve(ownNumbers.size());
        uint32_t current = gameGpu;
        for (const uint32_t own : ownNumbers)
        {
            if (own != 0)
                current = own;
            effective.push_back(current);
        }
        return effective;
    }

    inline std::vector<std::string> gpuAssignmentProblems(const std::vector<std::string>&  rowNames,
                                                          const std::vector<uint32_t>&     ownNumbers,
                                                          const std::vector<GpuListEntry>& list)
    {
        std::vector<std::string> problems;
        for (size_t k = 0; k < list.size(); k++)
        {
            if (!list[k].id)
                problems.push_back("the gpus entry '" + list[k].text + "' for GPU " + std::to_string(k + firstListedGpu) +
                                   " is not vendor:device or vendor:device@dddd:bb:dd.f in hex");
        }
        const std::vector<uint32_t> effective = effectiveGpus(ownNumbers);
        std::set<uint32_t>          reported;
        for (size_t row = 0; row < rowNames.size() && row < effective.size(); row++)
        {
            const uint32_t gpu = effective[row];
            if (gpu == gameGpu || gpu - firstListedGpu < list.size() || !reported.insert(gpu).second)
                continue;
            const std::string named = list.empty()      ? std::string("no GPU")
                                      : list.size() == 1 ? std::string("only GPU 2")
                                                         : "GPUs 2 to " + std::to_string(list.size() + 1);
            problems.push_back(rowNames[row] + " runs on GPU " + std::to_string(gpu) + ", but the gpus list names " + named);
        }
        return problems;
    }

    struct GpuCandidate
    {
        uint32_t                  vendor = 0;
        uint32_t                  device = 0;
        std::optional<PciAddress> pci;
    };

    enum class GpuMatchKind
    {
        Found,
        NotFound,
        Ambiguous,
        ListedTwice,
        IsTheGame
    };

    struct GpuMatch
    {
        GpuMatchKind kind  = GpuMatchKind::NotFound;
        size_t       index = 0;
    };

    inline GpuMatch matchGpu(const GpuId& id, const std::vector<GpuCandidate>& candidates, size_t gameIndex)
    {
        std::vector<size_t> hits;
        for (size_t i = 0; i < candidates.size(); i++)
        {
            const GpuCandidate& c = candidates[i];
            if (c.vendor == id.vendor && c.device == id.device && (!id.pci || (c.pci && *c.pci == *id.pci)))
                hits.push_back(i);
        }
        if (hits.empty())
            return {GpuMatchKind::NotFound, 0};
        if (hits.size() > 1)
            return {id.pci ? GpuMatchKind::ListedTwice : GpuMatchKind::Ambiguous, 0};
        if (hits.front() == gameIndex)
            return {GpuMatchKind::IsTheGame, hits.front()};
        return {GpuMatchKind::Found, hits.front()};
    }

    inline std::vector<uint32_t> gpusOfRows(const std::vector<std::string>& rows, const std::vector<uint32_t>& ownNumbers,
                                            const std::vector<std::string>& chosen)
    {
        const std::vector<uint32_t> effective = effectiveGpus(ownNumbers);
        std::vector<uint32_t>       gpus;
        gpus.reserve(chosen.size());
        for (const std::string& name : chosen)
        {
            const auto at = std::find(rows.begin(), rows.end(), name);
            gpus.push_back(at == rows.end() ? gameGpu : effective[static_cast<size_t>(at - rows.begin())]);
        }
        return gpus;
    }

    inline std::vector<uint32_t> otherGpusAmong(const std::vector<uint32_t>& gpus)
    {
        std::vector<uint32_t> used;
        for (const uint32_t gpu : gpus)
            if (gpu != gameGpu && std::find(used.begin(), used.end(), gpu) == used.end())
                used.push_back(gpu);
        return used;
    }

    struct GpuStatusFacts
    {
        std::optional<std::string> notMade;
        bool                       rowsAssigned   = false;
        bool                       runsChain      = false;
        bool                       sharingEnabled = true;
        bool                       backOnGameGpu  = false;
        bool                       beingMade      = false;
        std::string                refusal;
    };

    inline std::string gpusInUseLine(size_t inUse, size_t total)
    {
        if (total <= 1)
            return "1 GPU";
        return std::to_string(inUse) + " of " + std::to_string(total) + " in use";
    }

    inline std::string gpuStatus(const GpuStatusFacts& facts)
    {
        if (facts.notMade)
            return "could not start: " + *facts.notMade;
        if (!facts.rowsAssigned)
            return "no effect runs on it: pick it in an effect's GPU menu on the Effects card";
        if (facts.runsChain)
            return "in use";
        if (!facts.sharingEnabled)
            return "used from the next launch";
        if (facts.backOnGameGpu)
            return "its effects run on GPU 1 until a rebuild, after a failed hand-off or wait";
        if (facts.beingMade)
            return "starting";
        if (!facts.refusal.empty())
            return "its effects run on GPU 1: " + facts.refusal;
        return "its effects run on GPU 1, and the Effects card says why";
    }

    inline std::vector<uint32_t> otherGpusUsed(const std::vector<uint32_t>& ownNumbers)
    {
        std::vector<uint32_t> used;
        for (const uint32_t gpu : effectiveGpus(ownNumbers))
            if (gpu != gameGpu && std::find(used.begin(), used.end(), gpu) == used.end())
                used.push_back(gpu);
        return used;
    }

    inline std::string shortGpuName(std::string_view deviceName)
    {
        std::string name(deviceName);
        if (const size_t open = name.rfind(" ("); open != std::string::npos && name.back() == ')')
            name.erase(open);
        for (const std::string_view mark : {"(R)", "(r)", "(TM)", "(tm)"})
            for (size_t at = name.find(mark); at != std::string::npos; at = name.find(mark))
                name.erase(at, mark.size());
        const auto dropLeading = [&](std::string_view word) {
            if (name.size() > word.size() && std::string_view(name).substr(0, word.size()) == word)
                name.erase(0, word.size());
        };
        for (const std::string_view vendor : {"NVIDIA ", "AMD ", "Intel ", "Apple "})
            dropLeading(vendor);
        dropLeading("GeForce ");
        if (name != "Radeon Graphics")
            dropLeading("Radeon ");
        if (const size_t processor = name.find(" Processor"); processor != std::string::npos)
        {
            name.erase(processor);
            if (const size_t last = name.rfind(' '); last != std::string::npos && name.find("-Core", last) != std::string::npos)
                name.erase(last);
            name += " graphics";
        }
        std::string tidy;
        for (const char c : name)
            if (c != ' ' || (!tidy.empty() && tidy.back() != ' '))
                tidy += c;
        while (!tidy.empty() && tidy.back() == ' ')
            tidy.pop_back();
        return tidy.empty() ? std::string(deviceName) : tidy;
    }

    inline bool assignsAnotherGpu(const std::vector<std::string>& rowNames, const std::vector<uint32_t>& ownNumbers,
                                  const std::vector<GpuListEntry>& list)
    {
        if (!gpuAssignmentProblems(rowNames, ownNumbers, list).empty())
            return false;
        for (const uint32_t gpu : effectiveGpus(ownNumbers))
            if (gpu != gameGpu)
                return true;
        return false;
    }

    inline uint32_t ownGpuAfterSwap(uint32_t own, uint32_t first, uint32_t second)
    {
        return own == first ? second : own == second ? first : own;
    }

    inline uint32_t ownGpuAfterRemove(uint32_t own, uint32_t removed)
    {
        return own == removed ? gameGpu : own > removed ? own - 1 : own;
    }

    inline std::string tripLabel(const std::vector<uint32_t>& otherGpus)
    {
        if (otherGpus.empty())
            return "";
        if (otherGpus.size() == 1)
            return "Round trip to GPU " + std::to_string(otherGpus[0]);
        std::string named;
        for (size_t i = 0; i < otherGpus.size(); i++)
            named += (i == 0 ? "" : i + 1 == otherGpus.size() ? " and " : ", ") + std::to_string(otherGpus[i]);
        return "Round trip through GPUs " + named;
    }
}

#endif
