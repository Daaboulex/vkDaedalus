#include "overlay/ui_map.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "logger.hpp"

namespace
{
    struct MappedItem
    {
        std::string          window;
        std::string          label;
        ImRect               box;
        ImRect               windowBox;
        bool                 disabled = false;
        ImGuiItemStatusFlags status   = 0;
        bool                 placed   = false;
    };

    std::string                          mapPath;
    std::vector<MappedItem>              items;
    std::unordered_map<ImGuiID, size_t>  itemIndex;
    uint64_t                             frame = 0;
    const char*                          pendingName = nullptr;

    std::string oneLine(std::string text)
    {
        for (char& c : text)
            if (c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        return text;
    }
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* itemData)
{
    const auto [at, added] = itemIndex.emplace(id, items.size());
    if (added)
        items.emplace_back();
    MappedItem& item = items[at->second];
    item.window      = ctx->CurrentWindow ? ctx->CurrentWindow->Name : "";
    item.windowBox   = ctx->CurrentWindow ? ctx->CurrentWindow->Rect() : ImRect();
    item.box         = bb;
    item.disabled    = itemData != nullptr && (itemData->ItemFlags & ImGuiItemFlags_Disabled) != 0;
    item.placed      = true;
    if (pendingName != nullptr)
    {
        item.label  = pendingName;
        pendingName = nullptr;
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
    const auto [at, added] = itemIndex.emplace(id, items.size());
    if (added)
        items.emplace_back();
    if (label != nullptr && *label != '\0')
        items[at->second].label = label;
    items[at->second].status = flags;
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
    return nullptr;
}

namespace vkBasalt
{
    namespace UiMap
    {
        void enableIfAsked(ImGuiContext* context)
        {
            const char* path = std::getenv("VKDAEDALUS_UI_MAP");
            if (path == nullptr || *path == '\0' || context == nullptr)
                return;
            mapPath                      = path;
            context->TestEngineHookItems = true;
            Logger::info(std::string("writing the overlay's item map to ") + path + " for UI tests");
        }

        void beginFrame()
        {
            items.clear();
            itemIndex.clear();
            pendingName = nullptr;
        }

        void nameNextItem(const char* label)
        {
            pendingName = label;
        }

        void markChecked(unsigned int id, bool checked)
        {
            const auto found = itemIndex.find(id);
            if (found == itemIndex.end())
                return;
            ImGuiItemStatusFlags& status = items[found->second].status;
            status = (status | ImGuiItemStatusFlags_Checkable) & ~ImGuiItemStatusFlags_Checked;
            if (checked)
                status |= ImGuiItemStatusFlags_Checked;
        }

        void endFrame()
        {
            if (mapPath.empty())
                return;
            const std::string partial = mapPath + ".partial";
            {
                std::ofstream out(partial, std::ios::trunc);
                if (!out)
                {
                    Logger::err("cannot write the overlay's item map to " + partial);
                    return;
                }
                const ImGuiIO& io = ImGui::GetIO();
                out << "frame " << ++frame << " mouse " << io.MousePos.x << ' ' << io.MousePos.y << ' ' << io.MouseDown[0] << ' ' << io.MouseDown[1] << ' ' << io.MouseDown[2]
                    << "\n";
                for (const MappedItem& item : items)
                {
                    const bool pastTheEdge = item.box.Min.x >= item.windowBox.Max.x || item.box.Max.x <= item.windowBox.Min.x;
                    if (!item.placed || (item.label.empty() && !pastTheEdge))
                        continue;
                    out << oneLine(item.window) << '\t' << (item.label.empty() ? std::string("(an item past the window edge)") : oneLine(item.label)) << '\t' << item.box.Min.x << '\t' << item.box.Min.y << '\t'
                        << item.box.Max.x << '\t' << item.box.Max.y << '\t' << (item.disabled ? 1 : 0) << '\t'
                        << ((item.status & ImGuiItemStatusFlags_Checked) ? 1 : 0) << '\t' << ((item.status & ImGuiItemStatusFlags_Opened) ? 1 : 0)
                        << "\n";
                }
                for (ImGuiWindow* window : GImGui->Windows)
                {
                    if (!window->Active || (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)))
                        continue;
                    if (window->HasCloseButton && !(window->Flags & ImGuiWindowFlags_NoTitleBar))
                    {
                        const ImRect bar = window->TitleBarRect();
                        out << oneLine(window->Name) << "\t(title bar)\t" << bar.Min.x << '\t' << bar.Min.y << '\t' << bar.Max.x << '\t' << bar.Max.y
                            << "\t0\t0\t0\n";
                    }
                    out << oneLine(window->Name) << "\t(scroll range)\t0\t0\t" << window->ScrollMax.x << '\t' << window->ScrollMax.y << "\t0\t0\t0\n";
                }
                if (!out)
                {
                    Logger::err("writing the overlay's item map to " + partial + " failed");
                    return;
                }
            }
            if (std::rename(partial.c_str(), mapPath.c_str()) != 0)
                Logger::err("cannot move the overlay's item map into place at " + mapPath);
        }
    }
}
