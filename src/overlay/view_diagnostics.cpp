#include "imgui_overlay.hpp"
#include "gpu_probe.hpp"
#include "gpu_assignment.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"
#include "ui_map.hpp"
#include "util.hpp"
#include "overlay/widgets.hpp"

#include <chrono>
#include <cstdio>
#include <map>
#include <future>
#include <algorithm>

#include "imgui.h"

namespace vkBasalt
{
    namespace
    {
        constexpr std::chrono::milliseconds sampleInterval{200};

        static std::vector<GpuEntry> gpuInventory;

        std::string gpuKey(const GpuEntry& entry)
        {
            return entry.pciAddress.empty() ? entry.name : entry.pciAddress;
        }

    }

    void ImGuiOverlay::renderGpuReadings()
    {
        static bool initialized = false;
        if (!initialized)
        {
            gpuInventory = buildGpuInventory(pLogicalDevice);
            initialized = true;
        }

        const auto now = std::chrono::steady_clock::now();

        static std::future<std::vector<GpuEntry>> sample;
        if (sample.valid() && sample.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            gpuInventory = sample.get();
        }

        static auto lastGpuSampleTime = std::chrono::steady_clock::now();
        static auto lastUsersSampleTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        static bool usersAsked          = false;
        if (!sample.valid() && now - lastGpuSampleTime >= sampleInterval)
        {
            lastGpuSampleTime = now;
            const bool withUsers = usersAsked && now - lastUsersSampleTime >= std::chrono::seconds(3);
            if (withUsers)
                lastUsersSampleTime = now;
            sample = std::async(std::launch::async, [inventory = gpuInventory, withUsers]() mutable {
                for (GpuEntry& entry : inventory)
                    sampleGpuStats(entry);
                if (withUsers)
                    sampleDrmUsers(inventory);
                return inventory;
            });
        }

        usersAsked                    = false;
        const OverlayPalette& palette = activePalette();
        ui::sectionLabel("Readings");
        if (gpuInventory.empty())
            ui::note("No GPU found");

        for (const GpuEntry& entry : gpuInventory)
        {
            if (entry.software)
                continue;
            const uint32_t entryVendor = entry.vendor == GpuVendor::AMD      ? 0x1002u
                                         : entry.vendor == GpuVendor::NVIDIA ? 0x10deu
                                         : entry.vendor == GpuVendor::Intel  ? 0x8086u
                                                                             : 0u;
            const auto numbered = std::find_if(state.gpus.begin(), state.gpus.end(), [&](const OverlayGpu& gpu) {
                const std::optional<GpuId> id = parseGpuId(gpu.id);
                return entryVendor != 0 && id && id->vendor == entryVendor && id->device == entry.pciDeviceId
                       && (!id->pci || formatPciAddress(*id->pci) == entry.pciAddress);
            });
            const std::string model = entry.vulkanUsable ? shortGpuName(entry.name) : entry.name;

            ImGui::PushID(gpuKey(entry).c_str());
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
            ui::beginPanel();
            ui::panelRow(ui::Size::control);
            ImGui::AlignTextToFramePadding();
            if (numbered != state.gpus.end())
            {
                ui::label(("GPU " + std::to_string(numbered->number)).c_str());
                ImGui::SameLine();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(palette.muted, "%s", model.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                usersAsked = true;
                std::string users;
                for (size_t i = 0; i < entry.users.size(); i++)
                    users += (i ? ", " : "") + entry.users[i];
                ImGui::SetTooltip("%s%s%s%s", entry.name.c_str(), entry.pciAddress.empty() ? "" : ("\n" + entry.pciAddress).c_str(),
                                  entry.note.empty() ? "" : ("\n" + entry.note).c_str(), users.empty() ? "" : ("\nused by " + users).c_str());
            }
            const char* source = entry.vendor == GpuVendor::NVIDIA ? "NVIDIA" : "kernel";
            ui::panelRow(ui::Size::control);
            if (entry.busyPercent < 0 && entry.vramTotalMB <= 0)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(palette.faint, "No readings from this driver");
            }
            else
            {
                const float half = (ImGui::GetContentRegionAvail().x - ui::px(ui::Size::groupGap)) * 0.5f;
                char        busy[16];
                std::snprintf(busy, sizeof(busy), entry.busyPercent >= 0 ? "%.0f%%" : "none", entry.busyPercent);
                ui::meter(("##gpuusage-" + gpuKey(entry)).c_str(), entry.vendor == GpuVendor::Intel ? "Clock" : "Busy", entry.busyPercent / 100.0f, busy, half);
                ImGui::SetItemTooltip(entry.vendor == GpuVendor::Intel ? "GPU clock as a share of its highest, from the %s driver, every %.1f s while this card is open"
                                                                       : "How busy the GPU is, from the %s driver, every %.1f s while this card is open",
                                      source, sampleInterval.count() / 1000.0);
                ImGui::SameLine(0.0f, ui::px(ui::Size::groupGap));
                char vram[32];
                if (entry.vramTotalMB > 0)
                    std::snprintf(vram, sizeof(vram), "%.1f / %.0f GB", entry.vramUsedMB / 1024.0f, entry.vramTotalMB / 1024.0f);
                else
                    std::snprintf(vram, sizeof(vram), "none");
                ui::meter(("##vramusage-" + gpuKey(entry)).c_str(), "VRAM", entry.vramTotalMB > 0 ? entry.vramUsedMB / entry.vramTotalMB : 0.0f, vram, half);
                ImGui::SetItemTooltip("Video memory used, from the %s driver", source);
            }
            if (entry.gttTotalMB > 0)
            {
                ui::panelRow(ui::Size::control);
                char gtt[32];
                std::snprintf(gtt, sizeof(gtt), "%.1f / %.0f GB", entry.gttUsedMB / 1024.0f, entry.gttTotalMB / 1024.0f);
                ui::meter(("##gttusage-" + gpuKey(entry)).c_str(), "In system RAM", entry.gttUsedMB / entry.gttTotalMB, gtt, ImGui::GetContentRegionAvail().x);
                ImGui::SetItemTooltip("GPU memory that lives in system RAM (GTT): buffers shared with other GPUs and the CPU, and anything VRAM could not hold");
            }
            ui::endPanel();
            ImGui::PopID();
        }

    }

    void ImGuiOverlay::renderRoundTrip()
    {
        const OverlayPalette& palette = activePalette();
        if (latency.tripGpus.empty())
            return;
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
        ui::sectionLabel(tripLabel(latency.tripGpus).c_str());
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        if (!latency.effectsOn || !latency.roundTripMilliseconds)
        {
            ui::beginPanel();
            ui::factRow("Round trip", latency.effectsOn ? "waiting" : "effects off", ui::Mark::Absent);
            ImGui::SetItemTooltip(latency.effectsOn ? "The first timing has not come back from the GPU yet." : "Not measured while the effects are off.");
            ui::endPanel();
            return;
        }
        struct Part
        {
            std::string label;
            double      ms;
            ImVec4      colour;
            const char* how;
        };
        std::vector<Part> parts;
        bool              split = !latency.helperCopies.empty();
        double            inside = 0.0;
        for (const uint32_t gpu : latency.tripGpus)
            for (const HelperCopies& copies : latency.helperCopies)
            {
                if (copies.gpu != gpu)
                    continue;
                if (copies.inMs < 0.0f || copies.outMs < 0.0f || copies.effectsMs < 0.0f)
                    split = false;
                const std::string n = std::to_string(gpu);
                parts.push_back({"Upload to GPU " + n, copies.inMs, palette.info, "The frame copied from system memory into this GPU: two timestamps on it."});
                parts.push_back({"Effects on GPU " + n, copies.effectsMs, palette.accent, "This GPU's effects: the timestamps around its rows."});
                parts.push_back({"Download from GPU " + n, copies.outMs, palette.ok, "The frame copied from this GPU back to system memory: two timestamps on it."});
                inside += copies.inMs + copies.effectsMs + copies.outMs;
            }
        const double trip = *latency.roundTripMilliseconds;
        if (split)
            parts.push_back({"Waiting", std::max(0.0, trip - inside), palette.disabled,
                             "The round trip less the parts above: the hand-offs between the GPUs and their queues."});
        if (split)
        {
            std::vector<ui::BarPart> bar;
            for (const Part& part : parts)
                bar.push_back({part.ms, part.colour});
            ui::splitBar(bar);
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        }
        ui::beginPanel();
        const auto cell = [&](const Part& part, float width) {
            char value[32];
            std::snprintf(value, sizeof(value), "%.2f ms", part.ms);
            ui::factCell(part.label.c_str(), value, ui::Mark::Measured, &part.colour, width);
            ImGui::SetItemTooltip("%s", part.how);
        };
        if (split)
            for (size_t i = 0; i < parts.size(); i += 2)
            {
                ui::panelRow(ui::Size::control);
                const float between = ui::px(ui::Size::groupGap);
                const float half    = (ImGui::GetContentRegionAvail().x - between) * 0.5f;
                cell(parts[i], half);
                if (i + 1 < parts.size())
                {
                    ImGui::SameLine(0.0f, between);
                    cell(parts[i + 1], half);
                }
            }
        char total[32];
        std::snprintf(total, sizeof(total), "%.2f ms", trip);
        ui::factRow("Round trip", total, ui::Mark::Sum);
        ImGui::SetItemTooltip("Measured on GPU 1, from the end of its copy out to the start of its copy back.\n"
                              "GPU 1's own two copies are not in it. Each frame waits this long for the other GPUs.%s",
                              split ? "" : "\nTurn on Per-effect GPU timing on the Performance card to split it into its parts.");
        ui::endPanel();
    }

}
